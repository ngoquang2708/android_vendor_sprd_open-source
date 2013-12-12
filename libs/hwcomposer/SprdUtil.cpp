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
 ** File: SprdUtil.cpp                DESCRIPTION                             *
 **                                   Transform or composer Hardware layers   *
 **                                   when display controller cannot deal     *
 **                                   with these function                     *
 ******************************************************************************
 ******************************************************************************
 ** Author:         zhongjun.chen@spreadtrum.com                              *
 *****************************************************************************/


#include <ui/GraphicBufferAllocator.h>
#include "SprdUtil.h"
#include "dump.h"

using namespace android;


#ifdef TRANSFORM_USE_DCAM
OSDTransform::OSDTransform(FrameBufferInfo *fbInfo)
    :  mL(NULL),
       mFBInfo(fbInfo),
       mBuffer(NULL),
       mInitFLag(false),
       mDebugFlag(0)
{
#ifdef _PROC_OSD_WITH_THREAD
    sem_init(&startSem, 0, 0);
    sem_init(&doneSem, 0, 0);
#endif
}

OSDTransform::~OSDTransform()
{
#ifdef _PROC_OSD_WITH_THREAD
    sem_destroy(&startSem);
    sem_destroy(&doneSem);
#endif
}

void OSDTransform::onStart(SprdHWLayer *l, private_handle_t* buffer)
{
    if (l == NULL || buffer == NULL)
    {
        ALOGE("onOSDTransform, input parameters are NULL");
        return;
    }

    mL = l;
    mBuffer = buffer;

#ifndef _PROC_OSD_WITH_THREAD
    transformOSD();
#else
    sem_post(&startSem);
#endif
}

void OSDTransform::onWait()
{
#ifdef _PROC_OSD_WITH_THREAD
    sem_wait(&doneSem);
#endif
}

#ifdef _PROC_OSD_WITH_THREAD
void OSDTransform::onFirstRef()
{
    run("OSDTransform", PRIORITY_URGENT_DISPLAY);
}

status_t OSDTransform::readyToRun()
{
    return NO_ERROR;
}

bool OSDTransform::threadLoop()
{
    sem_wait(&startSem);

    transformOSD();

    sem_post(&doneSem);

    return true;
}
#endif

int OSDTransform::transformOSD()
{
     if (mL == NULL || mBuffer == NULL)
     {
         ALOGE("layer == NULL || mBuffer == NULL");
         return -1;
     }
     hwc_layer_1_t *layer = mL;
     struct sprdYUV *srcImg = mL->getSprdSRCYUV();
     struct sprdRect *srcRect = mL->getSprdSRCRect();
     struct sprdRect *FBRect = mL->getSprdFBRect();
     if (layer == NULL || srcImg == NULL ||
         srcRect == NULL || FBRect == NULL)
     {
         ALOGE("Failed to get OSD SprdHWLayer parameters");
         return -1;
     }

     const native_handle_t *pNativeHandle = layer->handle;
     struct private_handle_t *private_h = (struct private_handle_t *)pNativeHandle;

    queryDebugFlag(&mDebugFlag);

    if (private_h->flags & private_handle_t::PRIV_FLAGS_USES_PHY)
    {
        if (0 == layer->transform)
        {
           ALOGI_IF(mDebugFlag, "OSD display with rot copy");

           int ret = camera_roataion_copy_data(mFBInfo->fb_width, mFBInfo->fb_height, private_h->phyaddr, buffer2->phyaddr);
           if (-1 == ret)
           {
               ALOGE("do OSD rotation copy fail");
           }
        }
        else
        {
               ALOGI_IF(mDebugFlag, "OSD display with rot");
               int degree = -1;

               switch (layer->transform)
               {
                   case HAL_TRANSFORM_ROT_90:
                       degree = 90;
                       break;
                   case HAL_TRANSFORM_ROT_270:
                       degree = 270;
                   default:
                       degree = 180;
                       break;
               }

               int ret = camera_rotation(HW_ROTATION_DATA_RGB888, degree, mFBInfo->fb_width, mFBInfo->fb_height,
                          private_h->phyaddr, buffer2->phyaddr);
               if (-1 == ret)
               {
                   ALOGE("do OSD rotation fail");
               }
       }
    }
    else
    {
        ALOGI_IF(mDebugFlag, "OSD display with dma copy");

        camera_rotation_copy_data_from_virtual(mFBInfo->fb_width, mFBInfo->fb_height, private_h->base, buffer2->phyaddr);
    }

    mL = NULL;
    mBuffer = NULL;

    return 0;
}

#endif


