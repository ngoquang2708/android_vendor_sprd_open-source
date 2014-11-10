/*
 * DRM based mode setting test program
 * Copyright 2008 Tungsten Graphics
 *   Jakob Bornecrantz <jakob@tungstengraphics.com>
 * Copyright 2008 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/*
 * This fairly simple test program dumps output in a similar format to the
 * "xrandr" tool everyone knows & loves.  It's necessarily slightly different
 * since the kernel separates outputs into encoder and connector structures,
 * each with their own unique ID.  The program also allows test testing of the
 * memory management and mode setting APIs by allowing the user to specify a
 * connector and mode to use for mode setting.  If all works as expected, a
 * blue background should be painted on the monitor attached to the specified
 * connector after the selected mode is set.
 *
 * TODO: use cairo to write the mode info on the selected output once
 *       the mode has been programmed, along with possible test patterns.
 */
//#include "config.h"

//#include <assert.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/time.h>

#include "ion_sprd.h"
#include "MemoryHeapIon.h"


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
//#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <math.h>

#include <linux/ion.h>
//#include <binder/MemoryHeapIon.h>
//using namespace android;

#ifdef GSP_INTF_SO
#include "gsp_hal.h"
#else
#include "gsp_hal.hpp"
#endif

#include "gralloc_priv.h"
//#include <ui/GraphicBufferAllocator.h>
//#include <ui/GraphicBufferMapper.h>
//#include <ui/Rect.h>



#define ALIGN(x, a) (((x) + (a) - 1) & ~((a) + 1))
#define DEFAULT_SIZE    (1280 * 720 * 4)
#define MAX_CMD     5

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

typedef int32_t (*GSP_PROCESS)(GSP_CONFIG_INFO_T *pgsp_cfg_info);
typedef int32_t (*GSP_GETCAPABILITY)(GSP_CAPABILITY_T *pGsp_cap);

/*
typedef struct
{
    uint16_t    w;
    uint16_t    h;
} GSP_RECT_SIZE_T;

typedef struct
{
    uint16_t    x;
    uint16_t    y;
} GSP_POSITION_T;
*/
typedef enum
{
    GSP_INTF_INVALID,
    GSP_INTF_GET,
    GSP_INTF_PUT,
    GSP_INTF_MAX,
}
GSP_INTF_OPS_E;// the address type of gsp can process

typedef struct
{
    char                    *filename;
    char                    pallet;// pallet enable flag

    char                    format;
    char                    pixel_format;// for alloc buffer
    char                    rotation;
    char                    alpha;
    uint32_t                pixel_w;// pixel size, for add frame

    FILE                    *raw_fp;// raw image file fp
    char                    map_once;// 0:map every time; 1:once


    char                    addr_type;//0 physical; 1 iova
    GSP_DATA_ADDR_T         pa;
    GSP_DATA_ADDR_T         va;
    uint32_t                buffersize;
    //private_handle_t      *Buffer_handle;
    class MemoryHeapIon      *MemoryHeap;


    char                    need_copy;
    char                    addr_type_cpy;//0 physical; 1 iova
    GSP_DATA_ADDR_T         pa_cpy;
    GSP_DATA_ADDR_T         va_cpy;
    uint32_t                buffersize_cpy;
    //private_handle_t      *buffer_handle_cpy;
    class MemoryHeapIon *MemoryHeap_cpy;


    uint32_t size_y;
    uint32_t size_u;
    uint32_t size_v;
    uint32_t size_all;


    GSP_RECT_SIZE_T         pitch;
    GSP_POSITION_T          clip_start;
    GSP_RECT_SIZE_T         clip_size;
    GSP_POSITION_T          out_start;
    GSP_RECT_SIZE_T         out_size;
}
GSP_LAYER_INFO_T;


typedef struct
{
    char                    hold_flag;// don't exit programe when gsp return, to take a special checking
    char                    performance_flag;// test 1000 times to get an average time cost
    char                    power_flag;// take a test at fixed frequence to test power
    //char                    so_flag;// 0:linked gsp hal; 1:load gsp hal so
    GSP_PROCESS      gsp_process;
    GSP_GETCAPABILITY gsp_getCapability;
    char                    map_once;// 0:map every time; 1:once
}
GSP_MISC_INFO_T;


int s_log_out = 1;

//GSP_PROCESS gsp_process = NULL;
//static int s_so_flag = 1;// 0:linked gsp hal; 1:load gsp hal so


static void print_data(uint32_t base,uint32_t c)
{
    uint32_t* pWord = (uint32_t*)base;
    while(c)
    {
        ALOGE("%08x ", *pWord);
        pWord++;
        c--;
    }
    ALOGE("\n");
}


static void usage()
{
    ALOGE("usage:\n");
    ALOGE("sprdgsptest -f0 /data/gsp/640x480_YUV420SP.raw -cf0 4 -pw0 640 -ph0 480 -ix0 0 -iy0 0 -iw0 640 -ih0 480 -rot0 1 -ox0 0 -oy0 0 -ow0 320 -oh0 480 -fd /data/gsp/out/320x480_YUV420SP.raw -cfd 4 -pwd 320 -phd 480\n");
    ALOGE("-fx			string : Layer 0/1/d raw filename\n");
    ALOGE("-cfx		integer: Layer 0/1 raw file format,0-ARGB888 1-XRGB888 2-ARGB565 3-RGB565 4-YUV420_2P 5-YUV420_3P 6-YUV400_1P 7-YUV422_2P\n");
    ALOGE("-cfd		integer: Layer d raw file format,0-ARGB888 1-XRGB888 2-ARGB565 3-RGB565 4-YUV420_2P 5-YUV420_3P 6-YUV422_2P\n");
    ALOGE("-pwx		integer: Layer 0/1/d raw file width\n");
    ALOGE("-phx		integer: Layer 0/1/d raw file height\n");
    ALOGE("-ixx		integer: Layer 0/1 clip region start point x\n");
    ALOGE("-iyx		integer: Layer 0/1 clip region start point y\n");
    ALOGE("-oxx		integer: Layer 0/1 out region start point x\n");
    ALOGE("-oyx		integer: Layer 0/1 out region start point y\n");
    ALOGE("-iwx		integer: Layer 0/1 clip region width\n");
    ALOGE("-ihx		integer: Layer 0/1 clip region height\n");
    ALOGE("-ow0		integer: Layer 0 out region width\n");
    ALOGE("-oh0		integer: Layer 0 out region height\n");
    ALOGE("-rotx		integer: Layer 0/1 rotation angle,0-0 degree,1-90 degree,2=180 degree,3-270 degree\n");
    ALOGE("-btx		integer: Layer 0/1/d buffer type, 0-physical buffer, 1-virtual buffer\n");
    ALOGE("-cbtx		integer: Layer 0/1 copy temp buffer type, 0-physical buffer, 1-virtual buffer\n");
    ALOGE("-cpyx		integer: Layer 0/1 need copy flag\n");
    ALOGE("-help			   : show this help message\n");
    ALOGE("Built on %s %s, Written by Rico.yin(tianci.yin@spreadtrum.com)\n", __DATE__, __TIME__);
}

void print_main_params(int argc, char **argv)
{
    int i;
    ALOGE("argc:%d\n", argc);
    for (i=1; i<argc; i++)
    {
        ALOGE("argv[%d]:%s\n", i, argv[i]);
    }
}

