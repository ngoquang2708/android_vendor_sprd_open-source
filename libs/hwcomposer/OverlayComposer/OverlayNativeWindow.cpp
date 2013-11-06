/*
 * Copyright (C) 2010 The Android Open Source Project
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


/******************************************************************************
 **                   Edit    History                                         *
 **---------------------------------------------------------------------------*
 ** DATE          Module              DESCRIPTION                             *
 ** 16/08/2013    Hardware Composer   Add a new feature to Harware composer,  *
 **                                   verlayComposer use GPU to do the        *
 **                                   Hardware layer blending on Overlay      *
 **                                   buffer, and then post the OVerlay       *
 **                                   buffer to Display                       *
 ******************************************************************************
 ** Author:         fushou.yang@spreadtrum.com                                *
 **                 zhongjun.chen@spreadtrum.com                              *
 *****************************************************************************/



#include <hardware/hwcomposer.h>
#include <hardware/hardware.h>
#include <sys/ioctl.h>
#include "sprd_fb.h"

#include <cutils/properties.h>
#include "dump_bmp.h"

#include "OverlayNativeWindow.h"


extern int dump_layer(const char* path ,const char* pSrc , const char* ptype,
                      int width , int height , int format ,int64_t randNum,
                      int index , int LayerIndex = 0);

