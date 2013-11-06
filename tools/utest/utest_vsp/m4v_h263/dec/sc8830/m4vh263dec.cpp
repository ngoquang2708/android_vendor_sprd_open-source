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
using namespace android;


//#include "mpeg4dec.h"
#include "m4v_h263_dec_api.h"


#include "util.h"



#define ERR(x...)	fprintf(stderr, ##x)
#define INFO(x...)	fprintf(stdout, ##x)




#define ONEFRAME_BITSTREAM_BFR_SIZE	(1500*1024)  //for bitstream size of one encoded frame.
#define DEC_YUV_BUFFER_NUM   3


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

typedef enum
{
    H263_MODE = 0,MPEG4_MODE,
    FLV_MODE,
    UNKNOWN_MODE
} MP4DecodingMode;


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

FT_MP4DecSetCurRecPic mMP4DecSetCurRecPic;
FT_MP4DecMemCacheInit mMP4DecMemCacheInit;
FT_MP4DecInit mMP4DecInit;
FT_MP4DecVolHeader mMP4DecVolHeader;
FT_MP4DecMemInit mMP4DecMemInit;
FT_MP4DecDecode mMP4DecDecode;
FT_MP4DecRelease mMP4DecRelease;
FT_Mp4GetVideoDimensions mMp4GetVideoDimensions;
FT_Mp4GetBufferDimensions mMp4GetBufferDimensions;
FT_MP4DecReleaseRefBuffers mMP4DecReleaseRefBuffers;

unsigned int framenum_bs = 0;

sp<MemoryHeapIon> iDecExtPmemHeap;
void*  iDecExtVAddr =NULL;
uint32 iDecExtPhyAddr =NULL;

int extMemoryAlloc(void *  mHandle,unsigned int width,unsigned int height) {

    int32 Frm_width_align = ((width + 15) & (~15));
    int32 Frm_height_align = ((height + 15) & (~15));

#if 0 //removed it, bug 121132, xiaowei@2013.01.25
    mWidth = Frm_width_align;
    mHeight = Frm_height_align;
#endif

//    ALOGI("%s, %d, Frm_width_align: %d, Frm_height_align: %d", __FUNCTION__, __LINE__, Frm_width_align, Frm_height_align);

    int32 mb_num_x = Frm_width_align/16;
    int32 mb_num_y = Frm_height_align/16;
    int32 mb_num_total = mb_num_x * mb_num_y;
    int32 frm_size = (mb_num_total * 256);
    int32 i;
    MMCodecBuffer extra_mem;
    uint32 extra_mem_size;



//    if (mDecoderSwFlag)
//    {
//        extra_mem_size[HW_NO_CACHABLE] = 0;
//        extra_mem_size[HW_CACHABLE] = 0;
//    }else
    {
        extra_mem_size = mb_num_total * (32 + 3 * 80) + 1024;
        iDecExtPmemHeap = new MemoryHeapIon("/dev/ion", extra_mem_size, MemoryHeapBase::NO_CACHING, ION_HEAP_CARVEOUT_MASK);
        int fd = iDecExtPmemHeap->getHeapID();
        if(fd>=0)
        {
            int ret,phy_addr, buffer_size;
            ret = iDecExtPmemHeap->get_phy_addr_from_ion(&phy_addr, &buffer_size);
            if(ret)
            {
                ERR ("iDecExtPmemHeap get_phy_addr_from_ion fail %d",ret);
            }

            iDecExtPhyAddr =(uint32)phy_addr;
//            ALOGD ("%s: ext mem pmempool %x,%x,%x,%x\n", __FUNCTION__, iDecExtPmemHeap->getHeapID(),iDecExtPmemHeap->base(),phy_addr,buffer_size);
            iDecExtVAddr = (void *)iDecExtPmemHeap->base();
            extra_mem.common_buffer_ptr =(uint8 *) iDecExtVAddr;
            extra_mem.common_buffer_ptr_phy = (void *)iDecExtPhyAddr;
            extra_mem.size = extra_mem_size;
        }
    }

    (*mMP4DecMemInit)((MP4Handle *)mHandle, &extra_mem);

    return 1;
}

