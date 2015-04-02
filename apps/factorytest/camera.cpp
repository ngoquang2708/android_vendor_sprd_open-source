#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <getopt.h>
#include <sys/cdefs.h>
#include <sys/types.h>
#include <utils/Log.h>
#include <utils/String16.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <cutils/properties.h>
#include <linux/ion.h>
#include <binder/MemoryHeapIon.h>
#include <camera/Camera.h>
#include <semaphore.h>
#include <linux/android_pmem.h>

#include <hardware/camera.h>
#include <linux/fb.h>
#include <dlfcn.h>
#include <hardware/bluetooth.h>




extern "C"
{
#include "testitem.h"
}

static int                 mNumberOfCameras;


struct preview_stream_ops mHalPreviewWindow;


struct alloc_device_t* gdevice;
 //framebuffer_device_t* fbDev;
static int musage;
static int mwidth;
static int mheight;
static int mformat;
static int front_back=0;

static int ml,mr,mt,mb;

#define MIN_NUM_FRAME_BUFFERS  2
#define MAX_NUM_FRAME_BUFFERS  3



 int32_t mGrallocBufferIndex;
 void*       mBase;
 int buffer_count;
 struct frame_buffer
{
buffer_handle_t handle;
int stride;
int inuse;
}buffers[MAX_NUM_FRAME_BUFFERS],buffers2[32];

#define ENGTEST_PREVIEW_BUF_NUM 2
#define ENGTEST_PREVIEW_WIDTH 	640//800
#define ENGTEST_PREVIEW_HEIGHT 	480//608
#define ENGTEST_MAX_MISCHEAP_NUM 10

#define USE_PHYSICAL_ADD 0
#define USE_IOMM_ADD 1
static int g_mem_method = USE_PHYSICAL_ADD;/*0: physical address, 1: iommu  address*/

#define SPRD_FB_DEV					"/dev/graphics/fb0"
static int fb_fd = -1;
static struct fb_fix_screeninfo fix;
static struct fb_var_screeninfo var;

struct frame_buffer_t {
    uint32_t phys_addr;
	uint32_t virt_addr;
    uint32_t length;								 //buffer's length is different from cap_image_size
};

#define SPRD_LCD_WIDTH				600
#define SPRD_LCD_HEIGHT				800
#define SPRD_MAX_PREVIEW_BUF 		ENGTEST_PREVIEW_BUF_NUM
static struct frame_buffer_t fb_buf[SPRD_MAX_PREVIEW_BUF+1];
static uint8_t tmpbuf[SPRD_LCD_WIDTH*SPRD_LCD_HEIGHT*4];
static uint8_t tmpbuf1[SPRD_LCD_WIDTH*SPRD_LCD_HEIGHT*4];

static uint32_t post_preview_buf[ENGTEST_PREVIEW_WIDTH*ENGTEST_PREVIEW_WIDTH*2];
static uint32_t rot_buf[ENGTEST_PREVIEW_WIDTH*ENGTEST_PREVIEW_WIDTH*2];

#define RGB565(r,g,b)       ((unsigned short)((((unsigned char)(r)>>3)|((unsigned short)(((unsigned char)(g)>>2))<<5))|(((unsigned short)((unsigned char)(b>>3)))<<11)))
static int cb_buffer_size;
static int cb_buffer_number;
static int cb_buffer_index;

