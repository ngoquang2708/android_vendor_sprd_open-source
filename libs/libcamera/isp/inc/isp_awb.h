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
#ifndef _ISP_AWB_H_
#define _ISP_AWB_H_
/*------------------------------------------------------------------------------*
*				Dependencies					*
*-------------------------------------------------------------------------------*/
#include <linux/types.h>
#include "isp_awb_alg_v01.h"
/*------------------------------------------------------------------------------*
*				Compiler Flag					*
*-------------------------------------------------------------------------------*/
#ifdef __cplusplus
extern "C"
{
#endif
/*------------------------------------------------------------------------------*
*				Micro Define					*
*-------------------------------------------------------------------------------*/
#define ISP_AWB_WEIGHT_TAB_NUM 0x03
#define ISP_AWB_TEMPERATRE_NUM 0x14

#define ISP_AWB_SKIP_FOREVER 0xFFFFFFFF
#define ISP_AWB_TRIM_CONTER 0x0C
#define ISP_AWB_WINDOW_CONTER 0x03
/*------------------------------------------------------------------------------*
*				Data Structures					*
*-------------------------------------------------------------------------------*/
enum isp_awb_wditht{
	ISP_AWB_WDITHT_AVG=0x00,
	ISP_AWB_WDITHT_CENTER,
	ISP_AWB_WDITHT_CUSTOMER,
	ISP_AWB_WDITHT_MAX
};


struct isp_awb_stat{
	uint32_t* r_ptr;
	uint32_t* g_ptr;
	uint32_t* b_ptr;
};

struct isp_awb_cali_info {
	uint32_t r_sum;
	uint32_t b_sum;
	uint32_t gr_sum;
	uint32_t gb_sum;
};

struct isp_awb_coord{
	uint16_t x;
	uint16_t yb;
	uint16_t yt;
};

struct isp_awb_gain{
	uint16_t r;
	uint16_t g;
	uint16_t b;
};

struct isp_awb_rgb{
	uint16_t r;
	uint16_t g;
	uint16_t b;
};

struct isp_awb_estable{
	uint32_t valid;
	uint32_t invalid;
	struct isp_awb_rgb valid_rgb[1024];
	struct isp_awb_rgb invalid_rgb[1024];
};

struct isp_awb_param
{
	uint32_t bypass;
	uint32_t back_bypass;
	uint32_t monitor_bypass;
	uint32_t init;
	uint32_t alg_id;
	struct isp_pos back_monitor_pos;
	struct isp_size back_monitor_size;
	enum isp_alg_mode alg_mode;
	enum isp_awb_mode mode;
	enum isp_awb_wditht weight;
	uint8_t* weight_ptr[ISP_AWB_WEIGHT_TAB_NUM];
	struct isp_awb_coord win[ISP_AWB_TEMPERATRE_NUM];
	struct isp_awb_light_weight light;
	uint32_t steady_speed;
	struct isp_awb_cali_info cali_info;
	struct isp_awb_cali_info golden_info;
	uint8_t weight_tab[2][1024];
	uint8_t weight_id;
	uint8_t target_zone;
	uint8_t cur_index;
	uint8_t prv_index;
	uint8_t gain_index;
	uint8_t matrix_index;
	uint32_t valid_block;
	uint32_t stab_conter;
	struct isp_awb_estable east;
	struct isp_awb_rgb cur_rgb;
	struct isp_awb_gain target_gain;
	struct isp_awb_gain cur_gain;
	struct isp_awb_gain fix_gain[9];
	struct isp_awb_rgb gain_convert[8];
	uint32_t gain_div;
	//uint32_t cur_color_temperature;
	uint32_t cur_T;
	uint32_t target_T;
	uint32_t set_eb;
	uint32_t quick_mode;
	uint32_t smart;
	struct isp_awb_map scanline_map;
	struct isp_awb_wp_count_range wp_count_range;
	struct isp_awb_g_estimate_param g_estimate;
	struct isp_awb_linear_func t_func;
	struct isp_awb_gain_adjust gain_adjust;
	uint32_t alg_handle;
	uint32_t debug_level;
	uint32_t white_point_thres;
	struct isp_awb_alg1_init_param alg1_init_param;
	struct isp_awb_alg1_frame_param alg1_frame_param;
	struct isp_awb_alg1_result alg1_result;
	uint32_t(*continue_focus_stat) (uint32_t handler_id, uint32_t param);
	uint32_t(*mointor_info) (uint32_t handler_id, void* param_ptr);
	uint32_t(*set_monitor_win) (struct isp_pos pos, struct isp_size win_size);
	uint32_t(*recover_monitor_wn) (void* param_ptr);
	int32_t(*set_saturation_offset) (uint32_t handler_id, uint8_t offset);
	int32_t(*set_hue_offset) (uint32_t handler_id, uint8_t offset);
	int32_t(*get_ev_lux) (uint32_t handler_id);
	uint32_t (*GetDefaultGain)(uint32_t handler_id);
	int (*change_param)(uint32_t handler_id, uint32_t cmd);
};

/*------------------------------------------------------------------------------*
*				Data Prototype					*
*-------------------------------------------------------------------------------*/

uint32_t isp_awb_init(uint32_t handler_id);
uint32_t isp_awb_deinit(uint32_t handler_id);
uint32_t isp_awb_calculation(void);
uint32_t isp_awb_set_flash_gain(void);
/*------------------------------------------------------------------------------*
*				Compiler Flag					*
*-------------------------------------------------------------------------------*/
#ifdef __cplusplus
}
#endif
/*------------------------------------------------------------------------------*/
#endif
// End


