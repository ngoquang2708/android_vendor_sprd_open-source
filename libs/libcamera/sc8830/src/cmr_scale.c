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

#define LOG_TAG "Cmr_scale"

#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include "cmr_cvt.h"
#include "cmr_oem.h"
#include <linux/types.h>
#include <asm/ioctl.h>
#include "sprd_scale_k.h"

#define SCALE_MSG_QUEUE_SIZE 20

enum scale_work_mode {
	SC_FRAME = SCALE_MODE_NORMAL,
	SC_SLICE_EXTERNAL = SCALE_MODE_SLICE,
	SC_SLICE_INTERNAL
};

struct scale_file {
	int fd;
	pthread_mutex_t cb_mutex;
	pthread_t scale_thread;
	sem_t thread_sync_sem;
	sem_t done_sem;
	cmr_evt_cb scale_evt_cb;
	uint32_t msg_que_handle;
};

struct scale_cfg_param_t{
	struct scale_frame_param_t frame_params;
	cmr_evt_cb scale_cb;
};

static char scaler_dev_name[50] = "/dev/sprd_scale";

static enum scale_fmt_e cmr_scale_fmt_cvt(uint32_t cmt_fmt)
{
	enum scale_fmt_e sc_fmt = SCALE_FTM_MAX;

	switch (cmt_fmt) {
	case IMG_DATA_TYPE_YUV422:
		sc_fmt = SCALE_YUV422;
		break;

	case IMG_DATA_TYPE_YUV420:
		sc_fmt = SCALE_YUV420;
		break;

	case IMG_DATA_TYPE_RGB565:
		sc_fmt = SCALE_RGB565;
		break;

	case IMG_DATA_TYPE_RGB888:
		sc_fmt = SCALE_RGB888;
		break;

	default:
		CMR_LOGE("scale format error");
		break;
	}

	return sc_fmt;
}

static void* cmr_scale_thread_proc(void* data)
{
	int ret = 0;
	int thread_exit_flag = 0;
	CMR_MSG_INIT(message);
	struct scale_file *file = (struct scale_file *)data;

	if (!file) {
		CMR_LOGE("scale erro: file is null");
		return NULL;
	}

	sem_post(&file->thread_sync_sem);

	if (-1 == file->fd) {
		CMR_LOGE("scale error: fd is invalid");
		return NULL;
	}

	CMR_LOGI("scale thread: In");

	while(1) {
		ret = cmr_msg_get(file->msg_que_handle, &message, 1);
		if (ret) {
			CMR_LOGE("scale error: msg destroied");
			break;
		}

		CMR_LOGV("scale message.msg_type 0x%x, data 0x%x", message.msg_type, (uint32_t)message.data);

		switch (message.msg_type) {
		case CMR_EVT_SCALE_INIT:
			CMR_LOGI("scale init");
			break;

		case CMR_EVT_SCALE_START:
			CMR_LOGI("scale start");
			struct img_frm frame;

			struct scale_cfg_param_t *cfg_params = (struct scale_cfg_param_t *)message.data;
			struct scale_frame_param_t *frame_params = &cfg_params->frame_params;

			ret = ioctl(file->fd, SCALE_IO_START, frame_params);
			if (ret) {
				CMR_LOGE("scale error: start");
			}

			pthread_mutex_lock(&file->cb_mutex);
			file->scale_evt_cb = cfg_params->scale_cb;
			pthread_mutex_unlock(&file->cb_mutex);

			if (file->scale_evt_cb) {
				memset((void *)&frame, 0x00, sizeof(frame));
				frame.size.width = frame_params->output_size.w;
				frame.size.height = frame_params->output_size.h;
				frame.addr_phy.addr_y = frame_params->output_addr.yaddr;
				frame.addr_phy.addr_u = frame_params->output_addr.uaddr;
				frame.addr_phy.addr_v = frame_params->output_addr.vaddr;
				pthread_mutex_lock(&file->cb_mutex);
				(*file->scale_evt_cb)(CMR_IMG_CVT_SC_DONE, &frame);
				pthread_mutex_unlock(&file->cb_mutex);
			} else {
				sem_post(&file->done_sem);
			}
			break;

		case CMR_EVT_SCALE_EXIT:
			CMR_LOGI("scale exit");
			thread_exit_flag = 1;
			sem_post(&file->thread_sync_sem);
			break;

		default:
			break;
		}

		if (1 == message.alloc_flag) {
			free(message.data);
		}

		if (thread_exit_flag) {
			break;
		}
	}
	CMR_LOGI("scale thread: Out");

	return NULL;
}

