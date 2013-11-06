/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

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

#include <linux/ion.h>
#include <binder/MemoryHeapIon.h>
using namespace android;

#include "gsp_hal.h"

//#define CALCULATE_PSNR

#define ERR(x...)   fprintf(stderr, ##x)
#define INFO(x...)  fprintf(stdout, ##x)


#define CLIP_16(x)  (((x) + 15) & (~15))


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

//#define GSP_HAL_CONCURRENT_TEST

#ifdef GSP_HAL_CONCURRENT_TEST
unsigned int width = 0;
unsigned int height = 0;
unsigned int informat = 1;
unsigned int outformat = 1;

GSP_DATA_ADDR_T va_in0;
GSP_DATA_ADDR_T pa_in0;
GSP_DATA_ADDR_T va_out;
GSP_DATA_ADDR_T pa_out;
#endif


#ifdef GSP_HAL_CONCURRENT_TEST

static pthread_t gsp_test_thread[3] = {0};
static volatile int force_exit_flag = 0;


static void* gsp_test_thread_proc(void* data)
{
    uint32_t sleep_time = 0;

    ERR("gsp_test_thread%d_proc: enter. Line:%d\n", data,__LINE__);
    while(!force_exit_flag)
    {
        sleep_time = 50 + (rand()%256);
        ERR("thread[%d],sleep:%dms zzzzzzz...\n",data, sleep_time);

        usleep(sleep_time * 1000);

        GSP_CFC((GSP_LAYER_SRC_DATA_FMT_E)informat,
                (GSP_LAYER_DST_DATA_FMT_E)outformat,
                width,
                height,
                va_in0.addr_y,
                pa_in0.addr_y,
                va_out.addr_y,
                pa_out.addr_y);


        ERR("thread[%d]: done. Line:%d\n", data,__LINE__);
    }
    ERR("thread[%d]: exit. Line:%d\n", data,__LINE__);

    gsp_test_thread[(uint32_t)data] = 0;
    return NULL;
}



int32_t create_gsp_test_thread(void)
{
    int32_t ret = 0;
    uint32_t i = 0;
    pthread_attr_t attr;

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    i=0;
    ret = pthread_create(&gsp_test_thread[i], &attr, gsp_test_thread_proc, (void*)i);
    i++;
    ret = pthread_create(&gsp_test_thread[i], &attr, gsp_test_thread_proc, (void*)i);
    i++;
    ret = pthread_create(&gsp_test_thread[i], &attr, gsp_test_thread_proc, (void*)i);
    pthread_attr_destroy(&attr);
    return ret;
}

void sigterm_handler(int signo)
{
    force_exit_flag = 1;
    ERR("sigterm_handler. Line:%d\n", __LINE__);		
	//exit(0);
}
#endif


