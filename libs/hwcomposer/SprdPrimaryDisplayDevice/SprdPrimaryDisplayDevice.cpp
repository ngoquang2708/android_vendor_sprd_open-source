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
 ** 22/09/2013    Hardware Composer   Responsible for processing some         *
 **                                   Hardware layers. These layers comply    *
 **                                   with display controller specification,  *
 **                                   can be displayed directly, bypass       *
 **                                   SurfaceFligner composition. It will     *
 **                                   improve system performance.             *
 ******************************************************************************
 ** File:SprdPrimaryDisplayDevice.cpp DESCRIPTION                             *
 **                                   Manage the PrimaryDisplayDevice         *
 **                                   including prepare and commit            *
 ******************************************************************************
 ******************************************************************************
 ** Author:         zhongjun.chen@spreadtrum.com                              *
 *****************************************************************************/

#include "SprdPrimaryDisplayDevice.h"

using namespace android;

SprdPrimaryDisplayDevice:: SprdPrimaryDisplayDevice()
   : mFBInfo(0),
     mLayerList(0),
     mOverlayPlane(0),
     mPrimaryPlane(0),
#ifdef OVERLAY_COMPOSER_GPU
     mOverlayComposer(NULL),
#endif
     mVsyncEvent(0),
     mUtil(0),
     mPostFrameBuffer(true),
     mForceOverlayFlag(false),
     mDebugFlag(0),
     mDumpFlag(0)
    {

    }

bool SprdPrimaryDisplayDevice:: Init(FrameBufferInfo **fbInfo)
{
    loadFrameBufferHAL(&mFBInfo);
    if (mFBInfo == NULL) {
        ALOGE("Can NOT get FrameBuffer info");
        return false;
    }

    mLayerList = new SprdHWLayerList(mFBInfo);
    if (mLayerList == NULL)
    {
        ALOGE("new SprdHWLayerList failed");
        return false;
    }

    mOverlayPlane = new SprdOverlayPlane(mFBInfo);
    if (mOverlayPlane == NULL)
    {
        ALOGE("new SprdOverlayPlane failed");
        return false;
    }

    mPrimaryPlane = new SprdPrimaryPlane(mFBInfo);
    if (mPrimaryPlane == NULL)
    {
        ALOGE("new SprdPrimaryPlane failed");
        return false;
    }

#ifdef OVERLAY_COMPOSER_GPU
    mOverlayComposer = new OverlayComposer(mPrimaryPlane);
    if (mOverlayComposer == NULL)
    {
        ALOGE("new OverlayComposer failed");
        return false;
    }
#endif

    mVsyncEvent = new SprdVsyncEvent();
    if (mVsyncEvent == NULL)
    {
        ALOGE("new SprdVsyncEvent failed");
        return false;
    }

    mUtil = new SprdUtil(mFBInfo);
    if (mUtil == NULL)
    {
        ALOGE("new SprdUtil failed");
        return false;
    }

    *fbInfo = mFBInfo;

    return true;
}

SprdPrimaryDisplayDevice:: ~SprdPrimaryDisplayDevice()
{
    eventControl(0);

    if (mUtil != NULL)
    {
        delete mUtil;
        mUtil = NULL;
    }

    if (mVsyncEvent != NULL)
    {
        mVsyncEvent->requestExitAndWait();
    }

    if (mPrimaryPlane)
    {
        delete mPrimaryPlane;
        mPrimaryPlane = NULL;
    }

    if (mOverlayPlane)
    {
        delete mOverlayPlane;
        mOverlayPlane = NULL;
    }

    if (mLayerList)
    {
        delete mLayerList;
        mLayerList = NULL;
    }
}