int parse_main_params(int argc, char **argv,
                      GSP_LAYER_INFO_T *pLayer0,
                      GSP_LAYER_INFO_T *pLayer1,
                      GSP_LAYER_INFO_T *pLayerd,
                      GSP_MISC_INFO_T *pMisc)
{
    int i;

    memset((void*)pLayer0,0,sizeof(GSP_LAYER_INFO_T));
    memset((void*)pLayer1,0,sizeof(GSP_LAYER_INFO_T));
    memset((void*)pLayerd,0,sizeof(GSP_LAYER_INFO_T));
    memset((void*)pMisc,0,sizeof(GSP_MISC_INFO_T));

    for (i=1; i<argc; i+=2)
    {
        //ALOGE("%s:%s\n", argv[i], argv[1+i]);
        if (strcmp(argv[i], "-f0") == 0 && (i < argc-1))
        {
            pLayer0->filename = argv[1+i];
        }
        else if (strcmp(argv[i], "-f1") == 0 && (i < argc-1))
        {
            pLayer1->filename = argv[1+i];
        }
        else if (strcmp(argv[i], "-plt0") == 0 && (i < argc-1))
        {
            pLayer0->pallet= atoi(argv[1+i]);
        }
        else if (strcmp(argv[i], "-plt1") == 0 && (i < argc-1))
        {
            pLayer1->pallet= atoi(argv[1+i]);
        }
        else if (strcmp(argv[i], "-fd") == 0 && (i < argc-1))
        {
            pLayerd->filename = argv[1+i];
        }
        else if (strcmp(argv[i], "-cf0") == 0 && (i < argc-1))
        {
            pLayer0->format = atoi(argv[1+i]);
        }
        else if (strcmp(argv[i], "-cf1") == 0 && (i < argc-1))
        {
            pLayer1->format = atoi(argv[1+i]);
        }
        else if (strcmp(argv[i], "-cfd") == 0 && (i < argc-1))
        {
            pLayerd->format = atoi(argv[1+i]);
        }
        else if (strcmp(argv[i], "-pw0") == 0 && (i < argc-1))
        {
            pLayer0->pitch.w = atoi(argv[1+i]);
        }
        else if (strcmp(argv[i], "-ph0") == 0 && (i < argc-1))
        {
            pLayer0->pitch.h = atoi(argv[1+i]);
        }
        else if (strcmp(argv[i], "-pw1") == 0 && (i < argc-1))
        {
            pLayer1->pitch.w = atoi(argv[1+i]);
        }
        else if (strcmp(argv[i], "-ph1") == 0 && (i < argc-1))
        {
            pLayer1->pitch.h = atoi(argv[1+i]);
        }
        else if (strcmp(argv[i], "-pwd") == 0 && (i < argc-1))
        {
            pLayerd->pitch.w = atoi(argv[1+i]);
        }
        else if (strcmp(argv[i], "-phd") == 0 && (i < argc-1))
        {
            pLayerd->pitch.h = atoi(argv[1+i]);
        }
        else if (strcmp(argv[i], "-ix0") == 0 && (i < argc-1))
        {
            pLayer0->clip_start.x = atoi(argv[1+i]);
        }
        else if (strcmp(argv[i], "-iy0") == 0 && (i < argc-1))
        {
            pLayer0->clip_start.y = atoi(argv[1+i]);
        }
        else if (strcmp(argv[i], "-ix1") == 0 && (i < argc-1))
        {
            pLayer1->clip_start.x = atoi(argv[1+i]);
        }
        else if (strcmp(argv[i], "-iy1") == 0 && (i < argc-1))
        {
            pLayer1->clip_start.y = atoi(argv[1+i]);
        }
        else if (strcmp(argv[i], "-ox0") == 0 && (i < argc-1))
        {
            pLayer0->out_start.x = atoi(argv[1+i]);
        }
        else if (strcmp(argv[i], "-oy0") == 0 && (i < argc-1))
        {
            pLayer0->out_start.y = atoi(argv[1+i]);
        }
        else if (strcmp(argv[i], "-ox1") == 0 && (i < argc-1))
        {
            pLayer1->out_start.x = atoi(argv[1+i]);
        }
        else if (strcmp(argv[i], "-oy1") == 0 && (i < argc-1))
        {
            pLayer1->out_start.y = atoi(argv[1+i]);
        }
        else if (strcmp(argv[i], "-iw0") == 0 && (i < argc-1))
        {
            pLayer0->clip_size.w = atoi(argv[1+i]);
        }
        else if (strcmp(argv[i], "-ih0") == 0 && (i < argc-1))
        {
            pLayer0->clip_size.h = atoi(argv[1+i]);
        }
        else if (strcmp(argv[i], "-iw1") == 0 && (i < argc-1))
        {
            pLayer1->clip_size.w = atoi(argv[1+i]);
        }
        else if (strcmp(argv[i], "-ih1") == 0 && (i < argc-1))
        {
            pLayer1->clip_size.h = atoi(argv[1+i]);
        }
        else if (strcmp(argv[i], "-ow0") == 0 && (i < argc-1))
        {
            pLayer0->out_size.w = atoi(argv[1+i]);
        }
        else if (strcmp(argv[i], "-oh0") == 0 && (i < argc-1))
        {
            pLayer0->out_size.h = atoi(argv[1+i]);
        }
        else if (strcmp(argv[i], "-rot0") == 0 && (i < argc-1))
        {
            pLayer0->rotation = atoi(argv[1+i]);
        }
        else if (strcmp(argv[i], "-rot1") == 0 && (i < argc-1))
        {
            pLayer1->rotation = atoi(argv[1+i]);
        }
        else if (strcmp(argv[i], "-bt0") == 0 && (i < argc-1))     // buffer type, 0 physical buffer, 1 iova
        {
            pLayer0->addr_type = atoi(argv[1+i]);
        }
        else if (strcmp(argv[i], "-bt1") == 0 && (i < argc-1))     // buffer type, 0 physical buffer, 1 iova
        {
            pLayer1->addr_type = atoi(argv[1+i]);
        }
        else if (strcmp(argv[i], "-btd") == 0 && (i < argc-1))     // buffer type, 0 physical buffer, 1 iova
        {
            pLayerd->addr_type = atoi(argv[1+i]);
        }
        else if (strcmp(argv[i], "-cbt0") == 0 && (i < argc-1))     //cpy buffer type, 0 physical buffer, 1 iova
        {
            pLayer0->addr_type_cpy = atoi(argv[1+i]);
        }
        else if (strcmp(argv[i], "-cbt1") == 0 && (i < argc-1))     //cpy buffer type, 0 physical buffer, 1 iova
        {
            pLayer1->addr_type_cpy = atoi(argv[1+i]);
        }
        else if (strcmp(argv[i], "-cpy0") == 0 && (i < argc-1))
        {
            pLayer0->need_copy = atoi(argv[1+i]);//copy first
        }
        else if (strcmp(argv[i], "-cpy1") == 0 && (i < argc-1))
        {
            pLayer1->need_copy = atoi(argv[1+i]);//copy first
        }
        else if (strcmp(argv[i], "-hold") == 0 && (i < argc-1))
        {
            pMisc->hold_flag = atoi(argv[1+i]);
        }
        else if (strcmp(argv[i], "-perf") == 0 && (i < argc-1))
        {
            pMisc->performance_flag = atoi(argv[1+i]);
        }
        else if (strcmp(argv[i], "-pwr") == 0 && (i < argc-1))
        {
            pMisc->power_flag = atoi(argv[1+i]);
        }
        else if (strcmp(argv[i], "-mp") == 0 && (i < argc-1))
        {
            pMisc->map_once = atoi(argv[1+i]);
        }
        else if (strcmp(argv[i], "-alpha0") == 0 && (i < argc-1))
        {
            pLayer0->alpha = atoi(argv[1+i]);
        }
        else if (strcmp(argv[i], "-alpha1") == 0 && (i < argc-1))
        {
            pLayer1->alpha = atoi(argv[1+i]);
        }
        else if (strcmp(argv[i], "-help") == 0)
        {
            ALOGE("%s[%d]:%s\n", __func__, __LINE__,argv[i]);
            usage();
            return -1;
        }
        else
        {
            ALOGE("%s[%d]:%s\n", __func__, __LINE__,argv[i]);
            //usage();
            //return -1;
        }
    }
    return 0;
}

