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
 ** 16/06/2014    Hardware Composer   Responsible for processing some         *
 **                                   Hardware layers. These layers comply    *
 **                                   with Virtual Display specification,     *
 **                                   can be displayed directly, bypass       *
 **                                   SurfaceFligner composition. It will     *
 **                                   improve system performance.             *
 ******************************************************************************
 ** File:SprdWIDIBlit.cpp             DESCRIPTION                             *
 **                                   WIDIBLIT: Wireless Display Blit         *
 **                                   Responsible for blit image data to      *
 **                                   Virtual Display.                        *
 ******************************************************************************
 ******************************************************************************
 ** Author:         zhongjun.chen@spreadtrum.com                              *
 *****************************************************************************/

#include <binder/MemoryHeapIon.h>
#include "SprdWIDIBlit.h"
#include "../SprdHWLayer.h"
#include "../SprdPrimaryDisplayDevice/SprdFrameBufferHAL.h"
#include "../SprdTrace.h"

namespace android
{


SprdWIDIBlit:: SprdWIDIBlit(SprdVirtualPlane *plane)
    : mDisplayPlane(plane),
      mAccelerator(0),
      mFBInfo(0),
      mDebugFlag(0)
{

}

SprdWIDIBlit:: ~SprdWIDIBlit()
{
    if (mFBInfo)
    {
        free(mFBInfo);
        mFBInfo = NULL;
    }
}

void SprdWIDIBlit:: onStart()
{
    sem_post(&startSem);
}

void SprdWIDIBlit:: onDisplay()
{
    HWC_TRACE_CALL;
    sem_wait(&doneSem);
}

int queryBlit()
{

    return 0;
}


/*
 *  Private interface
 * */
void SprdWIDIBlit:: onFirstRef()
{
    run("SprdWIDIBlit", PRIORITY_URGENT_DISPLAY + PRIORITY_MORE_FAVORABLE);
}

status_t SprdWIDIBlit:: readyToRun()
{
    sem_init(&startSem, 0, 0);
    sem_init(&doneSem, 0, 0);

    if (mDisplayPlane == NULL)
    {
        ALOGE("SprdWIDIBlit:: readyToRun mDisplayPlane is NULL");
        return -1;
    }

    mFBInfo = (FrameBufferInfo *)malloc(sizeof(FrameBufferInfo));
    if (mFBInfo == NULL)
    {
        ALOGE("malloc FrameBufferInfo failed");
        return -1;
    }
    mAccelerator = new SprdUtil(mFBInfo);
    if (mAccelerator == NULL)
    {
        free(mFBInfo);
        ALOGE("new SprdUtil failed");
        return -1;
    }
    mAccelerator->getGSPCapability(NULL);
    mAccelerator->forceUpdateAddrType(GSP_ADDR_TYPE_PHYSICAL);

    return NO_ERROR;
}

bool SprdWIDIBlit:: threadLoop()
{
    sem_wait(&startSem);

    int ret = -1;
    bool SourcePhyAddrType = -1;
    bool DestPhyAddrType = -1;
    SprdHWLayer *SprdHWSourceLayer = NULL;
    private_handle_t *DisplayHandle = NULL;
    struct sprdRect *SourceRect = NULL;
    unsigned int width = 0;
    unsigned int height = 0;
    int format = -1;
    int size = -1;
    int size2 = 1;


    HWC_TRACE_BEGIN_WIDIBLIT;

    DisplayHandle = mDisplayPlane->dequeueBuffer();
    if (DisplayHandle == NULL)
    {
        ALOGE("SprdWIDIBlit:: threadLoop DisplayHanle is NULL");
        return true;
    }


    mDisplayPlane->getPlaneGeometry(&width, &height, &format);
    if (mFBInfo == NULL)
    {
        ALOGE("SprdWIDIBlit:: threadLoop mFBInfo is NULL");
        return true;
    }

    mFBInfo->fb_width = width;
    mFBInfo->fb_height = height;
    mFBInfo->format = format;

    mAccelerator->UpdateFBInfo(mFBInfo);

    SprdHWSourceLayer = mDisplayPlane->getSprdHWSourceLayer();
    if (SprdHWSourceLayer == NULL)
    {
        ALOGE("SprdWIDIBlit:: threadLoop SprdHWSourceLayer is NULL");
        return true;
    }

    hwc_layer_1_t *AndroidLayer = SprdHWSourceLayer->getAndroidLayer();
    if (AndroidLayer == NULL)
    {
        ALOGE("SprdWIDIBlit:: threadLoop FBTLayer is NULL");
        return true;
    }
    struct private_handle_t *privateH = (struct private_handle_t *)AndroidLayer->handle;
    if (privateH == NULL)
    {
        ALOGE("SprdWIDIBlit:: threadLoop private handle is NULL");
        return true;
    }

    SourcePhyAddrType = (privateH->flags & private_handle_t::PRIV_FLAGS_USES_PHY);

    DestPhyAddrType = (DisplayHandle->flags & private_handle_t::PRIV_FLAGS_USES_PHY);

    ALOGI_IF(mDebugFlag, "SprdWIDIBlit:: threadLoop source handle addr: %p, flag: 0x%x, dest handle addr: %p, flag: 0x%x", (void *)privateH, privateH->flags, (void *)DisplayHandle, DisplayHandle->flags);
    if (SourcePhyAddrType && DestPhyAddrType)
    {
        MemoryHeapIon::Get_phy_addr_from_ion(DisplayHandle->share_fd, &(DisplayHandle->phyaddr), &size);
        MemoryHeapIon::Get_phy_addr_from_ion(privateH->share_fd, &(privateH->phyaddr), &size2);

        mAccelerator->UpdateOutputFormat(GSP_DST_FMT_YUV420_2P);

        if (SprdHWSourceLayer->checkYUVLayerFormat())
        {
            ret = mAccelerator->composerLayers(SprdHWSourceLayer, NULL, NULL, DisplayHandle, GSP_DST_FMT_MAX_NUM);
        }
        else if (SprdHWSourceLayer->checkRGBLayerFormat())
        {
            ret = mAccelerator->composerLayers(NULL, SprdHWSourceLayer, NULL, DisplayHandle, GSP_DST_FMT_MAX_NUM);
        }
    }
    else
    {
        ALOGI_IF(mDebugFlag, "SprdWIDIBlit:: threadLoop Source(SourcePhyAddrType: %d) or Dest(DestPhyAddrType: %d) do not use ION_PhyAddr, will Use NEON to Blit", SourcePhyAddrType, DestPhyAddrType);
        if ((void *)(privateH->base) == NULL || (void *)(DisplayHandle->base) == NULL)
        {
            ALOGE("SprdWIDIBlit:: threadLoop Source virtual address: %p or Dest virtual addr: %p is NULL",
                  (void *)(privateH->base), (void *)(DisplayHandle->base));
            return true;
        }

        /*
         *  Blit with NEON
         * */
        /*
         *  Source Information
         * */
        uint8_t *inrgb = (uint8_t *)(privateH->base);
        int32_t width_org = privateH->width;
        int32_t height_org = privateH->height;

        /*
         *  Destination Information
         * */
        uint8_t *outy = (uint8_t *)(DisplayHandle->base);
        uint8_t *outuv = (uint8_t *)(DisplayHandle->base + DisplayHandle->stride * DisplayHandle->height);
        int32_t width_dst = DisplayHandle->width;
        int32_t height_dst = DisplayHandle->height;

        ret = NEONBlit(inrgb, outy, outuv, width_org, height_org, width_dst, height_dst);
    }

    if (ret != 0)
    {
        ALOGE("SprdWIDIBlit:: threadLoop Accelerator composerLayers failed");
        //return true;
    }

    mDisplayPlane->queueBuffer();

    queryDebugFlag(&mDebugFlag);
    SourceRect = SprdHWSourceLayer->getSprdSRCRect();
    ALOGI_IF(mDebugFlag, "SprdWIDIBlit Source Layer width: %d, height: %d, format: %d, Destination width: %d, heigh: %d, format: %d",
             privateH->width, privateH->height, privateH->format,
             DisplayHandle->width, DisplayHandle->height, DisplayHandle->format);

    sem_post(&doneSem);

    HWC_TRACE_END;

    return true;
}

int SprdWIDIBlit:: NEONBlit(uint8_t *inrgb, uint8_t *outy, uint8_t *outuv, int32_t width_org, int32_t height_org, int32_t width_dst, int32_t height_dst)
{
    HWC_TRACE_CALL;
    if (inrgb == NULL || outy == NULL || outuv == NULL)
    {
        ALOGE("SprdWIDIBlit:: NEONBlit input is NULL");
        return -1;
    }
    uint32_t i, j;
    uint8_t *argb_ptr = inrgb;
    uint8_t *y_ptr = outy;
    uint8_t *temp_y_ptr = y_ptr;
    uint8_t *uv_ptr = outuv;
    uint8_t *argb_tmpptr;
    uint8x8_t r1fac = vdup_n_u8(66);

    uint8x8_t g1fac = vdup_n_u8(129);
    ///////// uint8x8_t g11fac = vdup_n_u8(1);   ///////128+1 =129

    uint8x8_t b1fac = vdup_n_u8(25);
    uint8x8_t r2fac = vdup_n_u8(38);
    uint8x8_t g2fac = vdup_n_u8(74);
    uint8x8_t b2fac = vdup_n_u8(112);
    // int8x8_t r3fac = vdup_n_s16(112);
    uint8x8_t g3fac = vdup_n_u8(94);
    uint8x8_t b3fac = vdup_n_u8(18);

    uint8x8_t y_base = vdup_n_u8(16);
    uint8x8_t uv_base = vdup_n_u8(128);


    for (i=height_org; i>0; i-=2)    /////  line
    {
       for (j=(width_org>>3); j>0; j-=2)   ///// col
       {
           uint8_t y, cb, cr;
           int8_t r, g, b;
           uint8_t p_r[16],p_g[16],p_b[16];
           uint16x8_t temp;
           uint8x8_t result;
           uint8x8_t result_cr;
           uint8x8x2_t result_uv;


           // y = RGB2Y(r, g, b);
           uint8x8x4_t argb = vld4_u8(argb_ptr);
           temp = vmull_u8(argb.val[0],r1fac);    ///////////////////////y  0,1,2
           temp = vmlal_u8(temp,argb.val[1],g1fac);
           temp = vmlal_u8(temp,argb.val[2],b1fac);
           result = vshrn_n_u16(temp,8);
           result = vadd_u8(result,y_base);
           vst1_u8(y_ptr,result);     ////*y_ptr = y;


           argb_tmpptr= argb_ptr + 32;
           temp_y_ptr = y_ptr + 8;
           uint8x8x4_t argb1 = vld4_u8(argb_tmpptr);
           // y = RGB2Y(r, g, b);
           temp = vmull_u8(argb1.val[0],r1fac);    ///////////////////////y
           temp = vmlal_u8(temp,argb1.val[1],g1fac);
           temp = vmlal_u8(temp,argb1.val[2],b1fac);
           result = vshrn_n_u16(temp,8);
           result = vadd_u8(result,y_base);
           vst1_u8(temp_y_ptr,result);     ////*y_ptr = y;

           vst1_u8(p_r,argb.val[0]);
           vst1_u8(p_r+8,argb1.val[0]);
           vst1_u8(p_g,argb.val[1]);
           vst1_u8(p_g+8,argb1.val[1]);
           vst1_u8(p_b,argb.val[2]);
           vst1_u8(p_b+8,argb1.val[2]);
           uint8x8x2_t rgb_r = vld2_u8(p_r);
           uint8x8x2_t rgb_g = vld2_u8(p_g);
           uint8x8x2_t rgb_b = vld2_u8(p_b);

           //cb = RGB2CR(r, g, b);
           temp = vmull_u8(rgb_b.val[0],b2fac);    ///////////////////////cb
           temp = vmlsl_u8(temp,rgb_g.val[0],g2fac);
           temp = vmlsl_u8(temp,rgb_r.val[0],r2fac);
           result = vshrn_n_u16(temp,8);
           result = vadd_u8(result,uv_base);

           //cr = RGB2CB(r, g, b);
           temp = vmull_u8(rgb_r.val[0],b2fac);    ///////////////////////cr
           temp = vmlsl_u8(temp,rgb_g.val[0],g3fac);
           temp = vmlsl_u8(temp,rgb_b.val[0],b3fac);
           result_cr = vshrn_n_u16(temp,8);
           result_cr = vadd_u8(result_cr,uv_base);

           result_uv = vzip_u8(result_cr,result);  /////uuuuuuuuvvvvvvvv -->> uvuvuvuvuvuvuvuvuv
           vst1_u8(uv_ptr,result_uv.val[0]);
           uv_ptr += 8;
           vst1_u8(uv_ptr,result_uv.val[1]);
           uv_ptr += 8;

           argb_tmpptr= argb_ptr + (width_org<<2);
           temp_y_ptr = y_ptr + width_dst;
           argb = vld4_u8(argb_tmpptr);

           // y = RGB2Y(r, g, b);
           temp = vmull_u8(argb.val[0],r1fac);    ///////////////////////y
           temp = vmlal_u8(temp,argb.val[1],g1fac);
           temp = vmlal_u8(temp,argb.val[2],b1fac);
           result = vshrn_n_u16(temp,8);
           result = vadd_u8(result,y_base);
           vst1_u8(temp_y_ptr,result);   ////*y_ptr = y;


           argb_tmpptr =argb_ptr +( width_org<<2)+32;
           temp_y_ptr = y_ptr + width_dst + 8;
           argb = vld4_u8(argb_tmpptr);

           // y = RGB2Y(r, g, b);
           temp = vmull_u8(argb.val[0],r1fac);    ///////////////////////y
           temp = vmlal_u8(temp,argb.val[1],g1fac);
           temp = vmlal_u8(temp,argb.val[2],b1fac);
           result = vshrn_n_u16(temp,8);
           result = vadd_u8(result,y_base);
           vst1_u8(temp_y_ptr,result);     ////*y_ptr = y;

           y_ptr += 16;
           argb_ptr += 64;
       }

       y_ptr += width_dst;
       argb_ptr += width_org<<2;
    }

    return 0;
}


}
