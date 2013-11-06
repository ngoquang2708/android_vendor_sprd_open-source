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
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include "cmr_cvt.h"
#include "sprd_rot_k.h"

static char               rot_dev_name[50] = "/dev/sprd_rotation";
static int                rot_fd = -1;
static cmr_evt_cb         rot_evt_cb = NULL;
static pthread_mutex_t    rot_cb_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t          rot_thread;
static void*              rot_user_data;
static sem_t              rot_sem;
static pthread_mutex_t    rot_status_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint32_t           rot_running = 0;

static ROT_DATA_FORMAT_E cmr_rot_fmt_cvt(uint32_t cmr_fmt)
{
	ROT_DATA_FORMAT_E        fmt = ROT_FMT_MAX;

	switch (cmr_fmt) {
	case IMG_DATA_TYPE_YUV422:
		fmt = ROT_YUV422;
		break;
	case IMG_DATA_TYPE_YUV420:
		fmt = ROT_YUV420;
		break;
	case IMG_DATA_TYPE_RGB565:
		fmt = ROT_RGB565;
		break;
	case IMG_DATA_TYPE_RGB888:
		fmt = ROT_RGB888;
		break;
	default:
		break;
	}

	return fmt;
}

static void* cmr_rot_thread_proc(void* data)
{
	int                      evt_id;
	struct img_frm           frame;
	uint32_t                 param;

	CMR_LOGV("rot_thread In");

	bzero(&frame, sizeof(frame));

	while(1) {
		if (-1 == ioctl(rot_fd, ROT_IO_IS_DONE, &param)) {
			CMR_LOGV("To exit rot thread");
			break;
		} else {
			CMR_LOGV("rot done OK. 0x%x", (uint32_t)rot_evt_cb);
			frame.reserved = rot_user_data;
			evt_id = CMR_IMG_CVT_ROT_DONE;
			pthread_mutex_lock(&rot_cb_mutex);
			if (rot_evt_cb) {
				(*rot_evt_cb)(evt_id, &frame);
			}
			pthread_mutex_unlock(&rot_cb_mutex);
			pthread_mutex_lock(&rot_status_mutex);
			rot_running = 0;
			pthread_mutex_unlock(&rot_status_mutex);
			sem_post(&rot_sem);
		}
	}

	CMR_LOGV("rot_thread Out");
	return NULL;
}

static int   cmr_rot_create_thread(void)
{
	int                      ret = 0;
	pthread_attr_t           attr;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	ret = pthread_create(&rot_thread, &attr, cmr_rot_thread_proc, NULL);
	pthread_attr_destroy(&attr);
	return ret;
}

int cmr_rot_init(void)
{
	int                      ret = 0;

	rot_fd = open(rot_dev_name, O_RDWR, 0);

	if (-1 == rot_fd) {
		CMR_LOGE("Fail to open rotation device.");
		return -ENODEV;
	} else {
		CMR_LOGV("OK to open rotation device.");
	}

	ret = pthread_mutex_init(&rot_cb_mutex, NULL);
	if (ret) {
		CMR_LOGE("Failed to init mutex : %d", ret);
		exit(EXIT_FAILURE);
	}

	ret = pthread_mutex_init(&rot_status_mutex, NULL);
	if (ret) {
		CMR_LOGE("Failed to init status mutex : %d", ret);
		exit(EXIT_FAILURE);
	}


	sem_init(&rot_sem, 0, 1);

	ret = cmr_rot_create_thread();
	rot_evt_cb = NULL;

	return ret;
}

int cmr_rot_evt_reg(cmr_evt_cb  rot_event_cb)
{
	pthread_mutex_lock(&rot_cb_mutex);
	rot_evt_cb = rot_event_cb;
	pthread_mutex_unlock(&rot_cb_mutex);
	return 0;
}