int SprdPrimaryDisplayDevice:: getDisplayAttributes(DisplayAttributes *dpyAttributes)
{
    float refreshRate = 60.0;
    framebuffer_device_t *fbDev = mFBInfo->fbDev;

    if (dpyAttributes == NULL)
    {
        ALOGE("Input parameter is NULL");
        return -1;
    }

    if (fbDev->fps > 0)
    {
        refreshRate = fbDev->fps;
    }

    dpyAttributes->vsync_period = 1000000000l / refreshRate;
    dpyAttributes->xres = mFBInfo->fb_width;
    dpyAttributes->yres = mFBInfo->fb_height;
    dpyAttributes->stride = mFBInfo->stride;
    dpyAttributes->xdpi = mFBInfo->xdpi * 1000.0;
    dpyAttributes->ydpi = mFBInfo->ydpi * 1000.0;
    dpyAttributes->connected = true;

    return 0;
}

int SprdPrimaryDisplayDevice:: prepare(hwc_display_contents_1_t *list)
{
    bool ret = false;
    mForceOverlayFlag = false;

    queryDebugFlag(&mDebugFlag);

    ALOGI_IF(mDebugFlag, "HWC start prepare");

    if (list == NULL)
    {
        ALOGE("The input parameters list is NULl");
        return -1;
    }

    ret = mLayerList->updateGeometry(list);
    if (ret == false)
    {
        ALOGE("(FILE:%s, line:%d, func:%s) updateGeometry failed",
              __FILE__, __LINE__, __func__);
        return -1;
    }

    ret = mLayerList->revistGeometry(&mForceOverlayFlag);
    if (ret == false)
    {
        ALOGE("(FILE:%s, line:%d, func:%s) revistGeometry failed",
              __FILE__, __LINE__, __func__);
        return -1;
    }

    return 0;
}