SprdUtil::~SprdUtil()
{
#ifdef TRANSFORM_USE_GPU
    destroy_transform_thread();
#endif
#ifdef TRANSFORM_USE_DCAM
#ifdef SCAL_ROT_TMP_BUF
    GraphicBufferAllocator::get().free((buffer_handle_t)tmpBuffer);
#endif
#endif
#ifdef PROCESS_VIDEO_USE_GSP
    if (mGspDev)
    {
        mGspDev->common.close(&(mGspDev->common));
        mGspDev = NULL;
    }
#endif
}

bool SprdUtil::transformLayer(SprdHWLayer *l1, SprdHWLayer *l2,
                         private_handle_t* buffer1, private_handle_t* buffer2)
{
#ifdef TRANSFORM_USE_DCAM
    if (l2 && buffer2)
    {
        mOSDTransform->onStart(l2, buffer2);
    }

    if (l1 && buffer1)
    {
        /*
         * Temporary video buffer info for dcam transform
         **/
        int format = HAL_PIXEL_FORMAT_YCbCr_420_SP;

#ifdef SCAL_ROT_TMP_BUF
        if (tmpDCAMBuffer == NULL)
        {
            int stride;
            int size;

            GraphicBufferAllocator::get().alloc(mFBInfo->fb_width, mFBInfo->fb_height, format, GRALLOC_USAGE_OVERLAY_BUFFER, (buffer_handle_t*)&tmpDCAMBuffer, &stride);

            MemoryHeapIon::Get_phy_addr_from_ion(tmpDCAMBuffer->share_fd, &(tmpDCAMBuffer->phyaddr), &size);
            if (tmpDCAMBuffer == NULL)
            {
                ALOGE("Cannot alloc the tmpBuffer ION buffer");
                return false;
            }

            Rect bounds(mFBInfo->fb_width, mFBInfo->fb_height);
            GraphicBufferMapper::get().lock((buffer_handle_t)tmpDCAMBuffer, GRALLOC_USAGE_SW_READ_OFTEN, bounds, &tmpDCAMBuffer->base);
        }
#endif

        hwc_layer_1_t *layer = l1->getAndroidLayer();
        struct sprdRect *srcRect = l1->getSprdSRCRect();
        struct sprdRect *FBRect = l1->getSprdFBRect();
        if (layer == NULL || srcImg == NULL ||
            srcRect == NULL || FBRect == NULL)
        {
            ALOGE("Failed to get Video SprdHWLayer parameters");
            return -1;
        }

        const native_handle_t *pNativeHandle = layer->handle;
        struct private_handle_t *private_h = (struct private_handle_t *)pNativeHandle;

        int dstFormat = -1;
#ifdef VIDEO_LAYER_USE_RGB
        dstFormat = HAL_PIXEL_FORMAT_RGBA_8888;
#else
        dstFormat = HAL_PIXEL_FORMAT_YCbCr_420_SP;
#endif
        int ret = transform_layer(private_h->phyaddr, private_h->base, private_h->format,
                      layer->transform, srcImg->w, srcImg->h,
                      buffer1->phyaddr, buffer1->base, dstFormat,
                      FBRect->w, FBRect->h, srcRect,
                      tmpDCAMBuffer->phyaddr, tmpDCAMBuffer->base);
        if (ret != 0)
        {
            ALOGE("DCAM transform video layer failed");
            return false;
        }

    }

    if (l2 && buffer2)
    {
        mOSDTransform->onWait();
    }

#endif

#ifdef TRANSFORM_USE_GPU
    gpu_transform_info_t transformInfo;

    getTransformInfo(l1, l2, buffer1, buffer2, &transformInfo);

    gpu_transform_layers(&transformInfo);
#endif

    return true;
}

#ifdef PROCESS_VIDEO_USE_GSP
int SprdUtil::openGSPDevice()
{
    hw_module_t const* pModule;

    if (hw_get_module(GSP_HARDWARE_MODULE_ID, &pModule) == 0)
    {
        pModule->methods->open(pModule, "gsp", (hw_device_t**)(&mGspDev));
        if (mGspDev == NULL)
        {
            ALOGE("hwcomposer open GSP lib failed! ");
            return -1;
        }
    }
    else
    {
        ALOGE("hwcomposer can't find GSP lib ! ");
        return -1;
    }

    return 0;
}