static int dec_init(MP4Handle *mHandle, int format, unsigned char* pheader_buffer, unsigned int header_size,
                    unsigned char* pbuf_inter, unsigned char* pbuf_inter_phy, unsigned int size_inter,
                    unsigned char* pbuf_extra, unsigned char* pbuf_extra_phy, unsigned int size_extra)
{
    MMCodecBuffer InterMemBfr;
    MMCodecBuffer ExtraMemBfr;
    MMDecVideoFormat video_format;
    int ret;

    INFO("dec_init IN\n");

    InterMemBfr.common_buffer_ptr = pbuf_inter;
    InterMemBfr.common_buffer_ptr_phy = pbuf_inter_phy;
    InterMemBfr.size = size_inter;

    ExtraMemBfr.common_buffer_ptr = pbuf_extra;
    ExtraMemBfr.common_buffer_ptr_phy = pbuf_extra_phy;
    ExtraMemBfr.size	= size_extra;

    video_format.video_std = format;
    video_format.i_extra = header_size;
    video_format.p_extra = pheader_buffer;
    video_format.frame_width = 0;
    video_format.frame_height = 0;

    ret = (*mMP4DecInit)(mHandle, &InterMemBfr);
    if (ret == 0)
    {
        (*mMP4DecMemInit)(mHandle, &ExtraMemBfr);
    }

    INFO("dec_init OUT\n");

    return ret;
}

static int dec_decode_frame(MP4Handle *mHandle, unsigned char* pframe, unsigned char* pframe_y,unsigned int size, int* frame_effective, unsigned int* width, unsigned int* height, int* type)
{
    MMDecInput dec_in;
    MMDecOutput dec_out;
    int ret;

    do
    {
        dec_in.pStream= pframe;
        dec_in.pStream_phy= pframe_y;
        dec_in.dataLen = size;
        dec_in.beLastFrm = 0;
        dec_in.expected_IVOP = 0;
        dec_in.beDisplayed = 1;
        dec_in.err_pkt_num = 0;

        dec_out.VopPredType = -1;
        dec_out.frameEffective = 0;

        ret = (*mMP4DecDecode)(mHandle, &dec_in, &dec_out);
        if (ret == 0)
        {
            *width = dec_out.frame_width;
            *height = dec_out.frame_height;
            *type = dec_out.VopPredType;
            *frame_effective = dec_out.frameEffective;
        }
    } while (framenum_bs == 1 && ret == MMDEC_MEMORY_ALLOCED);

    return ret;
}


static void dec_release(MP4Handle *mHandle)
{
    (*mMP4DecReleaseRefBuffers)(mHandle);

    (*mMP4DecRelease)(mHandle);
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

    INFO("size: %d, pbuffer:%0x, %0x, %0x, %0x, %0x, %0x, %0x, %0x\n", size, pbuffer[0], pbuffer[1], pbuffer[2], pbuffer[3], pbuffer[4], pbuffer[5], pbuffer[6], pbuffer[7]);

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
    if (type == 0)
    {
        return "I";
    }
    else if (type == 1)
    {
        return "P";
    }
    else if (type == 2)
    {
        return "B";
    }
    else
    {
        return "N";
    }
}