void print_layer_params(GSP_LAYER_INFO_T *pLayer)
{
    ALOGE("{%04dx%04d[(%04d,%04d)%04dx%04d]} ==rot:%d alpha:%03d copy:%d==> [(%04d,%04d)%04dx%04d] bufferType:%d format:%d file:%s \n",
          pLayer->pitch.w,
          pLayer->pitch.h,
          pLayer->clip_start.x,
          pLayer->clip_start.y,
          pLayer->clip_size.w,
          pLayer->clip_size.h,
          pLayer->rotation,
          pLayer->alpha,
          pLayer->need_copy,
          pLayer->out_start.x,
          pLayer->out_start.y,
          pLayer->out_size.w,
          pLayer->out_size.h,
          pLayer->addr_type,
          pLayer->format,
          pLayer->filename);
}

void print_misc_params(GSP_MISC_INFO_T *pMisc)
{
    ALOGE("%s[%d],performance_flag:%d, power_flag:%d, hold_flag:%d, layers params:\n", __func__, __LINE__,
          pMisc->performance_flag,
          pMisc->power_flag,
          pMisc->hold_flag);
}
int calc_input_plane_size(GSP_LAYER_INFO_T *pLayer)
{
    switch(pLayer->format)
    {
        case GSP_SRC_FMT_ARGB888:
        case GSP_SRC_FMT_RGB888:
            pLayer->pixel_w = 4;
            pLayer->size_y = pLayer->pitch.w * pLayer->pitch.h * 4;
            pLayer->size_u = 0;
            pLayer->size_v = 0;
            //pLayer->pixel_format = HAL_PIXEL_FORMAT_RGBA_8888;
            break;
        case GSP_SRC_FMT_ARGB565:
            pLayer->pixel_w = 2;
            pLayer->size_y = pLayer->pitch.w * pLayer->pitch.h * 2;
            pLayer->size_u = pLayer->pitch.w * pLayer->pitch.h;
            pLayer->size_v = 0;
            //pLayer->pixel_format = HAL_PIXEL_FORMAT_RGB_565;
            break;
        case GSP_SRC_FMT_RGB565:
            pLayer->pixel_w = 2;
            pLayer->size_y = pLayer->pitch.w * pLayer->pitch.h * 2;
            pLayer->size_u = 0;
            pLayer->size_v = 0;
            //pLayer->pixel_format = HAL_PIXEL_FORMAT_RGB_565;
            break;
        case GSP_SRC_FMT_YUV420_2P:
            pLayer->pixel_w = 1;
            pLayer->size_y = pLayer->pitch.w * pLayer->pitch.h;
            pLayer->size_u = pLayer->size_y/2;
            pLayer->size_v = 0;
            //pLayer->pixel_format = HAL_PIXEL_FORMAT_YCbCr_420_SP;
            break;
        case GSP_SRC_FMT_YUV420_3P:
            pLayer->pixel_w = 1;
            pLayer->size_y = pLayer->pitch.w * pLayer->pitch.h;
            pLayer->size_u = pLayer->size_y/4;
            pLayer->size_v = pLayer->size_u;
            //pLayer->pixel_format = HAL_PIXEL_FORMAT_YCbCr_420_SP;
            break;
        case GSP_SRC_FMT_YUV400_1P:
            pLayer->pixel_w = 1;
            pLayer->size_y = pLayer->pitch.w * pLayer->pitch.h;
            pLayer->size_u = 0;
            pLayer->size_v = 0;
            //pLayer->pixel_format = HAL_PIXEL_FORMAT_YCbCr_420_SP;
            break;
        case GSP_SRC_FMT_YUV422_2P:
            pLayer->pixel_w = 1;
            pLayer->size_y = pLayer->pitch.w * pLayer->pitch.h;
            pLayer->size_u = pLayer->size_y;
            pLayer->size_v = 0;
            //pLayer->pixel_format = HAL_PIXEL_FORMAT_YCbCr_422_SP;
            break;
        case GSP_SRC_FMT_8BPP:
            pLayer->pixel_w = 1;
            pLayer->size_y = pLayer->pitch.w * pLayer->pitch.h;
            pLayer->size_u = 0;
            pLayer->size_v = 0;
            //pLayer->pixel_format = HAL_PIXEL_FORMAT_YCbCr_420_SP;
            break;
        default:
            return -1;//testing not support the other color format
            break;
    }

    pLayer->size_all = pLayer->size_y + pLayer->size_u + pLayer->size_v;
    return 0;
}

int calc_output_plane_size(GSP_LAYER_INFO_T *pLayer)
{
    switch(pLayer->format)
    {
        case GSP_DST_FMT_ARGB888:
        case GSP_DST_FMT_RGB888:
            pLayer->pixel_w = 4;
            pLayer->size_y = pLayer->pitch.w * pLayer->pitch.h * 4;
            pLayer->size_u = 0;
            pLayer->size_v = 0;
            //pLayer->pixel_format = HAL_PIXEL_FORMAT_RGBA_8888;
            break;
        case GSP_DST_FMT_ARGB565:
            pLayer->pixel_w = 2;
            pLayer->size_y = pLayer->pitch.w * pLayer->pitch.h * 2;
            pLayer->size_u = pLayer->pitch.w * pLayer->pitch.h;
            pLayer->size_v = 0;
            //pLayer->pixel_format = HAL_PIXEL_FORMAT_RGB_565;
            break;
        case GSP_DST_FMT_RGB565:
            pLayer->pixel_w = 2;
            pLayer->size_y = pLayer->pitch.w * pLayer->pitch.h * 2;
            pLayer->size_u = 0;
            pLayer->size_v = 0;
            //pLayer->pixel_format = HAL_PIXEL_FORMAT_RGB_565;
            break;
        case GSP_DST_FMT_YUV420_2P:
            pLayer->pixel_w = 1;
            pLayer->size_y = pLayer->pitch.w * pLayer->pitch.h;
            pLayer->size_u = pLayer->size_y/2;
            pLayer->size_v = 0;
            //pLayer->pixel_format = HAL_PIXEL_FORMAT_YCbCr_420_SP;
            break;
        case GSP_DST_FMT_YUV420_3P:
            pLayer->pixel_w = 1;
            pLayer->size_y = pLayer->pitch.w * pLayer->pitch.h;
            pLayer->size_u = pLayer->size_y/4;
            pLayer->size_v = pLayer->size_u;
            //pLayer->pixel_format = HAL_PIXEL_FORMAT_YCbCr_420_SP;
            break;
        case GSP_DST_FMT_YUV422_2P:
            pLayer->pixel_w = 1;
            pLayer->size_y = pLayer->pitch.w * pLayer->pitch.h;
            pLayer->size_u = pLayer->size_y;
            pLayer->size_v = 0;
            //pLayer->pixel_format = HAL_PIXEL_FORMAT_YCbCr_422_SP;
            break;
        default:
            ALOGE("not supported format!");
            return -1;//testing not support the other color format
    }

    pLayer->size_all = pLayer->size_y + pLayer->size_u + pLayer->size_v;
    return 0;
}



static void add_frame_to_rgba(char* base,uint32_t w,uint32_t h)
{
    uint32_t v=0xffffffff,r=0,c0=0,c1=0,c2=0,c3=0,c4=0;
    uint32_t* base_walk = (uint32_t*)base;
    uint32_t first_r=0,second_r=16;
    uint32_t first_c=0,second_c=16;

    memset(base_walk+w*first_r,         v,w*4); // 0
    memset(base_walk+w*second_r,        v,w*4); // 10
    memset(base_walk+(w*h>>1),          v,w*4);
    memset(base_walk+w*(h-1-second_r),  v,w*4);
    memset(base_walk+w*(h-1-first_r),   v,w*4);

    base_walk = (uint32_t*)base;
    r=0;
    c0=first_c;
    c1=second_c;
    c2=(w>>1);
    c3=w-1-second_c;
    c4=w-1-first_c;
    while(r < h)
    {
        *(base_walk+c0) = v;
        *(base_walk+c1) = v;
        *(base_walk+c2) = v;
        *(base_walk+c3) = v;
        *(base_walk+c4) = v;
        base_walk += w;
        r++;
    }
}