int SprdUtil::composerLayers(SprdHWLayer *l1, SprdHWLayer *l2, private_handle_t* buffer1, private_handle_t* buffer2)
{
    int32_t ret = 0;
    static int openFlag = 0;
    hwc_layer_1_t *layer1 = NULL;
    hwc_layer_1_t *layer2 = NULL;
    int layer2_Format = -1;
    private_handle_t* buffer = NULL;

    queryDebugFlag(&mDebugFlag);

    if (openFlag == 0)
    {
        int r = openGSPDevice();
        openFlag = 1;
        if (r != 0)
        {
            ALOGE("open GSP device failed");
            openFlag = 0;
        }
    }

    /*
     *  Composer Video layer and OSD layer,
     *  Or transform Video layer or OSD layer
     * */
    if (l1 != NULL)
    {
        buffer = buffer1;
    }
    else if (l1 == NULL && l2 != NULL)
    {
        buffer = buffer2;
    }

    if (buffer == NULL)
    {
        ALOGE("The output buffer1 or buffer2 is NULL in func:%s", __func__);
        return -1;
    }


    GSP_CONFIG_INFO_T gsp_cfg_info;
    uint32_t video_check_result = 0;
    uint32_t osd_check_result = 0;

    memset(&gsp_cfg_info,0,sizeof(gsp_cfg_info));

    if(l1)
    {
        layer1 = l1->getAndroidLayer();
        struct sprdYUV *srcImg1 = l1->getSprdSRCYUV();
        struct sprdRect *srcRect1 = l1->getSprdSRCRect();
        struct sprdRect *FBRect1 = l1->getSprdFBRect();
        if (layer1 == NULL || srcImg1 == NULL ||
            srcRect1 == NULL || FBRect1 == NULL)
        {
            ALOGE("Failed to get Video SprdHWLayer parameters");
            return -1;
        }

        struct private_handle_t *private_h1 = (struct private_handle_t *)(layer1->handle);

        ALOGI_IF(mDebugFlag,"GSP check layer1 L%d,L1 info [x%d,y%d,w%d,h%d,p%d,s%d] r%d [x%d,y%d,w%d,h%d]",__LINE__,
                 srcRect1->x, srcRect1->y,
                 srcRect1->w, srcRect1->h,
                 private_h1->width, private_h1->height,
                 layer1->transform,
                 FBRect1->x, FBRect1->y,
                 FBRect1->w, FBRect1->h);


        if(private_h1 && (private_h1->flags & private_handle_t::PRIV_FLAGS_USES_PHY)) {
            video_check_result = 1;

            //config Video ,use GSP L0
            switch(private_h1->format) {
            case HAL_PIXEL_FORMAT_YCbCr_420_SP:
                gsp_cfg_info.layer0_info.img_format = GSP_SRC_FMT_YUV420_2P;
                //gsp_cfg_info.layer0_info.endian_mode.uv_word_endn = GSP_WORD_ENDN_0;
                gsp_cfg_info.layer0_info.endian_mode.uv_word_endn = GSP_WORD_ENDN_1;
                break;
            case HAL_PIXEL_FORMAT_YCrCb_420_SP:
                gsp_cfg_info.layer0_info.img_format = GSP_SRC_FMT_YUV420_2P;//?
                gsp_cfg_info.layer0_info.endian_mode.uv_word_endn = GSP_WORD_ENDN_0;//?
                break;
            case HAL_PIXEL_FORMAT_YV12:
                gsp_cfg_info.layer0_info.img_format = GSP_SRC_FMT_YUV420_3P;//?
                break;
            default:
                return -1;
            }
            gsp_cfg_info.layer0_info.src_addr.addr_y = private_h1->phyaddr;
            gsp_cfg_info.layer0_info.src_addr.addr_v = gsp_cfg_info.layer0_info.src_addr.addr_uv = private_h1->phyaddr + private_h1->width * private_h1->height;
            //gsp_cfg_info.layer0_info.src_addr.addr_v = gsp_cfg_info.layer0_info.src_addr.addr_uv = private_h0->phyaddr + context->src_img.w*context->src_img.h;
            gsp_cfg_info.layer0_info.clip_rect.st_x = srcRect1->x;
            gsp_cfg_info.layer0_info.clip_rect.st_y = srcRect1->y;
            gsp_cfg_info.layer0_info.clip_rect.rect_w = srcRect1->w;
            gsp_cfg_info.layer0_info.clip_rect.rect_h = srcRect1->h;

            gsp_cfg_info.layer0_info.des_rect.st_x = FBRect1->x;
            gsp_cfg_info.layer0_info.des_rect.st_y = FBRect1->y;
            gsp_cfg_info.layer0_info.des_rect.rect_w = FBRect1->w;
            gsp_cfg_info.layer0_info.des_rect.rect_h = FBRect1->h;
            gsp_cfg_info.layer0_info.alpha = 0xff;

            switch(layer1->transform) {
            case 0:
                gsp_cfg_info.layer0_info.rot_angle = GSP_ROT_ANGLE_0;
                break;
            case HAL_TRANSFORM_FLIP_H:// 1
                gsp_cfg_info.layer0_info.rot_angle = GSP_ROT_ANGLE_180_M;
                break;
            case HAL_TRANSFORM_FLIP_V:// 2
                gsp_cfg_info.layer0_info.rot_angle = GSP_ROT_ANGLE_0_M;
                break;
            case HAL_TRANSFORM_ROT_180:// 3
                gsp_cfg_info.layer0_info.rot_angle = GSP_ROT_ANGLE_180;
                break;
            case HAL_TRANSFORM_ROT_90:// 4
            default:
                gsp_cfg_info.layer0_info.rot_angle = GSP_ROT_ANGLE_270;
                break;
            case (HAL_TRANSFORM_ROT_90|HAL_TRANSFORM_FLIP_H)://5
                gsp_cfg_info.layer0_info.rot_angle = GSP_ROT_ANGLE_90_M;
                break;
            case (HAL_TRANSFORM_ROT_90|HAL_TRANSFORM_FLIP_V)://6
                gsp_cfg_info.layer0_info.rot_angle = GSP_ROT_ANGLE_270_M;
                break;
            case HAL_TRANSFORM_ROT_270:// 7
                gsp_cfg_info.layer0_info.rot_angle = GSP_ROT_ANGLE_90;
                break;
            }
            //gsp_cfg_info.layer0_info.pitch = context->src_img.w;
            gsp_cfg_info.layer0_info.pitch = private_h1->width;
            gsp_cfg_info.layer0_info.layer_en = 1;


            ALOGI_IF(mDebugFlag,"GSP process layer1 L%d,L1 des_w %d des_h %d",__LINE__, FBRect1->w, FBRect1->h);

            ALOGI_IF(mDebugFlag,"GSP process layer1 L%d,L1 [x%d,y%d,w%d,h%d,p%d] r%d [x%d,y%d,w%d,h%d]",__LINE__,
                     gsp_cfg_info.layer0_info.clip_rect.st_x,
                     gsp_cfg_info.layer0_info.clip_rect.st_y,
                     gsp_cfg_info.layer0_info.clip_rect.rect_w,
                     gsp_cfg_info.layer0_info.clip_rect.rect_h,
                     gsp_cfg_info.layer0_info.pitch,
                     gsp_cfg_info.layer0_info.rot_angle,
                     gsp_cfg_info.layer0_info.des_rect.st_x,
                     gsp_cfg_info.layer0_info.des_rect.st_y,
                     gsp_cfg_info.layer0_info.des_rect.rect_w,
                     gsp_cfg_info.layer0_info.des_rect.rect_h);
        }
        else
        {
            ALOGE("GSP process layer1 L%d,video layer use virtual addr!",__LINE__);
        }

    } else {
        ALOGI_IF(mDebugFlag,"GSP find layer1 do not exists. L%d,L1 == NULL ",__LINE__);
    }

    if(l2)
    {
        layer2 = l2->getAndroidLayer();
        struct sprdYUV *srcImg2 = l2->getSprdSRCYUV();
        struct sprdRect *srcRect2 = l2->getSprdSRCRect();
        struct sprdRect *FBRect2 = l2->getSprdFBRect();
        if (layer2 == NULL || srcImg2 == NULL ||
            srcRect2 == NULL || FBRect2 == NULL)
        {
            ALOGE("Failed to get OSD SprdHWLayer parameters");
            return -1;
        }

        struct private_handle_t *private_h2 = (struct private_handle_t *)(layer2->handle);

        ALOGI_IF(mDebugFlag,"GSP check layer2 L%d,L2 info [x%d,y%d,w%d,h%d] r%d [x%d,y%d,w%d,h%d]",__LINE__,
                 0, 0,
                 private_h2->width, private_h2->height,
                 layer2->transform,
                 0, 0,
                 mFBInfo->fb_width, mFBInfo->fb_height);

        layer2_Format = private_h2->format;

        if(private_h2->flags && (private_h2->flags & private_handle_t::PRIV_FLAGS_USES_PHY))
        {
            osd_check_result = 1;
            //config OSD,use GSP L1
            if (layer2_Format == HAL_PIXEL_FORMAT_RGBA_8888 ||
                layer2_Format == HAL_PIXEL_FORMAT_RGBX_8888)
            {
                gsp_cfg_info.layer1_info.img_format = GSP_SRC_FMT_ARGB888;
            }
            else if (layer2_Format == HAL_PIXEL_FORMAT_RGB_565)
            {
                gsp_cfg_info.layer1_info.img_format = GSP_SRC_FMT_RGB565;
            }
            gsp_cfg_info.layer1_info.src_addr.addr_v =
                gsp_cfg_info.layer1_info.src_addr.addr_uv =
                    gsp_cfg_info.layer1_info.src_addr.addr_y = private_h2->phyaddr;

            gsp_cfg_info.layer1_info.clip_rect.rect_w = private_h2->width;
            gsp_cfg_info.layer1_info.clip_rect.rect_h = private_h2->height;
            gsp_cfg_info.layer1_info.alpha = 0xff;

            if (0 != layer2->transform) {
                switch(layer2->transform) {
                case HAL_TRANSFORM_ROT_90:
                    gsp_cfg_info.layer1_info.rot_angle = GSP_ROT_ANGLE_270;
                    break;
                case HAL_TRANSFORM_ROT_270:
                    gsp_cfg_info.layer1_info.rot_angle = GSP_ROT_ANGLE_90;
                    break;
                default:// 180
                    gsp_cfg_info.layer1_info.rot_angle = GSP_ROT_ANGLE_180;
                    break;
                }
            }
            gsp_cfg_info.layer1_info.pitch = private_h2->width;
            gsp_cfg_info.layer1_info.des_pos.pos_pt_x = gsp_cfg_info.layer1_info.des_pos.pos_pt_y = 0;
            gsp_cfg_info.layer1_info.layer_en = 1;

            ALOGI_IF(mDebugFlag,"GSP process layer2 L%d,L2 [x%d,y%d,w%d,h%d,p%d] r%d [x%d,y%d]",__LINE__,
                     gsp_cfg_info.layer1_info.clip_rect.st_x,
                     gsp_cfg_info.layer1_info.clip_rect.st_y,
                     gsp_cfg_info.layer1_info.clip_rect.rect_w,
                     gsp_cfg_info.layer1_info.clip_rect.rect_h,
                     gsp_cfg_info.layer1_info.pitch,
                     gsp_cfg_info.layer1_info.rot_angle,
                     gsp_cfg_info.layer1_info.des_pos.pos_pt_x,
                     gsp_cfg_info.layer1_info.des_pos.pos_pt_y);

        }
        else
        {
            ALOGE("GSP process layer2 L%d,osd layer use virtual addr!",__LINE__);
        }

    } else {
        ALOGI_IF(mDebugFlag,"GSP process layer2 L%d,L2 == NULL, use pallet to clean the area L1 not covered. ",__LINE__);
        osd_check_result = 1;

        gsp_cfg_info.layer1_info.grey.r_val = 0;
        gsp_cfg_info.layer1_info.grey.g_val = 0;
        gsp_cfg_info.layer1_info.grey.b_val = 0;
        gsp_cfg_info.layer1_info.clip_rect.st_x = 0;
        gsp_cfg_info.layer1_info.clip_rect.st_y = 0;

        gsp_cfg_info.layer1_info.clip_rect.rect_w = mFBInfo->fb_width;
        gsp_cfg_info.layer1_info.clip_rect.rect_h = mFBInfo->fb_height;
        gsp_cfg_info.layer1_info.pitch = mFBInfo->fb_width;

        //the 3-plane addr should not be used by GSP
        gsp_cfg_info.layer1_info.src_addr.addr_y = gsp_cfg_info.layer0_info.src_addr.addr_y;
        gsp_cfg_info.layer1_info.src_addr.addr_uv = gsp_cfg_info.layer0_info.src_addr.addr_uv;
        gsp_cfg_info.layer1_info.src_addr.addr_v = gsp_cfg_info.layer0_info.src_addr.addr_v;

        gsp_cfg_info.layer1_info.pallet_en = 1;
        gsp_cfg_info.layer1_info.alpha = 0x1;

        gsp_cfg_info.layer1_info.rot_angle = GSP_ROT_ANGLE_0;
        gsp_cfg_info.layer1_info.des_pos.pos_pt_x = 0;
        gsp_cfg_info.layer1_info.des_pos.pos_pt_y = 0;
        gsp_cfg_info.layer1_info.layer_en = 1;
    }


    if (video_check_result || osd_check_result) {
        uint32_t current_overlay_paddr = 0;
        uint32_t current_overlay_vaddr = 0;

        //config output
        current_overlay_vaddr = (unsigned int)buffer->base;
        current_overlay_paddr = (uint32_t)buffer->phyaddr;
        gsp_cfg_info.layer_des_info.src_addr.addr_y = current_overlay_paddr;
        gsp_cfg_info.layer_des_info.src_addr.addr_v =
            gsp_cfg_info.layer_des_info.src_addr.addr_uv = current_overlay_paddr + mFBInfo->fb_width * mFBInfo->fb_height;
        gsp_cfg_info.layer_des_info.pitch = mFBInfo->fb_width;


        if (l1 != NULL)
        {
#ifdef VIDEO_LAYER_USE_RGB
            //videoOverlayFormat = HAL_PIXEL_FORMAT_RGBX_8888;
            gsp_cfg_info.layer_des_info.img_format = GSP_DST_FMT_ARGB888;
            gsp_cfg_info.layer_des_info.endian_mode.a_swap_mode = GSP_A_SWAP_RGBA;
            gsp_cfg_info.layer_des_info.endian_mode.y_word_endn = GSP_WORD_ENDN_1;
#else
#ifdef GSP_OUTPUT_USE_YUV420
            gsp_cfg_info.layer_des_info.img_format = GSP_DST_FMT_YUV420_2P;
#else
            gsp_cfg_info.layer_des_info.img_format = GSP_DST_FMT_YUV422_2P;
#endif
#endif
        }
        else if (l2 != NULL)
        {
            if (layer2_Format == HAL_PIXEL_FORMAT_RGBA_8888 ||
                layer2_Format == HAL_PIXEL_FORMAT_RGBX_8888)
            {
                gsp_cfg_info.layer_des_info.img_format = GSP_DST_FMT_ARGB888;
                //gsp_cfg_info.layer_des_info.endian_mode.a_swap_mode = GSP_A_SWAP_RGBA;
                //gsp_cfg_info.layer_des_info.endian_mode.y_word_endn = GSP_WORD_ENDN_1;
            }
            else if (layer2_Format == HAL_PIXEL_FORMAT_RGB_565)
            {
                gsp_cfg_info.layer_des_info.img_format = GSP_DST_FMT_RGB565;
            }
        }

        //in sc8830 first GSP version hw, GSP don't support odd width/height and x/y
        gsp_cfg_info.layer0_info.clip_rect.st_x &= 0xfffe;
        gsp_cfg_info.layer0_info.clip_rect.st_y &= 0xfffe;
        gsp_cfg_info.layer0_info.clip_rect.rect_w &= 0xfffe;
        gsp_cfg_info.layer0_info.clip_rect.rect_h &= 0xfffe;
        gsp_cfg_info.layer0_info.des_rect.st_x &= 0xfffe;
        gsp_cfg_info.layer0_info.des_rect.st_y &= 0xfffe;
        gsp_cfg_info.layer0_info.des_rect.rect_w &= 0xfffe;
        gsp_cfg_info.layer0_info.des_rect.rect_h &= 0xfffe;
        gsp_cfg_info.layer1_info.clip_rect.st_x &= 0xfffe;
        gsp_cfg_info.layer1_info.clip_rect.st_y &= 0xfffe;
        gsp_cfg_info.layer1_info.clip_rect.rect_w &= 0xfffe;
        gsp_cfg_info.layer1_info.clip_rect.rect_h &= 0xfffe;
        gsp_cfg_info.layer1_info.des_pos.pos_pt_x &= 0xfffe;
        gsp_cfg_info.layer1_info.des_pos.pos_pt_y &= 0xfffe;



#ifdef GSP_SCALING_UP_TWICE

        if((((gsp_cfg_info.layer0_info.rot_angle & 0x1) == 0) &&
                (((gsp_cfg_info.layer0_info.clip_rect.rect_w * 4) < gsp_cfg_info.layer0_info.des_rect.rect_w)
                 ||((gsp_cfg_info.layer0_info.clip_rect.rect_h * 4) < gsp_cfg_info.layer0_info.des_rect.rect_h)))
                ||(((gsp_cfg_info.layer0_info.rot_angle & 0x1) == 1) &&
                   (((gsp_cfg_info.layer0_info.clip_rect.rect_w * 4) < gsp_cfg_info.layer0_info.des_rect.rect_h)
                    || ((gsp_cfg_info.layer0_info.clip_rect.rect_h * 4) < gsp_cfg_info.layer0_info.des_rect.rect_w))))
        {
            GSP_CONFIG_INFO_T gsp_cfg_info_phase1 = gsp_cfg_info;
            GSP_LAYER_DST_DATA_FMT_E phase1_des_format = GSP_DST_FMT_YUV420_2P;//GSP_DST_FMT_YUV422_2P; //GSP_DST_FMT_ARGB888

            if (tmpBuffer == NULL)
            {
                int format = -1;
                format = HAL_PIXEL_FORMAT_RGBA_8888; //PRIVATE_HALF_BUFFER;
                int stride;

                GraphicBufferAllocator::get().alloc(mFBInfo->fb_width, mFBInfo->fb_height, format, GRALLOC_USAGE_OVERLAY_BUFFER, (buffer_handle_t*)&tmpBuffer, &stride);
                if (tmpBuffer == NULL)
                {
                    ALOGE("Cannot alloc the tmpBuffer ION buffer");
                    return -1;
                }
            }

            /*phase1*/
            gsp_cfg_info_phase1.layer_des_info.img_format = phase1_des_format;
            gsp_cfg_info_phase1.layer_des_info.src_addr.addr_y = (uint32_t)tmpBuffer->phyaddr;
            gsp_cfg_info_phase1.layer_des_info.src_addr.addr_v =
                gsp_cfg_info_phase1.layer_des_info.src_addr.addr_uv = gsp_cfg_info_phase1.layer_des_info.src_addr.addr_y + mFBInfo->fb_width * mFBInfo->fb_height;

            gsp_cfg_info_phase1.layer0_info.des_rect.st_x = 0;
            gsp_cfg_info_phase1.layer0_info.des_rect.st_y = 0;
            gsp_cfg_info_phase1.layer0_info.des_rect.rect_w = gsp_cfg_info_phase1.layer0_info.clip_rect.rect_w;
            gsp_cfg_info_phase1.layer0_info.des_rect.rect_h = gsp_cfg_info_phase1.layer0_info.clip_rect.rect_h;
            if((gsp_cfg_info.layer0_info.rot_angle & 0x1) == 0) {
                if((gsp_cfg_info_phase1.layer0_info.clip_rect.rect_w * 4) < gsp_cfg_info.layer0_info.des_rect.rect_w) {
                    gsp_cfg_info_phase1.layer0_info.des_rect.rect_w = ((gsp_cfg_info.layer0_info.des_rect.rect_w + 7)/4 & 0xfffe);
                }
                if((gsp_cfg_info_phase1.layer0_info.clip_rect.rect_h * 4) < gsp_cfg_info.layer0_info.des_rect.rect_h) {
                    gsp_cfg_info_phase1.layer0_info.des_rect.rect_h = ((gsp_cfg_info.layer0_info.des_rect.rect_h + 7)/4 & 0xfffe);
                }
            } else {
                if((gsp_cfg_info_phase1.layer0_info.clip_rect.rect_w * 4) < gsp_cfg_info.layer0_info.des_rect.rect_h) {
                    gsp_cfg_info_phase1.layer0_info.des_rect.rect_w = ((gsp_cfg_info.layer0_info.des_rect.rect_h + 7)/4 & 0xfffe);
                }
                if((gsp_cfg_info_phase1.layer0_info.clip_rect.rect_h * 4) < gsp_cfg_info.layer0_info.des_rect.rect_w) {
                    gsp_cfg_info_phase1.layer0_info.des_rect.rect_h = ((gsp_cfg_info.layer0_info.des_rect.rect_w + 7)/4 & 0xfffe);
                }
            }
            gsp_cfg_info_phase1.layer_des_info.pitch = gsp_cfg_info_phase1.layer0_info.des_rect.rect_w;
            gsp_cfg_info_phase1.layer0_info.rot_angle = GSP_ROT_ANGLE_0;
            gsp_cfg_info_phase1.layer1_info.layer_en = 0;//disable Layer1

            ALOGI_IF(mDebugFlag,"scaling twice phase 1,set_GSP_layers L%d,src_addr_y:0x%08x,des_addr_y:0x%08x",__LINE__,
                     gsp_cfg_info_phase1.layer0_info.src_addr.addr_y,
                     gsp_cfg_info_phase1.layer_des_info.src_addr.addr_y);

            ret = mGspDev->GSP_Proccess(&gsp_cfg_info_phase1);
            if(0 == ret) {
                ALOGI_IF(mDebugFlag,"scaling twice phase 1,set_GSP_layers L%d,GSP_Proccess ret 0",__LINE__);
            } else {
                ALOGE("scaling twice phase 1,set_GSP_layers L%d,GSP_Proccess ret err!! debugenable = 1;",__LINE__);
                return ret;
            }

            /*phase2*/
            gsp_cfg_info.layer0_info.img_format = (GSP_LAYER_SRC_DATA_FMT_E)phase1_des_format;
            gsp_cfg_info.layer0_info.clip_rect = gsp_cfg_info_phase1.layer0_info.des_rect;
            gsp_cfg_info.layer0_info.pitch = gsp_cfg_info_phase1.layer_des_info.pitch;
            gsp_cfg_info.layer0_info.src_addr = gsp_cfg_info_phase1.layer_des_info.src_addr;
            gsp_cfg_info.layer0_info.endian_mode = gsp_cfg_info_phase1.layer_des_info.endian_mode;

            ALOGI_IF(mDebugFlag,"scaling twice phase 2,set_GSP_layers L%d,src_addr_y:0x%08x,des_addr_y:0x%08x",__LINE__,
                     gsp_cfg_info.layer0_info.src_addr.addr_y,
                     gsp_cfg_info.layer_des_info.src_addr.addr_y);
            ret = mGspDev->GSP_Proccess(&gsp_cfg_info);
            if(0 == ret) {
                ALOGI_IF(mDebugFlag,"scaling twice phase 2,set_GSP_layers L%d,GSP_Proccess ret 0",__LINE__);
            } else {
                ALOGE("scaling twice phase 2,set_GSP_layers L%d,GSP_Proccess ret err!! debugenable = 1;",__LINE__);
            }
        } else {
            ALOGI_IF(mDebugFlag,"GSP process layers L%d,Ld [p%d], the output buffer phyAddr:%p, virAddr:%p",__LINE__,gsp_cfg_info.layer_des_info.pitch, (void *)buffer->phyaddr, (void *)buffer->base);
             ret = mGspDev->GSP_Proccess(&gsp_cfg_info);
         }
#else
        ALOGI_IF(mDebugFlag,"GSP process layers L%d,Ld [p%d], the output buffer phyAddr:%p, virAddr:%p",__LINE__,gsp_cfg_info.layer_des_info.pitch, (void *)buffer->phyaddr, (void *)buffer->base);
        if(mGspDev) {
            ret = mGspDev->GSP_Proccess(&gsp_cfg_info);
        }
#endif
        //ret = GSP_Proccess(&gsp_cfg_info);
    }

    return 0;
}
#endif

