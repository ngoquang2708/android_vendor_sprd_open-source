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

#include <sys/types.h>
#include "isp_log.h"
#include "isp_ctrl.h"
#include "isp_app.h"
#include "isp_app_msg.h"

/**---------------------------------------------------------------------------*
**				Micro Define					*
**----------------------------------------------------------------------------*/
#define ISP_APP_EVT_START                (1 << 0)
#define ISP_APP_EVT_STOP                  (1 << 1)
#define ISP_APP_EVT_INIT                    (1 << 2)
#define ISP_APP_EVT_DEINIT                (1 << 3)
#define ISP_APP_EVT_CONTINUE           (1 << 4)
#define ISP_APP_EVT_CONTINUE_STOP (1 << 5)
#define ISP_APP_EVT_SIGNAL               (1 << 6)
#define ISP_APP_EVT_SIGNAL_NEXT     (1 << 7)
#define ISP_APP_EVT_IOCTRL              (1 << 8)
#define ISP_APP_EVT_SOF                   (1 << 9)
#define ISP_APP_EVT_CTRL_CALLBAC   (1 << 10)

#define ISP_APP_EVT_MASK                (uint32_t)(ISP_APP_EVT_START | ISP_APP_EVT_STOP | ISP_APP_EVT_INIT \
					| ISP_APP_EVT_DEINIT | ISP_APP_EVT_CONTINUE | ISP_APP_EVT_CONTINUE_STOP \
					| ISP_APP_EVT_SIGNAL | ISP_APP_EVT_SIGNAL_NEXT | ISP_APP_EVT_IOCTRL \
					| ISP_APP_EVT_SOF | ISP_APP_EVT_CTRL_CALLBAC)

#define ISP_APP_THREAD_QUEUE_NUM 50

#define ISP_APP_TRAC(_x_) ISP_LOG _x_
#define ISP_APP_RETURN_IF_FAIL(exp,warning) do{if(exp) {ISP_APP_TRAC(warning); return exp;}}while(0)
#define ISP_APP_TRACE_IF_FAIL(exp,warning) do{if(exp) {ISP_APP_TRAC(warning);}}while(0)

#define ISP_APP_EB 0x01
#define ISP_APP_UEB 0x00

#define ISP_APP_ZERO 0x00
#define ISP_APP_ONE 0x01
#define ISP_APP_TWO 0x02

#define ISP_APP_INVALID 0xffffffff

/**---------------------------------------------------------------------------*
**				Data Structures 					*
**---------------------------------------------------------------------------*/
enum isp_app_return {
	ISP_APP_SUCCESS=0x00,
	ISP_APP_PARAM_NULL,
	ISP_APP_PARAM_ERROR,
	ISP_APP_CALLBACK_NULL,
	ISP_APP_ALLOC_ERROR,
	ISP_APP_NO_READY,
	ISP_APP_ERROR,
	ISP_APP_RETURN_MAX=0xffffffff
};

enum isp_app_status{
	ISP_APP_CLOSE=0x00,
	ISP_APP_IDLE,
	ISP_APP_RUN,
	ISP_APP_STATE_MAX
};

struct isp_app_context{
	uint32_t isp_status;
	uint32_t isp_ctrl_sync;
	pthread_t app_thr;
	uint32_t app_queue;
	uint32_t app_status;
	pthread_mutex_t app_mutex;
	pthread_mutex_t cond_mutex;

	pthread_cond_t init_cond;
	pthread_cond_t deinit_cond;
	pthread_cond_t continue_cond;
	pthread_cond_t continue_stop_cond;
	pthread_cond_t signal_cond;
	pthread_cond_t ioctrl_cond;
	pthread_cond_t thread_common_cond;

	uint32_t ae_stab;
	uint32_t af_flag;
	uint32_t lum_measure_flag;
	struct isp_af_win af_info;
	enum isp_ae_weight lum_measure_mode;

	proc_callback ctrl_callback;
};

struct isp_app_respond
{
	uint32_t rtn;
};

/**---------------------------------------------------------------------------*
**				extend Variables and function			*
**---------------------------------------------------------------------------*/

/**---------------------------------------------------------------------------*
**				Local Variables 					*
**---------------------------------------------------------------------------*/

static struct isp_app_context* s_isp_app_context_ptr=NULL;
/**---------------------------------------------------------------------------*
**					Constant Variables				*
**---------------------------------------------------------------------------*/
static int _isp_app_msg_post(struct isp_app_msg *message);

/**---------------------------------------------------------------------------*
**					Local Function Prototypes			*
**---------------------------------------------------------------------------*/

/* ispAppGetContext --
*@
*@
*@ return:
*/
static struct isp_app_context* ispAppGetContext(void)
{
	return s_isp_app_context_ptr;
}

/* _isp_AppInitContext --
*@
*@
*@ return:
*/
static uint32_t _isp_AppInitContext(void)
{
	int32_t rtn = ISP_APP_SUCCESS;

	if (NULL == s_isp_app_context_ptr) {
		s_isp_app_context_ptr = (struct isp_app_context*)malloc(sizeof(struct isp_app_context));
		if (NULL == s_isp_app_context_ptr) {
			ISP_LOG("alloc context buf error");
			rtn=ISP_APP_ALLOC_ERROR;
			return rtn;
		}
	}

	if (NULL != s_isp_app_context_ptr) {
		memset((void*)s_isp_app_context_ptr, 0x00, sizeof(struct isp_app_context));
	}

	return rtn;
}

