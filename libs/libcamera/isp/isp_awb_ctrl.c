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
 #define LOG_TAG "isp_awb_ctrl"

#include "isp_com.h"
#include "isp_log.h"
#include "isp_awb.h"
#include "isp_awb_ctrl.h"
/*------------------------------------------------------------------------------*
*					Compiler Flag				*
*-------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------*
*					Micro Define				*
*-------------------------------------------------------------------------------*/
#define ISP_LOGI

#if 0
int32_t _adjust_sat_hue_by_gain(uint32_t *r_gain, uint32_t *b_gain, uint32_t clr_t, uint32_t r_coef, uint32_t b_coef)
{
	int32_t i, j;

	int32_t rgain = *r_gain;
	int32_t bgain = *b_gain;
	int32_t MAX_RATGAIN = ((r_coef)*2);
	int32_t MAX_BATGAIN =((b_coef)*2);
	//int32_t MAX_RATGAIN = ((128+18)*2);
	//int32_t MAX_BATGAIN =((128-12)*2);

	int32_t START_TC = 33;
	int32_t END_TC = 26;
	int32_t INTERNAL_TC=( END_TC-START_TC);

	int32_t MAX_RK = (256*(MAX_RATGAIN - 256)/INTERNAL_TC);
	int32_t MAX_BK = (256*(MAX_BATGAIN - 256)/INTERNAL_TC);

	clr_t = clr_t / 100;

	if (clr_t  >= START_TC) {
		return 0;
	}

	if(clr_t <= END_TC) {
		clr_t = END_TC;
	}

	*r_gain = rgain * ((clr_t-START_TC)* MAX_RK + 256*256)/256/256;
	*b_gain = bgain * ((clr_t-START_TC)* MAX_BK + 256*256)/256/256;

	return 0;
}
#endif


static void _set_smart_param(uint32_t handler_id, struct isp_awb_param *awb_param, struct smart_light_calc_result *smart_result)
{
	if (0 != smart_result->envi.update)
		awb_param->envi_id = smart_result->envi.envi_id;

	/*the lsc and cmc uset the same index now*/
	if (0 != smart_result->cmc.update || 0 != smart_result->lsc.update) {

		int32_t smart_index = awb_param->cur_index;

		if (smart_result->lsc.index[0] != smart_result->cmc.index[0]
			|| smart_result->lsc.index[1] != smart_result->cmc.index[1]) {

			ISP_LOGI("lsc index = (%d, %d), cmc index = (%d, %d)",
				smart_result->lsc.index[0], smart_result->lsc.index[1],
				smart_result->cmc.index[0], smart_result->cmc.index[1]);
		}

		if (smart_result->lsc.weight[0] > smart_result->lsc.weight[1])
			smart_index = smart_result->lsc.index[0];
		else
			smart_index = smart_result->lsc.index[1];

		if (smart_index >= 0)
			awb_param->cur_index = smart_index;

		ISP_LOGI("awb prv:%d, cur:%d", awb_param->prv_index, awb_param->cur_index);

		if (smart_result->lsc.update) {

			/* adjust lnc param */
			awb_param->change_param(handler_id, ISP_CHANGE_LNC, PNULL);
		}

		if (smart_result->lsc.update) {

			void *param = (void*)smart_result->cmc.weight;
			/* adjust cmc param */
			awb_param->change_param(handler_id, ISP_CHANGE_CMC, (void*)param);
		}

		awb_param->prv_index = awb_param->cur_index;
	}

	if (0 != smart_result->hue_saturation.update) {
		/*set gain to cce module*/
	}

	if (0 != smart_result->gain.update) {
		awb_param->cur_gain.r = smart_result->gain.gain.r;
		awb_param->cur_gain.g = smart_result->gain.gain.g;
		awb_param->cur_gain.b = smart_result->gain.gain.b;
	}
}