static void add_frame_to_y(char* base,uint32_t w,uint32_t h)
{
    uint32_t r=0,c0=0,c1=0,c2=0,c3=0,c4=0;
    char* base_walk = base;
    uint32_t first_r=0,second_r=16;
    uint32_t first_c=0,second_c=16;

    memset(base_walk+w*first_r,            0xff,w); // 0
    memset(base_walk+w*second_r,        0xff,w); // 10
    memset(base_walk+(w*h>>1),    0xff,w);
    memset(base_walk+w*(h-1-second_r),    0xff,w);
    memset(base_walk+w*(h-1-first_r),    0xff,w);

    base_walk = base;
    r=0;
    c0=first_c;
    c1=second_c;
    c2=(w>>1);
    c3=w-1-second_c;
    c4=w-1-first_c;
    while(r < h)
    {
        *(base_walk+c0) = 0xff;
        *(base_walk+c1) = 0xff;
        *(base_walk+c2) = 0xff;
        *(base_walk+c3) = 0xff;
        *(base_walk+c4) = 0xff;
        base_walk += w;
        r++;
    }
}


int iommu_map_if_need(GSP_LAYER_INFO_T *pLayer)
{
    unsigned long       mmu_addr = 0;
    size_t      temp_size = 0;

    if(pLayer == NULL) return 0;

    if(pLayer->addr_type == 1)   //iova buffer
    {
        if(pLayer->map_once == 0 && pLayer->MemoryHeap != NULL)
        {
            if((pLayer->MemoryHeap->get_gsp_iova(&mmu_addr, &pLayer->buffersize) == 0) && (mmu_addr != 0) && (pLayer->buffersize > 0))
            {
                pLayer->pa.addr_y = mmu_addr;
                pLayer->pa.addr_uv = pLayer->pa.addr_y + pLayer->size_y;
                pLayer->pa.addr_v = pLayer->pa.addr_uv + pLayer->size_u;
                ALOGE_IF(s_log_out,"[%d] map iommu addr success! %p\n",__LINE__,(void*)mmu_addr);
            }
            else
            {
                ALOGE("[%d] map iommu addr failed!\n",__LINE__);
                return -1;
            }
        }
    }

    if(pLayer->need_copy == 1)
    {
        if(pLayer->addr_type_cpy == 1)   //iova buffer
        {
            if(pLayer->map_once == 0 && pLayer->MemoryHeap_cpy != NULL)
            {
                if((pLayer->MemoryHeap_cpy->get_gsp_iova(&mmu_addr, &pLayer->buffersize_cpy) == 0) && (mmu_addr != 0) && (pLayer->buffersize_cpy > 0))
                {
                    pLayer->pa_cpy.addr_y = mmu_addr;
                    pLayer->pa_cpy.addr_uv = pLayer->pa_cpy.addr_y + pLayer->size_y;
                    pLayer->pa_cpy.addr_v = pLayer->pa_cpy.addr_uv + pLayer->size_u;
                    ALOGE_IF(s_log_out,"[%d] map iommu cpy addr success! %p\n",__LINE__,(void*)mmu_addr);
                }
                else
                {
                    ALOGE("[%d] map iommu cpy addr failed!\n",__LINE__);
                    return -1;
                }
            }
        }
    }
    return 0;
}

int iommu_unmap_if_need(GSP_LAYER_INFO_T *pLayer)
{
    if(pLayer == NULL) return 0;

    if(pLayer->MemoryHeap)
    {
        if(pLayer->addr_type == 1)   //iova buffer
        {
            if(pLayer->pa.addr_y && (pLayer->map_once == 0))
            {
                pLayer->MemoryHeap->free_gsp_iova(pLayer->pa.addr_y, pLayer->buffersize);
                pLayer->pa.addr_y = 0;
                ALOGE_IF(s_log_out,"[%d] unmap iommu addr success!\n",__LINE__);
            }
        }
    }

    if(pLayer->need_copy == 1)
    {
        if(pLayer->MemoryHeap_cpy)
        {
            if(pLayer->addr_type_cpy == 1)   //iova buffer
            {
                if(pLayer->pa_cpy.addr_y && (pLayer->map_once == 0))
                {
                    pLayer->MemoryHeap_cpy->free_gsp_iova(pLayer->pa_cpy.addr_y, pLayer->buffersize_cpy);
                    pLayer->pa_cpy.addr_y = 0;
                    ALOGE_IF(s_log_out,"[%d] unmap iommu cpy addr success!\n",__LINE__);
                }
            }
        }
    }
    return 0;
}

int alloc_buffer(GSP_LAYER_INFO_T *pLayer)
{
    unsigned long       mmu_addr = 0;
    size_t      temp_size = 0;

    //alloc none cached and buffered memory
    if(pLayer->addr_type == 0)   //physical buffer
    {
        ALOGE("[%d] alloc phy buffer \n",__LINE__);
        pLayer->MemoryHeap = new MemoryHeapIon("/dev/ion", pLayer->size_all, NO_CACHING, ION_HEAP_ID_MASK_OVERLAY);//GRALLOC_USAGE_OVERLAY_BUFFER ION_HEAP_CARVEOUT_MASK
        pLayer->MemoryHeap->get_phy_addr_from_ion((unsigned long *)&pLayer->pa.addr_y, &temp_size);
    }
    else     // iova
    {
        ALOGE("[%d] alloc virt buffer \n",__LINE__);
        pLayer->MemoryHeap = new MemoryHeapIon("/dev/ion", pLayer->size_all, NO_CACHING, ION_HEAP_ID_MASK_SYSTEM);
        if(pLayer->map_once == 1)
        {
            if((pLayer->MemoryHeap->get_gsp_iova(&mmu_addr, &pLayer->buffersize) == 0) && (mmu_addr != 0) && (pLayer->buffersize > 0))
            {
                pLayer->pa.addr_y = mmu_addr;
                ALOGE_IF(s_log_out,"[%d] map iommu addr success! %p\n",__LINE__,(void*)mmu_addr);
            }
            else
            {
                ALOGE("[%d] map buffer0 iommu addr failed!\n",__LINE__);
                return -1;
            }
        }
    }
    pLayer->pa.addr_uv = pLayer->pa.addr_y + pLayer->size_y;
    pLayer->pa.addr_v = pLayer->pa.addr_uv + pLayer->size_u;

    pLayer->va.addr_y = (uint32_t)pLayer->MemoryHeap->get_virt_addr_from_ion();
    if(pLayer->va.addr_y == 0) return -1;
    pLayer->va.addr_uv = pLayer->va.addr_y + pLayer->size_y;
    pLayer->va.addr_v = pLayer->va.addr_uv + pLayer->size_u;
    memset((void*)pLayer->va.addr_y,0,pLayer->size_all);

    if(pLayer->need_copy == 1)
    {
        ALOGE("[%d] alloc cpy buffer \n",__LINE__);
        if(pLayer->addr_type_cpy == 0)   //physical buffer
        {
            ALOGE("[%d] alloc cpy phy buffer \n",__LINE__);
            pLayer->MemoryHeap_cpy = new MemoryHeapIon("/dev/ion", pLayer->size_all, NO_CACHING, ION_HEAP_ID_MASK_OVERLAY);//GRALLOC_USAGE_OVERLAY_BUFFER ION_HEAP_CARVEOUT_MASK
            pLayer->MemoryHeap_cpy->get_phy_addr_from_ion((unsigned long *)&pLayer->pa_cpy.addr_y, &temp_size);
        }
        else     // iova
        {
            ALOGE("[%d] alloc cpy virt buffer \n",__LINE__);
            pLayer->MemoryHeap_cpy = new MemoryHeapIon("/dev/ion", pLayer->size_all, NO_CACHING, ION_HEAP_ID_MASK_SYSTEM);
            if(pLayer->map_once == 1)
            {
                if((pLayer->MemoryHeap_cpy->get_gsp_iova(&mmu_addr, &pLayer->buffersize_cpy) == 0) && (mmu_addr != 0) && (pLayer->buffersize_cpy > 0))
                {
                    pLayer->pa_cpy.addr_y = mmu_addr;
                    ALOGE_IF(s_log_out,"[%d] map buffer0 iommu addr success!\n",__LINE__);
                }
                else
                {
                    ALOGE("[%d] map buffer0 iommu addr failed!\n",__LINE__);
                    return -1;
                }
            }
        }
        pLayer->pa_cpy.addr_uv = pLayer->pa_cpy.addr_y + pLayer->size_y;
        pLayer->pa_cpy.addr_v = pLayer->pa_cpy.addr_uv + pLayer->size_u;

        pLayer->va_cpy.addr_y = (uint32_t)pLayer->MemoryHeap_cpy->get_virt_addr_from_ion();
        if(pLayer->va_cpy.addr_y == 0) return -1;
        pLayer->va_cpy.addr_uv = pLayer->va_cpy.addr_y + pLayer->size_y;
        pLayer->va_cpy.addr_v = pLayer->va_cpy.addr_uv + pLayer->size_u;
        memset((void*)pLayer->va_cpy.addr_y,0,pLayer->size_all);
    }
    return 0;
}