/* _isp_AppDeinitContext --
*@
*@
*@ return:
*/
static int32_t _isp_AppDeinitContext(void)
{
	int32_t rtn = ISP_APP_SUCCESS;

	if (NULL != s_isp_app_context_ptr) {
		free(s_isp_app_context_ptr);
		s_isp_app_context_ptr = NULL;
	}

	return rtn;
}

/* _isp_AppAfDenoiseRecover --
*@
*@
*@ return:
*/
uint32_t _isp_AppAfDenoiseRecover(void)
{
	int32_t rtn=ISP_APP_SUCCESS;
	uint32_t denoise_level=0x00;

	rtn = isp_ctrl_capability(ISP_DENOISE_LEVEL, (void*)&denoise_level);

	if (ISP_APP_ZERO != (0x80000000&denoise_level)) {
		denoise_level&=0xff;
		rtn = isp_ctrl_ioctl(ISP_CTRL_AF_DENOISE, (void*)&denoise_level);
	} else {
		rtn = isp_ctrl_ioctl(ISP_CTRL_DENOISE, (void*)&denoise_level);
	}

	return rtn;
}

/* _isp_AppSetLumMeasureCond --
*@
*@
*@ return:
*/
uint32_t _isp_AppSetLumMeasureCond(void)
{
	int32_t rtn = ISP_APP_SUCCESS;
	struct isp_app_context* isp_context_ptr = ispAppGetContext();
	uint32_t ae_change = ISP_APP_EB;

	if (ISP_APP_EB == isp_context_ptr->lum_measure_flag) {
		rtn = isp_ctrl_ioctl(ISP_CTRL_GET_AE_CHG, (void*)&ae_change);
		isp_context_ptr->lum_measure_flag = ISP_APP_UEB;
	}

	return rtn;
}

/* _isp_AppLumMeasureRecover --
*@
*@
*@ return:
*/
uint32_t _isp_AppLumMeasureRecover(void)
{
	int32_t rtn = ISP_APP_SUCCESS;
	struct isp_app_context* isp_context_ptr = ispAppGetContext();

	rtn = isp_ctrl_ioctl(ISP_CTRL_AE_MEASURE_LUM, (void*)&isp_context_ptr->lum_measure_mode);

	return rtn;
}

/* _isp_AppCtrlCallback --
*@
*@
*@ return:
*/
int32_t _isp_AppCtrlCallback(int32_t mode, void* param_ptr)
{
	int32_t rtn=ISP_APP_SUCCESS;
	struct isp_app_context* isp_context_ptr=ispAppGetContext();
	ISP_APP_MSG_INIT(isp_ctrl_msg);

	isp_context_ptr->ctrl_callback(mode, param_ptr);

	if (ISP_APP_ZERO != (ISP_CALLBACK_EVT&mode)) {
		if ((ISP_AF_NOTICE_CALLBACK == (ISP_AF_NOTICE_CALLBACK&mode))
			|| (ISP_AE_CHG_CALLBACK == (ISP_AE_CHG_CALLBACK&mode))) {
			isp_ctrl_msg.msg_type = ISP_APP_EVT_CTRL_CALLBAC;
			isp_ctrl_msg.sub_msg_type = mode;
			isp_ctrl_msg.alloc_flag = 0x00;
			isp_ctrl_msg.respond = 0x00;
			rtn = _isp_app_msg_post(&isp_ctrl_msg);
			ISP_APP_RETURN_IF_FAIL(rtn, ("ctrl callback send msg to app thread error"));
		}
	}

	return rtn;
}

/* _isp_AppCtrlCallbackHandler --
*@
*@
*@ return:
*/
uint32_t _isp_AppCtrlCallbackHandler(int32_t mode, void* param_ptr)
{
	int32_t rtn=ISP_APP_SUCCESS;

	if (ISP_APP_ZERO != (ISP_CALLBACK_EVT&mode)) {
		if (ISP_AF_NOTICE_CALLBACK == (ISP_AF_NOTICE_CALLBACK&mode)) {
			rtn = _isp_AppAfDenoiseRecover();
			rtn = _isp_AppSetLumMeasureCond();
		}
		if (ISP_AE_CHG_CALLBACK == (ISP_AE_CHG_CALLBACK&mode)) {
			rtn = _isp_AppLumMeasureRecover();
		}
	}

	return rtn;
}

/* _isp_AppStopVideoHandler --
*@
*@
*@ return:
*/
uint32_t _isp_AppStopVideoHandler(void)
{
	int32_t rtn=ISP_APP_SUCCESS;
	struct isp_app_context* isp_context_ptr=ispAppGetContext();
	struct isp_af_notice af_notice;

	if (ISP_APP_EB == isp_context_ptr->af_flag) {
		ISP_LOG("App Stop ISP_AF_NOTICE_CALLBACK");
		af_notice.valid_win=0x00;
		isp_context_ptr->ctrl_callback(ISP_CALLBACK_EVT|ISP_AF_NOTICE_CALLBACK, (void*)&af_notice);
		isp_context_ptr->af_flag = ISP_APP_UEB;
	}

	return rtn;
}

