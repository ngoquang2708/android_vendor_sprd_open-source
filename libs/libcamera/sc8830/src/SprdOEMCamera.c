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
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <math.h>
#include "SprdOEMCamera.h"
#include "cmr_oem.h"
#include "sprd_rot_k.h"
#include "isp_video.h"
static int camera_capture_need_exit(void);

struct camera_context        cmr_cxt;
struct camera_context        *g_cxt = &cmr_cxt;
#define IS_PREVIEW           (CMR_PREVIEW == g_cxt->preview_status)
#define IS_CAPTURE           (CMR_CAPTURE == g_cxt->capture_status || CMR_CAPTURE_SLICE == g_cxt->capture_status)
#define IS_PREV_FRM(id)      ((id & CAMERA_PREV_ID_BASE) == CAMERA_PREV_ID_BASE)
#define IS_CAP0_FRM(id)      ((id & CAMERA_CAP0_ID_BASE) == CAMERA_CAP0_ID_BASE)
#define IS_CAP1_FRM(id)      ((id & CAMERA_CAP1_ID_BASE) == CAMERA_CAP1_ID_BASE)
#define IS_CAP_FRM(id)       (IS_CAP0_FRM(id) || IS_CAP1_FRM(id))
#define YUV_NO_SCALING       ((ZOOM_BY_CAP == g_cxt->cap_zoom_mode) && \
				(g_cxt->cap_orig_size.width == g_cxt->picture_size.width) && \
				(g_cxt->cap_orig_size.height == g_cxt->picture_size.height))
#define RAW_NO_SCALING       ((ZOOM_POST_PROCESS == g_cxt->cap_zoom_mode) && \
				0 == g_cxt->zoom_level && \
				(g_cxt->cap_orig_size.width == g_cxt->picture_size.width) && \
				(g_cxt->cap_orig_size.height == g_cxt->picture_size.height))
#define TAKE_PIC_CANCEL      do {                                                  \
									if (camera_capture_need_exit()) {               \
										camera_takepic_done(g_cxt);                 \
										g_cxt->jpeg_cxt.jpeg_state = JPEG_IDLE;     \
										CMR_LOGE("exit.");                          \
										return CAMERA_SUCCESS;                      \
									}                                               \
								} while (0)
#define TAKE_PIC_CANCEL_NORETURNVALUE      do {                                                     \
													if (camera_capture_need_exit()) {               \
														camera_takepic_done(g_cxt);                 \
														g_cxt->jpeg_cxt.jpeg_state = JPEG_IDLE;     \
														CMR_LOGE("exit.");                          \
														return;                      \
													}                                               \
												} while (0)

#define NO_SCALING           (YUV_NO_SCALING || RAW_NO_SCALING)
#define IMAGE_FORMAT         "YVU420_SEMIPLANAR"
#define SET_CHN_IDLE(x)      do {g_cxt->v4l2_cxt.chn_status[x] = CHN_IDLE;} while (0)
#define SET_CHN_BUSY(x)      do {g_cxt->v4l2_cxt.chn_status[x] = CHN_BUSY;} while (0)
#define IS_CHN_IDLE(x)       (g_cxt->v4l2_cxt.chn_status[x] == CHN_IDLE)
#define IS_CHN_BUSY(x)       (g_cxt->v4l2_cxt.chn_status[x] == CHN_BUSY)
#define IS_ZSL_MODE(x)       ((CAMERA_ZSL_MODE == x) || (CAMERA_ZSL_CONTINUE_SHOT_MODE == x))
#define IS_NON_ZSL_MODE(x)   ((CAMERA_ZSL_MODE != x) && (CAMERA_ZSL_CONTINUE_SHOT_MODE != x))
#define IS_NO_MALLOC_MEM   ((CAMERA_HDR_MODE == g_cxt->cap_mode) && ((1 == g_cxt->cap_cnt)||(2 == g_cxt->cap_cnt)))
#define IS_WAIT_FOR_NORMAL_CONTINUE(x,y)  (((x) == CAMERA_NORMAL_CONTINUE_SHOT_MODE)&&((y) != g_cxt->total_capture_num))
#define USE_SENSOR_OFF_ON_FOR_HDR    1

#define	bzero(b, len)		memset((b), '\0', (len))
#define       FREE_PMEM_BAK_OEM               1

//camera_takepic_step timestamp
enum CAMERA_TAKEPIC_STEP {
		CMR_STEP_TAKE_PIC = 0,
		CMR_STEP_CAP_S,
		CMR_STEP_CAP_E,
		CMR_STEP_ISP_PP_S,
		CMR_STEP_ISP_PP_E,
		CMR_STEP_JPG_DEC_S,
		CMR_STEP_JPG_DEC_E,
		CMR_STEP_SC_S,
		CMR_STEP_SC_E,
		CMR_STEP_JPG_ENC_S,
		CMR_STEP_JPG_ENC_E,
		CMR_STEP_THUM_ENC_S,
		CMR_STEP_THUM_ENC_E,
		CMR_STEP_WR_EXIF_S,
		CMR_STEP_WR_EXIF_E,
		CMR_STEP_CALL_BACK,
		CMR_STEP_MAX
};

struct CAMERA_TAKEPIC_STAT {
		char              step_name[20];
		nsecs_t          timestamp;
		uint32_t         valid;
};
struct CAMERA_TAKEPIC_STAT cap_stp[CMR_STEP_MAX] ={
		{"takepicture",           0, 0},
		{"capture start",        0, 0},
		{"capture end",          0, 0},
		{"isp pp start",           0, 0},
		{"isp pp end",             0, 0},
		{"jpeg dec start",        0, 0},
		{"jpeg dec end",         0, 0},
		{"scaling start",          0, 0},
		{"scaling end",           0,  0},
		{"jpeg enc start",       0,  0},
		{"jpeg enc end",         0,  0},
		{"thumb enc start",     0,  0},
		{"thumb enc end",       0,  0},
		{"write exif start",       0,  0},
		{"write exif end",         0,  0},
		{"call back",               0,  0},
};

#define TAKE_PICTURE_STEP(a)  do {                                                  \
		cap_stp[a].timestamp = systemTime(CLOCK_MONOTONIC); \
		cap_stp[a].valid = 1;                                                 \
	} while (0)

static void camera_sensor_evt_cb(int evt, void* data);
static int  camera_isp_evt_cb(int evt, void* data);
static void camera_jpeg_evt_cb(int evt, void* data);
static void camera_v4l2_evt_cb(int evt, void* data);
static void camera_rot_evt_cb(int evt, void* data);
static void camera_scaler_evt_cb(int evt, void* data);
static int  camera_create_main_thread(int32_t camera_id);
static int  camera_destroy_main_thread(void);
static int  camera_get_sensor_preview_mode(struct img_size* target_size, uint32_t *work_mode);
static int  camera_get_sensor_capture_mode(struct img_size* target_size, uint32_t *work_mode);
static int  camera_preview_init(int format_mode);
static void camera_set_client_data(void* user_data);
static void camera_set_hal_cb(camera_cb_f_type cmr_cb);
static void camera_set_af_cb(camera_cb_f_type cmr_cb);
static void camera_call_af_cb(camera_cb_f_type cmr_cb,
			camera_cb_type cb,
			const void *client_data,
			camera_func_type func,
			int32_t parm4);
static int  camera_capture_init(void);
static void *camera_main_routine(void *client_data);
static int  camera_internal_handle(uint32_t evt_type, uint32_t sub_type, struct frm_info *data);
static int  camera_v4l2_handle(uint32_t evt_type, uint32_t sub_type, struct frm_info *data);
static int  camera_isp_handle(uint32_t evt_type, uint32_t sub_type, void *data);
static int  camera_jpeg_codec_handle(uint32_t evt_type, uint32_t sub_type, void *data);
static int  camera_scale_handle(uint32_t evt_type, uint32_t sub_type, struct img_frm *data);
static int  camera_rotation_handle(uint32_t evt_type, uint32_t sub_type, struct img_frm *data);
static int  camera_sensor_handle(uint32_t evt_type, uint32_t sub_type, struct frm_info *data);
static int  camera_img_cvt_handle(uint32_t evt_type, uint32_t sub_type, struct img_frm *data);
static void *camera_af_thread_proc(void *data);
static int  camera_v4l2_preview_handle(struct frm_info *data);
static int  camera_v4l2_capture_handle(struct frm_info *data);
static int  camera_alloc_preview_buf(struct buffer_cfg *buffer, uint32_t format);
static int  camera_capture_ability(SENSOR_MODE_INFO_T *sn_mode,
			struct img_frm_cap *img_cap,
			struct img_size *cap_size);
static int  camera_alloc_capture_buf(struct buffer_cfg *buffer, uint32_t cap_number, uint32_t ch_id);
static int  camera_start_isp_process(struct frm_info *data);
static int  camera_start_jpeg_decode(struct frm_info *data);
static int  camera_start_jpeg_encode(struct frm_info *data);
static int camera_jpeg_decode_next(struct frm_info *data);
static int  camera_start_scale(struct frm_info *data);
static int  camera_scale_next(struct frm_info *data);
static int  camera_scale_done(struct frm_info *data);
static int  camera_start_rotate(struct frm_info *data);
static int  camera_start_preview_internal(void);
static int  camera_stop_preview_internal(void);
static int  camera_before_set(enum restart_mode re_mode);
static int  camera_after_set(enum restart_mode re_mode,
			enum img_skip_mode skip_mode,
			uint32_t skip_number);
static int camera_jpeg_encode_done(uint32_t thumb_stream_size);
static int camera_jpeg_encode_handle(JPEG_ENC_CB_PARAM_T *data);
static int camera_jpeg_decode_handle(JPEG_DEC_CB_PARAM_T *data);
static int camera_set_frame_type(camera_frame_type *frame_type, struct frm_info* info);
static int camera_capture_yuv_process(struct frm_info *data);
static int camera_init_internal(uint32_t camera_id);
static int camera_stop_internal(void);
static int camera_flush_msg_queue(void);
static int camera_preview_err_handle(uint32_t evt_type);
static int camera_capture_err_handle(uint32_t evt_type);
static int camera_jpeg_encode_thumb(uint32_t *stream_size_ptr);
static int camera_convert_to_thumb(void);
static int camera_isp_proc_handle(struct ips_out_param *isp_out);
static int camera_af_init(void);
static int camera_af_deinit(void);
static int camera_uv422_to_uv420(uint32_t dst, uint32_t src, uint32_t width, uint32_t height);
static int camera_set_cancel_capture(int set_val);
static int camera_prev_thread_init(void);
static int camera_prev_thread_deinit(void);
static void *camera_prev_thread_proc(void *data);
static int camera_prev_thread_handle();
static int camera_prev_thread_rot_handle(uint32_t evt_type, uint32_t sub_type, struct img_frm * data);
static int camera_preview_weak_init(int format_mode);
static int camera_cap_thread_init(void);
static int camera_cap_thread_deinit(void);
static void *camera_cap_thread_proc(void *data);
static int camera_cap_subthread_init(void);
static int camera_cap_subthread_deinit(void);
static void *camera_cap_subthread_proc(void * data);
static void camera_capture_hdr_data(struct frm_info *data);
static int camera_take_picture_hdr(int cap_cnt);
static int camera_jpeg_specify_notify_done(int state);
static int camera_jpeg_specify_wait_done(void);
static int camera_is_jpeg_specify_process(int state);
static int camera_get_take_picture(void);
static int camera_set_take_picture(int set_val);
static int camera_capture_init_raw(void);
static void camera_start_convert_thum(void);
static int camera_capture_get_max_size(SENSOR_MODE_INFO_T *sn_mode, uint32_t *io_width, uint32_t *io_height);
static int camera_cb_thread_init(void);
static int camera_cb_thread_deinit(void);
static void *camera_cb_thread_proc(void *data);
static void camera_callback_handle(camera_cb_type cb, camera_func_type func, int32_t cb_param);
static void camera_capture_step_statisic(void);
static int camera_capture_init_continue(void);
static void _camera_autofocus_stop_handle(void);
static int camera_take_picture_continue(int cap_cnt);
static int camera_is_jpeg_encode_direct_process(void);
static int camera_post_convert_thum_msg(void);

camera_ret_code_type camera_encode_picture(camera_frame_type *frame,
					camera_handle_type *handle,
					camera_cb_f_type callback,
					void *client_data)
{
	int                      ret = CAMERA_SUCCESS;

	CMR_LOGV("w h %d %d", frame->dx, frame->dy);

	return ret;
}

int camera_sensor_init(int32_t camera_id)
{
	struct camera_ctrl       *ctrl = &g_cxt->control;
	struct sensor_context    *sensor_cxt = &g_cxt->sn_cxt;
	uint32_t                 sensor_num;
	int                      ret = CAMERA_SUCCESS;
	int sensor_ret = SENSOR_FAIL;

	if (ctrl->sensor_inited) {
		CMR_LOGI("sensor intialized before");
		goto exit;
	}

	/*camera_set_sensormark();*/
	sensor_ret = Sensor_Init(camera_id, &sensor_num);
	if (SENSOR_SUCCESS != sensor_ret) {
		CMR_LOGE("No sensor %d", sensor_num);
		ret = -CAMERA_NO_SENSOR;
		goto exit;
	} else {
		if ((uint32_t)camera_id >= sensor_num) {
			CMR_LOGE("No sensor %d", sensor_num);
			ret = -CAMERA_NO_SENSOR;
			goto exit;
		}
		/*
		sensor_ret = Sensor_Open(camera_id);
		if (sensor_ret) {
			CMR_LOGE("Failed to open %d sensor", camera_id);
			ret = -CAMERA_NO_SENSOR;
			goto exit;
		}
		*/
		sensor_cxt->cur_id = camera_id;
		sensor_cxt->sensor_info = Sensor_GetInfo();
		if (NULL == sensor_cxt->sensor_info) {
			CMR_LOGE("Failed to Get sensor info");
			ret = -CAMERA_NOT_SUPPORTED;
			goto sensor_exit;
		}
		if (SENSOR_IMAGE_FORMAT_RAW == sensor_cxt->sensor_info->image_format) {
			sensor_cxt->sn_if.img_fmt = V4L2_SENSOR_FORMAT_RAWRGB;
			CMR_LOGV("It's RawRGB Sensor, %d", sensor_cxt->sn_if.img_fmt);
			ret = Sensor_GetRawSettings(&sensor_cxt->raw_settings, &sensor_cxt->setting_length);
			if (ret) {
				CMR_LOGE("Failed to Get sensor raw settings");
				ret = -CAMERA_NOT_SUPPORTED;
				goto sensor_exit;
			}
		} else {
			sensor_cxt->sn_if.img_fmt = V4L2_SENSOR_FORMAT_YUV;
			CMR_LOGV("It's YUV Sensor, %d", sensor_cxt->sn_if.img_fmt);
		}

		camera_sensor_inf(sensor_cxt);

		Sensor_EventReg(camera_sensor_evt_cb);
		ctrl->sensor_inited = 1;
		goto exit;
	}

sensor_exit:
	Sensor_Close();
exit:

	/*camera_save_sensormark();*/
	return ret;
}

int camera_sensor_deinit(void)
{
	struct camera_ctrl       *ctrl = &g_cxt->control;
	struct sensor_context    *sensor_cxt = &g_cxt->sn_cxt;
	uint32_t                 sensor_ret;
	int                      ret = CAMERA_SUCCESS;

	if (0 == ctrl->sensor_inited) {
		CMR_LOGI("sensor has been de-intialized");
		goto exit;
	}
	Sensor_EventReg(NULL);
	ret = Sensor_Close();
	bzero(sensor_cxt, sizeof(*sensor_cxt));
	ctrl->sensor_inited = 0;

exit:
	return ret;
}

int camera_v4l2_init(void)
{
	struct camera_ctrl       *ctrl = &g_cxt->control;
	struct v4l2_context      *cxt  = &g_cxt->v4l2_cxt;
	int                      i, ret   = CAMERA_SUCCESS;

	if (0 == ctrl->v4l2_inited) {
		ret = cmr_v4l2_init();
		if (ret) {
			CMR_LOGE("Failed to init v4l2 %d", ret);
			ret = -CAMERA_NOT_SUPPORTED;
			goto exit;
		}

		ret = cmr_v4l2_scale_capability(&cxt->sc_capability, &cxt->sc_factor);
		if (ret) {
			CMR_LOGE("Failed to get v4l2 frame scaling capability %d", ret);
			ret = -CAMERA_NOT_SUPPORTED;
			goto exit;
		}
		cmr_v4l2_evt_reg(camera_v4l2_evt_cb);
		cxt->v4l2_state = V4L2_IDLE;
		ctrl->v4l2_inited = 1;
		for (i = 0; i < CHN_MAX; i++) {
			cxt->chn_status[i] = CHN_IDLE;
			cxt->chn_frm_deci[i] = 0;
		}

	}

exit:
	return ret;
}

int camera_v4l2_deinit(void)
{
	struct camera_ctrl       *ctrl = &g_cxt->control;
	struct v4l2_context      *cxt  = &g_cxt->v4l2_cxt;
	int                      ret   = CAMERA_SUCCESS;

	if (0 == ctrl->v4l2_inited) {
		CMR_LOGI("V4L2 has been de-intialized");
		goto exit;
	}
	ret = Sensor_StreamOff();
	if (ret) {
		CMR_LOGE("Failed to switch off the sensor stream, %d", ret);
	}
	ret = cmr_v4l2_if_decfg(&g_cxt->sn_cxt.sn_if);
	if (ret) {
		CMR_LOGE("close mipi IF fail.");
	}

	ret = cmr_v4l2_deinit();
	if (ret) {
		CMR_LOGE("Failed to init v4l2 %d", ret);
		ret = -CAMERA_NOT_SUPPORTED;
		goto exit;
	}
	bzero(cxt, sizeof(*cxt));
	ctrl->v4l2_inited = 0;

exit:
	return ret;
}

int camera_isp_init(void)
{
	struct camera_ctrl       *ctrl = &g_cxt->control;
	struct isp_context       *cxt = &g_cxt->isp_cxt;
	struct isp_init_param    isp_param;
	int                      ret = CAMERA_SUCCESS;
	struct isp_video_limit   isp_limit;
	SENSOR_EXP_INFO_T		 *sensor_info_ptr;

	CMR_PRINT_TIME;

	if (0 == ctrl->sensor_inited || V4L2_SENSOR_FORMAT_RAWRGB != g_cxt->sn_cxt.sn_if.img_fmt) {
		CMR_LOGI("No need to init ISP %d %d", ctrl->sensor_inited, g_cxt->sn_cxt.sn_if.img_fmt);
		goto exit;
	}

	if (0 == ctrl->isp_inited) {
		isp_param.isp_id = ISP_ID_SC8830;
		sensor_info_ptr = g_cxt->sn_cxt.sensor_info;
		isp_param.setting_param_ptr = sensor_info_ptr;
		if (0 != sensor_info_ptr->sensor_mode_info[SENSOR_MODE_COMMON_INIT].width) {
			isp_param.size.w = sensor_info_ptr->sensor_mode_info[SENSOR_MODE_COMMON_INIT].width;
			isp_param.size.h = sensor_info_ptr->sensor_mode_info[SENSOR_MODE_COMMON_INIT].height;
		} else {
			isp_param.size.w = sensor_info_ptr->sensor_mode_info[SENSOR_MODE_PREVIEW_ONE].width;
			isp_param.size.h = sensor_info_ptr->sensor_mode_info[SENSOR_MODE_PREVIEW_ONE].height;
		}
		isp_param.ctrl_callback = camera_isp_evt_cb;
		ret = isp_init(&isp_param);
		if (ret) {
			CMR_LOGE("Failed to init ISP %d", ret);
			ret = -CAMERA_NOT_SUPPORTED;
			goto exit;
		}
		ret = isp_capability(ISP_VIDEO_SIZE, &isp_limit);
		if (ret) {
			CMR_LOGE("Failed to get the limitation of ISP %d", ret);
			ret = -CAMERA_NOT_SUPPORTED;
			goto exit;
		}
		cxt->width_limit = isp_limit.width;
		cxt->isp_state = ISP_IDLE;
		ctrl->isp_inited = 1;

	}

exit:
	return ret;
}

int camera_isp_deinit(void)
{
	struct camera_ctrl       *ctrl = &g_cxt->control;
	struct isp_context       *isp_cxt = &g_cxt->isp_cxt;
	int                      ret = CAMERA_SUCCESS;

	if (0 == ctrl->isp_inited) {
		CMR_LOGI("V4L2 has been de-intialized");
		goto exit;
	}
	/*cmr_isp_evt_reg(NULL);*/
	ret = isp_deinit();
	if (ret) {
		CMR_LOGE("Failed to de-init ISP %d", ret);
		ret = -CAMERA_NOT_SUPPORTED;
		goto exit;
	}
	bzero(isp_cxt, sizeof(*isp_cxt));
	ctrl->isp_inited = 0;

exit:
	return ret;
}

int camera_jpeg_init(void)
{
	struct camera_ctrl       *ctrl = &g_cxt->control;
	struct jpeg_context      *cxt  = &g_cxt->jpeg_cxt;
	int                      ret   = CAMERA_SUCCESS;

	if (0 == ctrl->jpeg_inited) {
		ret = jpeg_init();
		if (CAMERA_SUCCESS == ret) {
			jpeg_evt_reg(camera_jpeg_evt_cb);
			cxt->jpeg_state = JPEG_IDLE;
			ctrl->jpeg_inited = 1;
		} else {
			CMR_LOGE("Failed to init JPEG Codec %d", ret);
			ret = -CAMERA_NOT_SUPPORTED;
		}
	}

	return ret;
}

int camera_jpeg_deinit(void)
{
	struct camera_ctrl       *ctrl = &g_cxt->control;
	struct jpeg_context      *cxt  = &g_cxt->jpeg_cxt;
	int                      ret   = CAMERA_SUCCESS;

	if (0 == ctrl->jpeg_inited) {
		CMR_LOGI("JPEG Codec has been de-intialized");
		goto exit;
	}

	do
	{
		usleep(200000);
	}while((JPEG_IDLE != g_cxt->jpeg_cxt.jpeg_state)&&(JPEG_ERR != g_cxt->jpeg_cxt.jpeg_state));
	jpeg_evt_reg(NULL);
	ret = jpeg_deinit();
	if (ret) {
		CMR_LOGE("Failed to de-init JPEG Codec %d", ret);
		ret = -CAMERA_NOT_SUPPORTED;
		goto exit;
	}
	bzero(cxt, sizeof(*cxt));
	ctrl->jpeg_inited = 0;

exit:
	return ret;
}

int camera_dma_copy_init(void)
{
	struct camera_ctrl       *ctrl = &g_cxt->control;
	int                      ret   = CAMERA_SUCCESS;

	if (1 == ctrl->dma_copy_inited) {
		CMR_LOGI("dma copy has been intialized");
		goto exit;
	}

	ret = cmr_dma_copy_init();
	if (ret) {
		CMR_LOGE("Failed to init dma copy %d", ret);
		ret = -CAMERA_NOT_SUPPORTED;
	} else {
		ctrl->dma_copy_inited = 1;
	}

exit:
	return ret;
}

int camera_dma_copy_deinit(void)
{
	struct camera_ctrl       *ctrl = &g_cxt->control;
	int                      ret   = CAMERA_SUCCESS;

	if (0 == ctrl->dma_copy_inited) {
		CMR_LOGI("dma copy has been de-intialized");
		goto exit;
	}

	ret = cmr_dma_copy_deinit();
	if (ret) {
		CMR_LOGE("Failed to de-init dma copy %d", ret);
		ret = -CAMERA_NOT_SUPPORTED;
		goto exit;
	}

	ctrl->dma_copy_inited = 0;

exit:
	return ret;
}

int camera_dma_copy_data(struct _dma_copy_cfg_tag dma_copy_cfg)
{

	int                      ret = CAMERA_SUCCESS;

	if (!IS_PREVIEW) {
		CMR_LOGE("Preview Stoped");
		return ret;
	}

	return cmr_dma_cpy(dma_copy_cfg);
}

int camera_rotation_init(void)
{
	struct camera_ctrl       *ctrl = &g_cxt->control;
	struct rotation_context  *cxt  = &g_cxt->rot_cxt;
	int                      ret   = CAMERA_SUCCESS;

	if (1 == ctrl->rot_inited) {
		CMR_LOGI("Rot has been intialized");
		goto exit;
	}

	ret = cmr_rot_init();
	if (ret) {
		CMR_LOGE("Failed to init Rot %d", ret);
		ret = -CAMERA_NOT_SUPPORTED;
	} else {
		cmr_rot_evt_reg(camera_rot_evt_cb);
		cxt->rot_state = IMG_CVT_IDLE;
		sem_init(&cxt->cmr_rot_sem, 0, 1);
		ctrl->rot_inited = 1;
	}

exit:
	return ret;
}

int camera_rotation_deinit(void)
{
	struct camera_ctrl       *ctrl = &g_cxt->control;
	struct rotation_context  *cxt  = &g_cxt->rot_cxt;
	int                      ret   = CAMERA_SUCCESS;

	if (0 == ctrl->rot_inited) {
		CMR_LOGI("Rot has been de-intialized");
		goto exit;
	}

	ret = cmr_rot_deinit();
	if (ret) {
		CMR_LOGE("Failed to de-init ROT %d", ret);
		ret = -CAMERA_NOT_SUPPORTED;
		goto exit;
	}
	sem_destroy(&cxt->cmr_rot_sem);
	bzero(cxt, sizeof(*cxt));
	ctrl->rot_inited = 0;

exit:
	return ret;
}

int camera_scaler_init(void)
{
	struct camera_ctrl       *ctrl = &g_cxt->control;
	struct scaler_context    *cxt  = &g_cxt->scaler_cxt;
	int                      ret   = CAMERA_SUCCESS;

	if (1 == ctrl->scaler_inited) {
		CMR_LOGI("scaler has been intialized");
		goto exit;
	}

	ret = cmr_scale_init();
	if (ret) {
		CMR_LOGE("Failed to init scaler %d", ret);
		ret = -CAMERA_NOT_SUPPORTED;
	} else {
		ret = cmr_scale_capability(&cxt->sc_capability, &cxt->sc_factor);
		if (ret) {
			CMR_LOGE("Failed to get frame scaling capability %d", ret);
			ret = -CAMERA_NOT_SUPPORTED;
			goto exit;
		}
		cmr_scale_evt_reg(camera_scaler_evt_cb);
		cxt->scale_state = IMG_CVT_IDLE;
		ctrl->scaler_inited = 1;
	}

exit:
	return ret;
}

int camera_scaler_deinit(void)
{
	struct camera_ctrl       *ctrl = &g_cxt->control;
	struct scaler_context    *cxt  = &g_cxt->scaler_cxt;
	int                      ret   = CAMERA_SUCCESS;

	if (0 == ctrl->scaler_inited) {
		CMR_LOGI("scaler has been de-intialized");
		goto exit;
	}

	ret = cmr_scale_deinit();
	if (ret) {
		CMR_LOGE("Failed to de-init scaler %d", ret);
		ret = -CAMERA_NOT_SUPPORTED;
		goto exit;
	}
	bzero(cxt, sizeof(*cxt));
	ctrl->scaler_inited = 0;
exit:
	return ret;
}

int camera_prev_thread_init(void)
{
	CMR_MSG_INIT(message);
	int                      ret = CAMERA_SUCCESS;
	pthread_attr_t           attr;

	CMR_LOGV("inited, %d", g_cxt->prev_inited);

	CMR_PRINT_TIME;
	if (!g_cxt->prev_inited) {
		ret = cmr_msg_queue_create(CAMERA_PREV_MSG_QUEUE_SIZE, &g_cxt->prev_msg_que_handle);
		if (ret) {
			CMR_LOGE("NO Memory, Failed to create message queue");
			return ret;
		}
		sem_init(&g_cxt->prev_sync_sem, 0, 0);
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		ret = pthread_create(&g_cxt->prev_thread, &attr, camera_prev_thread_proc, NULL);
		sem_wait(&g_cxt->prev_sync_sem);
		g_cxt->prev_inited = 1;
		message.msg_type = CMR_EVT_PREV_INIT;
		message.data = 0;
		ret = cmr_msg_post(g_cxt->prev_msg_que_handle, &message);
		if (ret) {
			CMR_LOGE("Faile to send one msg to camera prev  thread");
		}

	}
	return ret;
}

int camera_prev_thread_deinit(void)
{
	CMR_MSG_INIT(message);
	int                      ret = CAMERA_SUCCESS;

	CMR_LOGV("inited, %d", g_cxt->prev_inited);

	if (g_cxt->prev_inited) {
		message.msg_type = CMR_EVT_PREV_EXIT;
		ret = cmr_msg_post(g_cxt->prev_msg_que_handle, &message);
		if (ret) {
			CMR_LOGE("Faile to send one msg to camera prev thread");
		}
		sem_wait(&g_cxt->prev_sync_sem);
		sem_destroy(&g_cxt->prev_sync_sem);
		cmr_msg_queue_destroy(g_cxt->prev_msg_que_handle);
		g_cxt->prev_msg_que_handle = 0;
		g_cxt->prev_inited = 0;
	}
	return ret ;
}

int camera_prev_thread_handle(struct frm_info *data)
{
	int                      ret = CAMERA_SUCCESS;

	ret =  camera_v4l2_preview_handle(data);

	return ret;
}

int camera_prev_thread_rot_handle(uint32_t evt_type, uint32_t sub_type, struct img_frm * data)
{
	int                      ret = CAMERA_SUCCESS;
	ret = camera_rotation_handle(evt_type, sub_type, data);
	return ret;
}

void *camera_prev_thread_proc(void *data)
{
	CMR_MSG_INIT(message);
	int ret = CAMERA_SUCCESS;
	int prev_thread_exit_flag = 0;
	camera_cb_info      cb_info;

	CMR_PRINT_TIME;
	sem_post(&g_cxt->prev_sync_sem);
	CMR_PRINT_TIME;
	while (1) {
		ret = cmr_msg_get(g_cxt->prev_msg_que_handle, &message);
		if (ret) {
			CMR_LOGE("preview thread: Message queue destroied");
			break;
		}

		CMR_LOGE("preview thread: message.msg_type 0x%x, data 0x%x", message.msg_type, (uint32_t)message.data);

		switch (message.msg_type) {
		case CMR_EVT_PREV_INIT:
			CMR_PRINT_TIME;
			CMR_LOGE("preview_thread inited\n");
			CMR_PRINT_TIME;
			break;

		case CMR_EVT_PREV_V4L2_TX_DONE:          /*preview handle*/
			CMR_PRINT_TIME;
			struct frm_info *frm_data = (struct frm_info *)message.data;

			if (!IS_PREV_FRM(frm_data->frame_id)) {
				CMR_LOGE("Wrong frame id %d, drop this frame", frm_data->frame_id);
				break;
			}

			ret = camera_prev_thread_handle(frm_data);
			if (ret) {
				CMR_LOGE("preview failed %d", ret);
				memset(&cb_info, 0, sizeof(camera_cb_info));
				cb_info.cb_type = CAMERA_EXIT_CB_FAILED;
				cb_info.cb_func = CAMERA_FUNC_START_PREVIEW;
				camera_callback_start(&cb_info);
			}
			CMR_PRINT_TIME;
			break;

		case CMR_EVT_PREV_CVT_ROT_DONE:
			CMR_PRINT_TIME;
			ret = camera_prev_thread_rot_handle(message.msg_type, message.sub_msg_type, (struct img_frm *)message.data);
			if(ret){
				CMR_LOGE("preview rot failed %d", ret);
				/*to do , preview error handle*/
			}
			CMR_PRINT_TIME;
			break;

		case CMR_EVT_PREV_V4L2_TX_NO_MEM:
			CMR_PRINT_TIME;
			CMR_LOGE("CMR_V4L2_TX_NO_MEM, something wrong");
			CMR_PRINT_TIME;
			break;

		case CMR_EVT_PREV_V4L2_TX_ERR:
		case CMR_EVT_PREV_V4L2_CSI2_ERR:
			CMR_PRINT_TIME;

			CMR_PRINT_TIME;
			break;

		case CMR_EVT_PREV_EXIT:
			CMR_LOGV("prev thread exit");
			prev_thread_exit_flag = 1;
			sem_post(&g_cxt->prev_sync_sem);
			CMR_PRINT_TIME;
			break;

		default:
			break;
		}

		if(1 == message.alloc_flag){
			free(message.data);
		}

		if(prev_thread_exit_flag) {
			CMR_LOGI("prev_rout proc exit.");
			break;
		}
	}


	CMR_LOGI("exit.");

	return NULL;
}


int camera_cap_thread_init(void)
{
	CMR_MSG_INIT(message);
	int                      ret = CAMERA_SUCCESS;
	pthread_attr_t           attr;

	CMR_LOGV("inited, %d", g_cxt->cap_inited);

	CMR_PRINT_TIME;
	if (!g_cxt->cap_inited) {
		ret = cmr_msg_queue_create(CAMERA_CAP_MSG_QUEUE_SIZE, &g_cxt->cap_msg_que_handle);
		if (ret) {
			CMR_LOGE("NO Memory, Failed to create capture message queue \n");
			return ret;
		}
		sem_init(&g_cxt->cap_sync_sem, 0, 0);
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		ret = pthread_create(&g_cxt->cap_thread, &attr, camera_cap_thread_proc, NULL);
		sem_wait(&g_cxt->cap_sync_sem);
		g_cxt->cap_inited = 1;
		message.msg_type = CMR_EVT_CAP_INIT;
		message.data = 0;
		ret = cmr_msg_post(g_cxt->cap_msg_que_handle, &message);
		if (ret) {
			CMR_LOGE("Faile to send one msg to camera prev  thread");
		}

	}
	return ret;
}

int camera_cap_thread_deinit(void)
{
	CMR_MSG_INIT(message);
	int                      ret = CAMERA_SUCCESS;

	CMR_LOGV("inited, %d", g_cxt->cap_inited);

	if (g_cxt->cap_inited) {
		message.msg_type = CMR_EVT_CAP_EXIT;
		ret = cmr_msg_post(g_cxt->cap_msg_que_handle, &message);
		if (ret) {
			CMR_LOGE("Faile to send one msg to camera prev thread");
		}
		sem_wait(&g_cxt->cap_sync_sem);
		sem_destroy(&g_cxt->cap_sync_sem);
		cmr_msg_queue_destroy(g_cxt->cap_msg_que_handle);
		g_cxt->cap_msg_que_handle = 0;
		g_cxt->cap_inited = 0;
	}
	return ret ;
}

int camera_cap_subthread_init(void)
{
	CMR_MSG_INIT(message);
	int                      ret = CAMERA_SUCCESS;
	pthread_attr_t           attr;

	CMR_LOGV("inited, %d", g_cxt->cap_sub_inited);

	CMR_PRINT_TIME;
	if (!g_cxt->cap_sub_inited) {
		ret = cmr_msg_queue_create(CAMERA_CAP_MSG_QUEUE_SIZE, &g_cxt->cap_sub_msg_que_handle);
		if (ret) {
			CMR_LOGE("NO Memory, Failed to create capture sub message queue \n");
			return ret;
		}
		sem_init(&g_cxt->cap_sub_sync_sem, 0, 0);
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		ret = pthread_create(&g_cxt->cap_sub_thread, &attr, camera_cap_subthread_proc, NULL);
		sem_wait(&g_cxt->cap_sub_sync_sem);
		g_cxt->cap_sub_inited = 1;
		message.msg_type = CMR_EVT_CAP_INIT;
		message.data = 0;
		ret = cmr_msg_post(g_cxt->cap_sub_msg_que_handle, &message);
		if (ret) {
			CMR_LOGE("Faile to send one msg to camera prev  thread");
		}

	}
	return ret;
}

int camera_cap_subthread_deinit(void)
{
	CMR_MSG_INIT(message);
	int                      ret = CAMERA_SUCCESS;

	CMR_LOGV("inited, %d", g_cxt->cap_sub_inited);

	if (g_cxt->cap_sub_inited) {
		message.msg_type = CMR_EVT_CAP_EXIT;
		ret = cmr_msg_post(g_cxt->cap_sub_msg_que_handle, &message);
		if (ret) {
			CMR_LOGE("Faile to send one msg to camera prev thread");
		}
		sem_wait(&g_cxt->cap_sub_sync_sem);
		sem_destroy(&g_cxt->cap_sub_sync_sem);
		cmr_msg_queue_destroy(g_cxt->cap_sub_msg_que_handle);
		g_cxt->cap_sub_msg_que_handle = 0;
		g_cxt->cap_sub_inited = 0;
	}
	return ret ;
}

int camera_take_continue_picture(int cap_cnt)
{
	int                      preview_format;
	uint32_t                 skip_number = 0;
	int                      ret = CAMERA_SUCCESS;
	struct buffer_cfg        buffer_info;

	CMR_LOGI("start.");

	ret = camera_capture_init_continue();
	if (ret) {
		CMR_LOGE("Failed to init raw capture mode.");
		return -CAMERA_FAILED;
	}

	if (V4L2_SENSOR_FORMAT_RAWRGB != g_cxt->sn_cxt.sn_if.img_fmt) {
		g_cxt->skip_mode = IMG_SKIP_HW;
	}

	if (IMG_SKIP_HW == g_cxt->skip_mode) {
		skip_number = g_cxt->sn_cxt.sensor_info->capture_skip_num;
	}

	CMR_PRINT_TIME;
	skip_number = 1;
	ret = cmr_v4l2_cap_start(skip_number);
	if (ret) {
		CMR_LOGE("Fail to start V4L2 Capture");
		return -CAMERA_FAILED;
	}
	g_cxt->v4l2_cxt.v4l2_state = V4L2_PREVIEW;
	CMR_PRINT_TIME;

	g_cxt->capture_raw_status = CMR_CAPTURE;
	return ret;
}

