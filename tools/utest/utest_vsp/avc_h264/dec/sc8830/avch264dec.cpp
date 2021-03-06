#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <dlfcn.h>

#include <linux/ion.h>
#include <binder/MemoryHeapIon.h>
#include "ion_sprd.h"
using namespace android;


//#include "h264dec.h"
#include "avc_dec_api.h"


#include "util.h"



#define ERR(x...)	fprintf(stderr, ##x)
#define INFO(x...)	fprintf(stdout, ##x)




#define ONEFRAME_BITSTREAM_BFR_SIZE	(1024*1024*2)  //for bitstream size of one encoded frame.
#define DEC_YUV_BUFFER_NUM  17


#define	STARTCODE_H263_PSC	0x00008000
#define STARTCODE_MP4V_VO	0x000001b0
#define STARTCODE_MP4V_VOP	0x000001b6
#define	STARTCODE_MJPG_SOI	0xffd80000
#define	STARTCODE_FLV1_PSC	0x00008400
#define STARTCODE_H264_NAL1	0x00000100
#define STARTCODE_H264_NAL2	0x00000101


static const unsigned int table_startcode1[] =
{
    STARTCODE_H263_PSC,
    STARTCODE_MP4V_VO,
    STARTCODE_MJPG_SOI,
    STARTCODE_FLV1_PSC,
    STARTCODE_H264_NAL1
};

static const unsigned int table_maskcode1[] =
{
    0x000073ff,
    0x00000005,
    0x0000ffff,
    0x000073ff,
    0x0000007f
};

static const unsigned int table_startcode2[] =
{
    STARTCODE_H263_PSC,
    STARTCODE_MP4V_VOP,
    STARTCODE_MJPG_SOI,
    STARTCODE_FLV1_PSC,
    STARTCODE_H264_NAL2
};

static const unsigned int table_maskcode2[] =
{
    0x000073ff,
    0x00000005,
    0x0000ffff,
    0x000073ff,
    0x00000074
};





static void usage()
{
    INFO("usage:\n");
    INFO("utest_vsp_dec -i filename_bitstream -o filename_yuv [OPTIONS]\n");
    INFO("-i       string: input bitstream filename\n");
    INFO("[OPTIONS]:\n");
    INFO("-o       string: output yuv filename\n");
    INFO("-format integer: video format(0:H263 / 1:MPEG4 / 2:MJPG / 3:FLVH263 / 4:H264), auto detection if default\n");
    INFO("-frames integer: number of frames to decode, default is 0 means all frames\n");
    INFO("-help          : show this help message\n");
    INFO("Built on %s %s, Written by JamesXiong(james.xiong@spreadtrum.com)\n", __DATE__, __TIME__);
}

void* mLibHandle;
FT_H264DecGetNALType mH264DecGetNALType;
//    FT_H264GetBufferDimensions mH264GetBufferDimensions;
FT_H264DecGetInfo mH264DecGetInfo;
FT_H264DecInit mH264DecInit;
FT_H264DecDecode mH264DecDecode;
FT_H264_DecReleaseDispBfr mH264_DecReleaseDispBfr;
FT_H264DecRelease mH264DecRelease;
FT_H264Dec_SetCurRecPic  mH264Dec_SetCurRecPic;
FT_H264Dec_GetLastDspFrm  mH264Dec_GetLastDspFrm;
FT_H264Dec_ReleaseRefBuffers  mH264Dec_ReleaseRefBuffers;
FT_H264DecMemInit mH264DecMemInit;
static bool mIOMMUEnabled = false;

static int dec_init(AVCHandle *mHandle, int format, unsigned char* pheader_buffer, unsigned int header_size,
                    unsigned char* pbuf_inter, unsigned char* pbuf_inter_phy, unsigned int size_inter/*, MMCodecBuffer extra_mem[]*/)
{
    MMCodecBuffer InterMemBfr;
    MMDecVideoFormat video_format;
    int ret;
    INFO("dec_init IN\n");

    InterMemBfr.common_buffer_ptr = pbuf_inter;
    InterMemBfr.common_buffer_ptr_phy = pbuf_inter_phy;
    InterMemBfr.size = size_inter;

    video_format.video_std = format;
    video_format.i_extra = header_size;
    video_format.p_extra = pheader_buffer;
    video_format.frame_width = 0;
    video_format.frame_height = 0;
    video_format.uv_interleaved = 1;
    video_format.video_std = H264;
    ret = (*mH264DecInit)(mHandle, &InterMemBfr,&video_format);
//	if (ret == 0)
//	{
//		H264DecMemInit(extra_mem);
//	}
    INFO("dec_init OUT\n");

    return ret;
}