int free_buffer(GSP_LAYER_INFO_T *pLayer)
{
    //alloc none cached and buffered memory
    if(pLayer->MemoryHeap)
    {
        if(pLayer->addr_type == 1)   //iova buffer
        {
            if(pLayer->pa.addr_y && (pLayer->map_once == 1))
            {
                pLayer->MemoryHeap->free_gsp_iova(pLayer->pa.addr_y, pLayer->buffersize);
            }
        }
        delete pLayer->MemoryHeap;
        pLayer->MemoryHeap = NULL;
        pLayer->pa.addr_y = 0;
        pLayer->va.addr_y = 0;
        pLayer->buffersize = 0;
    }

    if(pLayer->need_copy == 1)
    {
        if(pLayer->MemoryHeap_cpy)
        {
            if(pLayer->addr_type_cpy == 1)   //iova buffer
            {
                if(pLayer->pa_cpy.addr_y && (pLayer->map_once == 1))
                {
                    pLayer->MemoryHeap_cpy->free_gsp_iova(pLayer->pa_cpy.addr_y, pLayer->buffersize_cpy);
                }
            }
            delete pLayer->MemoryHeap_cpy;
            pLayer->MemoryHeap_cpy = NULL;
            pLayer->pa_cpy.addr_y = 0;
            pLayer->va_cpy.addr_y = 0;
            pLayer->buffersize_cpy = 0;
        }
    }
    return 0;
}

int open_raw_file(GSP_LAYER_INFO_T *pLayer,const char *pFlag)
{
    if(pLayer->filename)
    {
        pLayer->raw_fp = fopen(pLayer->filename, pFlag);
        if (pLayer->raw_fp == NULL)
        {
            ALOGE("Failed to open raw_file %s!\n", pLayer->filename);
            return -1;
        }
        else
        {
            ALOGE("open raw_file %s success\n", pLayer->filename);
        }
    }
    else
    {
        ALOGE("raw file name is null\n");
    }
    return 0;
}

int read_raw_file(GSP_LAYER_INFO_T *pLayer)
{
    if(pLayer->va.addr_y != 0 && pLayer->raw_fp != NULL)
    {
        if (fread((void*)pLayer->va.addr_y, sizeof(unsigned char), pLayer->size_all, pLayer->raw_fp) != pLayer->size_all)
        {
            ALOGE("Failed to read raw_file: %s\n", pLayer->filename);
            return -1;
        }
        else
        {
            ALOGE("read raw_file %s success\n", pLayer->filename);
            print_data(pLayer->va.addr_y,16);
        }
    }
    return 0;
}

int write_raw_file(GSP_LAYER_INFO_T *pLayer)
{
    if(pLayer->va.addr_y != 0 && pLayer->raw_fp != NULL)
    {
        if (fwrite((void*)pLayer->va.addr_y, sizeof(unsigned char), pLayer->size_all, pLayer->raw_fp) != pLayer->size_all)
        {
            ALOGE("Failed to read raw_file: %s\n", pLayer->filename);
            return -1;
        }
        else
        {
            ALOGE("write raw_file %s success\n", pLayer->filename);
            print_data(pLayer->va.addr_y,16);
        }
    }
    return 0;
}

int close_raw_file(GSP_LAYER_INFO_T *pLayer)
{
    if (pLayer->raw_fp != NULL)
    {
        fclose(pLayer->raw_fp);
        pLayer->raw_fp = NULL;
    }
    return 0;
}

int add_frame_boundary(GSP_LAYER_INFO_T *pLayer)
{
    if(pLayer->va.addr_y != 0 && pLayer->pitch.w != 0 && pLayer->pitch.h != 0)
    {
        if(pLayer->pixel_w == 4)
        {
            add_frame_to_rgba((char*)pLayer->va.addr_y,pLayer->pitch.w,pLayer->pitch.h);
        }
        else if(pLayer->pixel_w == 1)
        {
            add_frame_to_y((char*)pLayer->va.addr_y,pLayer->pitch.w,pLayer->pitch.h);
        }
        ALOGE("add white frame success\n");
    }
    else
    {
        ALOGE("not add white frame.\n");
        return -1;
    }
    return 0;
}

