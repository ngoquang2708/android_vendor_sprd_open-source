/*
 * Copyright (C) 2012 The Android Open Source Project
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
#ifndef _ISP_CTRL_H_
#define _ISP_CTRL_H_
/*----------------------------------------------------------------------------*
**				Dependencies					*
**---------------------------------------------------------------------------*/
#include <sys/types.h>
#include "isp_app.h"
/**---------------------------------------------------------------------------*
**				Micro Define					*
**----------------------------------------------------------------------------*/

/**---------------------------------------------------------------------------*
**				Data Prototype					*
**----------------------------------------------------------------------------*/

int isp_ctrl_init(struct isp_init_param* ptr);
int isp_ctrl_deinit(void);
int isp_ctrl_capability(enum isp_capbility_cmd cmd, void* param_ptr);
int isp_ctrl_ioctl(enum isp_ctrl_cmd cmd, void* param_ptr);
int isp_ctrl_video_start(struct isp_video_start* param_ptr);
int isp_ctrl_video_stop(void);
int isp_ctrl_proc_start(struct ips_in_param* in_param_ptr, struct ips_out_param* out_param_ptr);
int isp_ctrl_proc_next(struct ipn_in_param* in_ptr, struct ips_out_param *out_ptr);

/**---------------------------------------------------------------------------*/
#endif
// End