int32_t _smart_light_calc(uint32_t handler_id, struct isp_smart_light_param *smart_light_param, 
				struct isp_awb_gain *gain, uint16_t ct, int32_t bright_value)
{
	int32_t rtn = SMART_LIGHT_SUCCESS;
	struct smart_light_calc_param *calc_param = &smart_light_param->calc_param;
	struct smart_light_calc_result *calc_result = &smart_light_param->calc_result;

	memset(calc_result, 0, sizeof(*calc_result));

	if (ISP_UEB == smart_light_param->init) {
		ISP_LOGI("smart have not init");
		return SMART_LIGHT_ERROR;
	}

	if (0 == smart_light_param->smart) {
		ISP_LOGI("smart disable!");
		return SMART_LIGHT_ERROR;
	}

	calc_param->bv = bright_value < 0 ? 0 : bright_value;
	calc_param->smart = smart_light_param->smart;
	calc_param->ct = ct;
	calc_param->gain.r = gain->r;
	calc_param->gain.g = gain->g;
	calc_param->gain.b = gain->b;

	rtn = smart_light_calculation(handler_id, (void *)calc_param, (void *)calc_result);

	return rtn;
}

static void _set_init_param(struct isp_awb_param* awb_param, struct isp_awb_init_param *init_param)
{
	uint32_t i = 0;

	init_param->alg_id = awb_param->alg_id;
	init_param->base_gain = awb_param->base_gain;
	init_param->debug_level = awb_param->debug_level;
	init_param->img_size = awb_param->stat_img_size;
	init_param->win_size = awb_param->win_size;
	init_param->init_ct = awb_param->init_ct;
	init_param->init_gain = awb_param->init_gain;
	init_param->map_data = awb_param->map_data;
	init_param->steady_speed = awb_param->steady_speed;
	init_param->target_zone = awb_param->target_zone;
	init_param->ct_info = awb_param->ct_info;
	init_param->weight_of_count_func = awb_param->weight_of_count_func;
	init_param->weight_of_ct_func = awb_param->weight_of_ct_func;
	init_param->weight_of_pos_lut = awb_param->weight_of_pos_lut;

	init_param->weight_of_ct_func.weight_func.num = 0;

	init_param->scene_adjust_intensity[ISP_AWB_SCENE_GREEN] = awb_param->green_adjust_intensity;
	init_param->scene_adjust_intensity[ISP_AWB_SCENE_SKIN] = awb_param->skin_adjust_intensity;

	memcpy(init_param->value_range, awb_param->value_range,
			sizeof(struct isp_awb_range) * ISP_AWB_ENVI_NUM);

	memcpy(init_param->win, awb_param->win,
			sizeof(struct isp_awb_coord) * ISP_AWB_TEMPERATRE_NUM);

	ISP_LOGI("alg id = %d", init_param->alg_id);
	ISP_LOGI("base_gain = %d", init_param->base_gain);
	ISP_LOGI("img_size = (%d, %d)", init_param->img_size.w, init_param->img_size.h);
	ISP_LOGI("win_size = (%d, %d)", init_param->win_size.w, init_param->win_size.h);
	ISP_LOGI("init ct = %d, init gain=(%d, %d, %d)", init_param->init_ct, init_param->init_gain.r,
				init_param->init_gain.g, init_param->init_gain.b);
	ISP_LOGI("map data = 0x%x, len=%d", (uint32_t)init_param->map_data.addr, init_param->map_data.len);
	ISP_LOGI("steady_speed = %d", init_param->steady_speed);
	ISP_LOGI("target_zone = %d", init_param->target_zone);
	ISP_LOGI("ct_info = (%d, %d, %d, %d)", init_param->ct_info.data[0], init_param->ct_info.data[1],
			init_param->ct_info.data[2], init_param->ct_info.data[3]);
	ISP_LOGI("count func.num = %d, base=%d, 0=(%d, %d), 1=(%d, %d)",
			init_param->weight_of_count_func.weight_func.num,
			init_param->weight_of_count_func.base,
			init_param->weight_of_count_func.weight_func.samples[0].x,
			init_param->weight_of_count_func.weight_func.samples[0].y,
			init_param->weight_of_count_func.weight_func.samples[1].x,
			init_param->weight_of_count_func.weight_func.samples[1].y);

	ISP_LOGI("ct func.num = %d, 0=(%d, %d), 1=(%d, %d)",
			init_param->weight_of_ct_func.weight_func.num,
			init_param->weight_of_ct_func.weight_func.samples[0].x,
			init_param->weight_of_ct_func.weight_func.samples[0].y,
			init_param->weight_of_ct_func.weight_func.samples[1].x,
			init_param->weight_of_ct_func.weight_func.samples[1].y);

	ISP_LOGI("pos lut = 0x%x, w=%d, h=%d",
			(uint32_t)init_param->weight_of_pos_lut.weight, init_param->weight_of_pos_lut.w,
			init_param->weight_of_pos_lut.h);

	for (i=0; i<ISP_AWB_ENVI_NUM; i++) {
		ISP_LOGI("[%d] = (%d, %d)", i, init_param->value_range[i].min,
				init_param->value_range[i].max);
	}

	ISP_LOGI("scene adjust intensity: green = %d, skin=%d", init_param->scene_adjust_intensity[ISP_AWB_SCENE_GREEN],
							init_param->scene_adjust_intensity[ISP_AWB_SCENE_SKIN]);
}