static int cmr_scale_create_thread(struct scale_file *file)
{
	int  ret = 0;
	pthread_attr_t attr;

	if (!file) {
		CMR_LOGE("scale error: file is null");
		return -1;
	}

	ret = cmr_msg_queue_create(SCALE_MSG_QUEUE_SIZE, &file->msg_que_handle);
	if (ret) {
		CMR_LOGE("scale error: create thread msg");
		return -1;
	}

	sem_init(&file->thread_sync_sem, 0, 0);
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	ret = pthread_create(&file->scale_thread, &attr, cmr_scale_thread_proc, (void *)file);
	sem_wait(&file->thread_sync_sem);

	return ret;
}

static int cmr_scale_kill_thread(struct scale_file *file)
{
	int ret = 0;
	CMR_MSG_INIT(message);
	uint32_t queue_handle = 0;

	if (!file) {
		CMR_LOGE("scale error: file is null");
		return -1;
	}

	message.msg_type = CMR_EVT_SCALE_EXIT;
	queue_handle = file->msg_que_handle;
	ret = cmr_msg_post(queue_handle, &message, 1);
	if (ret) {
		CMR_LOGE("scale error: send msg");
	}
	sem_wait(&file->thread_sync_sem);
	sem_destroy(&file->thread_sync_sem);

	cmr_msg_queue_destroy(file->msg_que_handle);
	file->msg_que_handle = 0;

	return ret;
}

int cmr_scale_evt_reg(int handle,cmr_evt_cb  scale_event_cb)
{
	struct scale_file *file = (struct scale_file*)(handle);

	if (!file) {
		CMR_LOGE("scale error: file is null");
		return -1;
	}

	pthread_mutex_lock(&file->cb_mutex);
	file->scale_evt_cb = scale_event_cb;
	pthread_mutex_unlock(&file->cb_mutex);

	return 0;
}

int cmr_scale_open(void)
{
	int ret = 0;
	int fd = -1;
	int handle = 0;
	int time_out = 3;
	struct scale_file *file = NULL;

	file = (struct scale_file*)calloc(1, sizeof(struct scale_file));
	if(!file) {
		CMR_LOGE("scale error: no memory for file");
		ret = -1;
		goto exit;
	}

	for ( ; time_out > 0; time_out--) {
		fd = open(scaler_dev_name, O_RDWR, 0);

		if (-1 == fd) {
			CMR_LOGI("scale sleep 50ms");
			usleep(50*1000);
		} else {
			break;
		}
	};

	if (0 == time_out) {
		CMR_LOGE("scale error: open device");
		goto free_file;
	}

	file->fd = fd;

	ret = pthread_mutex_init(&file->cb_mutex, NULL);
	if (ret) {
		CMR_LOGE("scale error: init cb mutex");
		goto free_file;
	}

	sem_init(&file->done_sem, 0, 0);

	ret = cmr_scale_create_thread(file);
	if (ret) {
		CMR_LOGE("scale error: create thread");
		goto free_cb;
	}

	file->scale_evt_cb = NULL;
	handle = (int)file;

	goto exit;

free_cb:
	pthread_mutex_destroy(&file->cb_mutex);
free_file:
	if(file)
		free(file);
	file = NULL;
exit:
	CMR_LOGI("scale handle: %x",handle);

	return handle;
}

