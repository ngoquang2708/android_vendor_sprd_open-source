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

//#include <cutils/log.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/cdefs.h>
#include "gsp_hal.hpp"
#include "ion_sprd.h"
//#include "config.h"



static int32_t gsp_hal_layer0_params_check (GSP_LAYER0_CONFIG_INFO_T *layer0_info)
{
    float coef_factor_w = 0.0;
    float coef_factor_h = 0.0;
    //uint32_t pixel_cnt = 0x1000000;//max 16M pixel

    if(layer0_info->layer_en == 0)
    {
        return GSP_NO_ERR;
    }

    if(layer0_info->clip_rect.st_x & 0x1
       ||layer0_info->clip_rect.st_y & 0x1
       ||layer0_info->clip_rect.rect_w & 0x1
       ||layer0_info->clip_rect.rect_h & 0x1
       ||layer0_info->des_rect.st_x & 0x1
       ||layer0_info->des_rect.st_y & 0x1
       ||layer0_info->des_rect.rect_w & 0x1
       ||layer0_info->des_rect.rect_h & 0x1)
    {
        ALOGE("param check err: Line:%d\n", __LINE__);
        return GSP_HAL_PARAM_CHECK_ERR;
    }

    //source check
    if((layer0_info->pitch & 0xfffff000UL)// pitch > 4095
       ||((layer0_info->clip_rect.st_x + layer0_info->clip_rect.rect_w) > layer0_info->pitch) //
       ||((layer0_info->clip_rect.st_y + layer0_info->clip_rect.rect_h) & 0xfffff000UL) // > 4095
      )
    {
        ALOGE("param check err: Line:%d\n", __LINE__);
        return GSP_HAL_PARAM_CHECK_ERR;
    }
    //destination check
    if(((layer0_info->des_rect.st_x + layer0_info->des_rect.rect_w) & 0xfffff000UL) // > 4095
       ||((layer0_info->des_rect.st_y + layer0_info->des_rect.rect_h) & 0xfffff000UL) // > 4095
      )
    {

        ALOGE("param check err: Line:%d\n", __LINE__);
        return GSP_HAL_PARAM_CHECK_ERR;
    }

    if(layer0_info->rot_angle >= GSP_ROT_ANGLE_MAX_NUM)
    {

        ALOGE("param check err: Line:%d\n", __LINE__);
        return GSP_HAL_PARAM_CHECK_ERR;
    }

    //scaling range check
    if(layer0_info->rot_angle == GSP_ROT_ANGLE_90
       ||layer0_info->rot_angle == GSP_ROT_ANGLE_270
       ||layer0_info->rot_angle == GSP_ROT_ANGLE_90_M
       ||layer0_info->rot_angle == GSP_ROT_ANGLE_270_M)
    {
        coef_factor_w = layer0_info->clip_rect.rect_h*1.0/layer0_info->des_rect.rect_w;
        coef_factor_h = layer0_info->clip_rect.rect_w*1.0/layer0_info->des_rect.rect_h;
        //coef_factor_w = CEIL(layer0_info->clip_rect.rect_h*100,layer0_info->des_rect.rect_w);
        //coef_factor_h = CEIL(layer0_info->clip_rect.rect_w*100,layer0_info->des_rect.rect_h);
    }
    else
    {
        coef_factor_w = layer0_info->clip_rect.rect_w*1.0/layer0_info->des_rect.rect_w;
        coef_factor_h = layer0_info->clip_rect.rect_h*1.0/layer0_info->des_rect.rect_h;
        //coef_factor_w = CEIL(layer0_info->clip_rect.rect_w*100,layer0_info->des_rect.rect_w);
        //coef_factor_h = CEIL(layer0_info->clip_rect.rect_h*100,layer0_info->des_rect.rect_h);
    }
    if(coef_factor_w < 0.25 //larger than 4 times
       ||coef_factor_h < 0.25 //larger than 4 times
       ||coef_factor_w > 16.0 //smaller than 1/16
       ||coef_factor_h > 16.0 //smaller than 1/16
       ||(coef_factor_w > 1.0 && coef_factor_h < 1.0) //one direction scaling down, the other scaling up
       ||(coef_factor_h > 1.0 && coef_factor_w < 1.0) //one direction scaling down, the other scaling up
      )
    {
        ALOGE("param check err: (%dx%d)-Rot:%d->(%dx%d),Line:%d\n",
              layer0_info->clip_rect.rect_w,
              layer0_info->clip_rect.rect_h,
              layer0_info->rot_angle,
              layer0_info->des_rect.rect_w,
              layer0_info->des_rect.rect_h,
              __LINE__);

        ALOGE("param check err: coef_factor_w:%f,coef_factor_h:%f,Line:%d\n",coef_factor_w,coef_factor_h,__LINE__);
        return GSP_HAL_PARAM_CHECK_ERR;
    }

    /*
        //source buffer size check
        pixel_cnt = (layer0_info->clip_rect.st_y + layer0_info->clip_rect.rect_h) * layer0_info->pitch;
        switch(layer0_info->img_format)
        {
        case GSP_SRC_FMT_ARGB888:
        case GSP_SRC_FMT_RGB888:
        {
            //y buffer check
            if(pixel_cnt*4 > (GSP_IMG_SRC1_ADDR_Y - GSP_IMG_SRC0_ADDR_Y))
            {
                GSP_ASSERT();
            }
        }
        break;
        case GSP_SRC_FMT_ARGB565:
        {
            //alpha buffer check
            if(pixel_cnt > GSP_IMG_SRC0_ADDR_AV_SIZE)
            {
                GSP_ASSERT();
            }
        }
        case GSP_SRC_FMT_RGB565:
        {
            //y buffer check
            if(pixel_cnt*2 > (GSP_IMG_SRC0_ADDR_AV - GSP_IMG_SRC0_ADDR_Y))
            {
                GSP_ASSERT();
            }
        }
        break;
        case GSP_SRC_FMT_YUV420_2P:
        {
            //y buffer check
            if(pixel_cnt > GSP_IMG_SRC0_ADDR_Y_SIZE)
            {
                GSP_ASSERT();
            }
            //uv buffer check
            if(pixel_cnt/2 > GSP_IMG_SRC0_ADDR_UV_SIZE)
            {
                GSP_ASSERT();
            }
        }
        break;
        case GSP_SRC_FMT_YUV420_3P:
        {
            //y buffer check
            if(pixel_cnt > GSP_IMG_SRC0_ADDR_Y_SIZE)
            {
                GSP_ASSERT();
            }
            //u buffer check
            if(pixel_cnt/4 > GSP_IMG_SRC0_ADDR_UV_SIZE)
            {
                GSP_ASSERT();
            }
            //v buffer check
            if(pixel_cnt/4 > GSP_IMG_SRC0_ADDR_AV_SIZE)
            {
                GSP_ASSERT();
            }
        }
        break;
        case GSP_SRC_FMT_YUV400_1P:
        {
            //y buffer check
            if(pixel_cnt > GSP_IMG_SRC0_ADDR_Y_SIZE)
            {
                GSP_ASSERT();
            }
        }
        break;
        case GSP_SRC_FMT_YUV422_2P:
        {
            //y buffer check
            if(pixel_cnt > GSP_IMG_SRC0_ADDR_Y_SIZE)
            {
                GSP_ASSERT();
            }
            //uv buffer check
            if(pixel_cnt > GSP_IMG_SRC0_ADDR_UV_SIZE)
            {
                GSP_ASSERT();
            }
        }
        break;
        case GSP_SRC_FMT_8BPP:
        {
            //alpha buffer check
            if(pixel_cnt > GSP_IMG_SRC0_ADDR_Y_SIZE)
            {
                GSP_ASSERT();
            }
        }
        break;
        default:
            GSP_ASSERT();
            break;

        }
    */
    return GSP_NO_ERR;
}