enum isp_awb_envi_id _envi_id_convert(enum smart_light_envi_id envi_id)
{
	switch (envi_id) {
	case SMART_ENVI_COMMON:
		return ISP_AWB_ENVI_COMMON;

	case SMART_ENVI_INDOOR_NORMAL:
		return ISP_AWB_ENVI_INDOOR;

	case SMART_ENVI_LOW_LIGHT:
		return ISP_AWB_ENVI_LOW_LIGHT;

	case SMART_ENVI_OUTDOOR_HIGH:
		return ISP_AWB_ENVI_OUTDOOR_HIGH;

	case SMART_ENVI_OUTDOOR_MIDDLE:
		return ISP_AWB_ENVI_OUTDOOR_MIDDLE;

	case SMART_ENVI_OUTDOOR_NORMAL:
		return ISP_AWB_ENVI_OUTDOOR_LOW;

	default:
		return ISP_AWB_ENVI_COMMON;
	}
}

/* isp_awb_init --
*@
*@
*@ return:
*/
uint32_t isp_awb_ctrl_init(uint32_t handler_id)
{
	uint32_t rtn=ISP_SUCCESS;

	struct isp_context* isp_cxt =ispGetAlgContext(0);
	struct isp_awb_param* awb_param=&isp_cxt->awb;
	struct isp_smart_light_param *smart_light_param = &isp_cxt->smart_light;
	struct isp_awb_init_param *init_param = &awb_param->init_param;
	uint32_t i=0x00;

	ISP_LOGI("init awb gain = (%d, %d, %d)", awb_param->cur_gain.r,
			awb_param->cur_gain.g, awb_param->cur_gain.b);

	isp_cxt->awb_get_stat=ISP_END_FLAG;
	awb_param->monitor_bypass = ISP_UEB;
	awb_param->stab_conter=ISP_ZERO;
	awb_param->alg_mode=ISP_ALG_FAST;
	awb_param->matrix_index=ISP_ZERO;
	awb_param->gain_div=0x100;
	awb_param->win_size = isp_cxt->awbm.win_size;

	/*temp use*/
	awb_param->green_adjust_intensity = 42;
	awb_param->skin_adjust_intensity = 42;

	ISP_LOGI("smart = 0x%d, envi=%d", smart_light_param->smart, awb_param->envi_id);

	/*disable weight of ct function*/
	memset(&awb_param->weight_of_ct_func, 0, sizeof(awb_param->weight_of_ct_func));

	rtn = smart_light_init(handler_id, (void *)&smart_light_param->init_param, NULL);
	if (ISP_SUCCESS == rtn)
		smart_light_param->init = ISP_EB;
	else
		ISP_LOGI("smart_light_init failed!");

	_set_init_param(awb_param, init_param);
	rtn = isp_awb_init(handler_id, init_param, NULL);
	if (ISP_SUCCESS == rtn)
		awb_param->init = ISP_EB;
	else
		ISP_LOGI("isp_awb_init failed!");

	awb_param->init = ISP_EB;

	return rtn;
}

