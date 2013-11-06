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
#ifndef _GSP_HAL_H_
#define _GSP_HAL_H_

#ifdef   __cplusplus
extern   "C"
{
#endif

#include <fcntl.h>
#include <sys/ioctl.h>
#include <cutils/log.h>
//#include "sprd_rot_k.h"
#include "gsp_types_shark.h"
//#include "scale_rotate.h"
//#include "img_scale_u.h"
//#include "cmr_common.h"
//#include <semaphore.h>
#include <stdlib.h>



    /*
    func:gsp_hal_open
    desc:open GSP device
    return: -1 means failed,other success
    notes: a thread can't open the device again unless it close first
    */
    extern int32_t gsp_hal_open(void);



    /*
    func:gsp_hal_close
    desc:close GSP device
    return: -1 means failed,0 success
    notes:
    */
    extern int32_t gsp_hal_close(int32_t gsp_fd);


    /*
    func:gsp_hal_config
    desc:set GSP device config parameters
    return: -1 means failed,0 success
    notes:
    */
    extern int32_t gsp_hal_config(int32_t gsp_fd,GSP_CONFIG_INFO_T *gsp_cfg_info);


    /*
    func:gsp_hal_trigger
    desc:trigger GSP to run
    return: -1 means failed,0 success
    notes:
    */
    extern int32_t gsp_hal_trigger(int32_t gsp_fd);


    /*
    func:gsp_hal_waitdone
    desc:wait GSP finish
    return: -1 means thread interrupt by signal,0 means GSP done successfully
    notes:
    */
    extern int32_t gsp_hal_waitdone(int32_t gsp_fd);


    /*
    func:GSP_CFC
    desc:implement color format convert
    note:1 the source and destination image buffer should be physical-coherent memory space,
           the caller should ensure the in and out buffer size is large enough, or the GSP will access cross the buffer border,
           these two will rise unexpected exception.
         2 this function will be block until GSP process over.
    */
    extern int32_t GSP_CFC(GSP_LAYER_SRC_DATA_FMT_E in_format,
                           GSP_LAYER_DST_DATA_FMT_E out_format,
                           uint32_t width,
                           uint32_t height,
                           uint32_t in_vaddr,
                           uint32_t in_paddr,
                           uint32_t out_vaddr,
                           uint32_t out_paddr);

    /*
    func:GSP_Proccess
    desc:all the GSP function can be complete in this function, such as CFC,scaling,blend,rotation and mirror,clipping.
    note:1 the source and destination image buffer should be physical-coherent memory space,
           the caller should ensure the in and out buffer size is large enough, or the GSP will access cross the buffer border,
           these two will rise unexpected exception.
         2 this function will be block until GSP process over.
    */

    int32_t GSP_Proccess(GSP_CONFIG_INFO_T *pgsp_cfg_info);

#ifdef GSP_HAL_TEST
    extern HW_SCALE_DATA_FORMAT_E g_output_fmt;
    extern uint32_t g_output_width;
    extern uint32_t g_output_height;
    extern uint32_t g_output_yaddr;
    extern uint32_t g_output_uvaddr;
    extern HW_SCALE_DATA_FORMAT_E g_input_fmt;
    extern uint32_t g_input_uv_endian;
    extern uint32_t g_input_width;
    extern uint32_t g_input_height;
    extern uint32_t g_input_yaddr;
    extern uint32_t g_intput_uvaddr;
    extern struct sprd_rect g_trim_rect;
    extern HW_ROTATION_MODE_E g_rotation;
    extern int32_t create_gsp_test_thread(void);
#endif

#ifdef   __cplusplus
}
#endif

#endif


