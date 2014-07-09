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
//extern   "C"
//{
#endif
#include <fcntl.h>
#include <sys/ioctl.h>
//#include <cutils/log.h>
#include <stdlib.h>
//#include <hardware/hardware.h>

//#include "gsp_types.h"//reserve for tizen or unit test
#include "gsp_types_shark.h"




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
func:GSP_Proccess
desc:all the GSP function can be complete in this function, such as CFC,scaling,blend,rotation and mirror,clipping.
note:1 the source and destination image buffer should be physical-coherent memory space,
       the caller should ensure the in and out buffer size is large enough, or the GSP will access cross the buffer border,
       these two will rise unexpected exception.
     2 this function will be block until GSP process over.
*/
extern int32_t GSP_Proccess(GSP_CONFIG_INFO_T *pgsp_cfg_info);


/*
func:GSP_GetAddrType
desc:get the address type of GSP can process, virtual addr, or physical
return:
*/
//extern int32_t GSP_GetAddrType(GSP_ADDR_TYPE_E* pType);



/**
 * The id of this module
 */
#define GSP_HARDWARE_MODULE_ID "sprd_gsp"


#ifdef   __cplusplus
//}
#endif
#endif