int cmr_rot(enum img_rot_angle  angle,
		struct img_frm  *src_img,
		struct img_rect *trim,
		struct img_frm  *dst_img,
		void            *user_data)
{
	struct _rot_cfg_tag      rot_cfg;
	int                      ret = 0;

	if (-1 == rot_fd) {
		CMR_LOGE("Invalid fd");
		return -ENODEV;
	}

	if (NULL == src_img || NULL == dst_img) {
		CMR_LOGE("Wrong parameter 0x%x 0x%x", (uint32_t)src_img, (uint32_t)dst_img);
		return -EINVAL;
	}

	CMR_LOGV("angle %d, src 0x%x 0x%x, w h %d %d, dst 0x%x 0x%x",
		angle,
		src_img->addr_phy.addr_y,
		src_img->addr_phy.addr_u,
		src_img->size.width,
		src_img->size.height,
		dst_img->addr_phy.addr_y,
		dst_img->addr_phy.addr_u);


	if ((uint32_t)angle < (uint32_t)(IMG_ROT_90)) {
		CMR_LOGE("Wrong angle %d", angle);
		return -EINVAL;
	}

	sem_wait(&rot_sem);

	rot_cfg.format          = cmr_rot_fmt_cvt(src_img->fmt);
	if (rot_cfg.format >= ROT_FMT_MAX) {
		CMR_LOGE("Unsupported format %d, %d", src_img->fmt, rot_cfg.format);
		sem_post(&rot_sem);
		return -EINVAL;
	}

	rot_cfg.angle = angle - IMG_ROT_90 + ROT_90;
	rot_cfg.src_addr.y_addr = src_img->addr_phy.addr_y;
	rot_cfg.src_addr.u_addr = src_img->addr_phy.addr_u;
	rot_cfg.src_addr.v_addr = src_img->addr_phy.addr_v;
	rot_cfg.dst_addr.y_addr = dst_img->addr_phy.addr_y;
	rot_cfg.dst_addr.u_addr = dst_img->addr_phy.addr_u;
	rot_cfg.dst_addr.v_addr = dst_img->addr_phy.addr_v;
	rot_cfg.img_size.w      = (uint16_t)src_img->size.width;
	rot_cfg.img_size.h      = (uint16_t)src_img->size.height;

	rot_user_data = user_data;

	ret = ioctl(rot_fd, ROT_IO_CFG, &rot_cfg);
	if (ret) {
		CMR_LOGE("Unsupported format %d, %d", src_img->fmt, rot_cfg.format);
		sem_post(&rot_sem);
		return -EINVAL;
	}

	ret = ioctl(rot_fd, ROT_IO_START, 1);
	pthread_mutex_lock(&rot_status_mutex);
	rot_running = 1;
	pthread_mutex_unlock(&rot_status_mutex);


	return ret;
}

int cmr_rot_wait_done(void)
{
	int                      ret = 0;
	uint32_t                 need_wait = 0;

	pthread_mutex_lock(&rot_status_mutex);
	need_wait = rot_running;
	pthread_mutex_unlock(&rot_status_mutex);

	if (need_wait) {
		CMR_LOGV("Wait for rot done.");
		sem_wait(&rot_sem);
		sem_post(&rot_sem);
	}

	return ret;
}

static int cmr_rot_kill_thread(void)
{
	int                      ret = 0;
	char                     write_ch = 0;
	void                     *dummy;

	if (-1 == rot_fd) {
		CMR_LOGE("invalid fd");
		return -ENODEV;
	}

	ret = write(rot_fd, &write_ch, 1);// kill thread;
	if (ret > 0) {
		ret = pthread_join(rot_thread, &dummy);
	}

	return ret;
}

int cmr_rot_deinit(void)
{
	int                      ret = 0;

	CMR_LOGV("Start to close rotation device.");

	if (-1 == rot_fd) {
		CMR_LOGE("Invalid fd");
		return -ENODEV;
	}

	sem_wait(&rot_sem);
	sem_post(&rot_sem);

	/* thread should be killed before fd deinited */
	ret = cmr_rot_kill_thread();
	if (ret) {
		CMR_LOGE("Failed to kill the thread. errno : %d", ret);
		exit(EXIT_FAILURE);
	}

	sem_destroy(&rot_sem);

	/* then close fd */
	if (-1 == close(rot_fd)) {
		exit(EXIT_FAILURE);
	}
	rot_fd = -1;


	pthread_mutex_lock(&rot_cb_mutex);
	rot_evt_cb = NULL;
	pthread_mutex_unlock(&rot_cb_mutex);
	pthread_mutex_destroy(&rot_cb_mutex);
	CMR_LOGV("close device.");
	return 0;
}