static int32_t gsp_hal_layer1_params_check(GSP_LAYER1_CONFIG_INFO_T *layer1_info)
{
    //uint32_t pixel_cnt = 0x1000000;//max 16M pixel

    if(layer1_info->layer_en == 0)
    {
        return GSP_NO_ERR;
    }

    if(layer1_info->clip_rect.st_x & 0x1
       ||layer1_info->clip_rect.st_y & 0x1
       ||layer1_info->clip_rect.rect_w & 0x1
       ||layer1_info->clip_rect.rect_h & 0x1
       ||layer1_info->des_pos.pos_pt_x & 0x1
       ||layer1_info->des_pos.pos_pt_y & 0x1)
    {
        ALOGE("param check err: Line:%d\n", __LINE__);
        return GSP_HAL_PARAM_CHECK_ERR;
    }

    //source check
    if( (layer1_info->pitch & 0xf000UL)// pitch > 4095
        ||((layer1_info->clip_rect.st_x + layer1_info->clip_rect.rect_w) > layer1_info->pitch) //
        ||((layer1_info->clip_rect.st_y + layer1_info->clip_rect.rect_h) & 0xfffff000UL) // > 4095
      )
    {

        ALOGE("param check err: Line:%d\n", __LINE__);
        return GSP_HAL_PARAM_CHECK_ERR;
    }

    if(layer1_info->rot_angle >= GSP_ROT_ANGLE_MAX_NUM)
    {

        ALOGE("param check err: Line:%d\n", __LINE__);
        return GSP_HAL_PARAM_CHECK_ERR;
    }

    /*
        //source buffer size check
        pixel_cnt = (layer1_info->clip_rect.st_y + layer1_info->clip_rect.rect_h) * layer1_info->pitch;
        switch(layer1_info->img_format)
        {
        case GSP_SRC_FMT_ARGB888:
        case GSP_SRC_FMT_RGB888:
        {
            //y buffer check
            if(pixel_cnt*4 > (GSP_IMG_DST_ADDR_Y - GSP_IMG_SRC1_ADDR_Y))
            {
                GSP_ASSERT();
            }
        }
        break;
        case GSP_SRC_FMT_ARGB565:
        {
            //alpha buffer check
            if(pixel_cnt > GSP_IMG_SRC1_ADDR_AV_SIZE)
            {
                GSP_ASSERT();
            }
        }
        case GSP_SRC_FMT_RGB565:
        {
            //y buffer check
            if(pixel_cnt*2 > (GSP_IMG_SRC1_ADDR_AV - GSP_IMG_SRC1_ADDR_Y))
            {
                GSP_ASSERT();
            }
        }
        break;
        case GSP_SRC_FMT_YUV420_2P:
        {
            //y buffer check
            if(pixel_cnt > GSP_IMG_SRC1_ADDR_Y_SIZE)
            {
                GSP_ASSERT();
            }
            //uv buffer check
            if(pixel_cnt/2 > GSP_IMG_SRC1_ADDR_UV_SIZE)
            {
                GSP_ASSERT();
            }
        }
        break;
        case GSP_SRC_FMT_YUV420_3P:
        {
            //y buffer check
            if(pixel_cnt > GSP_IMG_SRC1_ADDR_Y_SIZE)
            {
                GSP_ASSERT();
            }
            //u buffer check
            if(pixel_cnt/4 > GSP_IMG_SRC1_ADDR_UV_SIZE)
            {
                GSP_ASSERT();
            }
            //v buffer check
            if(pixel_cnt/4 > GSP_IMG_SRC1_ADDR_AV_SIZE)
            {
                GSP_ASSERT();
            }
        }
        break;
        case GSP_SRC_FMT_YUV400_1P:
        {
            //y buffer check
            if(pixel_cnt > GSP_IMG_SRC1_ADDR_Y_SIZE)
            {
                GSP_ASSERT();
            }
        }
        break;
        case GSP_SRC_FMT_YUV422_2P:
        {
            //y buffer check
            if(pixel_cnt > GSP_IMG_SRC1_ADDR_Y_SIZE)
            {
                GSP_ASSERT();
            }
            //uv buffer check
            if(pixel_cnt > GSP_IMG_SRC1_ADDR_UV_SIZE)
            {
                GSP_ASSERT();
            }
        }
        break;
        case GSP_SRC_FMT_8BPP:
        {
            //alpha buffer check
            if(pixel_cnt > GSP_IMG_SRC1_ADDR_Y_SIZE)
            {
                GSP_ASSERT();
            }
        }
        break;
        default:
            GSP_ASSERT();
            break;

        }
        */
    return GSP_NO_ERR;
}
static int32_t gsp_hal_misc_params_check(GSP_CONFIG_INFO_T *gsp_cfg_info)
{
    if((gsp_cfg_info->misc_info.gsp_clock & (~3))
       ||(gsp_cfg_info->misc_info.ahb_clock & (~3)))
    {
        ALOGE("param check err: gsp_clock or ahb_clock larger than 3! Line:%d\n", __LINE__);
        return GSP_HAL_PARAM_CHECK_ERR;
    }
    if(gsp_cfg_info->layer0_info.layer_en == 1 && gsp_cfg_info->layer0_info.pallet_en == 1
       && gsp_cfg_info->layer1_info.layer_en == 1 && gsp_cfg_info->layer1_info.pallet_en == 1)
    {
        ALOGE("param check err: both layer pallet are enable! Line:%d\n", __LINE__);
        return GSP_HAL_PARAM_CHECK_ERR;
    }
    return GSP_NO_ERR;
}