int cmr_scale_start(int handle,
		struct img_frm *src_img,
		struct img_rect *rect,
		struct img_frm *dst_img,
		cmr_evt_cb cmr_event_cb)
{
	int ret = 0;
	CMR_MSG_INIT(message);
	struct scale_cfg_param_t *cfg_params = NULL;
	struct scale_frame_param_t *frame_params = NULL;
	struct scale_file *file = (struct scale_file*)(handle);

	if (!file) {
		CMR_LOGE("scale error: file hand is null");
		goto exit;
	}

	cfg_params =(struct scale_cfg_param_t*)calloc(1, sizeof(struct scale_cfg_param_t));
	if (!cfg_params) {
		CMR_LOGE("scale error: no mem for cfg parameters");
		goto exit;
	}

	frame_params = &cfg_params->frame_params;
	cfg_params->scale_cb = cmr_event_cb;

	/*set scale input parameters*/
	memcpy((void*)&frame_params->input_size, (void*)&src_img->size,
		sizeof(struct scale_size_t));

	if (rect) {
		memcpy((void*)&frame_params->input_rect, (void*)rect,
			sizeof(struct scale_rect_t ));
	} else {
		frame_params->input_rect.x = 0;
		frame_params->input_rect.y = 0;
		frame_params->input_rect.w = src_img->size.width;
		frame_params->input_rect.h = src_img->size.height;
	}

	frame_params->input_format = cmr_scale_fmt_cvt(src_img->fmt);

	memcpy((void*)&frame_params->input_addr , (void*)&src_img->addr_phy,
		sizeof(struct scale_addr_t));

	memcpy((void*)&frame_params->input_endian, (void*)&src_img->data_end,
		sizeof(struct scale_endian_sel_t));

	/*set scale output parameters*/
	memcpy((void*)&frame_params->output_size, (void*)&dst_img->size,
		sizeof(struct scale_size_t));

	frame_params->output_format =cmr_scale_fmt_cvt(dst_img->fmt);

	memcpy((void*)&frame_params->output_addr , (void*)&dst_img->addr_phy,
		sizeof(struct scale_addr_t));

	memcpy((void*)&frame_params->output_endian, (void*)&dst_img->data_end,
		sizeof(struct scale_endian_sel_t));

	/*set scale mode*/
	frame_params->scale_mode = SCALE_MODE_NORMAL;

	/*start scale*/
	message.data = (void *)cfg_params;
	message.alloc_flag = 1;
	message.msg_type = CMR_EVT_SCALE_START;
	ret = cmr_msg_post(file->msg_que_handle, &message, 1);

	if (ret) {
		CMR_LOGE("scale error: fail to send start msg");

		goto free_frame;
	}

	if (NULL == cmr_event_cb) {
		sem_wait(&file->done_sem);
	}

	return ret;

free_frame:
	free(cfg_params);
	cfg_params = NULL;

exit:
	return -1;

}

int cmr_scale_close(int handle)
{
	int ret = 0;
	struct scale_file *file = (struct scale_file*)(handle);

	CMR_LOGI("scale close device enter");

	if (!file) {
		CMR_LOGI("scale fail: file hand is null");
		goto exit;
	}

	ret = cmr_scale_kill_thread(file);
	if (ret) {
		CMR_LOGE("scale error: kill thread");
	}

	sem_destroy(&file->done_sem);
	pthread_mutex_destroy(&file->cb_mutex);

	if (-1 != file->fd) {
		if (-1 == close(file->fd)) {
			CMR_LOGE("scale error: close");
		}
	} else {
		CMR_LOGE("scale error: fd is invalid");
	}

	free(file);

exit:
	CMR_LOGI("scale close device exit");

	return ret;
}

int cmr_scale_capability(int handle,uint32_t *width, uint32_t *sc_factor)
{
	int ret = 0;
	uint32_t rd_word[2] = {0, 0};

	struct scale_file *file = (struct scale_file*)(handle);

	if (!file) {
		CMR_LOGE("scale error: file hand is null");
		return -ENODEV;
	}

	if (-1 == file->fd) {
		CMR_LOGE("Fail to open scaler device.");
		return -ENODEV;
	}

	if (NULL == width || NULL == sc_factor) {
		CMR_LOGE("scale error: param={0x%x, 0x%x)", (uint32_t)width, (uint32_t)sc_factor);
		return -ENODEV;
	}

	ret = read(file->fd, rd_word, 2*sizeof(uint32_t));
	*width = rd_word[0];
	*sc_factor = rd_word[1];

	CMR_LOGI("scale width=%d, sc_factor=%d", *width, *sc_factor);

	return ret;
}