int set_gsp_cfg_info(GSP_CONFIG_INFO_T *pgsp_cfg_info,
                     GSP_LAYER_INFO_T *pLayer0,
                     GSP_LAYER_INFO_T *pLayer1,
                     GSP_LAYER_INFO_T *pLayerd)
{
//#define GSP_BUFFER_FD
    if(pLayer0->size_all > 0)
    {
#ifndef GSP_BUFFER_FD
        pgsp_cfg_info->layer0_info.src_addr.addr_y = pLayer0->pa.addr_y;
        pgsp_cfg_info->layer0_info.src_addr.addr_v =
            pgsp_cfg_info->layer0_info.src_addr.addr_uv = pLayer0->pa.addr_y+pLayer0->pitch.w*pLayer0->pitch.h;
#else
        pgsp_cfg_info->layer0_info.mem_info.is_pa = !pLayer0->addr_type;
        pgsp_cfg_info->layer0_info.mem_info.share_fd = (int)pLayer0->MemoryHeap->mIonHandle;
        pgsp_cfg_info->layer0_info.mem_info.uv_offset = pLayer0->pitch.w*pLayer0->pitch.h;
        pgsp_cfg_info->layer0_info.mem_info.v_offset = pgsp_cfg_info->layer0_info.mem_info.uv_offset;
#endif
        pgsp_cfg_info->layer0_info.img_format = (GSP_LAYER_SRC_DATA_FMT_E)pLayer0->format;
        pgsp_cfg_info->layer0_info.clip_rect.st_x = pLayer0->clip_start.x;
        pgsp_cfg_info->layer0_info.clip_rect.st_y = pLayer0->clip_start.y;
        pgsp_cfg_info->layer0_info.clip_rect.rect_w = pLayer0->clip_size.w;
        pgsp_cfg_info->layer0_info.clip_rect.rect_h = pLayer0->clip_size.h;
        pgsp_cfg_info->layer0_info.rot_angle = (GSP_ROT_ANGLE_E)pLayer0->rotation;
        pgsp_cfg_info->layer0_info.des_rect.st_x = pLayer0->out_start.x;
        pgsp_cfg_info->layer0_info.des_rect.st_y = pLayer0->out_start.y;
        pgsp_cfg_info->layer0_info.des_rect.rect_w = pLayer0->out_size.w;
        pgsp_cfg_info->layer0_info.des_rect.rect_h = pLayer0->out_size.h;
        pgsp_cfg_info->layer0_info.pitch = pLayer0->pitch.w;
        pgsp_cfg_info->layer0_info.alpha = (pLayer0->alpha==0)?0xff:pLayer0->alpha;
        pgsp_cfg_info->layer0_info.layer_en = 1;

        if(pLayer0->filename == NULL || pLayer0->pallet)
        {
            pgsp_cfg_info->layer0_info.pallet_en = 1;
            pgsp_cfg_info->layer0_info.grey.a_val = 255;
            pgsp_cfg_info->layer0_info.grey.r_val = 255;
            pgsp_cfg_info->layer0_info.grey.g_val = 0;
            pgsp_cfg_info->layer0_info.grey.b_val = 0;
            //pgsp_cfg_info->layer0_info.alpha = 255;
        }
    }

    if(pLayer1->size_all > 0)
    {
#ifndef GSP_BUFFER_FD
        pgsp_cfg_info->layer1_info.src_addr.addr_y = pLayer1->pa.addr_y;
        pgsp_cfg_info->layer1_info.src_addr.addr_v =
            pgsp_cfg_info->layer1_info.src_addr.addr_uv = pLayer1->pa.addr_y+pLayer1->pitch.w*pLayer1->pitch.h;
#else
        pgsp_cfg_info->layer1_info.mem_info.is_pa = !pLayer1->addr_type;
        pgsp_cfg_info->layer1_info.mem_info.share_fd = (int)pLayer1->MemoryHeap->mIonHandle;
        pgsp_cfg_info->layer1_info.mem_info.uv_offset = pLayer1->pitch.w*pLayer1->pitch.h;
        pgsp_cfg_info->layer1_info.mem_info.v_offset = pgsp_cfg_info->layer1_info.mem_info.uv_offset;
#endif
        pgsp_cfg_info->layer1_info.img_format = (GSP_LAYER_SRC_DATA_FMT_E)pLayer1->format;
        pgsp_cfg_info->layer1_info.clip_rect.st_x = pLayer1->clip_start.x;
        pgsp_cfg_info->layer1_info.clip_rect.st_y = pLayer1->clip_start.y;
        pgsp_cfg_info->layer1_info.clip_rect.rect_w = pLayer1->clip_size.w;
        pgsp_cfg_info->layer1_info.clip_rect.rect_h = pLayer1->clip_size.h;
        pgsp_cfg_info->layer1_info.rot_angle = (GSP_ROT_ANGLE_E)pLayer1->rotation;
        pgsp_cfg_info->layer1_info.des_pos.pos_pt_x = pLayer1->out_start.x;
        pgsp_cfg_info->layer1_info.des_pos.pos_pt_y = pLayer1->out_start.y;
        pgsp_cfg_info->layer1_info.pitch = pLayer1->pitch.w;
        pgsp_cfg_info->layer1_info.alpha = (pLayer1->alpha==0)?0xff:pLayer1->alpha;
        pgsp_cfg_info->layer1_info.layer_en = pLayer1->size_all?1:0;
        if(pLayer1->filename == NULL || pLayer1->pallet)
        {
            pgsp_cfg_info->layer1_info.pallet_en = 1;
            pgsp_cfg_info->layer1_info.grey.a_val = 0;
            pgsp_cfg_info->layer1_info.grey.r_val = 0;
            pgsp_cfg_info->layer1_info.grey.g_val = 0;
            pgsp_cfg_info->layer1_info.grey.b_val = 0;
            pgsp_cfg_info->layer1_info.src_addr.addr_y = pgsp_cfg_info->layer0_info.src_addr.addr_y;
            pgsp_cfg_info->layer1_info.src_addr.addr_v = pgsp_cfg_info->layer0_info.src_addr.addr_uv;
            pgsp_cfg_info->layer1_info.src_addr.addr_uv = pgsp_cfg_info->layer0_info.src_addr.addr_v;
        }
    }

    if(pLayer0->size_all > 0 || pLayer1->size_all > 0)
    {
        pgsp_cfg_info->layer_des_info.img_format = (GSP_LAYER_DST_DATA_FMT_E)pLayerd->format;//GSP_DST_FMT_YUV420_2P;
        pgsp_cfg_info->layer_des_info.pitch = pLayerd->pitch.w;
#ifndef GSP_BUFFER_FD
        pgsp_cfg_info->layer_des_info.src_addr.addr_y = pLayerd->pa.addr_y;
        pgsp_cfg_info->layer_des_info.src_addr.addr_v =
            pgsp_cfg_info->layer_des_info.src_addr.addr_uv = pLayerd->pa.addr_y+pLayerd->pitch.h*pLayerd->pitch.w;
#else
        pgsp_cfg_info->layer_des_info.mem_info.is_pa = !pLayerd->addr_type;
        pgsp_cfg_info->layer_des_info.mem_info.share_fd = (int)pLayerd->MemoryHeap->mIonHandle;
        pgsp_cfg_info->layer_des_info.mem_info.uv_offset = pLayerd->pitch.w*pLayerd->pitch.h;
        pgsp_cfg_info->layer_des_info.mem_info.v_offset = pgsp_cfg_info->layer_des_info.mem_info.uv_offset;
#endif
    }
    ALOGE("L0 {%04dx%04d[(%04d,%04d)%04dx%04d]} ==rot:%d alpha:%03d copy:%d==> [(%04d,%04d)%04dx%04d] bufferType:%d format:%d \n",
          pgsp_cfg_info->layer0_info.pitch,
          0,
          pgsp_cfg_info->layer0_info.clip_rect.st_x,
          pgsp_cfg_info->layer0_info.clip_rect.st_y,
          pgsp_cfg_info->layer0_info.clip_rect.rect_w,
          pgsp_cfg_info->layer0_info.clip_rect.rect_h,
          pgsp_cfg_info->layer0_info.rot_angle,
          pgsp_cfg_info->layer0_info.alpha,
          0,
          pgsp_cfg_info->layer0_info.des_rect.st_x,
          pgsp_cfg_info->layer0_info.des_rect.st_y,
          pgsp_cfg_info->layer0_info.des_rect.rect_w,
          pgsp_cfg_info->layer0_info.des_rect.rect_h,
          0,
          pgsp_cfg_info->layer0_info.img_format);
    ALOGE("L1 {%04dx%04d[(%04d,%04d)%04dx%04d]} ==rot:%d alpha:%03d copy:%d==> [(%04d,%04d)%04dx%04d] bufferType:%d format:%d \n",
          pgsp_cfg_info->layer1_info.pitch,
          0,
          pgsp_cfg_info->layer1_info.clip_rect.st_x,
          pgsp_cfg_info->layer1_info.clip_rect.st_y,
          pgsp_cfg_info->layer1_info.clip_rect.rect_w,
          pgsp_cfg_info->layer1_info.clip_rect.rect_h,
          pgsp_cfg_info->layer1_info.rot_angle,
          pgsp_cfg_info->layer1_info.alpha,
          0,
          pgsp_cfg_info->layer1_info.des_pos.pos_pt_x,
          pgsp_cfg_info->layer1_info.des_pos.pos_pt_y,
          0,
          0,
          0,
          pgsp_cfg_info->layer1_info.img_format);

    return 0;
}

