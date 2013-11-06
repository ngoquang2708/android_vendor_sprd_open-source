#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <math.h>
#include <dlfcn.h>

#include <linux/ion.h>
#include <binder/MemoryHeapIon.h>
using namespace android;


//#include "mpeg4enc.h"
#include "m4v_h263_enc_api.h"


#include "util.h"

//#define CALCULATE_PSNR

#define ERR(x...)	fprintf(stderr, ##x)
#define INFO(x...)	fprintf(stdout, ##x)


#define CLIP_16(x)	(((x) + 15) & (~15))


#define ONEFRAME_BITSTREAM_BFR_SIZE	(1500*1024)  //for bitstream size of one encoded frame.




static void usage()
{
    INFO("usage:\n");
    INFO("utest_vsp_enc -i filename_yuv -w width -h height -o filename_bitstream [OPTIONS]\n");
    INFO("-i                string : input yuv filename\n");
    INFO("-w                integer: intput yuv width\n");
    INFO("-h                integer: intput yuv height\n");
    INFO("-o                string : output bitstream filename\n");
    INFO("[OPTIONS]:\n");
    INFO("-format           integer: video format(0 is h263 / 1 is mpeg4), default is 1\n");
    INFO("-framerate        integer: framerate, default is 25\n");
    INFO("-max_key_interval integer: maximum keyframe interval, default is 30\n");
    INFO("-bitrate          integer: target bitrate in kbps if cbr(default), default is 512\n");
    INFO("-qp               integer: qp[1...31] if vbr, default is 8\n");
    INFO("-frames           integer: number of frames to encode, default is 0(all frames)\n");
    INFO("-help                    : show this help message\n");
    INFO("Built on %s %s, Written by JamesXiong(james.xiong@spreadtrum.com)\n", __DATE__, __TIME__);
}


void* mLibHandle;
FT_MP4EncInit        mMP4EncInit;
FT_MP4EncSetConf        mMP4EncSetConf;
FT_MP4EncGetConf        mMP4EncGetConf;
FT_MP4EncStrmEncode        mMP4EncStrmEncode;
FT_MP4EncGenHeader        mMP4EncGenHeader;
FT_MP4EncRelease        mMP4EncRelease;


static int enc_init(MP4Handle *mHandle, unsigned int width, unsigned int height, int format,
                    unsigned char* pbuf_inter, unsigned char* pbuf_inter_phy, unsigned int size_inter,
                    unsigned char* pbuf_extra, unsigned char* pbuf_extra_phy, unsigned int size_extra,
                    unsigned char* pbuf_stream, unsigned char* pbuf_stream_phy, unsigned int size_stream)
{
    MMCodecBuffer InterMemBfr;
    MMCodecBuffer ExtraMemBfr;
    MMCodecBuffer StreamMemBfr;
    MMEncVideoInfo encInfo;

    INFO("enc_init IN\n");

    InterMemBfr.common_buffer_ptr = pbuf_inter;
    InterMemBfr.common_buffer_ptr_phy = pbuf_inter_phy;
    InterMemBfr.size = size_inter;

    ExtraMemBfr.common_buffer_ptr = pbuf_extra;
    ExtraMemBfr.common_buffer_ptr_phy = pbuf_extra_phy;
    ExtraMemBfr.size	= size_extra;

    StreamMemBfr.common_buffer_ptr = pbuf_stream;
    StreamMemBfr.common_buffer_ptr_phy = pbuf_stream_phy;
    StreamMemBfr.size	= size_stream;

    encInfo.is_h263 = (format == 0) ? 1 : 0;
    encInfo.frame_width = width;
    encInfo.frame_height = height;
    encInfo.uv_interleaved = 1;
    encInfo.time_scale = 1000;

    return (*mMP4EncInit)(mHandle, &InterMemBfr, &ExtraMemBfr,&StreamMemBfr, &encInfo);
}

static void enc_set_parameter(MP4Handle *mHandle, int format, int framerate, int cbr, int bitrate, int qp)
{
    MMEncConfig encConfig;
    INFO("enc_set_parameter 0\n");

    (*mMP4EncGetConf)(mHandle, &encConfig);

    encConfig.h263En = (format == 0) ? 1 : 0;
    encConfig.RateCtrlEnable = cbr;
    encConfig.targetBitRate = bitrate;
    encConfig.FrameRate = framerate;
    encConfig.QP_IVOP = qp;
    encConfig.QP_PVOP = qp;
    encConfig.vbv_buf_size = bitrate/2;
    encConfig.profileAndLevel = 1;

    (*mMP4EncSetConf)(mHandle, &encConfig);

    INFO("enc_set_parameter 1\n");

}