/* _isp_AppCallBack --
*@
*@
*@ return:
*/
static int32_t _isp_AppCallBack(int32_t mode, void* param_ptr)
{
	int32_t rtn = ISP_APP_SUCCESS;
	struct isp_app_context* isp_context_ptr = ispAppGetContext();
	ISP_APP_MSG_INIT(isp_ctrl_msg);
	uint32_t cmd = mode&0xffff;

	switch(cmd)
	{
		case ISP_AE_STAB_CALLBACK:
			ISP_LOG("APP ISP_AE_STAB_CALLBACK");
			isp_context_ptr->ae_stab = ISP_APP_EB;
			break;

		case ISP_SOF_CALLBACK:
			if ((ISP_APP_EB == isp_context_ptr->af_flag)
				&&(ISP_APP_EB == isp_context_ptr->ae_stab)) {
				ISP_LOG("APP ISP_SOF_CALLBACK");
				isp_ctrl_msg.msg_type = ISP_APP_EVT_SOF;
				isp_ctrl_msg.alloc_flag = 0x00;
				isp_ctrl_msg.respond = 0x00;
				rtn = _isp_app_msg_post(&isp_ctrl_msg);
				ISP_APP_RETURN_IF_FAIL(rtn, ("send msg to app thread error"));
			}
			break;

		default :
			break;
	}

	return rtn;
}

/* _isp_AppSofHandler --
*@
*@
*@ return:
*/
static int32_t _isp_AppSofHandler(void)
{
	int32_t rtn = ISP_APP_SUCCESS;
	struct isp_app_context* isp_context_ptr = ispAppGetContext();

	if (ISP_APP_EB == isp_context_ptr->af_flag) {
		struct isp_af_win af_param;
		memcpy((void*)&af_param, (void*)&isp_context_ptr->af_info, sizeof(struct isp_af_win));
		rtn = isp_ctrl_ioctl(ISP_CTRL_AF, (void*)&af_param);
		isp_context_ptr->af_flag = ISP_APP_UEB;
	}

	return rtn;
}

/* _isp_AppAfIoCtrlHandler --
*@
*@
*@ return:
*/
static int32_t _isp_AppAfIoCtrlHandler(void* param_ptr)
{
	int32_t rtn = ISP_APP_SUCCESS;
	struct isp_app_context* isp_context_ptr = ispAppGetContext();
	struct isp_af_win* af_param_ptr=(struct isp_af_win*)param_ptr;
	uint32_t ae_stab = ISP_APP_EB;

	if (ISP_APP_EB == af_param_ptr->ae_touch) {
		rtn = isp_ctrl_ioctl(ISP_CTRL_AE_TOUCH, (void*)&af_param_ptr->ae_touch_rect);
		if (ISP_APP_SUCCESS == rtn) {
			isp_context_ptr->lum_measure_flag = ISP_APP_EB;
			isp_context_ptr->ae_stab=ISP_APP_UEB;
			rtn = isp_ctrl_ioctl(ISP_CTRL_GET_AE_STAB, (void*)&ae_stab);
		} else {
			isp_context_ptr->lum_measure_flag = ISP_APP_UEB;
			rtn = ISP_APP_SUCCESS;
		}
	}

	if (ISP_APP_EB != isp_context_ptr->ae_stab) {
		memcpy((void*)&isp_context_ptr->af_info, af_param_ptr, sizeof(struct isp_af_win));
		isp_context_ptr->af_flag = ISP_APP_EB;
		rtn = ISP_APP_ERROR;
	}

	return rtn;
}


/* _isp_AppIoCtrlHandler --
*@
*@
*@ return:
*/
static int32_t _isp_AppIoCtrlHandler(enum isp_ctrl_cmd io_cmd, void* param_ptr)
{
	int32_t rtn = ISP_APP_SUCCESS;
	struct isp_app_context* isp_context_ptr = ispAppGetContext();
	enum isp_ctrl_cmd cmd = io_cmd&0x3fffffff;

	switch (cmd)
	{
		case ISP_CTRL_AF:
		{
			uint32_t denoise_level=0xfe;
			rtn = isp_ctrl_ioctl(ISP_CTRL_AF_DENOISE, (void*)&denoise_level);
			rtn = _isp_AppAfIoCtrlHandler(param_ptr);
			break ;
		}
		case ISP_CTRL_AE_MEASURE_LUM:
		{
			isp_context_ptr->lum_measure_mode=*(uint32_t*)param_ptr;
			break;
		}
		default :
			break ;
	}

	return rtn;
}

/* _isp_AppSetStatus --
*@
*@
*@ return:
*/
static int32_t _isp_AppSetStatus(uint32_t status)
{
	int32_t rtn = ISP_APP_SUCCESS;
	struct isp_app_context* isp_context_ptr = ispAppGetContext();

	isp_context_ptr->isp_status = status;

	return rtn;
}