static char *bcamera_para=(char *)malloc(sizeof(char)*4096);
static char *fcamera_para="auto-exposure=0;auto-exposure-supported=0;brightness=3;brightness-supported=true;brightness-values=0,1,2,3,4,5,6;cameraid=front_camera;cameraid-values=back_camera,front_camera;capture-mode=1;contrast=3;contrast-supported=true;contrast-values=0,1,2,3,4,5,6;effect=none;effect-values=none,mono,negative,sepia,cold,antique;exposure-compensation=0;exposure-compensation-step=0;flash-mode=off;flash-mode-supported=false;flash-mode-values=off;focal-length=3.75;focus-distances=2.0,2.5,Infinity;focus-mode=infinity;focus-mode-values=infinity;hdr=0;hdr-supported=false;horizontal-view-angle=54;jpeg-quality=100;jpeg-thumbnail-height=240;jpeg-thumbnail-quality=80;jpeg-thumbnail-size-values=320x240,0x0;jpeg-thumbnail-width=320;max-brightness=6;max-contrast=6;max-exposure-compensation=0;max-num-detected-faces-hw=10;max-num-metering-areas=0;max-slow-motion=3;max-zoom=15;min-exposure-compensation=0;picture-format=jpeg;picture-format-values=jpeg;picture-size=640x480;picture-size-values=640x480,320x240;preferred-preview-size-for-video=;preview-env=0;preview-format=yuv420sp;preview-format-values=yuv420sp,yuv420p;preview-fps-range=1000,30000;preview-fps-range-values=(1000,30000);preview-frame-rate=25;preview-frame-rate-values=5,10,12,15,20,25,30;preview-size=640x480;preview-size-values=864x640,720x480,640x480,352x288,320x240,176x144;saturation=3;saturation-supported=true;saturation-values=0,1,2,3,4,5,6;scene-mode=auto;scene-mode-values=auto,night;slow-motion=1;slow-motion-supported=false;slow-motion-values=1;smile-snap-mode=0;smooth-brightness-supported=false;smooth-contrast-supported=false;smooth-zoom-supported=false;vertical-view-angle=54;video-frame-format=yuv420sp;video-frame-format-values=yuv420sp,yuv420p;video-picture-size-values=1280x960,1280x960,1280x960;video-size=720x480;video-size-values=;whitebalance=auto;whitebalance-values=auto,incandescent,fluorescent,daylight,cloudy-daylight;zoom=0;zoom-ratios=100,110,120,130,140,150,160,170,180,190,200,220,240,260,280,300;zoom-supported=true;zsl=0;zsl-supported=true";
static char *bc_para=NULL;
static char *fc_para=NULL;
void RGBRotate90_anticlockwise(uint8_t *des,uint8_t *src,int width,int height, int bits)
{
	if ((!des)||(!src))
	{
		return;
	}

	int n = 0;
	int linesize;
	int i,j;
	int m = bits/8;

	//LOGD("mmitest %s: bits=%d; m=%d\r\n",__FUNCTION__, bits, m);
	linesize = width*m;

	for(j = 0;j < width ;j++)
	{
		for(i= height;i>0;i--)
		{
			memcpy(&des[n],&src[linesize*(i-1)+j*m],m);
			n+=m;
		}
	}

	//LOGD("mmitest outof %s: \r\n",__FUNCTION__);
}


void YUVRotate90(uint8_t *des,uint8_t *src,int width,int height)
{
	int i=0,j=0,n=0;
	int hw=width/2,hh=height/2;

	for(j=width;j>0;j--)
		for(i=0;i<height;i++)
		{
			des[n++] = src[width*i+j];
		}
	unsigned char *ptmp = src+width*height;
	for(j=hw;j>0;j--)
		for(i=0;i<hh;i++)
		{
			des[n++] = ptmp[hw*i+j];
		}

	ptmp = src+width*height*5/4;
	for(j=hw;j>0;j--)
		for(i=0;i<hh;i++)
		{
			des[n++] = ptmp[hw*i+j];
		}

	//LOGD("mmitest outof %s: \r\n",__FUNCTION__);
}
void  StretchColors(void* pDest, int nDestWidth, int nDestHeight, int nDestBits, void* pSrc, int nSrcWidth, int nSrcHeight, int nSrcBits)
{
	double dfAmplificationX = ((double)nDestWidth)/nSrcWidth;
	double dfAmplificationY = ((double)nDestHeight)/nSrcHeight;

	const int nSrcColorLen = nSrcBits/8;
	const int nDestColorLen = nDestBits/8;
	int i = 0;
	int j = 0;
	//LOGD("mmitest %s\r\n",__FUNCTION__);
	for(i = 0; i<nDestHeight; i++)
		for(j = 0; j<nDestWidth; j++)
		{
			double tmp = i/dfAmplificationY;
			int nLine = (int)tmp;

			if(tmp - nLine > 0.5)
			++nLine;

			if(nLine >= nSrcHeight)
			--nLine;

			tmp = j/dfAmplificationX;
			int nRow = (int)tmp;

			if(tmp - nRow > 0.5)
			++nRow;

			if(nRow >= nSrcWidth)
			--nRow;

			unsigned char *pSrcPos = (unsigned char*)pSrc + (nLine*nSrcWidth + nRow)*nSrcColorLen;
			unsigned char *pDestPos = (unsigned char*)pDest + (i*nDestWidth + j)*nDestColorLen;

			*pDestPos++ = *pSrcPos++;
			*pDestPos++ = *pSrcPos++;
			*pDestPos++ = *pSrcPos++;

			if(nDestColorLen == 4)
			*pDestPos = 0;
		}

		//LOGD("mmitest outof %s: \r\n",__FUNCTION__);
}