static int32_t gsp_hal_layerdes_params_check(GSP_CONFIG_INFO_T *gsp_cfg_info)
{
    //uint32_t pixel_cnt = 0x1000000;//max 16M pixel
    //uint32_t max_h0 = 4096;//max 4k
    //uint32_t max_h1 = 4096;//max 4k
    //uint32_t max_h = 4096;//max 4k

    GSP_LAYER0_CONFIG_INFO_T    *layer0_info = &gsp_cfg_info->layer0_info;
    GSP_LAYER1_CONFIG_INFO_T    *layer1_info = &gsp_cfg_info->layer1_info;
    GSP_LAYER_DES_CONFIG_INFO_T *layer_des_info = &gsp_cfg_info->layer_des_info;

    if((layer0_info->layer_en == 0) && (layer1_info->layer_en == 0))
    {

        ALOGE("param check err: Line:%d\n", __LINE__);
        return GSP_HAL_PARAM_CHECK_ERR;
    }

    if((layer_des_info->pitch & 0xfffff000UL))   // des pitch > 4095
    {

        ALOGE("param check err: Line:%d\n", __LINE__);
        return GSP_HAL_PARAM_CHECK_ERR;
    }
    if(layer0_info->layer_en == 1)
    {
        if((layer0_info->des_rect.st_x + layer0_info->des_rect.rect_w) > layer_des_info->pitch)
        {

            ALOGE("param check err: Line:%d\n", __LINE__);
            return GSP_HAL_PARAM_CHECK_ERR;
        }
    }

    if(layer1_info->layer_en == 1)
    {
        if((layer1_info->des_pos.pos_pt_x + layer1_info->clip_rect.rect_w > layer_des_info->pitch)
           &&(layer1_info->rot_angle == GSP_ROT_ANGLE_0
              ||layer1_info->rot_angle == GSP_ROT_ANGLE_180
              ||layer1_info->rot_angle == GSP_ROT_ANGLE_0_M
              ||layer1_info->rot_angle == GSP_ROT_ANGLE_180_M))
        {

            ALOGE("param check err: Line:%d\n", __LINE__);
            return GSP_HAL_PARAM_CHECK_ERR;
        }
        else if((layer1_info->des_pos.pos_pt_x + layer1_info->clip_rect.rect_h > layer_des_info->pitch)
                &&(layer1_info->rot_angle == GSP_ROT_ANGLE_90
                   ||layer1_info->rot_angle == GSP_ROT_ANGLE_270
                   ||layer1_info->rot_angle == GSP_ROT_ANGLE_90_M
                   ||layer1_info->rot_angle == GSP_ROT_ANGLE_270_M))
        {

            ALOGE("param check err: Line:%d\n", __LINE__);
            return GSP_HAL_PARAM_CHECK_ERR;
        }
    }

    if((GSP_DST_FMT_YUV420_2P <= layer_des_info->img_format) && (layer_des_info->img_format <= GSP_DST_FMT_YUV422_2P))   //des color is yuv
    {
        if((layer0_info->des_rect.st_x & 0x01)
           ||(layer0_info->des_rect.st_y & 0x01)
           ||(layer1_info->des_pos.pos_pt_x & 0x01)
           ||(layer1_info->des_pos.pos_pt_y & 0x01))   //des start point at odd address
        {

            ALOGE("param check err: Line:%d\n", __LINE__);
            return GSP_HAL_PARAM_CHECK_ERR;
        }
    }

    if(layer_des_info->compress_r8_en == 1
       && layer_des_info->img_format != GSP_DST_FMT_RGB888)
    {

        ALOGE("param check err: Line:%d\n", __LINE__);
        return GSP_HAL_PARAM_CHECK_ERR;
    }
    /*
        //destination buffer size check
        max_h0 = layer0_info->des_rect.st_y + layer0_info->des_rect.rect_h;
        if((layer1_info->clip_rect.rect_w > layer1_info->clip_rect.rect_h)
                && (layer1_info->rot_angle == GSP_ROT_ANGLE_90
                    ||layer1_info->rot_angle == GSP_ROT_ANGLE_270
                    ||layer1_info->rot_angle == GSP_ROT_ANGLE_90_M
                    ||layer1_info->rot_angle == GSP_ROT_ANGLE_270_M))
        {
            max_h1 = layer1_info->des_pos.pos_pt_y + layer1_info->clip_rect.rect_w;
        }
        else
        {
            max_h1 = layer1_info->des_pos.pos_pt_y + layer1_info->clip_rect.rect_h;
        }
        max_h = (max_h0 > max_h1)?max_h0:max_h1;
        pixel_cnt = max_h * layer_des_info->pitch;

        switch(layer_des_info->img_format)
        {
        case GSP_DST_FMT_ARGB888:
        case GSP_DST_FMT_RGB888:
        {
            //y buffer check
            if(pixel_cnt*4 > (GSP_IMG_DST_ADDR_END - GSP_IMG_DST_ADDR_Y))
            {
                GSP_ASSERT();
            }
        }
        break;
        case GSP_DST_FMT_ARGB565:
        {
            //alpha buffer check
            if(pixel_cnt > GSP_IMG_DST_ADDR_AV_SIZE)
            {
                GSP_ASSERT();
            }
        }
        case GSP_DST_FMT_RGB565:
        {
            //y buffer check
            if(pixel_cnt*2 > (GSP_IMG_DST_ADDR_AV - GSP_IMG_DST_ADDR_Y))
            {
                GSP_ASSERT();
            }
        }
        break;
        case GSP_DST_FMT_YUV420_2P:
        {
            //y buffer check
            if(pixel_cnt > GSP_IMG_DST_ADDR_Y_SIZE)
            {
                GSP_ASSERT();
            }
            //uv buffer check
            if(pixel_cnt/2 > GSP_IMG_DST_ADDR_UV_SIZE)
            {
                GSP_ASSERT();
            }
        }
        break;
        case GSP_DST_FMT_YUV420_3P:
        {
            //y buffer check
            if(pixel_cnt > GSP_IMG_DST_ADDR_Y_SIZE)
            {
                GSP_ASSERT();
            }
            //u buffer check
            if(pixel_cnt/4 > GSP_IMG_DST_ADDR_UV_SIZE)
            {
                GSP_ASSERT();
            }
            //v buffer check
            if(pixel_cnt/4 > GSP_IMG_DST_ADDR_AV_SIZE)
            {
                GSP_ASSERT();
            }
        }
        break;
        case GSP_DST_FMT_YUV422_2P:
        {
            //y buffer check
            if(pixel_cnt > GSP_IMG_DST_ADDR_Y_SIZE)
            {
                GSP_ASSERT();
            }
            //uv buffer check
            if(pixel_cnt > GSP_IMG_DST_ADDR_UV_SIZE)
            {
                GSP_ASSERT();
            }
        }
        break;
        default:
            GSP_ASSERT();
            break;
        }
        */
    return GSP_NO_ERR;
}

