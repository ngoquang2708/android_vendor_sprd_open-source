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


#include "gsp_hal.h"

#define LOGE(x...)  fprintf(stdout, ##x)

static int32_t gsp_hal_layer0_params_check (GSP_LAYER0_CONFIG_INFO_T *layer0_info)
{
    float coef_factor_w = 0.0;
    float coef_factor_h = 0.0;

    uint32_t pixel_cnt = 0x1000000;//max 16M pixel

    if(layer0_info->layer_en == 0)
    {
        return 0;
    }

    //source check
    if((layer0_info->pitch & 0xfffff000UL)// pitch > 4095
       ||((layer0_info->clip_rect.st_x + layer0_info->clip_rect.rect_w) > layer0_info->pitch) //
       ||((layer0_info->clip_rect.st_y + layer0_info->clip_rect.rect_h) & 0xfffff000UL) // > 4095
      )
    {
        LOGE("param check err: Line:%d\n", __LINE__);
        return -1;
    }
    //destination check
    if(((layer0_info->des_rect.st_x + layer0_info->des_rect.rect_w) & 0xfffff000UL) // > 4095
       ||((layer0_info->des_rect.st_y + layer0_info->des_rect.rect_h) & 0xfffff000UL) // > 4095
      )
    {

        LOGE("param check err: Line:%d\n", __LINE__);
        return -1;
    }

    if(layer0_info->rot_angle >= GSP_ROT_ANGLE_MAX_NUM)
    {

        LOGE("param check err: Line:%d\n", __LINE__);
        return -1;
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
        LOGE("param check err: (%dx%d)-Rot:%d->(%dx%d),Line:%d\n",
             layer0_info->clip_rect.rect_w,
             layer0_info->clip_rect.rect_h,
             layer0_info->rot_angle,
             layer0_info->des_rect.rect_w,
             layer0_info->des_rect.rect_h,
             __LINE__);

        LOGE("param check err: coef_factor_w:%ul,coef_factor_h:%ul,Line:%d\n",coef_factor_w,coef_factor_h, __LINE__);
        return -1;
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
    return 0;
}


static int32_t gsp_hal_layer1_params_check(GSP_LAYER1_CONFIG_INFO_T *layer1_info)
{
    uint32_t pixel_cnt = 0x1000000;//max 16M pixel

    if(layer1_info->layer_en == 0)
    {
        return 0;
    }

    //source check
    if( (layer1_info->pitch & 0xf000UL)// pitch > 4095
        ||((layer1_info->clip_rect.st_x + layer1_info->clip_rect.rect_w) > layer1_info->pitch) //
        ||((layer1_info->clip_rect.st_y + layer1_info->clip_rect.rect_h) & 0xfffff000UL) // > 4095
      )
    {

        LOGE("param check err: Line:%d\n", __LINE__);
        return -1;
    }

    if(layer1_info->rot_angle >= GSP_ROT_ANGLE_MAX_NUM)
    {

        LOGE("param check err: Line:%d\n", __LINE__);
        return -1;
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
    return 0;
}
static int32_t gsp_hal_misc_params_check(GSP_MISC_CONFIG_INFO_T *misc_info)
{
    return 0;
}

static int32_t gsp_hal_layerdes_params_check(GSP_CONFIG_INFO_T *gsp_cfg_info)
{
    uint32_t pixel_cnt = 0x1000000;//max 16M pixel
    uint32_t max_h0 = 4096;//max 4k
    uint32_t max_h1 = 4096;//max 4k
    uint32_t max_h = 4096;//max 4k

    GSP_LAYER0_CONFIG_INFO_T    *layer0_info = &gsp_cfg_info->layer0_info;
    GSP_LAYER1_CONFIG_INFO_T    *layer1_info = &gsp_cfg_info->layer1_info;
    GSP_LAYER_DES_CONFIG_INFO_T *layer_des_info = &gsp_cfg_info->layer_des_info;

    if(layer0_info->layer_en == 0 && layer1_info->layer_en == 0)
    {

        LOGE("param check err: Line:%d\n", __LINE__);
        return -1;
    }

    if((layer_des_info->pitch & 0xfffff000UL))// des pitch > 4095
    {

        LOGE("param check err: Line:%d\n", __LINE__);
        return -1;
    }
    if(layer0_info->layer_en == 1)
    {
        if((layer0_info->des_rect.st_x + layer0_info->des_rect.rect_w) > layer_des_info->pitch)
        {

            LOGE("param check err: Line:%d\n", __LINE__);
            return -1;
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

            LOGE("param check err: Line:%d\n", __LINE__);
            return -1;
        }
        else if((layer1_info->des_pos.pos_pt_x + layer1_info->clip_rect.rect_h > layer_des_info->pitch)
                &&(layer1_info->rot_angle == GSP_ROT_ANGLE_90
                   ||layer1_info->rot_angle == GSP_ROT_ANGLE_270
                   ||layer1_info->rot_angle == GSP_ROT_ANGLE_90_M
                   ||layer1_info->rot_angle == GSP_ROT_ANGLE_270_M))
        {

            LOGE("param check err: Line:%d\n", __LINE__);
            return -1;
        }
    }

    if((GSP_DST_FMT_YUV420_2P <= layer_des_info->img_format) && (layer_des_info->img_format <= GSP_DST_FMT_YUV422_2P))//des color is yuv
    {
        if((layer0_info->des_rect.st_x & 0x01)
           ||(layer0_info->des_rect.st_y & 0x01)
           ||(layer1_info->des_pos.pos_pt_x & 0x01)
           ||(layer1_info->des_pos.pos_pt_y & 0x01))//des start point at odd address
        {

            LOGE("param check err: Line:%d\n", __LINE__);
            return -1;
        }
    }

    if(layer_des_info->compress_r8_en == 1
       && layer_des_info->img_format != GSP_DST_FMT_RGB888)
    {

        LOGE("param check err: Line:%d\n", __LINE__);
        return -1;
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
    return 0;
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
       ||gsp_hal_misc_params_check(&gsp_cfg_info->misc_info)
       ||gsp_hal_layerdes_params_check(gsp_cfg_info))
    {
        return -1;
    }
    return 0;
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
        LOGE("gsp thread%d,Camera_rotation fail : open gsp device. Line:%d \n", __LINE__);
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
        return -1;
    }

    if (close(gsp_fd))
    {
        if (close(gsp_fd))
        {
            LOGE("gsp_rotation err : close gsp_fd . Line:%d \n", __LINE__);
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
        return -1;
    }

    //software params check
    ret = gsp_hal_params_check(gsp_cfg_info);
    if(ret)
    {
        LOGE("gsp param check err,exit without config gsp reg: Line:%d\n", __LINE__);
        return -1;
    }

    ret = ioctl(gsp_fd, GSP_IO_SET_PARAM, gsp_cfg_info);
    if(0 == ret)//gsp hw check params err
    {
        LOGE("gsp set params ok, trigger now. Line:%d \n", __LINE__);
    }
    else
    {
        LOGE("hwcomposer gsp set params err:%d  . Line:%d \n",ret, __LINE__);
        ret = -1;
    }
    return ret;
}



/*
func:gsp_hal_trigger
desc:trigger GSP to run
return: -1 means failed,0 success
notes:
*/
int32_t gsp_hal_trigger(int32_t gsp_fd)
{
    int32_t ret = 0;

    if(gsp_fd == -1)
    {
        return -1;
    }

    ret = ioctl(gsp_fd, GSP_IO_TRIGGER_RUN, 1);
    if(0 == ret)
    {
        LOGE("gsp trigger ok, Line:%d \n", __LINE__);
    }
    else
    {
        LOGE("gsp trigger err:%d  . Line:%d \n",ret, __LINE__);
        ret = -1;
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
    int32_t ret = 0;

    if(gsp_fd == -1)
    {
        return -1;
    }

    ret = ioctl(gsp_fd, GSP_IO_WAIT_FINISH, 1);
    if(0 == ret)
    {
        LOGE("gsp wait finish ok, return. Line:%d \n", __LINE__);
    }
    else
    {
        LOGE("gsp wait finish err:%d  . Line:%d\n",ret, __LINE__);
        ret = -1;
    }

    return ret;
}

/*
func:GSP_CFC
desc:implement color format convert
note:1 the source and destination image buffer should be physical-coherent memory space,
       the caller should ensure the in and out buffer size is large enough, or the GSP will access cross the buffer border,
       these two will rise unexpected exception.
     2 this function will be block until GSP process over.
*/
int32_t GSP_CFC(GSP_LAYER_SRC_DATA_FMT_E in_format,
                GSP_LAYER_DST_DATA_FMT_E out_format,
                uint32_t width,
                uint32_t height,
                uint32_t in_vaddr,
                uint32_t in_paddr,
                uint32_t out_vaddr,
                uint32_t out_paddr)
{
    int32_t ret = 0;
    int32_t gsp_fd = -1;
    uint32_t pixel_cnt = 0;
    GSP_CONFIG_INFO_T gsp_cfg_info = {0};

    LOGE("%s:%d,informat:%d outformat:%d w:%d h:%d invaddr:0x%08x inpaddr:0x%08x outvaddr:0x%08x outpaddr:0x%08x \n",
         __func__, __LINE__,
         in_format,
         out_format,
         width,
         height,
         in_vaddr,
         in_paddr,
         out_vaddr,
         out_paddr);

    gsp_cfg_info.layer1_info.img_format = in_format;
    gsp_cfg_info.layer1_info.clip_rect.rect_w = width;
    gsp_cfg_info.layer1_info.clip_rect.rect_h = height;
    gsp_cfg_info.layer1_info.pitch = width;
    gsp_cfg_info.layer1_info.layer_en = 1;

    pixel_cnt = width*height;
    switch(in_format)
    {
        case GSP_SRC_FMT_ARGB888:
        case GSP_SRC_FMT_RGB888:
        case GSP_SRC_FMT_RGB565:
            gsp_cfg_info.layer1_info.src_addr.addr_y = in_paddr;
            break;
        case GSP_SRC_FMT_YUV422_2P:
        case GSP_SRC_FMT_YUV420_2P:
            gsp_cfg_info.layer1_info.src_addr.addr_y = in_paddr;
            gsp_cfg_info.layer1_info.src_addr.addr_uv =
                gsp_cfg_info.layer1_info.src_addr.addr_v = in_paddr + pixel_cnt;
            break;
        case GSP_SRC_FMT_YUV420_3P:
            gsp_cfg_info.layer1_info.src_addr.addr_y = in_paddr;
            gsp_cfg_info.layer1_info.src_addr.addr_uv = in_paddr + pixel_cnt;
            gsp_cfg_info.layer1_info.src_addr.addr_v = gsp_cfg_info.layer1_info.src_addr.addr_uv + pixel_cnt/4;
            break;

        case GSP_SRC_FMT_ARGB565:
        default:
            ret = -1;
            return ret;
            break;
    }

    gsp_cfg_info.layer_des_info.img_format = out_format;
    switch(out_format)
    {
        case GSP_DST_FMT_ARGB888:
        case GSP_DST_FMT_RGB888:
        case GSP_DST_FMT_RGB565:
            gsp_cfg_info.layer_des_info.src_addr.addr_y = out_paddr;
            break;
        case GSP_DST_FMT_YUV422_2P:
        case GSP_DST_FMT_YUV420_2P:
            gsp_cfg_info.layer_des_info.src_addr.addr_y = out_paddr;
            gsp_cfg_info.layer_des_info.src_addr.addr_uv =
                gsp_cfg_info.layer_des_info.src_addr.addr_v = out_paddr + pixel_cnt;
            break;
        case GSP_DST_FMT_YUV420_3P:
            gsp_cfg_info.layer_des_info.src_addr.addr_y = out_paddr;
            gsp_cfg_info.layer_des_info.src_addr.addr_uv = out_paddr + pixel_cnt;
            gsp_cfg_info.layer_des_info.src_addr.addr_v = gsp_cfg_info.layer_des_info.src_addr.addr_uv + pixel_cnt/4;
            break;
        case GSP_DST_FMT_ARGB565:
        default:
            ret = -1;
            return ret;
            break;
    }
    gsp_cfg_info.layer_des_info.pitch = gsp_cfg_info.layer1_info.clip_rect.rect_w;


    gsp_fd = gsp_hal_open();
    if(-1 == gsp_fd)
    {
        LOGE("%s:%d,opend gsp failed \n", __func__, __LINE__);
        return -1;
    }

    ret = gsp_hal_config(gsp_fd,&gsp_cfg_info);
    if(-1 == ret)
    {
        LOGE("%s:%d,cfg gsp failed \n", __func__, __LINE__);
        goto exit;
    }
    ret = gsp_hal_trigger(gsp_fd);
    if(-1 == ret)
    {
        LOGE("%s:%d,trigger gsp failed \n", __func__, __LINE__);
        goto exit;
    }
    ret = gsp_hal_waitdone(gsp_fd);
    if(-1 == ret)
    {
        LOGE("%s:%d,wait done gsp failed \n", __func__, __LINE__);
        goto exit;
    }

exit:
    ret = gsp_hal_close(gsp_fd);
    return ret;

}

/*
func:GSP_Proccess
desc:all the GSP function can be complete in this function, such as CFC,scaling,blend,rotation and mirror,clipping.
note:1 the source and destination image buffer should be physical-coherent memory space,
       the caller should ensure the in and out buffer size is large enough, or the GSP will access cross the buffer border,
       these two will rise unexpected exception.
     2 this function will be block until GSP process over.
*/

int32_t GSP_Proccess(GSP_CONFIG_INFO_T *pgsp_cfg_info)
{
    int32_t ret = 0;
    int32_t gsp_fd = -1;

    gsp_fd = gsp_hal_open();
    if(-1 == gsp_fd)
    {
        LOGE("%s:%d,opend gsp failed \n", __func__, __LINE__);
        return -1;
    }

    ret = gsp_hal_config(gsp_fd,pgsp_cfg_info);
    if(-1 == ret)
    {
        LOGE("%s:%d,cfg gsp failed \n", __func__, __LINE__);
        goto exit;
    }
    ret = gsp_hal_trigger(gsp_fd);
    if(-1 == ret)
    {
        LOGE("%s:%d,trigger gsp failed \n", __func__, __LINE__);
        goto exit;
    }
    ret = gsp_hal_waitdone(gsp_fd);
    if(-1 == ret)
    {
        LOGE("%s:%d,wait done gsp failed \n", __func__, __LINE__);
        goto exit;
    }

exit:
    ret = gsp_hal_close(gsp_fd);
    return ret;

}


#ifdef GSP_HAL_TEST

/*
func:gsp_hal_config_and_wait_done
desc:operate on "/dev/sprd_gsp",config GSP parameters and wait GSP done
return:0 means success,other means failed
*/
static int32_t gsp_hal_config_and_wait_done(void* pid,GSP_CONFIG_INFO_T *gsp_cfg_info)
{
    int32_t ret = 0,ret1 = 0;
    int32_t gsp_fd = -1;

    gsp_fd = open("/dev/sprd_gsp", O_RDWR, 0);
    if (-1 == gsp_fd)
    {
        LOGE("gsp thread%d,Camera_rotation fail : open gsp device. Line:%d \n",pid, __LINE__);
        return -1;
    }
    LOGE("gsp thread%d,open /dev/sprd_gsp success:%d  . Line:%d \n",pid,ret, __LINE__);

    LOGE("gsp thread%d,bf set params. Line:%d ",pid, __LINE__);
    ret = ioctl(gsp_fd, GSP_IO_SET_PARAM, gsp_cfg_info);
    if(0 == ret)//gsp hw check params err
    {
        LOGE("gsp thread%d,set params ok, trigger now. Line:%d \n",pid, __LINE__);
        ret = ioctl(gsp_fd, GSP_IO_TRIGGER_RUN, 1);
        if(0 == ret)
        {
            LOGE("gsp thread%d,trigger ok, wait finish. Line:%d \n",pid, __LINE__);
            ret = ioctl(gsp_fd, GSP_IO_WAIT_FINISH, 1);
            if(0 == ret)
            {
                LOGE("gsp thread%d,wait finish ok, return. Line:%d \n",pid, __LINE__);
            }
            else
            {
                LOGE("gsp thread%d,wait finish err:%d  . Line:%d \n",pid,ret, __LINE__);
            }
        }
        else
        {
            LOGE("gsp thread%d,trigger err:%d  . Line:%d \n",pid,ret, __LINE__);
        }
    }
    else
    {
        LOGE("gsp thread%d,hwcomposer gsp set params err:%d  . Line:%d \n",pid,ret, __LINE__);
    }

    if (close(gsp_fd))
    {
        if (close(gsp_fd))
        {
            LOGE("gsp_rotation err : close gsp_fd . Line:%d \n", __LINE__);
            return -1;
        }
    }
    return ret;
}



static int32_t camera_rotation_if(HW_ROTATION_DATA_FORMAT_E rot_format,
                                  int32_t degree,
                                  uint32_t width,
                                  uint32_t height,
                                  uint32_t in_addr,
                                  uint32_t out_addr)
{
    int32_t ret = 0;
    GSP_CONFIG_INFO_T gsp_cfg_info = {0};

    gsp_cfg_info.layer0_info.img_format = HAL2Kernel_RotSrcColorFormatConvert(rot_format);
    gsp_cfg_info.layer0_info.rot_angle = HAL2Kernel_RotMirrConvert(degree);
    gsp_cfg_info.layer0_info.clip_rect.rect_w = width;
    gsp_cfg_info.layer0_info.clip_rect.rect_h = height;
    gsp_cfg_info.layer0_info.pitch = width;
    if(gsp_cfg_info.layer0_info.rot_angle == GSP_ROT_ANGLE_0
       ||gsp_cfg_info.layer0_info.rot_angle == GSP_ROT_ANGLE_180
       ||gsp_cfg_info.layer0_info.rot_angle == GSP_ROT_ANGLE_0_M
       ||gsp_cfg_info.layer0_info.rot_angle == GSP_ROT_ANGLE_180_M)
    {
        gsp_cfg_info.layer0_info.des_rect.rect_w = width;
        gsp_cfg_info.layer0_info.des_rect.rect_h = height;
    }
    else
    {
        gsp_cfg_info.layer0_info.des_rect.rect_w = height;
        gsp_cfg_info.layer0_info.des_rect.rect_h = width;
    }
    gsp_cfg_info.layer0_info.layer_en = 1;
    gsp_cfg_info.layer0_info.src_addr.addr_y = in_addr;
    gsp_cfg_info.layer0_info.src_addr.addr_v = gsp_cfg_info.layer0_info.src_addr.addr_uv = in_addr + width * height;

    gsp_cfg_info.layer_des_info.img_format = HAL2Kernel_RotDesColorFormatConvert(rot_format);
    gsp_cfg_info.layer_des_info.src_addr.addr_y = out_addr;
    gsp_cfg_info.layer_des_info.src_addr.addr_v = gsp_cfg_info.layer_des_info.src_addr.addr_uv = out_addr + width * height;
    gsp_cfg_info.layer_des_info.pitch = gsp_cfg_info.layer0_info.des_rect.rect_w;

    //software params check
    ret = gsp_hal_params_check(&gsp_cfg_info);
    if(ret)
    {
        return ret;
    }
    ret = gsp_hal_config_and_wait_done(0,&gsp_cfg_info);
    return ret;

}


static int32_t do_scaling_and_rotaion_if(void* pid,
        int32_t fd,
        HW_SCALE_DATA_FORMAT_E output_fmt,
        uint32_t output_width,
        uint32_t output_height,
        uint32_t output_yaddr,
        uint32_t output_uvaddr,
        HW_SCALE_DATA_FORMAT_E input_fmt,
        uint32_t input_uv_endian,
        uint32_t input_width,//src pitch
        uint32_t input_height,//src slice
        uint32_t input_yaddr,
        uint32_t intput_uvaddr,
        struct sprd_rect *trim_rect,//
        HW_ROTATION_MODE_E rotation,
        uint32_t tmp_addr)
{
    int32_t ret = 0;
    GSP_CONFIG_INFO_T gsp_cfg_info = {0};

    gsp_cfg_info.layer0_info.img_format = HAL2Kernel_ScaleSrcColorFormatConvert(input_fmt);
    gsp_cfg_info.layer0_info.rot_angle = HAL2Kernel_ScaleAngleConvert(rotation);
    gsp_cfg_info.layer0_info.clip_rect.st_x = trim_rect->x;
    gsp_cfg_info.layer0_info.clip_rect.st_y = trim_rect->y;
    gsp_cfg_info.layer0_info.clip_rect.rect_w = trim_rect->w;
    gsp_cfg_info.layer0_info.clip_rect.rect_h = trim_rect->h;
    gsp_cfg_info.layer0_info.pitch = input_width;
    if(gsp_cfg_info.layer0_info.rot_angle == GSP_ROT_ANGLE_0
       ||gsp_cfg_info.layer0_info.rot_angle == GSP_ROT_ANGLE_180
       ||gsp_cfg_info.layer0_info.rot_angle == GSP_ROT_ANGLE_0_M
       ||gsp_cfg_info.layer0_info.rot_angle == GSP_ROT_ANGLE_180_M)
    {
        gsp_cfg_info.layer0_info.des_rect.rect_w = output_width;
        gsp_cfg_info.layer0_info.des_rect.rect_h = output_height;
    }
    else
    {
        gsp_cfg_info.layer0_info.des_rect.rect_w = output_height;
        gsp_cfg_info.layer0_info.des_rect.rect_h = output_width;
    }
    gsp_cfg_info.layer0_info.endian_mode.uv_word_endn = input_uv_endian;// TODO: the param use correctly?
    gsp_cfg_info.layer0_info.src_addr.addr_y = input_yaddr;
    gsp_cfg_info.layer0_info.src_addr.addr_v = gsp_cfg_info.layer0_info.src_addr.addr_uv = intput_uvaddr;
    gsp_cfg_info.layer0_info.layer_en = 1;

    gsp_cfg_info.layer_des_info.img_format = HAL2Kernel_ScaleDesColorFormatConvert(output_fmt);
    gsp_cfg_info.layer_des_info.src_addr.addr_y = output_yaddr;
    gsp_cfg_info.layer_des_info.src_addr.addr_v = gsp_cfg_info.layer_des_info.src_addr.addr_uv = output_uvaddr;
    gsp_cfg_info.layer_des_info.pitch = gsp_cfg_info.layer0_info.des_rect.rect_w;

    //software params check
    ret = gsp_hal_params_check(&gsp_cfg_info);
    if(ret)
    {
        LOGE("param check err,exit without config gsp reg: Line:%d\n", __LINE__);
        return ret;
    }
    ret = gsp_hal_config_and_wait_done(pid,&gsp_cfg_info);
    return ret;
}


HW_SCALE_DATA_FORMAT_E g_output_fmt;
uint32_t g_output_width;
uint32_t g_output_height;
uint32_t g_output_yaddr;
uint32_t g_output_uvaddr;
HW_SCALE_DATA_FORMAT_E g_input_fmt;
uint32_t g_input_uv_endian;
uint32_t g_input_width;
uint32_t g_input_height;
uint32_t g_input_yaddr;
uint32_t g_intput_uvaddr;
struct sprd_rect g_trim_rect;
HW_ROTATION_MODE_E g_rotation;


static pthread_t          gsp_test_thread0 = 0;
static pthread_t          gsp_test_thread1 = 0;
static pthread_t          gsp_test_thread2 = 0;


static void* gsp_test_thread_proc(void* data)
{
    uint32_t sleep_time = 0;

    LOGE("gsp_test_thread%d_proc: enter. Line:%d\n", data,__LINE__);
    while(1)
    {
        //sleep_time = 1500 + (rand()%1000);
        sleep_time = 50 + (rand()%256);
        LOGE("gsp thread%d,sleep:%dms zzzzzzz...\n",data, sleep_time);
        usleep(sleep_time * 1000);
#if 0
        LOGE("src_color:%d", g_input_fmt);
        LOGE("src_pitch:%d", g_input_width);
        LOGE("src_slice:%d", g_input_height);
        LOGE("src_x:%d", g_trim_rect.x);
        LOGE("src_y:%d", g_trim_rect.y);
        LOGE("src_w:%d", g_trim_rect.w);
        LOGE("src_h:%d", g_trim_rect.h);
        LOGE("src_uv_endian:%d", g_input_uv_endian);
        LOGE("src_rot:%d", g_rotation);
        LOGE("src_yaddr:0x%08x", g_input_yaddr);
        LOGE("src_uvaddr:0x%08x", g_intput_uvaddr);
        LOGE("des_color:%d", g_output_fmt);
        LOGE("des_pitch:%d", g_output_width);
        LOGE("des_slice:%d", g_output_height);
        LOGE("des_yaddr:0x%08x", g_output_yaddr);
        LOGE("des_uvaddr:0x%08x", g_output_uvaddr);
#endif

        do_scaling_and_rotaion_if(data,
                                  NULL,
                                  g_output_fmt,
                                  g_output_width,
                                  g_output_height,
                                  g_output_yaddr,
                                  g_output_uvaddr,
                                  g_input_fmt,
                                  g_input_uv_endian,
                                  g_input_width,
                                  g_input_height,
                                  g_input_yaddr,
                                  g_intput_uvaddr,
                                  &g_trim_rect,
                                  g_rotation,
                                  0);
        LOGE("gsp_test_thread%d_proc: done. Line:%d\n", data,__LINE__);
    }
    LOGE("gsp_test_thread_proc: exit. Line:%d\n", __LINE__);

    gsp_test_thread0 = 0;
    return NULL;
}



int32_t create_gsp_test_thread(void)
{
    int32_t                  ret = 0;
    pthread_attr_t           attr;

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    ret = pthread_create(&gsp_test_thread0, &attr, gsp_test_thread_proc, 0);
    ret = pthread_create(&gsp_test_thread1, &attr, gsp_test_thread_proc, 1);
    ret = pthread_create(&gsp_test_thread2, &attr, gsp_test_thread_proc, 2);
    pthread_attr_destroy(&attr);
    return ret;
}

#endif