void yuv420_to_rgb(int width, int height, unsigned char *src, unsigned int *dst)
{
    int frameSize = width * height;
	int j = 0, yp = 0, i = 0;
	unsigned short *dst16 = (unsigned short *)dst;

    unsigned char *yuv420sp = src;
    for (j = 0, yp = 0; j < height; j++) {
        int uvp = frameSize + (j >> 1) * width, u = 0, v = 0;
        for (i = 0; i < width; i++, yp++) {
            int y = (0xff & ((int) yuv420sp[yp])) - 16;
            if (y < 0) y = 0;
            if ((i & 1) == 0) {
                u = (0xff & yuv420sp[uvp++]) - 128;
                v = (0xff & yuv420sp[uvp++]) - 128;
            }

            int y1192 = 1192 * y;
            int r = (y1192 + 1634 * v);
            int g = (y1192 - 833 * v - 400 * u);
            int b = (y1192 + 2066 * u);

            if (r < 0) r = 0; else if (r > 262143) r = 262143;
            if (g < 0) g = 0; else if (g > 262143) g = 262143;
            if (b < 0) b = 0; else if (b > 262143) b = 262143;

			if(var.bits_per_pixel == 32) {
                dst[yp] = ((((r << 6) & 0xff0000)>>16)<<16)|(((((g >> 2) & 0xff00)>>8))<<8)|((((b >> 10) & 0xff))<<0);
			} else {
                dst16[yp] = RGB565((((r << 6) & 0xff0000)>>16), (((g >> 2) & 0xff00)>>8), (((b >> 10) & 0xff)));
			}
		}
    }

	//LOGD("mmitest outof %s: \r\n",__FUNCTION__);
}

static void eng_test_fb_update(unsigned n)
{
    printf("DCAM: active framebuffer[%d], bits_per_pixel=%d\r\n",n, var.bits_per_pixel);
    if (n > 1) return;

    var.yres_virtual = var.yres * 2;
    var.yoffset = n * var.yres;
    if (ioctl(fb_fd, FBIOPUT_VSCREENINFO, &var) < 0) {
        printf("DCAM: active fb swap failed\r\n");
    }

}
static void __notify_cb(int32_t msg_type, int32_t ext1,
                            int32_t ext2, void *user)
{
    LOGD("mmitest %s msg_type %d\r\n", __FUNCTION__,msg_type);
}



static void data_mirror(uint8_t *des,uint8_t *src,int width,int height, int bits)
{
	if ((!des)||(!src))
	{
		return;
	}
	LOGD("mmitest width=%d,height=%d",width,height);

	int n = 0;
	int linesize;
	int i,j;
	int num;
	int lineunm;
	int m = bits/8;

	linesize = width*m;

	for(j=0;j<height;j++)
	{

			for(i= 0;i< width;i++)
			{

					memcpy(&des[n],&src[linesize-(i+1)*m+j*linesize],m);
					n+=m;

			}
	}

	LOGD("mmitest out of mirror");
}

static void __data_cb(int32_t msg_type,
                          const camera_memory_t *data, unsigned int index,
                          camera_frame_metadata_t *metadata,
                          void *user)
{

	unsigned char* tmp=(unsigned char*)data->data;
	yuv420_to_rgb(ENGTEST_PREVIEW_WIDTH,ENGTEST_PREVIEW_HEIGHT, tmp+index*cb_buffer_size, \
					post_preview_buf);
	if(front_back==1)
	{
		LOGD("mmitest I am back camra");
		StretchColors((void *)(fb_buf[2].virt_addr), var.yres, var.xres, var.bits_per_pixel, \
						post_preview_buf, ENGTEST_PREVIEW_WIDTH, ENGTEST_PREVIEW_HEIGHT, var.bits_per_pixel);

		RGBRotate90_anticlockwise((uint8_t *)(fb_buf[cb_buffer_index].virt_addr), (uint8_t *)(fb_buf[2].virt_addr),
						var.yres, var.xres, var.bits_per_pixel);
	}
	else
	{
		LOGD("mmitest I am front camra");
		StretchColors((void *)(fb_buf[3].virt_addr), var.yres, var.xres, var.bits_per_pixel, \
						post_preview_buf, ENGTEST_PREVIEW_WIDTH, ENGTEST_PREVIEW_HEIGHT, var.bits_per_pixel);

		data_mirror((uint8_t *)(fb_buf[2].virt_addr), (uint8_t *)(fb_buf[3].virt_addr),
						 var.yres,var.xres, var.bits_per_pixel);
		RGBRotate90_anticlockwise((uint8_t *)(fb_buf[cb_buffer_index].virt_addr), (uint8_t *)(fb_buf[2].virt_addr),
								 var.yres,var.xres, var.bits_per_pixel);
	}
	eng_test_fb_update(cb_buffer_index);
	cb_buffer_index++;
	if(cb_buffer_index==2)
		cb_buffer_index=0;

}




