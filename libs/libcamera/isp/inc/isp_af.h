/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _ISP_AF_H_
#define _ISP_AF_H_
/*------------------------------------------------------------------------------*
*					Dependencies				*
*-------------------------------------------------------------------------------*/
#include <linux/types.h>
#include "isp_af_alg_v03.h"

/*------------------------------------------------------------------------------*
*					Compiler Flag				*
*-------------------------------------------------------------------------------*/
#ifdef  __cplusplus
extern "C"
{
#endif

/*------------------------------------------------------------------------------*
*					Micro Define				*
*-------------------------------------------------------------------------------*/
#define ISP_AF_END_FLAG 0x80000000
/*------------------------------------------------------------------------------*
*					Data Structures				*
*-------------------------------------------------------------------------------*/

enum isp_af_status{
	ISP_AF_SUCCESS,
	ISP_AF_START,
	ISP_AF_FAIL,
	ISP_AF_CONTINUE,
	ISP_AF_FINISH,
	ISP_AF_STOP,
	ISP_AF_STATUS_MAX,
};

struct isp_awbm_param{
	uint32_t bypass;
	uint8_t ddr_bypass;
	uint8_t avgshf;
	uint16_t res;
	uint16_t s_ptr[10];
	uint16_t e_ptr[10];
	struct isp_pos win_start;
	struct isp_size win_size;
	uint32_t ddr_addr;
};

struct camera_ctn_af_cal_param{
	uint32_t data_type;
	uint32_t *data;
};

enum camera_ctn_af_cal_type {
	CAMERA_CTN_AF_DATA_AE,
	CAMERA_CTN_AF_DATA_AWB,
	CAMERA_CTN_AF_DATA_AF,
	CAMERA_CTN_AF_DATA_MAX

};


struct camera_ctn_af_cal_cfg{
	uint32_t *awb_r_base;
	uint32_t *awb_g_base;
	uint32_t *awb_b_base;
	uint32_t *awb_r_diff;
	uint32_t *awb_g_diff;
	uint32_t *awb_b_diff;
	uint32_t *af_base;
	uint32_t *af_pre;
	uint32_t *af_diff;
	uint32_t *af_diff2;
	uint32_t awb_cal_count;
	uint32_t af_cal_count;
	uint32_t awb_stab_cal_count;
	uint32_t af_stab_cal_count;
	uint32_t awb_cal_value_threshold;
	uint32_t awb_cal_num_threshold;
	uint32_t awb_cal_value_stab_threshold;
	uint32_t awb_cal_num_stab_threshold;
	uint32_t awb_cal_cnt_stab_threshold;
	uint32_t af_cal_threshold;
	uint32_t af_cal_stab_threshold;
	uint32_t af_cal_cnt_stab_threshold;
	uint32_t awb_cal_skip_cnt;
	uint32_t af_cal_skip_cnt;
	uint32_t cal_state;
	uint32_t awb_is_stab;
	uint32_t af_is_stab;
	uint32_t af_cal_need_af;
};



struct isp_af_param{
	uint32_t bypass;
	uint32_t back_bypass;
	uint32_t init;
	enum isp_focus_mode mode;
	uint32_t status;
	uint32_t continue_status;
	uint32_t continue_stat_flag;
	uint32_t monitor_bypass;
	uint32_t have_success_record;
	uint16_t win[9][4];
	uint32_t valid_win;
	uint32_t suc_win;
	uint32_t max_step;
	uint32_t min_step;
	uint32_t cur_step;
	uint32_t stab_period;
	uint32_t set_point;
	uint32_t ae_status;
	uint32_t awb_status;
	uint32_t alg_id;
	uint16_t max_tune_step;
	uint16_t af_rough_step[32];
	uint16_t af_fine_step[32];
	uint8_t rough_count;
	uint8_t fine_count;
	uint8_t awbm_flag;
	uint8_t afm_flag;
	uint32_t awbm_win_w;
	uint32_t awbm_win_h;
	int32_t (*CfgAwbm)(uint32_t handler_id, struct isp_awbm_param* param_ptr);
	int32_t (*AfmEb)(uint32_t handler_id);
	int32_t (*AwbmEb_immediately)(uint32_t handler_id);
	uint32_t(*continue_focus_stat) (uint32_t handler_id, uint32_t param);
	uint32_t(*go_position) (uint32_t param);
	uint32_t default_rough_step_len;
	uint32_t peak_thr_0;
	uint32_t peak_thr_1;
	uint32_t peak_thr_2;
	uint32_t detect_thr;
	uint32_t detect_step_mum;
	uint32_t start_area_range;
	uint32_t end_area_range;
	uint32_t noise_thr;
	uint32_t end_handler_flag;
	struct af_contex_struct_v03 alg_v03_context;
	struct camera_ctn_af_cal_cfg ctn_af_cal_cfg;
};


/*------------------------------------------------------------------------------*
*					Data Prototype				*
*-------------------------------------------------------------------------------*/
uint32_t isp_af_init(uint32_t handler_id);
uint32_t isp_af_deinit(uint32_t handler_id);
uint32_t isp_af_calculation(uint32_t handler_id);
uint32_t isp_af_end(uint32_t handler_id, uint8_t stop_mode);
uint32_t isp_continue_af_calc(uint32_t handler_id);
uint32_t isp_af_get_mode(uint32_t handler_id, uint32_t *mode);
uint32_t isp_af_set_postion(uint32_t handler_id, uint32_t step);
uint32_t isp_af_get_stat_value(uint32_t handler_id, void* param_ptr);

/*------------------------------------------------------------------------------*
*					Compiler Flag				*
*-------------------------------------------------------------------------------*/
#ifdef	 __cplusplus
}
#endif
/*-----------------------------------------------------------------------------*/
#endif
// End