int main(int argc, char **argv)
{
    char* filename_in = NULL;
    char* filename_out = NULL;
#ifndef GSP_HAL_CONCURRENT_TEST
    unsigned int width = 0;
    unsigned int height = 0;
    unsigned int informat = 1;
    unsigned int outformat = 1;
#endif
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
            filename_in = argv[++i];
        }
        else if (strcmp(argv[i], "-o") == 0 && (i < argc-1))
        {
            filename_out = argv[++i];
        }
        else if (strcmp(argv[i], "-w") == 0 && (i < argc-1))
        {
            width = CLIP_16(atoi(argv[++i]));
        }
        else if (strcmp(argv[i], "-h") == 0 && (i < argc-1))
        {
            height = CLIP_16(atoi(argv[++i]));
        }
        else if (strcmp(argv[i], "-informat") == 0 && (i < argc-1))
        {
            informat = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "-outformat") == 0 && (i < argc-1))
        {
            outformat = atoi(argv[++i]);
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

    ERR("%s:%d,params:if:%s of:%s w:%d h:%d icf:%d ocf:%d\n", __func__, __LINE__,
        filename_in,
        filename_out,
        width,
        height,
        informat,
        outformat);


    /* check argument */
    if ((filename_in == NULL) || (filename_out == NULL))
    {
        usage();
        return -1;
    }
    if ((width == 0) || (height == 0))
    {
        usage();
        return -1;
    }

#ifndef GSP_HAL_CONCURRENT_TEST
    GSP_DATA_ADDR_T va_in0;
    GSP_DATA_ADDR_T pa_in0;
    GSP_DATA_ADDR_T va_out;
    GSP_DATA_ADDR_T pa_out;
#endif
    unsigned int size_in0_y = 0;
    unsigned int size_in0_u = 0;
    unsigned int size_in0_v = 0;

    GSP_DATA_ADDR_T va_in1;
    GSP_DATA_ADDR_T pa_in1;
    int temp_size = 0;

    unsigned int size_in1_y = 0;
    unsigned int size_in1_u = 0;
    unsigned int size_in1_v = 0;

    unsigned int size_out_y = 0;
    unsigned int size_out_u = 0;
    unsigned int size_out_v = 0;

    sp<MemoryHeapIon> MemoryHeapIn0 = NULL;
    sp<MemoryHeapIon> MemoryHeapIn1 = NULL;
    sp<MemoryHeapIon> MemoryHeapOut = NULL;


    //calc size of each plane
    switch(informat)
    {
        case GSP_SRC_FMT_ARGB888:
        case GSP_SRC_FMT_RGB888:
            size_in0_y = width * height * 4;
            size_in0_u = 0;
            size_in0_v = 0;
            break;
        case GSP_SRC_FMT_ARGB565:
            size_in0_y = width * height * 2;
            size_in0_u = width * height;
            size_in0_v = 0;
            break;
        case GSP_SRC_FMT_RGB565:
            size_in0_y = width * height * 2;
            size_in0_u = 0;
            size_in0_v = 0;
            break;
        case GSP_SRC_FMT_YUV420_2P:
            size_in0_y = width * height;
            size_in0_u = size_in0_y/2;
            size_in0_v = 0;
            break;
        case GSP_SRC_FMT_YUV420_3P:
            size_in0_y = width * height;
            size_in0_u = size_in0_y/4;
            size_in0_v = size_in0_u;
            break;
        default:
            return -1;//testing not support the other color format
            break;
    }

    switch(outformat)
    {
        case GSP_DST_FMT_ARGB888:
        case GSP_DST_FMT_RGB888:
            size_out_y = width * height * 4;
            size_out_u = 0;
            size_out_v = 0;
            break;

        case GSP_DST_FMT_ARGB565:
            size_out_y = width * height * 2;
            size_out_u = width * height;
            size_out_v = 0;
            break;
        case GSP_DST_FMT_RGB565:
            size_out_y = width * height * 2;
            size_out_u = 0;
            size_out_v = 0;
            break;
        case GSP_DST_FMT_YUV420_2P:
            size_out_y = width * height;
            size_out_u = size_out_y/2;
            size_out_v = 0;
            break;
        case GSP_DST_FMT_YUV420_3P:
            size_out_y = width * height;
            size_out_u = size_out_y/4;
            size_out_v = size_out_u;
            break;
        default:
            return -1;//testing not support the other color format
            break;
    }

    {
        //alloc none cached and buffered memory from pmem for layer0
        MemoryHeapIn0 = new MemoryHeapIon("/dev/ion", (size_in0_y+size_in0_u+size_in0_v), MemoryHeapBase::NO_CACHING, ION_HEAP_CARVEOUT_MASK);
        MemoryHeapIn0->get_phy_addr_from_ion((int*)&pa_in0.addr_y, &temp_size);
        va_in0.addr_y = (uint32_t)MemoryHeapIn0->base();

        ERR("%s:%d,in0 alloc pa:0x%08x va:0x%08x ,size:%d\n", __func__, __LINE__,pa_in0.addr_y,va_in0.addr_y,(size_in0_y+size_in0_u+size_in0_v));

        if (va_in0.addr_y == 0xffffffff)
        {
            ERR("Failed to alloc yuv pmem buffer\n");
            return -1;
        }
        va_in0.addr_uv = va_in0.addr_y + size_in0_y;
        va_in0.addr_v = va_in0.addr_uv + size_in0_u;
        pa_in0.addr_uv = pa_in0.addr_y + size_in0_y;
        pa_in0.addr_v = pa_in0.addr_uv + size_in0_u;

        {
            //alloc none cached and buffered memory from pmem for layer des
            MemoryHeapOut = new MemoryHeapIon("/dev/ion", (size_out_y+size_out_u+size_out_v), MemoryHeapBase::NO_CACHING, ION_HEAP_CARVEOUT_MASK);
            MemoryHeapOut->get_phy_addr_from_ion((int*)&pa_out.addr_y, &temp_size);
            va_out.addr_y = (uint32_t)MemoryHeapOut->base();

            ERR("%s:%d,out alloc pa:0x%08x va:0x%08x ,size:%d\n", __func__, __LINE__,pa_out.addr_y,va_out.addr_y,(size_out_y+size_out_u+size_out_v));
            if (va_out.addr_y == 0xffffffff)
            {
                ERR("Failed to alloc yuv pmem buffer\n");
                return -1;
            }
            va_out.addr_uv = va_out.addr_y + size_in0_y;
            va_out.addr_v = va_out.addr_uv + size_in0_u;
            pa_out.addr_uv = pa_out.addr_y + size_in0_y;
            pa_out.addr_v = pa_out.addr_uv + size_in0_u;

            {
                FILE* fp_in0 = NULL;
                FILE* fp_in1 = NULL;
                FILE* fp_out = NULL;

                fp_in0 = fopen(filename_in, "rb");
                if (fp_in0 == NULL)
                {
                    ERR("Failed to open file %s\n", filename_in);
                    return -1;
                }

                {
                    fp_out = fopen(filename_out, "wb");
                    if (fp_out == NULL)
                    {
                        ERR("Failed to open file %s\n", filename_out);
                        return -1;
                    }

                    if (fread((void*)va_in0.addr_y, sizeof(unsigned char), (size_in0_y+size_in0_u+size_in0_v), fp_in0) != (size_in0_y+size_in0_u+size_in0_v))
                    {
                        ERR("Failed to read file %s\n", filename_in);
                        return -1;
                    }
#ifdef GSP_HAL_CONCURRENT_TEST

                    create_gsp_test_thread();
                    while(force_exit_flag == 0)
                    {
                        usleep(100000 * 1000);
                    }
                    while(gsp_test_thread[0] || gsp_test_thread[1] || gsp_test_thread[2])
                    {
                        usleep(10);
                    }

#else
                    GSP_CFC((GSP_LAYER_SRC_DATA_FMT_E)informat,
                            (GSP_LAYER_DST_DATA_FMT_E)outformat,
                            width,
                            height,
                            va_in0.addr_y,
                            pa_in0.addr_y,
                            va_out.addr_y,
                            pa_out.addr_y);

#endif

                    ERR("%s:%d,write file params: addr:0x%08x size:%d \n",  __func__, __LINE__,va_out.addr_y,(size_out_y+size_out_u+size_out_v));

                    if (fwrite((void*)va_out.addr_y, sizeof(unsigned char), (size_out_y+size_out_u+size_out_v), fp_out) != size_out_y)
                    {
                        ERR("Failed to write file %s\n", filename_out);
                        return -1;
                    }


                    if (fp_out != NULL)
                    {
                        fclose(fp_out);
                        fp_out = NULL;
                    }
                }

                if (fp_in0 != NULL)
                {
                    fclose(fp_in0);
                    fp_in0 = NULL;
                }
            }

            //release layer des pmem
            if (MemoryHeapOut != NULL)
            {
                MemoryHeapOut.clear();
                MemoryHeapOut = NULL;
            }
        }

        //release layer0 pmem
        if (MemoryHeapIn0 != NULL)
        {
            MemoryHeapIn0.clear();
            MemoryHeapIn0 = NULL;
        }
    }
}