int gsp_cpy_process(GSP_CONFIG_INFO_T *pgsp_cfg_info,
                    GSP_LAYER_INFO_T *pLayer0,
                    GSP_LAYER_INFO_T *pLayer1,
                    GSP_MISC_INFO_T *pMisc)
{
    GSP_CONFIG_INFO_T gsp_cfg_info_cpy = *pgsp_cfg_info;
    int ret = 0;

    if(pLayer0->need_copy)
    {
        if(gsp_cfg_info_cpy.layer0_info.layer_en == 1)
        {
            gsp_cfg_info_cpy.layer1_info.layer_en = 0;
            gsp_cfg_info_cpy.layer0_info.clip_rect.rect_w = gsp_cfg_info_cpy.layer0_info.pitch;
            gsp_cfg_info_cpy.layer0_info.clip_rect.st_x = 0;
            gsp_cfg_info_cpy.layer0_info.clip_rect.rect_h += gsp_cfg_info_cpy.layer0_info.clip_rect.st_y;
            gsp_cfg_info_cpy.layer0_info.clip_rect.st_y = 0;

            gsp_cfg_info_cpy.layer_des_info.src_addr.addr_y = pLayer0->pa_cpy.addr_y;
            gsp_cfg_info_cpy.layer_des_info.src_addr.addr_uv =
                gsp_cfg_info_cpy.layer_des_info.src_addr.addr_v =
                    gsp_cfg_info_cpy.layer_des_info.src_addr.addr_y +
                    gsp_cfg_info_cpy.layer0_info.clip_rect.rect_w*gsp_cfg_info_cpy.layer0_info.clip_rect.rect_h;
            gsp_cfg_info_cpy.layer0_info.des_rect = gsp_cfg_info_cpy.layer0_info.clip_rect;
            gsp_cfg_info_cpy.layer0_info.rot_angle = GSP_ROT_ANGLE_0;
            gsp_cfg_info_cpy.layer_des_info.img_format = (GSP_LAYER_DST_DATA_FMT_E)gsp_cfg_info_cpy.layer0_info.img_format;
            memset(&gsp_cfg_info_cpy.layer0_info.endian_mode,0,sizeof(gsp_cfg_info_cpy.layer0_info.endian_mode));
            gsp_cfg_info_cpy.layer_des_info.endian_mode = gsp_cfg_info_cpy.layer0_info.endian_mode;
            gsp_cfg_info_cpy.layer_des_info.pitch = gsp_cfg_info_cpy.layer0_info.clip_rect.rect_w;
            gsp_cfg_info_cpy.misc_info.split_pages = 1;//
            //ALOGE("copy0 L:%d\n", __LINE__);
            ret = (*pMisc->gsp_process)(&gsp_cfg_info_cpy);
            if(ret == 0)
            {
                if(gsp_cfg_info_cpy.layer0_info.layer_en == 1)
                {
                    pgsp_cfg_info->layer0_info.src_addr.addr_y = gsp_cfg_info_cpy.layer_des_info.src_addr.addr_y;
                    pgsp_cfg_info->layer0_info.src_addr.addr_uv =
                        pgsp_cfg_info->layer0_info.src_addr.addr_v = gsp_cfg_info_cpy.layer_des_info.src_addr.addr_v;
                }
            }
            else
            {
                return -1;
            }
        }
    }


    gsp_cfg_info_cpy = *pgsp_cfg_info;
    if(pLayer1->need_copy)
    {

        if(gsp_cfg_info_cpy.layer1_info.layer_en == 1)
        {
            gsp_cfg_info_cpy.layer0_info.layer_en = 0;
            gsp_cfg_info_cpy.layer1_info.clip_rect.rect_w = gsp_cfg_info_cpy.layer1_info.pitch;
            gsp_cfg_info_cpy.layer1_info.clip_rect.st_x = 0;
            gsp_cfg_info_cpy.layer1_info.clip_rect.rect_h += gsp_cfg_info_cpy.layer1_info.clip_rect.st_y;
            gsp_cfg_info_cpy.layer1_info.clip_rect.st_y = 0;

            gsp_cfg_info_cpy.layer_des_info.src_addr.addr_y = pLayer1->pa_cpy.addr_y;
            gsp_cfg_info_cpy.layer_des_info.src_addr.addr_uv =
                gsp_cfg_info_cpy.layer_des_info.src_addr.addr_v =
                    gsp_cfg_info_cpy.layer_des_info.src_addr.addr_y +
                    gsp_cfg_info_cpy.layer1_info.clip_rect.rect_w*gsp_cfg_info_cpy.layer1_info.clip_rect.rect_h;
            //gsp_cfg_info_cpy.layer1_info.des_rect = gsp_cfg_info_cpy.layer1_info.clip_rect;
            gsp_cfg_info_cpy.layer1_info.des_pos.pos_pt_x =
                gsp_cfg_info_cpy.layer1_info.des_pos.pos_pt_y = 0;
            gsp_cfg_info_cpy.layer1_info.rot_angle = GSP_ROT_ANGLE_0;
            gsp_cfg_info_cpy.layer_des_info.img_format = (GSP_LAYER_DST_DATA_FMT_E)gsp_cfg_info_cpy.layer1_info.img_format;
            memset(&gsp_cfg_info_cpy.layer1_info.endian_mode,0,sizeof(gsp_cfg_info_cpy.layer1_info.endian_mode));
            gsp_cfg_info_cpy.layer_des_info.endian_mode = gsp_cfg_info_cpy.layer1_info.endian_mode;
            gsp_cfg_info_cpy.layer_des_info.pitch = gsp_cfg_info_cpy.layer1_info.clip_rect.rect_w;
            gsp_cfg_info_cpy.misc_info.split_pages = 1;//
            //ALOGE("copy1 L:%d\n", __LINE__);
            ret = (*pMisc->gsp_process)(&gsp_cfg_info_cpy);
            if(ret == 0)
            {
                if(gsp_cfg_info_cpy.layer0_info.layer_en == 1)
                {
                    pgsp_cfg_info->layer1_info.src_addr.addr_y = gsp_cfg_info_cpy.layer_des_info.src_addr.addr_y;
                    pgsp_cfg_info->layer1_info.src_addr.addr_uv =
                        pgsp_cfg_info->layer1_info.src_addr.addr_v = gsp_cfg_info_cpy.layer_des_info.src_addr.addr_v;
                }
            }
            else
            {
                return -1;
            }
        }
    }

    return 0;
}

static int64_t systemTime()
{
    struct timespec t;
    t.tv_sec = t.tv_nsec = 0;
    clock_gettime(CLOCK_MONOTONIC, &t);
    ALOGI_IF(s_log_out,"time: %ld : %09ld.\n",t.tv_sec,t.tv_nsec);
    return t.tv_sec*1000000000LL + t.tv_nsec;
}
/*
static int64_t systemTime()
{
    struct timeval curtime;
    gettimeofday(&curtime, NULL);
}
*/

#ifdef GSP_INTF_SO

static int get_gsp_interface(GSP_MISC_INFO_T *misc, GSP_INTF_OPS_E ops)
{
    static hw_module_t const* pModule = NULL;
    static gsp_device_t *gsp_dev = NULL;
    if((GSP_INTF_PUT < ops || ops < GSP_INTF_GET || misc == NULL)
       || ((ops == GSP_INTF_GET && (pModule != NULL || gsp_dev != NULL))
           || (ops == GSP_INTF_PUT && (pModule == NULL || gsp_dev == NULL))))
    {
        ALOGE("%s[%d] param err, return\n",__func__,__LINE__);
        return -1;
    }

    if(ops == GSP_INTF_GET)
    {
        if(hw_get_module(GSP_HARDWARE_MODULE_ID, &pModule)==0)
        {
            ALOGE("get gsp module ok\n");
        }
        else
        {
            ALOGE("get gsp module failed\n");
            goto exit;
        }
        pModule->methods->open(pModule,"gsp",(hw_device_t**)&gsp_dev);
        if(gsp_dev)
        {
            misc->gsp_process = gsp_dev->GSP_Proccess;
            misc->gsp_getCapability = gsp_dev->GSP_GetCapability;
        }
        else
        {
            goto exit;
        }
    }
    else
    {
        gsp_dev->common.close(&gsp_dev->common);
        //hw_put_module(pModule);
        gsp_dev = NULL;
        pModule = NULL;
        misc->gsp_process=NULL;
    }
    return 0;

exit:
    if(gsp_dev)
    {
        gsp_dev->common.close(&gsp_dev->common);
        gsp_dev = NULL;
    }
    if(pModule)
    {
        //hw_put_module(pModule);
        pModule = NULL;
    }
    return -1;
}

