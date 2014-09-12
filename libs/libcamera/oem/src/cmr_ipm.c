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

#define LOG_TAG "cmr_ipm"

#include "cmr_ipm.h"
#include "cmr_msg.h"

extern struct class_tab_t hdr_tab_info;
extern struct class_tab_t fd_tab_info;

struct class_tab_t* arith_info_tab[] =
{
	NULL,
	&hdr_tab_info,
	&fd_tab_info,
};

#define CHECK_HANDLE_VALID(handle) \
	do { \
		if (!handle) { \
			return CMR_CAMERA_INVALID_PARAM; \
		} \
	} while(0)

cmr_int cmr_ipm_init(struct ipm_init_in *in, cmr_handle *ipm_handle)
{
	cmr_int                ret = CMR_CAMERA_SUCCESS;
	struct ipm_context_t   *handle;

	if (!in || !ipm_handle) {
		CMR_LOGE("Invalid Param!");
		return CMR_CAMERA_INVALID_PARAM;
	}

	handle = (struct ipm_context_t *)malloc(sizeof(struct ipm_context_t));
	if (!handle) {
		CMR_LOGE("No mem!");
		return CMR_CAMERA_NO_MEM;
	}

	cmr_bzero(handle, sizeof(struct ipm_context_t));

	handle->init_in = *in;

	*ipm_handle = (cmr_handle)handle;

	return ret;
}

cmr_int cmr_ipm_deinit(cmr_handle ipm_handle)
{
	cmr_int               ret     = CMR_CAMERA_SUCCESS;
	struct ipm_context_t  *handle = (struct ipm_context_t *)ipm_handle;

	CHECK_HANDLE_VALID(handle);

	free(handle);

	return ret;
}

cmr_int cmr_ipm_open(cmr_handle ipm_handle, cmr_uint class_type,struct ipm_open_in *in,
			struct ipm_open_out *out, cmr_handle *ipm_class_handle)
{
	cmr_int              ret = CMR_CAMERA_SUCCESS;

	if (!out || !in || !ipm_handle || !ipm_class_handle) {
		CMR_LOGE("Invalid Param!");
		return CMR_CAMERA_INVALID_PARAM;
	}

	if (!arith_info_tab[class_type]->ops->open) {
		CMR_LOGE("Invalid ops Param!");
		return CMR_CAMERA_INVALID_PARAM;
	}

	ret = arith_info_tab[class_type]->ops->open(ipm_handle,in,out,ipm_class_handle);

	return ret;
}
cmr_int cmr_ipm_close(cmr_handle ipm_class_handle)
{
	cmr_int              ret             = CMR_CAMERA_SUCCESS;
	struct ipm_common    *common_handle  = (struct ipm_common *)ipm_class_handle;
	cmr_uint             class_type;

	class_type = common_handle->class_type;

	if (!arith_info_tab[class_type]->ops->close) {
		CMR_LOGE("Invalid ops Param!");
		return CMR_CAMERA_INVALID_PARAM;
	}

	ret = arith_info_tab[class_type]->ops->close(ipm_class_handle);

	return ret;
}

cmr_int ipm_transfer_frame(cmr_handle ipm_class_handle, struct ipm_frame_in *in, struct ipm_frame_out *out)
{
	cmr_int              ret             = CMR_CAMERA_SUCCESS;
	struct ipm_common    *common_handle  = (struct ipm_common *)ipm_class_handle;
	cmr_uint             class_type;

	class_type = common_handle->class_type;

	if (!arith_info_tab[class_type]->ops->transfer_frame) {
		CMR_LOGE("Invalid ops Param!");
		return CMR_CAMERA_INVALID_PARAM;
	}

	ret = arith_info_tab[class_type]->ops->transfer_frame(ipm_class_handle,in,out);

	return ret;
}

cmr_int cmr_ipm_pre_proc(cmr_handle ipm_class_handle)
{
	cmr_int              ret             = CMR_CAMERA_SUCCESS;
	struct ipm_common    *common_handle  = (struct ipm_common *)ipm_class_handle;
	cmr_uint             class_type;

	class_type = common_handle->class_type;

	if (!arith_info_tab[class_type]->ops->pre_proc) {
		CMR_LOGE("Invalid ops Param!");
		return CMR_CAMERA_INVALID_PARAM;
	}

	ret = arith_info_tab[class_type]->ops->pre_proc(ipm_class_handle);

	return ret;
}

cmr_int cmr_ipm_post_proc(cmr_handle ipm_class_handle)
{
	cmr_int              ret             = CMR_CAMERA_SUCCESS;
	struct ipm_common    *common_handle  = (struct ipm_common *)ipm_class_handle;
	cmr_uint             class_type;

	class_type = common_handle->class_type;

	if (!arith_info_tab[class_type]->ops->post_proc) {
		CMR_LOGE("Invalid ops Param!");
		return CMR_CAMERA_INVALID_PARAM;
	}

	ret = arith_info_tab[class_type]->ops->post_proc(ipm_class_handle);

	return ret;
}

cmr_int cmr_ipm_get_capability (struct ipm_capability *out)
{
	cmr_int              ret  = CMR_CAMERA_SUCCESS;
	enum ipm_class_type  type = IPM_TYPE_NONE;

	for (; arith_info_tab[type] != NULL; ++type) {
		out->class_type_bits |= type;
	}

	return ret;
}