static int enc_get_header(MP4Handle *mHandle, unsigned char* pheader, unsigned int* size)
{
    MMEncOut encOut;
    (*mMP4EncGenHeader)(mHandle, &encOut);

    if ((encOut.strmSize > (int)(*size)) || (encOut.strmSize <= 0))
    {
        return -1;
    }

    memcpy(pheader, encOut.pOutBuf, encOut.strmSize);
    *size = encOut.strmSize;

    return 0;
}


static int enc_encode_frame(MP4Handle *mHandle, unsigned char* py, unsigned char* py_phy, unsigned char* puv, unsigned char* puv_phy, unsigned char* pframe, unsigned int* size, unsigned char**pyuv_rec, unsigned int timestamp, int* type, int bs_remain_len = 0)
{
    MMEncIn vid_in;
    MMEncOut vid_out;
    int ret;

    vid_in.time_stamp = timestamp;
    vid_in.vopType = *type;
    vid_in.bs_remain_len = bs_remain_len;
    vid_in.channel_quality = 1;
    vid_in.p_src_y = py;
    vid_in.p_src_u = puv;
    vid_in.p_src_v = 0;
    vid_in.p_src_y_phy = py_phy;
    vid_in.p_src_u_phy = puv_phy;
    vid_in.p_src_v_phy = 0;

    ret = (*mMP4EncStrmEncode)(mHandle, &vid_in, &vid_out);
    if ((vid_out.strmSize > ONEFRAME_BITSTREAM_BFR_SIZE) || (vid_out.strmSize <= 0))
    {
        return -1;
    }

    memcpy(pframe, vid_out.pOutBuf, vid_out.strmSize);
    *size = vid_out.strmSize;

//	*pyuv_rec = vid_out.pRecYUV;

    *type = vid_in.vopType;

    return ret;
}


static void enc_release(MP4Handle *mHandle)
{
    (*mMP4EncRelease)(mHandle);
}







static const char* format2str(int format)
{
    if (format == 0)
    {
        return "H263";
    }
    else
    {
        return "MPEG4";
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
        return "S";
    }
}

#if defined(CALCULATE_PSNR)
static float psnr(unsigned char* pframe_org, unsigned char* pframe_rec, unsigned int width, unsigned int height)
{
    unsigned int sse = 0;
    for (unsigned int i=0; i<width*height; i++)
    {
        int dif = (*pframe_org ++) - (*pframe_rec ++);
        sse += dif * dif;
    }
    return (sse == 0) ? 99.9f : (48.131f - 10.0f * log10f((float)sse / (float)(width*height)));
}
#endif