int SprdPrimaryDisplayDevice:: commit(hwc_display_contents_1_t* list)
{
    bool ret = false;
    bool DisplayFBTargetLayerFlag = false;
    bool DirectDisplayFlag = false;
    SprdHWLayer *OverlayLayer = NULL;
    SprdHWLayer *PrimaryLayer = NULL;
    private_handle_t* buffer1 = NULL;
    private_handle_t* buffer2 = NULL;

    hwc_layer_1_t *FBTargetLayer = NULL;

    if (list == NULL)
    {
        /*
         * release our resources, the screen is turning off
         * in our case, there is nothing to do.
         * */
         return 0;
    }

    ALOGI_IF(mDebugFlag, "HWC start commit");

    for (size_t i = 0; i < list->numHwLayers; i++)
    {
        hwc_layer_1_t *AndroidLayer = &(list->hwLayers[i]);

        if (AndroidLayer && AndroidLayer->compositionType == HWC_OVERLAY)
        {
            if (mLayerList->getPlaneType(i) == PLANE_OVERLAY)
            {
                OverlayLayer = mLayerList->getSprdLayer(i);

                OverlayLayer->setAndroidLayer(AndroidLayer);

                mOverlayPlane->AttachOverlayLayer(OverlayLayer);
            }
            else if (mLayerList->getPlaneType(i) == PLANE_PRIMARY)
            {
                bool DirectDisplay = ((mForceOverlayFlag == false)&&
                                      (list->numHwLayers == 1));

                PrimaryLayer = mLayerList->getSprdLayer(i);

                PrimaryLayer->setAndroidLayer(AndroidLayer);

                mPrimaryPlane->AttachPrimaryLayer(PrimaryLayer, DirectDisplay);
            }
        }
        else if (AndroidLayer && AndroidLayer->compositionType == HWC_FRAMEBUFFER_TARGET
                 && mForceOverlayFlag == false)
        {
            ALOGI_IF(mDebugFlag, "This is HWC_FRAMEBUFFER_TARGET layer");

            //mPrimaryPlane->AttachFramebufferTargetLayer(AndroidLayer);

            //DisplayFBTargetLayerFlag = true;

            FBTargetLayer = AndroidLayer;
        }
        AndroidLayer = NULL;
    }

    /*
     *  This is temporary methods for displaying Framebuffer target layer, has some bug in FB HAL.
     * */
    if (FBTargetLayer && mForceOverlayFlag == false)
    {
        const native_handle_t *pNativeHandle = FBTargetLayer->handle;
        struct private_handle_t *privateH = (struct private_handle_t *)pNativeHandle;

        ALOGI_IF(mDebugFlag, "Start Displaying FramebufferTarget layer");

        mFBInfo->fbDev->post(mFBInfo->fbDev, privateH);

        goto displayDone;
    }

#ifdef OVERLAY_COMPOSER_GPU
    if (mForceOverlayFlag)
    {
        ALOGI_IF(mDebugFlag, "Start OverlayComposer composition misson");
        mOverlayComposer->onComposer(list);

        ALOGI_IF(mDebugFlag, "Start OverlayComposer display misson");
        mOverlayComposer->onDisplay();

        goto displayDone;
    }
#endif

    if (OverlayLayer != NULL)
    {
        mOverlayPlane->dequeueBuffer();

        buffer1 = mOverlayPlane->getPlaneBuffer();
    }
    else
    {
        mOverlayPlane->disable();
    }

    if (PrimaryLayer != NULL ||
        DisplayFBTargetLayerFlag)
    {
        mPrimaryPlane->dequeueBuffer();

        if (DisplayFBTargetLayerFlag == false)
        {
            buffer2 = mPrimaryPlane->getPlaneBuffer();

            DirectDisplayFlag = mPrimaryPlane->GetDirectDisplay();
        }
    }
    else
    {
       mPrimaryPlane->disable();
    }


    if (DirectDisplayFlag == false && DisplayFBTargetLayerFlag == false)
    {
#ifdef TRANSFORM_USE_DCAM
        mUtil->transformLayer(OverlayLayer, PrimaryLayer, buffer1, buffer2);
#endif

#ifdef PROCESS_VIDEO_USE_GSP
        mUtil->composerLayers(OverlayLayer, PrimaryLayer, buffer1, buffer2);
#endif
    }

   buffer1 = NULL;
   buffer2 = NULL;

   if (mOverlayPlane->online())
   {
       mOverlayPlane->queueBuffer();
   }

   if (mPrimaryPlane->online())
   {
       mPrimaryPlane->queueBuffer();
   }


   if (OverlayLayer || PrimaryLayer ||
       DisplayFBTargetLayerFlag)
   {
       mPrimaryPlane->display(OverlayLayer, PrimaryLayer, DisplayFBTargetLayerFlag);
   }


displayDone:
   queryDumpFlag(&mDumpFlag);
   if (DisplayFBTargetLayerFlag &&
       (mDumpFlag & HWCOMPOSER_DUMP_FRAMEBUFFER_FLAG))
   {
       dumpFrameBuffer(mFBInfo->pFrontAddr, "FrameBuffer", mFBInfo->fb_width, mFBInfo->fb_height, mFBInfo->format);
   }

    OverlayLayer = NULL;
    PrimaryLayer = NULL;

    mForceOverlayFlag = false;
    DirectDisplayFlag = false;

    DisplayFBTargetLayerFlag = false;

    closeAcquireFDs(list);

    return 0;
}

void SprdPrimaryDisplayDevice:: setVsyncEventProcs(const hwc_procs_t *procs)
{
    sp<SprdVsyncEvent> VE = getVsyncEventHandle();
    if (VE == NULL)
    {
        ALOGE("getVsyncEventHandle failed");
        return;
    }

    VE->setVsyncEventProcs(procs);
}

void SprdPrimaryDisplayDevice:: eventControl(int enabled)
{
    sp<SprdVsyncEvent> VE = getVsyncEventHandle();
    if (VE == NULL)
    {
        ALOGE("getVsyncEventHandle failed");
        return;
    }

    VE->setEnabled(enabled);
}