/*
func:gsp_hal_params_check
desc:check gsp config params before config to kernel
return:0 means success,other means failed
*/
static int32_t gsp_hal_params_check(GSP_CONFIG_INFO_T *gsp_cfg_info)
{
    if(gsp_hal_layer0_params_check(&gsp_cfg_info->layer0_info)
       ||gsp_hal_layer1_params_check(&gsp_cfg_info->layer1_info)
       ||gsp_hal_misc_params_check(gsp_cfg_info)
       ||gsp_hal_layerdes_params_check(gsp_cfg_info))
    {
        return GSP_HAL_PARAM_CHECK_ERR;
    }
    return GSP_NO_ERR;
}


/*
func:gsp_hal_open
desc:open GSP device
return: -1 means failed,other success
notes: a thread can't open the device again unless it close first
*/
int32_t gsp_hal_open(void)
{
    int32_t gsp_fd = -1;

    gsp_fd = open("/dev/sprd_gsp", O_RDWR, 0);
    if (-1 == gsp_fd)
    {
        ALOGE("open gsp device failed! Line:%d \n", __LINE__);
    }

    return gsp_fd;
}


/*
func:gsp_hal_close
desc:close GSP device
return: -1 means failed,0 success
notes:
*/
int32_t gsp_hal_close(int32_t gsp_fd)
{
    if(gsp_fd == -1)
    {
        ALOGE("%s[%d]: err gsp_fd is null! return.\n",__func__, __LINE__);
        return GSP_HAL_PARAM_ERR;
    }

    if (close(gsp_fd))
    {
        if (close(gsp_fd))
        {
            ALOGE("gsp_rotation err : close gsp_fd . Line:%d \n", __LINE__);
            return -1;
        }
    }

    return 0;
}