namespace android {


OverlayNativeWindow::OverlayNativeWindow(overlayDevice_t * OLDev)
    : mOverlayDev(OLDev), mNumBuffers(NUM_FRAME_BUFFERS),
      mNumFreeBuffers(NUM_FRAME_BUFFERS), mBufferHead(0),
      mUpdateOnDemand(false)
{

}

bool OverlayNativeWindow::Init()
{
    int dstFormat = HAL_PIXEL_FORMAT_RGBA_8888;

    mOverlayDev->bufHandle_1 = wrapBuffer(mOverlayDev->fb_width, mOverlayDev->fb_height, dstFormat, 0);
    if (mOverlayDev->bufHandle_1 == NULL)
    {
        ALOGE("wrapBuffer Failed, can NOT get wrap handle");
        return false;
    }


    mOverlayDev->bufHandle_2 = wrapBuffer(mOverlayDev->fb_width, mOverlayDev->fb_height, dstFormat, 1);
    if (mOverlayDev->bufHandle_2 == NULL)
    {
        ALOGE("wrapBuffer Failed, can NOT get wrap handle");
        return false;
    }

    mOverlayDev->stride = mOverlayDev->bufHandle_2->width;


    buffers[0] = new NativeBuffer(
                     mOverlayDev->bufHandle_1->width, mOverlayDev->bufHandle_1->height,
                     mOverlayDev->bufHandle_1->format, NULL);

    buffers[0]->handle = mOverlayDev->bufHandle_1;
    buffers[0]->stride = mOverlayDev->stride;

    buffers[1] = new NativeBuffer(
                     mOverlayDev->bufHandle_2->width, mOverlayDev->bufHandle_2->height,
                     mOverlayDev->bufHandle_2->format, NULL);

    buffers[1]->handle = mOverlayDev->bufHandle_2;
    buffers[1]->stride = mOverlayDev->stride;
    //ALOGI(" %s %d handle1 is 0x%x handle2 is 0x%x",__func__,__LINE__,buffers[0]->handle,buffers[1]->handle);


    ANativeWindow::setSwapInterval = setSwapInterval;
    ANativeWindow::dequeueBuffer = dequeueBuffer;
    ANativeWindow::lockBuffer = lockBuffer;
    ANativeWindow::queueBuffer = queueBuffer;
    ANativeWindow::query = query;
    ANativeWindow::perform = perform;

    return true;
}

OverlayNativeWindow::~OverlayNativeWindow()
{
    buffers[0]->handle = NULL;
    buffers[1]->handle = NULL;

    if (mOverlayDev->bufHandle_1)
    {
        unWrapBuffer(mOverlayDev->bufHandle_1);
        mOverlayDev->bufHandle_1 = NULL;
    }

    if (mOverlayDev->bufHandle_2)
    {
        unWrapBuffer(mOverlayDev->bufHandle_2);
        mOverlayDev->bufHandle_2 = NULL;
    }
}

uint32_t OverlayNativeWindow::getBufferPhyAddr(int index)
{
    return (mOverlayDev->overlay_phy_addr + mOverlayDev->overlay_buf_size * index);
}

uint32_t OverlayNativeWindow::getBufferVirAddr(int index)
{
    return ((unsigned int)mOverlayDev->overlay_v_addr + mOverlayDev->overlay_buf_size * index);

}

private_handle_t *OverlayNativeWindow::wrapBuffer(unsigned int w, unsigned int h, int format, int index)
{
    private_handle_t *pH;

    ump_handle ump_h;

    uint32_t size;
    uint32_t stride;

    getSizeStride(w, h, format, size, stride);

    ump_h = ump_handle_create_from_phys_block(getBufferPhyAddr(index),
                                              size);
    if (ump_h == NULL)
    {
        ALOGE("ump_h create fail");
        return NULL;
    }
      ALOGI("create come here phy 0x%x, w is %d  h is %d size is %d line is %d",
        getBufferPhyAddr(index), w, h, size, __LINE__);

    pH = new private_handle_t(private_handle_t::PRIV_FLAGS_USES_UMP,
                             size, getBufferVirAddr(index),
                             private_handle_t::LOCK_STATE_MAPPED,
                             ump_secure_id_get(ump_h),
                             ump_h);
    pH->width = stride;
    pH->height = h;
    pH->format = format;
    pH->phyaddr = getBufferPhyAddr(index);

    return pH;

}

void OverlayNativeWindow::unWrapBuffer(private_handle_t *h)
{
    ump_free_handle_from_mapped_phys_block((ump_handle)h->ump_mem_handle);
}



int OverlayNativeWindow::dequeueBuffer(ANativeWindow* window,
        ANativeWindowBuffer** buffer)
{
    OverlayNativeWindow* self = getSelf(window);
    Mutex::Autolock _l(self->mutex);
    overlayDevice_t* dev = self->mOverlayDev;
    //ALOGI("OverlayNativeWindow::dequeueBuffer");
    int index = self->mBufferHead;

    // wait for a free buffer
    while (!self->mNumFreeBuffers) {
        self->mCondition.wait(self->mutex);
    }
    // get this buffer
    self->mNumFreeBuffers--;
    self->mCurrentBufferIndex = index;

    *buffer = self->buffers[index].get();

    return 0;
}

#define OVERLAY_BUF_NUM 2

int OverlayNativeWindow::queueBuffer(ANativeWindow* window,
        ANativeWindowBuffer* buffer)
{
    //ALOGI("OverlayNativeWindow::queueBuffer %d",__LINE__);
    uint32_t current_overlay_paddr = 0;
    OverlayNativeWindow* self = getSelf(window);
    Mutex::Autolock _l(self->mutex);
    overlayDevice_t* dev = self->mOverlayDev;

    EGLBoolean result;
    int layer_indexs = 0;

    current_overlay_paddr = dev->overlay_phy_addr + dev->overlay_buf_size * self->mCurrentBufferIndex;


    uint32_t current_overlay_vir_addr = ((unsigned int)dev->overlay_v_addr) + dev->overlay_buf_size*self->mCurrentBufferIndex;

    /*  DUMP INFO */
    char value[256];
    if(0 != property_get("dump.hwcomposer.flag" , value , "0")) {
            int flag = atoi(value);
            if(flag == 16) {
                static int index = 0;
                dump_layer("/data/Image/", (char *)current_overlay_vir_addr , "GPUOverlayBlend",
                           dev->fb_width , dev->fb_height , HAL_PIXEL_FORMAT_RGBA_8888, 2 , index);
                index++;
            }
    }

    struct overlay_setting ov_setting;
    ov_setting.layer_index = SPRD_LAYERS_IMG;
    //ov_setting.layer_index = SPRD_LAYERS_OSD;
    ov_setting.data_type = SPRD_DATA_FORMAT_RGB888;
    ov_setting.y_endian = SPRD_DATA_ENDIAN_B0B1B2B3;//dispc need RGBA
    ov_setting.uv_endian = SPRD_DATA_ENDIAN_B0B1B2B3;
    ov_setting.rb_switch = 1;

    ov_setting.rect.x = 0;
    ov_setting.rect.y = 0;
    ov_setting.rect.w = dev->fb_width;
    ov_setting.rect.h = dev->fb_height;
    ov_setting.buffer = (unsigned char*)current_overlay_paddr;


    if (ioctl(dev->fbfd, SPRD_FB_SET_OVERLAY, &ov_setting) == -1) {
        ALOGE("fail video SPRD_FB_SET_OVERLAY");//
        ioctl(dev->fbfd, SPRD_FB_SET_OVERLAY, &ov_setting);
    }

    self->mBufferHead = (self->mBufferHead + 1)%OVERLAY_BUF_NUM;


    struct overlay_display_setting display_setting;
#ifdef  _SUPPORT_SYNC_DISP
    display_setting.display_mode = SPRD_DISPLAY_OVERLAY_SYNC;
#else
    display_setting.display_mode = SPRD_DISPLAY_OVERLAY_ASYNC;
#endif

    display_setting.layer_index = SPRD_LAYERS_IMG;
    display_setting.rect.x = 0;
    display_setting.rect.y = 0;
    display_setting.rect.w = dev->fb_width;
    display_setting.rect.h = dev->fb_height;

    ioctl(dev->fbfd, SPRD_FB_DISPLAY_OVERLAY, &display_setting);


    const int index = self->mCurrentBufferIndex;
    self->front = static_cast<NativeBuffer*>(buffer);
    self->mNumFreeBuffers++;
    self->mCondition.broadcast();

    dev->overlay_sur_flag = 0;

    return 0;
}

int OverlayNativeWindow::lockBuffer(ANativeWindow* window,
        ANativeWindowBuffer* buffer)
{
    OverlayNativeWindow* self = getSelf(window);
    Mutex::Autolock _l(self->mutex);

    const int index = self->mCurrentBufferIndex;

    // wait that the buffer we're locking is not front anymore
    while (self->front == buffer) {
        self->mCondition.wait(self->mutex);
    }

    return 0;
}

int OverlayNativeWindow::query(const ANativeWindow* window,
        int what, int* value)
{
    const OverlayNativeWindow* self = getSelf(window);
    Mutex::Autolock _l(self->mutex);

#ifdef VIDEO_LAYER_USE_RGB
    int dstFormat = HAL_PIXEL_FORMAT_RGBX_8888;
#else
    int dstFormat = HAL_PIXEL_FORMAT_YCbCr_420_SP;
#endif

    //ALOGI("%s %d",__func__,__LINE__);
    overlayDevice_t* dev = self->mOverlayDev;

    switch (what) {
        case NATIVE_WINDOW_WIDTH:
            *value = dev->fb_width;
            return NO_ERROR;
        case NATIVE_WINDOW_HEIGHT:
            *value = dev->fb_height;
            return NO_ERROR;
        case NATIVE_WINDOW_FORMAT:
            *value = HAL_PIXEL_FORMAT_RGBA_8888;
            return NO_ERROR;
        case NATIVE_WINDOW_CONCRETE_TYPE:
            *value = NATIVE_WINDOW_FRAMEBUFFER;
            return NO_ERROR;
        case NATIVE_WINDOW_QUEUES_TO_WINDOW_COMPOSER:
            *value = 0;
            return NO_ERROR;
        case NATIVE_WINDOW_DEFAULT_WIDTH:
            *value = dev->fb_width;
            return NO_ERROR;
        case NATIVE_WINDOW_DEFAULT_HEIGHT:
            *value = dev->fb_height;
            return NO_ERROR;
        case NATIVE_WINDOW_TRANSFORM_HINT:
            *value = 0;
            return NO_ERROR;
    }
    *value = 0;
    return BAD_VALUE;

}

int OverlayNativeWindow::perform(ANativeWindow* window,
        int operation, ...)
{
    switch (operation) {
        case NATIVE_WINDOW_CONNECT:
        case NATIVE_WINDOW_DISCONNECT:
        case NATIVE_WINDOW_SET_USAGE:
        case NATIVE_WINDOW_SET_BUFFERS_GEOMETRY:
        case NATIVE_WINDOW_SET_BUFFERS_DIMENSIONS:
        case NATIVE_WINDOW_SET_BUFFERS_FORMAT:
        case NATIVE_WINDOW_SET_BUFFERS_TRANSFORM:
        case NATIVE_WINDOW_API_CONNECT:
        case NATIVE_WINDOW_API_DISCONNECT:
            // TODO: we should implement these
            return NO_ERROR;

        case NATIVE_WINDOW_LOCK:
        case NATIVE_WINDOW_UNLOCK_AND_POST:
        case NATIVE_WINDOW_SET_CROP:
        case NATIVE_WINDOW_SET_BUFFER_COUNT:
        case NATIVE_WINDOW_SET_BUFFERS_TIMESTAMP:
        case NATIVE_WINDOW_SET_SCALING_MODE:
            return INVALID_OPERATION;
    }
    return NAME_NOT_FOUND;
}


int OverlayNativeWindow::setSwapInterval(
        ANativeWindow* window, int interval)
{
    //hwc_composer_device_t* fb = getSelf(window)->hwc_dev;
  //  return fb->setSwapInterval(fb, interval);
    return 0;
}

};