int camera_cap_continue(void)
{
	int ret = CAMERA_SUCCESS;

	camera_cfg_rot_cap_param_reset();

	if (CAMERA_NORMAL_CONTINUE_SHOT_MODE == g_cxt->cap_mode) {
		if (V4L2_SENSOR_FORMAT_JPEG == g_cxt->sn_cxt.sn_if.img_fmt
			|| IMG_DATA_TYPE_RAW == g_cxt->cap_original_fmt)
			ret = camera_take_picture_continue(g_cxt->cap_cnt);
		else
			g_cxt->chn_2_status = CHN_BUSY;
		/*ret = camera_take_continue_picture(0);*/
	}
	if (CAMERA_ZSL_CONTINUE_SHOT_MODE == g_cxt->cap_mode) {
		g_cxt->chn_2_status = CHN_BUSY;
	}
	return ret;
}

int camera_jpeg_specify_notify_done(int state)
{
	int ret = CAMERA_SUCCESS;


	pthread_mutex_lock(&g_cxt->jpeg_specify_cxt.jpeg_specify_mutex);
	g_cxt->jpeg_specify_cxt.jpeg_specify_state = state;
	pthread_mutex_unlock(&g_cxt->jpeg_specify_cxt.jpeg_specify_mutex);

	sem_post(&g_cxt->jpeg_specify_cxt.jpeg_specify_cap_sem);

	CMR_LOGE("done state=%d",state);

	return ret;
}

int camera_jpeg_specify_wait_done(void)
{
	int ret = CAMERA_SUCCESS;
	int t_ret = 0;


	if (!camera_capture_need_exit()) {
		t_ret = sem_wait(&g_cxt->jpeg_specify_cxt.jpeg_specify_cap_sem);
	}
	pthread_mutex_lock(&g_cxt->jpeg_specify_cxt.jpeg_specify_mutex);
	if (t_ret) {
		g_cxt->jpeg_specify_cxt.jpeg_specify_state = CAMERA_FAILED;
		CMR_LOGE("wait fail");
	}
	if (camera_capture_need_exit()) {
		g_cxt->jpeg_specify_cxt.jpeg_specify_state = CAMERA_FAILED;
		CMR_LOGE("cancel cap");
	}
	ret = g_cxt->jpeg_specify_cxt.jpeg_specify_state;
	pthread_mutex_unlock(&g_cxt->jpeg_specify_cxt.jpeg_specify_mutex);

	CMR_LOGE("done ret=%d",ret);

	return ret;
}

int camera_is_jpeg_specify_process(int state)
{
	int ret = 0;


	if ((CAMERA_HDR_MODE == g_cxt->cap_mode || CAMERA_NORMAL_CONTINUE_SHOT_MODE == g_cxt->cap_mode)
		&& IMG_DATA_TYPE_JPEG == g_cxt->cap_original_fmt) {
		ret = 1;
		camera_jpeg_specify_notify_done(state);
	}

	CMR_LOGE("cap_original_fmt=%d", g_cxt->cap_original_fmt);
	CMR_LOGE("ret=%d,state=%d", ret, state);

	return ret;
}

int camera_jpeg_specify_stop_jpeg_decode(void)
{
	int ret = CAMERA_SUCCESS;


	CMR_LOGI("jpeg_state=%d",g_cxt->jpeg_cxt.jpeg_state);
	if (0 != g_cxt->jpeg_cxt.handle) {
		jpeg_stop(g_cxt->jpeg_cxt.handle);
		g_cxt->jpeg_cxt.handle = 0;
	} else {
		CMR_LOGI("don't need stop jpeg.");
	}
	g_cxt->jpeg_cxt.jpeg_state = JPEG_IDLE;

	return ret;

}

int camera_take_picture_continue(int cap_cnt)
{
	int                      preview_format;
	uint32_t                 skip_number = 0;
	int                      ret = CAMERA_SUCCESS;
	struct buffer_cfg        buffer_info;

	CMR_LOGI("start.");

	ret = cmr_v4l2_if_cfg(&g_cxt->sn_cxt.sn_if);
	if (ret) {
		CMR_LOGE("the sensor interface is unsupported by V4L2");
		return -CAMERA_FAILED;
	}

	ret = camera_capture_init();
	if (ret) {
		CMR_LOGE("Failed to init raw capture mode.");
		return -CAMERA_FAILED;
	}


	CMR_PRINT_TIME;
	skip_number = 1;
	ret = cmr_v4l2_cap_start(skip_number);
	if (ret) {
		CMR_LOGE("Fail to start V4L2 Capture");
		return -CAMERA_FAILED;
	}
	g_cxt->v4l2_cxt.v4l2_state = V4L2_PREVIEW;
	CMR_PRINT_TIME;
	ret = Sensor_StreamOn();
	if (ret) {
		CMR_LOGE("Fail to switch on the sensor stream");
		return -CAMERA_FAILED;
	}
	g_cxt->capture_raw_status = CMR_CAPTURE;

	g_cxt->cap_cnt = cap_cnt;
	return ret;
}

int camera_cap_post(void *data)
{
	int ret = CAMERA_SUCCESS;
	CMR_MSG_INIT(message);
	int tmp = 0;
	int jpeg_ret = CAMERA_SUCCESS;

	g_cxt->cap_cnt++;
	g_cxt->cap_cnt_for_err = g_cxt->cap_cnt;
	CMR_LOGI("g_cxt->cap_cnt,%d.",g_cxt->cap_cnt);
	if (CAMERA_NORMAL_MODE == g_cxt->cap_mode) {
		CMR_LOGI("need to stop cap.");
		ret = cmr_v4l2_cap_stop();
		if (ret) {
			CMR_LOGE("Failed to stop v4l2 capture, %d", ret);
			return -CAMERA_FAILED;
		}

		CMR_PRINT_TIME;
		g_cxt->sn_cxt.previous_sensor_mode = SENSOR_MODE_MAX;
		ret = Sensor_StreamOff();
		if (ret) {
			CMR_LOGE("Failed to switch off the sensor stream, %d", ret);
		}
		ret = cmr_v4l2_if_decfg(&g_cxt->sn_cxt.sn_if);
		if (ret) {
			CMR_LOGE("Failed to stop IF , %d", ret);
			return -CAMERA_FAILED;
		}

		if (ISP_COWORK == g_cxt->isp_cxt.isp_state) {
			ret = isp_video_stop();
			g_cxt->isp_cxt.isp_state = ISP_IDLE;
			if (ret) {
				CMR_LOGE("Failed to stop ISP video mode, %d", ret);
			}
		}
		CMR_PRINT_TIME;
	/*	ret = camera_snapshot_stop_set();
		if (ret) {
			CMR_LOGE("Failed to exit snapshot %d", ret);
			return -CAMERA_FAILED;
		}*/
	} else if (CAMERA_HDR_MODE == g_cxt->cap_mode) {
		g_cxt->cap_process_id = 0;
		ret = cmr_v4l2_cap_stop();
		if (ret) {
			CMR_LOGE("Failed to stop v4l2 capture, %d", ret);
			return -CAMERA_FAILED;
		}

		CMR_PRINT_TIME;
#if !USE_SENSOR_OFF_ON_FOR_HDR
		ret = Sensor_GetMode(&g_cxt->sn_cxt.previous_sensor_mode);
		if (ret) {
#endif
			ret = Sensor_StreamOff();
			if (ret) {
				CMR_LOGE("Failed to switch off the sensor stream, %d", ret);
			}
			ret = cmr_v4l2_if_decfg(&g_cxt->sn_cxt.sn_if);
			if (ret) {
				CMR_LOGE("Failed to stop IF , %d", ret);
				return -CAMERA_FAILED;
			}
			g_cxt->sn_cxt.previous_sensor_mode = SENSOR_MODE_MAX;
#if !USE_SENSOR_OFF_ON_FOR_HDR
		}
#endif

		if (ISP_COWORK == g_cxt->isp_cxt.isp_state) {
			ret = isp_video_stop();
			g_cxt->isp_cxt.isp_state = ISP_IDLE;
			if (ret) {
				CMR_LOGE("Failed to stop ISP video mode, %d", ret);
			}
		}
		CMR_PRINT_TIME;

		if (IMG_DATA_TYPE_JPEG == g_cxt->cap_original_fmt) {
			ret = camera_start_jpeg_decode(data);
			if (ret) {
				CMR_LOGE("Start JpegDec Failed, %d", ret);
				return CAMERA_JPEG_SPECIFY_FAILED;
			}
			jpeg_ret = camera_jpeg_specify_wait_done();
			if (jpeg_ret) {
				CMR_LOGE("hdr wait %d", jpeg_ret);

				camera_jpeg_specify_stop_jpeg_decode();
				return CAMERA_JPEG_SPECIFY_FAILED;
			} else {
				((struct frm_info*)data)->data_endian.uv_endian = 1;
			}
		}


		camera_capture_hdr_data((struct frm_info *)data);
		if (HDR_CAP_NUM == g_cxt->cap_cnt) {
#if !USE_SENSOR_OFF_ON_FOR_HDR
			ret = Sensor_GetMode(&g_cxt->sn_cxt.previous_sensor_mode);
			if (ret) {
#endif
				g_cxt->sn_cxt.previous_sensor_mode = SENSOR_MODE_MAX;
				ret = Sensor_StreamOff();
				if (ret) {
					CMR_LOGE("Failed to switch off the sensor stream, %d", ret);
				}
				ret = cmr_v4l2_if_decfg(&g_cxt->sn_cxt.sn_if);
				if (ret) {
					CMR_LOGE("Failed to stop IF , %d", ret);
					return -CAMERA_FAILED;
				}
#if !USE_SENSOR_OFF_ON_FOR_HDR
			}
#endif
		/*	ret = camera_snapshot_stop_set();
			if (ret) {
				CMR_LOGE("Failed to exit snapshot %d", ret);
				return -CAMERA_FAILED;
			}*/
		} else {
			tmp = g_cxt->cap_cnt;
			camera_call_cb(CAMERA_EVT_CB_FLUSH,
				camera_get_client_data(),
				CAMERA_FUNC_TAKE_PICTURE,
				0);
			ret = camera_take_picture_hdr(tmp);
			g_cxt->cap_cnt = tmp;
			if (ret) {
				CMR_LOGE("Failed to camera_take_picture_hdr %d.", ret);
				return -CAMERA_FAILED;
			} else {
				CMR_LOGI("exit,%d.",g_cxt->cap_cnt);
				return CAMERA_EXIT;
			}
		}
	} else if (CAMERA_NORMAL_CONTINUE_SHOT_MODE == g_cxt->cap_mode) {
		if (IMG_DATA_TYPE_JPEG == g_cxt->cap_original_fmt
			|| IMG_DATA_TYPE_RAW == g_cxt->cap_original_fmt) {
			CMR_PRINT_TIME;

			ret = cmr_v4l2_cap_stop();
			if (ret) {
				CMR_LOGE("Failed to stop v4l2 capture, %d", ret);
				return -CAMERA_FAILED;
			}

			g_cxt->sn_cxt.previous_sensor_mode = SENSOR_MODE_MAX;
			ret = Sensor_StreamOff();
			if (ret) {
				CMR_LOGE("Failed to switch off the sensor stream, %d", ret);
			}
			ret = cmr_v4l2_if_decfg(&g_cxt->sn_cxt.sn_if);
			if (ret) {
				CMR_LOGE("Failed to stop IF , %d", ret);
				return -CAMERA_FAILED;
			}


			if (IMG_DATA_TYPE_JPEG == g_cxt->cap_original_fmt) {
				ret = camera_start_jpeg_decode(data);
				if (ret) {
					CMR_LOGE("Start JpegDec Failed, %d", ret);
					return CAMERA_JPEG_SPECIFY_FAILED;
				}
				jpeg_ret = camera_jpeg_specify_wait_done();
				if (jpeg_ret) {
					CMR_LOGE("burst cap wait %d", jpeg_ret);
					camera_jpeg_specify_stop_jpeg_decode();
					return CAMERA_JPEG_SPECIFY_FAILED;
				} else {
					((struct frm_info*)data)->data_endian.uv_endian = 1;
				}
			}
		}else if  (g_cxt->cap_cnt == g_cxt->total_capture_num) {
			ret = cmr_v4l2_cap_stop();
			if (ret) {
				CMR_LOGE("Failed to stop v4l2 capture, %d", ret);
				return -CAMERA_FAILED;
			}
			CMR_PRINT_TIME;
			g_cxt->sn_cxt.previous_sensor_mode = SENSOR_MODE_MAX;
			ret = Sensor_StreamOff();
			if (ret) {
				CMR_LOGE("Failed to switch off the sensor stream, %d", ret);
			}
			ret = cmr_v4l2_if_decfg(&g_cxt->sn_cxt.sn_if);
			if (ret) {
				CMR_LOGE("Failed to stop IF , %d", ret);
				return -CAMERA_FAILED;
			}
			CMR_PRINT_TIME;
		/*	ret = camera_snapshot_stop_set();
			if (ret) {
				CMR_LOGE("Failed to exit snapshot %d", ret);
				return -CAMERA_FAILED;
			}*/
		} else {
			g_cxt->chn_2_status = CHN_IDLE;
			if ((TAKE_PICTURE_NEEDED == camera_get_take_picture()) && IS_CHN_BUSY(CHN_2)) {
				message.msg_type = CMR_EVT_BEFORE_CAPTURE;
				message.alloc_flag = 0;
				ret = cmr_msg_post(g_cxt->msg_queue_handle, &message);
				CMR_LOGI("send post.");
			}
		}
	}else if (CAMERA_ZSL_CONTINUE_SHOT_MODE == g_cxt->cap_mode) {
		g_cxt->chn_2_status = CHN_IDLE;
		if ((TAKE_PICTURE_NEEDED == camera_get_take_picture()) && IS_CHN_BUSY(CHN_2)) {
			message.msg_type = CMR_EVT_BEFORE_CAPTURE;
			message.alloc_flag = 0;
			ret = cmr_msg_post(g_cxt->msg_queue_handle, &message);
			CMR_LOGI("send post.");
		}
	}
	return ret;
}

void *camera_cap_thread_proc(void *data)
{
	CMR_MSG_INIT(message);
	int                      ret = CAMERA_SUCCESS;
	int                      exit_flag = 0;
	camera_cb_info           cb_info;

	CMR_PRINT_TIME;
	sem_post(&g_cxt->cap_sync_sem);
	CMR_PRINT_TIME;
	while (1) {
		ret = cmr_msg_get(g_cxt->cap_msg_que_handle, &message);
		if (ret) {
			CMR_LOGE("Capture thread: Message queue destroied");
			break;
		}

		CMR_LOGE("capture thread: message.msg_type 0x%x, data 0x%x", message.msg_type, (uint32_t)message.data);

		switch (message.msg_type) {
		case CMR_EVT_CAP_INIT:
			CMR_PRINT_TIME;
			CMR_LOGE("capture_thread inited\n");
			CMR_PRINT_TIME;
			break;

		case CMR_EVT_CAP_TX_DONE:
			CMR_PRINT_TIME;
			struct frm_info *data = (struct frm_info *)message.data;
			if (0 == data) {
				break;
			}
			if (!IS_CAP_FRM(data->frame_id)) {
				CMR_LOGE("capture: wrong frame id %d, drop this frame", data->frame_id);
				break;
			} else {
				CMR_LOGV("cap: frame id=%x \n", data->frame_id);
			}
			if (TAKE_PICTURE_NO == camera_get_take_picture()) {
				cmr_v4l2_free_frame(data->channel_id, data->frame_id);
			} else {
				ret = camera_cap_post(data);
				if (CAMERA_EXIT == ret || CAMERA_JPEG_SPECIFY_FAILED == ret) {
					CMR_LOGI("normal exit.");
					cmr_v4l2_free_frame(data->channel_id, data->frame_id);
					if (CAMERA_JPEG_SPECIFY_FAILED == ret) {
						memset(&cb_info, 0, sizeof(camera_cb_info));
						cb_info.cb_type = CAMERA_EXIT_CB_FAILED;
						cb_info.cb_func = CAMERA_FUNC_TAKE_PICTURE;
						camera_callback_start(&cb_info);
					}
					break;
				}
				ret = camera_v4l2_capture_handle(data);
				if (ret) {
					CMR_LOGE("capture failed %d", ret);
					memset(&cb_info, 0, sizeof(camera_cb_info));
					cb_info.cb_type = CAMERA_EXIT_CB_FAILED;
					cb_info.cb_func = CAMERA_FUNC_TAKE_PICTURE;
					camera_callback_start(&cb_info);
				}

				camera_wait_takepicdone(g_cxt);
				/*cmr_v4l2_free_frame(data->channel_id, data->frame_id);*/
				if ((g_cxt->cap_cnt == g_cxt->total_capture_num) || (CAMERA_HDR_MODE == g_cxt->cap_mode)) {
					g_cxt->capture_status = CMR_IDLE;
					camera_snapshot_stop_set();
					ret = camera_set_take_picture(TAKE_PICTURE_NO);
					if (CAMERA_ZSL_CONTINUE_SHOT_MODE == g_cxt->cap_mode) {
						g_cxt->chn_2_status = CHN_BUSY;
					}
				} else {
					if (camera_capture_need_exit()) {
						g_cxt->jpeg_cxt.jpeg_state = JPEG_IDLE;
						g_cxt->capture_status = CMR_IDLE;
						CMR_LOGV("done.");
						break;
					}
					camera_cap_continue();
				}
				cmr_v4l2_free_frame(data->channel_id, data->frame_id);
				if (camera_capture_need_exit()) {
					g_cxt->jpeg_cxt.jpeg_state = JPEG_IDLE;
					g_cxt->capture_status = CMR_IDLE;
					CMR_LOGV("done.");
					break;
				}
			}
			CMR_PRINT_TIME;
			break;

		case CMR_EVT_CAP_RAW_TX_DONE:
		{
			CMR_PRINT_TIME;
			struct frm_info *data = (struct frm_info *)message.data;

			if (CAMERA_TOOL_RAW_MODE != g_cxt->cap_mode
				&& (IMG_DATA_TYPE_JPEG == g_cxt->cap_original_fmt
					||IMG_DATA_TYPE_RAW == g_cxt->cap_original_fmt)) {
				if (0 == data) {
					break;
				}
				if (!IS_CAP_FRM(data->frame_id)) {
					CMR_LOGE("capture: wrong frame id %d, drop this frame", data->frame_id);
					break;
				} else {
					CMR_LOGV("cap: frame id=%x \n", data->frame_id);
				}
				if (TAKE_PICTURE_NO == camera_get_take_picture()) {
					cmr_v4l2_free_frame(data->channel_id, data->frame_id);
				} else {
					ret = camera_cap_post(data);
					if (CAMERA_EXIT == ret || CAMERA_JPEG_SPECIFY_FAILED == ret) {
						CMR_LOGI("normal exit.");
						cmr_v4l2_free_frame(data->channel_id, data->frame_id);
						if (CAMERA_JPEG_SPECIFY_FAILED == ret) {
							memset(&cb_info, 0, sizeof(camera_cb_info));
							cb_info.cb_type = CAMERA_EXIT_CB_FAILED;
							cb_info.cb_func = CAMERA_FUNC_TAKE_PICTURE;
							camera_callback_start(&cb_info);
						}
						break;
					}
					ret = camera_v4l2_capture_handle(data);
					if (ret) {
						CMR_LOGE("capture failed %d", ret);
						memset(&cb_info, 0, sizeof(camera_cb_info));
						cb_info.cb_type = CAMERA_EXIT_CB_FAILED;
						cb_info.cb_func = CAMERA_FUNC_TAKE_PICTURE;
						camera_callback_start(&cb_info);
					}

					camera_wait_takepicdone(g_cxt);
					cmr_v4l2_free_frame(data->channel_id, data->frame_id);
					if (camera_capture_need_exit()) {
						g_cxt->jpeg_cxt.jpeg_state = JPEG_IDLE;
						g_cxt->capture_status = CMR_IDLE;
						CMR_LOGV("done.");
						break;
					}
					if ((g_cxt->cap_cnt == g_cxt->total_capture_num) || (CAMERA_HDR_MODE == g_cxt->cap_mode)) {
						g_cxt->capture_status = CMR_IDLE;
						camera_snapshot_stop_set();
						ret = camera_set_take_picture(TAKE_PICTURE_NO);
						if (CAMERA_ZSL_CONTINUE_SHOT_MODE == g_cxt->cap_mode) {
							g_cxt->chn_2_status = CHN_BUSY;
						}
					} else {
						camera_cap_continue();
					}
					/*cmr_v4l2_free_frame(data->channel_id, data->frame_id);*/
				}
			} else {
				if (!IS_CAP_FRM(data->frame_id)) {
					CMR_LOGE("capture: wrong frame id %d, drop this frame", data->frame_id);
					break;
				} else {
					CMR_LOGV("cap raw: frame id=%x \n", data->frame_id);
				}

				ret = camera_v4l2_capture_handle(data);
				if (ret) {
					CMR_LOGE("capture failed %d", ret);
					memset(&cb_info, 0, sizeof(camera_cb_info));
					cb_info.cb_type = CAMERA_EXIT_CB_FAILED;
					cb_info.cb_func = CAMERA_FUNC_TAKE_PICTURE;
					camera_callback_start(&cb_info);
				}
				ret = camera_cap_post(data);
				if (CAMERA_EXIT == ret) {
					CMR_LOGI("normal exit.");
					break;
				}
				if (g_cxt->cap_cnt == g_cxt->total_capture_num) {
					camera_snapshot_stop_set();
				}
			}
			CMR_PRINT_TIME;
		}
			break;

		case CMR_EVT_CAP_EXIT:
			CMR_LOGV("capture thread CMR_EVT_CAP_EXIT \n");
			exit_flag = 1;
			sem_post(&g_cxt->cap_sync_sem);
			CMR_PRINT_TIME;
			break;

		default:
			break;
		}

		if (1 == message.alloc_flag) {
			if (message.data) {
				free(message.data);
				message.data = 0;
			}
		}

		if (exit_flag) {
			CMR_LOGI("capture thread exit ");
			break;
		}
	}

	CMR_LOGI("capture thread exit done \n");

	return NULL;
}

void *camera_cap_subthread_proc(void *data)
{
	CMR_MSG_INIT(message);
	int                      ret = CAMERA_SUCCESS;
	int                      exit_flag = 0;

	CMR_PRINT_TIME;
	sem_post(&g_cxt->cap_sub_sync_sem);
	CMR_PRINT_TIME;
	while (1) {
		ret = cmr_msg_get(g_cxt->cap_sub_msg_que_handle, &message);
		if (ret) {
			CMR_LOGE("Capture subthread: Message queue destroied");
			break;
		}

		CMR_LOGE("capture subthread: message.msg_type 0x%x, data 0x%x", message.msg_type, (uint32_t)message.data);

		switch (message.msg_type) {
		case CMR_EVT_CAP_INIT:
			CMR_PRINT_TIME;
			CMR_LOGE("capture sub thread inited\n");
			CMR_PRINT_TIME;
			break;


		case CMR_IMG_CVT_SC_DONE:
			CMR_PRINT_TIME;
			ret = camera_img_cvt_handle(message.msg_type,
					message.sub_msg_type,
					(struct img_frm *)message.data);
			CMR_PRINT_TIME;
			break;

		case CMR_JPEG_ENC_DONE:
			CMR_PRINT_TIME;
			ret = camera_jpeg_codec_handle(message.msg_type,
					message.sub_msg_type,
					(void *)message.data);
			CMR_PRINT_TIME;
			break;

		case CMR_EVT_CAP_EXIT:
			CMR_LOGV("capture sub thread CMR_EVT_CAP_EXIT \n");
			exit_flag = 1;
			sem_post(&g_cxt->cap_sub_sync_sem);
			CMR_PRINT_TIME;
			break;

		default:
			break;
		}

		if (1 == message.alloc_flag) {
			if (message.data) {
				free(message.data);
				message.data = 0;
			}
		}

		if (exit_flag) {
			CMR_LOGI("capture sub thread exit ");
			break;
		}
	}

	CMR_LOGI("capture sub thread exit done \n");

	return NULL;
}


int camera_local_init(void)
{
	int                      ret = CAMERA_SUCCESS;

	ret = cmr_msg_queue_create(CAMERA_OEM_MSG_QUEUE_SIZE, &g_cxt->msg_queue_handle);
	if (ret) {
		CMR_LOGE("NO Memory, Frailed to create message queue");
	}

	g_cxt->preview_status  = CMR_IDLE;
	g_cxt->capture_status  = CMR_IDLE;
	g_cxt->capture_raw_status  = CMR_IDLE;
	g_cxt->is_take_picture = TAKE_PICTURE_NO;
	g_cxt->prev_buf_id = 0;
	g_cxt->prev_self_restart = 0;
	g_cxt->sn_cxt.previous_sensor_mode = SENSOR_MODE_MAX;

	g_cxt->set_flag = 0x0;
	g_cxt->ae_wait_stab = 0x0;

	g_cxt->is_reset_if_cfg = 0;

	pthread_mutex_init(&g_cxt->cb_mutex, NULL);
	pthread_mutex_init(&g_cxt->data_mutex, NULL);
	pthread_mutex_init(&g_cxt->prev_mutex, NULL);
	pthread_mutex_init(&g_cxt->take_mutex, NULL);
	pthread_mutex_init(&g_cxt->recover_mutex, NULL);
	pthread_mutex_init(&g_cxt->af_cb_mutex, NULL);
	pthread_mutex_init(&g_cxt->cancel_mutex, NULL);
	pthread_mutex_init(&g_cxt->take_raw_mutex, NULL);
	pthread_mutex_init(&g_cxt->main_prev_mutex, NULL);
	pthread_mutex_init(&g_cxt->jpeg_specify_cxt.jpeg_specify_mutex, NULL);

	ret = camera_sync_var_init(g_cxt);

	return ret;
}

int camera_local_deinit(void)
{
	int                      ret = CAMERA_SUCCESS;

	g_cxt->preview_status = CMR_IDLE;
	g_cxt->capture_status = CMR_IDLE;
	g_cxt->capture_raw_status  = CMR_IDLE;
	g_cxt->ae_wait_stab = 0x0;

	pthread_mutex_destroy (&g_cxt->cancel_mutex);
	pthread_mutex_destroy (&g_cxt->af_cb_mutex);
	pthread_mutex_destroy(&g_cxt->recover_mutex);
	pthread_mutex_destroy(&g_cxt->take_mutex);
	pthread_mutex_destroy (&g_cxt->prev_mutex);
	pthread_mutex_destroy (&g_cxt->data_mutex);
	pthread_mutex_destroy (&g_cxt->cb_mutex);
	pthread_mutex_destroy (&g_cxt->take_raw_mutex);
	pthread_mutex_destroy (&g_cxt->main_prev_mutex);
	pthread_mutex_destroy (&g_cxt->jpeg_specify_cxt.jpeg_specify_mutex);

	cmr_msg_queue_destroy(g_cxt->msg_queue_handle);
	g_cxt->msg_queue_handle = 0;

	ret = camera_sync_var_deinit(g_cxt);

	return ret;
}

int camera_init_internal(uint32_t camera_id)
{
	struct camera_ctrl       *ctrl = &g_cxt->control;
	int                      ret = CAMERA_SUCCESS;

	CMR_PRINT_TIME;
	ret = camera_sensor_init(camera_id);
	if (ret) {
		CMR_LOGE("Failed to init sensor %d", ret);
		goto exit;
	}
	CMR_PRINT_TIME;
	ret = camera_v4l2_init();
	if (ret) {
		CMR_LOGE("Failed to init V4L2 manager %d", ret);
		goto sensor_deinit;
	}

	ret = camera_cb_thread_init();
	if (ret) {
		CMR_LOGE("Failed to init callback manager %d", ret);
		goto v4l2_deinit;
	}

	ret = camera_cap_thread_init();
	if (ret) {
		CMR_LOGE("Failed to init capture manager %d", ret);
		goto cb_deinit;
	}

	ret = camera_cap_subthread_init();
	if (ret) {
		CMR_LOGE("Failed to init sub capture manager %d", ret);
		goto cap_deinit;
	}

	ret = camera_prev_thread_init();
	if (ret) {
		CMR_LOGE("Failed to init preview manager %d", ret);
		goto cap_sub_deinit;
	}

	ret = camera_isp_init();
	if (ret) {
		CMR_LOGE("Failed to init ISP driver %d", ret);
		goto prev_thread_deinit;
	}

	ret = camera_jpeg_init();
	if (ret) {
		CMR_LOGE("Failed to init jpeg codec %d", ret);
		goto isp_deinit;
	}

	ret = camera_rotation_init();
	if (ret) {
		CMR_LOGE("Fail to init Rot device %d", ret);
		goto jpeg_deinit;
	}

	ret = camera_scaler_init();
	if (ret) {
		CMR_LOGE("Failed to init scaler %d", ret);
		goto scale_deinit;
	}

	ret = camera_dma_copy_init();
	if (ret) {
		CMR_LOGE("Fail to init dma copy device %d", ret);
		goto rot_deinit;
	}

	ret = camera_setting_init();
	if (ret) {
		CMR_LOGE("Fail to init Setting sub-module");
	} else {
		g_cxt->camera_id = camera_id;
		goto exit;
	}

dma_copy_deinit:
	camera_dma_copy_deinit();
rot_deinit:
	camera_rotation_deinit();
scale_deinit:
	camera_scaler_deinit();
jpeg_deinit:
	camera_jpeg_deinit();
isp_deinit:
	camera_isp_deinit();
prev_thread_deinit:
	camera_prev_thread_deinit();
cap_sub_deinit:
	camera_cap_subthread_deinit();
cap_deinit:
	camera_cap_thread_deinit();
cb_deinit:
	camera_cb_thread_deinit();
v4l2_deinit:
	camera_v4l2_deinit();
sensor_deinit:
	camera_sensor_deinit();
exit:
	return ret;

}
camera_ret_code_type camera_init(int32_t camera_id)
{
	int                      ret = CAMERA_SUCCESS;

	CMR_LOGV("%d", camera_id);

	bzero(g_cxt, sizeof(*g_cxt));
	ret = camera_local_init();
	if (ret) {
		CMR_LOGE("Failed to init local variables %d", ret);
		return ret;
	}

	ret = camera_create_main_thread(camera_id);
	if (ret) {
		CMR_LOGE("Fail to send message to camera main thread");
		return ret;
	}

	return ret;
}

void camera_flash_handle(void)
{
	if (1 == g_cxt->is_dv_mode) {
		g_cxt->is_dv_mode = 0;
		camera_preview_stop_set();
	}
}

int camera_stop_internal(void)
{
	camera_flash_handle();

	camera_af_deinit();

	arithmetic_fd_deinit();

	camera_setting_deinit();

	camera_dma_copy_deinit();

	camera_scaler_deinit();

	camera_rotation_deinit();

	camera_jpeg_deinit();

	camera_isp_deinit();

	camera_v4l2_deinit();

	camera_sensor_deinit();

	camera_prev_thread_deinit();

	camera_cap_subthread_deinit();

	camera_cap_thread_deinit();

	camera_cb_thread_deinit();

	camera_call_cb(CAMERA_EXIT_CB_DONE,
			camera_get_client_data(),
			CAMERA_FUNC_STOP,
			0);

	return CAMERA_SUCCESS;
}

camera_ret_code_type camera_stop( camera_cb_f_type callback, void *client_data)
{
	CMR_MSG_INIT(message);
	int                      ret = CAMERA_SUCCESS;

	camera_set_client_data(client_data);
	camera_set_hal_cb(callback);

	camera_destroy_main_thread();

	camera_local_deinit();

	return ret;
}

camera_ret_code_type camera_release_frame(uint32_t index)
{
	int                      ret = CAMERA_SUCCESS;

	if (!IS_PREVIEW || g_cxt->v4l2_cxt.v4l2_state != V4L2_PREVIEW) {
		CMR_LOGE("Not in Preview, cmr %d, v4l2 %d", g_cxt->preview_status, g_cxt->v4l2_cxt.v4l2_state);
		return CAMERA_SUCCESS;
	}

	/*
	   only the frame whose rotation angle is zero should be released by app,
	   otherwise, it will be released after rotation done;
	 */
	index += CAMERA_PREV_ID_BASE;
	if (IMG_ROT_0 == g_cxt->prev_rot) {
		if (index >= CAMERA_PREV_ID_BASE &&
			index < CAMERA_PREV_ID_BASE + CAMERA_PREV_FRM_CNT) {
			ret = cmr_v4l2_free_frame(CHN_1, index);
			CMR_LOGV("release the frame whose index is 0x%x, rot %d, ret %d",
				index,
				g_cxt->prev_rot,
				ret);
		} else {
			CMR_LOGE("wrong index, 0x%x ", index);
		}

	}

	return CAMERA_SUCCESS;
}

int camera_preview_sensor_mode(void)
{
	SENSOR_EXP_INFO_T        *sn_info = NULL;
	SENSOR_MODE_INFO_T       *sn_mode = NULL;
	int                      ret = CAMERA_SUCCESS;

	if (IMG_ROT_90 == g_cxt->prev_rot || IMG_ROT_270 == g_cxt->prev_rot) {
		g_cxt->preview_size.width = g_cxt->display_size.height;
		g_cxt->preview_size.height = g_cxt->display_size.width;
	} else {
		g_cxt->preview_size.width  = g_cxt->display_size.width;
		g_cxt->preview_size.height = g_cxt->display_size.height;
	}

	ret = camera_get_sensor_preview_mode(&g_cxt->preview_size, &g_cxt->sn_cxt.preview_mode);
	if (ret) {
		CMR_LOGE("Unsupported display size");
		ret = -CAMERA_NOT_SUPPORTED;
	}
	sn_info = g_cxt->sn_cxt.sensor_info;
	if (NULL == sn_info) {
		CMR_LOGE("NO Sensor.");
		return -CAMERA_NOT_SUPPORTED;
	}
	sn_mode = &sn_info->sensor_mode_info[g_cxt->sn_cxt.preview_mode];
	if (SENSOR_IMAGE_FORMAT_YUV422 != sn_mode->image_format &&
		SENSOR_IMAGE_FORMAT_RAW != sn_mode->image_format) {
		CMR_LOGE("Wrong sensor formast %d", sn_mode->image_format);
		ret = -CAMERA_NOT_SUPPORTED;
	} else {
		if (SENSOR_IMAGE_FORMAT_YUV422 == sn_mode->image_format) {
			g_cxt->sn_cxt.sn_if.img_fmt = V4L2_SENSOR_FORMAT_YUV;
		} else {
			g_cxt->sn_cxt.sn_if.img_fmt = V4L2_SENSOR_FORMAT_RAWRGB;
		}
		g_cxt->sn_cxt.sn_if.if_spec.mipi.pclk = sn_mode->pclk;
	}

	return ret;
}

int camera_capture_sensor_mode(void)
{
	int                      ret = CAMERA_SUCCESS;
	SENSOR_MODE_INFO_T       *sensor_mode;

	CMR_LOGV("cap_rot %d, capture size %d %d",
		g_cxt->cap_rot,
		g_cxt->picture_size.width,
		g_cxt->picture_size.height);

	if (IMG_ROT_90 == g_cxt->cap_rot || IMG_ROT_270 == g_cxt->cap_rot) {
		g_cxt->capture_size.width  = g_cxt->picture_size.height;
		g_cxt->capture_size.height = g_cxt->picture_size.width;
	} else {
		g_cxt->capture_size.width  = g_cxt->picture_size.width;
		g_cxt->capture_size.height = g_cxt->picture_size.height;
	}
	if (1 != g_cxt->is_dv_mode) {
		ret = camera_get_sensor_capture_mode(&g_cxt->capture_size, &g_cxt->sn_cxt.capture_mode);
		if (ret) {
			CMR_LOGE("Unsupported picture size");
			ret = -CAMERA_NOT_SUPPORTED;
		}
	} else {
		g_cxt->sn_cxt.capture_mode = g_cxt->sn_cxt.preview_mode;
		CMR_LOGI("use preview mode for record.");
	}
	sensor_mode = &g_cxt->sn_cxt.sensor_info->sensor_mode_info[g_cxt->sn_cxt.capture_mode];

	if (SENSOR_IMAGE_FORMAT_YUV422 == sensor_mode->image_format) {
		g_cxt->sn_cxt.sn_if.img_fmt = V4L2_SENSOR_FORMAT_YUV;
	} else if (SENSOR_IMAGE_FORMAT_RAW == sensor_mode->image_format) {
		g_cxt->sn_cxt.sn_if.img_fmt = V4L2_SENSOR_FORMAT_RAWRGB;
	} else if (SENSOR_IMAGE_FORMAT_JPEG == sensor_mode->image_format) {
		g_cxt->sn_cxt.sn_if.img_fmt = V4L2_SENSOR_FORMAT_JPEG;
	} else {
		CMR_LOGE("Wrong sensor formast %d", sensor_mode->image_format);
		ret = -CAMERA_NOT_SUPPORTED;
	}
	g_cxt->sn_cxt.sn_if.if_spec.mipi.pclk = sensor_mode->pclk;

	g_cxt->max_size.width = g_cxt->picture_size.width;
	g_cxt->max_size.height = g_cxt->picture_size.height;

	ret = camera_capture_get_max_size(sensor_mode, &g_cxt->max_size.width, &g_cxt->max_size.height);
/*
	g_cxt->max_size.width  = MAX(g_cxt->picture_size.width,  sensor_mode->width);
	g_cxt->max_size.height = MAX(g_cxt->picture_size.height, sensor_mode->height);
*/
	return ret;
}