/* isp_awb_deinit --
*@
*@
*@ return:
*/
uint32_t isp_awb_ctrl_deinit(uint32_t handler_id)
{
	uint32_t rtn=ISP_SUCCESS;

	struct isp_context* isp_cxt=ispGetAlgContext(0);
	struct isp_awb_param *awb_param = &isp_cxt->awb;
	struct isp_smart_light_param *smart_light_param = &isp_cxt->smart_light;

	if (ISP_EB == awb_param->init) {
		isp_awb_deinit(handler_id, NULL, NULL);
		awb_param->init=ISP_UEB;
	}

	if (ISP_EB == smart_light_param->init) {
		smart_light_deinit(handler_id, NULL, NULL);
		smart_light_param->init = ISP_UEB;
	}

	/*to avoid gain different after snapshot*/
	awb_param->init_ct = awb_param->cur_ct;
	awb_param->init_gain = awb_param->cur_gain;

	ISP_LOGI("deinit awb gain = (%d, %d, %d)", awb_param->cur_gain.r,
			awb_param->cur_gain.g, awb_param->cur_gain.b);

	return rtn;
}

/* isp_awb_calculation --
*@
*@
*@ return:
*/
uint32_t isp_awb_ctrl_calculation(uint32_t handler_id)
{
	uint32_t rtn=ISP_SUCCESS;
	struct isp_context* isp_cxt = ispGetAlgContext(0);
	struct isp_ae_param *ae_param = &isp_cxt->ae;
	struct isp_awb_param* awb_param = &isp_cxt->awb;
	struct isp_smart_light_param *smart_light_param = &isp_cxt->smart_light;
	struct isp_awb_calc_param *calc_param = &awb_param->calc_param;
	struct isp_awb_calc_result *calc_result = &awb_param->calc_result;
	struct isp_awb_gain gain = {0, 0 , 0};
	struct isp_awb_result awb_result = {{0, 0, 0}, 0};
	uint32_t setting_index = awb_param->cur_setting_index;
	uint16_t ct = 0;
	uint32_t bv = 0;

	if(ISP_EB==isp_cxt->awb_get_stat)
		isp_cxt->cfg.callback(handler_id, ISP_CALLBACK_EVT|ISP_AWB_STAT_CALLBACK,
					(void*)&isp_cxt->awb_stat, sizeof(struct isp_awb_statistic_info));

	if ((ISP_END_FLAG!=isp_cxt->af.continue_status)
		&&(PNULL!=awb_param->continue_focus_stat)) {
		awb_param->continue_focus_stat(handler_id, ISP_AWB_STAT_FLAG);
	}

	if(ISP_EB == awb_param->bypass)
		goto EXIT;

	if (ISP_UEB == awb_param->init) {
		ISP_LOGI("awb have not init");
		goto EXIT;
	}

	calc_param->awb_stat = &isp_cxt->awb_stat;
	calc_param->envi_id = _envi_id_convert(awb_param->envi_id);

	rtn = isp_awb_calculation(handler_id, calc_param, calc_result);
	gain = calc_result->gain;
	ct = calc_result->ct;

	ISP_LOGI("gain = (%d, %d, %d), ct=%d, rtn=%d", gain.r, gain.g, gain.b, ct, rtn);

	/* update ccm and lsc parameters */
	if (ISP_SUCCESS == rtn && gain.r > 0 && gain.b > 0 && gain.g > 0) {

		struct isp_awb_gain last_gain = awb_param->cur_gain;
		int32_t bright_value = 0;

		awb_param->cur_gain = gain;
		awb_param->cur_ct = ct;

		/*disable smart for alg_id 0*/
		if (0 == awb_param->alg_id)
			smart_light_param->calc_param.smart = 0;

		bright_value = awb_param->get_ev_lux(handler_id);
		rtn = _smart_light_calc(handler_id, smart_light_param, &gain, ct, bright_value);
		if (ISP_SUCCESS == rtn)
			_set_smart_param(handler_id, awb_param, &smart_light_param->calc_result);

		if(awb_param->cur_gain.r != last_gain.r || awb_param->cur_gain.g != last_gain.g
			|| awb_param->cur_gain.b != last_gain.b) {

			awb_param->set_eb=ISP_EB;
			awb_param->stab_conter=ISP_ZERO;

			ISP_LOGI("cur gain=(%d, %d, %d), last gain=(%d, %d, %d)",
				awb_param->cur_gain.r, awb_param->cur_gain.g, awb_param->cur_gain.b,
				last_gain.r, last_gain.g, last_gain.b);
		} else {

			awb_param->set_eb = ISP_UEB;
			awb_param->monitor_bypass = ISP_UEB;
			awb_param->stab_conter++;
		}
	}

	ISP_LOGI("calc awb gain = (%d, %d, %d)", awb_param->cur_gain.r,
			awb_param->cur_gain.g, awb_param->cur_gain.b);

EXIT:

	return rtn;
}