bool openEncoder(const char* libName)
{
    if(mLibHandle) {
        dlclose(mLibHandle);
    }

    ALOGI("openEncoder, lib: %s",libName);

    mLibHandle = dlopen(libName, RTLD_NOW);
    if(mLibHandle == NULL) {
        ERR("openEncoder, can't open lib: %s",libName);
        return false;
    }

    mMP4EncInit = (FT_MP4EncInit)dlsym(mLibHandle, "MP4EncInit");
    if(mMP4EncInit == NULL) {
        ERR("Can't find MP4EncInit in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mMP4EncSetConf = (FT_MP4EncSetConf)dlsym(mLibHandle, "MP4EncSetConf");
    if(mMP4EncSetConf == NULL) {
        ERR("Can't find MP4EncSetConf in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mMP4EncGetConf = (FT_MP4EncGetConf)dlsym(mLibHandle, "MP4EncGetConf");
    if(mMP4EncGetConf == NULL) {
        ERR("Can't find MP4EncGetConf in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mMP4EncStrmEncode = (FT_MP4EncStrmEncode)dlsym(mLibHandle, "MP4EncStrmEncode");
    if(mMP4EncStrmEncode == NULL) {
        ERR("Can't find MP4EncStrmEncode in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mMP4EncGenHeader = (FT_MP4EncGenHeader)dlsym(mLibHandle, "MP4EncGenHeader");
    if(mMP4EncGenHeader == NULL) {
        ERR("Can't find MP4EncGenHeader in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mMP4EncRelease = (FT_MP4EncRelease)dlsym(mLibHandle, "MP4EncRelease");
    if(mMP4EncRelease == NULL) {
        ERR("Can't find MP4EncRelease in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
    }

    return true;
}

int vsp_enc(char* filename_yuv, char* filename_bs, unsigned int width, unsigned int height, int format, unsigned int framerate, unsigned int max_key_interval, int cbr, unsigned int bitrate, unsigned int qp, unsigned int frames = 0)
{
    // yuv file and bs file
    FILE* fp_yuv = NULL;
    FILE* fp_bs = NULL;

    // yuv420p buffer, read from yuv file
    unsigned char* py = NULL;
    unsigned char* pu = NULL;
    unsigned char* pv = NULL;

    // yuv420p buffer, rec frame
    unsigned char* py_rec = NULL;
    unsigned char* pu_rec = NULL;
    unsigned char* pv_rec = NULL;

    // yuv420sp buffer, transform from yuv420p and to encode
    sp<MemoryHeapIon> pmem_yuv420sp = NULL;
    unsigned char* pyuv = NULL;
    unsigned char* pyuv_phy = NULL;

    // yuv420sp buffer, rec frame
    unsigned char* pyuv_rec = NULL;

    // bitstream buffer, encode from yuv420sp and write to bs file
    unsigned char header_buffer[32];
    unsigned int header_size = 32;
    unsigned char frame_buffer[ONEFRAME_BITSTREAM_BFR_SIZE];
    unsigned int frame_size = ONEFRAME_BITSTREAM_BFR_SIZE;

    unsigned int framenum_yuv = 0;
    unsigned int framenum_bs = 0;
    unsigned int bs_total_len = 0;
    unsigned int time_total_ms = 0;
    int bs_remain_len = bitrate/2;
#if defined(CALCULATE_PSNR)
    float psnr_total[3] = {.0f, .0f, .0f};
    float psnr_y = .0f;
    float psnr_u = .0f;
    float psnr_v = .0f;
#endif

    MP4Handle *mHandle = NULL;

    // VSP buffer
//	unsigned char* pbuf_inter = NULL;
//	unsigned int size_inter = 0;
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


    fp_yuv = fopen(filename_yuv, "rb");
    if (fp_yuv == NULL)
    {
        ERR("Failed to open file %s\n", filename_yuv);
        goto err;
    }
    fp_bs = fopen(filename_bs, "wb");
    if (fp_bs == NULL)
    {
        ERR("Failed to open file %s\n", filename_bs);
        goto err;
    }


    /* yuv420p buffer */
    py = (unsigned char*)malloc(width * height * sizeof(unsigned char));
    if (py == NULL)
    {
        ERR("Failed to alloc yuv buffer\n");
        goto err;
    }
    pu = (unsigned char*)malloc(width/2 * height/2 * sizeof(unsigned char));
    if (pu == NULL)
    {
        ERR("Failed to alloc yuv buffer\n");
        goto err;
    }
    pv = (unsigned char*)malloc(width/2 * height/2 * sizeof(unsigned char));
    if (pv == NULL)
    {
        ERR("Failed to alloc yuv buffer\n");
        goto err;
    }
    py_rec = (unsigned char*)malloc(width * height * sizeof(unsigned char));
    if (py_rec == NULL)
    {
        ERR("Failed to alloc yuv rec buffer\n");
        goto err;
    }
    pu_rec = (unsigned char*)malloc(width/2 * height/2 * sizeof(unsigned char));
    if (pu_rec == NULL)
    {
        ERR("Failed to alloc yuv rec buffer\n");
        goto err;
    }
    pv_rec = (unsigned char*)malloc(width/2 * height/2 * sizeof(unsigned char));
    if (pv_rec == NULL)
    {
        ERR("Failed to alloc yuv rec buffer\n");
        goto err;
    }

    /* yuv420sp buffer */
    pmem_yuv420sp = new MemoryHeapIon("/dev/ion", width * height*3/2, MemoryHeapBase::NO_CACHING, ION_HEAP_CARVEOUT_MASK);
    pmem_yuv420sp->get_phy_addr_from_ion(&phy_addr, &size);
    pyuv = (unsigned char*)pmem_yuv420sp->base();
    pyuv_phy = (unsigned char*)phy_addr;
    if (pyuv == NULL)
    {
        ERR("Failed to alloc yuv pmem buffer\n");
        goto err;
    }




    INFO("Try to encode %s[%dx%d] to %s, format = %s", filename_yuv, width, height, filename_bs, format2str(format));
    if (cbr)
    {
        INFO(", framerate = %d, bitrate = %dkbps\n", framerate, bitrate/1000);
    }
    else
    {
        INFO(", framerate = %d, QP = %d\n", framerate, qp);
    }

    mHandle = (MP4Handle *)malloc(sizeof(MP4Handle));
    memset(mHandle, 0, sizeof(MP4Handle));

    openEncoder("libomx_m4vh263enc_hw_sprd.so");


    /* step 1 - init vsp */
    size_inter = MP4ENC_INTERNAL_BUFFER_SIZE;
    pmem_inter = new MemoryHeapIon("/dev/ion", size_inter, MemoryHeapBase::NO_CACHING, ION_HEAP_CARVEOUT_MASK);
    pmem_inter->get_phy_addr_from_ion(&phy_addr, &size);
    pbuf_inter = (unsigned char*)pmem_inter->base();
    pbuf_inter_phy = (unsigned char*)phy_addr;
    if (pbuf_inter == NULL)
    {
        ERR("Failed to alloc inter memory\n");
        goto err;
    }

    INFO("pbuf_inter: %x\n", pbuf_inter);

    size_extra = width * height * 3/2 * 2 + 10*1024;
    pmem_extra = new MemoryHeapIon("/dev/ion", size_extra, MemoryHeapBase::NO_CACHING, ION_HEAP_CARVEOUT_MASK);
    pmem_extra->get_phy_addr_from_ion(&phy_addr, &size);
    pbuf_extra = (unsigned char*)pmem_extra->base();
    pbuf_extra_phy = (unsigned char*)phy_addr;
    if (pbuf_extra == NULL)
    {
        ERR("Failed to alloc extra memory\n");
        goto err;
    }
    INFO("pbuf_inter: %x\n", pbuf_extra);

    size_stream = ONEFRAME_BITSTREAM_BFR_SIZE;
    pmem_stream = new MemoryHeapIon("/dev/ion", size_stream, MemoryHeapBase::NO_CACHING, ION_HEAP_CARVEOUT_MASK);
    pmem_stream->get_phy_addr_from_ion(&phy_addr, &size);
    pbuf_stream = (unsigned char*)pmem_stream->base();
    pbuf_stream_phy = (unsigned char*)phy_addr;
    if (pbuf_stream == NULL)
    {
        ERR("Failed to alloc stream memory\n");
        goto err;
    }

    INFO("pbuf_stream: %x\n", pbuf_stream);

    if (enc_init(mHandle, width, height, format, pbuf_inter, pbuf_inter_phy, size_inter, pbuf_extra, pbuf_extra_phy, size_extra, pbuf_stream, pbuf_stream_phy, size_stream) != 0)
    {
        ERR("Failed to init VSP\n");
        goto err;
    }

    INFO("enc_init end\n");

    /* step 2 - set vsp  and get header */
    enc_set_parameter(mHandle, format, framerate, cbr, bitrate, qp);
    if (format)	// m4v
    {
        if (enc_get_header(mHandle, header_buffer, &header_size) < 0)
        {
            ERR("Failed to get header\n");
            goto err;
        }
        INFO("header: size = %d\n", header_size);
    }



    /* step 3 - encode with vsp */
    while (!feof(fp_yuv))
    {
        // judge vop type
        int type = 0;
        if (max_key_interval == 0)
        {
            if (framenum_bs > 0)
            {
                type = 1;
            }
        }
        else
        {
            if ((framenum_bs % max_key_interval) != 0)
            {
                type = 1;
            }
        }

        // read yuv420p
        if (read_yuv_frame(py, pu, pv, width, height, fp_yuv) != 0)
        {
            break;
        }
        framenum_yuv ++;



        // yuv420p to yuv420sp
        yuv420p_to_yvu420sp(py, pu, pv, pyuv, pyuv+width*height, width, height);


        // encode yuv420sp to bitstream
        int64_t start = systemTime();
        int ret = enc_encode_frame(mHandle, pyuv, pyuv_phy, pyuv+width*height, pyuv_phy+width*height, frame_buffer, &frame_size, &pyuv_rec, framenum_bs*1000/framerate, &type, bs_remain_len);
        if (ret != 0)
        {
            ERR("Failed to encode frame %d\n", ret);
            break;
        }
        int64_t end = systemTime();
        bs_remain_len += (frame_size << 3);
        bs_remain_len -= bitrate / framerate;
        if (bs_remain_len > (int)bitrate)
        {
            bs_remain_len = bitrate;
        }
        else if (bs_remain_len < 0)
        {
            bs_remain_len = 0;
        }
        unsigned int duration = (unsigned int)((end-start) / 1000000L);


        // psnr
#if defined(CALCULATE_PSNR)
        yvu420sp_to_yuv420p(pyuv_rec, pyuv_rec+width*height, py_rec, pu_rec, pv_rec, width, height);
        psnr_y = psnr(py, py_rec, width, height);
        psnr_u = psnr(pu, pu_rec, width/2, height/2);
        psnr_v = psnr(pv, pv_rec, width/2, height/2);
        psnr_total[0] += psnr_y;
        psnr_total[1] += psnr_u;
        psnr_total[2] += psnr_v;
#endif

        // write header if key frame for MPEG4
        if (format && (type == 0))
        {
            if (fwrite(header_buffer, sizeof(unsigned char), header_size, fp_bs) != header_size)
            {
                break;
            }
            bs_total_len += header_size;
        }


        // write frame
        if (fwrite(frame_buffer, sizeof(unsigned char), frame_size, fp_bs) != frame_size)
        {
            break;
        }
        bs_total_len += frame_size;
        time_total_ms += duration;
        framenum_bs ++;



        INFO("frame %d: time = %dms, type = %s, size = %d ", framenum_bs, duration, type2str(type), frame_size);
#if defined(CALCULATE_PSNR)
        INFO("psnr (%2.2f %2.2f %2.2f)", psnr_y, psnr_u, psnr_v);
#endif
        INFO("\n");


        if (frames != 0)
        {
            if (framenum_bs >= frames)
            {
                break;
            }
        }
    }


    /* step 4 - release vsp */
    enc_release(mHandle);


    INFO("\nFinish encoding %s[%dx%d] to %s(%s, size = %d)\n", filename_yuv, width, height, filename_bs, format2str(format), bs_total_len);
    if (framenum_bs > 0)
    {
        INFO("average time = %dms, average bitrate = %dkbps", time_total_ms/framenum_bs, bs_total_len*8*framerate/framenum_bs/1000);

#if defined(CALCULATE_PSNR)
        INFO(", average psnr (%2.2f %2.2f %2.2f)", psnr_total[0]/framenum_bs, psnr_total[1]/framenum_bs, psnr_total[2]/framenum_bs);
#endif
        INFO("\n");

    }



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

    if (pyuv != NULL)
    {
        pmem_yuv420sp.clear();
        pyuv = NULL;
    }
    if (py_rec != NULL)
    {
        free(py_rec);
        py_rec = NULL;
    }
    if (pu_rec != NULL)
    {
        free(pu_rec);
        pu_rec = NULL;
    }
    if (pv_rec != NULL)
    {
        free(pv_rec);
        pv_rec = NULL;
    }
    if (py != NULL)
    {
        free(py);
        py = NULL;
    }
    if (pu != NULL)
    {
        free(pu);
        pu = NULL;
    }
    if (pv != NULL)
    {
        free(pv);
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


int main(int argc, char **argv)
{
    char* filename_yuv = NULL;
    char* filename_bs = NULL;
    unsigned int width = 0;
    unsigned int height = 0;
    int format = 1;
    unsigned int framerate = 25;
    unsigned int max_key_interval = 30;
    int cbr = 1;
    unsigned int bitrate = 512;
    unsigned int qp = 8;
    unsigned int frames = 0;
    int i;




    /* parse argument */

    if (argc < 9)
    {
        usage();
        return -1;
    }

    for (i=1; i<argc; i++)
    {
        if (strcmp(argv[i], "-i") == 0 && (i < argc-1))
        {
            filename_yuv = argv[++i];
        }
        else if (strcmp(argv[i], "-o") == 0 && (i < argc-1))
        {
            filename_bs = argv[++i];
        }
        else if (strcmp(argv[i], "-w") == 0 && (i < argc-1))
        {
            width = CLIP_16(atoi(argv[++i]));
        }
        else if (strcmp(argv[i], "-h") == 0 && (i < argc-1))
        {
            height = CLIP_16(atoi(argv[++i]));
        }
        else if (strcmp(argv[i], "-format") == 0 && (i < argc-1))
        {
            format = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "-framerate") == 0 && (i < argc-1))
        {
            framerate = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "-max_key_interval") == 0 && (i < argc-1))
        {
            max_key_interval = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "-bitrate") == 0 && (i < argc-1))
        {
            cbr = 1;
            bitrate = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "-qp") == 0 && (i < argc-1))
        {
            cbr = 0;
            qp = atoi(argv[++i]);
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
    if ((filename_yuv == NULL) || (filename_bs == NULL))
    {
        usage();
        return -1;
    }
    if ((width == 0) || (height == 0) || (framerate == 0))
    {
        usage();
        return -1;
    }



    return vsp_enc(filename_yuv, filename_bs, width, height, format, framerate, max_key_interval, cbr, bitrate*1000, qp, frames);
}