static void __data_cb_timestamp(nsecs_t timestamp, int32_t msg_type,
                             const camera_memory_t *data, unsigned index,
                             void *user)
{
    LOGD("mmitest %s msg_type %d \r\n", __FUNCTION__,msg_type);
}


int mapfd(int fd, size_t size, uint32_t offset)
{
    if (size == 0) {
        pmem_region reg;
        int err = ioctl(fd, PMEM_GET_TOTAL_SIZE, &reg);
        if (err == 0)
            size = reg.len;
        if (size == 0) { // try fstat
            struct stat sb;
            if (fstat(fd, &sb) == 0)
                size = sb.st_size;
        }
        // if it didn't work, let mmap() fail.
    }

      {
        void* base = (uint8_t*)mmap(0, size,
                PROT_READ|PROT_WRITE, MAP_SHARED, fd, offset);
        if (base == MAP_FAILED) {
            printf("mmap(fd=%d, size=%u) failed (%s)",
                    fd, uint32_t(size), strerror(errno));
            close(fd);
            return -errno;
        }
        printf("mmap(fd=%d, base=%p, size=%lu)\f\n", fd, base, size);
        mBase = base;
        //mNeedUnmap = true;
      }
      return 0;
}

static void __put_memory(camera_memory_t *data)
    {
	printf("put_memory\r\n");
    }
camera_memory_t *handle;
static camera_memory_t* __get_memory(int fd, size_t buf_size, uint_t num_bufs,
                                         void *user __attribute__((unused)))
{
	const size_t pagesize = getpagesize();
	uint32_t size= buf_size * num_bufs;
    size = ((size + pagesize-1) & ~(pagesize-1));
    mapfd(dup(fd), size, 0);

    handle=(camera_memory_t *)malloc(sizeof(camera_memory_t));
    handle->data = mBase;
    handle->size = buf_size * num_bufs;
    handle->handle = handle;
    cb_buffer_size=buf_size;
    cb_buffer_number=num_bufs;
    handle->release = __put_memory;
    return handle;
}


   static int __dequeue_buffer(struct preview_stream_ops* w,
                                buffer_handle_t** buffer, int *stride)
    {
	  int i;
	  //LOGD("dequeue buffer\r\n");

	  if(mGrallocBufferIndex>=32)
		mGrallocBufferIndex=0;
	  for(i=0;i<buffer_count;i++)
	  {
		if(buffers2[i].inuse==0)
			break;
	  }

	  if(mGrallocBufferIndex<buffer_count)
	  {
	  	int rc = gdevice->alloc(gdevice,mwidth, mheight, mformat,
                        musage, &buffers2[i].handle, &buffers2[i].stride);
        LOGD("fb buffer %d allocation failed w=%d, h=%d, err=%s\r\n",mGrallocBufferIndex, mwidth, mheight, strerror(-rc));
        if (rc)
            return 0;
        mGrallocBufferIndex++;
	}

      *buffer = &buffers2[i].handle;
	  *stride=  buffers2[i].stride;
	   buffers2[i].inuse=1;

        return 0;
    }