/* _isp_AppGetStatus --
*@
*@
*@ return:
*/
static int32_t _isp_AppGetStatus(void)
{
	int32_t rtn = ISP_APP_SUCCESS;
	struct isp_app_context* isp_context_ptr = ispAppGetContext();

	return isp_context_ptr->isp_status;
}

/* _isp_set_app_init_param --
*@
*@
*@ return:
*/
static int32_t _isp_set_app_init_param(struct isp_init_param* ptr)
{
	int32_t rtn = ISP_APP_SUCCESS;
	struct isp_app_context* isp_context_ptr = ispAppGetContext();

	ptr->self_callback = _isp_AppCallBack;
	isp_context_ptr->ctrl_callback = ptr->ctrl_callback;
	ptr->ctrl_callback = _isp_AppCtrlCallback;

	return rtn;
}

/* _isp_app_init --
*@
*@
*@ return:
*/
static int _isp_app_init(struct isp_init_param* ptr)
{
	int rtn = ISP_APP_SUCCESS;

	rtn = _isp_set_app_init_param(ptr);
	ISP_APP_RETURN_IF_FAIL(rtn, ("set app init param error"));

	rtn = isp_ctrl_init(ptr);
	ISP_APP_RETURN_IF_FAIL(rtn, ("ctr init error"));

	return rtn;
}

/* _isp_app_deinit --
*@
*@
*@ return:
*/
static int _isp_app_deinit(void)
{
	int rtn = ISP_APP_SUCCESS;

	rtn = isp_ctrl_deinit();
	ISP_APP_RETURN_IF_FAIL(rtn, ("ctr deinit error"));

	return rtn;
}


/* _isp_set_app_video_param --
*@
*@
*@ return:
*/
static int32_t _isp_set_app_video_param(struct isp_video_start* ptr)
{
	int32_t rtn = ISP_APP_SUCCESS;
	struct isp_app_context* isp_context_ptr = ispAppGetContext();

	isp_context_ptr->ae_stab = ISP_APP_INVALID;
	isp_context_ptr->af_flag = ISP_APP_INVALID;

	return rtn;
}

/* _isp_app_video_start --
*@
*@
*@ return:
*/
static int _isp_app_video_start(struct isp_video_start* ptr)
{
	int rtn = ISP_APP_SUCCESS;

	rtn = _isp_set_app_video_param(ptr);
	ISP_APP_RETURN_IF_FAIL(rtn, ("set app video param error"));

	rtn = isp_ctrl_video_start(ptr);
	ISP_APP_RETURN_IF_FAIL(rtn, ("app video start error"));

	return rtn;
}

/* _isp_msg_queue_create --
*@
*@
*@ return:
*/
static int _isp_msg_queue_create(uint32_t count, uint32_t *queue_handle)
{
	int rtn = ISP_APP_SUCCESS;

	rtn = isp_app_msg_queue_create( count, queue_handle);

	return rtn;
}

/* _isp_msg_queue_destroy --
*@
*@
*@ return:
*/
static int _isp_msg_queue_destroy(uint32_t queue_handle)
{
	int rtn = ISP_APP_SUCCESS;

	rtn = isp_app_msg_queue_destroy(queue_handle);

	return rtn;
}

/* _isp_cond_wait --
*@
*@
*@ return:
*/
static int _isp_cond_wait(pthread_cond_t* cond_ptr, pthread_mutex_t* mutex_ptr)
{
	int rtn = ISP_APP_SUCCESS;

	rtn = pthread_mutex_lock(mutex_ptr);
	ISP_APP_RETURN_IF_FAIL(rtn, ("lock cond mutex %d error", rtn));
	rtn = pthread_cond_wait(cond_ptr, mutex_ptr);
	ISP_APP_RETURN_IF_FAIL(rtn, ("cond wait %d error", rtn));
	rtn = pthread_mutex_unlock(mutex_ptr);
	ISP_APP_RETURN_IF_FAIL(rtn, ("unlock cond mutex %d error", rtn));

	return rtn;
}

/* _isp_cond_signal --
*@
*@
*@ return:
*/
static int _isp_cond_signal(pthread_cond_t* cond_ptr, pthread_mutex_t* mutex_ptr)
{
	int rtn = ISP_APP_SUCCESS;
	rtn = pthread_mutex_lock(mutex_ptr);
	ISP_APP_RETURN_IF_FAIL(rtn, ("lock cond mutex %d error", rtn));
	rtn=pthread_cond_signal(cond_ptr);
	ISP_APP_RETURN_IF_FAIL(rtn, ("cond signal %d error", rtn));
	rtn = pthread_mutex_unlock(mutex_ptr);
	ISP_APP_RETURN_IF_FAIL(rtn, ("unlock cond mutex %d error", rtn));
	return rtn;
}

/* _isp_app_msg_get --
*@
*@
*@ return:
*/
static int _isp_app_msg_get(struct isp_app_msg *message)
{
	int rtn = ISP_APP_SUCCESS;
	struct isp_app_context* isp_context_ptr = ispAppGetContext();

	rtn = isp_app_msg_get( isp_context_ptr->app_queue, message);

	return rtn;
}