#ifdef TRANSFORM_USE_GPU
int SprdUtil::getTransformInfo(SprdHWLayer *l1, SprdHWLayer *l2,
                           private_handle_t* buffer1, private_handle_t* buffer2,
                           gpu_transform_info_t *transformInfo)
{
    memset(transformInfo , 0 , sizeof(gpu_transform_info_t));

    /*
     * Init parameters for Video transform
     * */
    if(l1 && buffer1)
    {
        hwc_layer_1_t *layer = l1->getAndroidLayer();
        struct sprdYUV *srcImg = l1->getSprdSRCYUV();
        struct sprdRect *srcRect = l1->getSprdSRCRect();
        struct sprdRect *FBRect = l1->getSprdFBRect();
        if (layer == NULL || srcImg == NULL ||
            srcRect == NULL || FBRect == NULL)
        {
            ALOGE("Failed to get Video SprdHWLayer parameters");
            return -1;
        }

        const native_handle_t *pNativeHandle = layer->handle;
        struct private_handle_t *private_h = (struct private_handle_t *)pNativeHandle;

        transformInfo->flag |= VIDEO_LAYER_EXIST;
        transformInfo->video.srcPhy = private_h->phyaddr;
        transformInfo->video.srcVirt =  private_h->base;
        transformInfo->video.srcFormat = private_h->format;
        transformInfo->video.transform = layer->transform;
        transformInfo->video.srcWidth = srcImg->w;
        transformInfo->video.srcHeight = srcImg->h;
        transformInfo->video.dstPhy = buffer1->phyaddr;
        transformInfo->video.dstVirt = (uint32_t)buffer1->base;
        transformInfo->video.dstFormat = HAL_PIXEL_FORMAT_RGBX_8888;
        transformInfo->video.dstWidth = FBRect->w;
        transformInfo->video.dstHeight = FBRect->h;

        transformInfo->video.tmp_phy_addr = 0;
        transformInfo->video.tmp_vir_addr = 0;
        transformInfo->video.trim_rect.x  = srcRect->x;
        transformInfo->video.trim_rect.y  = srcRect->y;
        transformInfo->video.trim_rect.w  = srcRect->w;
        transformInfo->video.trim_rect.h  = srcRect->h;
    }

    /*
     * Init parameters for OSD transform
     * */
    if(l2 && buffer2)
    {
        hwc_layer_1_t *layer = l2->getAndroidLayer();
        struct sprdYUV *srcImg = l2->getSprdSRCYUV();
        struct sprdRect *srcRect = l2->getSprdSRCRect();
        struct sprdRect *FBRect = l2->getSprdFBRect();
        if (layer == NULL || srcImg == NULL ||
            srcRect == NULL || FBRect == NULL)
        {
            ALOGE("Failed to get OSD SprdHWLayer parameters");
            return -1;
        }

        const native_handle_t *pNativeHandle = layer->handle;
        struct private_handle_t *private_h = (struct private_handle_t *)pNativeHandle;

        transformInfo->flag |= OSD_LAYER_EXIST;
        transformInfo->osd.srcPhy = private_h->phyaddr;
        transformInfo->osd.srcVirt = private_h->base;
        transformInfo->osd.srcFormat = HAL_PIXEL_FORMAT_RGBA_8888;
        transformInfo->osd.transform = layer->transform;
        transformInfo->osd.srcWidth = private_h->width;
        transformInfo->osd.srcHeight = private_h->height;
        transformInfo->osd.dstPhy = buffer2->phyaddr;
        transformInfo->osd.dstVirt = (uint32_t)buffer2->base;
        transformInfo->osd.dstFormat = HAL_PIXEL_FORMAT_RGBA_8888;
        transformInfo->osd.dstWidth = FBRect->w;
        transformInfo->osd.dstHeight = FBRect->h;
        transformInfo->osd.tmp_phy_addr = 0;
        transformInfo->osd.tmp_vir_addr = 0;
        transformInfo->osd.trim_rect.x  = 0;
        transformInfo->osd.trim_rect.y  = 0;
        transformInfo->osd.trim_rect.w  = private_h->width; // osd overlay must be full screen
        transformInfo->osd.trim_rect.h  = private_h->height; // osd overlay must be full screen
    }

    return 0;
}
#endif