camera_ret_code_type camera_set_dimensions(uint16_t picture_width,
					uint16_t picture_height,
					uint16_t display_width,
					uint16_t display_height,
					camera_cb_f_type callback,
					void *client_data,
					uint32_t can_resize)
{
	int                      ret = CAMERA_SUCCESS;

	CMR_LOGV("picture %d %d, display %d %d,rot %d",
		picture_width,
		picture_height,
		display_width,
		display_height,
		g_cxt->prev_rot);

	/*g_cxt->prev_rot = IMG_ROT_90;*/
	if (picture_width && picture_height && display_width && display_height) {
		g_cxt->display_size.width  = display_width;
		g_cxt->display_size.height = display_height;
		ret = camera_preview_sensor_mode();

		if (can_resize) {
			g_cxt->picture_size.width  = CAMERA_ALIGNED_16(picture_width);
			g_cxt->picture_size.height = CAMERA_ALIGNED_16(picture_height);
		} else {
			g_cxt->picture_size.width  = picture_width;
			g_cxt->picture_size.height = picture_height;
		}

		if (g_cxt->is_cfg_rot_cap && (IMG_ROT_0 != g_cxt->cfg_cap_rot)) {
			g_cxt->actual_picture_size.width = picture_height;
			g_cxt->actual_picture_size.height = picture_width;
		} else {
			g_cxt->actual_picture_size.width = picture_width;
			g_cxt->actual_picture_size.height = picture_height;
		}

		CMR_LOGV("picture after ALIGNED_16 is %d %d picture is %d %d",
			g_cxt->picture_size.width,
			g_cxt->picture_size.height,
			g_cxt->actual_picture_size.width,
			g_cxt->actual_picture_size.height);
		g_cxt->picture_size_backup = g_cxt->picture_size;
		ret = camera_capture_sensor_mode();
	} else {
		ret = -CAMERA_INVALID_PARM;
	}

	return ret;
}

camera_ret_code_type camera_cfg_rot_cap_param_reset(void)
{
	int                      ret = CAMERA_SUCCESS;

	g_cxt->picture_size = g_cxt->picture_size_backup;
	g_cxt->cap_orig_size = g_cxt->cap_orig_size_backup;
	g_cxt->cap_rot = g_cxt->cap_rot_backup;
	g_cxt->thum_size = g_cxt->thum_size_backup;

	return ret;
}

camera_ret_code_type camera_set_encode_properties(camera_encode_properties_type *encode_properties)
{
	int                      ret = CAMERA_SUCCESS;

	if (NULL == encode_properties) {
		return CAMERA_INVALID_PARM;
	}

	CMR_LOGV("Take photo format %d",encode_properties->format);

	switch (encode_properties->format) {
	case CAMERA_RAW:
		g_cxt->cap_target_fmt = V4L2_SENSOR_FORMAT_RAWRGB;
		break;
	case CAMERA_JPEG:
		g_cxt->cap_target_fmt = V4L2_SENSOR_FORMAT_JPEG;
		break;
	case CAMERA_YCBCR_ENCODE:
		g_cxt->cap_target_fmt = V4L2_SENSOR_FORMAT_YUV;
		break;
	default:
		ret = CAMERA_INVALID_FORMAT;
		break;
	}

	return ret;
}

camera_ret_code_type camera_set_parm(camera_parm_type id,
				uint32_t         parm,
				camera_cb_f_type callback,
				void            *client_data)
{
	return camera_set_ctrl(id, parm, camera_before_set, camera_after_set);
}


camera_ret_code_type camera_set_position(camera_position_type *position,
					camera_cb_f_type      callback,
					void                 *client_data)
{
	int                      ret = CAMERA_SUCCESS;

	ret = camera_set_pos_type(position);

	return ret;
}

camera_ret_code_type camera_set_thumbnail_properties(uint32_t width,
						uint32_t height,
						uint32_t quality)
{
	(void) quality;

	CMR_LOGV("%d,%d.",g_cxt->thum_size.width,g_cxt->thum_size.height);
	/*if (0 == width || 0 == height)
			return CAMERA_INVALID_PARM;*/
	g_cxt->thum_size.width  = width;
	g_cxt->thum_size.height = height;
	g_cxt->thum_size_backup = g_cxt->thum_size;

	return CAMERA_SUCCESS;
}

camera_ret_code_type camera_start(camera_cb_f_type callback,
				void *client_data,
				int  display_height,
				int  display_width)
{
	camera_ret_code_type ret_type = CAMERA_SUCCESS;

	CMR_LOGV("OK to init_device.");
	/*change the status from INIT to IDLE.*/
	callback(CAMERA_STATUS_CB, client_data, CAMERA_FUNC_START, 0);
	CMR_LOGV("OK to change the status from INIT to IDLE.");

	return ret_type;
}

int camera_before_set_internal(enum restart_mode re_mode)
{
	int                      ret = CAMERA_SUCCESS;
	uint32_t                 sec = 0;
	uint32_t                 usec = 0;

	CMR_LOGV("restart mode %d", re_mode);
	if (re_mode >= RESTART_MAX) {
		CMR_LOGE("Wrong restart mode");
		return CAMERA_INVALID_PARM;
	}

	switch (re_mode) {
	case RESTART_HEAVY:
	case RESTART_MIDDLE:
		if (RESTART_HEAVY == re_mode) {
			g_cxt->stop_preview_mode = 1;
			ret = camera_stop_preview_internal();
			Sensor_Close();
			CMR_LOGI("id:%d.",g_cxt->sn_cxt.cur_id);
		} else {
			g_cxt->stop_preview_mode = 0;
			ret = camera_stop_preview_internal();
		}
		break;
	case RESTART_LIGHTLY:
		ret = cmr_v4l2_get_cap_time(&sec, &usec);
		g_cxt->restart_timestamp = sec * 1000000000LL + usec * 1000;
		break;
	case RESTART_ZOOM:
		if (IS_CHN_BUSY(CHN_2)) {
			ret = cmr_v4l2_cap_pause(CHN_2, 1);
			SET_CHN_IDLE(CHN_2);
		}
		break;
	default:
		break;
	}
	return ret;
}

int camera_before_set(enum restart_mode re_mode)
{
	CMR_MSG_INIT(message);
	int                      ret = CAMERA_SUCCESS;

	CMR_LOGV("before_set");

	message.msg_type = CMR_EVT_BEFORE_SET;
	message.sub_msg_type = re_mode;
	ret = cmr_msg_post(g_cxt->msg_queue_handle, &message);
	if (ret) {
		CMR_LOGE("Fail to send message to camera main thread");
		return ret;
	}

	ret = camera_wait_stop(g_cxt);
	return ret;
}

int camera_after_set_internal(enum restart_mode re_mode)
{
	int                      ret = CAMERA_SUCCESS;
	uint32_t                 skip_number_l = 0, sensor_num = 0;

	CMR_LOGV("after set %d, skip mode %d, skip number %d",
		re_mode,
		g_cxt->skip_mode,
		g_cxt->skip_num);

	if (IMG_SKIP_HW == g_cxt->skip_mode) {
		skip_number_l = g_cxt->skip_num;
	}

	switch (re_mode) {
	case RESTART_HEAVY:
		ret = Sensor_Init(g_cxt->sn_cxt.cur_id, &sensor_num);
		if (ret) {
			CMR_LOGE("Failed to init sensor");
			return -CAMERA_FAILED;
		}
		Sensor_EventReg(camera_sensor_evt_cb);
		ret  = camera_start_preview_internal();
		break;
	case RESTART_MIDDLE:
		/*ret = camera_preview_init(g_cxt->preview_fmt);
		if (ret) {
			CMR_LOGE("Failed to restart preview");
			return -CAMERA_FAILED;
		}
		ret = cmr_v4l2_cap_start(skip_number_l);
		if (ret) {
			CMR_LOGE("Failed to restart preview");
			return -CAMERA_FAILED;
		} else {
			ret = Sensor_StreamOn();
			if (ret) {
				CMR_LOGE("Fail to switch on the sensor stream");
				return -CAMERA_FAILED;
			}
			g_cxt->v4l2_cxt.v4l2_state = V4L2_PREVIEW;
			g_cxt->preview_status = CMR_PREVIEW;
		}*/
		ret  = camera_start_preview_internal();
		break;
	case RESTART_LIGHTLY:
		pthread_mutex_lock(&g_cxt->main_prev_mutex);
		g_cxt->restart_skip_cnt=0;
		g_cxt->restart_skip_en = 1;
		pthread_mutex_unlock(&g_cxt->main_prev_mutex);
		break;
	case RESTART_ZOOM:

		ret = camera_preview_weak_init(g_cxt->preview_fmt);
		if (ret) {
			if (CMR_V4L2_RET_RESTART == ret) {
				g_cxt->stop_preview_mode = 1;
				ret = camera_stop_preview_internal();
				if (ret) {
					CMR_LOGE("Failed to stop preview");
					return -CAMERA_FAILED;
				}
				g_cxt->prev_self_restart = 1;
				ret  = camera_start_preview_internal();
				if (ret) {
					CMR_LOGE("Failed to restart preview");
					return -CAMERA_FAILED;
				}
				g_cxt->prev_self_restart = 0;
				return 0;
			} else {
				CMR_LOGE("Failed to init preview when preview");
				return -CAMERA_FAILED;
			}
		}
		CMR_LOGV("cap_mode %d", g_cxt->cap_mode);
		if (IS_ZSL_MODE(g_cxt->cap_mode)) {
			ret = camera_capture_init();
			if (ret) {
				CMR_LOGE("Failed to init capture when preview");
				return -CAMERA_FAILED;
			}
			SET_CHN_BUSY(CHN_2);
			ret = cmr_v4l2_cap_resume(CHN_2,
					skip_number_l,
					g_cxt->v4l2_cxt.chn_frm_deci[CHN_2],
					-1);
			if (ret) {
				SET_CHN_IDLE(CHN_2);
				CMR_LOGE("error.");
			}
		}
		break;
	default:
		CMR_LOGE("Wrong re-start mode");
		ret = -CAMERA_INVALID_PARM;
		break;
	}

	CMR_LOGV("Exit");

	return ret;
}

int camera_after_set(enum restart_mode re_mode,
			enum img_skip_mode skip_mode,
			uint32_t skip_number)
{
	CMR_MSG_INIT(message);
	int                      ret = CAMERA_SUCCESS;

	CMR_LOGV("after_set");
	if (skip_mode > IMG_SKIP_SW) {
		CMR_LOGE("Wrong skip mode");
		return -CAMERA_FAILED;
	}

	g_cxt->skip_mode = skip_mode;
	g_cxt->skip_num  = skip_number;

	message.msg_type = CMR_EVT_AFTER_SET;
	message.sub_msg_type = re_mode;
	ret = cmr_msg_post(g_cxt->msg_queue_handle, &message);
	if (ret) {
		CMR_LOGE("Fail to send message to camera main thread");
		return ret;
	}

	ret = camera_wait_start(g_cxt);
	g_cxt->set_flag ++;
	/*ret = camera_wait_set(g_cxt);*/

	return ret;
}

int camera_start_preview_internal(void)
{
	uint32_t                 skip_number = 0;
	int                      ret = CAMERA_SUCCESS;
	uint32_t                 sensor_mode = SENSOR_MODE_MAX;
	SENSOR_MODE_INFO_T       *sensor_if_mode = NULL;
	uint32_t                 isp_param = 1;

	CMR_LOGV("preview format is %d", g_cxt->preview_fmt);

	ret = arithmetic_fd_init();
	if (ret) {
		CMR_LOGE("Failed to init arithmetic %d", ret);
	}


	CMR_LOGV("previous mode %d, img_fmt= %d",\
		g_cxt->sn_cxt.previous_sensor_mode, g_cxt->sn_cxt.sn_if.img_fmt);

	if (SENSOR_MODE_MAX == g_cxt->sn_cxt.previous_sensor_mode) {
		ret = cmr_v4l2_if_cfg(&g_cxt->sn_cxt.sn_if);
		if (ret) {
			CMR_LOGE("the sensor interface is unsupported by V4L2");
			return -CAMERA_FAILED;
		}
	} else {
		CMR_LOGV("preview mode %d",g_cxt->sn_cxt.preview_mode);
		if (g_cxt->sn_cxt.previous_sensor_mode != g_cxt->sn_cxt.preview_mode) {
			ret = Sensor_StreamOff();
			if (ret) {
				CMR_LOGE("Failed to switch off the sensor stream");
				return -CAMERA_FAILED;
			}
			ret = cmr_v4l2_if_decfg(&g_cxt->sn_cxt.sn_if);
			if (ret) {
				CMR_LOGE("Failed to close sensor interface");
			}
			usleep(100);

			sensor_if_mode = &g_cxt->sn_cxt.sensor_info->sensor_mode_info[g_cxt->sn_cxt.preview_mode];
			if (SENSOR_IMAGE_FORMAT_YUV422 == sensor_if_mode->image_format) {
				g_cxt->sn_cxt.sn_if.img_fmt = V4L2_SENSOR_FORMAT_YUV;
			} else if (SENSOR_IMAGE_FORMAT_RAW == sensor_if_mode->image_format) {
				g_cxt->sn_cxt.sn_if.img_fmt = V4L2_SENSOR_FORMAT_RAWRGB;
			} else {
				CMR_LOGE("Wrong sensor formast %d", sensor_if_mode->image_format);
				ret = -CAMERA_NOT_SUPPORTED;
			}
			g_cxt->sn_cxt.sn_if.if_spec.mipi.pclk = sensor_if_mode->pclk;
			ret = cmr_v4l2_if_cfg(&g_cxt->sn_cxt.sn_if);
			if (ret) {
				CMR_LOGE("the sensor interface is unsupported by V4L2");
				return -CAMERA_FAILED;
			}
		}
	}

	CMR_PRINT_TIME;
	ret = camera_preview_start_set();
	if (ret) {
		CMR_LOGE("Failed to set sensor preview mode");
		return -CAMERA_FAILED;
	}
#if 1
	if (V4L2_SENSOR_FORMAT_RAWRGB == g_cxt->sn_cxt.sn_if.img_fmt) {
		camera_isp_ae_stab_set(1);
		camera_isp_get_ae_stab(&isp_param);
	}
#endif
	CMR_PRINT_TIME;
	ret = camera_preview_init(g_cxt->preview_fmt);
	if (ret) {
		CMR_LOGE("Failed to init preview mode");
		return -CAMERA_FAILED;
	}

	/*g_cxt->total_capture_num = CAMERA_NORMAL_CAP_FRM_CNT;*/
	if (IS_ZSL_MODE(g_cxt->cap_mode)) {
		g_cxt->total_cap_num = CAMERA_CAP_FRM_CNT;
		ret = camera_capture_init();
	} else {
		g_cxt->total_cap_num = CAMERA_NORMAL_CAP_FRM_CNT;
	}

	if (IMG_SKIP_HW == g_cxt->skip_mode) {
		skip_number = g_cxt->skip_num;
	}

	ret = cmr_v4l2_cap_start(skip_number);
	if (ret) {
		CMR_LOGE("Fail to start V4L2 Capture");
		return -CAMERA_FAILED;
	}
	if (SENSOR_MODE_MAX == g_cxt->sn_cxt.previous_sensor_mode) {
		ret = Sensor_StreamOn();
		if (ret) {
			CMR_LOGE("Fail to switch on the sensor stream");
			return -CAMERA_FAILED;
		}
	} else {
		ret = Sensor_GetMode(&sensor_mode);
		if (ret) {
			CMR_LOGE("Fail to switch on the sensor stream");
			return -CAMERA_FAILED;
		} else {
			if (sensor_mode != g_cxt->sn_cxt.previous_sensor_mode) {
				ret = Sensor_StreamOn();
				if (ret) {
					CMR_LOGE("Fail to switch on the sensor stream");
					return -CAMERA_FAILED;
				}
			}
		}
	}

	g_cxt->v4l2_cxt.v4l2_state = V4L2_PREVIEW;
	g_cxt->preview_status = CMR_PREVIEW;
	ret = camera_af_init();
	if (ret) {
		CMR_LOGE("Fail to initialize AF");
	}

	return 0;
}

camera_ret_code_type camera_start_preview(camera_cb_f_type callback,
					void *client_data,takepicture_mode mode)
{
	CMR_MSG_INIT(message);
	int          ret = CAMERA_SUCCESS;

	CMR_LOGV("start preview");
	CMR_PRINT_TIME;
	camera_set_client_data(client_data);
	camera_set_hal_cb(callback);

	g_cxt->err_code = 0;
	g_cxt->recover_status = NO_RECOVERY;
	g_cxt->cmr_set.bflash = 1;
	g_cxt->cap_mode = mode;
	message.msg_type = CMR_EVT_START;
	message.sub_msg_type = CMR_PREVIEW;
	ret = cmr_msg_post(g_cxt->msg_queue_handle, &message);
	if (ret) {
		CMR_LOGE("Fail to send message to camera main thread");
		return ret;
	}

	ret = camera_wait_start(g_cxt);

	CMR_LOGV("start preview finished... %d", g_cxt->err_code);
	return g_cxt->err_code;
}

int camera_stop_capture_raw_internal(void)
{
	int                      ret = CAMERA_SUCCESS;
	uint32_t		  autoflash = 0;

	CMR_LOGV("preview_status=%d, cap_status=%d,  cap_raw_status=%d \n",
		g_cxt->preview_status, g_cxt->capture_status, g_cxt->capture_raw_status);

	if (CMR_IDLE == g_cxt->capture_raw_status) {
		CMR_LOGE("Not in capture, capture_raw_status=%d \n", g_cxt->capture_raw_status);
		return ret;
	}

	pthread_mutex_lock(&g_cxt->take_raw_mutex);
	CMR_PRINT_TIME;

	g_cxt->capture_raw_status = CMR_IDLE;
	g_cxt->chn_0_status       = CHN_IDLE;

	g_cxt->pre_frm_cnt = 0;
	g_cxt->restart_skip_cnt = 0;
	g_cxt->restart_skip_en = 0;
	if (CMR_IDLE == g_cxt->preview_status) {
		ret = cmr_v4l2_cap_stop();
		g_cxt->v4l2_cxt.v4l2_state = V4L2_IDLE;
		if (ret) {
			CMR_LOGE("Failed to stop V4L2 capture, %d", ret);
		}
		g_cxt->sn_cxt.previous_sensor_mode = SENSOR_MODE_MAX;
		ret = Sensor_StreamOff();
		if (ret) {
			CMR_LOGE("Failed to switch off the sensor stream, %d", ret);
		}
		ret = cmr_v4l2_if_decfg(&g_cxt->sn_cxt.sn_if);
		if (ret) {
			CMR_LOGE("Failed to stop IF , %d", ret);
			return -CAMERA_FAILED;
		}

		CMR_PRINT_TIME;
	}

	pthread_mutex_unlock(&g_cxt->take_raw_mutex);
	CMR_PRINT_TIME;

	return ret;
}

static void _camera_autofocus_stop_handle(void)
{
	int                    status = CAMERA_SUCCESS;
	status = pthread_mutex_trylock(&g_cxt->af_cb_mutex);
	if (EBUSY != status) {
		pthread_mutex_unlock(&g_cxt->af_cb_mutex);
	} else {
		camera_autofocus_quit();
		pthread_mutex_lock(&g_cxt->af_cb_mutex);
		pthread_mutex_unlock(&g_cxt->af_cb_mutex);
	}
}

int camera_stop_preview_internal(void)
{
	int                      ret = CAMERA_SUCCESS;
	uint32_t		         autoflash = 0;

	if (!IS_PREVIEW) {
		CMR_LOGE("Not in preview, %d", g_cxt->preview_status);
		return ret;
	}
	CMR_LOGV("preview_status=%d, cap_status=%d \n",
		g_cxt->preview_status, g_cxt->capture_status);

	pthread_mutex_lock(&g_cxt->prev_mutex);
	CMR_PRINT_TIME;

	g_cxt->preview_status = CMR_IDLE;

	camera_autofocus_stop(0);

	g_cxt->chn_1_status   = CHN_IDLE;
	SET_CHN_IDLE(CHN_1);
	CMR_PRINT_TIME;

	g_cxt->pre_frm_cnt = 0;
	g_cxt->restart_skip_cnt = 0;
	g_cxt->restart_skip_en = 0;

	if (V4L2_PREVIEW == g_cxt->v4l2_cxt.v4l2_state) {
		ret = cmr_v4l2_cap_stop();
		g_cxt->v4l2_cxt.v4l2_state = V4L2_IDLE;
		if (ret) {
			CMR_LOGE("Failed to stop V4L2 capture, %d", ret);
		}

		if(g_cxt->cmr_set.bflash){
			ret = Sensor_Ioctl(SENSOR_IOCTL_FLASH, (uint32_t)&autoflash);
			if (ret) {
				g_cxt->cmr_set.auto_flash = 1;
				CMR_LOGE("Failed to read auto flash mode, %d", ret);
			} else {
				g_cxt->cmr_set.auto_flash = autoflash;
				g_cxt->cmr_set.bflash = 0;
			}
		}
	}
	CMR_PRINT_TIME;

	ret = camera_ae_enable(0);
	if (ret) {
		CMR_LOGE("ae disable fail %d", ret);
	}
	if (0 == g_cxt->stop_preview_mode) {
		ret = Sensor_GetMode(&g_cxt->sn_cxt.previous_sensor_mode);
		if (ret) {
			g_cxt->sn_cxt.previous_sensor_mode = SENSOR_MODE_MAX;
			ret = Sensor_StreamOff();
			if (ret) {
				CMR_LOGE("Failed to switch off the sensor stream, %d", ret);
			}
			ret = cmr_v4l2_if_decfg(&g_cxt->sn_cxt.sn_if);
			if (ret) {
				CMR_LOGE("Failed to stop IF , %d", ret);
				pthread_mutex_unlock(&g_cxt->prev_mutex);
				return -CAMERA_FAILED;
			}
			CMR_LOGE("get sensor mode fail.");
		} else {
			CMR_LOGI("get sensor mode is %d.",g_cxt->sn_cxt.previous_sensor_mode);
		}
	} else {
		g_cxt->sn_cxt.previous_sensor_mode = SENSOR_MODE_MAX;
		ret = Sensor_StreamOff();
		if (ret) {
			CMR_LOGE("Failed to switch off the sensor stream, %d", ret);
		}
		ret = cmr_v4l2_if_decfg(&g_cxt->sn_cxt.sn_if);
		if (ret) {
			CMR_LOGE("Failed to stop IF , %d", ret);
			pthread_mutex_unlock(&g_cxt->prev_mutex);
			return -CAMERA_FAILED;
		}
	}
	pthread_mutex_unlock(&g_cxt->prev_mutex);
	CMR_PRINT_TIME;

	if (ISP_COWORK == g_cxt->isp_cxt.isp_state) {
		ret = isp_video_stop();
		g_cxt->isp_cxt.isp_state = ISP_IDLE;
		if (ret) {
			CMR_LOGE("Failed to stop ISP video mode, %d", ret);
		}
	}

	cmr_rot_wait_done();

	g_cxt->rot_cxt.rot_state = IMG_CVT_ROT_DONE;
	/*arithmetic_hdr_deinit();*/

	return ret;
}

int camera_flush_msg_queue(void)
{
	CMR_MSG_INIT(message);
	int                      ret = CAMERA_SUCCESS;
	int                      cnt = 0;

	while (CMR_MSG_SUCCESS == ret) {
		ret = cmr_msg_peak(g_cxt->msg_queue_handle, &message);
		if (ret) {
			CMR_LOGV("no more msg");
			break;
		}

		if (message.alloc_flag) {
			free(message.data);
		}
		cnt ++;
	}

	CMR_LOGV("release count %d", cnt);
	return ret;
}

camera_ret_code_type camera_stop_preview(void)
{
	CMR_MSG_INIT(message);
	int                      ret = CAMERA_SUCCESS;
#if FREE_PMEM_BAK_OEM
#else
	pthread_mutex_lock(&g_cxt->cb_mutex);
	pthread_mutex_unlock(&g_cxt->cb_mutex);
#endif
	CMR_PRINT_TIME;
	/*camera_flush_msg_queue();*/
	message.msg_type = CMR_EVT_STOP;
	message.sub_msg_type = CMR_PREVIEW;
	ret = cmr_msg_post(g_cxt->msg_queue_handle, &message);
	if (ret) {
		CMR_LOGE("Fail to send message to camera main thread");
		return ret;
	}

	camera_wait_stop(g_cxt);
	CMR_PRINT_TIME;
	CMR_LOGV("stop preview... %d", ret);

	return ret;
}

int camera_take_picture_done(struct frm_info *data)
{
	camera_frame_type        frame_type;
	int                      ret = CAMERA_SUCCESS;
	camera_cb_info           cb_info;

	memset(&frame_type,0,sizeof(frame_type));

	TAKE_PIC_CANCEL;
	CMR_PRINT_TIME;
	ret = camera_set_frame_type(&frame_type, data);
	if (CAMERA_SUCCESS == ret) {
		camera_call_cb(CAMERA_EVT_CB_SNAPSHOT_DONE,
				camera_get_client_data(),
				CAMERA_FUNC_TAKE_PICTURE,
				(uint32_t)&frame_type);
		CMR_LOGE("CAMERA_EVT_CB_SNAPSHOT_DONE.");

		camera_call_cb(CAMERA_EXIT_CB_DONE,
				camera_get_client_data(),
				CAMERA_FUNC_TAKE_PICTURE,
				(uint32_t)&frame_type);
		CMR_LOGE("CAMERA_EXIT_CB_DONE.");

	} else {
		memset(&cb_info, 0, sizeof(camera_cb_info));
		cb_info.cb_type = CAMERA_EXIT_CB_FAILED;
		cb_info.cb_func = CAMERA_FUNC_TAKE_PICTURE;
		camera_callback_start(&cb_info);
	}
	camera_takepic_callback_done(g_cxt);
	return ret;
}

int camera_capture_init_internal(takepicture_mode cap_mode)
{
	uint32_t                 skip_number = 0;
	int                      ret = CAMERA_SUCCESS;

	if (CMR_IDLE != g_cxt->capture_status) {
		CMR_LOGV("capture_status=%d is not idle, no need to prepare capture \n",
					g_cxt->capture_status);
		return ret;
	}

	g_cxt->cap_mode = cap_mode;
	camera_set_cancel_capture(0);
	ret = camera_capture_init();
	if (ret) {
		CMR_LOGE("Failed to init capture mode.");
		return -CAMERA_FAILED;
	}

	if (IMG_SKIP_HW == g_cxt->skip_mode) {
		skip_number = g_cxt->sn_cxt.sensor_info->capture_skip_num;
	}
	CMR_PRINT_TIME;
	ret = cmr_v4l2_cap_start(skip_number);
	if (ret) {
		CMR_LOGE("Fail to start V4L2 Capture");
		return -CAMERA_FAILED;
	}
	g_cxt->v4l2_cxt.v4l2_state = V4L2_PREVIEW;
	CMR_PRINT_TIME;

	if (CHN_IDLE == g_cxt->chn_1_status) {
			ret = Sensor_StreamOn();
		if (ret) {
			CMR_LOGE("Fail to switch on the sensor stream");
			return -CAMERA_FAILED;
		}
	}

	g_cxt->capture_status = CMR_CAPTURE_PREPARE;

	return ret;
}


int camera_take_picture_internal(takepicture_mode cap_mode)
{
	uint32_t                 skip_number = 0;
	int                      ret = CAMERA_SUCCESS;
	uint32_t                 sensor_mode = 0;

	g_cxt->cap_mode = cap_mode;
	camera_set_cancel_capture(0);

	CMR_LOGI("previous_sensor_mode is %d.is_reset_if_cfg=%d",g_cxt->sn_cxt.previous_sensor_mode,g_cxt->is_reset_if_cfg);

	if (g_cxt->sn_cxt.previous_sensor_mode == SENSOR_MODE_MAX) {
		ret = cmr_v4l2_if_cfg(&g_cxt->sn_cxt.sn_if);
		if (ret) {
			CMR_LOGE("the sensor interface is unsupported by V4L2");
			return -CAMERA_FAILED;
		}
	} else {
		ret = Sensor_GetMode(&sensor_mode);
		if (ret) {
			CMR_LOGE("Fail to switch on the sensor stream");
			return -CAMERA_FAILED;
		} else {
			if (sensor_mode != g_cxt->sn_cxt.capture_mode
				|| g_cxt->is_reset_if_cfg) {
				g_cxt->is_reset_if_cfg = 0;

				ret = Sensor_StreamOff();
				if (ret) {
					CMR_LOGE("Fail to switch off the sensor stream");
					return -CAMERA_FAILED;
				}
				ret = cmr_v4l2_if_decfg(&g_cxt->sn_cxt.sn_if);
				if (ret) {
					CMR_LOGE("the senso interface is unsupported by V4L2");
					return -CAMERA_FAILED;
				}
				g_cxt->sn_cxt.previous_sensor_mode = SENSOR_MODE_MAX;
				usleep(100);
				ret = cmr_v4l2_if_cfg(&g_cxt->sn_cxt.sn_if);
				if (ret) {
					CMR_LOGE("the sensor interface is unsupported by V4L2");
					return -CAMERA_FAILED;
				}
			}
		}
	}
	ret = camera_capture_init();
	if (ret) {
		CMR_LOGE("Failed to init raw capture mode.");
		return -CAMERA_FAILED;
	}

	ret = camera_snapshot_start_set();
	if (ret) {
		CMR_LOGE("Failed to snapshot");
		return -CAMERA_FAILED;
	}

	if (CAMERA_HDR_MODE == cap_mode) {
		ret = arithmetic_hdr_init(g_cxt->cap_orig_size.width,g_cxt->cap_orig_size.height);
		if (ret) {
			CMR_LOGE("malloc fail for hdr.");
			return -CAMERA_FAILED;
		}
	}

	if (V4L2_SENSOR_FORMAT_RAWRGB != g_cxt->sn_cxt.sn_if.img_fmt) {
		g_cxt->skip_mode      = IMG_SKIP_HW;
	}

	if (IMG_SKIP_HW == g_cxt->skip_mode) {
		skip_number = g_cxt->sn_cxt.sensor_info->capture_skip_num;
	}

	CMR_PRINT_TIME;
	ret = cmr_v4l2_cap_start(skip_number);
	if (ret) {
		CMR_LOGE("Fail to start V4L2 Capture");
		return -CAMERA_FAILED;
	}
	g_cxt->v4l2_cxt.v4l2_state = V4L2_PREVIEW;
	CMR_PRINT_TIME;
	if (g_cxt->sn_cxt.previous_sensor_mode == SENSOR_MODE_MAX) {
		ret = Sensor_StreamOn();
		if (ret) {
			CMR_LOGE("Fail to switch on the sensor stream");
			return -CAMERA_FAILED;
		}
	} else {
		ret = Sensor_GetMode(&sensor_mode);
		if (ret) {
			CMR_LOGE("Fail to switch on the sensor stream");
			return -CAMERA_FAILED;
		} else {
			if (sensor_mode != g_cxt->sn_cxt.previous_sensor_mode) {
				ret = Sensor_StreamOn();
				if (ret) {
					CMR_LOGE("Fail to switch on the sensor stream");
					return -CAMERA_FAILED;
				}
			}
		}
	}

	if ((CAMERA_RAW_MODE == cap_mode) || (CAMERA_TOOL_RAW_MODE == cap_mode)) {
		g_cxt->capture_raw_status = CMR_CAPTURE;
	} else if (IS_NON_ZSL_MODE(cap_mode)) {
		g_cxt->capture_status = CMR_CAPTURE;
	}

	return ret;
}

int camera_take_picture_internal_raw(takepicture_mode cap_mode)
{
	uint32_t                 skip_number = 0;
	int                      ret = CAMERA_SUCCESS;
	uint32_t                 sensor_mode = 0;

	if (CMR_IDLE != g_cxt->capture_raw_status) {
		CMR_LOGV("capture_raw_status=%d is not idle, no need to take capture \n",
					g_cxt->capture_raw_status);
		return ret;
	}

	g_cxt->cap_mode = cap_mode;
	camera_set_cancel_capture(0);

	if ((0 != g_cxt->sn_cxt.sn_if.if_type) && (CAMERA_TOOL_RAW_MODE == cap_mode)) {
		/* if mipi, set to half word for capture raw */
		g_cxt->sn_cxt.sn_if.if_spec.mipi.is_loose = 1;
	}
	ret = Sensor_GetMode(&sensor_mode);
	if (ret) {
		CMR_LOGE("Fail to switch on the sensor stream");
		return -CAMERA_FAILED;
	} else {
		if (sensor_mode != g_cxt->sn_cxt.capture_mode) {
			ret = Sensor_StreamOff();
			if (ret) {
				CMR_LOGE("Fail to switch off the sensor stream");
				return -CAMERA_FAILED;
			}
			ret = cmr_v4l2_if_decfg(&g_cxt->sn_cxt.sn_if);
			if (ret) {
				CMR_LOGE("the senso interface is unsupported by V4L2");
				return -CAMERA_FAILED;
			}
			CMR_LOGV("Sensor workmode %d", g_cxt->sn_cxt.capture_mode);
			ret = Sensor_SetMode(g_cxt->sn_cxt.capture_mode);
			if (ret) {
				CMR_LOGE("Sensor can't work at this mode %d", g_cxt->sn_cxt.capture_mode);
				return -CAMERA_FAILED;
			}
			usleep(100);
			ret = cmr_v4l2_if_cfg(&g_cxt->sn_cxt.sn_if);
			if (ret) {
				CMR_LOGE("the sensor interface is unsupported by V4L2");
				return -CAMERA_FAILED;
			}
		}
	}



	ret = camera_capture_init_raw();
	if (ret) {
		CMR_LOGE("Failed to init raw capture mode.");
		return -CAMERA_FAILED;
	}

	if (IMG_SKIP_HW == g_cxt->skip_mode) {
		skip_number = g_cxt->sn_cxt.sensor_info->capture_skip_num;
	}
	CMR_PRINT_TIME;
	ret = cmr_v4l2_cap_start(skip_number);
	if (ret) {
		CMR_LOGE("Fail to start V4L2 Capture");
		return -CAMERA_FAILED;
	}
	g_cxt->v4l2_cxt.v4l2_state = V4L2_PREVIEW;
	CMR_PRINT_TIME;

	ret = Sensor_StreamOn();
	if (ret) {
		CMR_LOGE("Fail to switch on the sensor stream");
		return -CAMERA_FAILED;
	}

	g_cxt->capture_raw_status = CMR_CAPTURE;

	return ret;
}

int camera_set_take_picture(int set_val)
{
	CMR_LOGV("in");
	pthread_mutex_lock(&g_cxt->take_mutex);
	g_cxt->is_take_picture = set_val;
	g_cxt->hdr_cnt = 0;
	pthread_mutex_unlock(&g_cxt->take_mutex);
	if (TAKE_PICTURE_NEEDED == set_val) {
		camera_set_cancel_capture(0);
	} else {
		camera_set_cancel_capture(1);
	}
	CMR_LOGV("out");

	return CAMERA_SUCCESS;
}

int camera_set_take_picture_cap_mode(takepicture_mode cap_mode)
{
	pthread_mutex_lock(&g_cxt->take_mutex);
	g_cxt->cap_mode = cap_mode;/*CAMERA_NORMAL_MODE;*/
	g_cxt->hdr_cnt = 0;
	g_cxt->cap_cnt = 0;
	g_cxt->cap_cnt_for_err = 0;
	pthread_mutex_unlock(&g_cxt->take_mutex);
	CMR_LOGE("cap mode is %d.",cap_mode);
	return CAMERA_SUCCESS;
}

int camera_get_take_picture(void)
{
	int                      ret = 0;

	pthread_mutex_lock(&g_cxt->take_mutex);
	ret = g_cxt->is_take_picture;
	pthread_mutex_unlock(&g_cxt->take_mutex);

	return ret;
}


camera_ret_code_type camera_take_picture(camera_cb_f_type    callback,
					void                 *client_data,takepicture_mode cap_mode)
{
	CMR_MSG_INIT(message);
	int                      ret = CAMERA_SUCCESS;

	TAKE_PICTURE_STEP(CMR_STEP_TAKE_PIC);
	CMR_LOGI("start");
	if (IS_ZSL_MODE(cap_mode) || (CAMERA_RAW_MODE == cap_mode)) {
		camera_preflash();
	}

	pthread_mutex_lock(&g_cxt->recover_mutex);
	camera_set_client_data(client_data);
	camera_set_hal_cb(callback);
	camera_set_take_picture_cap_mode(cap_mode);
	camera_set_take_picture(TAKE_PICTURE_NEEDED);
	pthread_mutex_unlock(&g_cxt->recover_mutex);

	if (IS_ZSL_MODE(cap_mode) || (CAMERA_RAW_MODE == cap_mode)) {
		camera_snapshot_start_set();
	}

	if (IS_NON_ZSL_MODE(cap_mode)) {
		message.msg_type = CMR_EVT_START;
		message.sub_msg_type = CMR_CAPTURE;
		message.data = (void*)cap_mode;
		ret = cmr_msg_post(g_cxt->msg_queue_handle, &message);
		if (ret) {
			CMR_LOGE("Fail to send message to camera main thread");
			return ret;
		}
		ret = camera_wait_start(g_cxt);
		if (CAMERA_SUCCESS == ret) {
			ret = g_cxt->err_code;
		}
	} else {
		if ((TAKE_PICTURE_NEEDED == camera_get_take_picture()) && IS_CHN_BUSY(CHN_2)) {
			g_cxt->capture_status = CMR_CAPTURE;
			g_cxt->chn_2_status = CHN_BUSY;
			message.msg_type = CMR_EVT_BEFORE_CAPTURE;
			message.alloc_flag = 0;
			ret = cmr_msg_post(g_cxt->msg_queue_handle, &message);
			CMR_LOGI("send post.");
		}
	}
	return ret;
}