/*
func:gsp_hal_config
desc:set GSP device config parameters
return: -1 means failed,0 success
notes:
*/
int32_t gsp_hal_config(int32_t gsp_fd,GSP_CONFIG_INFO_T *gsp_cfg_info)
{
    int32_t ret = 0;

    if(gsp_fd == -1)
    {
        return GSP_HAL_PARAM_ERR;
    }

    //software params check
    ret = gsp_hal_params_check(gsp_cfg_info);
    if(ret)
    {
        ALOGE("gsp param check err,exit without config gsp reg: Line:%d\n", __LINE__);
        return ret;
    }

    ret = ioctl(gsp_fd, GSP_IO_SET_PARAM, gsp_cfg_info);
    if(0 == ret)   //gsp hw check params err
    {
        //ALOGI_IF(debugenable,"gsp set params ok, trigger now. Line:%d \n", __LINE__);
        //ALOGI_IF(debugenable,"gsp set params ok, trigger now. Line:%d \n", __LINE__);
    }
    else
    {
        ALOGE("hwcomposer gsp set params err:%d  . Line:%d \n",ret, __LINE__);
        //ret = -1;
    }
    return ret;
}


/*
func:gsp_get_addr_type
desc:set GSP device config parameters
return:
notes:
*/
/*
int32_t gsp_get_addr_type(int32_t gsp_fd,GSP_ADDR_TYPE_E* pType)
{
    int32_t ret = 0;

    if(gsp_fd == -1 || pType == NULL)
    {
        ALOGE("%s[%d]: err gsp_fd is null,or pType is null! return.\n",__func__,__LINE__);
        return GSP_HAL_PARAM_ERR;
    }

    ALOGI_IF(debugenable,"gsp get addr type,ctl code:%08x,Line:%d \n", GSP_IO_GET_ADDR_TYPE,__LINE__);
    ret = ioctl(gsp_fd, GSP_IO_GET_ADDR_TYPE, 1);
    ALOGI_IF(debugenable,"gsp get addr type return %d,Line:%d \n", ret,__LINE__);
    if(GSP_ADDR_TYPE_INVALUE < ret && ret < GSP_ADDR_TYPE_MAX)
    {
        *pType = (GSP_ADDR_TYPE_E)ret;
        ret = 0;
    }
    else
    {
        ALOGE("%s[%d]: get addr type return err!\n",__func__,__LINE__);
    }
    return ret;
}
*/

