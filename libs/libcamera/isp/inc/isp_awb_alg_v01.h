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
#ifndef _ISP_AWB_ALG_V01_H_
#define _ISP_AWB_ALG_V01_H_
/*------------------------------------------------------------------------------*
*				Dependencies					*
*-------------------------------------------------------------------------------*/
#include <linux/types.h>
#include <sys/types.h>
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
#define ISP_AWB_G_ESTIMATE_NUM		0x6
#define ISP_AWB_GAIN_ADJUST_NUM		0x9
#define ISP_AWB_LIGHT_NUM		0x10
/*------------------------------------------------------------------------------*
*				Data Structures					*
*-------------------------------------------------------------------------------*/
enum isp_awb_alg_mode
{
	ALG_MODE_BASIC = 0,
	ALG_MODE_OUTDOOR_DETECTION = 1,
	ALG_MODE_RECHECK = 2,
	ALG_MODE_LOW_LIGHT_DETECTION = 4
};

struct isp_awb_light_weight
{
	uint16_t	t_thr[ISP_AWB_LIGHT_NUM];
	uint16_t	w_thr[ISP_AWB_LIGHT_NUM];
	uint16_t	num;
};

struct isp_awb_g_estimate_param
{
	uint16_t t_thr[ISP_AWB_G_ESTIMATE_NUM];
	uint16_t g_thr[ISP_AWB_G_ESTIMATE_NUM][2];
	uint16_t w_thr[ISP_AWB_G_ESTIMATE_NUM][2];
	uint32_t num;
};

struct isp_awb_linear_func
{
	int32_t a;
	int32_t b;
	uint32_t shift;
};

struct isp_awb_wp_count_range
{
	uint16_t min_proportion;//min_proportion / 256
	uint16_t max_proportion;//max_proportion / 256
};

struct isp_awb_gain_adjust
{
	uint16_t t_thr[ISP_AWB_GAIN_ADJUST_NUM];
	uint16_t w_thr[ISP_AWB_GAIN_ADJUST_NUM];
	uint32_t num;
};

struct isp_awb_map{
	uint16_t *addr;
	uint32_t len;		//by bytes
};

struct isp_awb_rect
{
	uint16_t	x;
	uint16_t	y;
	uint16_t	w;
	uint16_t	h;
};

struct isp_awb_size
{
	uint16_t	w;
	uint16_t	h;
};

struct isp_awb_pos
{
	uint16_t	x;
	uint16_t	y;
};

struct isp_awb_stat_img_info
{
	uint32_t	*r_data;
	uint32_t	*g_data;
	uint32_t	*b_data;

	struct isp_awb_pos win_pos;
	struct isp_awb_size win_size;
	struct isp_awb_size win_num;
};

struct isp_awb_alg1_init_param
{
	struct isp_awb_map 			awb_map;
	struct isp_awb_g_estimate_param		g_estimate;
	struct isp_awb_linear_func		t_func;
	struct isp_awb_wp_count_range		wp_count_range;
	struct isp_awb_gain_adjust		gain_adjust;
	struct isp_awb_light_weight		light;
	uint16_t				steady_speed;
	uint16_t				debug_level;
	uint16_t				wp_thres;
	uint16_t				indoor_ev_range[2];
	uint16_t				outdoor_ev_range[2];
	uint16_t				low_light_ev_range[2];
	uint16_t				base_gain;
	struct isp_awb_size			max_win_num;
	uint32_t				mode;	//default: 0
	uint32_t				max_ev;
};

struct isp_awb_alg1_frame_param
{
	struct isp_awb_stat_img_info 	stat_img_info;

	uint32_t	ev;			//index of ev
	uint32_t	lnc;			//index of lnc

	uint8_t		recheck;		//whether to do recheck
};

struct isp_awb_alg1_result
{
	uint32_t	r_gain;
	uint32_t	g_gain;
	uint32_t	b_gain;
	uint32_t	T;
	struct isp_awb_pos win_pos;
	struct isp_awb_size win_size;
	struct isp_awb_size win_num;
	uint8_t		recheck;		//whether need to recheck the result
};


/*------------------------------------------------------------------------------*
*				Data Prototype					*
*-------------------------------------------------------------------------------*/



/*------------------------------------------------------------------------------*
*				Functions						*
*-------------------------------------------------------------------------------*/
uint32_t ISP_AWBInitAlg1(uint32_t handler_id, struct isp_awb_alg1_init_param *init_param);
void ISP_AWBDeinitAlg1(uint32_t handler_id, uint32_t handle);
uint32_t ISP_AWBCalcGainAlg1(uint32_t handler_id, uint32_t handle, struct isp_awb_alg1_frame_param *frame_param,
				struct isp_awb_alg1_result *result);
/*------------------------------------------------------------------------------*
*				Compiler Flag					*
*-------------------------------------------------------------------------------*/
#ifdef __cplusplus
}
#endif
/*------------------------------------------------------------------------------*/
#endif
// End