static int __lock_buffer(struct preview_stream_ops* w,
	buffer_handle_t* /*buffer*/)
  {
	printf("lock buffer\r\n");
        return 0;
  }

    static int __enqueue_buffer(struct preview_stream_ops* w,
                      buffer_handle_t* buffer)
    {
	int i;
	for(i=0;i<buffer_count;i++)
	{
		if(buffers2[i].handle==*buffer)
			buffers2[i].inuse=0;
	}
	//LOGD("mmitest out enqueue buffer\r\n");
	return 0;
    }

    static int __cancel_buffer(struct preview_stream_ops* w,
                      buffer_handle_t* buffer)
    {
     int i;
	for(i=0;i<buffer_count;i++)
	{
		if(buffers2[i].handle==*buffer)
			buffers2[i].inuse=0;
	}
	     return 0;
    }

    static int __set_buffer_count(struct preview_stream_ops* w, int count)
    {
	//LOGD("mmitest set buffer count %d\r\n",count);
	buffer_count=count;
        return 0;
	//LOGD("mmitest out buffer count \r\n");
    }

    static int __set_buffers_geometry(struct preview_stream_ops* w,
                      int width, int height, int format)
    {
	//LOGD("mmitest set buffers geometry w %d h %d f %d\r\n",width,height,format);
	mwidth=width;
	mheight=height;
	mformat=format;
    return 0;
    }

    static int __set_crop(struct preview_stream_ops *w,
                      int left, int top, int right, int bottom)
    {
	printf("set crop l %d t %d r %d b %d\r\n",left,top,right,bottom);
	ml=left;
	mt=top;
	mr=right;
	mb=bottom;
    return 0;
    }

    static int __set_timestamp(struct preview_stream_ops *w,
                               int64_t timestamp) {
	printf("set timestamp\r\n");
        return 0;
    }

    static int __set_usage(struct preview_stream_ops* w, int usage)
    {
	printf("set usage %d\r\n",usage);
	musage=usage;
        return 0;
    }

    static int __set_swap_interval(struct preview_stream_ops *w, int interval)
    {
	printf("set swap interval\r\n");
        return 0;
    }

    static int __get_min_undequeued_buffer_count(
                      const struct preview_stream_ops *w,
                      int *count)
    {
	printf("get min undequeue buffer count %d\r\n",*count);
	*count=1;
	return 0;
    }



void initHalPreviewWindow()
{
        mHalPreviewWindow.cancel_buffer = __cancel_buffer;
        mHalPreviewWindow.lock_buffer = __lock_buffer;
        mHalPreviewWindow.dequeue_buffer = __dequeue_buffer;
        mHalPreviewWindow.enqueue_buffer = __enqueue_buffer;
        mHalPreviewWindow.set_buffer_count = __set_buffer_count;
        mHalPreviewWindow.set_buffers_geometry = __set_buffers_geometry;
        mHalPreviewWindow.set_crop = __set_crop;
        mHalPreviewWindow.set_timestamp = __set_timestamp;
        mHalPreviewWindow.set_usage = __set_usage;
        mHalPreviewWindow.set_swap_interval = __set_swap_interval;

        mHalPreviewWindow.get_min_undequeued_buffer_count =
                __get_min_undequeued_buffer_count;
}