/*
func:gsp_hal_trigger
desc:trigger GSP to run
return: -1 means failed,0 success
notes:
*/
int32_t gsp_hal_trigger(int32_t gsp_fd)
{
    int32_t ret = GSP_NO_ERR;

    if(gsp_fd == -1)
    {
        return GSP_HAL_PARAM_ERR;
    }

    ret = ioctl(gsp_fd, GSP_IO_TRIGGER_RUN, 1);
    if(0 == ret)
    {
        //ALOGE("gsp trigger ok, Line:%d \n", __LINE__);
        //ALOGI_IF(debugenable,"gsp trigger ok, Line:%d \n", __LINE__);
    }
    else
    {
        ALOGE("gsp trigger err:%d  . Line:%d \n",ret, __LINE__);
        //ret = -1;
    }

    return ret;
}


/*
func:gsp_hal_waitdone
desc:wait GSP finish
return: -1 means thread interrupt by signal,0 means GSP done successfully
notes:
*/
int32_t gsp_hal_waitdone(int32_t gsp_fd)
{
    int32_t ret = GSP_NO_ERR;

    if(gsp_fd == -1)
    {
        return GSP_HAL_PARAM_ERR;
    }

    ret = ioctl(gsp_fd, GSP_IO_WAIT_FINISH, 1);
    if(0 == ret)
    {
        //ALOGE("gsp wait finish ok, return. Line:%d \n", __LINE__);
        //ALOGI_IF(debugenable,"gsp wait finish ok, return. Line:%d \n", __LINE__);
    }
    else
    {
        ALOGE("gsp wait finish err:%d  . Line:%d\n",ret, __LINE__);
        //ret = -1;
    }

    return ret;
}