camera_ret_code_type camera_take_picture_raw(camera_cb_f_type    callback,
					void                 *client_data,takepicture_mode cap_mode)
{
	CMR_MSG_INIT(message);
	int                      ret = CAMERA_SUCCESS;

	camera_set_client_data(client_data);
	camera_set_hal_cb(callback);

	g_cxt->err_code = 0;
	message.msg_type = CMR_EVT_START;
	message.sub_msg_type = CMR_CAPTURE;
	message.data = (void *)CAMERA_TOOL_RAW_MODE;
	ret = cmr_msg_post(g_cxt->msg_queue_handle, &message);
	if (ret) {
		CMR_LOGE("Fail to send message to camera main thread");
		return ret;
	}

	ret = camera_wait_start(g_cxt);
	if (CAMERA_SUCCESS == ret) {
		ret = g_cxt->err_code;
	}
	return ret;
}

int camera_take_picture_hdr(int cap_cnt)
{
	int                      preview_format;
	uint32_t                 skip_number = 0;
	int                      ret = CAMERA_SUCCESS;
	struct buffer_cfg        buffer_info;

	CMR_LOGI("start.");

#if USE_SENSOR_OFF_ON_FOR_HDR
	ret = cmr_v4l2_if_cfg(&g_cxt->sn_cxt.sn_if);
	if (ret) {
		CMR_LOGE("the sensor interface is unsupported by V4L2");
		return -CAMERA_FAILED;
	}
#endif
	if ((HDR_CAP_NUM-2) == cap_cnt) {
		camera_set_hdr_ev(SENSOR_HDR_EV_LEVE_1);
	} else {
		camera_set_hdr_ev(SENSOR_HDR_EV_LEVE_2);
	}

	ret = camera_capture_init();
	if (ret) {
		CMR_LOGE("Failed to init raw capture mode.");
		return -CAMERA_FAILED;
	}

	if (V4L2_SENSOR_FORMAT_RAWRGB != g_cxt->sn_cxt.sn_if.img_fmt) {
		g_cxt->skip_mode = IMG_SKIP_HW;
	}

	if (IMG_SKIP_HW == g_cxt->skip_mode) {
		skip_number = g_cxt->sn_cxt.sensor_info->capture_skip_num;
	}

	CMR_PRINT_TIME;
	skip_number = 1;
	ret = cmr_v4l2_cap_start(skip_number);
	if (ret) {
		CMR_LOGE("Fail to start V4L2 Capture");
		return -CAMERA_FAILED;
	}
	g_cxt->v4l2_cxt.v4l2_state = V4L2_PREVIEW;
	CMR_PRINT_TIME;
#if USE_SENSOR_OFF_ON_FOR_HDR
	ret = Sensor_StreamOn();
	if (ret) {
		CMR_LOGE("Fail to switch on the sensor stream");
		return -CAMERA_FAILED;
	}
#endif
	g_cxt->capture_raw_status = CMR_CAPTURE;
	return ret;
}

int camera_stop_capture_internal(void)
{
	int                      ret = CAMERA_SUCCESS;

	if (CMR_IDLE == g_cxt->preview_status) {
		ret = cmr_v4l2_cap_stop();
		g_cxt->v4l2_cxt.v4l2_state = V4L2_IDLE;
		if (ret) {
			CMR_LOGE("Failed to stop V4L2 capture, %d", ret);
		}
		g_cxt->sn_cxt.previous_sensor_mode = SENSOR_MODE_MAX;
		ret = Sensor_StreamOff();
		if (ret) {
			CMR_LOGE("Failed to switch off the sensor stream, %d", ret);
		}
		ret = cmr_v4l2_if_decfg(&g_cxt->sn_cxt.sn_if);
		if (ret) {
			CMR_LOGE("Failed to stop IF , %d", ret);
			return -CAMERA_FAILED;
		}
		CMR_PRINT_TIME;
	}

	if((JPEG_ENCODE == g_cxt->jpeg_cxt.jpeg_state)
		|| JPEG_DECODE == g_cxt->jpeg_cxt.jpeg_state) {
		if (0 != g_cxt->jpeg_cxt.handle) {
			jpeg_stop(g_cxt->jpeg_cxt.handle);
			g_cxt->jpeg_cxt.handle = 0;
		} else {
			CMR_LOGI("don't need stop jpeg.");
		}
	}
	arithmetic_hdr_deinit();
	return ret;
}

camera_ret_code_type camera_stop_capture(void)
{
	CMR_MSG_INIT(message);
	int                      ret = CAMERA_SUCCESS;

	CMR_LOGV("stop capture.");
	ret = camera_set_take_picture(TAKE_PICTURE_NO);

	message.msg_type = CMR_EVT_STOP;
	message.sub_msg_type = CMR_CAPTURE;
	ret = cmr_msg_post(g_cxt->msg_queue_handle, &message);

	if (ret) {
		CMR_LOGE("Fail to send message to camera main thread");
		return ret;
	}

	camera_wait_stop(g_cxt);

	return ret;
}

camera_ret_code_type camera_cancel_autofocus(void)
{
	int                      ret = CAMERA_SUCCESS;

	ret = camera_autofocus_stop(1);
	return ret;
}

uint32_t camera_get_size_align_page(uint32_t size)
{
	return size;
	uint32_t buffer_size, page_size;

	page_size = getpagesize();
	buffer_size = size;
	buffer_size = (buffer_size + page_size - 1) & ~(page_size - 1);

	return buffer_size;
}

int camera_start_autofocus(camera_focus_e_type focus,
			camera_cb_f_type callback,
			void *client_data)
{
	CMR_MSG_INIT(message);
	int                      ret = CAMERA_SUCCESS;

	CMR_LOGV("focus %d, client_data 0x%x", focus, (uint32_t)client_data);
	CMR_PRINT_TIME;
	if (NULL == callback) {
		CMR_LOGE("NULL callback pointer");
		return CAMERA_INVALID_PARM;
	}
	camera_set_client_data(client_data);
	camera_autofocus();

	message.msg_type = CMR_EVT_AF_START;
	message.data     = callback;
	ret = cmr_msg_post(g_cxt->af_msg_que_handle, &message);
	if (ret) {
		CMR_LOGE("Faile to send one msg to camera main thread");
	}
	CMR_PRINT_TIME;

	return ret;
}

int camera_af_init(void)
{
	CMR_MSG_INIT(message);
	int                      ret = CAMERA_SUCCESS;
	pthread_attr_t           attr;

	CMR_LOGV("inited, %d", g_cxt->af_inited);

	CMR_PRINT_TIME;
	if (!g_cxt->af_inited) {
		ret = cmr_msg_queue_create(CAMERA_AF_MSG_QUEUE_SIZE, &g_cxt->af_msg_que_handle);
		if (ret) {
			CMR_LOGE("NO Memory, Frailed to create message queue");
		}
		sem_init(&g_cxt->af_sync_sem, 0, 0);
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		CMR_LOGI("create before.");
		ret = pthread_create(&g_cxt->af_thread, &attr, camera_af_thread_proc, NULL);
		CMR_LOGI("create after.");
		sem_wait(&g_cxt->af_sync_sem);
		g_cxt->af_inited = 1;
		g_cxt->af_busy = 0;
		message.msg_type = CMR_EVT_AF_INIT;
		message.data     = 0;
		ret = cmr_msg_post(g_cxt->af_msg_que_handle, &message);
		if (ret) {
			CMR_LOGE("Faile to send one msg to camera af thread");
		}

	} else {
		g_cxt->ae_wait_stab = 0x0;

	}
	return ret;
}

int camera_af_deinit(void)
{
	CMR_MSG_INIT(message);
	int                      ret = CAMERA_SUCCESS;

	CMR_LOGV("inited, %d", g_cxt->af_inited);

	if (g_cxt->af_inited) {
		message.msg_type = CMR_EVT_AF_EXIT;
		ret = cmr_msg_post(g_cxt->af_msg_que_handle, &message);
		if (ret) {
			CMR_LOGE("Faile to send one msg to camera main thread");
		}
		sem_wait(&g_cxt->af_sync_sem);
		sem_destroy(&g_cxt->af_sync_sem);
		cmr_msg_queue_destroy(g_cxt->af_msg_que_handle);
		g_cxt->af_msg_que_handle = 0;
		g_cxt->af_inited = 0;
	}
	return ret ;
}

int camera_set_frame_type(camera_frame_type *frame_type, struct frm_info* info)
{
	uint32_t                 frm_id;
	int                      ret = CAMERA_SUCCESS;
	uint32_t                 chn_id;
	int                      skip_frame_gap;

	if (NULL == frame_type || NULL == info) {
		CMR_LOGE("Wrong param, frame_type 0x%x, info 0x%x",
			(uint32_t)frame_type,
			(uint32_t)info);
		return -CAMERA_INVALID_PARM;
	}

	chn_id = info->channel_id;

	if (CHN_1 == info->channel_id) {
		if (g_cxt->prev_rot) {
			frm_id = g_cxt->prev_rot_index % CAMERA_PREV_ROT_FRM_CNT;
			frame_type->buf_id = frm_id; // more than CAMERA_PREV_FRM_CNT
			frame_type->buf_Virt_Addr = (uint32_t*)g_cxt->prev_rot_frm[frm_id].addr_vir.addr_y;
			frame_type->buffer_phy_addr = g_cxt->prev_rot_frm[frm_id].addr_phy.addr_y;
		} else {
			frm_id = info->frame_id - CAMERA_PREV_ID_BASE;
			frame_type->buf_id = frm_id;
			frame_type->buf_Virt_Addr = (uint32_t*)g_cxt->prev_frm[frm_id].addr_vir.addr_y;
			frame_type->buffer_phy_addr = g_cxt->prev_frm[frm_id].addr_phy.addr_y;

		}
		frame_type->dx = g_cxt->display_size.width;
		frame_type->dy = g_cxt->display_size.height;
#if 0
		if (( g_cxt->pre_frm_cnt > 5) && ( g_cxt->pre_frm_cnt < 8)) {
			camera_save_to_file(g_cxt->pre_frm_cnt,
					IMG_DATA_TYPE_YUV420,
					frame_type->dx,
					frame_type->dy,
					&g_cxt->prev_frm[frm_id].addr_vir);
		}
#endif
		if (0 == g_cxt->arithmetic_cxt.fd_num) {
			skip_frame_gap = FACE_DETECT_GAP_MAX;
		} else {
			skip_frame_gap = FACE_DETECT_GAP_MIN;
		}
		if ((1 == g_cxt->arithmetic_cxt.fd_flag) && (1 == g_cxt->arithmetic_cxt.fd_inited)
			&& (0 == (g_cxt->pre_frm_cnt % (skip_frame_gap + 1)))) {
			CMR_LOGI("face detect start.");
			g_cxt->arithmetic_cxt.fd_num = 0;
			arithmetic_fd_start((void*)frame_type->buf_Virt_Addr);
		}
	} else if (CHN_2 == info->channel_id) {
		frm_id = info->frame_id - CAMERA_CAP0_ID_BASE;
		frame_type->buf_Virt_Addr = (uint32_t*)g_cxt->cap_mem[frm_id].target_yuv.addr_vir.addr_y;
		frame_type->buffer_phy_addr = g_cxt->cap_mem[frm_id].target_yuv.addr_phy.addr_y;
		frame_type->Y_Addr = (uint8_t *)g_cxt->cap_mem[frm_id].target_yuv.addr_vir.addr_y;
		frame_type->CbCr_Addr = (uint8_t *)g_cxt->cap_mem[frm_id].target_yuv.addr_vir.addr_u;
		frame_type->dx = g_cxt->picture_size.width;
		frame_type->dy = g_cxt->picture_size.height;
		frame_type->captured_dx = g_cxt->picture_size.width;
		frame_type->captured_dy = g_cxt->picture_size.height;
		frame_type->rotation = 0;
		frame_type->header_size = 0;
		frame_type->buffer_uv_phy_addr = g_cxt->cap_mem[frm_id].target_yuv.addr_phy.addr_u;
		CMR_LOGI("cap yuv addr 0x%x.",(uint32_t)frame_type->buf_Virt_Addr);

	} else if (CHN_0 == info->channel_id) {
		frm_id = info->frame_id - CAMERA_CAP0_ID_BASE;
		if (CAMERA_TOOL_RAW_MODE == g_cxt->cap_mode) {
			frame_type->buf_Virt_Addr = (uint32_t*)g_cxt->cap_mem[frm_id].cap_raw.addr_vir.addr_y;
			frame_type->buffer_phy_addr = g_cxt->cap_mem[frm_id].target_yuv.addr_phy.addr_y;
			frame_type->Y_Addr = (uint8_t *)g_cxt->cap_mem[frm_id].target_yuv.addr_vir.addr_y;
			frame_type->CbCr_Addr = (uint8_t *)g_cxt->cap_mem[frm_id].target_yuv.addr_vir.addr_u;
			frame_type->dx = g_cxt->cap_orig_size.width;
			frame_type->dy = g_cxt->cap_orig_size.height;
			frame_type->captured_dx = g_cxt->cap_orig_size.width;
			frame_type->captured_dy = g_cxt->cap_orig_size.height;
			frame_type->rotation = 0;
			frame_type->header_size = 0;
			frame_type->buffer_uv_phy_addr = g_cxt->cap_mem[frm_id].target_yuv.addr_phy.addr_u;
			CMR_LOGI("cap raw addr 0x%x.",(uint32_t)frame_type->buf_Virt_Addr);
		} else {
			frame_type->buf_Virt_Addr = (uint32_t*)g_cxt->cap_mem[frm_id].target_yuv.addr_vir.addr_y;
			frame_type->buffer_phy_addr = g_cxt->cap_mem[frm_id].target_yuv.addr_phy.addr_y;
			frame_type->Y_Addr = (uint8_t *)g_cxt->cap_mem[frm_id].target_yuv.addr_vir.addr_y;
			frame_type->CbCr_Addr = (uint8_t *)g_cxt->cap_mem[frm_id].target_yuv.addr_vir.addr_u;
			frame_type->dx = g_cxt->picture_size.width;
			frame_type->dy = g_cxt->picture_size.height;
			frame_type->captured_dx = g_cxt->picture_size.width;
			frame_type->captured_dy = g_cxt->picture_size.height;
			frame_type->rotation = 0;
			frame_type->header_size = 0;
			frame_type->buffer_uv_phy_addr = g_cxt->cap_mem[frm_id].target_yuv.addr_phy.addr_u;

			send_capture_data(0x02,/* yuv420 */
					g_cxt->picture_size.width,
					g_cxt->picture_size.height,
					(char *)g_cxt->cap_mem[frm_id].target_yuv.addr_vir.addr_y,
					g_cxt->picture_size.width*g_cxt->picture_size.height,
					(char *)g_cxt->cap_mem[frm_id].target_yuv.addr_vir.addr_u,
					g_cxt->picture_size.width*g_cxt->picture_size.height/2,
					0, 0);

			CMR_LOGI("cap yuv addr 0x%x.",(uint32_t)frame_type->buf_Virt_Addr);
		}
	}
	frame_type->format = CAMERA_YCBCR_4_2_0;
	frame_type->timestamp = info->sec * 1000000000LL + info->usec * 1000;

	CMR_LOGV("index 0x%x, addr 0x%x 0x%x, w h %d %d, format %d, sec %d usec %d",
		info->frame_id,
		(uint32_t)frame_type->buf_Virt_Addr,
		frame_type->buffer_phy_addr,
		frame_type->dx,
		frame_type->dy,
		frame_type->format,
		info->sec,
		info->usec);

	return ret;
}