#else
static int get_gsp_interface(GSP_MISC_INFO_T *misc, GSP_INTF_OPS_E ops)
{
    if(GSP_INTF_PUT < ops || ops < GSP_INTF_GET || misc == NULL)
    {
        ALOGE("%s[%d] param err, return\n",__func__,__LINE__);
        return -1;
    }

    if(ops == GSP_INTF_GET)
    {
        misc->gsp_process=GSP_Proccess;
    }
    else
    {
        misc->gsp_process=NULL;
    }
    return 0;
}
#endif

#define PRINTF_RETURN() printf("%s[%d] return.\n",__func__,__LINE__)

int main(int argc, char **argv)
{
    GSP_LAYER_INFO_T        layer0;
    GSP_LAYER_INFO_T        layer1;
    GSP_LAYER_INFO_T        layerd;
    GSP_MISC_INFO_T          misc;

    int ret = 0;
    uint32_t test_cnt = 0;
    uint32_t test_cnt_max = 1;

    int64_t start_time = 0;//used for performace
    int64_t end_time = 0;//used for performace
    int64_t single_max = 0;//
    int64_t single_min = 1000000;//
    GSP_CAPABILITY_T Gsp_cap;

    //print_main_params();

    if (argc < 4)
    {
        usage();
        return -1;
    }


    /* parse argument */
    ret = parse_main_params( argc, argv,
                             &layer0, &layer1, &layerd, &misc);
    print_misc_params(&misc);
    print_layer_params(&layerd);
    print_layer_params(&layer0);
    print_layer_params(&layer1);
    /*params check*/
    if(ret || layerd.filename == NULL)
    {
        PRINTF_RETURN();
        return ret;
    }


    //calc size of each plane
    ret = calc_input_plane_size(&layer0);
    ret |= calc_input_plane_size(&layer1);
    ret |= calc_output_plane_size(&layerd);
    if(ret)
    {
        PRINTF_RETURN();
        return ret;
    }
    ALOGE("cf0:%d, total_size:%d\n",layer0.format,layer0.size_all);
    ALOGE("cf1:%d, total_size:%d\n",layer1.format,layer1.size_all);
    ALOGE("cfd:%d, total_size:%d\n",layerd.format,layerd.size_all);
    /*size check*/
    if((layer0.size_all == 0 && layer1.size_all == 0) || (layerd.size_all == 0))
    {
        PRINTF_RETURN();
        return ret;
    }


    if(misc.map_once)
    {
        layer0.map_once = misc.map_once;
        layer1.map_once = misc.map_once;
        layerd.map_once = misc.map_once;
    }

    if(layer0.filename != NULL && layer0.size_all > 0)
    {
        ALOGE("L0 alloc_buffer \n");
        ret = alloc_buffer(&layer0);
        if(ret)
        {
            ALOGE("L0 alloc_buffer failed\n");
            goto free_layer_buff;
        }
    }

    if(layer1.filename != NULL && layer1.size_all > 0)
    {
        ALOGE("L1 alloc_buffer \n");
        ret = alloc_buffer(&layer1);
        if(ret)
        {
            ALOGE("L1 alloc_buffer failed\n");
            goto free_layer_buff;
        }
    }

    ALOGE("Ld alloc_buffer \n");
    ret = alloc_buffer(&layerd);
    if(ret)
    {
        ALOGE("Ld alloc_buffer failed\n");
        goto free_layer_buff;
    }

    ALOGE("%s[%d],out alloc pa:0x%08x va:0x%08x ,size:%d\n", __func__, __LINE__,layerd.pa.addr_y,layerd.va.addr_y,layerd.size_all);

    ret = open_raw_file(&layer0,"r");
    ret |= open_raw_file(&layer1,"r");
    ret |= open_raw_file(&layerd,"wb");
    if(ret)
    {
        if(!(misc.power_flag || misc.performance_flag))
        {
            goto close_file;
        }
    }


    ret = read_raw_file(&layer0);
    ret |= read_raw_file(&layer1);
    if(ret)
    {
        if(!(misc.power_flag || misc.performance_flag))
        {
            goto close_file;
        }
    }
    //add_frame_boundary(&layer0);


    ret = get_gsp_interface(&misc, GSP_INTF_GET);
    if(ret)
    {
        goto close_file;
    }

    (*misc.gsp_getCapability)(&Gsp_cap);
    if(Gsp_cap.magic == CAPABILITY_MAGIC_NUMBER)
    {
        ALOGE("GSP Capability info: version:%d, support %s buffer, %s page boundary issue, %s process reduce yuv\n",
              Gsp_cap.version,
              (Gsp_cap.buf_type_support == GSP_ADDR_TYPE_IOVIRTUAL)?"virtual":"physical",
              (Gsp_cap.video_need_copy == 1)?"with":"without",
              (Gsp_cap.blend_video_with_OSD == 1)?"can":"can't");
    }

    if(misc.performance_flag)
    {
        s_log_out = 0;
        test_cnt_max = 1000;
        start_time = systemTime()/1000;
    }
    else if(misc.power_flag)
    {
        test_cnt_max = 10000000;
    }

    while(test_cnt < test_cnt_max)
    {
        //ALOGE("test time test_cnt %d\n", test_cnt);

        int64_t single_end = 0;//used for power test
        int64_t single_st = 0;//used for power test
        GSP_CONFIG_INFO_T gsp_cfg_info;
        if(misc.power_flag || misc.performance_flag)
        {
            single_st = systemTime()/1000;
        }
        memset(&gsp_cfg_info,0,sizeof(gsp_cfg_info));


        if(misc.map_once == 0)   // map every time
        {
                iommu_map_if_need(&layer0);
                iommu_map_if_need(&layer1);
                iommu_map_if_need(&layerd);
        }
        set_gsp_cfg_info(&gsp_cfg_info,&layer0,&layer1,&layerd);

        gsp_cpy_process(&gsp_cfg_info,&layer0,&layer1,&misc);

        gsp_cfg_info.misc_info.split_pages = 0;
        ret = (*misc.gsp_process)(&gsp_cfg_info);
        if(misc.map_once == 0)   // unmap every time
        {
            iommu_unmap_if_need(&layer0);
            iommu_unmap_if_need(&layer1);
            iommu_unmap_if_need(&layerd);
        }
        if(0 == ret)
        {
            ALOGE_IF(s_log_out,"GSP_Proccess ok\n");
            while(misc.hold_flag);
        }
        else
        {
            ALOGE("GSP_Proccess err:%d!!\n",ret);
            goto close_file;
        }

        if(misc.power_flag || misc.performance_flag)
        {
            int64_t calc = 0;
            single_end = systemTime()/1000;
            calc = single_end - single_st;
            single_max = (single_max<calc)?calc:single_max;
            single_min =  (single_min<calc)?single_min:calc;
            if(calc < 30000 && misc.power_flag)
            {
                usleep(30000-calc);
                //sleep(2);
            }
        }

        test_cnt++;
    }


    if(misc.performance_flag)
    {
        int64_t calc = 0;
        end_time = systemTime()/1000;
        calc = end_time - start_time;
        calc /= test_cnt_max;
        ALOGE("GSP start:%lld, end:%lld,max:%lld,min:%lld,avg:%lld us !!\n",start_time,end_time,single_max,single_min,calc);
    }

    ALOGE("%s[%d],write %s params: addr:0x%08x size:%d \n",  __func__, __LINE__,layerd.filename,layerd.va.addr_y,(layerd.size_y+layerd.size_u+layerd.size_v));
    ret = write_raw_file(&layerd);
    if(ret)
    {
        goto close_file;
    }


close_file:

    close_raw_file(&layer0);
    close_raw_file(&layer1);
    close_raw_file(&layerd);

free_layer_buff:
    free_buffer(&layer0);
    free_buffer(&layer1);
    free_buffer(&layerd);
    get_gsp_interface(&misc, GSP_INTF_PUT);
    return 0;
}