/*
func:GSP_Proccess
desc:all the GSP function can be complete in this function, such as CFC,scaling,blend,rotation and mirror,clipping.
note:1 the source and destination image buffer should be physical-coherent memory space,
       the caller should ensure the in and out buffer size is large enough, or the GSP will access cross the buffer border,
       these two will rise unexpected exception.
     2 this function will be block until GSP process over.
return: 0 success, other err
*/
int32_t GSP_Proccess(GSP_CONFIG_INFO_T *pgsp_cfg_info)
{
    int32_t ret = 0;
    int32_t gsp_fd = -1;

    gsp_fd = gsp_hal_open();
    if(-1 == gsp_fd)
    {
        ALOGE("%s:%d,opend gsp failed \n", __func__, __LINE__);
        return GSP_HAL_PARAM_ERR;
    }

    ret = gsp_hal_config(gsp_fd,pgsp_cfg_info);
    if(0 != ret)
    {
        ALOGE("%s:%d,cfg gsp failed \n", __func__, __LINE__);
        goto exit;
    }
/*
    ret = gsp_hal_trigger(gsp_fd);
    if(0 != ret)
    {
        ALOGE("%s:%d,trigger gsp failed \n", __func__, __LINE__);
        goto exit;
    }

    ret = gsp_hal_waitdone(gsp_fd);
    if(0 != ret)
    {
        ALOGE("%s:%d,wait done gsp failed \n", __func__, __LINE__);
        goto exit;
    }
*/

exit:
    //ret = gsp_hal_close(gsp_fd);
    gsp_hal_close(gsp_fd);
    return ret;
}


/*
func:GSP_GetAddrType
desc:get the address type of GSP can process, virtual addr, or physical
return:
*/
/*
int32_t GSP_GetAddrType(GSP_ADDR_TYPE_E* pType)
{
    int32_t ret = 0;
    int32_t gsp_fd = -1;
    static volatile GSP_ADDR_TYPE_E s_GSPAddrType = GSP_ADDR_TYPE_INVALUE;

    if(pType == NULL)
    {
        ALOGE("%s[%d]: err pType is null! return.\n",__func__,__LINE__);
        return GSP_HAL_PARAM_ERR;
    }
    *pType = s_GSPAddrType;

    if(*pType == GSP_ADDR_TYPE_INVALUE)
    {
        gsp_fd = gsp_hal_open();
        if(-1 == gsp_fd)
        {
            ALOGE("%s:%d,opend gsp failed \n", __func__, __LINE__);
            return GSP_HAL_KERNEL_DRIVER_NOT_EXIST;
        }

        ret = gsp_get_addr_type(gsp_fd,pType);
        if(0 != ret)
        {
            ALOGE("%s:%d,gsp_get_addr_type() failed \n", __func__, __LINE__);
            goto exit;
        }
        s_GSPAddrType = *pType;

    exit:
        gsp_hal_close(gsp_fd);
    }
    return ret;
}

*/



