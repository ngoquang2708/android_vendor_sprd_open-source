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


#ifndef _OVERLAY_NATIVEWINDOW_H_
#define _OVERLAY_NATIVEWINDOW_H_

#include <binder/MemoryHeapIon.h>
#include <ui/FramebufferNativeWindow.h>
#include <ui/ANativeObjectBase.h>
#include "gralloc_priv.h"
#include "Utility.h"


/* Overlay device info */
typedef struct
{
    int fbfd;
    unsigned int fb_width;
    unsigned int fb_height;
    uint32_t stride;

    uint32_t overlay_phy_addr;
    void *overlay_v_addr;
    uint32_t overlay_buf_size;

    private_handle_t *bufHandle_1;
    private_handle_t *bufHandle_2;

    int overlay_gpu_flag;
    int overlay_sur_flag;
} overlayDevice_t;

namespace android {
// ----------------------------------------------------------------------------

//class Surface;
class NativeBuffer
    : public ANativeObjectBase<
        ANativeWindowBuffer,
        NativeBuffer,
        LightRefBase<NativeBuffer> >
{
public:
    NativeBuffer(int w, int h, int f, int u) : BASE() {
        ANativeWindowBuffer::width  = w;
        ANativeWindowBuffer::height = h;
        ANativeWindowBuffer::format = f;
        ANativeWindowBuffer::usage  = u;
    }
private:
    friend class LightRefBase<NativeBuffer>;
    ~NativeBuffer() { }; // this class cannot be overloaded
};


#define NUM_FRAME_BUFFERS  2


class OverlayNativeWindow   //: public overlayNativeWindow
    : public ANativeObjectBase<
        ANativeWindow,
        OverlayNativeWindow,
        LightRefBase<OverlayNativeWindow> >
{
public:
    OverlayNativeWindow(overlayDevice_t* overlayDev);
    ~OverlayNativeWindow();

    bool Init();

private:
    overlayDevice_t* mOverlayDev;
    int32_t mNumBuffers;
    int32_t mNumFreeBuffers;
    int32_t mBufferHead;
    sp<NativeBuffer> buffers[NUM_FRAME_BUFFERS];
    sp<NativeBuffer> front;

    mutable Mutex mutex;
    Condition mCondition;
    int32_t mCurrentBufferIndex;
    bool mUpdateOnDemand;


    friend class LightRefBase<OverlayNativeWindow>;

    static int setSwapInterval(ANativeWindow* window, int interval);
    static int dequeueBuffer(ANativeWindow* window, ANativeWindowBuffer** buffer);
    static int lockBuffer(ANativeWindow* window, ANativeWindowBuffer* buffer);
    static int queueBuffer(ANativeWindow* window, ANativeWindowBuffer* buffer);
    static int query(const ANativeWindow* window, int what, int* value);
    static int perform(ANativeWindow* window, int operation, ...);



    private_handle_t *wrapBuffer(unsigned int w, unsigned int h,
                                 int format, int index);

    void unWrapBuffer(private_handle_t *h);

    uint32_t getBufferPhyAddr(int index);
    uint32_t getBufferVirAddr(int index);

    inline unsigned int round_up_to_page_size(unsigned int x)
    {
         return (x + (PAGE_SIZE-1)) & ~(PAGE_SIZE-1);
    }

};
}; //android space

#endif