bool openDecoder(const char* libName)
{
    if(mLibHandle) {
        dlclose(mLibHandle);
    }

    INFO("openDecoder, lib: %s",libName);

    mLibHandle = dlopen(libName, RTLD_NOW);
    if(mLibHandle == NULL) {
        ERR("openDecoder, can't open lib: %s",libName);
        return false;
    }

    mMP4DecSetCurRecPic = (FT_MP4DecSetCurRecPic)dlsym(mLibHandle, "MP4DecSetCurRecPic");
    if(mMP4DecSetCurRecPic == NULL) {
        ERR("Can't find MP4DecSetCurRecPic in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

//    mMP4DecMemCacheInit = (FT_MP4DecMemCacheInit)dlsym(mLibHandle, "MP4DecMemCacheInit");
//    if(mMP4DecMemCacheInit == NULL){
//        LOGE("Can't find MP4DecMemCacheInit in %s",libName);
//        dlclose(mLibHandle);
//        mLibHandle = NULL;
//        return false;
//    }

    mMP4DecInit = (FT_MP4DecInit)dlsym(mLibHandle, "MP4DecInit");
    if(mMP4DecInit == NULL) {
        ERR("Can't find MP4DecInit in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mMP4DecVolHeader = (FT_MP4DecVolHeader)dlsym(mLibHandle, "MP4DecVolHeader");
    if(mMP4DecVolHeader == NULL) {
        ERR("Can't find MP4DecVolHeader in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mMP4DecMemInit = (FT_MP4DecMemInit)dlsym(mLibHandle, "MP4DecMemInit");
    if(mMP4DecMemInit == NULL) {
        ERR("Can't find MP4DecMemInit in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mMP4DecDecode = (FT_MP4DecDecode)dlsym(mLibHandle, "MP4DecDecode");
    if(mMP4DecDecode == NULL) {
        ERR("Can't find MP4DecDecode in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mMP4DecRelease = (FT_MP4DecRelease)dlsym(mLibHandle, "MP4DecRelease");
    if(mMP4DecRelease == NULL) {
        ERR("Can't find MP4DecRelease in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
    }

    mMp4GetVideoDimensions = (FT_Mp4GetVideoDimensions)dlsym(mLibHandle, "Mp4GetVideoDimensions");
    if(mMp4GetVideoDimensions == NULL) {
        ERR("Can't find Mp4GetVideoDimensions in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mMp4GetBufferDimensions = (FT_Mp4GetBufferDimensions)dlsym(mLibHandle, "Mp4GetBufferDimensions");
    if(mMp4GetBufferDimensions == NULL) {
        ERR("Can't find Mp4GetBufferDimensions in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mMP4DecReleaseRefBuffers = (FT_MP4DecReleaseRefBuffers)dlsym(mLibHandle, "MP4DecReleaseRefBuffers");
    if(mMP4DecReleaseRefBuffers == NULL) {
        ERR("Can't find MP4DecReleaseRefBuffers in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    return true;
}

sp<MemoryHeapIon> pmem_extra = NULL;

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

    MP4Handle *mHandle = NULL;


    // bitstream buffer, read from bs file
    unsigned char buffer_data[ONEFRAME_BITSTREAM_BFR_SIZE];
    int buffer_size = 0;

    // yuv420sp buffer, decode from bs buffer
    sp<MemoryHeapIon> pmem_yuv420sp = NULL;
    unsigned char* pyuv[DEC_YUV_BUFFER_NUM] = {NULL};
    unsigned char* pyuv_phy[DEC_YUV_BUFFER_NUM] = {NULL};

    // yuv420p buffer, transform from yuv420sp and write to yuv file
    unsigned char* py = NULL;
    unsigned char* pu = NULL;
    unsigned char* pv = NULL;

    //unsigned int framenum_bs = 0;
    unsigned int framenum_err = 0;
    unsigned int framenum_yuv = 0;
    unsigned int time_total_ms = 0;


    // VSP buffer
    sp<MemoryHeapIon> pmem_extra = NULL;
    unsigned char* pbuf_extra = NULL;
    unsigned char* pbuf_extra_phy = NULL;

    sp<MemoryHeapIon> pmem_inter = NULL;
    unsigned char* pbuf_inter = NULL;
    unsigned char* pbuf_inter_phy = NULL;

    sp<MemoryHeapIon> pmem_stream = NULL;
    unsigned char* pbuf_stream = NULL;
    unsigned char* pbuf_stream_phy = NULL;

    unsigned int size_extra = 0;
    unsigned int size_inter = 0;
    unsigned int size_stream = 0;

    int phy_addr = 0;
    int size = 0;
    framenum_bs = 0;

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




    /* bs buffer */
    pmem_stream = new MemoryHeapIon("/dev/ion", ONEFRAME_BITSTREAM_BFR_SIZE, MemoryHeapBase::NO_CACHING, ION_HEAP_CARVEOUT_MASK);
    if (pmem_stream->getHeapID() < 0)
    {
        ERR("Failed to alloc bitstream pmem buffer\n");
        goto err;
    }
    pmem_stream->get_phy_addr_from_ion(&phy_addr, &size);
    pbuf_stream = (unsigned char*)pmem_stream->base();
    pbuf_stream_phy = (unsigned char*)phy_addr;
    if (pbuf_stream == NULL)
    {
        ERR("Failed to alloc bitstream pmem buffer\n");
        goto err;
    }

    /* yuv420sp buffer */
    pmem_yuv420sp = new MemoryHeapIon("/dev/ion", width*height*3/2 * DEC_YUV_BUFFER_NUM, MemoryHeapBase::NO_CACHING, ION_HEAP_CARVEOUT_MASK);
    if (pmem_yuv420sp->getHeapID() < 0)
    {
        ERR("Failed to alloc yuv pmem buffer\n");
        goto err;
    }
    pmem_yuv420sp->get_phy_addr_from_ion(&phy_addr, &size);
    for (i=0; i<DEC_YUV_BUFFER_NUM; i++)
    {
        pyuv[i] = ((unsigned char*)pmem_yuv420sp->base()) + width*height*3/2 * i;
        pyuv_phy[i] = ((unsigned char*)phy_addr) + width*height*3/2 * i;
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


    mHandle = (MP4Handle *)malloc(sizeof(MP4Handle));
    memset(mHandle, 0, sizeof(tagMP4Handle));

    mHandle->userdata = (void *)mHandle;
    mHandle->VSP_extMemCb = extMemoryAlloc;
    mHandle->VSP_bindCb = NULL;
    mHandle->VSP_unbindCb = NULL;

    openDecoder("libomx_m4vh263dec_hw_sprd.so");

    /* step 1 - init vsp */
    size_inter = MP4DEC_INTERNAL_BUFFER_SIZE;
    pmem_inter = new MemoryHeapIon("/dev/ion", size_inter, MemoryHeapBase::NO_CACHING, ION_HEAP_CARVEOUT_MASK);
    if (pmem_inter->getHeapID() == NULL)
    {
        ERR("Failed to alloc inter memory\n");
        goto err;
    }

    pmem_inter->get_phy_addr_from_ion(&phy_addr, &size);
    pbuf_inter = (unsigned char*)pmem_inter->base();
    pbuf_inter_phy = (unsigned char*)phy_addr;
    if (pbuf_inter == NULL)
    {
        ERR("Failed to alloc inter memory\n");
        goto err;
    }

    size_extra = 5000 * 1024;
    pmem_extra = new MemoryHeapIon("/dev/ion", size_extra, MemoryHeapBase::NO_CACHING, ION_HEAP_CARVEOUT_MASK);
    if (pmem_extra->getHeapID() < 0)
    {
        ERR("Failed to alloc extra memory\n");
        goto err;
    }
    pmem_extra->get_phy_addr_from_ion(&phy_addr, &size);
    pbuf_extra = (unsigned char*)pmem_extra->base();
    pbuf_extra_phy = (unsigned char*)phy_addr;
    if (pbuf_extra == NULL)
    {
        ERR("Failed to alloc extra memory\n");
        goto err;
    }

    if (dec_init(mHandle, format, NULL, 0, pbuf_inter, pbuf_inter_phy, size_inter, pbuf_extra, pbuf_extra_phy, size_extra) != 0)
    {
        ERR("Failed to init VSP\n");
        goto err;
    }
    {
        MP4DecodingMode mode = (format== MPEG4_MODE) ? MPEG4_MODE : H263_MODE;
        MMDecVideoFormat video_format;

        if (mode == MPEG4_MODE)
        {
            int read_size = fread(buffer_data, 1, ONEFRAME_BITSTREAM_BFR_SIZE, fp_bs);
            if (read_size <= 0)
            {
                goto err;
            }

            // search a frame
            unsigned char* ptmp = buffer_data;
            unsigned int frame_size = 0;
            frame_size = find_frame(ptmp, buffer_size, startcode, maskcode);
            video_format.i_extra = frame_size;

            if( video_format.i_extra > 0)
            {
                memcpy(pbuf_stream, buffer_data,read_size);
                video_format.p_extra = pbuf_stream;
                video_format.p_extra_phy = pbuf_stream_phy;
            } else {
                video_format.p_extra = NULL;
                video_format.p_extra_phy = NULL;
            }

            video_format.video_std = MPEG4;
            video_format.frame_width = 0;
            video_format.frame_height = 0;
            video_format.uv_interleaved = 1;

            INFO(" MP4DecVolHeader \n");
            MMDecRet ret = (*mMP4DecVolHeader)(mHandle, &video_format);

            if (MMDEC_OK != ret)
            {
                ERR("Failed to decode Vol Header\n");
                goto err;
            }

            buffer_size = 0;
            fseek(fp_bs, 0, SEEK_SET);
        }
    }
    /* step 2 - decode with vsp */
    startcode = table_startcode2[format];
    maskcode = table_maskcode2[format];
    while (!feof(fp_bs))
    {
        int read_size = fread(buffer_data+buffer_size, 1, ONEFRAME_BITSTREAM_BFR_SIZE-buffer_size, fp_bs);
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
            int frame_effective = 0;
            int type = 0;
            unsigned int width_new = 0;
            unsigned int height_new = 0;
            unsigned char* pyuv420sp = pyuv[framenum_bs % DEC_YUV_BUFFER_NUM];
            unsigned char* pyuv420sp_phy = pyuv_phy[framenum_bs % DEC_YUV_BUFFER_NUM];
            (*mMP4DecSetCurRecPic)(mHandle, pyuv420sp, pyuv420sp_phy, NULL);
            framenum_bs ++;
            int64_t start = systemTime();
            int ret = dec_decode_frame(mHandle, pbuf_stream,pbuf_stream_phy, frame_size, &frame_effective, &width_new, &height_new, &type);
            int64_t end = systemTime();
            unsigned int duration = (unsigned int)((end-start) / 1000000L);
            time_total_ms += duration;

            if (duration < 40)
                usleep((40 - duration)*1000);

            if (ret != 0)
            {
                framenum_err ++;
                ERR("frame %d: time = %dms, size = %d, type = %s, failed(%d)\n", framenum_bs, duration, frame_size, type2str(type), ret);
                continue;
            }



            if ((width_new != width) || (height_new != height))
            {
                width = width_new;
                height = height_new;
            }
            INFO("frame %d[%dx%d]: time = %dms, size = %d, type = %s, effective(%d)\n", framenum_bs, width, height, duration, frame_size, type2str(type), frame_effective);


            if ((frame_effective) && (fp_yuv != NULL))
            {
                // yuv420sp to yuv420p
                yuv420sp_to_yuv420p(pyuv420sp, pyuv420sp+width*height, py, pu, pv, width, height);

                // write yuv420p
                if (write_yuv_frame(py, pu, pv, width, height, fp_yuv)!= 0)
                {
                    break;
                }

                framenum_yuv ++;
            }

            if (frames != 0)
            {
                if (framenum_yuv >= frames)
                {
                    goto  early_terminate;
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

    if(iDecExtVAddr)
    {
        iDecExtPmemHeap.clear();
        iDecExtVAddr = NULL;
    }
    if (pbuf_extra != NULL)
    {
        pmem_extra.clear();
        pbuf_extra = NULL;
    }

    if (pbuf_inter != NULL)
    {
        pmem_inter.clear();
        pbuf_inter = NULL;
    }

    if (pbuf_stream != NULL)
    {
        pmem_stream.clear();
        pbuf_stream = NULL;
    }
    if (pyuv[0] != NULL)
    {
        pmem_yuv420sp.clear();
        pyuv[0] = NULL;
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