static int dec_decode_frame(AVCHandle *mHandle, unsigned char* pframe, unsigned char* pframe_y, unsigned int size, MMDecOutput *dec_out) {
    MMDecInput dec_in;
    int ret;

    dec_in.pStream  = pframe;
    dec_in.pStream_phy= (uint32)pframe_y;
    dec_in.dataLen = size;
    dec_in.beLastFrm = 0;
    dec_in.expected_IVOP = 0;
    dec_in.beDisplayed = 1;
    dec_in.err_pkt_num = 0;

    dec_out->frameEffective = 0;

    ret = (*mH264DecDecode)(mHandle, &dec_in, dec_out);

    return ret;
}


static void dec_release(AVCHandle *mHandle)
{
    (*mH264Dec_ReleaseRefBuffers)(mHandle);

    (*mH264DecRelease)(mHandle);
}



static int sniff_format(unsigned char* pbuffer, unsigned int size, unsigned int startcode, unsigned int maskcode)
{
    int confidence = 0;

    unsigned int code = (pbuffer[0] << 24) | (pbuffer[1] << 16) | (pbuffer[2] << 8) | pbuffer[3];
    if ((code & (~maskcode)) == (startcode & (~maskcode)))
    {
        confidence = size;
    }

    for (unsigned int i=1; i<=size-4; i++)
    {
        unsigned int code = (pbuffer[i] << 24) | (pbuffer[i+1] << 16) | (pbuffer[i+2] << 8) | pbuffer[i+3];
        if ((code & (~maskcode)) == (startcode & (~maskcode)))
        {
            confidence ++;
        }
    }

    return confidence;
}

static int detect_format(unsigned char* pbuffer, unsigned int size)
{
    int format = 0;

    int confidence[5];
    int i;
    for (i=0; i<5; i++)
    {
        confidence[i] = sniff_format(pbuffer, size, table_startcode1[i], table_maskcode1[i]);
    }

    int max_confidence = 0;
    for (i=0; i<5; i++)
    {
        if (max_confidence < confidence[i])
        {
            max_confidence = confidence[i];
            format = i;
        }
    }

    return format;
}





static const char* format2str(int format)
{
    if (format == 0)
    {
        return "H263";
    }
    else if (format == 1)
    {
        return "MPEG4";
    }
    else if (format == 2)
    {
        return "MJPG";
    }
    else if (format == 3)
    {
        return "FLVH263";
    }
    else if (format == 4)
    {
        return "H264";
    }
    else
    {
        return "UnKnown";
    }
}

static const char* type2str(int type)
{
    if (type == 2)
    {
        return "I";
    }
    else if (type == 0)
    {
        return "P";
    }
    else if (type == 1)
    {
        return "B";
    }
    else
    {
        return "N";
    }
}