/* _isp_app_msg_post --
*@
*@
*@ return:
*/
static int _isp_app_msg_post(struct isp_app_msg *message)
{
	int rtn = ISP_APP_SUCCESS;
	struct isp_app_context* isp_context_ptr = ispAppGetContext();

	rtn = isp_app_msg_post( isp_context_ptr->app_queue, message);

	return rtn;
}

/* _isp_app_routine --
*@
*@
*@ return:
*/
static void *_isp_app_routine(void *client_data)
{
	int rtn = ISP_APP_SUCCESS;
	struct isp_app_respond* map_res_ptr;
	struct isp_app_respond* res_ptr;
	struct isp_app_context* isp_context_ptr = ispAppGetContext();
	ISP_APP_MSG_INIT(isp_ctrl_msg);
	ISP_APP_MSG_INIT(isp_ctrl_self_msg);
	uint32_t evt = 0X00;
	uint32_t sub_type = 0X00;
	void* param_ptr = NULL;

	ISP_LOG("enter isp ctrl routine.");

	while (1) {
		rtn = _isp_app_msg_get(&isp_ctrl_msg);
		if (rtn) {
			ISP_LOG("msg queue error");
			break;
		}

		_isp_AppSetStatus(ISP_APP_RUN);
		evt = (uint32_t)(isp_ctrl_msg.msg_type & ISP_APP_EVT_MASK);
		sub_type = isp_ctrl_msg.sub_msg_type;
		param_ptr = (void*)isp_ctrl_msg.data;
		map_res_ptr = (void*)isp_ctrl_msg.respond;

		switch (evt) {
			case ISP_APP_EVT_START:
				//ISP_LOG("ISP_APP_EVT_START");
				_isp_AppSetStatus(ISP_APP_IDLE);
				pthread_mutex_lock(&isp_context_ptr->cond_mutex);
				rtn = pthread_cond_signal(&isp_context_ptr->thread_common_cond);
				pthread_mutex_unlock(&isp_context_ptr->cond_mutex);
				break;

			case ISP_APP_EVT_STOP:
				//ISP_LOG("ISP_APP_EVT_STOP");
				_isp_AppSetStatus(ISP_APP_CLOSE);
				pthread_mutex_lock(&isp_context_ptr->cond_mutex);
				rtn = pthread_cond_signal(&isp_context_ptr->thread_common_cond);
				pthread_mutex_unlock(&isp_context_ptr->cond_mutex);
				break;

			case ISP_APP_EVT_INIT:
				//ISP_LOG("ISP_APP_EVT_INIT");
				rtn = _isp_app_init((struct isp_init_param*)param_ptr);
				pthread_mutex_lock(&isp_context_ptr->cond_mutex);
				rtn = pthread_cond_signal(&isp_context_ptr->init_cond);
				pthread_mutex_unlock(&isp_context_ptr->cond_mutex);
				break;

			case ISP_APP_EVT_DEINIT:
				//ISP_LOG("ISP_APP_EVT_DEINIT");
				rtn = _isp_app_deinit();
				pthread_mutex_lock(&isp_context_ptr->cond_mutex);
				rtn = pthread_cond_signal(&isp_context_ptr->deinit_cond);
				pthread_mutex_unlock(&isp_context_ptr->cond_mutex);
				break;

			case ISP_APP_EVT_CONTINUE:
				//ISP_LOG("ISP_APP_EVT_CONTINUE");
				rtn=_isp_app_video_start((struct isp_video_start*)param_ptr);
				//rtn = isp_ctrl_video_start((struct isp_video_start*)param_ptr);
				pthread_mutex_lock(&isp_context_ptr->cond_mutex);
				rtn = pthread_cond_signal(&isp_context_ptr->continue_cond);
				pthread_mutex_unlock(&isp_context_ptr->cond_mutex);
				break;

			case ISP_APP_EVT_CONTINUE_STOP:
				//ISP_LOG("ISP_APP_EVT_CONTINUE_STOP");
				rtn = isp_ctrl_video_stop();
				rtn = _isp_AppStopVideoHandler();
				pthread_mutex_lock(&isp_context_ptr->cond_mutex);
				rtn = pthread_cond_signal(&isp_context_ptr->continue_stop_cond);
				pthread_mutex_unlock(&isp_context_ptr->cond_mutex);
				break;

			case ISP_APP_EVT_SIGNAL:
				//ISP_LOG("ISP_APP_EVT_SIGNAL");
				rtn = isp_ctrl_proc_start((struct ips_in_param*)param_ptr, NULL);
				break;

			case ISP_APP_EVT_SIGNAL_NEXT:
				//ISP_LOG("ISP_APP_EVT_SIGNAL_NEXT");
				rtn=isp_ctrl_proc_next((struct ipn_in_param*)param_ptr, NULL);
				break;

			case ISP_APP_EVT_IOCTRL:
				//ISP_LOG("--app_isp_ioctl--cmd:0x%x", sub_type);
				rtn = _isp_AppIoCtrlHandler(sub_type, param_ptr);
				if (ISP_APP_SUCCESS == rtn) {
					rtn = isp_ctrl_ioctl(sub_type, param_ptr);
				}
				pthread_mutex_lock(&isp_context_ptr->cond_mutex);
				rtn = pthread_cond_signal(&isp_context_ptr->ioctrl_cond);
				pthread_mutex_unlock(&isp_context_ptr->cond_mutex);
				break;

			case ISP_APP_EVT_SOF:
				rtn = _isp_AppSofHandler();
				break;

			case ISP_APP_EVT_CTRL_CALLBAC:
				rtn = _isp_AppCtrlCallbackHandler(sub_type, param_ptr);
				break;

			default:
				ISP_LOG("--default--cmd:0x%x", sub_type);
				break;
		}

		if (0x01==isp_ctrl_msg.alloc_flag) {
			free(isp_ctrl_msg.data);
		}
		if(ISP_APP_CLOSE == _isp_AppGetStatus()) {
			break;
		}
		_isp_AppSetStatus(ISP_APP_IDLE);

	}

	ISP_LOG("exit isp app routine.");

	return NULL;

}