/* isp_awb_set_flash_gain --
*@
*@
*@ return:
*/
uint32_t isp_awb_set_flash_gain(void)
{
	uint32_t rtn=ISP_SUCCESS;
	uint32_t handler_id=0x00;
	struct isp_context* isp_context_ptr=ispGetAlgContext(handler_id);
	struct isp_awbc_param* awbc_ptr=(struct isp_awbc_param*)&isp_context_ptr->awbc;
	struct isp_awb_param* awb_param_ptr=&isp_context_ptr->awb;
	struct isp_flash_param* flash_param_ptr=&isp_context_ptr->ae.flash;
	uint32_t r_gain=0x00;
	uint32_t g_gain=0x00;
	uint32_t b_gain=0x00;
	uint32_t base_gain=awb_param_ptr->GetDefaultGain(handler_id);

	if(ISP_EB==flash_param_ptr->set_awb)
	{
		if((ISP_ZERO==flash_param_ptr->r_ratio)
			||(ISP_ZERO==flash_param_ptr->g_ratio)
			||(ISP_ZERO==flash_param_ptr->b_ratio))
		{
			ISP_LOG("ratio: 0x%x, 0x%x, 0x%x error\n", flash_param_ptr->r_ratio, flash_param_ptr->g_ratio, flash_param_ptr->b_ratio);
		}
		else
		{
			r_gain=flash_param_ptr->r_ratio;
			g_gain=flash_param_ptr->g_ratio;
			b_gain=flash_param_ptr->b_ratio;
			awb_param_ptr->cur_gain.r=((awb_param_ptr->cur_gain.r*(1024-flash_param_ptr->effect))+r_gain*flash_param_ptr->effect)>>0x0a;
			awb_param_ptr->cur_gain.g=((awb_param_ptr->cur_gain.g*(1024-flash_param_ptr->effect))+g_gain*flash_param_ptr->effect)>>0x0a;
			awb_param_ptr->cur_gain.b=((awb_param_ptr->cur_gain.b*(1024-flash_param_ptr->effect))+b_gain*flash_param_ptr->effect)>>0x0a;
			awbc_ptr->r_gain=awb_param_ptr->cur_gain.r;
			awbc_ptr->g_gain=awb_param_ptr->cur_gain.g;
			awbc_ptr->b_gain=awb_param_ptr->cur_gain.b;
		}
	}

	flash_param_ptr->set_awb=ISP_UEB;

	return rtn;
}

/*------------------------------------------------------------------------------*
*					Compiler Flag				*
*-------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------*/
