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
 ** File: SprdHWLayerList.h           DESCRIPTION                             *
 **                                   Mainly responsible for filtering HWLayer*
 **                                   list, find layers that meet OverlayPlane*
 **                                   and PrimaryPlane specifications and then*
 **                                   mark them as HWC_OVERLAY.               *
 ******************************************************************************
 ******************************************************************************
 ** Author:         zhongjun.chen@spreadtrum.com                              *
 *****************************************************************************/


#ifndef _SPRD_HWLAYER_LIST_H_
#define _SPRD_HWLAYER_LIST_H_

#include <hardware/hardware.h>
#include <hardware/hwcomposer.h>
#include <utils/RefBase.h>
#include <cutils/atomic.h>
#include <cutils/log.h>
#include "gralloc_priv.h"
#include "sc8825/dcam_hal.h"

#include "SprdFrameBufferHAL.h"

using namespace android;

/*
 *  YUV format layer info.
 * */
struct sprdYUV {
    uint32_t w;
    uint32_t h;
    uint32_t format;
    uint32_t y_addr;
    uint32_t u_addr;
    uint32_t v_addr;
};

/*
 *  Available layer rectangle.
 * */
struct sprdRect {
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
};

enum layerType {
    LAYER_OSD = 1,
    LAYER_OVERLAY,
    LAYER_INVALIDE
};

enum planeType {
    PLANE_PRIMARY = 1,
    PLANE_OVERLAY,
    PLANE_FRAMEBUFFER,
    PLANE_INVALIDATE
};


/*
 *  Android layers info from Android Framework is not enough,
 *  here, SprdHWLayer object just add some info.
 * */
class SprdHWLayer
{
public:
    SprdHWLayer()
        : mAndroidLayer(0), mLayerType(LAYER_INVALIDE), mFormat(-1),
          mDebugFlag(0)
    {

    }
    ~SprdHWLayer()
    {

    }

    inline void setAndroidLayer(hwc_layer_1_t *l)
    {
        mAndroidLayer = l;
    }

    inline hwc_layer_1_t *getAndroidLayer()
    {
        return mAndroidLayer;
    }

    inline void setLayerType(enum layerType t)
    {
        mLayerType = t;
    }

    inline enum layerType getLayerType()
    {
        return mLayerType;
    }

    inline void setPlaneType(enum planeType t)
    {
        mPlaneType = t;
    }

    inline enum planeType getPlaneType()
    {
        return mPlaneType;
    }

    inline void setLayerFormat(int f)
    {
        mFormat = f;
    }

    inline int getLayerFormat()
    {
        return mFormat;
    }

    inline struct sprdRect *getSprdSRCRect()
    {
        return &srcRect;
    }

    inline struct sprdYUV *getSprdSRCYUV()
    {
        return &srcYUV;
    }

    inline struct sprdRect *getSprdFBRect()
    {
        return &FBRect;
    }

    bool checkRGBLayerFormat();
    bool checkYUVLayerFormat();

private:
    hwc_layer_1_t *mAndroidLayer;
    enum layerType mLayerType;
    enum planeType mPlaneType;
    int mFormat;
    struct sprdRect srcRect;
    struct sprdRect FBRect;
    struct sprdYUV  srcYUV;
    bool mDirectDisplayFlag;
    int mDebugFlag;
};



/*
 *  Mainly responsible for traversaling HWLayer list,
 *  find layers that meet SprdDisplayPlane specification
 *  and then mark them as HWC_OVERLAY.
 * */
class SprdHWLayerList
{
public:
    SprdHWLayerList(FrameBufferInfo* fbInfo)
        : mFBInfo(fbInfo),
          mLayerList(0),mLayerCount(0),
          mRGBLayerCount(0), mYUVLayerCount(0),
          mOSDLayerCount(0), mOverlayLayerCount(0),
          mRGBLayerFullScreenFlag(false),
          mList(NULL),
          mDisableHWCFlag(false),
          mSkipLayerFlag(false),
          mDebugFlag(0), mDumpFlag(0)
    {

    }
    ~SprdHWLayerList();

    /*
     *  traversal HWLayer list first,
     *  and change some geometry.
     * */
    bool updateGeometry(hwc_display_contents_1_t *list);

    /*
     *  traversal HWLayer list again,
     *  mainly judge whether upper layer and bottom layer
     *  is consistent with SprdDisplayPlane Hardware requirements.
     * */
    bool revistGeometry(bool *forceOverlayFlag);

    bool checkHWLayerList(hwc_display_contents_1_t* list);

    inline SprdHWLayer *getSprdLayer(unsigned int index)
    {
        return &(mLayerList[index]);
    }

    inline unsigned int getSprdLayerCount()
    {
        return mLayerCount;
    }

    inline enum planeType getPlaneType(unsigned int index)
    {
        return (mLayerList[index].getPlaneType());
    }

private:
    FrameBufferInfo* mFBInfo;
    SprdHWLayer *mLayerList;
    unsigned int mLayerCount;
    unsigned int mRGBLayerCount;
    unsigned int mYUVLayerCount;
    int mOSDLayerCount;
    int mOverlayLayerCount;
    bool mRGBLayerFullScreenFlag;
    hwc_display_contents_1_t *mList;
    bool mDisableHWCFlag;
    bool mSkipLayerFlag;
    int mDebugFlag;
    int mDumpFlag;

    /*
     *  Filter OSD layer
     * */
    bool prepareOSDLayer(SprdHWLayer *l);

    /*
     *  Filter video layer
     * */
    bool prepareOverlayLayer(SprdHWLayer *l);

#ifdef OVERLAY_COMPOSER_GPU
    bool prepareOverlayComposerLayer(SprdHWLayer *l);

    bool revistOverlayComposerLayer(SprdHWLayer *YUVLayer, SprdHWLayer *RGBLayer, int LayerCount, int *FBLayerCount, bool *forceOverlayFlag);
#endif

    bool IsHWCLayer(hwc_layer_1_t *AndroidLayer);

    /*
     * set a HW layer as Overlay flag.
     * */
    void setOverlayFlag(SprdHWLayer *l, unsigned int index);

    /*
     *  reset a HW layer as normal framebuffer flag
     * */
    void resetOverlayFlag(SprdHWLayer *l);

    /*
     *  Force to set a layer to Overlay flag.
     * */
    void forceOverlay(SprdHWLayer *l);

    /*
     *  Clear framebuffer content to black color.
     * */
    void ClearFrameBuffer(hwc_layer_1_t *l, unsigned int index);

    void HWCLayerPreCheck();

    void dump_layer(hwc_layer_1_t const* l);
    void dump_yuv(uint8_t* pBuffer, uint32_t aInBufSize);

    inline int MIN(int x, int y)
    {
        return ((x < y) ? x : y);
    }

    inline int MAX(int x, int y)
    {
        return ((x > y) ? x : y);
    }
};
#endif