/* _isp_create_app_thread --
*@
*@
*@ return:
*/
static int _isp_create_app_thread(void)
{
	int rtn = ISP_APP_SUCCESS;
	struct isp_app_context* isp_context_ptr = ispAppGetContext();
	pthread_attr_t attr;
	ISP_APP_MSG_INIT(isp_main_msg);

	rtn = _isp_msg_queue_create(ISP_APP_THREAD_QUEUE_NUM, &isp_context_ptr->app_queue);
	ISP_APP_RETURN_IF_FAIL(rtn, ("careate ctrl queue error"));

	pthread_attr_init (&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	rtn = pthread_create(&isp_context_ptr->app_thr, &attr, _isp_app_routine, NULL);
	ISP_APP_RETURN_IF_FAIL(rtn, ("careate ctrl thread error"));
	pthread_attr_destroy(&attr);

	isp_main_msg.msg_type = ISP_APP_EVT_START;

	pthread_mutex_lock(&isp_context_ptr->cond_mutex);
	rtn = _isp_app_msg_post(&isp_main_msg);
	ISP_APP_RETURN_IF_FAIL(rtn, ("send msg to ctrl thread error"));

	rtn = pthread_cond_wait(&isp_context_ptr->thread_common_cond, &isp_context_ptr->cond_mutex);
	pthread_mutex_unlock(&isp_context_ptr->cond_mutex);

	return rtn;
}

/* _isp_destory_app_thread --
*@
*@
*@ return:
*/
static int _isp_destory_app_thread(void)
{
	int rtn = ISP_APP_SUCCESS;
	struct isp_app_context* isp_context_ptr = ispAppGetContext();
	ISP_APP_MSG_INIT(isp_main_msg);

	isp_main_msg.msg_type = ISP_APP_EVT_STOP;

	pthread_mutex_lock(&isp_context_ptr->cond_mutex);
	rtn = _isp_app_msg_post(&isp_main_msg);

	rtn = pthread_cond_wait(&isp_context_ptr->thread_common_cond,&isp_context_ptr->cond_mutex);
	pthread_mutex_unlock(&isp_context_ptr->cond_mutex);
	rtn = _isp_msg_queue_destroy(isp_context_ptr->app_queue);
	ISP_APP_RETURN_IF_FAIL(rtn, ("destroy ctrl queue error"));

	return rtn;
}

/* _isp_app_create_Resource --
*@
*@
*@ return:
*/
int _isp_app_create_Resource(void)
{
	int rtn=ISP_APP_SUCCESS;
	struct isp_app_context* isp_context_ptr = ispAppGetContext();

	pthread_mutex_init (&isp_context_ptr->app_mutex, NULL);
	pthread_mutex_init (&isp_context_ptr->cond_mutex, NULL);

	pthread_cond_init(&isp_context_ptr->init_cond, NULL);
	pthread_cond_init(&isp_context_ptr->deinit_cond, NULL);
	pthread_cond_init(&isp_context_ptr->continue_cond, NULL);
	pthread_cond_init(&isp_context_ptr->continue_stop_cond, NULL);
	pthread_cond_init(&isp_context_ptr->signal_cond, NULL);
	pthread_cond_init(&isp_context_ptr->ioctrl_cond, NULL);
	pthread_cond_init(&isp_context_ptr->thread_common_cond, NULL);

	_isp_create_app_thread();

	return rtn;
}

/* _isp_DeInitResource --
*@
*@
*@ return:
*/
int _isp_app_release_resource(void)
{
	int rtn = ISP_APP_SUCCESS;
	struct isp_app_context* isp_context_ptr = ispAppGetContext();

	_isp_destory_app_thread();
	
	pthread_mutex_destroy(&isp_context_ptr->app_mutex);
	pthread_mutex_destroy(&isp_context_ptr->cond_mutex);

	pthread_cond_destroy(&isp_context_ptr->init_cond);
	pthread_cond_destroy(&isp_context_ptr->deinit_cond);
	pthread_cond_destroy(&isp_context_ptr->continue_cond);
	pthread_cond_destroy(&isp_context_ptr->continue_stop_cond);
	pthread_cond_destroy(&isp_context_ptr->signal_cond);
	pthread_cond_destroy(&isp_context_ptr->ioctrl_cond);
	pthread_cond_destroy(&isp_context_ptr->thread_common_cond);

	return rtn;
}

// public

/* isp_init --
*@
*@
*@ return:
*/
int isp_init(struct isp_init_param* ptr)
{
	int rtn = ISP_APP_SUCCESS;
	struct isp_app_respond respond;
	struct isp_app_context* isp_context_ptr = NULL;
	ISP_APP_MSG_INIT(isp_main_msg);

	ISP_LOG("---isp_app_init-- start");

	rtn = _isp_AppInitContext();
	ISP_APP_RETURN_IF_FAIL(rtn, ("init isp app context error"));

	isp_context_ptr = ispAppGetContext();

	rtn = _isp_app_create_Resource();
	ISP_APP_RETURN_IF_FAIL(rtn, ("create app resource error"));

	respond.rtn = ISP_APP_SUCCESS;
	isp_main_msg.data = malloc(sizeof(struct isp_init_param));
	memcpy(isp_main_msg.data, ptr, sizeof(struct isp_init_param));
	isp_main_msg.alloc_flag = 1;
	isp_main_msg.msg_type = ISP_APP_EVT_INIT;
	isp_main_msg.respond=(void*)&respond;

	pthread_mutex_lock(&isp_context_ptr->cond_mutex);

	rtn = _isp_app_msg_post(&isp_main_msg);
	ISP_APP_RETURN_IF_FAIL(rtn, ("send msg to app thread error"));

	rtn = pthread_cond_wait(&isp_context_ptr->init_cond, &isp_context_ptr->cond_mutex);
	pthread_mutex_unlock(&isp_context_ptr->cond_mutex);
	ISP_APP_RETURN_IF_FAIL(rtn, ("pthread_cond_wait error"));

	ISP_LOG("---isp_app_init-- end");

	return rtn;
}

/* isp_deinit --
*@
*@
*@ return:
*/
int isp_deinit(void)
{
	int rtn = ISP_APP_SUCCESS;
	struct isp_app_respond respond;
	struct isp_app_context* isp_context_ptr = ispAppGetContext();
	ISP_APP_MSG_INIT(isp_main_msg);

	ISP_LOG("--isp_app_deinit--");

	// get mutex
	pthread_mutex_lock(&isp_context_ptr->app_mutex);

	respond.rtn = ISP_APP_SUCCESS;
	isp_main_msg.msg_type = ISP_APP_EVT_DEINIT;
	isp_main_msg.alloc_flag = 0x00;
	isp_main_msg.respond = (void*)&respond;

	// close hw isp
	pthread_mutex_lock(&isp_context_ptr->cond_mutex);
	rtn = _isp_app_msg_post(&isp_main_msg);
	ISP_APP_RETURN_IF_FAIL(rtn, ("send msg to ctrl thread error"));

	rtn = pthread_cond_wait(&isp_context_ptr->deinit_cond, &isp_context_ptr->cond_mutex);
	pthread_mutex_unlock(&isp_context_ptr->cond_mutex);
	ISP_APP_RETURN_IF_FAIL(rtn, ("pthread_cond_wait error"));

	rtn = pthread_mutex_unlock(&isp_context_ptr->app_mutex);
	ISP_APP_RETURN_IF_FAIL(rtn, ("pthread_mutex_unlock error"));

	rtn = _isp_app_release_resource();
	ISP_APP_RETURN_IF_FAIL(rtn, ("_isp_app_release_resource error"));

	ISP_LOG("--isp_app_deinit-- end");

	return rtn;
}

/* isp_capability --
*@
*@
*@ return:
*/
int isp_capability(enum isp_capbility_cmd cmd, void* param_ptr)
{
	int rtn = ISP_APP_SUCCESS;

	rtn = isp_ctrl_capability(cmd, param_ptr);

	return rtn;
}

/* isp_ioctl --
*@
*@
*@ return:
*/
int isp_ioctl(enum isp_ctrl_cmd cmd, void* param_ptr)
{
	int rtn = ISP_APP_SUCCESS;
	struct isp_app_respond respond;
	struct isp_app_context* isp_context_ptr = ispAppGetContext();
	ISP_APP_MSG_INIT(isp_main_msg);

	respond.rtn = ISP_APP_SUCCESS;
	isp_main_msg.msg_type = ISP_APP_EVT_IOCTRL;
	isp_main_msg.sub_msg_type = cmd;
	isp_main_msg.data = (void*)param_ptr;
	isp_main_msg.alloc_flag = 0x00;
	isp_main_msg.respond = (void*)&respond;

	pthread_mutex_lock(&isp_context_ptr->cond_mutex);
	rtn = _isp_app_msg_post(&isp_main_msg);
	ISP_APP_RETURN_IF_FAIL(rtn, ("send msg to ctrl thread error"));

	rtn = pthread_cond_wait(&isp_context_ptr->ioctrl_cond, &isp_context_ptr->cond_mutex);
	pthread_mutex_unlock(&isp_context_ptr->cond_mutex);
	ISP_APP_RETURN_IF_FAIL(rtn, ("pthread_cond_wait error"));

	rtn = respond.rtn;

	return rtn;
}

/* isp_video_start --
*@
*@
*@ return:
*/
int isp_video_start(struct isp_video_start* param_ptr)
{
	int rtn = ISP_APP_SUCCESS;
	struct isp_app_respond respond;
	struct isp_app_context* isp_context_ptr = ispAppGetContext();
	ISP_APP_MSG_INIT(isp_main_msg);

	ISP_LOG("--isp_app_video_start--");

	respond.rtn = ISP_APP_SUCCESS;
	isp_main_msg.data = malloc(sizeof(struct isp_video_start));
	memcpy(isp_main_msg.data, param_ptr, sizeof(struct isp_video_start));
	isp_main_msg.alloc_flag = 0x01;
	isp_main_msg.msg_type = ISP_APP_EVT_CONTINUE;
	isp_main_msg.sub_msg_type;
	isp_main_msg.respond = (void*)&respond;

	pthread_mutex_lock(&isp_context_ptr->cond_mutex);

	rtn = _isp_app_msg_post(&isp_main_msg);
	ISP_APP_RETURN_IF_FAIL(rtn, ("send msg to ctrl thread error"));

	rtn = pthread_cond_wait(&isp_context_ptr->continue_cond, &isp_context_ptr->cond_mutex);
	pthread_mutex_unlock(&isp_context_ptr->cond_mutex);
	ISP_APP_RETURN_IF_FAIL(rtn, ("pthread_cond_wait error"));

	ISP_LOG("--isp_app_video_start-- end");

	return rtn;
}

/* isp_video_start --
*@
*@
*@ return:
*/
int isp_video_stop(void)
{
	int rtn=ISP_APP_SUCCESS;
	struct isp_app_respond respond;
	struct isp_app_context* isp_context_ptr = ispAppGetContext();
	ISP_APP_MSG_INIT(isp_main_msg);

	ISP_LOG("--isp_app_video_stop--");

	respond.rtn = ISP_APP_SUCCESS;
	isp_main_msg.msg_type = ISP_APP_EVT_CONTINUE_STOP;
	isp_main_msg.sub_msg_type;
	isp_main_msg.data = NULL;
	isp_main_msg.alloc_flag = 0x00;
	isp_main_msg.respond = (void*)&respond;
	
	pthread_mutex_lock(&isp_context_ptr->cond_mutex);
	rtn = _isp_app_msg_post(&isp_main_msg);
	ISP_APP_RETURN_IF_FAIL(rtn, ("send msg to ctrl thread error"));

	rtn = pthread_cond_wait(&isp_context_ptr->continue_stop_cond, &isp_context_ptr->cond_mutex);
	pthread_mutex_unlock(&isp_context_ptr->cond_mutex);
	ISP_APP_RETURN_IF_FAIL(rtn, ("pthread_cond_wait error"));

	ISP_LOG("--isp_app_video_stop--end");

	return rtn;
}

/* isp_proc_start --
*@
*@
*@ return:
*/
int isp_proc_start(struct ips_in_param* in_param_ptr, struct ips_out_param* out_param_ptr)
{
	int rtn=ISP_APP_SUCCESS;
	struct isp_app_respond respond;
	ISP_APP_MSG_INIT(isp_main_msg);

	ISP_LOG("--isp_app_proc_start--");

	respond.rtn = ISP_APP_SUCCESS;
	isp_main_msg.msg_type = ISP_APP_EVT_SIGNAL;
	isp_main_msg.sub_msg_type;
	isp_main_msg.data = malloc(sizeof(struct ips_in_param));
	memcpy(isp_main_msg.data, in_param_ptr, sizeof(struct ips_in_param));
	isp_main_msg.alloc_flag = 0x01;
	isp_main_msg.respond = (void*)&respond;

	rtn = _isp_app_msg_post(&isp_main_msg);
	ISP_APP_RETURN_IF_FAIL(rtn, ("send msg to app thread error"));

	ISP_LOG("--isp_app_proc_start--end");

	return rtn;
}

/* isp_proc_next --
*@
*@
*@ return:
*/
int isp_proc_next(struct ipn_in_param* in_ptr, struct ips_out_param *out_ptr)
{
	int rtn = ISP_APP_SUCCESS;
	struct isp_app_respond respond;
	ISP_APP_MSG_INIT(isp_main_msg);

	ISP_LOG("--isp_app_proc_next--");

	respond.rtn = ISP_APP_SUCCESS;
	isp_main_msg.msg_type = ISP_APP_EVT_SIGNAL_NEXT;
	isp_main_msg.sub_msg_type;
	isp_main_msg.data = malloc(sizeof(struct ipn_in_param));
	memcpy(isp_main_msg.data, in_ptr, sizeof(struct ipn_in_param));
	isp_main_msg.alloc_flag = 0x01;
	isp_main_msg.respond = (void*)&respond;

	rtn = _isp_app_msg_post(&isp_main_msg);
	ISP_APP_RETURN_IF_FAIL(rtn, ("send msg to ctrl thread error"));

	ISP_LOG("--isp_app_proc_next--end");

	return rtn;
}

/**---------------------------------------------------------------------------*/