static int eng_test_fb_open(void)
{
	int i;
	void *bits;
	int offset_page_align;

	if(fb_fd==-1)
		fb_fd = open(SPRD_FB_DEV,O_RDWR);

	if(fb_fd<0) {
        printf("DCAM: %s Cannot open '%s': %d, %s\r\n", __func__, SPRD_FB_DEV, errno,  strerror(errno));
        return -1;
	}

    if(ioctl(fb_fd, FBIOGET_FSCREENINFO,&fix))
    {
        printf("DCAM: %s failed to get fix\r\n",__func__);
        close(fb_fd);
        return -1;
    }

    if(ioctl(fb_fd, FBIOGET_VSCREENINFO, &var))
    {
        printf("DCAM: %s failed to get var\r\n",__func__);
        close(fb_fd);
        return -1;
    }

	printf("%s: fix.smem_len=%d\r\n",__func__, fix.smem_len);

	bits = mmap(0, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    if (bits == MAP_FAILED) {
        printf("DCAM: failed to mmap framebuffer\r\n");
        return -1;
    }

	LOGD("mmitest %s: var.yres=%d; var.xres=%d\r\n",__func__, var.yres, var.xres);



	//set framebuffer address
	memset(&fb_buf, 0, sizeof(fb_buf));
	fb_buf[0].virt_addr = (uint32_t)bits;
    fb_buf[0].phys_addr = fix.smem_start;
	fb_buf[0].length = var.yres * var.xres * (var.bits_per_pixel/8);

    fb_buf[1].virt_addr = (uint32_t)(((unsigned) bits) + var.yres * var.xres * (var.bits_per_pixel/8));
	fb_buf[1].phys_addr = fix.smem_start+ var.yres * var.xres * (var.bits_per_pixel/8);
	fb_buf[1].length = var.yres * var.xres * (var.bits_per_pixel/8);

    fb_buf[2].virt_addr = (uint32_t)tmpbuf;
	fb_buf[2].length = var.yres * var.xres * (var.bits_per_pixel/8);

	fb_buf[3].virt_addr = (uint32_t)tmpbuf1;
	fb_buf[3].length = var.yres * var.xres * (var.bits_per_pixel/8);

	for(i=0; i<3; i++){
		LOGD("DCAM: buf[%d] virt_addr=0x%x, phys_addr=0x%x, length=%d\r\n", \
			i, fb_buf[i].virt_addr,fb_buf[i].phys_addr,fb_buf[i].length);
	}

	return 0;

}

static hw_module_t *fcfmodule;
static camera_module_t *fcmModule;
static camera_device_t *fcmDevice;
static hw_module_t *fcmodule;


int fcamera_start(void)
{
   int i,rc;
   int ret=0;
   mGrallocBufferIndex=0;
   front_back=0;
   ui_clear_rows(0,20);
   ui_set_color(CL_GREEN);//++++++++++


   gr_flip();
   if(hw_get_module(GRALLOC_HARDWARE_MODULE_ID,  (const hw_module_t **)&fcfmodule)<0)
   {
	LOGD("mmitest could not load gralloc module \r\n");
	return 0;
   }


   LOGD("mmitest open gralloc ok\r\n");
   rc=fcfmodule->methods->open(fcfmodule, GRALLOC_HARDWARE_GPU0, (struct hw_device_t**)&gdevice);

   if(rc!=0)
   {
	LOGD("mmitest open gpu failed \r\n");
	return 0;
   }


   eng_test_fb_open();

   if (hw_get_module(CAMERA_HARDWARE_MODULE_ID,(const hw_module_t **)&fcmModule) < 0)
   {
        LOGD("mmitest Could not load camera HAL module\r\n");
        mNumberOfCameras = 0;
	return 0;
   }

    LOGD("mmitestLoaded \"%s\" camera module\r\n", fcmModule->common.name);
    mNumberOfCameras = fcmModule->get_number_of_cameras();

    LOGD("mmitest have %d cameras\r\n", mNumberOfCameras);

    for(i=0;i<mNumberOfCameras;i++)
	{
        struct camera_info info;
        fcmModule->get_camera_info(i, &info);
        LOGD("mmitest camera %d face %d ori %d version %d \r\n",info.facing,info.orientation,info.device_version);
	}
    LOGD(" mmitest open device 1 \r\n");
    fcmodule=&fcmModule->common;

    rc = fcmodule->methods->open(fcmodule,"1",(hw_device_t **)&fcmDevice);
    if (rc != 0)
    {
	 LOGD("mmitest Could not open camera 1: %d", rc);
	 return 0;
    }
    fcamera_para=fcmDevice->ops->get_parameters(fcmDevice);

    LOGD("mmitest parameter0=%s\n",fcamera_para);
    LOGD("mmitest parameter0=%s\n",fcamera_para+512);
    LOGD("mmitest parameter0=%s\n",fcamera_para+1024);

    fc_para=strstr(fcamera_para,"preview-size=");
    strncpy(fc_para,"preview-size=640x480",20);

    LOGD("mmitest parameter1=%s\n",fcamera_para);
    LOGD("mmitest parameter1=%s\n",fcamera_para+512);
    LOGD("mmitest parameter1=%s\n",fcamera_para+1024);

    LOGD(" mmitest start  set_parameters\r\n");
    fcmDevice->ops->set_parameters(fcmDevice,fcamera_para);
    LOGD(" mmitest start  stop_parameters\r\n");
    usleep(500*1000);
    fcmDevice->ops->set_callbacks(fcmDevice,
                                   __notify_cb,
                                   __data_cb,
                                   __data_cb_timestamp,
                                   __get_memory,
                                   NULL);
    LOGD("mmitest open device 1 ok\r\n");


    initHalPreviewWindow();
    LOGD("mmitest after inithal\r\n");
    fcmDevice->ops->preview_enabled(fcmDevice);
    LOGD("mmitest after enable\r\n");
    fcmDevice->ops->set_preview_window(fcmDevice,&mHalPreviewWindow);
    LOGD("mmitest start preview\r\n");
    fcmDevice->ops->enable_msg_type(fcmDevice, CAMERA_MSG_PREVIEW_FRAME);

    fcmDevice->ops->start_preview(fcmDevice);
    LOGD("mmitest start to key\r\n");

    return ret;
}


int flashlightSetValue(int value)
{
    int ret = 0;
    char cmd[200] = " ";

    FUN_ENTER;
    sprintf(cmd, "echo 0x%02x > /sys/class/flash_test/flash_test/flash_value", value);
    ret = system(cmd) ? -1 : 0;
    LOGD("cmd = %s,ret = %d\n", cmd,ret);
    FUN_EXIT;

    return ret;
}



static hw_module_t *bcfmodule;
static camera_module_t *bcmModule;
static camera_device_t *bcmDevice;
static hw_module_t *bcmodule;

extern int usbin_state;

void * bcamera_start(void)
{
   int i,rc;
   int ret=0;
   front_back=1;
   mGrallocBufferIndex=0;

   ui_clear_rows(0,20);
   ui_set_color(CL_GREEN);//++++++
   gr_flip();

   if(hw_get_module(GRALLOC_HARDWARE_MODULE_ID,  (const hw_module_t **)&bcfmodule)<0)
   {
	LOGD("mmitest could not load gralloc module \r\n");
//	return 1;
   }

   LOGD("mmitest open gralloc ok\r\n");
   rc=bcfmodule->methods->open(bcfmodule, GRALLOC_HARDWARE_GPU0, (struct hw_device_t**)&gdevice);

   if(rc!=0)
   {
	LOGD("mmitest open gpu failed \r\n");
//	return 1;
   }

   eng_test_fb_open();


   if (hw_get_module(CAMERA_HARDWARE_MODULE_ID,(const hw_module_t **)&bcmModule) < 0)
   {
        LOGD("mmitest Could not load camera HAL module\r\n");
        mNumberOfCameras = 0;
	//	return 1;
   }

   LOGD("mmitestLoaded \"%s\" camera module\r\n", bcmModule->common.name);
   mNumberOfCameras = bcmModule->get_number_of_cameras();
   LOGD("mmitest have %d cameras\r\n", mNumberOfCameras);

   for(i=0;i<mNumberOfCameras;i++)
	{
		struct camera_info info;
        bcmModule->get_camera_info(i, &info);
        LOGD("mmitest camera %d face %d ori %d version %d \r\n",info.facing,info.orientation,info.device_version);
	}

   LOGD(" mmitest open device 0 \r\n");
   bcmodule=&bcmModule->common;
   rc = bcmodule->methods->open(bcmodule,"0",(hw_device_t **)&bcmDevice);
   if (rc != 0)
   {
	LOGD("mmitest Could not open camera 0: %d", rc);
	// return 1;
    }
   LOGD("mmitest open device 0 ok\r\n");
   bcamera_para=bcmDevice->ops->get_parameters(bcmDevice);

   LOGD("mmitest parameter0=%s\n",bcamera_para);
   LOGD("mmitest parameter0=%s\n",bcamera_para+512);
   LOGD("mmitest parameter0=%s\n",bcamera_para+1024);
   LOGD("mmitest parameter0=%s\n",bcamera_para+1536);

   bc_para=strstr(bcamera_para,"preview-size=");


   LOGD("mmitest parameter bcamera_para=%p\n",bcamera_para);

   LOGD("mmitest parameter bc_para=%p\n",bc_para);

   LOGD("mmitest parameter bc_para=%s\n",bc_para);

   strncpy(bc_para,"preview-size=640x480",20);

   LOGD("mmitest parameter bc_para=%s\n",bc_para);

   LOGD("mmitest parameter1=%s\n",bcamera_para);

   LOGD("mmitest parameter1=%s\n",bcamera_para+512);

   LOGD("mmitest parameter1=%s\n",bcamera_para+1024);

   LOGD("mmitest parameter1=%s\n",bcamera_para+1536);


   LOGD(" mmitest start  set_parameters\r\n");
   bcmDevice->ops->set_parameters(bcmDevice,bcamera_para);
   LOGD(" mmitest start  stop_parameters\r\n");
   usleep(500*1000);
   bcmDevice->ops->set_callbacks(bcmDevice,
                                   __notify_cb,
                                   __data_cb,
                                   __data_cb_timestamp,
                                   __get_memory,
                                   NULL);
   initHalPreviewWindow();
   LOGD("mmitest after inithal\r\n");
   bcmDevice->ops->preview_enabled(bcmDevice);
   LOGD("mmitest after enable\r\n");
   bcmDevice->ops->set_preview_window(bcmDevice,&mHalPreviewWindow);
   LOGD("mmitest start preview\r\n");
   bcmDevice->ops->enable_msg_type(bcmDevice, CAMERA_MSG_PREVIEW_FRAME);
   bcmDevice->ops->start_preview(bcmDevice);
   LOGD("mmitest start to key\r\n");

   flashlightSetValue(17);

   return 0;
}


static int bcamera_close(void)
{
	int i;
	flashlightSetValue(16);

	bcmDevice->ops->disable_msg_type(bcmDevice, CAMERA_MSG_PREVIEW_FRAME);
	LOGD("mmitest after disable msg\r\n");
	bcmDevice->ops->set_callbacks(bcmDevice, NULL,NULL,NULL,NULL,NULL);
	LOGD("mmitest after reset callback\r\n");
	bcmDevice->ops->stop_preview(bcmDevice);
	LOGD("mmitest after stop preivew\r\n");
	bcmDevice->ops->release(bcmDevice);
	LOGD("mmitest after release\r\n");
	bcmDevice->common.close((hw_device_t*)bcmDevice);
	for(mGrallocBufferIndex=0;mGrallocBufferIndex<buffer_count;mGrallocBufferIndex++)
		{
			gdevice->free(gdevice,buffers2[i].handle);
			buffers2[i].inuse=0;
		}
	mGrallocBufferIndex=0;
	gdevice->common.close(&gdevice->common);
	LOGD("mmitest after close\r\n");

	if(fb_fd>0)
		{
			close(fb_fd);
			fb_fd=-1;
		}
	bcfmodule = NULL;

	bcmodule = NULL;
	LOGD("mmitest after close\r\n");
	return 0;
}

static int fcamera_close(void)
{
	int i;
	fcmDevice->ops->disable_msg_type(fcmDevice, CAMERA_MSG_PREVIEW_FRAME);
	LOGD("mmitest after disable msg\r\n");
	fcmDevice->ops->set_callbacks(fcmDevice, NULL,NULL,NULL,NULL,NULL);
	LOGD("mmitest after reset callback\r\n");
	fcmDevice->ops->stop_preview(fcmDevice);
	LOGD("mmitest after stop preivew\r\n");
	fcmDevice->ops->release(fcmDevice);
	LOGD("mmitest after release\r\n");

	fcmDevice->common.close((hw_device_t*)fcmDevice);
	for(mGrallocBufferIndex=0;mGrallocBufferIndex<buffer_count;mGrallocBufferIndex++)
		{
			gdevice->free(gdevice,buffers2[i].handle);
			buffers2[i].inuse=0;
		}
	mGrallocBufferIndex=0;
	gdevice->common.close(&gdevice->common);
	LOGD("mmitest after close\r\n");
	if(fb_fd>0)
		{
			close(fb_fd);
			fb_fd=-1;
		}
	fcfmodule = NULL;
	fcmodule = NULL;
	return 0;
}



int test_bcamera_start(void)
{
	pthread_t t;
	int ret = 0;
	usbin_state=0;

	pthread_create(&t, NULL, (void*(*)(void*))bcamera_start, NULL);
	ret = ui_handle_button(NULL, NULL);// NULL,
	pthread_join(t, NULL); /* wait "handle key" thread exit. */
	bcamera_close();

	save_result(CASE_TEST_BCAMERA,ret);
	save_result(CASE_TEST_FLASH,ret);

	return ret;
}


int test_fcamera_start(void)
{
	pthread_t t;
	int ret = 0;
	usbin_state=0;

	pthread_create(&t, NULL, (void*(*)(void*))fcamera_start, NULL);
	ret = ui_handle_button(NULL, NULL);// NULL,
	pthread_join(t, NULL); /* wait "handle key" thread exit. */
	fcamera_close();

	save_result(CASE_TEST_FCAMERA,ret);

	return ret;
}