uint8_t *mCodecExtraBuffer;
static int VSP_malloc_cb(void* mHandle, unsigned int size_extra) {

    MMCodecBuffer extra_mem[MAX_MEM_TYPE];
    unsigned char*  mPbuf_extra_v;
    uint32  mPbuf_extra_p;

    sp<MemoryHeapIon> mPmem_extra;
    //INFO("width = %d, height = %d. \n", width, height);

    if (mIOMMUEnabled) {
        mPmem_extra = new MemoryHeapIon(SPRD_ION_WITH_MMU, size_extra, MemoryHeapBase::NO_CACHING, ION_HEAP_ID_MASK_SYSTEM);
    } else {
        mPmem_extra = new MemoryHeapIon(SPRD_ION_NO_MMU, size_extra, MemoryHeapBase::NO_CACHING, ION_HEAP_ID_MASK_MM);
    }
    int fd = mPmem_extra->getHeapID();
    //INFO("VSP_malloc_cb... \n");
    if(fd >= 0)
    {
        int ret,phy_addr, buffer_size;
        if (mIOMMUEnabled) {
            ret = mPmem_extra->get_mm_iova(&phy_addr, &buffer_size);
        } else {
            ret = mPmem_extra->get_phy_addr_from_ion(&phy_addr, &buffer_size);
        }
        if(ret < 0)
        {
            INFO ("mPmem_extra: get_phy_addr_from_ion fail %d",ret);
            return -1;
        }

        mPbuf_extra_p =(uint32)phy_addr;
        mPbuf_extra_v = (uint8 *)mPmem_extra->base();
        //INFO("mPbuf_extra_p = %08x, mPbuf_extra_v = %08x, buffer_size= %08x\n", mPbuf_extra_p, mPbuf_extra_v, buffer_size);
        extra_mem[HW_NO_CACHABLE].common_buffer_ptr =(uint8 *) mPbuf_extra_v;
        extra_mem[HW_NO_CACHABLE].common_buffer_ptr_phy = (void *)mPbuf_extra_p;
        extra_mem[HW_NO_CACHABLE].size = size_extra;
    } else
    {
        return -1;
    }

    (*mH264DecMemInit)((AVCHandle *)mHandle, extra_mem);
    return 0;
}

static int VSP_bind_unbind_NULL (void *userdata,void *pHeader)
{
    return 0;
}

static int ActivateSPS(void* aUserData, uint width,uint height, uint aNumBuffers)
{
    return 1;
}