int camera_create_main_thread(int32_t camera_id)
{
	int                      ret = CAMERA_SUCCESS;
	pthread_attr_t           attr;
	CMR_MSG_INIT(message);

	pthread_attr_init (&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	ret = pthread_create(&g_cxt->camera_main_thr, &attr, camera_main_routine, NULL);

	if (ret) {
		CMR_LOGE("Fail to careate main thread");
		return ret;
	}
	message.msg_type = CMR_EVT_INIT;
	message.sub_msg_type = camera_id;
	CMR_PRINT_TIME;
	ret = cmr_msg_post(g_cxt->msg_queue_handle, &message);
	if (ret) {
		CMR_LOGE("Fail to send message to camera main thread");
		return ret;
	}

	ret = camera_wait_init(g_cxt);
	CMR_PRINT_TIME;
	return ret;
}

int camera_destroy_main_thread(void)
{
	CMR_MSG_INIT(message);
	int                      ret = CAMERA_SUCCESS;

	message.msg_type = CMR_EVT_EXIT;
	ret = cmr_msg_post(g_cxt->msg_queue_handle, &message);
	if (ret) {
		CMR_LOGE("Faile to send one msg to camera main thread");
	}
	ret = camera_wait_exit(g_cxt);

	return ret;
}

void *camera_main_routine(void *client_data)
{
	CMR_MSG_INIT(message);
	uint32_t                 evt;
	int                      ret = CAMERA_SUCCESS;

	while (1) {
		ret = cmr_msg_get(g_cxt->msg_queue_handle, &message);
		if (ret) {
			CMR_LOGE("Message queue destroied");
			break;
		}

		CMR_LOGE("message.msg_type 0x%x, sub-type 0x%x",
			message.msg_type,
			message.sub_msg_type);
		evt = (uint32_t)(message.msg_type & CMR_EVT_MASK_BITS);

		switch (evt) {
		case CMR_EVT_OEM_BASE:
			ret = camera_internal_handle(message.msg_type,
					message.sub_msg_type,
					(struct frm_info*)message.data);
			break;

		case CMR_EVT_V4L2_BASE:
			ret = camera_v4l2_handle(message.msg_type,
					message.sub_msg_type,
					(struct frm_info *)message.data);
			break;

		case CMR_EVT_ISP_BASE:
			ret = camera_isp_handle(message.msg_type,
					message.sub_msg_type,
					(void *)message.data);
			break;

		case CMR_EVT_SENSOR_BASE:
			ret = camera_sensor_handle(message.msg_type,
					message.sub_msg_type,
					(struct frm_info *)message.data);
			break;

		case CMR_EVT_CVT_BASE:
			ret = camera_img_cvt_handle(message.msg_type,
					message.sub_msg_type,
					(struct img_frm *)message.data);
			break;

		case CMR_EVT_JPEG_BASE:
			ret = camera_jpeg_codec_handle(message.msg_type,
					message.sub_msg_type,
					(void *)message.data);
			break;

		default:
			CMR_LOGE("Unsupported MSG");
			break;
		}

		if (message.alloc_flag) {
			free(message.data);
		}

		if (0 == g_cxt->is_working) {
			CMR_LOGV("Camera main thread Exit!");
			break;
		}
	}

	return NULL;

}

void camera_sensor_evt_cb(int evt, void* data)
{
	CMR_MSG_INIT(message);
	int                      ret = CAMERA_SUCCESS;

	if (CMR_EVT_SENSOR_BASE != (CMR_EVT_SENSOR_BASE & evt)) {
		CMR_LOGE("Error param, 0x%x 0x%x", (uint32_t)data, evt);
		return;
	}

	message.msg_type = evt;
	ret = cmr_msg_post(g_cxt->msg_queue_handle, &message);
	if (ret) {
		CMR_LOGE("Faile to send one msg to camera main thread");
	}

	return;
}

void camera_v4l2_evt_cb(int evt, void* data)
{
	CMR_MSG_INIT(message);
	int                      ret = CAMERA_SUCCESS;
	struct frm_info          *info = (struct frm_info*)data;
	uint32_t                 queue_handle = g_cxt->msg_queue_handle;

	if (NULL == data ||
		CMR_EVT_V4L2_BASE != (CMR_EVT_V4L2_BASE & evt)) {
		CMR_LOGE("Error param, 0x%x 0x%x 0x%x", (uint32_t)data, evt, info->frame_id);
		return;
	}

	if ((V4L2_IDLE == g_cxt->v4l2_cxt.v4l2_state) || (1 == info->free)) {
		if (CMR_V4L2_TX_DONE == evt) {
			ret = cmr_v4l2_free_frame(info->channel_id, info->frame_id);
			CMR_LOGE("Wrong status, %d", g_cxt->v4l2_cxt.v4l2_state);
			return;
		}
	}
	if (CMR_V4L2_TX_DONE == evt) {
		if ((CHN_BUSY != g_cxt->chn_2_status) &&
			(CHN_2 == info->channel_id)) {
			ret = cmr_v4l2_free_frame(info->channel_id, info->frame_id);
			CMR_LOGE("discard, %d", g_cxt->chn_2_status);
			return;
		}
	}

	message.data = malloc(sizeof(struct frm_info));
	if (NULL == message.data) {
		cmr_v4l2_free_frame(info->channel_id, info->frame_id);
		CMR_LOGE("NO mem, Faile to alloc memory for one msg");
		return;
	}

	message.alloc_flag = 1;
	memcpy(message.data, data, sizeof(struct frm_info));
	message.msg_type = evt;
	if (CMR_V4L2_TX_DONE == evt) {
		if ((CHN_BUSY == g_cxt->chn_1_status) &&
			(CHN_1 == info->channel_id)) {
			message.msg_type = CMR_EVT_PREV_V4L2_TX_DONE;
			queue_handle = g_cxt->prev_msg_que_handle;
		} else if ((CHN_BUSY == g_cxt->chn_2_status) &&
			(CHN_2 == info->channel_id)) {
			message.msg_type = CMR_EVT_CAP_TX_DONE;
			queue_handle = g_cxt->cap_msg_que_handle;
		} else if ((CHN_BUSY == g_cxt->chn_0_status) &&
			(CHN_0 == info->channel_id)) {
			message.msg_type = CMR_EVT_CAP_RAW_TX_DONE;
			queue_handle = g_cxt->cap_msg_que_handle;
		}
	}
	ret = cmr_msg_post(queue_handle, &message);

	if (ret) {
		free(message.data);
		CMR_LOGE("Faile to send one msg to camera main thread");
	}

	return;
}

int32_t camera_isp_evt_cb(int32_t evt, void* data)
{
	CMR_MSG_INIT(message);
	uint32_t                 cmd;
	int                      ret = CAMERA_SUCCESS;

	if (CMR_EVT_ISP_BASE != (CMR_EVT_ISP_BASE & evt)) {
		CMR_LOGE("Error param, 0x%x", evt);
		return -1;
	}

	CMR_LOGV("evt, 0x%x", evt);

	message.sub_msg_type = (~CMR_EVT_ISP_BASE) & evt;
	CMR_LOGV("message.sub_msg_type, 0x%x", message.sub_msg_type);
	cmd = evt & 0xFF;
	if ((message.sub_msg_type & ISP_EVT_MASK) == 0) {
		ret = camera_isp_ctrl_done(cmd, data);
		CMR_LOGV("ret, %d", ret);
		return 0;
	}

	if (data) {
		message.data = malloc(sizeof(struct frm_info));
		if (NULL == message.data) {
			CMR_LOGE("NO mem, Faile to alloc memory for one msg");
			return -1;
		}
		message.alloc_flag = 1;
		memcpy(message.data, data, sizeof(struct frm_info));
	}
	message.msg_type = evt;
	if (CMR_IDLE != g_cxt->preview_status) {
		ret = cmr_msg_post(g_cxt->msg_queue_handle, &message);
		if (ret) {
			if (message.data) {
				free(message.data);
			}
			CMR_LOGE("Faile to send one msg to camera main thread");
		}
	} else {
		ret = camera_isp_handle(message.msg_type,
					message.sub_msg_type,
					(void *)message.data);
		if (message.data) {
			free(message.data);
		}

		if (ret) {
			CMR_LOGE("camera_isp_handle handle error.");
		}
	}
	return 0;
}

void camera_jpeg_evt_cb(int evt, void* data)
{
	CMR_MSG_INIT(message);
	int                      ret = CAMERA_SUCCESS;
	struct frm_info          *info = &g_cxt->jpeg_cxt.proc_status.frame_info;

	if (NULL == data || CMR_EVT_JPEG_BASE != (CMR_EVT_JPEG_BASE & evt)) {
		CMR_LOGE("Error param, 0x%x 0x%x", (uint32_t)data, evt);
		message.data = 0;
	} else {
		TAKE_PIC_CANCEL_NORETURNVALUE;
		if(CMR_JPEG_DEC_DONE == evt) {
			message.data = malloc(sizeof(JPEG_DEC_CB_PARAM_T));
		} else {
			message.data = malloc(sizeof(JPEG_ENC_CB_PARAM_T));
		}

		if (NULL == message.data) {
			CMR_LOGE("NO mem, Faile to alloc memory for one msg");
			return;
		}
		message.alloc_flag = 1;
		if(CMR_JPEG_DEC_DONE == evt) {
			memcpy(message.data, data, sizeof(JPEG_DEC_CB_PARAM_T));
		} else {
			memcpy(message.data, data, sizeof(JPEG_ENC_CB_PARAM_T));
		}
	}

	message.msg_type = evt;
	CMR_LOGV("evt 0x%x", evt);

	if (CHN_2 == info->channel_id) {
		ret = cmr_msg_post(g_cxt->cap_sub_msg_que_handle, &message);
	} else {
		ret = cmr_msg_post(g_cxt->msg_queue_handle, &message);
	}

	if (ret) {
		if (message.data) {
			free(message.data);
			CMR_LOGE("Faile to send one msg to camera main thread");
		}
	}

	return;
}

void camera_rot_evt_cb(int evt, void* data)
{
	CMR_MSG_INIT(message);
	int                      ret = CAMERA_SUCCESS;
	struct frm_info *frm_data = data;

	CMR_LOGV("0x%x", evt);

	if ((NULL == data) || (CMR_IMG_CVT_ROT_DONE != evt)) {
		CMR_LOGE("Error param, 0x%x 0x%x", (uint32_t)data, evt);
		return;
	}
	if (IS_PREVIEW && IS_PREV_FRM(frm_data->frame_id)) {
		message.data = malloc(sizeof(struct img_frm));
		if (NULL == message.data) {
			CMR_LOGE("NO mem, Faile to alloc memory for one msg");
			return;
		}
		message.msg_type = CMR_EVT_PREV_CVT_ROT_DONE;
		message.alloc_flag = 1;
		memcpy(message.data, data, sizeof(struct img_frm));
		ret = cmr_msg_post(g_cxt->prev_msg_que_handle, &message);
	} else {
		message.data = malloc(sizeof(struct img_frm));
		if (NULL == message.data) {
			CMR_LOGE("NO mem, Faile to alloc memory for one msg");
			return;
		}
		message.msg_type = evt;
		message.alloc_flag = 1;
		memcpy(message.data, data, sizeof(struct img_frm));
		ret = cmr_msg_post(g_cxt->msg_queue_handle, &message);
	}
	if (ret) {
		free(message.data);
		CMR_LOGE("Faile to send one msg to camera main thread");
	}

	return;
}

void camera_scaler_evt_cb(int evt, void* data)
{
	CMR_MSG_INIT(message);
	struct img_frm           frame;
	int                      ret = CAMERA_SUCCESS;
	struct frm_info          *info = &g_cxt->scaler_cxt.proc_status.frame_info;

	if (NULL == data || CMR_EVT_CVT_BASE != (CMR_EVT_CVT_BASE & evt)) {
		CMR_LOGE("Error param, 0x%x 0x%x", (uint32_t)data, evt);
		return;
	}

	message.data = malloc(sizeof(struct img_frm));
	if (NULL == message.data) {
		CMR_LOGE("NO mem, Faile to alloc memory for one msg");
		return;
	}
	message.msg_type = evt;
	message.alloc_flag = 1;
	memcpy(message.data, data, sizeof(struct img_frm));
	if ((CMR_IMG_CVT_SC_DONE == evt) && (CHN_2 == info->channel_id)) {
		ret = cmr_msg_post(g_cxt->cap_sub_msg_que_handle, &message);
	} else {
		ret = cmr_msg_post(g_cxt->msg_queue_handle, &message);
	}
	if (ret) {
		free(message.data);
		CMR_LOGE("Faile to send one msg to camera main thread");
	}

	return;

}

int camera_internal_handle(uint32_t evt_type, uint32_t sub_type, struct frm_info *data)
{
	int                      ret = CAMERA_SUCCESS;
	camera_cb_info           cb_info;
	int                      frm_num = -1;

	CMR_LOGV("evt_type 0x%x, sub_type %d", evt_type, sub_type);

	switch (evt_type) {
	case CMR_EVT_INIT:
		ret = camera_init_internal(sub_type);
		g_cxt->err_code = ret;
		camera_init_done(g_cxt);
		g_cxt->is_working = 1;
		break;

	case CMR_EVT_EXIT:
		if (CMR_PREVIEW == g_cxt->preview_status) {
			CMR_LOGV("OEM is still in Preview, stop it");
			if (CMR_CAPTURE == g_cxt->capture_status) {
				ret = camera_set_take_picture(TAKE_PICTURE_NO);
				ret = camera_stop_capture_internal();
				camera_call_cb(CAMERA_RSP_CB_SUCCESS,
					    camera_get_client_data(),
					    CAMERA_FUNC_RELEASE_PICTURE,
					    0);
			}
			camera_stop_preview_internal();
		} else if (CMR_CAPTURE == g_cxt->capture_raw_status) {
			CMR_LOGV("OEM is still in capture raw, stop it");
			camera_stop_capture_raw_internal();
		} else if (CMR_CAPTURE == g_cxt->capture_status) {
			camera_stop_capture_internal();
		}

		ret = camera_stop_internal();
		g_cxt->err_code = ret;
		camera_exit_done(g_cxt);
		g_cxt->is_working = 0;
		break;

	case CMR_EVT_START:
		if (CMR_PREVIEW == sub_type) {
			g_cxt->prev_buf_id = 0;
			ret = camera_start_preview_internal();
			if (CAMERA_SUCCESS == ret) {
				memset(&cb_info, 0, sizeof(camera_cb_info));
				cb_info.cb_type = CAMERA_RSP_CB_SUCCESS;
				cb_info.cb_func = CAMERA_FUNC_START_PREVIEW;
				camera_callback_start(&cb_info);
			}
		} else {
			if ((CMR_CAPTURE == sub_type)) {
				takepicture_mode cap_mode = (uint32_t)data;
				if((CAMERA_RAW_MODE == cap_mode) || (CAMERA_TOOL_RAW_MODE == cap_mode)) {
					camera_take_picture_internal_raw(cap_mode);
				} else {
					ret = camera_take_picture_internal((uint32_t)data);
				}
			} else {
				CMR_LOGV("No this sub-type");
			}
		}
		g_cxt->err_code = ret;
		camera_start_done(g_cxt);

		break;

	case CMR_EVT_STOP:
		if (CMR_PREVIEW == sub_type) {
			if (CMR_CAPTURE == g_cxt->capture_status) {
				ret = camera_set_take_picture(TAKE_PICTURE_NO);
				ret = camera_stop_capture_internal();
				camera_call_cb(CAMERA_RSP_CB_SUCCESS,
					    camera_get_client_data(),
					    CAMERA_FUNC_RELEASE_PICTURE,
					    0);
			}
			ret = camera_stop_preview_internal();
			camera_preview_stop_set();
#if CB_LIGHT_SYNC
			camera_call_cb(CAMERA_RSP_CB_SUCCESS,
					camera_get_client_data(),
					CAMERA_FUNC_STOP_PREVIEW,
					0);
#else
			memset(&cb_info, 0, sizeof(camera_cb_info));
			cb_info.cb_type = CAMERA_RSP_CB_SUCCESS;
			cb_info.cb_func = CAMERA_FUNC_STOP_PREVIEW;
			camera_callback_start(&cb_info);
#endif
		} else if (CMR_CAPTURE == sub_type) {
			ret = camera_stop_capture_internal();
			camera_call_cb(CAMERA_RSP_CB_SUCCESS,
				    camera_get_client_data(),
				    CAMERA_FUNC_RELEASE_PICTURE,
				    0);
		} else {
			CMR_LOGV("No this sub-type");
		}
		g_cxt->err_code = ret;
		camera_stop_done(g_cxt);
		break;

	case CMR_EVT_BEFORE_SET:
		ret = camera_before_set_internal(sub_type);
		g_cxt->err_code = ret;

		camera_stop_done(g_cxt);

		break;

	case CMR_EVT_AFTER_SET:
		ret = camera_after_set_internal(sub_type);
		g_cxt->err_code = ret;
		camera_start_done(g_cxt);

		break;
	case CMR_EVT_BEFORE_CAPTURE:
		if (IS_CHN_BUSY(CHN_2)) {
			ret = cmr_v4l2_cap_pause(CHN_2,0);
			SET_CHN_IDLE(CHN_2);
			camera_cap_path_done(g_cxt);
		}
		break;

	case CMR_EVT_AFTER_CAPTURE:
		if ((IS_ZSL_MODE(g_cxt->cap_mode) || IS_WAIT_FOR_NORMAL_CONTINUE(g_cxt->cap_mode,g_cxt->cap_cnt))
			&& CHN_0 != g_cxt->v4l2_cxt.proc_status.frame_info.channel_id) {
			if (IS_CHN_IDLE(CHN_2)) {
				SET_CHN_BUSY(CHN_2);
				if ((g_cxt->cap_cnt == g_cxt->total_capture_num) ||
					(CAMERA_HDR_MODE == g_cxt->cap_mode)) {
					camera_set_take_picture(0);
				}
				if (IS_ZSL_MODE(g_cxt->cap_mode)) {
					frm_num = -1;
				} else {
					frm_num = 1;
				}
				g_cxt->chn_2_status = CHN_BUSY;
				ret = cmr_v4l2_cap_resume(CHN_2,
					0,
					g_cxt->v4l2_cxt.chn_frm_deci[CHN_2],
					frm_num);
				if (ret) {
					SET_CHN_IDLE(CHN_2);
					g_cxt->chn_2_status = CHN_IDLE;
					CMR_LOGE("error.");
				}
			}
		}
		break;

	case CMR_EVT_CONVERT_THUM:
		camera_start_convert_thum();
		break;

	default:
		break;

	}

	return ret;
}

int camera_v4l2_handle(uint32_t evt_type, uint32_t sub_type, struct frm_info *data)
{
	int                      ret = CAMERA_SUCCESS;
	camera_cb_info           cb_info;

	if (NULL == data) {
		CMR_LOGE("Parameter error");
		return -CAMERA_INVALID_PARM;
	}

/*	if (CHN_1 == data->channel_id) {
		if(CMR_IDLE == g_cxt->preview_status || CMR_ERR == g_cxt->preview_status) {
			CMR_LOGE("Status error, preview_status = %d", g_cxt->preview_status);
			return -CAMERA_INVALID_STATE;
		}
	}

	if (CHN_2 == data->channel_id) {
		if(CMR_IDLE == g_cxt->capture_status || CMR_ERR == g_cxt->capture_status) {
			CMR_LOGE("Status error, capture_status = %d", g_cxt->capture_status);
			return -CAMERA_INVALID_STATE;
		}
	}

	if (CHN_0 == data->channel_id) {
		if(CMR_IDLE == g_cxt->capture_raw_status || CMR_ERR == g_cxt->capture_raw_status) {
			CMR_LOGE("Capture Raw status error, capture_raw_status = %d", g_cxt->capture_raw_status);
			return -CAMERA_INVALID_STATE;
		}
	}
*/
	(void)sub_type;
	switch (evt_type) {
	case CMR_V4L2_TX_DONE:
		break;

	case CMR_V4L2_TX_NO_MEM:
		if (IS_CAPTURE && IMG_DATA_TYPE_JPEG == g_cxt->cap_original_fmt) {
			CMR_LOGE("Need more memory for JPEG capture");
		} else {
			CMR_LOGE("CMR_V4L2_TX_NO_MEM, something wrong");
		}
		break;

	case CMR_V4L2_TX_ERROR:
	case CMR_V4L2_CSI2_ERR:
	case CMR_V4L2_TIME_OUT:
	case CMR_SENSOR_ERROR:

		CMR_LOGV("Error type 0x%x %d", evt_type, g_cxt->preview_status);
		if (IS_CHN_BUSY(CHN_1)) {
			ret = camera_preview_err_handle(evt_type);
			if (ret) {
				CMR_LOGE("Call cb to notice the upper layer something error blocked preview");
				memset(&cb_info, 0, sizeof(camera_cb_info));
				cb_info.cb_type = CAMERA_EXIT_CB_FAILED;
				cb_info.cb_func = CAMERA_FUNC_START_PREVIEW;
				camera_callback_start(&cb_info);
			}
		} else if (IS_CHN_BUSY(CHN_2)){
			ret = camera_capture_err_handle(evt_type);
			if (ret) {
				memset(&cb_info, 0, sizeof(camera_cb_info));
				cb_info.cb_type = CAMERA_EXIT_CB_FAILED;
				cb_info.cb_func = CAMERA_FUNC_TAKE_PICTURE;
				camera_callback_start(&cb_info);
			}
		} else {
			CMR_LOGE("don't handle error!");
		}
		CMR_LOGV("Errorhandle done.");
		break;
	default:
		break;

	}

	return ret;
}

int camera_isp_handle(uint32_t evt_type, uint32_t sub_type, void *data)
{
	int                      ret = CAMERA_SUCCESS;
	uint32_t                 cmd;

	CMR_LOGV("sub_type 0x%x, evt_type 0x%x", sub_type, evt_type);

	switch (sub_type & ISP_EVT_MASK) {
	case ISP_PROC_CALLBACK:
		ret = camera_isp_proc_handle((struct ips_out_param*)data);
		break;
	case ISP_AF_NOTICE_CALLBACK:
		ret = camera_isp_af_done(data);
		break;
	case ISP_FLASH_AE_CALLBACK:
		ret = camera_isp_alg_done(data);
		break;
	case ISP_AE_BAPASS_CALLBACK:
		ret = camera_isp_alg_done(data);
		break;
	case ISP_AF_STAT_CALLBACK:
		ret = camera_isp_af_stat(data);
		break;
	case ISP_FAST_AE_STAB_CALLBACK:
		ret = camera_isp_ae_stab(data);
		break;
	default:
		break;
	}
	return ret;

}

void camera_start_convert_thum(void)
{
	int ret = CAMERA_SUCCESS;

	CMR_PRINT_TIME;
	if ((0 != g_cxt->thum_size.width) && (0 != g_cxt->thum_size.height)) {
		ret = camera_convert_to_thumb();
		if (ret) {
			g_cxt->thum_ready = 0;
			CMR_LOGE("Failed to get thumbnail, %d", ret);
		}
	} else {
		g_cxt->thum_ready = 0;
		camera_convert_thum_done(g_cxt);
		CMR_LOGI("dont need thumbnail, %d", ret);
	}
}

int camera_jpeg_encode_handle(JPEG_ENC_CB_PARAM_T *data)
{
	uint32_t                 thumb_size = 0;
	int                      ret = CAMERA_SUCCESS;
	struct jpeg_enc_next_param enc_nxt_param;
	uint32_t                 in_slice_height = 0;

	if(NULL != data)
	    CMR_LOGV("stream buf 0x%x size 0x%x",
		    g_cxt->cap_mem[g_cxt->jpeg_cxt.index].target_jpeg.addr_vir.addr_y,
		    data->stream_size);

	CMR_PRINT_TIME;
	if (NULL == data || 0 == data->slice_height) {
		return CAMERA_INVALID_PARM;
	}
	CMR_LOGV("slice height %d index %d, stream size %d addr 0x%x total height %d",
		data->slice_height,
		g_cxt->jpeg_cxt.index,
		data->stream_size,
		data->stream_buf_vir,
		data->total_height);

	if ((((0 != g_cxt->zoom_level) && (ZOOM_POST_PROCESS == g_cxt->cap_zoom_mode))
		|| ((0 != g_cxt->cap_rot)&&(g_cxt->picture_size.width>=1280)))
		&& (data->total_height != g_cxt->picture_size.height)) {
		CMR_LOGI("Dont Need to handle.");
		return ret;
	}

	CMR_LOGI("slice_height_out=%d",g_cxt->jpeg_cxt.proc_status.slice_height_out);
	CMR_LOGI("data->total_height=%d",data->total_height);

	g_cxt->jpeg_cxt.proc_status.slice_height_out = data->total_height;
	if (g_cxt->jpeg_cxt.proc_status.slice_height_out == g_cxt->picture_size.height) {
		g_cxt->cap_mem[g_cxt->jpeg_cxt.index].target_jpeg.addr_vir.addr_u = data->stream_size;
		CMR_LOGV("Encode done");
		send_capture_data(0x10,/* jpg */
				g_cxt->picture_size.width,
				g_cxt->picture_size.height,
				(char *)g_cxt->cap_mem[g_cxt->jpeg_cxt.index].target_jpeg.addr_vir.addr_y,
				data->stream_size,
				0, 0, 0, 0);
#if 0
		ret = camera_save_to_file(990,
			IMG_DATA_TYPE_JPEG,
			g_cxt->picture_size.width,
			g_cxt->picture_size.height,
			&g_cxt->cap_mem[g_cxt->jpeg_cxt.index].target_jpeg.addr_vir);
#endif
		TAKE_PIC_CANCEL;
		if (0 != g_cxt->jpeg_cxt.handle) {
			ret = jpeg_stop(g_cxt->jpeg_cxt.handle);
			if (ret) {
				CMR_LOGE("Failed to stop jpeg, %d", ret);
			} else {
				g_cxt->jpeg_cxt.handle = 0;
			}
		} else {
			CMR_LOGI("don't need stop jpeg.");
		}
		CMR_PRINT_TIME;
		TAKE_PIC_CANCEL;
#if 0
		if (THUM_FROM_CAP != g_cxt->thum_from) {
			if ((0 != g_cxt->thum_size.width) && (0 != g_cxt->thum_size.height)) {
				ret = camera_convert_to_thumb();
				if (ret) {
					thumb_exist = 0;
					CMR_LOGE("Failed to get thumbnail, %d", ret);
				}
			} else {
				thumb_exist = 0;
				CMR_LOGI("dont need thumbnail, %d", ret);
			}
		}
#endif
#if 0
		CMR_PRINT_TIME;
		ret = cmr_v4l2_free_frame(CHN_2, CAMERA_CAP0_ID_BASE + g_cxt->jpeg_cxt.index);
		if (ret) {
				CMR_LOGE("Failed to free frame %d, index %d", ret, g_cxt->jpeg_cxt.index);
		}
		ret = cmr_v4l2_cap_resume(CHN_2,
				0,
				g_cxt->v4l2_cxt.chn_frm_deci[CHN_2]);
		SET_CHN_BUSY(CHN_2);
#endif
		camera_wait_convert_thum(g_cxt);
		CMR_LOGV("convert thum OK!");

		CMR_PRINT_TIME;
		TAKE_PIC_CANCEL;
		if (1 == g_cxt->thum_ready) {
			TAKE_PICTURE_STEP(CMR_STEP_THUM_ENC_S);
			ret = camera_jpeg_encode_thumb(&thumb_size);
			TAKE_PICTURE_STEP(CMR_STEP_THUM_ENC_E);
			if (ret) {
				CMR_LOGE("Failed to enc thumbnail, %d", ret);
				thumb_size = 0;
			}
		} else {
			thumb_size = 0;
		}
		ret = camera_jpeg_encode_done(thumb_size);
	} else {
		CMR_LOGV("Do nothing");
	}
	CMR_LOGI("test time:%d.",g_cxt->jpeg_cxt.jpeg_state);
	return ret;

}

int camera_jpeg_decode_handle(JPEG_DEC_CB_PARAM_T *data)
{
	int                      ret = CAMERA_SUCCESS;
	struct frm_info          capture_data;

	TAKE_PIC_CANCEL;

	CMR_LOGI("dec total height %d.",data->total_height);
	g_cxt->jpeg_cxt.proc_status.slice_height_out += data->slice_height;
	if (data->total_height >= g_cxt->cap_orig_size.height) {
		if (0 != g_cxt->jpeg_cxt.handle) {
			jpeg_stop(g_cxt->jpeg_cxt.handle);
			g_cxt->jpeg_cxt.handle = 0;
		} else {
			CMR_LOGI("don't need stop jpeg.");
		}
		TAKE_PICTURE_STEP(CMR_STEP_JPG_DEC_E);
		g_cxt->jpeg_cxt.jpeg_state = JPEG_IDLE;
		g_cxt->jpeg_cxt.proc_status.frame_info.data_endian = data->data_endian;
		if (camera_is_jpeg_specify_process(CAMERA_SUCCESS)) {
			g_cxt->cap_original_fmt = IMG_DATA_TYPE_YUV420;
			ret = CAMERA_SUCCESS;
		} else {
			g_cxt->cap_original_fmt = IMG_DATA_TYPE_YUV420;
			ret = camera_capture_yuv_process(&g_cxt->jpeg_cxt.proc_status.frame_info);
		}
	} else {
		ret = camera_jpeg_decode_next(&capture_data);
	}
	return ret;

}
int camera_jpeg_codec_handle(uint32_t evt_type, uint32_t sub_type, void *data)
{
	int                      ret = CAMERA_SUCCESS;
	camera_cb_info           cb_info;

	CMR_LOGV("status %d, evt 0x%x, data 0x%x",
		g_cxt->jpeg_cxt.jpeg_state, evt_type, (uint32_t)data);

	(void)sub_type;

	TAKE_PIC_CANCEL;

	switch (evt_type) {
	case CMR_JPEG_ENC_DONE:
		ret = camera_jpeg_encode_handle((JPEG_ENC_CB_PARAM_T*)data);
		break;
	case CMR_JPEG_DEC_DONE:
		ret = camera_jpeg_decode_handle((JPEG_DEC_CB_PARAM_T*)data);
		break;
	case CMR_JPEG_WEXIF_DONE:
		break;
	case CMR_JPEG_ENC_ERR:
		CMR_LOGE("jpeg codec error.");
		memset(&cb_info, 0, sizeof(camera_cb_info));
		cb_info.cb_type = CAMERA_EXIT_CB_FAILED;
		cb_info.cb_func = CAMERA_FUNC_ENCODE_PICTURE;
		camera_callback_start(&cb_info);
		break;
	case CMR_JPEG_DEC_ERR:
		CMR_LOGE("jpeg codec dec error.");

		g_cxt->err_code = -CAMERA_FAILED;
		if (CAMERA_HDR_MODE == g_cxt->cap_mode) {
			camera_jpeg_specify_notify_done(CAMERA_FAILED);
		} else {
			memset(&cb_info, 0, sizeof(camera_cb_info));
			cb_info.cb_type = CAMERA_EXIT_CB_FAILED;
			cb_info.cb_func = CAMERA_FUNC_TAKE_PICTURE;
			camera_callback_start(&cb_info);
		}
		break;

	}

	return ret;
}

int camera_scale_handle(uint32_t evt_type, uint32_t sub_type, struct img_frm *data)
{
	int                         ret = CAMERA_SUCCESS;
	struct scaler_context       *cxt = &g_cxt->scaler_cxt;
	uint32_t                    is_start_enc = 0;
	struct jpeg_enc_next_param  enc_nxt_param;

	CMR_LOGV("channel id %d, slice %d",
		cxt->proc_status.frame_info.channel_id,
		data->size.height);

	(void)sub_type;

	if (IMG_CVT_SCALING != g_cxt->scaler_cxt.scale_state) {
		CMR_LOGE("Error state %d", g_cxt->scaler_cxt.scale_state);
		return CAMERA_INVALID_STATE;
	}

	cxt->proc_status.slice_height_out += data->size.height;
	if (cxt->proc_status.slice_height_out >= CMR_SLICE_HEIGHT) {
		if (0 == cxt->proc_status.is_encoding) {
			cxt->proc_status.frame_info.height = cxt->proc_status.slice_height_out;
			ret = camera_start_jpeg_encode(&cxt->proc_status.frame_info);
			if (ret) {
				CMR_LOGE("Failed to start jpeg encode %d", ret);
				return -CAMERA_FAILED;
			}
			cxt->proc_status.is_encoding = 1;
		} else {
			bzero(&enc_nxt_param, sizeof(struct jpeg_enc_next_param));
			enc_nxt_param.handle       = g_cxt->jpeg_cxt.handle;
			enc_nxt_param.slice_height = data->size.height;
			enc_nxt_param.ready_line_num = cxt->proc_status.slice_height_out;
			CMR_LOGV("Jpeg need more slice, %d %d",
			g_cxt->jpeg_cxt.proc_status.slice_height_out,
			g_cxt->picture_size.height);
			ret = jpeg_enc_next(&enc_nxt_param);
			if (ret) {
				CMR_LOGE("Failed to next jpeg encode %d", ret);
				return -CAMERA_FAILED;
			}
		}
	} else {
		CMR_LOGV("Slice out height is too short to start endcode %d",
			cxt->proc_status.slice_height_out);
	}

	CMR_LOGV("channel_id %d, frame_id 0x%x",
		cxt->proc_status.frame_info.channel_id,
		cxt->proc_status.frame_info.frame_id);

	if (cxt->proc_status.slice_height_out < g_cxt->picture_size.height) {
		if (IMG_DATA_TYPE_RAW != g_cxt->cap_original_fmt) {
			ret = camera_scale_next(&cxt->proc_status.frame_info);
			if (CVT_RET_LAST == ret)
				ret = 0;
		}
	} else {
		ret = camera_scale_done(&cxt->proc_status.frame_info);
	}

	return ret;

}


int camera_rotation_handle(uint32_t evt_type, uint32_t sub_type, struct img_frm *data)
{
	camera_frame_type        frame_type;
	struct rotation_context  *cxt = &g_cxt->rot_cxt;
	struct frm_info          *info = &cxt->proc_status.frame_info;
	uint32_t                 frm_id;
	uint32_t                 tmp;
	int                      ret = CAMERA_SUCCESS;
	camera_cb_info           cb_info;

	if (IMG_CVT_ROTATING != g_cxt->rot_cxt.rot_state) {
		CMR_LOGE("Error state %d", g_cxt->rot_cxt.rot_state);
		goto exit;
	}

	(void)sub_type;

	if (CHN_1 == info->channel_id) {
		/* the source frame can be freed here*/
		CMR_LOGV("Rot Done");
		ret = cmr_v4l2_free_frame(info->channel_id, info->frame_id);
		CMR_LOGV("free frame done!");
		if (ret) {
			CMR_LOGE("Failed to free frame, %d, %d", info->frame_id, info->channel_id);
			goto exit;
		}

		if (g_cxt->set_flag > 0) {
			camera_set_done(g_cxt);
			g_cxt->set_flag --;
		}

		ret = camera_set_frame_type(&frame_type, info);
		if (ret) {
			CMR_LOGE("Failed to set frame type, %d, %d", info->frame_id, info->channel_id);
			goto exit;
		}

		g_cxt->prev_rot_index ++;
#if CB_LIGHT_SYNC
		camera_call_cb(CAMERA_EVT_CB_FRAME,
				camera_get_client_data(),
				CAMERA_FUNC_START_PREVIEW,
				(uint32_t)&frame_type);
#else
		memset(&cb_info, 0, sizeof(camera_cb_info));
		cb_info.cb_type = CAMERA_EVT_CB_FRAME;
		cb_info.cb_func = CAMERA_FUNC_START_PREVIEW;
		cb_info.cb_data = (void *)(&frame_type);
		cb_info.cb_data_length = sizeof(camera_frame_type);
		camera_callback_start(&cb_info);
#endif

	} else if (CHN_2 == info->channel_id) {
		if (IMG_ROT_90 == g_cxt->cap_rot || IMG_ROT_270 == g_cxt->cap_rot ||
			IMG_ROT_90 == g_cxt->cfg_cap_rot || IMG_ROT_270 == g_cxt->cfg_cap_rot) {
			tmp = g_cxt->cap_orig_size.width;
			g_cxt->cap_orig_size.width = g_cxt->cap_orig_size.height;
			g_cxt->cap_orig_size.height = tmp;
			info->height = g_cxt->cap_orig_size.height;
		}

		CMR_LOGV("orig size %d %d, %d %d",
			g_cxt->cap_orig_size.width, g_cxt->cap_orig_size.height,
			g_cxt->picture_size.width, g_cxt->picture_size.height);

#if 0
		camera_save_to_file(1, IMG_DATA_TYPE_YUV420,
					g_cxt->cap_orig_size.width,
					g_cxt->cap_orig_size.height,
					&g_cxt->cap_mem[0].cap_yuv.addr_vir);
#endif

		if (NO_SCALING) {
			ret = camera_start_jpeg_encode(info);
			if (ret) {
				CMR_LOGE("Failed to start jpeg encode %d", ret);
				return -CAMERA_FAILED;
			}

			if (CHN_2 == info->channel_id) {
				frm_id = info->frame_id - CAMERA_CAP0_ID_BASE;
				ret = camera_take_picture_done(info);
				if (ret) {
					CMR_LOGE("Failed to set take_picture done %d", ret);
					return -CAMERA_FAILED;
				}
			} else {
				frm_id = info->frame_id - CAMERA_CAP1_ID_BASE;
			}
		} else {
			ret = camera_start_scale(info);
		}
	} else {
		CMR_LOGV("Wrong status for ROT event");
	}

exit:

	g_cxt->rot_cxt.rot_state = IMG_CVT_ROT_DONE;
	sem_post(&g_cxt->rot_cxt.cmr_rot_sem);
	return ret;

}

int camera_sensor_handle(uint32_t evt_type, uint32_t sub_type, struct frm_info *data)
{
	int                      ret = CAMERA_SUCCESS;
	camera_cb_info           cb_info;

	(void)sub_type;

	CMR_LOGE("evt_type %d", evt_type);

	if (CMR_SENSOR_ERROR == evt_type) {
		if (IS_PREVIEW) {
			ret = camera_preview_err_handle(evt_type);
			if (ret) {
				CMR_LOGE("something error blocked preview");
				memset(&cb_info, 0, sizeof(camera_cb_info));
				cb_info.cb_type = CAMERA_EXIT_CB_FAILED;
				cb_info.cb_func = CAMERA_FUNC_START_PREVIEW;
				camera_callback_start(&cb_info);

			}
		}

	}
	return ret;
}

int camera_img_cvt_handle(uint32_t evt_type, uint32_t sub_type, struct img_frm *data)
{
	int                      ret = CAMERA_SUCCESS;

	(void)sub_type;

	CMR_LOGV("evt 0x%x", evt_type);
	if (CMR_IMG_CVT_SC_DONE == evt_type) {
		ret = camera_scale_handle(evt_type, sub_type, data);
	} else {
		ret = camera_rotation_handle(evt_type, sub_type, data);
	}
	return ret;
}

void *camera_af_thread_proc(void *data)
{
	CMR_MSG_INIT(message);
	int ret = CAMERA_SUCCESS;
	int af_exit_flag = 0;
	uint32_t ex_af_cancel_flag = 0;
	camera_cb_type cb_type = CAMERA_CB_MAX;
	uint32_t     isp_param = 1;

	CMR_PRINT_TIME;
	sem_post(&g_cxt->af_sync_sem);
	CMR_PRINT_TIME;
	while (1) {
		ret = cmr_msg_get(g_cxt->af_msg_que_handle, &message);
		if (ret) {
			CMR_LOGE("Message queue destroied");
			break;
		}

		CMR_LOGE("message.msg_type 0x%x, data 0x%x", message.msg_type, (uint32_t)message.data);

		switch (message.msg_type) {
		case CMR_EVT_AF_INIT:
			CMR_PRINT_TIME;
			ret = camera_autofocus_init();
			if (ret) {
				CMR_LOGE("Failed, %d", ret);
			}
			CMR_PRINT_TIME;
			break;
		case CMR_EVT_AF_START:
			CMR_PRINT_TIME;
			if (CMR_IDLE == g_cxt->preview_status) {
				CMR_LOGI("preview already stoped.");
				camera_autofocus_need_exit(&ex_af_cancel_flag);
				if (0x01 == ex_af_cancel_flag) {
					cb_type = CAMERA_EXIT_CB_ABORT;
				} else {
					cb_type = CAMERA_EXIT_CB_FAILED;
				}

				camera_call_af_cb((camera_cb_f_type)(message.data),
					cb_type,
					camera_get_client_data(),
					CAMERA_FUNC_START_FOCUS,
					0);
				break;

			}
			if (V4L2_SENSOR_FORMAT_RAWRGB == g_cxt->sn_cxt.sn_if.img_fmt) {
#if 0
				camera_isp_ae_stab_set(1);
				ret = camera_isp_get_ae_stab(&isp_param);
				CMR_LOGV("wait AE stable ....");
				ret = camera_isp_ae_wait_stab();
				if (ret) {
					CMR_LOGE("wait AE stable abnormal!");
				} else {
					CMR_LOGV("wait AE stable OK");
				}
#endif
			}
			pthread_mutex_lock(&g_cxt->af_cb_mutex);
			ret = camera_autofocus_start();
			pthread_mutex_unlock(&g_cxt->af_cb_mutex);
			camera_autofocus_need_exit(&ex_af_cancel_flag);

			if (0x01 == ex_af_cancel_flag) {
				cb_type = CAMERA_EXIT_CB_ABORT;
			} else if (CAMERA_SUCCESS == ret) {
				cb_type = CAMERA_EXIT_CB_DONE;
			} else {
				cb_type = CAMERA_EXIT_CB_FAILED;
			}

			camera_call_af_cb((camera_cb_f_type)(message.data),
				cb_type,
				camera_get_client_data(),
				CAMERA_FUNC_START_FOCUS,
				0);
			CMR_PRINT_TIME;
			break;

		case CMR_EVT_AF_EXIT:
			CMR_LOGV("AF exit");
			af_exit_flag = 1;
			sem_post(&g_cxt->af_sync_sem);
			CMR_PRINT_TIME;
			break;

		default:
			break;
		}
		if(af_exit_flag) {
			CMR_LOGI("AF proc exit.");
			break;
		}
	}

	CMR_LOGI("exit.");

	return NULL;
}

void camera_set_af_cb(camera_cb_f_type cmr_cb)
{
	pthread_mutex_lock(&g_cxt->af_cb_mutex);
	g_cxt->camera_af_cb = cmr_cb;
	pthread_mutex_unlock(&g_cxt->af_cb_mutex);
	return;
}
void camera_call_af_cb(camera_cb_f_type cmr_cb,
			camera_cb_type cb,
			const void *client_data,
			camera_func_type func,
			int32_t parm4)
{
	camera_cb_f_type         camera_af_run_cb = PNULL;
	if (cmr_cb) {
		camera_af_run_cb = cmr_cb;
		(*camera_af_run_cb)(cb, client_data, func, parm4);
	} else {
		CMR_LOGE("af cb pointer NULL error!");
	}
	return;
}
void camera_set_hal_cb(camera_cb_f_type cmr_cb)
{
	pthread_mutex_lock(&g_cxt->cb_mutex);
	g_cxt->camera_cb = cmr_cb;
	pthread_mutex_unlock(&g_cxt->cb_mutex);
	return;
}

void camera_call_cb(camera_cb_type cb,
                 const void *client_data,
                 camera_func_type func,
                 int32_t parm4)
{
	pthread_mutex_lock(&g_cxt->cb_mutex);
	if (g_cxt->camera_cb) {
		(*g_cxt->camera_cb)(cb, client_data, func, parm4);
	}
	pthread_mutex_unlock(&g_cxt->cb_mutex);
	return;
}

void camera_set_client_data(void* user_data)
{
	pthread_mutex_lock(&g_cxt->data_mutex);
	g_cxt->client_data = user_data;
	pthread_mutex_unlock(&g_cxt->data_mutex);
	return;
}

void *camera_get_client_data(void)
{
	void                     *user_data = NULL;

	pthread_mutex_lock(&g_cxt->data_mutex);
	user_data = g_cxt->client_data;
	pthread_mutex_unlock(&g_cxt->data_mutex);
	return user_data;
}

int camera_preview_init(int format_mode)
{
	int                      ret = CAMERA_SUCCESS;
	struct cap_cfg           v4l2_cfg;
	struct sn_cfg            sensor_cfg;
	uint32_t                 sn_work_mode;
	SENSOR_MODE_INFO_T       *sensor_mode;
	struct buffer_cfg        buffer_info;
	SENSOR_AE_INFO_T         *sensor_aec_info;


	memset(&v4l2_cfg,0,sizeof(v4l2_cfg));
	if (IS_ZSL_MODE(g_cxt->cap_mode)) {
		sn_work_mode = g_cxt->sn_cxt.capture_mode;
	} else {
		sn_work_mode = g_cxt->sn_cxt.preview_mode;
	}
	sensor_mode = &g_cxt->sn_cxt.sensor_info->sensor_mode_info[sn_work_mode];

	sensor_cfg.sn_size.width  = sensor_mode->width;
	sensor_cfg.sn_size.height = sensor_mode->height;
	sensor_cfg.frm_num        = -1;
	ret = cmr_v4l2_sn_cfg(&sensor_cfg);
	if (ret) {
		CMR_LOGE("Failed to set V4L2 the size of sensor");
		return -CAMERA_FAILED;
	}

	g_cxt->prev_rot_index = 0;
	g_cxt->skip_mode      = IMG_SKIP_HW;
	g_cxt->skip_num       = g_cxt->sn_cxt.sensor_info->preview_skip_num;
	g_cxt->pre_frm_cnt    = 0;
	g_cxt->restart_skip_cnt = 0;
	g_cxt->restart_skip_en = 0;

	v4l2_cfg.cfg.need_isp = 0;
	v4l2_cfg.cfg.need_binning = 0;
	if (SENSOR_IMAGE_FORMAT_YUV422 == sensor_mode->image_format) {
		g_cxt->sn_cxt.sn_if.img_fmt = V4L2_SENSOR_FORMAT_YUV;
	} else if (SENSOR_IMAGE_FORMAT_RAW == sensor_mode->image_format) {
		g_cxt->sn_cxt.sn_if.img_fmt = V4L2_SENSOR_FORMAT_RAWRGB;
		g_cxt->skip_mode = IMG_SKIP_SW;
		v4l2_cfg.cfg.need_isp = 1;
	} else {
		CMR_LOGE("Unsupported sensor format %d for preview", sensor_mode->image_format);
		ret = -CAMERA_INVALID_FORMAT;
		goto exit;
	}
	g_cxt->sn_cxt.sn_if.if_spec.mipi.pclk = sensor_mode->pclk;
	CMR_LOGI("sensor output, w h, %d %d", sensor_mode->width, sensor_mode->height);

	v4l2_cfg.channel_id               = CHN_1;
	v4l2_cfg.cfg.dst_img_size.width   = g_cxt->preview_size.width;
	v4l2_cfg.cfg.dst_img_size.height  = g_cxt->preview_size.height;
	v4l2_cfg.cfg.notice_slice_height  = v4l2_cfg.cfg.dst_img_size.height;
	v4l2_cfg.cfg.src_img_rect.start_x = sensor_mode->trim_start_x;
	v4l2_cfg.cfg.src_img_rect.start_y = sensor_mode->trim_start_y;
	v4l2_cfg.cfg.src_img_rect.width   = sensor_mode->trim_width;
	v4l2_cfg.cfg.src_img_rect.height  = sensor_mode->trim_height;
	ret = camera_get_trim_rect(&v4l2_cfg.cfg.src_img_rect, g_cxt->zoom_level, &v4l2_cfg.cfg.dst_img_size);
	if (ret) {
		CMR_LOGE("Failed to get trimming window for %d zoom level ", g_cxt->zoom_level);
		goto exit;
	}

	g_cxt->preview_rect.start_x = v4l2_cfg.cfg.src_img_rect.start_x;
	g_cxt->preview_rect.start_y = v4l2_cfg.cfg.src_img_rect.start_y;
	g_cxt->preview_rect.width   = v4l2_cfg.cfg.src_img_rect.width;
	g_cxt->preview_rect.height  = v4l2_cfg.cfg.src_img_rect.height;

	v4l2_cfg.cfg.dst_img_fmt = camera_get_img_type(format_mode);
	v4l2_cfg.frm_num = -1;
	ret = cmr_v4l2_cap_cfg(&v4l2_cfg);
	if (ret) {
		CMR_LOGE("Can't support this capture configuration");
		goto exit;
	}

	ret = camera_alloc_preview_buf(&buffer_info, v4l2_cfg.cfg.dst_img_fmt);
	if (ret) {
		CMR_LOGE("Failed to alloc preview buffer");
		goto exit;
	}

	ret = cmr_v4l2_buff_cfg(&buffer_info);
	if (ret) {
		CMR_LOGE("Failed to Q preview buffer");
		goto exit;
	}
	g_cxt->chn_1_status = CHN_BUSY;
	SET_CHN_BUSY(CHN_1);
	if (v4l2_cfg.cfg.need_isp) {
		uint32_t video_mode = 0;

		video_mode = g_cxt->cmr_set.video_mode;
		sensor_aec_info = &g_cxt->sn_cxt.sensor_info->sensor_video_info[sn_work_mode].ae_info[video_mode];
		camera_isp_ae_info(sensor_aec_info);
		camera_isp_wb_trim(&v4l2_cfg.cfg);

		ret = camera_isp_start(CMR_PREVIEW, v4l2_cfg.cfg.need_binning, sensor_mode);
	}

exit:
	return ret;
}

int camera_preview_weak_init(int format_mode)
{
	int                      ret = CAMERA_SUCCESS;
	struct cap_cfg           v4l2_cfg;
	SENSOR_MODE_INFO_T       *sensor_mode;
	struct buffer_cfg        buffer_info;
	struct isp_video_start   isp_param;

	memset(&v4l2_cfg,0,sizeof(v4l2_cfg));
	if (IS_ZSL_MODE(g_cxt->cap_mode)) {
		sensor_mode = &g_cxt->sn_cxt.sensor_info->sensor_mode_info[g_cxt->sn_cxt.capture_mode];
	} else {
		sensor_mode = &g_cxt->sn_cxt.sensor_info->sensor_mode_info[g_cxt->sn_cxt.preview_mode];
	}
	g_cxt->prev_rot_index = 0;
	v4l2_cfg.cfg.need_isp = 0;
	v4l2_cfg.cfg.need_binning = 0;
	if (SENSOR_IMAGE_FORMAT_YUV422 == sensor_mode->image_format) {
		g_cxt->sn_cxt.sn_if.img_fmt = V4L2_SENSOR_FORMAT_YUV;
	} else if (SENSOR_IMAGE_FORMAT_RAW == sensor_mode->image_format) {
		g_cxt->sn_cxt.sn_if.img_fmt = V4L2_SENSOR_FORMAT_RAWRGB;
		g_cxt->skip_mode = IMG_SKIP_SW;
		v4l2_cfg.cfg.need_isp = 1;
	} else {
		CMR_LOGE("Unsupported sensor format %d for preview", sensor_mode->image_format);
		ret = -CAMERA_INVALID_FORMAT;
		goto exit;
	}
	g_cxt->sn_cxt.sn_if.if_spec.mipi.pclk = sensor_mode->pclk;
	CMR_LOGI("sensor output, width, hegiht %d %d", sensor_mode->width, sensor_mode->height);
	v4l2_cfg.channel_id = CHN_1;
	v4l2_cfg.cfg.dst_img_size.width   = g_cxt->preview_size.width;
	v4l2_cfg.cfg.dst_img_size.height  = g_cxt->preview_size.height;
	v4l2_cfg.cfg.notice_slice_height  = v4l2_cfg.cfg.dst_img_size.height;
	v4l2_cfg.cfg.src_img_rect.start_x = sensor_mode->trim_start_x;
	v4l2_cfg.cfg.src_img_rect.start_y = sensor_mode->trim_start_y;
	v4l2_cfg.cfg.src_img_rect.width   = sensor_mode->trim_width;
	v4l2_cfg.cfg.src_img_rect.height  = sensor_mode->trim_height;
	ret = camera_get_trim_rect(&v4l2_cfg.cfg.src_img_rect, g_cxt->zoom_level, &v4l2_cfg.cfg.dst_img_size);
	if (ret) {
		CMR_LOGE("Failed to get trimming window for %d zoom level ", g_cxt->zoom_level);
		goto exit;
	}

	g_cxt->preview_rect.start_x = v4l2_cfg.cfg.src_img_rect.start_x;
	g_cxt->preview_rect.start_y = v4l2_cfg.cfg.src_img_rect.start_y;
	g_cxt->preview_rect.width   = v4l2_cfg.cfg.src_img_rect.width;
	g_cxt->preview_rect.height  = v4l2_cfg.cfg.src_img_rect.height;

	v4l2_cfg.cfg.dst_img_fmt = camera_get_img_type(format_mode);
	v4l2_cfg.frm_num = -1;
	ret = cmr_v4l2_cap_cfg(&v4l2_cfg);
	if (ret) {
		CMR_LOGE("Can't support this capture configuration");
		goto exit;
	}
	SET_CHN_BUSY(CHN_1);
	if (v4l2_cfg.cfg.need_isp) {
		ret = camera_isp_wb_trim(&v4l2_cfg.cfg);
	}

exit:
	return ret;
}

int camera_capture_init(void)
{
	struct img_size          capture_size;
	int                      ret = CAMERA_SUCCESS;
	struct cap_cfg           v4l2_cfg;
	SENSOR_MODE_INFO_T       *sensor_mode;
	struct buffer_cfg        buffer_info;
	struct sn_cfg            sensor_cfg;

	camera_cfg_rot_cap_param_reset();

	CMR_LOGV("capture size, %d %d, cap_rot %d",
		g_cxt->capture_size.width,
		g_cxt->capture_size.height,
		g_cxt->cap_rot);

	g_cxt->thum_ready = 0;
	CMR_LOGI("sn_cxt_cap_mode%d.",g_cxt->sn_cxt.capture_mode);
	sensor_mode = &g_cxt->sn_cxt.sensor_info->sensor_mode_info[g_cxt->sn_cxt.capture_mode];
	sensor_cfg.sn_size.width  = sensor_mode->width;
	sensor_cfg.sn_size.height = sensor_mode->height;

	if (IS_NON_ZSL_MODE(g_cxt->cap_mode)) {
		if (CAMERA_NORMAL_CONTINUE_SHOT_MODE != g_cxt->cap_mode
			|| SENSOR_IMAGE_FORMAT_JPEG == sensor_mode->image_format
			|| (SENSOR_IMAGE_FORMAT_RAW == sensor_mode->image_format
				&& sensor_mode->width > g_cxt->isp_cxt.width_limit)) {
			sensor_cfg.frm_num = 1;
		} else {
			sensor_cfg.frm_num = -1;
		}
		CMR_LOGI("sensor_cfg.frm_num=%d.",sensor_cfg.frm_num);
		ret = cmr_v4l2_sn_cfg(&sensor_cfg);
		if (ret) {
			CMR_LOGE("Failed to set V4L2 the size of sensor");
			return -CAMERA_FAILED;
		}
	}

	bzero(&v4l2_cfg, sizeof(struct cap_cfg));
	v4l2_cfg.channel_id = CHN_2;
	ret = camera_capture_ability(sensor_mode, &v4l2_cfg.cfg, &g_cxt->capture_size);
	if (ret) {
		CMR_LOGE("Failed to camera_capture_ability, %d", ret);
		goto exit;
	}

	if (IMG_DATA_TYPE_JPEG == v4l2_cfg.cfg.dst_img_fmt
		|| IMG_DATA_TYPE_RAW == v4l2_cfg.cfg.dst_img_fmt) {
		v4l2_cfg.channel_id = CHN_0;
	}
	if (IS_ZSL_MODE(g_cxt->cap_mode)) {
		v4l2_cfg.frm_num = -1;
	} else {
		v4l2_cfg.frm_num = 1;
	}
	ret = cmr_v4l2_cap_cfg(&v4l2_cfg);
	if (ret) {
		CMR_LOGE("Can't support this capture configuration");
		goto exit;
	} else {
		g_cxt->thum_from = THUM_FROM_SCALER;
	}
	g_cxt->v4l2_cxt.proc_status.frame_info.channel_id = v4l2_cfg.channel_id;

	if (RECOVERING != g_cxt->recover_status) {
		g_cxt->cap_cnt = 0;
	} else {
		g_cxt->cap_cnt = g_cxt->cap_cnt_for_err;
	}

	ret = camera_alloc_capture_buf(&buffer_info, g_cxt->total_cap_num, v4l2_cfg.channel_id);
	if (ret) {
		CMR_LOGE("Failed to alloc capture buffer");
		goto exit;
	}

	ret = cmr_v4l2_buff_cfg(&buffer_info);
	if (ret) {
		CMR_LOGE("Failed to Q capture buffer");
		goto exit;
	}

	if (IMG_DATA_TYPE_JPEG == v4l2_cfg.cfg.dst_img_fmt
		|| IMG_DATA_TYPE_RAW == v4l2_cfg.cfg.dst_img_fmt) {
		g_cxt->chn_0_status = CHN_BUSY;
		SET_CHN_BUSY(CHN_0);
	} else {
		g_cxt->chn_2_status = CHN_BUSY;
		SET_CHN_BUSY(CHN_2);
	}

	if (v4l2_cfg.cfg.need_isp && ISP_IDLE == g_cxt->isp_cxt.isp_state) {
		ret = camera_isp_start(CMR_CAPTURE,0,sensor_mode);
	}
	g_cxt->recover_status = NO_RECOVERY;
exit:
	return ret;
}

int camera_capture_init_continue(void)
{
	struct img_size          capture_size;
	int                      ret = CAMERA_SUCCESS;
	struct cap_cfg           v4l2_cfg;
	SENSOR_MODE_INFO_T       *sensor_mode;
	struct buffer_cfg        buffer_info;
	struct isp_video_start   isp_video_param;
	SENSOR_AE_INFO_T         *sensor_aec_info;
	struct sn_cfg            sensor_cfg;

	CMR_LOGV("capture size, %d %d, cap_rot %d",
		g_cxt->capture_size.width,
		g_cxt->capture_size.height,
		g_cxt->cap_rot);

	g_cxt->thum_ready = 0;
	CMR_LOGI("sn_cap_mode:%d.",g_cxt->sn_cxt.capture_mode);
	sensor_mode = &g_cxt->sn_cxt.sensor_info->sensor_mode_info[g_cxt->sn_cxt.capture_mode];
	sensor_cfg.sn_size.width  = sensor_mode->width;
	sensor_cfg.sn_size.height = sensor_mode->height;
	if (IS_NON_ZSL_MODE(g_cxt->cap_mode)) {
		sensor_cfg.frm_num = 1;
		ret = cmr_v4l2_sn_cfg(&sensor_cfg);
		if (ret) {
			CMR_LOGE("Failed to set V4L2 the size of sensor");
			return -CAMERA_FAILED;
		}
	}

	bzero(&v4l2_cfg, sizeof(struct cap_cfg));
	v4l2_cfg.channel_id = CHN_2;
	ret = camera_capture_ability(sensor_mode, &v4l2_cfg.cfg, &g_cxt->capture_size);
	if (ret) {
		CMR_LOGE("Failed to camera_capture_ability, %d", ret);
		goto exit;
	}

	if (IMG_DATA_TYPE_JPEG == v4l2_cfg.cfg.dst_img_fmt) {
		v4l2_cfg.channel_id = CHN_0;
	}
	v4l2_cfg.frm_num = 1;
	ret = cmr_v4l2_cap_cfg(&v4l2_cfg);
	if (ret) {
		CMR_LOGE("Can't support this capture configuration");
		goto exit;
	} else {
		g_cxt->thum_from = THUM_FROM_SCALER;
	}

	ret = camera_alloc_capture_buf(&buffer_info, g_cxt->total_cap_num, v4l2_cfg.channel_id);
	if (ret) {
		CMR_LOGE("Failed to alloc capture buffer");
		goto exit;
	}

	ret = cmr_v4l2_buff_cfg(&buffer_info);
	if (ret) {
		CMR_LOGE("Failed to Q capture buffer");
		goto exit;
	}

	if (IMG_DATA_TYPE_JPEG == v4l2_cfg.cfg.dst_img_fmt) {
		g_cxt->chn_0_status = CHN_BUSY;
		SET_CHN_BUSY(CHN_0);
	} else {
		g_cxt->chn_2_status = CHN_BUSY;
		SET_CHN_BUSY(CHN_2);
	}

	if (v4l2_cfg.cfg.need_isp && ISP_IDLE == g_cxt->isp_cxt.isp_state) {
		ret = camera_isp_start(CMR_CAPTURE,0,sensor_mode);
	}
	g_cxt->recover_status = NO_RECOVERY;
exit:
	return ret;
}

int camera_capture_init_raw(void)
{
	struct img_size          capture_size;
	int                      ret = CAMERA_SUCCESS;
	struct cap_cfg           v4l2_cfg;
	SENSOR_MODE_INFO_T       *sensor_mode;
	struct buffer_cfg        buffer_info;
	struct isp_video_start   isp_video_param;
	struct sn_cfg            sensor_cfg;

	CMR_LOGV("capture size, %d %d, cap_rot %d",
		g_cxt->capture_size.width,
		g_cxt->capture_size.height,
		g_cxt->cap_rot);

	g_cxt->thum_ready = 0;

	sensor_mode = &g_cxt->sn_cxt.sensor_info->sensor_mode_info[g_cxt->sn_cxt.capture_mode];

	CMR_LOGV("capture_mode=%d, w=%d, h=%d",
		g_cxt->sn_cxt.capture_mode, sensor_mode->width, sensor_mode->height);

	sensor_cfg.sn_size.width  = sensor_mode->width;
	sensor_cfg.sn_size.height = sensor_mode->height;
	sensor_cfg.frm_num        = 1;

	ret = cmr_v4l2_sn_cfg(&sensor_cfg);
	if (ret) {
		CMR_LOGE("Failed to set V4L2 the size of sensor");
		return -CAMERA_FAILED;
	}

	bzero(&v4l2_cfg, sizeof(struct cap_cfg));
	v4l2_cfg.channel_id               = CHN_0;
	v4l2_cfg.chn_deci_factor          = 0;
	v4l2_cfg.cfg.dst_img_fmt          = IMG_DATA_TYPE_RAW;

	ret = camera_capture_ability(sensor_mode, &v4l2_cfg.cfg, &g_cxt->capture_size);
	if (ret) {
		CMR_LOGE("Failed to camera_capture_ability, %d", ret);
		goto exit;
	}
	v4l2_cfg.frm_num = 1;
	ret = cmr_v4l2_cap_cfg(&v4l2_cfg);
	if (ret) {
		CMR_LOGE("Can't support this capture configuration");
		goto exit;
	} else {
		g_cxt->thum_from = THUM_FROM_SCALER;
	}

	g_cxt->cap_cnt = 0;
	g_cxt->total_cap_num = 1;
	ret = camera_alloc_capture_buf(&buffer_info, g_cxt->total_cap_num, v4l2_cfg.channel_id);
	if (ret) {
		CMR_LOGE("Failed to alloc capture buffer");
		goto exit;
	}

	ret = cmr_v4l2_buff_cfg(&buffer_info);
	if (ret) {
		CMR_LOGE("Failed to Q capture buffer");
		goto exit;
	}
	g_cxt->chn_0_status = CHN_BUSY;

exit:

	return ret;
}

int camera_get_sensor_capture_mode(struct img_size* target_size, uint32_t *work_mode)
{
	uint32_t                 width = 0, theight = 0, i;
	uint32_t                 search_width;
	uint32_t                 search_height;
	uint32_t                 target_mode = SENSOR_MODE_MAX;
	SENSOR_EXP_INFO_T        *sn_info = g_cxt->sn_cxt.sensor_info;
	uint32_t                 last_mode = SENSOR_MODE_PREVIEW_ONE;
	int                      ret = -CAMERA_FAILED;

	if (NULL == target_size || NULL == g_cxt->sn_cxt.sensor_info)
		return ret;
	search_width = target_size->width;
	search_height = target_size->height;

	CMR_LOGV("search_width = %d", search_width);
	for (i = SENSOR_MODE_PREVIEW_ONE; i < SENSOR_MODE_MAX; i++) {
		if (SENSOR_MODE_MAX != sn_info->sensor_mode_info[i].mode) {
			last_mode = i;
			width = sn_info->sensor_mode_info[i].width;
			theight = sn_info->sensor_mode_info[i].height;
			CMR_LOGV("width = %d", width);
			if (search_width <= width && search_height <= theight ) {
				target_mode = i;
				ret = CAMERA_SUCCESS;
				break;
			}
		}
	}

	if (i == SENSOR_MODE_MAX) {
		CMR_LOGV("can't find the right mode, use last available mode %d", last_mode);
		i = last_mode;
		target_mode = last_mode;
		ret = CAMERA_SUCCESS;
	}

	*work_mode = target_mode;
	CMR_LOGV("mode %d, width %d height %d",
		target_mode,
		sn_info->sensor_mode_info[i].width,
		sn_info->sensor_mode_info[i].height);

	return ret;
}

int camera_get_sensor_preview_mode(struct img_size* target_size, uint32_t *work_mode)
{
	uint32_t                 width = 0, height = 0, i, last_one = 0;
	uint32_t                 search_height;
	uint32_t                 target_mode = SENSOR_MODE_MAX;
	SENSOR_EXP_INFO_T        *sn_info = g_cxt->sn_cxt.sensor_info;
	int                      ret = -CAMERA_FAILED;

	if (NULL == target_size || NULL == g_cxt->sn_cxt.sensor_info || NULL == work_mode)
		return ret;
	search_height = target_size->height;

	CMR_LOGV("search_height = %d", search_height);
	for (i = SENSOR_MODE_PREVIEW_ONE; i < SENSOR_MODE_MAX; i++) {
		if (SENSOR_MODE_MAX != sn_info->sensor_mode_info[i].mode) {
			height = sn_info->sensor_mode_info[i].height;
			CMR_LOGV("height = %d", height);
			if (SENSOR_IMAGE_FORMAT_JPEG != sn_info->sensor_mode_info[i].image_format) {
				if (search_height <= height) {
					target_mode = i;
					ret = CAMERA_SUCCESS;
					break;
				} else {
					last_one = i;
				}
			}
		}
	}

	if (i == SENSOR_MODE_MAX) {
		CMR_LOGV("can't find the right mode, %d", i);
		target_mode = last_one;
	}
	CMR_LOGV("target_mode %d", target_mode);


	*work_mode = target_mode;
	return ret;
}

int camera_alloc_preview_buf(struct buffer_cfg *buffer, uint32_t format)
{
	uint32_t                 buffer_size, frame_size, frame_num, i, j;
	int                      ret = CAMERA_SUCCESS;

	if (NULL == buffer)
		return -CAMERA_INVALID_PARM;

	if (0 != g_cxt->cmr_set.preview_env) {
		buffer_size = ((g_cxt->display_size.width+15)&(~15)) * ((g_cxt->display_size.height+15)&(~15));
	} else {
		buffer_size = g_cxt->display_size.width * g_cxt->display_size.height;
	}
	if (IMG_DATA_TYPE_YUV420 == format) {
		frame_size = buffer_size * 3 / 2;
		CMR_LOGI("420.");
	} else if (IMG_DATA_TYPE_YUV422 == format) {
		frame_size = buffer_size * 2;
	} else {
		CMR_LOGE("Unsupported format %d", format);
		return -CAMERA_INVALID_PARM;
	}

	frame_size = camera_get_size_align_page(frame_size);
	if (NULL == g_cxt->prev_virt_addr_array || NULL == g_cxt->prev_phys_addr_array) {
		CMR_LOGE("Fail to malloc preview memory.");
		return -CAMERA_FAILED;
	}
	for (i = 0; i < CAMERA_PREV_FRM_CNT; i++) {
		if ((0 == g_cxt->prev_virt_addr_array[i]) || (0 == g_cxt->prev_phys_addr_array[i])) {
			CMR_LOGE("Fail to malloc preview memory.");
		         return  -CAMERA_FAILED;
		}
	}
	if (g_cxt->prev_rot) {
		for (i = CAMERA_PREV_FRM_CNT; i < CAMERA_PREV_FRM_CNT+ CAMERA_PREV_ROT_FRM_CNT; i++) {
			if ((0 == g_cxt->prev_virt_addr_array[i]) || (0 == g_cxt->prev_phys_addr_array[i])) {
				CMR_LOGE("Fail to malloc preview memory.");
		                  return  -CAMERA_FAILED;
			}
		}
	}

	frame_num = CAMERA_PREV_FRM_CNT;
	if (g_cxt->prev_rot) {
		frame_num += CAMERA_PREV_ROT_FRM_CNT;
	}

	if (frame_num > g_cxt->prev_mem_num || frame_size > g_cxt->prev_mem_size) {
		CMR_LOGE("prev_mem_size 0x%x, prev_mem_num %d, size 0x%x, num %d", g_cxt->prev_mem_size, g_cxt->prev_mem_num, frame_size, frame_num);
		return -CAMERA_NO_MEMORY;
	}

	CMR_LOGV("preview addr, vir 0x%p phy 0x%p", g_cxt->prev_virt_addr_array, g_cxt->prev_phys_addr_array);
	buffer->channel_id = CHN_1;
	buffer->base_id    = CAMERA_PREV_ID_BASE;
	buffer->count      = CAMERA_PREV_FRM_CNT;
	buffer->length     = frame_size;
	bzero((void*)&buffer->addr[0], (uint32_t)(V4L2_BUF_MAX*sizeof(struct img_addr)));
	CMR_LOGV("self restart %d, prev_id %d", g_cxt->prev_self_restart, g_cxt->prev_buf_id);
	if (g_cxt->prev_self_restart) {
		if (g_cxt->prev_buf_id == CAMERA_PREV_FRM_CNT - 1) {
			buffer->start_buf_id = 0;
		} else {
			buffer->start_buf_id = g_cxt->prev_buf_id + 1;
		}
	} else {
		buffer->start_buf_id = 0;
	}

	if (buffer->start_buf_id) {
		frame_num = 0;
		for (i = 0; i < buffer->start_buf_id; i++) {
			frame_num = (uint32_t)((buffer->start_buf_id + i) % CAMERA_PREV_FRM_CNT);
			g_cxt->prev_frm[i].addr_vir.addr_y = (uint32_t)g_cxt->prev_virt_addr_array[frame_num];
			g_cxt->prev_frm[i].addr_vir.addr_u = g_cxt->prev_frm[i].addr_vir.addr_y + buffer_size;

			g_cxt->prev_frm[i].addr_phy.addr_y = (uint32_t)g_cxt->prev_phys_addr_array[frame_num];
			g_cxt->prev_frm[i].addr_phy.addr_u = g_cxt->prev_frm[i].addr_phy.addr_y + buffer_size;
			g_cxt->prev_frm[i].fmt             = format;
			g_cxt->prev_frm[i].size.width      = g_cxt->preview_size.width;
			g_cxt->prev_frm[i].size.height     = g_cxt->preview_size.height;
			buffer->addr[i].addr_y = g_cxt->prev_frm[i].addr_phy.addr_y;
			buffer->addr[i].addr_u = g_cxt->prev_frm[i].addr_phy.addr_u;
		}
		CMR_LOGV("frame_num %d", frame_num);

		for (i = buffer->start_buf_id; i < CAMERA_PREV_FRM_CNT; i++) {
			frame_num ++;
			j = (uint32_t)(frame_num % CAMERA_PREV_FRM_CNT);
			g_cxt->prev_frm[i].addr_vir.addr_y = (uint32_t)g_cxt->prev_virt_addr_array[j];
			g_cxt->prev_frm[i].addr_vir.addr_u = g_cxt->prev_frm[i].addr_vir.addr_y + buffer_size;

			g_cxt->prev_frm[i].addr_phy.addr_y = (uint32_t)g_cxt->prev_phys_addr_array[j];
			g_cxt->prev_frm[i].addr_phy.addr_u = g_cxt->prev_frm[i].addr_phy.addr_y + buffer_size;
			g_cxt->prev_frm[i].fmt             = format;
			g_cxt->prev_frm[i].size.width      = g_cxt->preview_size.width;
			g_cxt->prev_frm[i].size.height     = g_cxt->preview_size.height;
			buffer->addr[i].addr_y = g_cxt->prev_frm[i].addr_phy.addr_y;
			buffer->addr[i].addr_u = g_cxt->prev_frm[i].addr_phy.addr_u;
		}
	}else {
		for (i = 0; i < CAMERA_PREV_FRM_CNT; i++) {
			g_cxt->prev_frm[i].addr_vir.addr_y = (uint32_t)g_cxt->prev_virt_addr_array[i];
			g_cxt->prev_frm[i].addr_vir.addr_u = g_cxt->prev_frm[i].addr_vir.addr_y + buffer_size;

			g_cxt->prev_frm[i].addr_phy.addr_y = (uint32_t)g_cxt->prev_phys_addr_array[i];
			g_cxt->prev_frm[i].addr_phy.addr_u = g_cxt->prev_frm[i].addr_phy.addr_y + buffer_size;
			g_cxt->prev_frm[i].fmt             = format;
			g_cxt->prev_frm[i].size.width      = g_cxt->preview_size.width;
			g_cxt->prev_frm[i].size.height     = g_cxt->preview_size.height;
			buffer->addr[i].addr_y = g_cxt->prev_frm[i].addr_phy.addr_y;
			buffer->addr[i].addr_u = g_cxt->prev_frm[i].addr_phy.addr_u;
		}
	}

	if (g_cxt->prev_rot) {
		for (i = 0; i < CAMERA_PREV_ROT_FRM_CNT; i++) {
			g_cxt->prev_rot_frm[i].addr_vir.addr_y = (uint32_t)g_cxt->prev_virt_addr_array[CAMERA_PREV_FRM_CNT+i];
			g_cxt->prev_rot_frm[i].addr_vir.addr_u = g_cxt->prev_rot_frm[i].addr_vir.addr_y + buffer_size;

			g_cxt->prev_rot_frm[i].addr_phy.addr_y = (uint32_t)g_cxt->prev_phys_addr_array[CAMERA_PREV_FRM_CNT+i];
			g_cxt->prev_rot_frm[i].addr_phy.addr_u = g_cxt->prev_rot_frm[i].addr_phy.addr_y + buffer_size;
			g_cxt->prev_rot_frm[i].fmt             = format;
			g_cxt->prev_rot_frm[i].size.width      = g_cxt->display_size.width;
			g_cxt->prev_rot_frm[i].size.height     = g_cxt->display_size.height;

		}
	}

	return ret;
}

int camera_capture_ability(SENSOR_MODE_INFO_T *sn_mode,
					struct img_frm_cap *img_cap,
					struct img_size *cap_size)
{
	int                      ret = CAMERA_SUCCESS;
	uint32_t                 tmp_width = 0;
	struct img_frm           *rot_frm;
	struct img_size          sensor_size;
	struct img_rect          sn_trim_rect;

	img_cap->need_isp = 0;
	if (SENSOR_IMAGE_FORMAT_YUV422 == sn_mode->image_format) {
		g_cxt->sn_cxt.sn_if.img_fmt = V4L2_SENSOR_FORMAT_YUV;
		g_cxt->cap_original_fmt = IMG_DATA_TYPE_YUV420;
		g_cxt->cap_zoom_mode = ZOOM_BY_CAP;
	} else if (SENSOR_IMAGE_FORMAT_RAW == sn_mode->image_format) {
		if (IMG_DATA_TYPE_RAW == img_cap->dst_img_fmt) {
			CMR_LOGV("Get RawData From RawRGB senosr");
			img_cap->need_isp = 0;
			g_cxt->cap_original_fmt = IMG_DATA_TYPE_RAW;
			g_cxt->cap_zoom_mode = ZOOM_POST_PROCESS;
		} else {
			if (sn_mode->width <= g_cxt->isp_cxt.width_limit) {
				CMR_LOGV("Need ISP to work at video mode");
				img_cap->need_isp = 1;
				g_cxt->cap_original_fmt = IMG_DATA_TYPE_YUV420;
				g_cxt->cap_zoom_mode = ZOOM_BY_CAP;
			} else {
				CMR_LOGV("change to CAMERA_RAW_MODE");

				img_cap->need_isp = 0;
				g_cxt->cap_original_fmt = IMG_DATA_TYPE_RAW;
				g_cxt->cap_zoom_mode = ZOOM_POST_PROCESS;
			}
		}
		g_cxt->sn_cxt.sn_if.img_fmt = V4L2_SENSOR_FORMAT_RAWRGB;
	} else if (SENSOR_IMAGE_FORMAT_JPEG == sn_mode->image_format) {
		g_cxt->sn_cxt.sn_if.img_fmt = V4L2_SENSOR_FORMAT_JPEG;
		g_cxt->cap_original_fmt = IMG_DATA_TYPE_JPEG;
		g_cxt->cap_zoom_mode = ZOOM_POST_PROCESS;
	} else {
		CMR_LOGE("Unsupported sensor format %d for capture", sn_mode->image_format);
		ret = -CAMERA_INVALID_FORMAT;
		goto exit;
	}

	img_cap->dst_img_fmt = g_cxt->cap_original_fmt;

	img_cap->notice_slice_height  = sn_mode->trim_height;
	img_cap->src_img_rect.start_x = sn_mode->trim_start_x;
	img_cap->src_img_rect.start_y = sn_mode->trim_start_y;
	img_cap->src_img_rect.width   = sn_mode->trim_width;
	img_cap->src_img_rect.height  = sn_mode->trim_height;
	ret = camera_get_trim_rect(&img_cap->src_img_rect, g_cxt->zoom_level, cap_size);
	if (ret) {
		CMR_LOGE("Failed to get trimming window for %d zoom level ", g_cxt->zoom_level);
		goto exit;
	}
	sn_trim_rect.start_x = img_cap->src_img_rect.start_x;
	sn_trim_rect.start_y = img_cap->src_img_rect.start_y;
	sn_trim_rect.width   = img_cap->src_img_rect.width;
	sn_trim_rect.height  = img_cap->src_img_rect.height;
	if (ZOOM_POST_PROCESS == g_cxt->cap_zoom_mode) {
		img_cap->src_img_rect.start_x = sn_mode->trim_start_x;
		img_cap->src_img_rect.start_y = sn_mode->trim_start_y;
		img_cap->src_img_rect.width   = sn_mode->trim_width;
		img_cap->src_img_rect.height  = sn_mode->trim_height;
		img_cap->dst_img_size.width   = sn_mode->trim_width;
		img_cap->dst_img_size.height  = sn_mode->trim_height;
		g_cxt->isp_cxt.drop_slice_num = sn_trim_rect.start_y / CMR_SLICE_HEIGHT;
		g_cxt->isp_cxt.drop_slice_cnt = 0;
		CMR_LOGI("drop cnt %d, drop num %d",
			g_cxt->isp_cxt.drop_slice_cnt,
			g_cxt->isp_cxt.drop_slice_num);
	} else {
		tmp_width = (uint32_t)(g_cxt->v4l2_cxt.sc_factor * img_cap->src_img_rect.width);
		if (img_cap->src_img_rect.width >= CAMERA_SAFE_SCALE_DOWN(g_cxt->capture_size.width) ||
			g_cxt->capture_size.width <= CMR_SCALING_TH) {
			/*if the out size is smaller than the in size, try to use scaler on the fly*/
			if (g_cxt->capture_size.width > tmp_width) {
				if (tmp_width > g_cxt->v4l2_cxt.sc_capability) {
					img_cap->dst_img_size.width  = g_cxt->v4l2_cxt.sc_capability;
				} else {
					img_cap->dst_img_size.width  = tmp_width;
				}
				img_cap->dst_img_size.height = (uint32_t)(img_cap->src_img_rect.height * g_cxt->v4l2_cxt.sc_factor);
			} else {
				/*just use scaler on the fly*/
				img_cap->dst_img_size.width  = g_cxt->capture_size.width;
				img_cap->dst_img_size.height = g_cxt->capture_size.height;
			}
		} else {
			/*if the out size is larger than the in size*/
			img_cap->dst_img_size.width   = img_cap->src_img_rect.width;
			img_cap->dst_img_size.height  = img_cap->src_img_rect.height;
		}

	}

	g_cxt->cap_orig_size.width    = img_cap->dst_img_size.width;
	g_cxt->cap_orig_size.height   = img_cap->dst_img_size.height;
	g_cxt->cap_orig_size_backup = g_cxt->cap_orig_size;
	CMR_LOGV("cap_orig_size %d %d", g_cxt->cap_orig_size.width, g_cxt->cap_orig_size.height);

	sensor_size.width  = sn_mode->width;
	sensor_size.height = sn_mode->height;
	if (!IS_NO_MALLOC_MEM) {
		g_cxt->cap_2_mems.free_mem(g_cxt->cap_2_mems.handle);
		ret = camera_arrange_capture_buf(&g_cxt->cap_2_mems,
					&sensor_size,
					&sn_trim_rect,
					&g_cxt->max_size,
					g_cxt->cap_original_fmt,
					&g_cxt->cap_orig_size,
					&g_cxt->thum_size,
					g_cxt->cap_mem,
					((IMG_ROT_0 != g_cxt->cap_rot) || g_cxt->is_cfg_rot_cap),
					g_cxt->total_cap_num);
	}
	if (0 == ret) {
		if ((IMG_ROT_0 != g_cxt->cap_rot) || g_cxt->is_cfg_rot_cap) {
			rot_frm = &g_cxt->cap_mem[0].cap_yuv_rot;

			if (0 == rot_frm->addr_phy.addr_y ||
				0 == rot_frm->addr_phy.addr_u ||
				0 == rot_frm->buf_size) {
				CMR_LOGE("No rotation buffer, %d", g_cxt->cap_rot);
				return -CAMERA_NO_MEMORY;
			}
		}
	}

exit:
	return ret;
}

int camera_alloc_capture_buf(struct buffer_cfg *buffer, uint32_t cap_number, uint32_t ch_id)
{
	uint32_t                 mem_size, buffer_size, frame_size, y_addr, u_addr, i;
	uint32_t                 y_addr_vir, u_addr_vir;
	int                      ret = CAMERA_SUCCESS;

	if (NULL == buffer)
		return -CAMERA_INVALID_PARM;

	buffer->channel_id = ch_id;
	buffer->base_id    = CAMERA_CAP0_ID_BASE;
	buffer->count      = CAMERA_CAP_FRM_CNT;
	buffer_size        = g_cxt->capture_size.width * g_cxt->capture_size.height;
	frame_size         = buffer_size * 3 / 2;
	for (i = 0; i < buffer->count; i++){
		if (IMG_DATA_TYPE_RAW == g_cxt->cap_original_fmt) {
			mem_size   = g_cxt->cap_mem[0].cap_raw.buf_size;
			y_addr     = g_cxt->cap_mem[0].cap_raw.addr_phy.addr_y;
			u_addr     = y_addr;
			y_addr_vir = g_cxt->cap_mem[0].cap_raw.addr_vir.addr_y;
			u_addr_vir = y_addr_vir;
			frame_size = buffer_size * RAWRGB_BIT_WIDTH / 8;
		} else if (IMG_DATA_TYPE_JPEG == g_cxt->cap_original_fmt) {
			mem_size   = g_cxt->cap_mem[0].target_jpeg.buf_size;
			y_addr     = g_cxt->cap_mem[0].target_jpeg.addr_phy.addr_y;
			u_addr     = y_addr;
			y_addr_vir = g_cxt->cap_mem[0].target_jpeg.addr_vir.addr_y;
			u_addr_vir = y_addr_vir;
			frame_size = CMR_JPEG_SZIE(g_cxt->capture_size.width, g_cxt->capture_size.height);
		} else if (IMG_DATA_TYPE_YUV420 == g_cxt->cap_original_fmt) {
			if ((IMG_ROT_0 != g_cxt->cap_rot) || g_cxt->is_cfg_rot_cap) {
				mem_size   = g_cxt->cap_mem[0].cap_yuv_rot.buf_size;
				y_addr     = g_cxt->cap_mem[0].cap_yuv_rot.addr_phy.addr_y;
				u_addr     = g_cxt->cap_mem[0].cap_yuv_rot.addr_phy.addr_u;
				y_addr_vir = g_cxt->cap_mem[0].cap_yuv_rot.addr_vir.addr_y;
				u_addr_vir = g_cxt->cap_mem[0].cap_yuv_rot.addr_vir.addr_u;
				frame_size = g_cxt->cap_orig_size.width * g_cxt->cap_orig_size.height * 3 / 2;
			} else {
				if (NO_SCALING) {
					mem_size   = g_cxt->cap_mem[0].target_yuv.buf_size;
					y_addr     = g_cxt->cap_mem[0].target_yuv.addr_phy.addr_y;
					u_addr     = g_cxt->cap_mem[0].target_yuv.addr_phy.addr_u;
					y_addr_vir = g_cxt->cap_mem[0].target_yuv.addr_vir.addr_y;
					u_addr_vir = g_cxt->cap_mem[0].target_yuv.addr_vir.addr_u;
				} else {
					mem_size   = g_cxt->cap_mem[0].cap_yuv.buf_size;
					y_addr     = g_cxt->cap_mem[0].cap_yuv.addr_phy.addr_y;
					u_addr     = g_cxt->cap_mem[0].cap_yuv.addr_phy.addr_u;
					y_addr_vir = g_cxt->cap_mem[0].cap_yuv.addr_vir.addr_y;
					u_addr_vir = g_cxt->cap_mem[0].cap_yuv.addr_vir.addr_u;

				}
				frame_size = buffer_size * 3 / 2;
			}
		} else {
			CMR_LOGE("Unsupported capture format!");
			ret = -CAMERA_NOT_SUPPORTED;
			break;
		}
		CMR_LOGV("capture addr, y 0x%x uv 0x%x", y_addr, u_addr);
		if (0 == y_addr || 0 == u_addr) {
			ret = -CAMERA_NO_MEMORY;
			break;
		}
		if (IMG_DATA_TYPE_JPEG == g_cxt->cap_original_fmt) {
			if ((frame_size - JPEG_EXIF_SIZE) > mem_size) {
				CMR_LOGE("Fail to malloc capture memory. 0x%x 0x%x 0x%x 0x%x",
					y_addr, u_addr, frame_size, mem_size);
				ret = -CAMERA_NO_MEMORY;
			break;
			}
		} else {
			if (frame_size > mem_size) {
				CMR_LOGE("Fail to malloc capture memory. 0x%x 0x%x 0x%x 0x%x",
					y_addr, u_addr, frame_size, mem_size);
				ret = -CAMERA_NO_MEMORY;
			break;
			}
		}

		g_cxt->cap_frm[i].addr_phy.addr_y = y_addr;
		g_cxt->cap_frm[i].addr_phy.addr_u = u_addr;

		g_cxt->cap_frm[i].addr_vir.addr_y = (uint32_t)g_cxt->cap_virt_addr + (uint32_t)(i * frame_size);
		g_cxt->cap_frm[i].addr_vir.addr_u = (uint32_t)g_cxt->cap_virt_addr + (uint32_t)(i * frame_size) + buffer_size;

		buffer->addr[i].addr_y = g_cxt->cap_frm[i].addr_phy.addr_y;
		buffer->addr[i].addr_u = g_cxt->cap_frm[i].addr_phy.addr_u;
	}
	buffer->length     = frame_size;

	return ret;
}

int camera_capture_max_img_size(uint32_t *max_width, uint32_t *max_height)
{
	int                      ret = CAMERA_SUCCESS;

	if (NULL == max_width || NULL == max_height) {
		return -CAMERA_INVALID_PARM;
	}

	ret = camera_capture_sensor_mode();
	if (ret) {
		CMR_LOGE("Failed to get sensor mode");
	}

	CMR_LOGV("%d %d", g_cxt->max_size.width, g_cxt->max_size.height);
	*max_width  = g_cxt->max_size.width;
	*max_height = g_cxt->max_size.height;

	return 0;
}

int camera_capture_get_buffer_size(uint32_t camera_id,
						uint32_t width,
						uint32_t height,
						uint32_t *size0,
						uint32_t *size1)
{
	struct img_size          local_size;
	int                      ret = CAMERA_SUCCESS;

	if (0 == width || 0 == height) {
		return -CAMERA_INVALID_PARM;
	}

	local_size.width  = width;
	local_size.height = height;

	ret = camera_capture_buf_size(camera_id,
					g_cxt->sn_cxt.sensor_info->image_format,
					&local_size,
					size0);

	return ret;
}

int camerea_set_preview_format(uint32_t pre_format)
{
	if (IS_PREVIEW)	{
		CMR_LOGE("Invalid camera status, 0x%x", pre_format);
		return -CAMERA_INVALID_STATE;
	}

	g_cxt->preview_fmt = pre_format;

	return CAMERA_SUCCESS;
}
int camera_set_preview_mem(uint32_t phy_addr, uint32_t vir_addr, uint32_t mem_size, uint32_t mem_num)
{
	if (0 == phy_addr || 0 == vir_addr || 0 == mem_size || 0 == mem_num)
		return -1;

	CMR_LOGV("phy_addr, 0x%x, vir_addr, 0x%x, mem_size 0x%x, mem_num %d", phy_addr, vir_addr, mem_size, mem_num);

	g_cxt->prev_phys_addr_array = (uint32_t*)phy_addr;
	g_cxt->prev_virt_addr_array = (uint32_t*)vir_addr;
	g_cxt->prev_mem_size = mem_size;
	g_cxt->prev_mem_num  = mem_num;

	return 0;
}

int camera_set_capture_mem(uint32_t     cap_index,
						uint32_t phy_addr0,
						uint32_t vir_addr0,
						uint32_t mem_size0,
						uint32_t phy_addr1,
						uint32_t vir_addr1,
						uint32_t mem_size1)
{
	struct img_size          max_size;
	int                      ret = CAMERA_SUCCESS;

	if (cap_index > CAMERA_CAP_FRM_CNT) {
		CMR_LOGE("Invalid cap_index %d", cap_index);
		return -CAMERA_NO_MEMORY;
	}
	if (0 == phy_addr0 || 0 == vir_addr0 || 0 == mem_size0) {
		CMR_LOGE("Invalid parameter 0x%x 0x%x 0x%x", phy_addr0, vir_addr0, mem_size0);
		return -CAMERA_NO_MEMORY;
	}

	g_cxt->cap_2_mems.mem_frm.buf_size = mem_size0;
	g_cxt->cap_2_mems.mem_frm.addr_phy.addr_y = phy_addr0;
	g_cxt->cap_2_mems.mem_frm.addr_vir.addr_y = vir_addr0;

	return ret;
}

int camera_set_capture_mem2(uint32_t     cap_index,
						uint32_t phy_addr0,
						uint32_t vir_addr0,
						uint32_t mem_size0,
						uint32_t alloc_mem,
						uint32_t free_mem,
						uint32_t handle)
{
	struct img_size          max_size;
	int                      ret = CAMERA_SUCCESS;

	if (cap_index > CAMERA_CAP_FRM_CNT) {
		CMR_LOGE("Invalid cap_index %d", cap_index);
		return -CAMERA_NO_MEMORY;
	}
	if (0 == phy_addr0 || 0 == vir_addr0 || 0 == mem_size0) {
		CMR_LOGE("Invalid parameter 0x%x 0x%x 0x%x", phy_addr0, vir_addr0, mem_size0);
		return -CAMERA_NO_MEMORY;
	}

	g_cxt->cap_2_mems.mem_frm.buf_size = mem_size0;
	g_cxt->cap_2_mems.mem_frm.addr_phy.addr_y = phy_addr0;
	g_cxt->cap_2_mems.mem_frm.addr_vir.addr_y = vir_addr0;

	g_cxt->cap_2_mems.alloc_mem = (alloc_mem_ptr)alloc_mem;
	g_cxt->cap_2_mems.free_mem = (free_mem_ptr)free_mem;
	g_cxt->cap_2_mems.handle = (void*)handle;

	return ret;
}

int camera_v4l2_preview_handle(struct frm_info *data)
{
	camera_frame_type        frame_type;
	int                      ret = CAMERA_SUCCESS;
	camera_cb_info           cb_info;

	memset(&frame_type,0,sizeof(camera_frame_type));
	CMR_PRINT_TIME;

	if (V4L2_IDLE == g_cxt->v4l2_cxt.v4l2_state) {
		CMR_LOGV("V4L2 Cap Stopped, skip this frtame");
		return ret;
	}

	if (CMR_IDLE == g_cxt->preview_status) {
		CMR_LOGV("discard.");
		return ret;
	}

	g_cxt->pre_frm_cnt++;

	if (IMG_SKIP_SW == g_cxt->skip_mode) {
		if ((g_cxt->pre_frm_cnt <= g_cxt->skip_num) || (1 == g_cxt->ae_wait_stab)) {
			CMR_LOGV("Ignore this frame, preview cnt %d, total skip num %d",
				g_cxt->pre_frm_cnt, g_cxt->skip_num);
			ret = cmr_v4l2_free_frame(data->channel_id, data->frame_id);
			return ret;
		}
	}

	if ((IMG_SKIP_SW == g_cxt->skip_mode) && g_cxt->restart_skip_en) {
		int64_t timestamp = data->sec * 1000000000LL + data->usec * 1000;
		CMR_LOGV("Restart skip: frame time = %lld, restart time=%lld \n", timestamp, g_cxt->restart_timestamp);
		if (timestamp > g_cxt->restart_timestamp) {
			pthread_mutex_lock(&g_cxt->main_prev_mutex);
			g_cxt->restart_skip_cnt++;
			pthread_mutex_unlock(&g_cxt->main_prev_mutex);

			if (g_cxt->restart_skip_cnt <= g_cxt->skip_num) {
				CMR_LOGV("Restart skip: Ignore this frame, preview cnt %d, total skip num %d",
					g_cxt->restart_skip_cnt, g_cxt->skip_num);
				ret = cmr_v4l2_free_frame(data->channel_id, data->frame_id);
				return ret;
			} else {
				pthread_mutex_lock(&g_cxt->main_prev_mutex);
				g_cxt->restart_skip_cnt = 0;
				g_cxt->restart_skip_en  = 0;
				pthread_mutex_unlock(&g_cxt->main_prev_mutex);
				CMR_LOGV("Restart skip: end \n");
			}
		} else {
			CMR_LOGV("Restart skip: frame is before restart, no need to skip this frame \n");
		}
	}

	/*if (g_cxt->cap_mode == CAMERA_HDR_MODE) {
		CMR_LOGV("Ignore this frame, HDR.");
		ret = cmr_v4l2_free_frame(data->channel_id, data->frame_id);
		return ret;
	}*/

	if (IMG_ROT_0 == g_cxt->prev_rot) {
		if(g_cxt->set_flag > 0){
			camera_set_done(g_cxt);
			g_cxt->set_flag--;
		}
		ret = camera_set_frame_type(&frame_type, data);
		g_cxt->prev_buf_id = frame_type.buf_id;
#if CB_LIGHT_SYNC
		camera_call_cb(CAMERA_EVT_CB_FRAME,
				camera_get_client_data(),
				CAMERA_FUNC_START_PREVIEW,
				(uint32_t)&frame_type);
#else
		memset(&cb_info, 0, sizeof(camera_cb_info));
		cb_info.cb_type = CAMERA_EVT_CB_FRAME;
		cb_info.cb_func = CAMERA_FUNC_START_PREVIEW;
		cb_info.cb_data = (void *)(&frame_type);
		cb_info.cb_data_length = sizeof(camera_frame_type);
		camera_callback_start(&cb_info);
#endif
	} else {
		CMR_LOGV("Need rotate");
		ret = camera_start_rotate(data);
	}
	pthread_mutex_lock(&g_cxt->recover_mutex);
	if (g_cxt->recover_status) {
		CMR_LOGV("Reset the recover status");
		g_cxt->recover_status = NO_RECOVERY;
	}
	pthread_mutex_unlock(&g_cxt->recover_mutex);
	return ret;
}

int camera_preview_err_handle(uint32_t evt_type)
{
	uint32_t                 rs_mode = RESTART_MAX;
	int                      ret = CAMERA_SUCCESS;

	if (RESTART == g_cxt->recover_status) {
		CMR_LOGE("No way to recover");
		return CAMERA_FAILED;
	}
	pthread_mutex_lock(&g_cxt->recover_mutex);

	switch (evt_type) {

	case CMR_V4L2_CSI2_ERR:
	case CMR_V4L2_TX_ERROR:

		if (RECOVERING == g_cxt->recover_status) {
			/* when in recovering */
			g_cxt->recover_cnt --;
			CMR_LOGI("recover_cnt, %d", g_cxt->recover_cnt);
			if (g_cxt->recover_cnt) {
			/* try once more */
				rs_mode = RESTART_MIDDLE;
			} else {
			/* tried three times, it hasn't recovered yet, restart */
				rs_mode = RESTART_HEAVY;
				g_cxt->recover_status = RESTART;
			}
		} else {
			/* not in recovering, start to recover by three times */
			rs_mode = RESTART_MIDDLE;
			g_cxt->recover_status = RECOVERING;
			g_cxt->recover_cnt = CAMERA_RECOVER_CNT;
			CMR_LOGI("Need recover, recover_cnt, %d", g_cxt->recover_cnt);
		}
		break;

	case CMR_SENSOR_ERROR:
	case CMR_V4L2_TIME_OUT:
		rs_mode = RESTART_HEAVY;
		/*g_cxt->preview_status = RESTART;*/
		CMR_LOGI("Sensor error, restart preview");
		break;

	}

	CMR_LOGV("rs_mode %d, recover_status %d", rs_mode, g_cxt->recover_status);

	ret = camera_before_set_internal(rs_mode);
	if (ret) {
		CMR_LOGV("Failed to stop preview %d", ret);
		pthread_mutex_unlock(&g_cxt->recover_mutex);
		return CAMERA_FAILED;
	}
	ret = camera_after_set_internal(rs_mode);
	if (ret) {
		CMR_LOGV("Failed to start preview %d", ret);
	}

	pthread_mutex_unlock(&g_cxt->recover_mutex);
	return ret;
}

int camera_capture_err_handle(uint32_t evt_type)
{
	uint32_t                 rs_mode = RESTART_MAX;
	int                      ret = CAMERA_SUCCESS;

	if (RESTART == g_cxt->recover_status) {
		CMR_LOGE("No way to recover");
		return CAMERA_FAILED;
	}

	pthread_mutex_lock(&g_cxt->recover_mutex);
	ret = cmr_v4l2_cap_stop();
	if (ret) {
		CMR_LOGE("Failed to stop v4l2 capture, %d", ret);
		pthread_mutex_unlock(&g_cxt->recover_mutex);
		return -CAMERA_FAILED;
	}
	g_cxt->sn_cxt.previous_sensor_mode = SENSOR_MODE_MAX;
	ret = Sensor_StreamOff();
	if (ret) {
		CMR_LOGE("Failed to switch off the sensor stream, %d", ret);
	}
	ret = cmr_v4l2_if_decfg(&g_cxt->sn_cxt.sn_if);
	if (ret) {
		CMR_LOGE("Failed to stop IF , %d", ret);
		pthread_mutex_unlock(&g_cxt->recover_mutex);
		return -CAMERA_FAILED;
	}

	CMR_PRINT_TIME;

	if (ISP_COWORK == g_cxt->isp_cxt.isp_state) {
			ret = isp_video_stop();
			g_cxt->isp_cxt.isp_state = ISP_IDLE;
			if (ret) {
				CMR_LOGE("Failed to stop ISP video mode, %d", ret);
			}
	}

	if (RECOVERING == g_cxt->recover_status) {
		g_cxt->recover_cnt --;
		CMR_LOGV("recover_cnt, %d", g_cxt->recover_cnt);
		if (0 == g_cxt->recover_cnt) {
			CMR_LOGE("restart fail.");
			ret = -CAMERA_FAILED;
		}
	} else {
		g_cxt->recover_status = RECOVERING;
		g_cxt->recover_cnt = CAMERA_RECOVER_CNT;
		CMR_LOGV("Need recover, recover_cnt, %d", g_cxt->recover_cnt);
	}
	if (CAMERA_SUCCESS == ret) {
		ret = camera_take_picture_internal(g_cxt->cap_mode);
		if (ret) {
			CMR_LOGE("restart fail.");
		}
	}

	CMR_LOGV("restart ret %d.",ret);
	pthread_mutex_unlock(&g_cxt->recover_mutex);
	return ret;
}

int camera_capture_yuv_process(struct frm_info *data)
{
	int                      ret = CAMERA_SUCCESS;

	CMR_LOGV("cap_zoom_mode %d, orig size %d %d, picture size %d %d, cap_original_fmt=%d, is_cfg_rot_cap=%d, cfg_cap_rot=%d",
		g_cxt->cap_zoom_mode,
		g_cxt->cap_orig_size.width,
		g_cxt->cap_orig_size.height,
		g_cxt->picture_size.width,
		g_cxt->picture_size.height,
		g_cxt->cap_original_fmt,
		g_cxt->is_cfg_rot_cap,
		g_cxt->cfg_cap_rot);

	TAKE_PIC_CANCEL;
	CMR_PRINT_TIME;

	if ((IMG_ROT_0 != g_cxt->cap_rot) || (g_cxt->is_cfg_rot_cap && (IMG_ROT_0 != g_cxt->cfg_cap_rot))) {
		if (IMG_ROT_0 != g_cxt->cfg_cap_rot) {
			uint32_t temp = 0;
			temp = g_cxt->picture_size.width;
			g_cxt->picture_size.width = g_cxt->picture_size.height;
			g_cxt->picture_size.height = temp;
			g_cxt->cap_rot = g_cxt->cfg_cap_rot;
		}
		ret = camera_start_rotate(data);
	} else {
		if ((!NO_SCALING) || (g_cxt->is_cfg_rot_cap && (IMG_ROT_0 == g_cxt->cfg_cap_rot))) {
			ret = camera_start_scale(data);
		} else {
			ret = camera_start_jpeg_encode(data);
			if (ret) {
				CMR_LOGE("Failed to start jpeg encode %d", ret);
			} else {
				if (IMG_DATA_TYPE_RAW != g_cxt->cap_original_fmt) {
					ret = camera_take_picture_done(data);
					if (ret) {
						CMR_LOGE("Failed to set take_picture done %d", ret);
					}
				}
			}
		}
	}

	return ret;

}

void camera_capture_hdr_data(struct frm_info *data)
{
	struct img_addr addr;
	uint32_t      size = g_cxt->cap_orig_size.width*g_cxt->cap_orig_size.height;
	uint32_t      frm_id = data->frame_id - CAMERA_CAP0_ID_BASE;
	CMR_LOGI(" s.");
	if (IMG_ROT_0 != g_cxt->cap_rot || g_cxt->is_cfg_rot_cap) {
		addr = g_cxt->cap_mem[frm_id].cap_yuv_rot.addr_vir;
	} else {
		if (NO_SCALING) {
			addr = g_cxt->cap_mem[frm_id].target_yuv.addr_vir;
		} else {
			addr = g_cxt->cap_mem[frm_id].cap_yuv.addr_vir;
		}
	}
	arithmetic_hdr_data(&addr, size,size/2,g_cxt->cap_cnt);
	/*camera_save_to_file(g_cxt->cap_cnt,
					IMG_DATA_TYPE_YUV420,
					g_cxt->picture_size.width,
					g_cxt->picture_size.height,
					&g_cxt->cap_mem[frm_id].target_yuv.addr_vir);*/
	CMR_LOGI(" e.");
}

int camera_v4l2_capture_handle(struct frm_info *data)
{
	camera_frame_type        frame_type;
	int                      ret = CAMERA_SUCCESS;
	struct frm_info *tmp_data = (struct frm_info *)data;
	camera_cb_info           cb_info;

	if (NULL == data) {
		CMR_LOGE("Invalid parameter, 0x%x", (uint32_t)data);
		return -CAMERA_INVALID_PARM;
	}

	TAKE_PIC_CANCEL;
	CMR_PRINT_TIME;
	if (CAMERA_HDR_MODE == g_cxt->cap_mode) {
			struct img_addr *data_addr;
			if (IMG_ROT_0 != g_cxt->cap_rot || g_cxt->is_cfg_rot_cap) {
				data_addr = &g_cxt->cap_mem[0].cap_yuv_rot.addr_vir;
			} else {
				if (NO_SCALING) {
					data_addr = &g_cxt->cap_mem[0].target_yuv.addr_vir;
				} else {
					data_addr = &g_cxt->cap_mem[0].cap_yuv.addr_vir;
				}
			}
			if(0 != arithmetic_hdr(data_addr,g_cxt->cap_orig_size.width,g_cxt->cap_orig_size.height)) {
				CMR_LOGE("hdr error.");
			}
			camera_call_cb(CAMERA_EVT_CB_FLUSH,
				camera_get_client_data(),
				CAMERA_FUNC_TAKE_PICTURE,
				0);
			arithmetic_hdr_deinit();
	} else if (CAMERA_TOOL_RAW_MODE == g_cxt->cap_mode) {
			CMR_LOGV("raw capture done: cap_original_fmt %d, cap_zoom_mode %d, rot %d",
				g_cxt->cap_original_fmt,
				g_cxt->cap_zoom_mode,
				g_cxt->cap_rot);
			ret = camera_take_picture_done(data);
			if (ret) {
				CMR_LOGE("Capture Raw: Failed to set take_picture done %d", ret);
			}
			return ret;
	} else if (CAMERA_RAW_MODE == g_cxt->cap_mode) {
		camera_stop_capture_raw_internal();
	}
	CMR_PRINT_TIME;
	CMR_LOGV("channel 0 capture done, cap_original_fmt %d, cap_zoom_mode %d, rot %d",
		g_cxt->cap_original_fmt,
		g_cxt->cap_zoom_mode,
		g_cxt->cap_rot);

#if CB_LIGHT_SYNC
	camera_call_cb(CAMERA_RSP_CB_SUCCESS,
		camera_get_client_data(),
		CAMERA_FUNC_TAKE_PICTURE,
		0);
#else
	memset(&cb_info, 0, sizeof(camera_cb_info));
	cb_info.cb_type = CAMERA_RSP_CB_SUCCESS;
	cb_info.cb_func = CAMERA_FUNC_TAKE_PICTURE;
	camera_callback_start(&cb_info);
#endif

	CMR_LOGV("Call done");
	TAKE_PICTURE_STEP(CMR_STEP_CAP_E);

	if ((IS_ZSL_MODE(g_cxt->cap_mode) || IS_WAIT_FOR_NORMAL_CONTINUE(g_cxt->cap_mode,g_cxt->cap_cnt)) &&
		(TAKE_PICTURE_NEEDED == camera_get_take_picture())) {
		if (CHN_0 != g_cxt->v4l2_cxt.proc_status.frame_info.channel_id) {
			CMR_LOGV("wait cap path IDLE ...");
			camera_wait_cap_path(g_cxt);
			CMR_LOGV("wait cap path IDLE OK");
		}
	} else {
		SET_CHN_IDLE(CHN_2);
	}

	switch (g_cxt->cap_original_fmt) {
	case IMG_DATA_TYPE_RAW:
		ret = camera_start_isp_process(data);
		break;
	case IMG_DATA_TYPE_JPEG:
		ret = camera_start_jpeg_decode(data);
		break;
	case IMG_DATA_TYPE_YUV420:
		ret = camera_capture_yuv_process(data);
		break;
	default:
		break;
	}

	return ret;
}


int camera_start_isp_process(struct frm_info *data)
{
	struct ips_in_param      ips_in;
	uint32_t                 frm_id;
	int                      ret = CAMERA_SUCCESS;
	struct ips_out_param     ips_out;
	uint32_t                 raw_pixel_width = 0;
	uint32_t raw_format;

	TAKE_PIC_CANCEL;

	if (NULL == data) {
		return -CAMERA_INVALID_PARM;
	}

	frm_id = data->frame_id - CAMERA_CAP0_ID_BASE;

	if (g_cxt->sn_cxt.sn_if.if_type) {
		ips_in.src_frame.img_fmt = ISP_DATA_CSI2_RAW10;
		raw_pixel_width=0xa;
		raw_format=0x08;
	} else {
		ips_in.src_frame.img_fmt = ISP_DATA_NORMAL_RAW10;
		raw_pixel_width=0x10;
		raw_format=0x04;
	}
	ips_in.src_frame.img_addr_phy.chn0 = g_cxt->cap_mem[frm_id].cap_raw.addr_phy.addr_y;
	ips_in.src_frame.img_size.w = g_cxt->cap_mem[frm_id].cap_raw.size.width;
	ips_in.src_frame.img_size.h = g_cxt->cap_mem[frm_id].cap_raw.size.height;
	ips_in.dst_frame.img_fmt = ISP_DATA_YUV420_2FRAME;
	if (NO_SCALING) {
		ips_in.dst_frame.img_addr_phy.chn0 = g_cxt->cap_mem[frm_id].target_yuv.addr_phy.addr_y;
		g_cxt->isp_cxt.cur_dst.addr_y = g_cxt->cap_mem[frm_id].target_yuv.addr_vir.addr_y;
		ips_in.dst_frame.img_addr_phy.chn1 = g_cxt->cap_mem[frm_id].target_yuv.addr_phy.addr_u;
		g_cxt->isp_cxt.cur_dst.addr_u = g_cxt->cap_mem[frm_id].target_yuv.addr_vir.addr_u;
		ips_in.dst_frame.img_size.w = g_cxt->cap_mem[frm_id].target_yuv.size.width;
		ips_in.dst_frame.img_size.h = g_cxt->cap_mem[frm_id].target_yuv.size.height;
	} else {
		ips_in.dst_frame.img_addr_phy.chn0 = g_cxt->cap_mem[frm_id].cap_yuv.addr_phy.addr_y;
		g_cxt->isp_cxt.cur_dst.addr_y = g_cxt->cap_mem[frm_id].cap_yuv.addr_vir.addr_y;
		ips_in.dst_frame.img_addr_phy.chn1 = g_cxt->cap_mem[frm_id].cap_yuv.addr_phy.addr_u;
		g_cxt->isp_cxt.cur_dst.addr_u = g_cxt->cap_mem[frm_id].cap_yuv.addr_vir.addr_u;
		ips_in.dst_frame.img_size.w = g_cxt->cap_mem[frm_id].cap_yuv.size.width;
		ips_in.dst_frame.img_size.h = g_cxt->cap_mem[frm_id].cap_yuv.size.height;
	}
	ips_in.src_avail_height = g_cxt->cap_mem[frm_id].cap_raw.size.height;

#if 0
	ips_in.src_slice_height = ips_in.src_avail_height;
	ips_in.dst_slice_height = ips_in.src_avail_height;
#else
	ips_in.src_slice_height = CMR_SLICE_HEIGHT;
	ips_in.dst_slice_height = CMR_SLICE_HEIGHT;
#endif

	send_capture_data(raw_format,/* raw */
			g_cxt->cap_mem[frm_id].cap_raw.size.width,
			g_cxt->cap_mem[frm_id].cap_raw.size.height,
			(char *)g_cxt->cap_mem[frm_id].cap_raw.addr_vir.addr_y,
			g_cxt->cap_mem[frm_id].cap_raw.size.width*g_cxt->cap_mem[frm_id].cap_raw.size.height * raw_pixel_width /8,
			0, 0, 0, 0);

#if 0
	camera_save_to_file(110,
			IMG_DATA_TYPE_RAW,
			g_cxt->cap_mem[frm_id].cap_raw.size.width,
			g_cxt->cap_mem[frm_id].cap_raw.size.height,
			&g_cxt->cap_mem[frm_id].cap_raw.addr_vir);
#endif

	ret = isp_proc_start(&ips_in, &ips_out);
	if (0 == ret) {
		CMR_LOGV("ISP post-process started");
		g_cxt->isp_cxt.isp_state = ISP_POST_PROCESS;
		g_cxt->isp_cxt.proc_status.slice_height_in = ips_in.dst_slice_height;
		g_cxt->isp_cxt.proc_status.slice_height_out = 0;
		g_cxt->isp_cxt.is_first_slice = 1;
		memcpy((void*)&g_cxt->isp_cxt.proc_status.frame_info,
			(void*)data,
			sizeof(struct frm_info));
		g_cxt->isp_cxt.proc_status.frame_info.data_endian.y_endian = 1;
		g_cxt->isp_cxt.proc_status.frame_info.data_endian.uv_endian = 1;
	} else {
		CMR_LOGV("Failed to start ISP, %d", ret);
	}
	return ret;
}

int camera_start_jpeg_decode(struct frm_info *data)
{
	struct jpeg_dec_in_param        dec_in;
	struct jpeg_dec_out_param       dec_out;
	uint32_t                 frm_id;
	struct img_frm           *frm;
	int                      ret = CAMERA_SUCCESS;

	TAKE_PIC_CANCEL;
	if (NULL == data || !IS_CAP_FRM(data->frame_id)) {
		CMR_LOGE("Parameter error, data 0x%x, frame id 0x%x",
			(uint32_t)data, data->frame_id);
		return -CAMERA_INVALID_PARM;
	}

	frm_id = data->frame_id - CAMERA_CAP0_ID_BASE;
	if (frm_id >= CAMERA_CAP_FRM_CNT) {
		CMR_LOGE("Wrong Frame id, 0x%x", data->frame_id);
		return -CAMERA_INVALID_PARM;
	}
	TAKE_PICTURE_STEP(CMR_STEP_JPG_DEC_S);
	frm = &g_cxt->cap_mem[frm_id].target_jpeg;
	dec_in.stream_buf_phy       = frm->addr_phy.addr_y;
	dec_in.stream_buf_vir       = frm->addr_vir.addr_y;
	dec_in.stream_buf_size      = data->length;
	dec_in.size.width           = g_cxt->cap_orig_size.width;
	dec_in.size.height          = g_cxt->cap_orig_size.height;
	dec_in.dst_endian.y_endian  = 1;
	dec_in.dst_endian.uv_endian = 2;
	dec_in.slice_height         = dec_in.size.height;

	if (IMG_ROT_0 == g_cxt->cap_rot) {
		if (NO_SCALING) {
			frm = &g_cxt->cap_mem[frm_id].target_yuv;
		} else {
			frm = &g_cxt->cap_mem[frm_id].cap_yuv;
		}
	} else {
		frm = &g_cxt->cap_mem[frm_id].cap_yuv_rot;
	}

	dec_in.dst_addr_phy.addr_y  = frm->addr_phy.addr_y;
	dec_in.dst_addr_phy.addr_u  = frm->addr_phy.addr_u;
	dec_in.dst_addr_vir.addr_y  = frm->addr_vir.addr_y;
	dec_in.dst_addr_vir.addr_u  = frm->addr_vir.addr_u;
	dec_in.dst_fmt              = IMG_DATA_TYPE_YUV420;

	dec_in.temp_buf_phy         = g_cxt->cap_mem[frm_id].jpeg_tmp.addr_phy.addr_y;
	dec_in.temp_buf_vir         = g_cxt->cap_mem[frm_id].jpeg_tmp.addr_vir.addr_y;
	dec_in.temp_buf_size        = g_cxt->cap_mem[frm_id].jpeg_tmp.buf_size;

	dec_in.slice_mod 			= JPEG_YUV_SLICE_ONE_BUF;
	ret = jpeg_dec_start(&dec_in, &dec_out);
	if (0 == ret) {
		CMR_LOGV("OK, handle 0x%x", dec_out.handle);
		g_cxt->jpeg_cxt.handle = dec_out.handle;
		g_cxt->jpeg_cxt.proc_status.slice_height_out = 0;
		g_cxt->jpeg_cxt.index = g_cxt->cap_cnt;
		memcpy((void*)&g_cxt->jpeg_cxt.proc_status.frame_info, data, sizeof(struct frm_info));
		g_cxt->jpeg_cxt.jpeg_state = JPEG_DECODE;
	} else {
		CMR_LOGV("Failed, 0x%x", ret);
		g_cxt->jpeg_cxt.jpeg_state = JPEG_ERR;
	}
	return ret;

}

int camera_jpeg_decode_next(struct frm_info *data)
{

	uint32_t                 frm_id;
	int                      ret = CAMERA_SUCCESS;
	struct jpeg_dec_next_param       dec_in;

	if (NULL == data) {
		return -CAMERA_INVALID_PARM;
	}
	bzero(&dec_in,sizeof(struct jpeg_dec_next_param));
	dec_in.handle = g_cxt->jpeg_cxt.handle;
	ret = jpeg_dec_next(&dec_in);
	return ret;

}

int camera_jpeg_encode_done(uint32_t thumb_stream_size)
{
	CMR_MSG_INIT(message);
	JPEGENC_CBrtnType        encoder_param;
	camera_encode_mem_type   encoder_type;
	struct img_frm           *jpg_frm;
	JINF_EXIF_INFO_T         *exif_ptr;
	struct jpeg_enc_exif_param      wexif_param;
	struct jpeg_wexif_cb_param    wexif_output;
	int                      ret = CAMERA_SUCCESS;

	memset(&wexif_param,0,sizeof(struct jpeg_enc_exif_param));
	jpg_frm = &g_cxt->cap_mem[g_cxt->jpeg_cxt.index].target_jpeg;

	CMR_LOGV("index %d, bitstream size %d", g_cxt->jpeg_cxt.index, jpg_frm->addr_vir.addr_u);
	/*camera_set_position(NULL,0,0);*/
	TAKE_PICTURE_STEP(CMR_STEP_JPG_ENC_E);
	TAKE_PICTURE_STEP(CMR_STEP_WR_EXIF_S);

	exif_ptr = camera_get_exif(g_cxt);
	bzero(&encoder_param, sizeof(JPEGENC_CBrtnType));
	encoder_param.header_size = 0;
	encoder_param.mode        = JPEGENC_MEM;
	encoder_type.buffer       = (uint8_t *)jpg_frm->addr_vir.addr_y;
	encoder_param.size        = jpg_frm->addr_vir.addr_u;
	encoder_param.outPtr      = &encoder_type;
	encoder_param.status      = JPEGENC_IMG_DONE;

	wexif_param.exif_ptr = exif_ptr;
	wexif_param.src_jpeg_addr_virt = (uint32_t)encoder_type.buffer;
	wexif_param.src_jpeg_size = encoder_param.size;
	wexif_param.target_addr_virt = wexif_param.src_jpeg_addr_virt - JPEG_EXIF_SIZE;
	wexif_param.target_size = JPEG_EXIF_SIZE + wexif_param.src_jpeg_size;
	jpg_frm = &g_cxt->cap_mem[g_cxt->jpeg_cxt.index].thum_jpeg;
	if ((0 != g_cxt->thum_size.width) && (0 != g_cxt->thum_size.height)) {
		wexif_param.thumbnail_addr_virt = jpg_frm->addr_vir.addr_y;
		wexif_param.thumbnail_size = thumb_stream_size;
	} else {
		wexif_param.thumbnail_addr_virt = 0;
		wexif_param.thumbnail_size = 0;
	}

	ret = jpeg_enc_add_eixf(&wexif_param,&wexif_output);
	TAKE_PICTURE_STEP(CMR_STEP_WR_EXIF_E);
	g_cxt->jpeg_cxt.jpeg_state = JPEG_IDLE;
	TAKE_PIC_CANCEL;
	if (0 == ret) {
		camera_wait_takepic_callback(g_cxt);
		CMR_LOGV("take pic done. cap cnt %d, total_cnt %d, cap_mode %d", g_cxt->cap_cnt,
			g_cxt->total_capture_num,
			g_cxt->cap_mode);
		encoder_param.need_free = 0;
		if ((g_cxt->cap_cnt == g_cxt->total_capture_num) ||
			(CAMERA_NORMAL_MODE == g_cxt->cap_mode) ||
			(CAMERA_HDR_MODE == g_cxt->cap_mode)) {
			encoder_param.need_free = 1;
		}

		encoder_type.buffer = (uint8_t *)wexif_output.output_buf_virt_addr;
		encoder_param.size  = wexif_output.output_buf_size;

		TAKE_PICTURE_STEP(CMR_STEP_CALL_BACK);
		camera_capture_step_statisic();

		camera_call_cb(CAMERA_EXIT_CB_DONE,
				camera_get_client_data(),
				CAMERA_FUNC_ENCODE_PICTURE,
				(uint32_t)&encoder_param);
	}

	message.msg_type = CMR_EVT_AFTER_CAPTURE;
	message.alloc_flag = 0;
	ret = cmr_msg_post(g_cxt->msg_queue_handle, &message);
	if (ret) {
		CMR_LOGE("Faile to send one msg to camera main thread");
	}

	camera_takepic_done(g_cxt);
	return ret;
}

static int camera_post_convert_thum_msg(void)
{
	CMR_MSG_INIT(message);
	int ret = CAMERA_SUCCESS;


	message.msg_type = CMR_EVT_CONVERT_THUM;
	message.alloc_flag = 0;
	ret = cmr_msg_post(g_cxt->msg_queue_handle, &message);
	if (ret) {
		CMR_LOGE("Fail to send one msg to camera main thread");
	}

	return ret;
}

int camera_start_jpeg_encode(struct frm_info *data)
{
	CMR_MSG_INIT(message);
	uint32_t                 frm_id;
	struct img_frm           *src_frm;
	struct img_frm           *target_frm;
	struct img_frm           *tmp_frm;
	int                      ret = CAMERA_SUCCESS;
	struct jpeg_enc_in_param  in_parm;
	struct jpeg_enc_out_param    out_parm;

	TAKE_PIC_CANCEL;

	TAKE_PICTURE_STEP(CMR_STEP_JPG_ENC_S);

	if (NULL == data || JPEG_ENCODE == g_cxt->jpeg_cxt.jpeg_state) {
		CMR_LOGV("wrong parameter 0x%x or status %d",
			(uint32_t)data,
			g_cxt->jpeg_cxt.jpeg_state);
		return -CAMERA_INVALID_PARM;
	}

	CMR_LOGV("channel_id %d, frame_id 0x%x", data->channel_id, data->frame_id);

	if ((CHN_2 == data->channel_id) || (CHN_0 == data->channel_id)) {
		frm_id = data->frame_id - CAMERA_CAP0_ID_BASE;
		if (frm_id >= CAMERA_CAP_FRM_CNT) {
			CMR_LOGE("Wrong Frame id, 0x%x", data->frame_id);
			return -CAMERA_INVALID_PARM;
		}
		src_frm = &g_cxt->cap_mem[frm_id].target_yuv;
		target_frm = &g_cxt->cap_mem[frm_id].target_jpeg;
	} else {
		frm_id = data->frame_id - CAMERA_CAP1_ID_BASE;
		if (frm_id >= CAMERA_CAP_FRM_CNT) {
			CMR_LOGE("Wrong Frame id, 0x%x", data->frame_id);
			return -CAMERA_INVALID_PARM;
		}
		src_frm    = &g_cxt->cap_mem[frm_id].thum_yuv;
		target_frm = &g_cxt->cap_mem[frm_id].thum_jpeg;
	}
	tmp_frm = &g_cxt->cap_mem[frm_id].jpeg_tmp;


	CMR_LOGE("slice_height=%d",data->height);

	in_parm.quality_level        = g_cxt->jpeg_cxt.quality;
	in_parm.slice_mod            = JPEG_YUV_SLICE_ONE_BUF;
	in_parm.size.width           = g_cxt->picture_size.width;
	in_parm.size.height          = g_cxt->picture_size.height;
	in_parm.src_addr_phy.addr_y  = src_frm->addr_phy.addr_y;
	in_parm.src_addr_phy.addr_u  = src_frm->addr_phy.addr_u;
	in_parm.src_addr_vir.addr_y  = src_frm->addr_vir.addr_y;
	in_parm.src_addr_vir.addr_u  = src_frm->addr_vir.addr_u;
	in_parm.slice_height         = data->height;
	in_parm.src_endian.y_endian  = data->data_endian.y_endian;
	in_parm.src_endian.uv_endian = data->data_endian.uv_endian;
	in_parm.stream_buf_phy       = target_frm->addr_phy.addr_y;
	in_parm.stream_buf_vir       = target_frm->addr_vir.addr_y;
	in_parm.stream_buf_size      = target_frm->buf_size;
#if 0
	in_parm.temp_buf_phy         = tmp_frm->addr_phy.addr_y;
	in_parm.temp_buf_vir         = tmp_frm->addr_vir.addr_y;
	in_parm.temp_buf_size        = tmp_frm->buf_size;
#else
	in_parm.temp_buf_phy         = 0;
	in_parm.temp_buf_vir         = 0;
	in_parm.temp_buf_size        = 0;
#endif

	CMR_LOGI("w h, %d %d, quality level %d", in_parm.size.width, in_parm.size.height,
		in_parm.quality_level);
	CMR_LOGI("slice height, %d, slice mode %d", in_parm.slice_height, in_parm.slice_mod);
	CMR_LOGI("phy addr 0x%x 0x%x, vir addr 0x%x 0x%x",
		in_parm.src_addr_phy.addr_y, in_parm.src_addr_phy.addr_u,
		in_parm.src_addr_vir.addr_y, in_parm.src_addr_vir.addr_u);

	CMR_LOGI("endian %d %d", in_parm.src_endian.y_endian, in_parm.src_endian.uv_endian);
	CMR_LOGI("stream phy 0x%x vir 0x%x, size 0x%x",
		in_parm.stream_buf_phy,
		in_parm.stream_buf_vir,
		in_parm.stream_buf_size);
	CMR_LOGI("temp_buf phy 0x%x vir 0x%x, size 0x%x",
		in_parm.temp_buf_phy,
		in_parm.temp_buf_vir,
		in_parm.temp_buf_size);

	g_cxt->jpeg_cxt.proc_status.frame_info = *data;
	g_cxt->jpeg_cxt.jpeg_state = JPEG_ENCODE;
	in_parm.out_size.width = g_cxt->actual_picture_size.width;
	in_parm.out_size.height = g_cxt->actual_picture_size.height;
	ret = jpeg_enc_start(&in_parm, &out_parm);
	if (0 == ret) {
		CMR_LOGV("OK, handle 0x%x", out_parm.handle);
		g_cxt->jpeg_cxt.handle = out_parm.handle;
		g_cxt->jpeg_cxt.proc_status.slice_height_in  = in_parm.slice_height;

		g_cxt->jpeg_cxt.proc_status.slice_height_out = 0;
		g_cxt->jpeg_cxt.index = frm_id;
	} else {
		CMR_LOGV("Failed, 0x%x", ret);
		g_cxt->jpeg_cxt.jpeg_state = JPEG_ERR;
	}

	if (in_parm.size.height == in_parm.slice_height) {
		ret = camera_post_convert_thum_msg();
	}

	return ret;
}

int camera_start_scale(struct frm_info *data)
{
	SENSOR_MODE_INFO_T       *sensor_mode;
	uint32_t                 frm_id;
	struct img_rect          rect;
	struct img_frm           src_frame, dst_frame;
	uint32_t                 slice_h = 0;
	uint32_t                 offset = 0;
	int                      ret = CAMERA_SUCCESS;
	struct scaler_context    *cxt = &g_cxt->scaler_cxt;
	struct frm_info          frm_data;


	TAKE_PIC_CANCEL;
	TAKE_PICTURE_STEP(CMR_STEP_SC_S);

	frm_data = *data;
	frm_data.channel_id = CHN_2;
	/*data->channel_id = CHN_2;*/
	frm_id = data->frame_id - CAMERA_CAP0_ID_BASE;

	if (g_cxt->cap_zoom_mode == ZOOM_BY_CAP) {
		rect.start_x = 0;
		rect.start_y = 0;
		rect.width   = g_cxt->cap_orig_size.width;
		rect.height  = g_cxt->cap_orig_size.height;
	} else {
		if (IMG_ROT_0 == g_cxt->cap_rot) {
			sensor_mode = &g_cxt->sn_cxt.sensor_info->sensor_mode_info[g_cxt->sn_cxt.capture_mode];
			rect.start_x = sensor_mode->trim_start_x;
			rect.start_y = sensor_mode->trim_start_y;
			rect.width   = sensor_mode->trim_width;
			rect.height  = sensor_mode->trim_height;
		} else {
			rect.start_x = 0;
			rect.start_y = 0;
			rect.width   = g_cxt->cap_orig_size.width;
			rect.height  = g_cxt->cap_orig_size.height;
		}
		ret = camera_get_trim_rect(&rect, g_cxt->zoom_level, &g_cxt->picture_size);
		if (ret) {
			CMR_LOGE("Failed to calculate scaling window, %d", ret);
			return ret;
		}
		if (IMG_DATA_TYPE_RAW == g_cxt->cap_original_fmt) {
#if 0
			g_cxt->isp_cxt.drop_slice_cnt ++;
			CMR_LOGV("drop slice cnt %d, drop total num %d, rect.start_y %d",
				g_cxt->isp_cxt.drop_slice_cnt,
				g_cxt->isp_cxt.drop_slice_num,
				rect.start_y);
			if (g_cxt->isp_cxt.drop_slice_cnt > g_cxt->isp_cxt.drop_slice_num) {
				rect.start_y -= (uint32_t)(g_cxt->isp_cxt.drop_slice_num * CMR_SLICE_HEIGHT);
				offset = (uint32_t)(g_cxt->isp_cxt.drop_slice_num *
						CMR_SLICE_HEIGHT * g_cxt->cap_orig_size.width);
				CMR_LOGV("New start_y %d, width %d, offset 0x%x",
					rect.start_y,
					g_cxt->cap_orig_size.width,
					offset);
			} else {
				CMR_LOGV("drop this slice");
				return ret;
			}
#endif
		} else {
			offset = (uint32_t)(rect.start_y * g_cxt->cap_orig_size.width);
			rect.start_y = 0;
			CMR_LOGV("New start_y %d, width %d, offset 0x%x",
				rect.start_y,
				g_cxt->cap_orig_size.width,
				offset);
		}
	}

	src_frame.size.width      = g_cxt->cap_orig_size.width;
	src_frame.size.height     = g_cxt->cap_orig_size.height;
	if (g_cxt->is_cfg_rot_cap && (IMG_ROT_0 == g_cxt->cfg_cap_rot)) {
		src_frame.addr_phy.addr_y = g_cxt->cap_mem[frm_id].cap_yuv_rot.addr_phy.addr_y + offset;
		src_frame.addr_phy.addr_u = g_cxt->cap_mem[frm_id].cap_yuv_rot.addr_phy.addr_u + (offset >> 1);
		src_frame.addr_phy.addr_v = g_cxt->cap_mem[frm_id].cap_yuv_rot.addr_phy.addr_v + (offset >> 1);
	} else {
		src_frame.addr_phy.addr_y = g_cxt->cap_mem[frm_id].cap_yuv.addr_phy.addr_y + offset;
		src_frame.addr_phy.addr_u = g_cxt->cap_mem[frm_id].cap_yuv.addr_phy.addr_u + (offset >> 1);
		src_frame.addr_phy.addr_v = g_cxt->cap_mem[frm_id].cap_yuv.addr_phy.addr_v + (offset >> 1);
	}

	dst_frame.size.width      = g_cxt->picture_size.width;
	dst_frame.size.height     = g_cxt->picture_size.height;
	memcpy((void*)&dst_frame.addr_phy,
		&g_cxt->cap_mem[frm_id].target_yuv.addr_phy,
		sizeof(struct img_addr));
	memcpy((void*)&dst_frame.addr_vir,
		&g_cxt->cap_mem[frm_id].target_yuv.addr_vir,
		sizeof(struct img_addr));

	slice_h = rect.height;

	dst_frame.fmt = IMG_DATA_TYPE_YUV420;
	cxt->proc_status.slice_height_out = 0;
	cxt->out_fmt = dst_frame.fmt;

	src_frame.fmt = IMG_DATA_TYPE_YUV420;
	src_frame.data_end = data->data_endian;
	dst_frame.data_end = data->data_endian;
	dst_frame.data_end.uv_endian = 1;
	CMR_LOGV("Data endian y, uv %d %d", data->data_endian.y_endian, data->data_endian.uv_endian);

	cxt->proc_status.frame_info = frm_data;
	cxt->proc_status.slice_height_in = slice_h;
	cxt->proc_status.is_encoding = 0;
	g_cxt->scaler_cxt.scale_state = IMG_CVT_SCALING;
	ret = cmr_scale_start(slice_h,
			&src_frame,
			&rect,
			&dst_frame,
			&g_cxt->cap_mem[frm_id].scale_tmp,
			NULL);
	if (ret) {
		CMR_LOGE("Failed to start scaler, %d", ret);
		g_cxt->scaler_cxt.scale_state = IMG_CVT_IDLE;
		return ret;
	}

	return ret;
}

int camera_scale_next(struct frm_info *data)
{
	struct scaler_context    *cxt = &g_cxt->scaler_cxt;
	int                      ret = CAMERA_SUCCESS;

	TAKE_PIC_CANCEL;

	ret = cmr_scale_next(0, NULL, NULL, NULL);

	return ret;
}

int camera_scale_done(struct frm_info *data)
{
	int                      ret = CAMERA_SUCCESS;
	struct frm_info          frm;

	memcpy((void*)&frm, (void*)data, sizeof(struct frm_info));
#if 0
	camera_save_to_file(0, IMG_DATA_TYPE_YUV420, g_cxt->capture_size.width, g_cxt->capture_size.height,
		&g_cxt->cap_mem[frm_id].target_yuv.addr_vir);
#endif
	TAKE_PICTURE_STEP(CMR_STEP_SC_E);
	CMR_PRINT_TIME;
	if (ret) {
		CMR_LOGV("Failed to deinit scaler, %d", ret);
		return ret;
	}

	if (CHN_2 == frm.channel_id) {
		ret = camera_take_picture_done(&frm);
		if (ret) {
			CMR_LOGE("Failed to set take_picture done %d", ret);
			return -CAMERA_FAILED;
		}
	}

	CMR_LOGV("channel id %d, frame id %d, height %d",
		frm.channel_id,
		frm.frame_id,
		g_cxt->capture_size.height);

	if ((THUM_FROM_CAP != g_cxt->thum_from) &&
		(0 != g_cxt->thum_size.width) &&
		(0 != g_cxt->thum_size.height)) {
		camera_scale_path_done(g_cxt);
	}

	return ret;
}

int camera_start_rotate(struct frm_info *data)
{
	uint32_t                 frm_id, rot_frm_id;
	struct img_rect          rect;
	int                      ret = CAMERA_SUCCESS;

	if (IS_PREVIEW && IS_PREV_FRM(data->frame_id)) {
		CMR_LOGE("Call Rotation in preview");
		if (IMG_CVT_ROTATING == g_cxt->rot_cxt.rot_state) {
			CMR_LOGE("Last rotate not finished yet, drop this frame");
			ret = cmr_v4l2_free_frame(data->channel_id, data->frame_id);
			return ret;
		} else {
			sem_wait(&g_cxt->rot_cxt.cmr_rot_sem);
			frm_id = data->frame_id - CAMERA_PREV_ID_BASE;
			rot_frm_id  = g_cxt->prev_rot_index % CAMERA_PREV_ROT_FRM_CNT;
			rect.start_x = 0;
			rect.start_y = 0;
			rect.width  = g_cxt->preview_size.width;
			rect.height = g_cxt->preview_size.height;
			g_cxt->rot_cxt.proc_status.frame_info = *data;
			g_cxt->rot_cxt.proc_status.slice_height_in = rect.height;
			g_cxt->rot_cxt.rot_state = IMG_CVT_ROTATING;
			ret = cmr_rot(g_cxt->prev_rot,
				&g_cxt->prev_frm[frm_id],
				&rect,
				&g_cxt->prev_rot_frm[rot_frm_id],
				NULL);
			if (ret) {
				g_cxt->rot_cxt.rot_state = IMG_CVT_ROT_DONE;
				sem_post(&g_cxt->rot_cxt.cmr_rot_sem);
				CMR_LOGE("Rot error");
			}
		}

	} else if (IS_CAP0_FRM(data->frame_id)) {
		TAKE_PIC_CANCEL;
		CMR_LOGE("Call Rotation after capture for channel %d, orig size %d %d rot_state %d",
			data->channel_id,
			g_cxt->cap_orig_size.width,
			g_cxt->cap_orig_size.height,
			g_cxt->rot_cxt.rot_state);

		sem_wait(&g_cxt->rot_cxt.cmr_rot_sem);
		frm_id = data->frame_id - CAMERA_CAP0_ID_BASE;
		rect.start_x = 0;
		rect.start_y = 0;
		rect.width  = g_cxt->cap_orig_size.width;
		rect.height = g_cxt->cap_orig_size.height;
		g_cxt->cap_mem[frm_id].cap_yuv_rot.size.width = rect.width;
		g_cxt->cap_mem[frm_id].cap_yuv_rot.size.height = rect.height;
		g_cxt->rot_cxt.proc_status.frame_info = *data;
		g_cxt->rot_cxt.proc_status.slice_height_in = rect.height;
		g_cxt->rot_cxt.rot_state = IMG_CVT_ROTATING;

		if (g_cxt->cap_orig_size.height == g_cxt->picture_size.width &&
			g_cxt->cap_orig_size.width == g_cxt->picture_size.height) {
			ret = cmr_rot(g_cxt->cap_rot,
				&g_cxt->cap_mem[frm_id].cap_yuv_rot,
				&rect,
				&g_cxt->cap_mem[frm_id].target_yuv,
				NULL);
		} else {
			ret = cmr_rot(g_cxt->cap_rot,
				&g_cxt->cap_mem[frm_id].cap_yuv_rot,
				&rect,
				&g_cxt->cap_mem[frm_id].cap_yuv,
				NULL);
		}
		if (ret) {
			g_cxt->rot_cxt.rot_state = IMG_CVT_ROT_DONE;
			sem_post(&g_cxt->rot_cxt.cmr_rot_sem);
			CMR_LOGE("Rot error");
		}
	}
	return ret;
}

int camera_get_data_redisplay(int output_addr,
				int output_width,
				int output_height,
				int input_addr_y,
				int input_addr_uv,
				int input_width,
				int input_height)
{
	struct img_frm           src_frame, dst_frame;
	uint32_t                 img_len = (uint32_t)(output_width * output_height);
	struct img_rect          rect;
	int                      ret = CAMERA_SUCCESS;
	uint32_t temp = 0;
	enum img_rot_angle angle = IMG_ROT_0;

	CMR_LOGV("input(w,h,addr)%d %d 0x%x output(w,h,addr)%d %d,0x%x rot %d",
		input_width, input_height, input_addr_y, output_width,output_height,
		output_addr,g_cxt->cfg_cap_rot);

	/* start scaling*/
	rect.start_x              = 0;
	rect.start_y              = 0;
	rect.width                = input_width;
	rect.height               = input_height;
	src_frame.size.width      = input_width;
	src_frame.size.height     = input_height;
	src_frame.fmt             = IMG_DATA_TYPE_YUV420;
	src_frame.data_end.y_endian = 1;
	src_frame.data_end.uv_endian = 1;
	src_frame.addr_phy.addr_y = input_addr_y;
	src_frame.addr_phy.addr_u = input_addr_uv;
	if (IMG_ROT_90 == g_cxt->cfg_cap_rot || IMG_ROT_270 == g_cxt->cfg_cap_rot)  {
		dst_frame.size.width      = output_height;
		dst_frame.size.height     = output_width;
		dst_frame.addr_phy.addr_y = output_addr + ((img_len * 3) >> 1);
		dst_frame.addr_phy.addr_u = dst_frame.addr_phy.addr_y + img_len;
	} else {
		dst_frame.size.width      = output_width;
		dst_frame.size.height     = output_height;
		dst_frame.addr_phy.addr_y = output_addr;
		dst_frame.addr_phy.addr_u = dst_frame.addr_phy.addr_y + img_len;
	}
	dst_frame.fmt             = IMG_DATA_TYPE_YUV420;
	dst_frame.data_end.y_endian = 1;
	dst_frame.data_end.uv_endian = 2;
	camera_sync_scale_start(g_cxt);
	cmr_scale_evt_reg(NULL);
	ret = cmr_scale_start(input_height,
			&src_frame,
			&rect,
			&dst_frame,
			NULL,
			NULL);
	cmr_scale_evt_reg(camera_scaler_evt_cb);
	camera_sync_scale_done(g_cxt);
	if (ret) {
		CMR_LOGV("dis scale fail, %d", ret);
		return -1;
	}

	/* start roattion*/
	if (IMG_ROT_0 != g_cxt->cfg_cap_rot) {
		if (IMG_ROT_90 == g_cxt->cfg_cap_rot) {
			angle = IMG_ROT_270;
		} else if (IMG_ROT_270 == g_cxt->cfg_cap_rot) {
			angle = IMG_ROT_90;
		} else {
			angle = g_cxt->cfg_cap_rot;
		}
		rect.start_x = 0;
		rect.start_y = 0;
		rect.width  = dst_frame.size.width;
		rect.height = dst_frame.size.height;
		src_frame.addr_phy.addr_y = dst_frame.addr_phy.addr_y;
		src_frame.addr_phy.addr_u = dst_frame.addr_phy.addr_u;
		src_frame.size.width = dst_frame.size.width;
		src_frame.size.height = dst_frame.size.height;
		src_frame.fmt = IMG_DATA_TYPE_YUV420;
		dst_frame.addr_phy.addr_y = output_addr;
		dst_frame.addr_phy.addr_u = dst_frame.addr_phy.addr_y + img_len;

		camera_sync_rotate_start(g_cxt);
		cmr_rot_evt_reg(NULL);
		ret = cmr_rot(angle,
				&src_frame,
				&rect,
				&dst_frame,
				NULL);
		cmr_rot_wait_done();
		cmr_rot_evt_reg(camera_rot_evt_cb);
		camera_sync_rotate_done(g_cxt);
		if (ret) {
			CMR_LOGV("dis rot fail, %d", ret);
			return -1;
		}
	}

	CMR_LOGV("Done, %d", ret);
	return ret;
}

struct camera_context *camera_get_cxt(void)
{
	return g_cxt;
}


static int camera_jpeg_encode_thumb(uint32_t *stream_size_ptr)
{
	struct img_frm           *src_frm;
	struct img_frm           *target_frm;
	struct jpeg_enc_in_param     in_parm;
	uint32_t                 sream_size;
	uint32_t                 frm_id = g_cxt->jpeg_cxt.index;
	int                      ret = CAMERA_SUCCESS;

	src_frm    = &g_cxt->cap_mem[frm_id].thum_yuv;
	target_frm = &g_cxt->cap_mem[frm_id].thum_jpeg;

	in_parm.src_addr_phy = src_frm->addr_phy;
	in_parm.src_addr_vir = src_frm->addr_vir;
	in_parm.stream_buf_phy = target_frm->addr_phy.addr_y;
	in_parm.stream_buf_vir = target_frm->addr_vir.addr_y;
	in_parm.size.width = g_cxt->thum_size.width;
	in_parm.size.height = g_cxt->thum_size.height;
	in_parm.slice_height = g_cxt->thum_size.height;
	in_parm.quality_level = g_cxt->jpeg_cxt.thumb_quality;
	in_parm.slice_mod = JPEG_YUV_SLICE_ONE_BUF;
	in_parm.size.width = g_cxt->thum_size.width;
	in_parm.size.height = g_cxt->thum_size.height;
	in_parm.stream_buf_size = target_frm->buf_size;
	ret = jpeg_enc_thumbnail(&in_parm, &sream_size);
	*stream_size_ptr = sream_size;
	CMR_LOGI("encode thumbnail return %d,stream size %d.",ret,sream_size);
	return ret;
}

static int camera_convert_to_thumb(void)
{
	struct img_frm           src_frame, dst_frame;
	uint32_t                 frm_id = g_cxt->jpeg_cxt.index;
	struct img_rect          rect;
	int                      ret = CAMERA_SUCCESS;

	if (!NO_SCALING) {
		camera_wait_scale_path(g_cxt);
	}

	if (IS_CHN_BUSY(CHN_2)) {
		CMR_LOGE("abnormal! path is busy yet! stop it");
		ret = cmr_v4l2_cap_pause(CHN_2, 0);
		SET_CHN_IDLE(CHN_2);
	}

	if (IMG_ROT_90 == g_cxt->cfg_cap_rot || IMG_ROT_270 == g_cxt->cfg_cap_rot) {
		uint32_t temp = 0;
		temp = g_cxt->thum_size.width;
		g_cxt->thum_size.width = g_cxt->thum_size.height;
		g_cxt->thum_size.height = temp;
	}
	src_frame.addr_phy.addr_y = g_cxt->cap_mem[frm_id].target_yuv.addr_phy.addr_y;
	src_frame.addr_phy.addr_u = g_cxt->cap_mem[frm_id].target_yuv.addr_phy.addr_u;
	src_frame.size.width      = g_cxt->picture_size.width;
	src_frame.size.height     = g_cxt->picture_size.height;
	src_frame.fmt             = IMG_DATA_TYPE_YUV420;
	src_frame.data_end.y_endian = 1;
	src_frame.data_end.uv_endian = 1;
	dst_frame.addr_phy.addr_y = g_cxt->cap_mem[frm_id].thum_yuv.addr_phy.addr_y;
	dst_frame.addr_phy.addr_u = g_cxt->cap_mem[frm_id].thum_yuv.addr_phy.addr_u;
	dst_frame.size.width      = g_cxt->thum_size.width;
	dst_frame.size.height     = g_cxt->thum_size.height;
	dst_frame.fmt             = IMG_DATA_TYPE_YUV420;
	dst_frame.data_end.y_endian = 1;
	dst_frame.data_end.uv_endian = 1;
	rect.start_x              = 0;
	rect.start_y              = 0;
	rect.width                = src_frame.size.width;
	rect.height               = src_frame.size.height;
	camera_sync_scale_start(g_cxt);
	cmr_scale_evt_reg(NULL);
	ret = cmr_scale_start(src_frame.size.height,
			&src_frame,
			&rect,
			&dst_frame,
			NULL,
			NULL);
	cmr_scale_evt_reg(camera_scaler_evt_cb);
	camera_sync_scale_done(g_cxt);
	CMR_LOGV("Done, %d", ret);
	g_cxt->thum_ready = 1;
	camera_convert_thum_done(g_cxt);
	if (ret) {
		CMR_LOGE("Failed to deinit scaler, %d", ret);
	}

	return ret;
}

uint32_t camera_get_rot_set(void)
{
	CMR_LOGI("rot set %d.",g_cxt->prev_rot);
	return g_cxt->prev_rot;
}

int camera_uv422_to_uv420(uint32_t dst, uint32_t src, uint32_t width, uint32_t height)
{
	int                      ret = CAMERA_SUCCESS;
	uint32_t                 i;

	CMR_LOGV("dst 0x%x, src 0x%x, w h %d %d", dst, src, width, height);
	for (i = 0; i < (height >> 1); i++) {
		memcpy((void*)dst, (void*)src, width);
		dst += width;
		src += (width << 1);
	}

	return ret;
}

int camera_isp_proc_handle(struct ips_out_param *isp_out)
{
	struct ipn_in_param        in_param;
	struct ips_out_param       out_param;
	struct process_status      *process = &g_cxt->isp_cxt.proc_status;
	uint32_t                   offset = 0;
	uint32_t                   frm_id = 0;
	uint32_t                   no_need = 0;
	struct jpeg_enc_next_param enc_nxt_param;
	int                        ret = CAMERA_SUCCESS;
	int                        is_jpeg_encode = 0;

	CMR_LOGV("total processed height %d", process->slice_height_out);

#if 0
	camera_save_to_file((uint32_t)(process->slice_height_out/CMR_SLICE_HEIGHT),
			IMG_DATA_TYPE_YUV422,
			g_cxt->cap_orig_size.width,
			CMR_SLICE_HEIGHT,
			&g_cxt->isp_cxt.cur_dst);

#endif

	process->slice_height_out += isp_out->output_height;


	is_jpeg_encode = camera_is_jpeg_encode_direct_process();

	if (g_cxt->isp_cxt.is_first_slice) {
		if (is_jpeg_encode) {
			process->frame_info.height = isp_out->output_height;
			ret = camera_capture_yuv_process(&process->frame_info);
		}
		g_cxt->isp_cxt.is_first_slice = 0;
	} else {
		if (is_jpeg_encode) {
			bzero(&enc_nxt_param, sizeof(struct jpeg_enc_next_param));
			enc_nxt_param.handle       = g_cxt->jpeg_cxt.handle;
			enc_nxt_param.slice_height = isp_out->output_height;
			enc_nxt_param.ready_line_num = process->slice_height_out;
			CMR_LOGV("Jpeg need more slice, %d %d",
				enc_nxt_param.slice_height,
				enc_nxt_param.ready_line_num);
			ret = jpeg_enc_next(&enc_nxt_param);
			if (ret) {
				CMR_LOGE("Failed to next jpeg encode %d", ret);
				return -CAMERA_FAILED;
			}
		} else {
#if 0
			CMR_LOGI("scale_state=%d",g_cxt->scaler_cxt.scale_state);

			if (g_cxt->scaler_cxt.scale_state != IMG_CVT_SCALING) {
				ret = camera_start_scale(&process->frame_info);
			} else {
				ret = camera_scale_next(&process->frame_info);
				if (CVT_RET_LAST == ret) {
					CMR_LOGE("No need to process next slice");
					return 0;
				}
			}
#endif
		}
	}

	if (process->slice_height_out == g_cxt->cap_orig_size.height) {
		if (is_jpeg_encode) {
			camera_post_convert_thum_msg();
			return camera_take_picture_done(&process->frame_info);
		} else {
			process->frame_info.height = process->slice_height_out;
			return camera_capture_yuv_process(&process->frame_info);
		}
	} else if (process->slice_height_out + process->slice_height_in <
		g_cxt->cap_orig_size.height) {
		in_param.src_slice_height = process->slice_height_in;
	} else {
		in_param.src_slice_height = g_cxt->cap_orig_size.height - process->slice_height_out;
		CMR_LOGV("last slice, %d", in_param.src_slice_height);
	}

	frm_id = process->frame_info.frame_id - CAMERA_CAP0_ID_BASE;
	offset = (uint32_t)(process->slice_height_out * g_cxt->cap_mem[frm_id].cap_raw.size.width);
	in_param.dst_addr_phy.chn0 = g_cxt->cap_mem[frm_id].cap_yuv.addr_phy.addr_y + offset;
	g_cxt->isp_cxt.cur_dst.addr_y = g_cxt->cap_mem[frm_id].cap_yuv.addr_vir.addr_y + offset;

	in_param.dst_addr_phy.chn1 = g_cxt->cap_mem[frm_id].cap_yuv.addr_phy.addr_u + (offset >> 1);
	g_cxt->isp_cxt.cur_dst.addr_u = g_cxt->cap_mem[frm_id].cap_yuv.addr_vir.addr_u + (offset >> 1);

	offset = (uint32_t)((offset * RAWRGB_BIT_WIDTH) >> 3);
	in_param.src_addr_phy.chn0 = g_cxt->cap_mem[frm_id].cap_raw.addr_phy.addr_y + offset;
	in_param.src_avail_height = g_cxt->cap_mem[frm_id].cap_raw.size.height;

	CMR_LOGV("next, src 0x%x, dst 0x%x 0x%x",
		in_param.src_addr_phy.chn0,
		in_param.dst_addr_phy.chn0,
		in_param.dst_addr_phy.chn1);

	ret = isp_proc_next(&in_param, &out_param);

	return ret;

}
void camera_set_start_facedetect(uint32_t param)
{
	g_cxt->arithmetic_cxt.fd_flag = param;
	CMR_LOGV("param %d.",param);
}

int camera_set_fd_mem(uint32_t phy_addr, uint32_t vir_addr, uint32_t mem_size)
{
	CMR_LOGV("phy_addr, 0x%x, vir_addr, 0x%x, mem_size 0x%x", phy_addr, vir_addr, mem_size);
	arithmetic_set_mem(phy_addr,vir_addr,mem_size);

	return 0;
}

int camera_set_change_size(uint32_t cap_width,uint32_t cap_height,uint32_t preview_width,uint32_t preview_height)
{
	int ret = 0;

	CMR_LOGI("start.");
	if (CMR_PREVIEW == g_cxt->preview_status) {
		if ((g_cxt->display_size.width != preview_width)
			|| (g_cxt->display_size.height != preview_height)) {
			CMR_LOGI("need to change size.");
			ret = 1;
		}
		if ((g_cxt->picture_size.width != CAMERA_ALIGNED_16(cap_width))
		   || (g_cxt->picture_size.height != CAMERA_ALIGNED_16(cap_height))) {
			if (IS_ZSL_MODE(g_cxt->cap_mode)) {
				CMR_LOGI("need to change size for ZSL.");
				ret = 2;
			} else {
				g_cxt->is_reset_if_cfg = 1;
			}
		}
	}
	CMR_LOGI("done,%d.",ret);
	return ret;
}

int camera_set_cancel_capture(int set_val)
{
	int        ret = CAMERA_SUCCESS;

	pthread_mutex_lock(&g_cxt->cancel_mutex);
	g_cxt->cap_canceled = set_val;
	pthread_mutex_unlock(&g_cxt->cancel_mutex);

	CMR_LOGV("cap_canceled %d", g_cxt->cap_canceled);
	return ret;
}

int camera_capture_need_exit(void)
{
	int        ret = 0;

	pthread_mutex_lock(&g_cxt->cancel_mutex);
	ret = g_cxt->cap_canceled == 1 ? 1 : 0;
	pthread_mutex_unlock(&g_cxt->cancel_mutex);

	return ret;
}

int camera_cb_thread_init(void)
{
	CMR_MSG_INIT(message);
	int                      ret = CAMERA_SUCCESS;
	pthread_attr_t           attr;

	CMR_LOGV("inited, %d", g_cxt->cb_inited);

	CMR_PRINT_TIME;
	if (!g_cxt->cb_inited) {
		ret = cmr_msg_queue_create(CAMERA_CB_MSG_QUEUE_SIZE, &g_cxt->cb_msg_que_handle);
		if (ret) {
			CMR_LOGE("NO Memory, Failed to create callback message queue \n");
			return ret;
		}
		sem_init(&g_cxt->cb_sync_sem, 0, 0);
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		ret = pthread_create(&g_cxt->cb_thread, &attr, camera_cb_thread_proc, NULL);
		sem_wait(&g_cxt->cb_sync_sem);
		g_cxt->cb_inited = 1;
		message.msg_type = CMR_EVT_CB_INIT;
		message.data = NULL;
		ret = cmr_msg_post(g_cxt->cb_msg_que_handle, &message);
		if (ret) {
			CMR_LOGE("Fail to send one msg to callback thread");
		}
	}
	return ret;
}

int camera_cb_thread_deinit(void)
{
	CMR_MSG_INIT(message);
	int                      ret = CAMERA_SUCCESS;

	CMR_LOGV("inited, %d", g_cxt->cb_inited);

	if (g_cxt->cb_inited) {
		message.msg_type = CMR_EVT_CB_EXIT;
		ret = cmr_msg_post(g_cxt->cb_msg_que_handle, &message);
		if (ret) {
			CMR_LOGE("Fail to send one msg to camera callback thread");
		}
		sem_wait(&g_cxt->cb_sync_sem);
		sem_destroy(&g_cxt->cb_sync_sem);
		cmr_msg_queue_destroy(g_cxt->cb_msg_que_handle);
		g_cxt->cb_msg_que_handle = 0;
		g_cxt->cb_inited = 0;
	}
	return ret ;
}

void *camera_cb_thread_proc(void *data)
{
	CMR_MSG_INIT(message);
	int                      ret = CAMERA_SUCCESS;
	int                      exit_flag = 0;
	camera_cb_info         * cbInfo = NULL;

	CMR_PRINT_TIME;
	sem_post(&g_cxt->cb_sync_sem);
	CMR_PRINT_TIME;
	while (1) {
		ret = cmr_msg_get(g_cxt->cb_msg_que_handle, &message);
		if (ret) {
			CMR_LOGE("callback thread: Message queue destroied");
			break;
		}

		switch (message.msg_type) {
		case CMR_EVT_CB_INIT:
			CMR_PRINT_TIME;
			CMR_LOGE("callback thread inited\n");
			CMR_PRINT_TIME;
			break;

		case CMR_EVT_CB_HANDLE:
			if (NULL != message.data) {
				cbInfo = (camera_cb_info *)message.data;
				CMR_LOGE("callback thread: message.msg_type 0x%x, cb func %d cb type %d",
				message.msg_type, cbInfo->cb_func, cbInfo->cb_type);

				CMR_PRINT_TIME;
				camera_callback_handle(cbInfo->cb_type,
							cbInfo->cb_func,
							(int32_t)(cbInfo->cb_data));
				CMR_PRINT_TIME;

				if (cbInfo->refer_data_length) {
					free(cbInfo->refer_data);
					cbInfo->refer_data = 0;
					cbInfo->refer_data_length = 0;
				}

				if (cbInfo->cb_data_length) {
					free(cbInfo->cb_data);
					cbInfo->cb_data = 0;
					cbInfo->cb_data_length = 0;
				}
				break;
			} else {
				CMR_LOGE("NULL message data");
				continue;
			}

		case CMR_EVT_CB_EXIT:
			CMR_LOGV("callbck thread CMR_EVT_CB_EXIT \n");
			exit_flag = 1;
			sem_post(&g_cxt->cb_sync_sem);
			CMR_PRINT_TIME;
			break;

		default:
			break;
		}

		if (1 == message.alloc_flag) {
			if (message.data) {
				free(message.data);
				message.data = 0;
			}
		}

		if (exit_flag) {
			CMR_LOGI("callback thread exit ");
			break;
		}
	}

	CMR_LOGI("callback thread exit done \n");

	return NULL;
}

void camera_callback_start(camera_cb_info *cb_info)
{
	CMR_MSG_INIT(message);
	int ret = CAMERA_SUCCESS;
	camera_cb_info *msg_cb_info = NULL;

	CMR_LOGV("get cb_data,0x%x, cb data length %d refer data 0x%x, refer data length 0x%x",
		(uint32_t)cb_info->cb_data, (uint32_t)cb_info->cb_data_length,
		(uint32_t)cb_info->refer_data, (uint32_t)cb_info->refer_data_length);
	message.data = malloc(sizeof(camera_cb_info));
	if (NULL == message.data) {
		CMR_LOGE("NO mem, Fail to alloc memory for one msg");
		return;
	}
	message.alloc_flag = 1;
	memcpy(message.data, cb_info, sizeof(camera_cb_info));
	msg_cb_info = (camera_cb_info *)message.data;
	/*if have callback data, malloc callback data, and set the callback data pointer*/
	if (msg_cb_info->cb_data_length) {
		msg_cb_info->cb_data = malloc(sizeof(msg_cb_info->cb_data_length));
		if ((NULL == msg_cb_info->cb_data) || (NULL == cb_info->cb_data)) {
			free(message.data);
			CMR_LOGE("NO mem, Fail to alloc memory or null for cb data!");
			return;
		} else {
			memcpy(msg_cb_info->cb_data, cb_info->cb_data, sizeof(msg_cb_info->cb_data_length));
		}
	}

	/*if have refer data, malloc refer data, and set the refer data pointer*/
	if (msg_cb_info->refer_data_length) {
		msg_cb_info->refer_data = malloc(sizeof(msg_cb_info->refer_data_length));
		if ((NULL == msg_cb_info->refer_data) || (NULL == cb_info->refer_data)) {
			free(msg_cb_info->cb_data);
			free(message.data);
			CMR_LOGE("NO mem, Fail to alloc memory or null for cb refer data!");
			return;
		} else {
			memcpy(msg_cb_info->refer_data, cb_info->refer_data, sizeof(msg_cb_info->refer_data_length));
		}
	}

	CMR_LOGE("send cb_data,0x%x, cb data length %d refer data 0x%x, refer data length 0x%x",
		(uint32_t)msg_cb_info->cb_data, (uint32_t)msg_cb_info->cb_data_length,
		(uint32_t)msg_cb_info->refer_data, (uint32_t)msg_cb_info->refer_data_length);

	message.msg_type = CMR_EVT_CB_HANDLE;
	ret = cmr_msg_post(g_cxt->cb_msg_que_handle, &message);
	if (ret) {
		if (msg_cb_info->refer_data) {
			free(msg_cb_info->refer_data);
		}
		if (msg_cb_info->cb_data) {
			free(msg_cb_info->cb_data);
		}
		free(message.data);
		CMR_LOGE("Fail to send one msg to camera callback thread");
	}

	return;
}

void camera_callback_handle(camera_cb_type cb, camera_func_type func, int32_t cb_param)
{
	int ret = CAMERA_SUCCESS;
	CMR_LOGV("callback handle start!");

	if (g_cxt->camera_cb) {
		/*camera_cb_type cb, const void * client_data, camera_func_type func, int32_t parm4*/
		(*g_cxt->camera_cb)(cb,
				camera_get_client_data(),
				func,
				cb_param);
	} else {
		CMR_LOGE("camera callback function error!");
	}

	CMR_LOGV("callback handle done!");
	return;
}

int camera_get_preview_rect(int *rect_x, int *rect_y, int *rect_width, int *rect_height)
{
	int ret = 0;
	SENSOR_MODE_INFO_T       *sensor_mode;

	if (NULL == g_cxt->sn_cxt.sensor_info) {
		CMR_LOGE("sensor info is NULL.");
		return -1;
	}

	sensor_mode = &g_cxt->sn_cxt.sensor_info->sensor_mode_info[g_cxt->sn_cxt.capture_mode];
	if (NULL == sensor_mode) {
		CMR_LOGE("sensor mode %d is NULL.",g_cxt->sn_cxt.preview_mode);
		return -1;
	}

	/*if ((g_cxt->preview_rect.width > sensor_mode->width) ||
		(g_cxt->preview_rect.height > sensor_mode->height)) {
		CMR_LOGE("camera_get_preview_rect error: preview rect: %d, %d is greate than sensor output: %d %d \n",
			g_cxt->preview_rect.width, g_cxt->preview_rect.height, sensor_mode->width, sensor_mode->height);

		return -1;
	}*/

	*rect_x      = g_cxt->preview_rect.start_x;
	*rect_y      = g_cxt->preview_rect.start_y;
	*rect_width  = g_cxt->preview_rect.width;
	*rect_height = g_cxt->preview_rect.height;

	CMR_LOGE("camera_get_preview_rect: x=%d, y=%d, w=%d, h=%d \n",
		*rect_x, *rect_y, *rect_width, *rect_height);

	return ret;
}

int camera_is_need_stop_preview(void)
{
	int        ret = 0;
	if (IS_PREVIEW) {
		ret = (g_cxt->sn_cxt.capture_mode == g_cxt->sn_cxt.preview_mode) ? 0 : 1;
	}
	CMR_LOGI("need:%d.",ret);
	return ret;
}

int camera_get_is_noscale(void)
{
	return NO_SCALING;
}

void camera_isp_ae_stab_set (uint32_t is_ae_stab_eb)
{

	g_cxt->is_isp_ae_stab_eb = is_ae_stab_eb;

}

int camera_capture_get_max_size(SENSOR_MODE_INFO_T *sn_mode, uint32_t *io_width, uint32_t *io_height)
{
	uint32_t                   zoom_mode = ZOOM_BY_CAP;
	uint32_t                   original_fmt = IMG_DATA_TYPE_YUV420;
	uint32_t                   need_isp = 0;
	struct img_rect            img_rc;
	struct img_size            img_sz;
	uint32_t                   tmp_width;
	int                        ret = CAMERA_SUCCESS;

	img_sz.width  = *io_width;
	img_sz.height = *io_height;
	if (SENSOR_IMAGE_FORMAT_YUV422 == sn_mode->image_format) {
		original_fmt = IMG_DATA_TYPE_YUV420;
		zoom_mode = ZOOM_BY_CAP;
	} else if (SENSOR_IMAGE_FORMAT_RAW == sn_mode->image_format) {
		if (sn_mode->width <= g_cxt->isp_cxt.width_limit) {
			CMR_LOGV("Need ISP to work at video mode");
			need_isp = 1;
			original_fmt = IMG_DATA_TYPE_YUV420;
			zoom_mode = ZOOM_BY_CAP;
		} else {
			CMR_LOGV("Need to process raw data");
			need_isp = 0;
			original_fmt = IMG_DATA_TYPE_RAW;
			zoom_mode = ZOOM_POST_PROCESS;
		}
	} else if (SENSOR_IMAGE_FORMAT_JPEG == sn_mode->image_format) {
		original_fmt = IMG_DATA_TYPE_JPEG;
		zoom_mode = ZOOM_POST_PROCESS;
	} else {
		CMR_LOGE("Unsupported sensor format %d for capture", sn_mode->image_format);
		ret = -CAMERA_INVALID_FORMAT;
		goto exit;
	}

	img_rc.start_x = sn_mode->trim_start_x;
	img_rc.start_y = sn_mode->trim_start_y;
	img_rc.width   = sn_mode->trim_width;
	img_rc.height  = sn_mode->trim_height;

	ret = camera_get_trim_rect(&img_rc, g_cxt->zoom_level, &img_sz);
	if (ret) {
		CMR_LOGE("Failed to get trimming window for %d zoom level ", g_cxt->zoom_level);
		goto exit;
	}
	if (ZOOM_POST_PROCESS == zoom_mode) {
		if (*io_width < sn_mode->width) {
			*io_width  = sn_mode->width;
			*io_height = sn_mode->height;
		}
	} else {
		tmp_width = (uint32_t)(g_cxt->v4l2_cxt.sc_factor * img_rc.width);
		if (img_rc.width >= CAMERA_SAFE_SCALE_DOWN(g_cxt->capture_size.width) ||
			g_cxt->capture_size.width <= CMR_SCALING_TH) {
			/*if the out size is smaller than the in size, try to use scaler on the fly*/
			if (g_cxt->capture_size.width > tmp_width) {
				if (tmp_width > g_cxt->v4l2_cxt.sc_capability) {
					img_sz.width  = g_cxt->v4l2_cxt.sc_capability;
				} else {
					img_sz.width  = tmp_width;
				}
				img_sz.width = (uint32_t)(img_rc.height * g_cxt->v4l2_cxt.sc_factor);
			} else {
				/*just use scaler on the fly*/
				img_sz.width  = g_cxt->capture_size.width;
				img_sz.height = g_cxt->capture_size.height;
			}
		} else {
			/*if the out size is larger than the in size*/
			img_sz.width   = img_rc.width;
			img_sz.height  = img_rc.height;
		}

		*io_width  = MAX(*io_width, img_sz.width);
		*io_height = MAX(*io_height, img_sz.height);
	}
exit:
	return ret;
}

int camera_get_is_nonzsl(void)
{
	return IS_NON_ZSL_MODE(g_cxt->cap_mode);
}

void camera_capture_step_statisic(void)
{
	int i = 0, time_delta = 0;

	ALOGE("*********************Take picture statistic*******Start******************");

	for (i = 0; i < CMR_STEP_MAX; i++) {
		if (i == 0) {
			ALOGE("%20s, %10d",
				cap_stp[i].step_name,
				0);
			continue;
		}

		if (1 == cap_stp[i].valid) {
			time_delta = (int)((cap_stp[i].timestamp - cap_stp[CMR_STEP_TAKE_PIC].timestamp)/1000000);
			ALOGE("%20s, %10d",
				cap_stp[i].step_name,
				time_delta);
		}
	}
	ALOGE("*********************Take picture statistic********End*******************");
}

void camera_set_stop_preview_mode(uint32_t stop_mode)
{
	g_cxt->stop_preview_mode = stop_mode;
	CMR_LOGI("stop preview mode is %d.",stop_mode);
}

int camera_is_sensor_support_zsl(void)
{
	int ret = 1;
	SENSOR_EXP_INFO_T *sensor_info_ptr = NULL;


	sensor_info_ptr = g_cxt->sn_cxt.sensor_info;

	if (SENSOR_IMAGE_FORMAT_JPEG == sensor_info_ptr->sensor_image_type) {
		ret = 0;
	}
	CMR_LOGV("ret=%d",ret);

	return ret;
}

static int camera_is_jpeg_encode_direct_process(void)
{
	int ret = 0;


	if ((IMG_ROT_0 != g_cxt->cap_rot)
		|| (g_cxt->is_cfg_rot_cap && (IMG_ROT_0 != g_cxt->cfg_cap_rot))) {
		/*ratation*/
		ret = 0;
	} else {
		if ((!NO_SCALING)
			|| (g_cxt->is_cfg_rot_cap && (IMG_ROT_0 == g_cxt->cfg_cap_rot))) {
			/*scaling*/
			ret = 0;
		} else {
			ret = 1;
		}
	}

	CMR_LOGV("ret=%d",ret);

	return ret;
}

int camera_isp_wb_trim(struct img_frm_cap *frm_cfg)
{
	int	ret = CAMERA_SUCCESS;
	struct isp_trim_size wb_trim;


	if (frm_cfg) {
		wb_trim.x = frm_cfg->src_img_rect.start_x;
		wb_trim.y = frm_cfg->src_img_rect.start_y;
		wb_trim.w = frm_cfg->src_img_rect.width;
		wb_trim.h = frm_cfg->src_img_rect.height;
		ret = isp_ioctl(ISP_CTRL_WB_TRIM,(void*)&wb_trim);
		if (CAMERA_SUCCESS != ret) {
			CMR_LOGE("set wb trim information error.");
		}
	}

	return ret;
}

int camera_isp_ae_info(SENSOR_AE_INFO_T *sensor_aec_info)
{
	int	ret = CAMERA_SUCCESS;


	if (sensor_aec_info) {
		CMR_LOGE("%d,%d,%d,%d",sensor_aec_info->min_frate,sensor_aec_info->max_frate,
				 sensor_aec_info->line_time,sensor_aec_info->gain);
		ret = isp_ioctl(ISP_CTRL_AE_INFO,(void*)sensor_aec_info);
		if (CAMERA_SUCCESS != ret) {
			CMR_LOGE("set ae information error.");
		}
	}

	return ret;
}

int camera_isp_start(uint32_t work_mode,uint32_t need_binning,SENSOR_MODE_INFO_T *sensor_mode)
{
	int	ret = CAMERA_SUCCESS;
	struct isp_video_start   isp_param;


	if (CMR_PREVIEW == work_mode) {
		if (sensor_mode) {
			isp_param.size.w = sensor_mode->width;
			if (need_binning) {
				isp_param.size.w = (isp_param.size.w >> 1);
			}
			isp_param.size.h = sensor_mode->height;
			isp_param.format = ISP_DATA_NORMAL_RAW10;
			isp_param.mode =  ISP_VIDEO_MODE_CONTINUE;
		}
	} else {
		if (sensor_mode) {
			isp_param.size.w = sensor_mode->width;
			isp_param.size.h = sensor_mode->height;
			isp_param.format = ISP_DATA_NORMAL_RAW10;
			isp_param.mode = ISP_VIDEO_MODE_SINGLE;
		}
	}

	CMR_LOGV("isp w h, %d %d", isp_param.size.w, isp_param.size.h);

	ret = isp_video_start(&isp_param);
	if (CAMERA_SUCCESS == ret) {
		g_cxt->isp_cxt.isp_state = ISP_COWORK;
	}



	return ret;
}

int camera_isp_awb_bypass(enum isp_alg_mode awb_mode)
{
	int ret = CAMERA_SUCCESS;
	struct isp_alg flash_param;


	flash_param.mode = awb_mode;
	ret = isp_ioctl(ISP_CTRL_ALG, (void*)&flash_param);
	if (CAMERA_SUCCESS != ret) {
		CMR_LOGE("ISP_CTRL_ALG error.");
	}

	return ret;
}

int camera_isp_ae_bypass(enum isp_alg_mode ae_mode)
{
	int ret = CAMERA_SUCCESS;
	struct isp_alg flash_param;


	flash_param.mode = ae_mode;
	flash_param.flash_eb = 0x01;
	ret = isp_ioctl(ISP_CTRL_ALG, (void*)&flash_param);
	if (CAMERA_SUCCESS != ret) {
		CMR_LOGE("ISP_AE_BYPASS error.");
	}

	return ret;
}

int camera_isp_flash_ratio(SENSOR_FLASH_LEVEL_T *flash_level)
{
	int ret = CAMERA_SUCCESS;
	struct isp_alg flash_param;


	flash_param.mode = ISP_ALG_FAST;
	flash_param.flash_eb = 0x01;
	/*flash_param.flash_ratio=flash_level.high_light*256/flash_level.low_light;*/
	/*because hardware issue high equal to low, so use hight div high */
	flash_param.flash_ratio = flash_level->high_light*256/flash_level->high_light;
	ret = isp_ioctl(ISP_CTRL_ALG, (void*)&flash_param);
	if (CAMERA_SUCCESS != ret) {
		CMR_LOGE("ISP_CTRL_FLASH_EG error.");
	}

	return ret;
}