bool openDecoder(const char* libName)
{
    if(mLibHandle) {
        dlclose(mLibHandle);
    }

    INFO("openDecoder, lib: %s\n",libName);

    mLibHandle = dlopen(libName, RTLD_NOW);
    if(mLibHandle == NULL) {
        ERR("openDecoder, can't open lib: %s",libName);
        return false;
    }

    mH264DecGetNALType = (FT_H264DecGetNALType)dlsym(mLibHandle, "H264DecGetNALType");
    if(mH264DecGetNALType == NULL) {
        ERR("Can't find H264DecGetNALType in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

//    mH264GetBufferDimensions = (FT_H264GetBufferDimensions)dlsym(mLibHandle, "H264GetBufferDimensions");
//    if(mH264GetBufferDimensions == NULL){
//        ALOGE("Can't find H264GetBufferDimensions in %s",libName);
//        dlclose(mLibHandle);
//        mLibHandle = NULL;
//        return false;
//    }

    mH264DecGetInfo = (FT_H264DecGetInfo)dlsym(mLibHandle, "H264DecGetInfo");
    if(mH264DecGetInfo == NULL) {
        ERR("Can't find H264DecGetInfo in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mH264DecInit = (FT_H264DecInit)dlsym(mLibHandle, "H264DecInit");
    if(mH264DecInit == NULL) {
        ERR("Can't find H264DecInit in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mH264DecDecode = (FT_H264DecDecode)dlsym(mLibHandle, "H264DecDecode");
    if(mH264DecDecode == NULL) {
        ERR("Can't find H264DecDecode in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }
    /*
        mH264_DecReleaseDispBfr = (FT_H264_DecReleaseDispBfr)dlsym(mLibHandle, "H264_DecReleaseDispBfr");
        if(mH264_DecReleaseDispBfr == NULL) {
            ERR("Can't find H264_DecReleaseDispBfr in %s",libName);
            dlclose(mLibHandle);
            mLibHandle = NULL;
            return false;
        }
    */
    mH264DecRelease = (FT_H264DecRelease)dlsym(mLibHandle, "H264DecRelease");
    if(mH264DecRelease == NULL) {
        ERR("Can't find H264DecRelease in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
    }

    mH264Dec_SetCurRecPic = (FT_H264Dec_SetCurRecPic)dlsym(mLibHandle, "H264Dec_SetCurRecPic");
    if(mH264Dec_SetCurRecPic == NULL) {
        ERR("Can't find H264Dec_SetCurRecPic in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mH264Dec_GetLastDspFrm = (FT_H264Dec_GetLastDspFrm)dlsym(mLibHandle, "H264Dec_GetLastDspFrm");
    if(mH264Dec_GetLastDspFrm == NULL) {
        ERR("Can't find H264Dec_GetLastDspFrm in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mH264Dec_ReleaseRefBuffers = (FT_H264Dec_ReleaseRefBuffers)dlsym(mLibHandle, "H264Dec_ReleaseRefBuffers");
    if(mH264Dec_ReleaseRefBuffers == NULL) {
        ERR("Can't find H264Dec_ReleaseRefBuffers in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mH264DecMemInit = (FT_H264DecMemInit)dlsym(mLibHandle, "H264DecMemInit");
    if(mH264DecMemInit == NULL) {
        ERR("Can't find H264DecMemInit in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    return true;
}

int main(int argc, char **argv)
{
    char* filename_bs = NULL;
    FILE* fp_bs = NULL;
    char* filename_yuv = NULL;
    FILE* fp_yuv = NULL;
    int format = -1;
    unsigned int frames = 0;
    unsigned int width = 1280;
    unsigned int height = 720;

    unsigned int startcode = 0;
    unsigned int maskcode = 0;
    int i;

    AVCHandle *mHandle;


    // bitstream buffer, read from bs file
    unsigned char buffer_data[ONEFRAME_BITSTREAM_BFR_SIZE];
    int buffer_size = 0;

    // yuv420sp buffer, decode from bs buffer
    sp<MemoryHeapIon> pmem_yuv420sp = NULL;
    sp<MemoryHeapIon> pmem_yuv420sp_num[DEC_YUV_BUFFER_NUM] = {NULL};
    unsigned char* pyuv[DEC_YUV_BUFFER_NUM] = {NULL};
    unsigned char* pyuv_phy[DEC_YUV_BUFFER_NUM] = {NULL};

    // yuv420p buffer, transform from yuv420sp and write to yuv file
    unsigned char* py = NULL;
    unsigned char* pu = NULL;
    unsigned char* pv = NULL;

    unsigned int framenum_bs = 0;
    unsigned int framenum_err = 0;
    unsigned int framenum_yuv = 0;
    unsigned int time_total_ms = 0;


    // VSP buffer
    sp<MemoryHeapIon> pmem_inter = NULL;
    unsigned char* pbuf_inter = NULL;
    unsigned char* pbuf_inter_phy = NULL;

    sp<MemoryHeapIon> pmem_stream = NULL;
    unsigned char* pbuf_stream = NULL;
    unsigned char* pbuf_stream_phy = NULL;

    unsigned int size_extra = 0;
    int size_inter = 0;
    int size_stream = 0;

    int phy_addr = 0;
    int size = 0;
    int size_yuv = 0;
    /* parse argument */

    if (argc < 3)
    {
        usage();
        return -1;
    }

    for (i=1; i<argc; i++)
    {
        if (strcmp(argv[i], "-i") == 0 && (i < argc-1))
        {
            filename_bs = argv[++i];
        }
        else if (strcmp(argv[i], "-o") == 0 && (i < argc-1))
        {
            filename_yuv = argv[++i];
        }
        else if (strcmp(argv[i], "-format") == 0 && (i < argc-1))
        {
            format = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "-frames") == 0 && (i < argc-1))
        {
            frames = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "-help") == 0)
        {
            usage();
            return 0;
        }
        else
        {
            usage();
            return -1;
        }
    }


    /* check argument */
    if (filename_bs == NULL)
    {
        usage();
        return -1;
    }



    fp_bs = fopen(filename_bs, "rb");
    if (fp_bs == NULL)
    {
        ERR("Failed to open file %s\n", filename_bs);
        goto err;
    }
    if (filename_yuv != NULL)
    {
        fp_yuv = fopen(filename_yuv, "wb");
        if (fp_yuv == NULL)
        {
            ERR("Failed to open file %s\n", filename_yuv);
            goto err;
        }
    }


    if ((format < 0) || (format > 4))
    {
        // detect bistream format
        unsigned char header_data[64];
        int header_size = fread(header_data, 1, 64, fp_bs);
        if (header_size <= 0)
        {
            ERR("Failed to read file %s\n", filename_bs);
            goto err;
        }
        rewind(fp_bs);
        format = detect_format(header_data, header_size);
    }

    /*MMU Enable or not enable.  shark:not enable;dophin:enable */
    mIOMMUEnabled = MemoryHeapIon::Mm_iommu_is_enabled();
    INFO("IOMMU enabled: %d\n", mIOMMUEnabled);

    /* bs buffer */
    if (mIOMMUEnabled) {
        pmem_stream = new MemoryHeapIon(SPRD_ION_WITH_MMU, ONEFRAME_BITSTREAM_BFR_SIZE, MemoryHeapBase::NO_CACHING, ION_HEAP_ID_MASK_SYSTEM);
    } else {
        pmem_stream = new MemoryHeapIon(SPRD_ION_NO_MMU, ONEFRAME_BITSTREAM_BFR_SIZE, MemoryHeapBase::NO_CACHING, ION_HEAP_ID_MASK_MM);
    }
    if (pmem_stream->getHeapID() < 0)
    {
        ERR("Failed to alloc bitstream pmem buffer\n");
        goto err;
    }
    if (mIOMMUEnabled) {
        pmem_stream->get_mm_iova(&phy_addr, &size_stream);
    } else {
        pmem_stream->get_phy_addr_from_ion(&phy_addr, &size_stream);
    }
    pbuf_stream = (unsigned char*)pmem_stream->base();
    pbuf_stream_phy = (unsigned char*)phy_addr;
    //INFO("pbuf_stream_phy = %08x, pbuf_stream = %08x, size_stream =%08x \n", pbuf_stream_phy, pbuf_stream, size_stream);
    if (pbuf_stream == NULL)
    {
        ERR("Failed to alloc bitstream pmem buffer\n");
        goto err;
    }

    /* yuv420sp buffer */


    if (mIOMMUEnabled) {
        for (i=0; i<DEC_YUV_BUFFER_NUM; i++)
        {
            pmem_yuv420sp = new MemoryHeapIon(SPRD_ION_WITH_MMU, width*height*3/2, MemoryHeapBase::NO_CACHING, ION_HEAP_ID_MASK_SYSTEM);
            if (pmem_yuv420sp->getHeapID() < 0)
            {
                ERR("Failed to alloc yuv pmem buffer\n");
                goto err;
            }

            pmem_yuv420sp->get_mm_iova(&phy_addr, &size_yuv);
            pyuv[i] = ((unsigned char*)pmem_yuv420sp->base()) ;//+ width*height*3/2 * i;
            pyuv_phy[i] = ((unsigned char*)phy_addr) ;//+ width*height*3/2 * i;
            pmem_yuv420sp_num[i] = pmem_yuv420sp;
            //INFO("pyuv_phy[%d] = %08x, pyuv[%d] = %08x ,size_yuv =%08x\n", i, pyuv_phy[i], i, pyuv[i],size_yuv);
        }

    } else {
        pmem_yuv420sp = new MemoryHeapIon(SPRD_ION_NO_MMU, width*height*3/2 * DEC_YUV_BUFFER_NUM, MemoryHeapBase::NO_CACHING, ION_HEAP_ID_MASK_MM);
        pmem_yuv420sp->get_phy_addr_from_ion(&phy_addr, &size_yuv);
        for (i=0; i<DEC_YUV_BUFFER_NUM; i++)
        {
            pyuv[i] = ((unsigned char*)pmem_yuv420sp->base()) + width*height*3/2 * i;
            pyuv_phy[i] = ((unsigned char*)phy_addr) + width*height*3/2 * i;
            //INFO("pyuv_phy[%d] = %08x, pyuv[%d] = %08x \n", i, pyuv_phy[i], i, pyuv[i]);
        }
    }

    if (pyuv[0] == NULL)
    {
        ERR("Failed to alloc yuv pmem buffer\n");
        goto err;
    }

    /* yuv420p buffer */
    py = (unsigned char*)vsp_malloc(width * height * sizeof(unsigned char), 4);
    if (py == NULL)
    {
        ERR("Failed to alloc yuv buffer\n");
        goto err;
    }
    pu = (unsigned char*)vsp_malloc(width/2 * height/2 * sizeof(unsigned char), 4);
    if (pu == NULL)
    {
        ERR("Failed to alloc yuv buffer\n");
        goto err;
    }
    pv = (unsigned char*)vsp_malloc(width/2 * height/2 * sizeof(unsigned char), 4);
    if (pv == NULL)
    {
        ERR("Failed to alloc yuv buffer\n");
        goto err;
    }




    INFO("Try to decode %s to %s, format = %s\n", filename_bs, filename_yuv, format2str(format));


    mHandle = (AVCHandle *)malloc(sizeof(AVCHandle));
    memset(mHandle, 0, sizeof(AVCHandle));

    mHandle->userdata = (void *)mHandle;
    mHandle->VSP_extMemCb = VSP_malloc_cb;
    mHandle->VSP_bindCb = VSP_bind_unbind_NULL;
    mHandle->VSP_unbindCb = VSP_bind_unbind_NULL;

    openDecoder("libomx_avcdec_hw_sprd.so");

    /* step 1 - init vsp */
    size_inter = H264_DECODER_INTERNAL_BUFFER_SIZE;
    /* if (mIOMMUEnabled) {
        pmem_inter = new MemoryHeapIon(SPRD_ION_WITH_MMU, size_inter, MemoryHeapBase::NO_CACHING, ION_HEAP_ID_MASK_SYSTEM);
    } else {
        pmem_inter = new MemoryHeapIon(SPRD_ION_NO_MMU, size_inter, MemoryHeapBase::NO_CACHING, ION_HEAP_ID_MASK_MM);
    }
    if (pmem_inter->getHeapID() == NULL)
    {
        ERR("Failed to alloc inter memory\n");
        goto err;
    }
    if (mIOMMUEnabled) {
        pmem_inter->get_mm_iova(&phy_addr, &size_inter);
    } else {
        pmem_inter->get_phy_addr_from_ion(&phy_addr, &size_inter);
    }
    pbuf_inter = (unsigned char*)pmem_inter->base();
    pbuf_inter_phy = (unsigned char*)phy_addr;
     INFO("pbuf_inter_phy = %08x, pbuf_inter = %08x \n", pbuf_inter_phy, pbuf_inter);
    if (pbuf_inter == NULL)
    {
        ERR("Failed to alloc inter memory\n");
        goto err;
    }*/

    pbuf_inter = (uint8 *)malloc(size_inter);
    pbuf_inter_phy = NULL;
//	H264Dec_RegSPSCB(ActivateSPS);
//        H264Dec_RegMallocCB( VSP_malloc_cb);
    if (dec_init(mHandle, format, NULL, 0, pbuf_inter, pbuf_inter_phy, size_inter/*, extra_mem*/) != 0)
    {
        ERR("Failed to init VSP\n");
        goto err;
    }

    /* step 2 - decode with vsp */
    startcode = table_startcode2[format];
    maskcode = table_maskcode2[format];
    while (!feof(fp_bs))
    {
        int read_size = fread(buffer_data+buffer_size, 1, ONEFRAME_BITSTREAM_BFR_SIZE-buffer_size, fp_bs);
        int iCount = 0;
        if (read_size <= 0)
        {
            break;
        }
        buffer_size += read_size;


        unsigned char* ptmp = buffer_data;
        unsigned int frame_size = 0;
        while (buffer_size > 0)
        {
            // search a frame
            frame_size = find_frame(ptmp, buffer_size, startcode, maskcode);
            if (frame_size == 0)
            {
                if ((ptmp == buffer_data) || feof(fp_bs))
                {
                    frame_size = buffer_size;
                }
                else
                {
                    break;
                }
            }


            // read a bitstream frame
            memcpy(pbuf_stream, ptmp, frame_size);

            ptmp += frame_size;
            buffer_size -= frame_size;

            // decode bitstream to yuv420sp
            unsigned char* pyuv420sp = pyuv[framenum_bs % DEC_YUV_BUFFER_NUM];
            unsigned char* pyuv420sp_phy = pyuv_phy[framenum_bs % DEC_YUV_BUFFER_NUM];
            *pyuv420sp = 16;
            INFO("pyuv420sp = %d\n",*pyuv420sp);
            (*mH264Dec_SetCurRecPic)(mHandle, pyuv420sp, pyuv420sp_phy, NULL, framenum_yuv);
            framenum_bs ++;
            MMDecOutput dec_out;
            dec_out.frameEffective = 0;
            int64_t start = systemTime();
            int ret = dec_decode_frame(mHandle, pbuf_stream, pbuf_stream_phy, frame_size, &dec_out);
            int64_t end = systemTime();
            unsigned int duration = (unsigned int)((end-start) / 1000000L);
            time_total_ms += duration;

            if (duration < 40)
                usleep((40 - duration)*1000);

            if (ret != 0) {
                framenum_err ++;
                ERR("frame %d: time = %dms, size = %d, failed(%d)\n", framenum_bs, duration, frame_size, ret);
                continue;
            }

            INFO("frame %d[%dx%d]: time = %dms, size = %d, effective(%d)\n",
                 framenum_bs, dec_out.frame_width, dec_out.frame_height, duration, frame_size, dec_out.frameEffective);

            if ((dec_out.frameEffective) && (fp_yuv != NULL)) {
                // yuv420sp to yuv420p
                yuv420sp_to_yuv420p(dec_out.pOutFrameY, dec_out.pOutFrameU, py, pu, pv, dec_out.frame_width, dec_out.frame_height);

                // write yuv420p
                if (write_yuv_frame(py, pu, pv, dec_out.frame_width, dec_out.frame_height, fp_yuv)!= 0)	{
                    break;
                }

                framenum_yuv ++;
            }

            if (frames != 0)
            {
                if (framenum_yuv >= frames)
                {
                    goto early_terminate;
                }
            }
        }

        if (buffer_size > 0)
        {
            memmove(buffer_data, ptmp, buffer_size);
        }
    }

early_terminate:
    /* step 3 - release vsp */
    dec_release(mHandle);


    INFO("Finish decoding %s(%s, %d frames) to %s(%d frames)", filename_bs, format2str(format), framenum_bs, filename_yuv, framenum_yuv);
    if (framenum_err > 0)
    {
        INFO(", %d frames failed", framenum_err);
    }
    if (framenum_bs > 0)
    {
        INFO(", average time = %dms", time_total_ms/framenum_bs);
    }
    INFO("\n");



err:
    if(mLibHandle)
    {
        dlclose(mLibHandle);
        mLibHandle = NULL;
    }

    if (mHandle != NULL)
    {
        free (mHandle);
    }

    if (pbuf_inter != NULL)
    {   //pmem_inter
        vsp_free(pbuf_inter);
        pbuf_inter = NULL;
        /* if (mIOMMUEnabled)
        {
               pmem_inter->free_mm_iova((int32)pbuf_inter_phy, size_inter);
         }
        pmem_inter.clear();
        pbuf_inter = NULL;
        pbuf_inter_phy = NULL;
        size_inter = 0;*/
    }

    if (pbuf_stream != NULL)
    {   //pmem_stream
        if (mIOMMUEnabled)
        {
            pmem_stream->free_mm_iova((int32)pbuf_stream_phy, size_stream);
        }
        pmem_stream.clear();
        pbuf_stream = NULL;
        pbuf_stream_phy = NULL;
        size_stream = 0;
    }
    if (pyuv[0] != NULL)
    {
        if (mIOMMUEnabled)
        {
            for (i=0; i<DEC_YUV_BUFFER_NUM; i++)
            {
                pmem_yuv420sp_num[i]->free_mm_iova((int32)pyuv_phy[i], size_yuv);
                pmem_yuv420sp_num[i].clear();
                pyuv[i] = NULL;
                pyuv_phy[i] = NULL;
                size_yuv = 0;
            }
        }
        else
        {
            pmem_yuv420sp.clear();
            pyuv[0] = NULL;
        }

    }
    if (py != NULL)
    {
        vsp_free(py);
        py = NULL;
    }
    if (pu != NULL)
    {
        vsp_free(pu);
        pu = NULL;
    }
    if (pv != NULL)
    {
        vsp_free(pv);
        pv = NULL;
    }

    if (fp_yuv != NULL)
    {
        fclose(fp_yuv);
        fp_yuv = NULL;
    }
    if (fp_bs != NULL)
    {
        fclose(fp_bs);
        fp_bs = NULL;
    }

    return 0;
}

