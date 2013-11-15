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

#define LOG_NDEBUG 0
#define LOG_TAG "SprdCameraHardware"

#include <utils/Log.h>
#include <utils/String16.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <cutils/properties.h>
#include "../../../gralloc/gralloc_priv.h"
#include "ion_sprd.h"

#include <camera/Camera.h>
#include <media/hardware/MetadataBufferType.h>

#include "SprdOEMCamera.h"
#include "SprdCameraHardwareInterface.h"


#ifdef CONFIG_CAMERA_ISP
extern "C" {
#include "isp_video.h"
}
#endif

//////////////////////////////////////////////////////////////////
namespace android {
#define FREE_PMEM_BAK 1

#define STOP_PREVIEW_BEFORE_CAPTURE 0

#define LOGV       ALOGV
#define LOGE       ALOGE
#define LOGI       ALOGI
#define LOGW       ALOGW
#define LOGD       ALOGD

#define PRINT_TIME 				0
#define ROUND_TO_PAGE(x)  	(((x)+0xfff)&~0xfff)
#define ARRAY_SIZE(x) 		(sizeof(x) / sizeof(*x))
#define METADATA_SIZE 		28 // (4 * 3)

#define SET_PARM(x,y) 	do { \
							LOGV("%s: set camera param: %s, %d", __func__, #x, y); \
							camera_set_parm (x, y, NULL, NULL); \
						} while(0)
#define SIZE_ALIGN(x)   (((x)+15)&(~15))
#define SWITCH_MONITOR_QUEUE_SIZE         50

static nsecs_t s_start_timestamp = 0;
static nsecs_t s_end_timestamp = 0;
static int s_use_time = 0;

#define GET_START_TIME do { \
                             s_start_timestamp = systemTime(); \
                       }while(0)
#define GET_END_TIME do { \
                             s_end_timestamp = systemTime(); \
                       }while(0)
#define GET_USE_TIME do { \
                            s_use_time = (s_end_timestamp - s_start_timestamp)/1000000; \
                     }while(0)

///////////////////////////////////////////////////////////////////
//static members
/////////////////////////////////////////////////////////////////////////////////////////
gralloc_module_t const* SprdCameraHardware::mGrallocHal = NULL;

const CameraInfo SprdCameraHardware::kCameraInfo[] = {
	{
        CAMERA_FACING_BACK,
        90,  /* orientation */
    },
#ifndef CONFIG_DCAM_SENSOR_NO_FRONT_SUPPORT
    {
        CAMERA_FACING_FRONT,
        270,  /* orientation */
    },
#endif
};

const CameraInfo SprdCameraHardware::kCameraInfo3[] = {
    {
        CAMERA_FACING_BACK,
        90,  /* orientation */
    },

    {
        CAMERA_FACING_FRONT,
        270,  /* orientation */
    },

    {
        2,
        0,  /* orientation */
    }
};

int SprdCameraHardware::getPropertyAtv()
{
	char propBuf_atv[PROPERTY_VALUE_MAX] = {0};
	int atv = 0;

	property_get("sys.camera.atv", propBuf_atv, "0");
	if (0 == strcmp(propBuf_atv, "1")) {
		atv = 1;
	}
	else {
		atv = 0;
	}

	LOGV("getPropertyAtv: %d", atv);

	return atv;
}

int SprdCameraHardware::getNumberOfCameras()
{
	int num = 0;

	if (1 == getPropertyAtv()) {
		num = sizeof(SprdCameraHardware::kCameraInfo3) / sizeof(SprdCameraHardware::kCameraInfo3[0]);
	} else {
		num = sizeof(SprdCameraHardware::kCameraInfo) / sizeof(SprdCameraHardware::kCameraInfo[0]);
	}

	LOGV("getNumberOfCameras: %d",num);
	return num;
}

int SprdCameraHardware::getCameraInfo(int cameraId, struct camera_info *cameraInfo)
{
	if (1 == getPropertyAtv()) {
		memcpy(cameraInfo, &kCameraInfo3[cameraId], sizeof(CameraInfo));
	} else {
		memcpy(cameraInfo, &kCameraInfo[cameraId], sizeof(CameraInfo));
	}
	return 0;
}
/////////////////////////////////////////////////////////////////////////////////////////
//SprdCameraHardware: public functions
/////////////////////////////////////////////////////////////////////////////////////////
SprdCameraHardware::SprdCameraHardware(int cameraId)
	:
	mPreviewHeapSize(0),
	mPreviewHeapNum(0),
	mPreviewDcamAllocBufferCnt(0),
	mPreviewHeapArray(NULL),
	mRawHeap(NULL),
	mRawHeapSize(0),
	mMiscHeap(NULL),
	mMiscHeapSize(0),
	mMiscHeapNum(0),
	mJpegHeapSize(0),
	mFDAddr(0),
	mMetadataHeap(NULL),
	mParameters(),
	mSetParameters(),
	mSetParametersBak(),
	mUseParameters(),
	mPreviewHeight_trimy(0),
	mPreviewWidth_trimx(0),
	mPreviewHeight_backup(0),
	mPreviewWidth_backup(0),
	mPreviewHeight(-1),
	mPreviewWidth(-1),
	mRawHeight(-1),
	mRawWidth(-1),
	mPreviewFormat(1),
	mPictureFormat(1),
	mPreviewStartFlag(0),
	mIsDvPreview(0),
	mRecordingMode(0),
	mBakParamFlag(0),
	mRecordingFirstFrameTime(0),
	mZoomLevel(0),
	mJpegSize(0),
	mNotify_cb(0),
	mData_cb(0),
	mData_cb_timestamp(0),
	mGetMemory_cb(0),
	mUser(0),
	mPreviewWindow(NULL),
	mMsgEnabled(0),
	mIsStoreMetaData(false),
	mIsFreqChanged(false),
	mCameraId(cameraId),
	miSPreviewFirstFrame(1),
	mCaptureMode(CAMERA_ZSL_MODE),
	mCaptureRawMode(0),
#ifdef CONFIG_CAMERA_ROTATION_CAPTURE
	mIsRotCapture(1),
#else
	mIsRotCapture(0),
#endif
	mTimeCoeff(1),
	mPreviewBufferUsage(PREVIEW_BUFFER_USAGE_DCAM),
	mSetFreqCount(0),
	mSwitchMonitorMsgQueHandle(0),
	mSwitchMonitorInited(0)
{
	LOGV("openCameraHardware: E cameraId: %d.", cameraId);

#if defined(CONFIG_BACK_CAMERA_ROTATION) || defined(CONFIG_FRONT_CAMERA_ROTATION)
	mPreviewBufferUsage = PREVIEW_BUFFER_USAGE_DCAM;
#endif

	memset(mPreviewHeapArray_phy, 0, sizeof(mPreviewHeapArray_phy));
	memset(mPreviewHeapArray_vir, 0, sizeof(mPreviewHeapArray_vir));
	memset(mMiscHeapArray, 0, sizeof(mMiscHeapArray));
	memset(mPreviewBufferHandle, 0, kPreviewBufferCount * sizeof(void*));
	memset(mPreviewCancelBufHandle, 0, kPreviewBufferCount * sizeof(void*));

#if FREE_PMEM_BAK

	memset(&mPreviewHeapInfoBak, 0, sizeof(mPreviewHeapInfoBak));
	mPreviewHeapBakUseFlag = 0;

	memset(&mRawHeapInfoBak, 0, sizeof(mRawHeapInfoBak));
	mRawHeapBakUseFlag = 0;

#endif
	setCameraState(SPRD_INIT, STATE_CAMERA);

	if (!mGrallocHal) {
		int ret = hw_get_module(GRALLOC_HARDWARE_MODULE_ID, (const hw_module_t **)&mGrallocHal);
		if (ret)
			LOGE("ERR(%s):Fail on loading gralloc HAL", __func__);
	}

	switch_monitor_thread_init((void *)this);

	if (1 == getPropertyAtv()) {
		mCameraId = 5; //for ATV
	} else {
		mCameraId = cameraId;
	}

	initDefaultParameters();

	LOGV("openCameraHardware: X cameraId: %d.", cameraId);
}

SprdCameraHardware::~SprdCameraHardware()
{
	LOGV("closeCameraHardware: E cameraId: %d.", mCameraId);
	LOGV("closeCameraHardware: X cameraId: %d.", mCameraId);
}

void SprdCameraHardware::release()
{
    LOGV("release E");
    LOGV("mLock:release E .\n");
    Mutex::Autolock l(&mLock);

    // Either preview was ongoing, or we are in the middle or taking a
    // picture.  It's the caller's responsibility to make sure the camera
    // is in the idle or init state before destroying this object.
	LOGV("release:camera state = %s, preview state = %s, capture state = %s",
		getCameraStateStr(getCameraState()), getCameraStateStr(getPreviewState()),
		getCameraStateStr(getCaptureState()));

	switch_monitor_thread_deinit((void *)this);

	if (isCapturing()) {
		cancelPictureInternal();
	}

	while (0 < mSetFreqCount) {
		set_ddr_freq("0");
		mSetFreqCount--;
	}

	if (isPreviewing()) {
		camera_set_stop_preview_mode(1);
		stopPreviewInternal();
	}

	if (isCameraInit()) {
		// When libqcamera detects an error, it calls camera_cb from the
		// call to camera_stop, which would cause a deadlock if we
		// held the mStateLock.  For this reason, we have an intermediate
		// state SPRD_INTERNAL_STOPPING, which we use to check to see if the
		// camera_cb was called inline.
		setCameraState(SPRD_INTERNAL_STOPPING, STATE_CAMERA);

		LOGV("stopping camera.");
		if(CAMERA_SUCCESS != camera_stop(camera_cb, this)){
			setCameraState(SPRD_ERROR, STATE_CAMERA);
			mMetadataHeap = NULL;
			LOGE("release: fail to camera_stop().");
			LOGV("mLock:release X.\n");
			return;
		}

		WaitForCameraStop();
	}

	mMetadataHeap = NULL;
	deinitCapture();

#if FREE_PMEM_BAK
	mCbPrevDataBusyLock.lock();
	/*preview bak heap check and free*/
	if (false == mPreviewHeapInfoBak.busy_flag) {
		LOGV("release free prev heap bak mem");
		clearPmem(&mPreviewHeapInfoBak);
		memset(&mPreviewHeapInfoBak, 0, sizeof(mPreviewHeapInfoBak));
	} else {
		LOGE("release prev mem busy, this is unknown error!!!");
	}
	mPreviewHeapBakUseFlag = 0;
	mCbPrevDataBusyLock.unlock();

	mCbCapDataBusyLock.lock();
	/* capture head check and free*/
	if (false == mRawHeapInfoBak.busy_flag) {
		LOGV("release free raw heap bak mem");
		clearPmem(&mRawHeapInfoBak);
		memset(&mRawHeapInfoBak, 0, sizeof(mRawHeapInfoBak));
	} else {
		LOGE("release cap mem busy, this is unknown error!!!");
	}
	mRawHeapBakUseFlag = 0;
	mCbCapDataBusyLock.unlock();
#endif

	LOGV("release X");
	LOGV("mLock:release X.\n");
}

int SprdCameraHardware::getCameraId() const
{
    return mCameraId;
}

status_t SprdCameraHardware::startPreview()
{
	LOGV("startPreview: E");
	Mutex::Autolock l(&mLock);

	setCaptureRawMode(0);

	bool isRecordingMode = (mMsgEnabled & CAMERA_MSG_VIDEO_FRAME) > 0 ? true : false;
	return startPreviewInternal(isRecordingMode);
}

void SprdCameraHardware::stopPreview()
{
	LOGV("stopPreview: E");
	Mutex::Autolock l(&mLock);
	camera_set_stop_preview_mode(0);
	stopPreviewInternal();
	setRecordingMode(false);
	LOGV("stopPreview: X");
}

bool SprdCameraHardware::previewEnabled()
{
    bool ret = 0;
    LOGV("mLock:previewEnabled E.\n");
    Mutex::Autolock l(&mLock);
    LOGV("mLock:previewEnabled X.\n");
    return isPreviewing();
}

status_t SprdCameraHardware::setPreviewWindow(preview_stream_ops *w)
{
    int min_bufs;

	LOGV("setPreviewWindow E");
	Mutex::Autolock l(&mParamLock);


    mPreviewWindow = w;
    

    LOGV("%s: mPreviewWindow %p", __func__, mPreviewWindow);

    if (!w) {
        mPreviewBufferUsage = PREVIEW_BUFFER_USAGE_DCAM;
        LOGE("preview window is NULL!");
        return NO_ERROR;
    }

/*
    if (isPreviewing()){
        LOGI("stop preview (window change)");
		camera_set_stop_preview_mode(0);
        stopPreviewInternal();
    }
*/
    if (w->get_min_undequeued_buffer_count(w, &min_bufs)) {
        LOGE("%s: could not retrieve min undequeued buffer count", __func__);
        return INVALID_OPERATION;
    }

    if (min_bufs >= kPreviewBufferCount) {
        LOGE("%s: min undequeued buffer count %d is too high (expecting at most %d)", __func__,
             min_bufs, kPreviewBufferCount - 1);
    }

    LOGV("%s: setting buffer count to %d", __func__, kPreviewBufferCount);
    if (w->set_buffer_count(w, kPreviewBufferCount)) {
        LOGE("%s: could not set buffer count", __func__);
        return INVALID_OPERATION;
    }

    int preview_width;
    int preview_height;
    mParameters.getPreviewSize(&preview_width, &preview_height);
    LOGV("%s: preview size: %dx%d.", __func__, preview_width, preview_height);

#if CAM_OUT_YUV420_UV
    int hal_pixel_format = HAL_PIXEL_FORMAT_YCbCr_420_SP;
#else
    int hal_pixel_format = HAL_PIXEL_FORMAT_YCrCb_420_SP;
#endif

    const char *str_preview_format = mParameters.getPreviewFormat();
	int usage;

    LOGV("%s: preview format %s", __func__, str_preview_format);

	if (preview_width < 640) {
		mPreviewBufferUsage = PREVIEW_BUFFER_USAGE_DCAM;
	}

#ifdef CONFIG_CAMERA_DMA_COPY
    usage = GRALLOC_USAGE_SW_WRITE_OFTEN | GRALLOC_USAGE_PRIVATE_0;
#else
    if (PREVIEW_BUFFER_USAGE_DCAM == mPreviewBufferUsage) {
        	usage = GRALLOC_USAGE_SW_WRITE_OFTEN;
    } else {
        	usage = GRALLOC_USAGE_SW_WRITE_OFTEN | GRALLOC_USAGE_PRIVATE_0;
    }
#endif

    if (preview_width > 640) {

		if (w->set_usage(w, usage )) {
        	LOGE("%s: could not set usage on gralloc buffer", __func__);
        	return INVALID_OPERATION;
    	}
    } else {

		if (w->set_usage(w, usage )) {
        	LOGE("%s: could not set usage on gralloc buffer", __func__);
        	return INVALID_OPERATION;
    	}
    }
	if (mParameters.getPreviewEnv()) {
		mIsDvPreview = 1;
	    if (w->set_buffers_geometry(w,
	                                SIZE_ALIGN(preview_width), SIZE_ALIGN(preview_height),
	                                hal_pixel_format)) {
	        LOGE("%s: could not set buffers geometry to %s",
	             __func__, str_preview_format);
	        return INVALID_OPERATION;
	    }
	} else {
		mIsDvPreview = 0;
	    if (w->set_buffers_geometry(w,
	                                preview_width, preview_height,
	                                hal_pixel_format)) {
	        LOGE("%s: could not set buffers geometry to %s",
	             __func__, str_preview_format);
	        return INVALID_OPERATION;
	    }

    }

    if (w->set_crop(w, 0, 0, preview_width-1, preview_height-1)) {
        LOGE("%s: could not set crop to %s",
             __func__, str_preview_format);
        return INVALID_OPERATION;
    }
/*
   status_t ret = startPreviewInternal(isRecordingMode());
   if (ret != NO_ERROR) {
		return INVALID_OPERATION;
   }
*/
    return NO_ERROR;
}

status_t SprdCameraHardware::takePicture()
{
    GET_START_TIME;
	takepicture_mode mode = getCaptureMode();
	LOGV("takePicture: E");

	LOGV("ISP_TOOL:takePicture: %d E", mode);

	print_time();

	Mutex::Autolock l(&mLock);

	if (SPRD_ERROR == mCameraState.capture_state) {
		LOGE("takePicture in error status, deinit capture at first ");
		deinitCapture();
	} else if (SPRD_IDLE != mCameraState.capture_state) {
		LOGE("take picture: action alread exist, direct return!");
		return ALREADY_EXISTS;
	}

	if (!iSZslMode()) {
		//stop preview first for debug
		if (isPreviewing()) {
			LOGV("call stopPreviewInternal in takePicture().");
			camera_set_stop_preview_mode(0);
			stopPreviewInternal();
		}
		LOGV("ok to stopPreviewInternal in takePicture. preview state = %d", getPreviewState());

		if (isPreviewing()) {
			LOGE("takePicture: stop preview error!, preview state = %d", getPreviewState());
			return UNKNOWN_ERROR;
		}
		if (!initCapture(mData_cb != NULL)) {
			deinitCapture();
			LOGE("takePicture initCapture failed. Not taking picture.");
			return UNKNOWN_ERROR;
		}
	}

	//wait for last capture being finished
	if (isCapturing()) {
		WaitForCaptureDone();
	}

	setCameraState(SPRD_INTERNAL_RAW_REQUESTED, STATE_CAPTURE);
    LOGV("INTERPOLATION::takePicture:mRawWidth=%d,mZoomLevel=%d",mRawWidth,mZoomLevel);
    if(CAMERA_SUCCESS != camera_take_picture(camera_cb, this, mode))
    {
    	setCameraState(SPRD_ERROR, STATE_CAPTURE);
		LOGE("takePicture: fail to camera_take_picture.");
		return UNKNOWN_ERROR;
    }

	bool result = WaitForCaptureStart();

	print_time();
    LOGV("takePicture: X");

    return result ? NO_ERROR : UNKNOWN_ERROR;
}

status_t SprdCameraHardware::cancelPicture()
{
	Mutex::Autolock l(&mLock);

	return cancelPictureInternal();
}

status_t SprdCameraHardware::setTakePictureSize(uint32_t width, uint32_t height)
{
	mRawWidth = width;
	mRawHeight = height;

	return NO_ERROR;
}

status_t SprdCameraHardware::startRecording()
{
	LOGV("mLock:startRecording S.\n");
	Mutex::Autolock l(&mLock);
	mRecordingFirstFrameTime = 0;

#if 1
	if (isPreviewing()) {
		if (camera_is_need_stop_preview()) {
			LOGV("wxz call stopPreviewInternal in startRecording().");
			camera_set_stop_preview_mode(1);
			setCameraState(SPRD_INTERNAL_PREVIEW_STOPPING, STATE_PREVIEW);
			if(CAMERA_SUCCESS != camera_stop_preview()){
				setCameraState(SPRD_ERROR, STATE_PREVIEW);
				freePreviewMem();
				LOGE("startRecording: fail to camera_stop_preview().");
				return INVALID_OPERATION;
			}

			WaitForPreviewStop();

			LOGV("startRecording: Freeing preview heap.");
			freePreviewMem();
		}
	}
#endif

	return startPreviewInternal(true);
}

void SprdCameraHardware::stopRecording()
{
	LOGV("stopRecording: E");
	Mutex::Autolock l(&mLock);
	camera_set_stop_preview_mode(1);
	stopPreviewInternal();
	mRecordingFirstFrameTime = 0;
	LOGV("stopRecording: X");
}

void SprdCameraHardware::releaseRecordingFrame(const void *opaque)
{
	LOGV("releaseRecordingFrame E. ");

	if (!isPreviewing()) {
		LOGE("releaseRecordingFrame: Preview not in progress!");
		return;
	}

	if (PREVIEW_BUFFER_USAGE_DCAM == mPreviewBufferUsage) {
		//Mutex::Autolock l(&mLock);
		uint8_t *addr = (uint8_t *)opaque;
		int32_t index;

		uint32_t *vaddr = NULL;
		uint32_t *paddr = NULL;

		if (mIsStoreMetaData) {
			index = (addr - (uint8_t *)mMetadataHeap->data) / (METADATA_SIZE);
			paddr = (uint32_t *) *((uint32_t*)addr + 1);
			vaddr = (uint32_t *) *((uint32_t*)addr + 2);

		} else {
			for (index=0; index<kPreviewBufferCount; index++) {
				if ((uint32_t)addr == mPreviewHeapArray_vir[index])	break;
			}

			if (index < kPreviewBufferCount) {
				vaddr = (uint32_t*)mPreviewHeapArray_vir[index];
				paddr = (uint32_t*)mPreviewHeapArray_phy[index];
			}
		}

		if (index > kPreviewBufferCount) {
			LOGV("releaseRecordingFrame error: index: %d, data: %x, w=%d, h=%d \n",
			index, (uint32_t)addr, mPreviewWidth, mPreviewHeight);
		}

		flush_buffer(CAMERA_FLUSH_PREVIEW_HEAP, index,
					(void*)vaddr,
					(void*)paddr,
					(int)mPreviewHeapSize);

		camera_release_frame(index);
		LOGV("releaseRecordingFrame: index: %d", index);
	} else {
		releasePreviewFrame();
	}
}

bool SprdCameraHardware::recordingEnabled()
{
	LOGV("recordingEnabled: E");
	Mutex::Autolock l(&mLock);
	LOGV("recordingEnabled: X");

	return isPreviewing() && (mMsgEnabled & CAMERA_MSG_VIDEO_FRAME);
}

status_t SprdCameraHardware::autoFocus()
{
	LOGV("Starting auto focus.");
	LOGV("mLock:autoFocus E.\n");
	Mutex::Autolock l(&mLock);

	if (!isPreviewing()) {
		LOGE("autoFocus: not previewing");
		return INVALID_OPERATION;
	}

	if (SPRD_IDLE != getFocusState()) {
		LOGE("autoFocus existing, direct return!");
		return ALREADY_EXISTS;
	}
	mMsgEnabled |= CAMERA_MSG_FOCUS;

	if(0 != camera_start_autofocus(CAMERA_AUTO_FOCUS, camera_cb, this)){
		LOGE("auto foucs fail.");
		//return INVALID_OPERATION;
	}

	setCameraState(SPRD_FOCUS_IN_PROGRESS, STATE_FOCUS);

	LOGV("mLock:autoFocus X.\n");
	return NO_ERROR;
}

status_t SprdCameraHardware::cancelAutoFocus()
{
	bool ret = 0;
	LOGV("mLock:CancelFocus E.\n");
	Mutex::Autolock l(&mLock);
	mMsgEnabled &= ~CAMERA_MSG_FOCUS;
	ret = camera_cancel_autofocus();

	WaitForFocusCancelDone();
	LOGV("mLock:CancelFocus X.\n");
	return ret;
}

void SprdCameraHardware::setCaptureRawMode(bool mode)
{
	mCaptureRawMode = mode;
	LOGV("ISP_TOOL: setCaptureRawMode: %d, %d", mode, mCaptureRawMode);
}

void SprdCameraHardware::antiShakeParamSetup( )
{
#ifdef CONFIG_CAMERA_ANTI_SHAKE
	mPreviewWidth = mPreviewWidth_backup + ((((mPreviewWidth_backup/ 10) + 15) >> 4) << 4);
	mPreviewHeight = mPreviewHeight_backup + ((((mPreviewHeight_backup/ 10) + 15) >> 4) << 4);
#endif
}

status_t SprdCameraHardware::checkSetParametersEnvironment( )
{
	status_t ret =  NO_ERROR;
	/*check capture status*/
	if (SPRD_IDLE != getCaptureState()) {
		LOGE("warning, camera HAL in capturing process, abnormal calling sequence!");
	}

	/*check preview status*/
	if ((SPRD_IDLE != getPreviewState()) && (SPRD_PREVIEW_IN_PROGRESS != getPreviewState())) {
		LOGE("camera HAL in preview changing process, not allow to setParameter");
		return PERMISSION_DENIED;
	}

	return ret;
}

status_t SprdCameraHardware::checkSetParameters(const SprdCameraParameters& params, const SprdCameraParameters& oriParams)
{
	status_t ret =  NO_ERROR;
	int32_t tmpSize = sizeof(SprdCameraParameters);
	ret = memcmp(&params, &oriParams, tmpSize);
	return ret;
}

status_t SprdCameraHardware::checkSetParameters(const SprdCameraParameters& params)
{
	//wxz20120316: check the value of preview-fps-range. //CR168435
	int min,max;
	params.getPreviewFpsRange(&min, &max);
	if((min > max) || (min < 0) || (max < 0)){
		LOGE("Error to FPS range: min: %d, max: %d.", min, max);
		return UNKNOWN_ERROR;
	}

	//check preview size
	int w,h;
	params.getPreviewSize(&w, &h);
	if((w < 0) || (h < 0)){
		LOGE("Error to preview size: w: %d, h: %d.", w, h);
		mParameters.setPreviewSize(640, 480);
		return UNKNOWN_ERROR;
	}

	return NO_ERROR;
}

status_t SprdCameraHardware::setParameters(const SprdCameraParameters& params)
{
	struct cmr_msg           message = {0, 0, 0, 0};
	status_t                 ret = NO_ERROR;
	Mutex::Autolock          l(&mLock);
	mParamLock.lock();

	if (checkSetParameters(params)) {
		mParamLock.unlock();
		return UNKNOWN_ERROR;
	}

	if (0 == checkSetParameters(params, mSetParameters)) {
		LOGV("setParameters same parameters with system, directly return!");
		mParamLock.unlock();
		return NO_ERROR;
	} else if (SPRD_IDLE != getSetParamsState()) {
		LOGV("setParameters is handling, backup the parameter!");
		mSetParametersBak = params;
		mBakParamFlag = 1;
		mParamLock.unlock();
		return NO_ERROR;
	} else {
		mSetParameters = params;
	}

	message.msg_type = CMR_EVT_SW_MON_SET_PARA;
	message.data = NULL;

	ret = cmr_msg_post(mSwitchMonitorMsgQueHandle, &message);
	if (ret) {
		LOGE("setParameters Fail to send one msg!");
		mParamLock.unlock();
		return NO_ERROR;
	}
	if (mParamWait.waitRelative(mParamLock, SET_PARAM_TIMEOUT)) {
		LOGE("setParameters wait timeout!");
	} else {
		LOGV("setParameters wait OK");
	}
	mParamLock.unlock();
	usleep(10*1000);
	return ret;
}

status_t SprdCameraHardware::setParametersInternal(const SprdCameraParameters& params)
{
	status_t ret =  NO_ERROR;
	uint32_t                 isZoomChange = 0;


	LOGV("setParametersInternal: E params = %p", &params);
	LOGV("mLock:setParametersInternal E.\n");
	mParamLock.lock();
#if 0
	// FIXME: verify params
	// yuv422sp is here only for legacy reason. Unfortunately, we release
	// the code with yuv422sp as the default and enforced setting. The
	// correct setting is yuv420sp.
	if(strcmp(params.getPreviewFormat(), "yuv422sp")== 0) {
		mPreviewFormat = 0;
	}
	else if(strcmp(params.getPreviewFormat(), "yuv420sp") == 0) {
		mPreviewFormat = 1;
	}
	else if(strcmp(params.getPreviewFormat(), "rgb565") == 0) {
		mPreviewFormat = 2;
	}
	else if(strcmp(params.getPreviewFormat(), "yuv420p") == 0) {
		mPreviewFormat = 3;
	}
	else {
		LOGE("Onlyyuv422sp/yuv420sp/rgb565 preview is supported.\n");
		return INVALID_OPERATION;
	}

	if(strcmp(params.getPictureFormat(), "yuv422sp")== 0) {
		mPictureFormat = 0;
	}
	else if(strcmp(params.getPictureFormat(), "yuv420sp")== 0) {
		mPictureFormat = 1;
	}
	else if(strcmp(params.getPictureFormat(), "rgb565")== 0) {
		mPictureFormat = 2;
	}
	else if(strcmp(params.getPictureFormat(), "jpeg")== 0) {
		mPictureFormat = 3;
	}
	else {
		LOGE("Onlyyuv422sp/yuv420sp/rgb565/jpeg  picture format is supported.\n");
		return INVALID_OPERATION;
	}
#else
	mPreviewFormat = 1;
	mPictureFormat = 1;
#endif

	LOGV("setParametersInternal: mPreviewFormat=%d,mPictureFormat=%d.",mPreviewFormat,mPictureFormat);

	if (0 == checkSetParameters(params, mParameters)) {
		LOGV("setParametersInternal X: same parameters with system, directly return!");
		mParamLock.unlock();
		return NO_ERROR;
	}

	ret = checkSetParametersEnvironment();
	if (NO_ERROR != ret) {
		LOGE("setParametersInternal X: invalid status , directly return!");
		mParamLock.unlock();
		return NO_ERROR;
	}

	/*check if the ZOOM level changed*/
	if (mParameters.getZoom() != ((SprdCameraParameters)params).getZoom()) {
		LOGV("setParametersInternal, zoom level changed");
		isZoomChange = 1;
	}

	// FIXME: will this make a deep copy/do the right thing? String8 i
	// should handle it
	mParameters = params;
	if (1 != mBakParamFlag) {
		/*if zoom parameter changed, then the action should be sync*/
		if (!isZoomChange) {
			mParamWait.signal();
		}
	} else {
		mBakParamFlag = 0;
	}
	LOGV("setParametersInternal param set OK.");
	mParamLock.unlock();

	// libqcamera only supports certain size/aspect ratios
	// find closest match that doesn't exceed app's request
	int width = 0, height = 0;
	int rawWidth = 0, rawHeight = 0;
	params.getPreviewSize(&width, &height);
	LOGV("setParametersInternal: requested preview size %d x %d", width, height);
	params.getPictureSize(&rawWidth, &rawHeight);
	LOGV("setParametersInternal:requested picture size %d x %d", rawWidth, rawHeight);

	mPreviewWidth = (width + 1) & ~1;
	mPreviewHeight = (height + 1) & ~1;
	mPreviewWidth_backup = mPreviewWidth;
	mPreviewHeight_backup = mPreviewHeight;
	mRawHeight = (rawHeight + 1) & ~1;
	mRawWidth = (rawWidth + 1) & ~1;

	antiShakeParamSetup();
	LOGV("setParametersInternal: requested picture size %d x %d", mRawWidth, mRawHeight);
	LOGV("setParametersInternal: requested preview size %d x %d", mPreviewWidth, mPreviewHeight);

	camera_cfg_rot_cap_param_reset();

	if (camera_set_change_size(mRawWidth, mRawHeight, mPreviewWidth, mPreviewHeight)) {
		if (isPreviewing()) {
			camera_set_stop_preview_mode(1);
			mPreviewCbLock.lock();
			stopPreviewInternal();
			mPreviewCbLock.unlock();
			if (NO_ERROR != setPreviewWindow(mPreviewWindow)) {
				LOGE("setParametersInternal X: setPreviewWindow fail, unknown error!");
				ret = UNKNOWN_ERROR;
				goto setParamEnd;
			}
			if (NO_ERROR != startPreviewInternal(isRecordingMode())) {
				LOGE("setParametersInternal X: change size startPreviewInternal fail, unknown error!");
				ret = UNKNOWN_ERROR;
				goto setParamEnd;
			}
		} else {
			if (NO_ERROR != setPreviewWindow(mPreviewWindow)) {
				LOGE("setParametersInternal X: setPreviewWindow fail, unknown error!");
				ret = UNKNOWN_ERROR;
				goto setParamEnd;
			}
		}
	}

	if ((1 == params.getInt("zsl")) &&
		((mCaptureMode != CAMERA_ZSL_CONTINUE_SHOT_MODE) && (mCaptureMode != CAMERA_ZSL_MODE))) {
		LOGI("mode change:stop preview.");
		if (isPreviewing()) {
			camera_set_stop_preview_mode(1);
			mPreviewCbLock.lock();
			stopPreviewInternal();
			mPreviewCbLock.unlock();
			if (NO_ERROR != startPreviewInternal(isRecordingMode())) {
				LOGE("setParametersInternal X: open ZSL startPreviewInternal fail, unknown error!");
				ret = UNKNOWN_ERROR;
				goto setParamEnd;
			}
		}
	}
	if ((0 == params.getInt("zsl")) &&
		((mCaptureMode == CAMERA_ZSL_CONTINUE_SHOT_MODE) || (mCaptureMode == CAMERA_ZSL_MODE))) {
		LOGI("mode change:stop preview.");
		if (isPreviewing()) {
			camera_set_stop_preview_mode(0);
			mPreviewCbLock.lock();
			stopPreviewInternal();
			mPreviewCbLock.unlock();
			if (NO_ERROR != startPreviewInternal(isRecordingMode())) {
				LOGE("setParametersInternal X: close ZSL startPreviewInternal fail, unknown error!");
				ret = UNKNOWN_ERROR;
				goto setParamEnd;
			}
		}
	}

	if(NO_ERROR != setCameraParameters()){
		ret = UNKNOWN_ERROR;
	}

setParamEnd:
	if (isZoomChange) {
		mParamWait.signal();
	}
	LOGV("mLock:setParametersInternal X.\n");

	return ret;
}

SprdCameraParameters SprdCameraHardware::getParameters()
{
	LOGV("getParameters: E");
	Mutex::Autolock          l(&mLock);
	Mutex::Autolock          pl(&mParamLock);
	if ((0 != checkSetParameters(mParameters, mSetParametersBak)) &&
		(1 == mBakParamFlag)) {
		mUseParameters = mSetParametersBak;
	} else {
		mUseParameters = mParameters;
	}

	LOGV("getParameters: X");
	return mUseParameters;
}

void SprdCameraHardware::setCallbacks(camera_notify_callback notify_cb,
                                    camera_data_callback data_cb,
                                    camera_data_timestamp_callback data_cb_timestamp,
                                    camera_request_memory get_memory,
                                    void *user)
{
	mNotify_cb = notify_cb;
	mData_cb = data_cb;
	mData_cb_timestamp = data_cb_timestamp;
	mGetMemory_cb = get_memory;
	mUser = user;
}

void SprdCameraHardware::enableMsgType(int32_t msgType)
{
	LOGV("mLock:enableMsgType E .\n");
	Mutex::Autolock lock(mLock);
	mMsgEnabled |= msgType;
	LOGV("mLock:enableMsgType X .\n");
}

void SprdCameraHardware::disableMsgType(int32_t msgType)
{
	LOGV("'mLock:disableMsgType E.\n");
	//Mutex::Autolock lock(mLock);
	if (msgType & CAMERA_MSG_VIDEO_FRAME) {
		mRecordingFirstFrameTime = 0;
	}
	mMsgEnabled &= ~msgType;
	LOGV("'mLock:disableMsgType X.\n");
}

bool SprdCameraHardware::msgTypeEnabled(int32_t msgType)
{
	LOGV("mLock:msgTypeEnabled E.\n");
	Mutex::Autolock lock(mLock);
	LOGV("mLock:msgTypeEnabled X.\n");
	return (mMsgEnabled & msgType);
}

status_t SprdCameraHardware::sendCommand(int32_t cmd, int32_t arg1, int32_t arg2)
{
    uint32_t buffer_size = mPreviewWidth * mPreviewHeight * 3 / 2;
    uint32_t addr = 0;
    LOGE("sendCommand: facedetect mem size 0x%x.",buffer_size);
	if(CAMERA_CMD_START_FACE_DETECTION == cmd){
		setFdmem(buffer_size);
        camera_set_start_facedetect(1);
	} else if(CAMERA_CMD_STOP_FACE_DETECTION == cmd) {
	    LOGE("sendCommand: not support the CAMERA_CMD_STOP_FACE_DETECTION.");
        camera_set_start_facedetect(0);
        FreeFdmem();
	}

	return NO_ERROR;
}

status_t SprdCameraHardware::storeMetaDataInBuffers(bool enable)
{
    // FIXME:
    // metadata buffer mode can be turned on or off.
    // Spreadtrum needs to fix this.
    if (!enable) {
		LOGE("Non-metadata buffer mode is not supported!");
		mIsStoreMetaData = false;
		return INVALID_OPERATION;
    }

	if(NULL == mMetadataHeap) {
		if(NULL == (mMetadataHeap = mGetMemory_cb(-1, METADATA_SIZE, kPreviewBufferCount, NULL))) {
			LOGE("fail to alloc memory for the metadata for storeMetaDataInBuffers.");
			return INVALID_OPERATION;
		}
	}

	mIsStoreMetaData = true;

	return NO_ERROR;
}

status_t SprdCameraHardware::dump(int fd) const
{
	const size_t SIZE = 256;
	char buffer[SIZE];
	String8 result;
	const Vector<String16> args;

	// Dump internal primitives.
//	snprintf(buffer, 255, "SprdCameraHardware::dump: state (%s, %s, %s)\n",
//				getCameraStateStr(getCameraState()), getCameraStateStr(getPreviewState()), getCameraStateStr(getCaptureState()));
	result.append(buffer);
	snprintf(buffer, 255, "preview width(%d) x height (%d)\n", mPreviewWidth, mPreviewHeight);
	result.append(buffer);
	snprintf(buffer, 255, "raw width(%d) x height (%d)\n", mRawWidth, mRawHeight);
	result.append(buffer);
	snprintf(buffer, 255, "preview frame size(%d), raw size (%d), jpeg size (%d) and jpeg max size (%d)\n", mPreviewHeapSize, mRawHeapSize, mJpegSize, mJpegHeapSize);
	result.append(buffer);
	write(fd, result.string(), result.size());
	mParameters.dump(fd, args);
	return NO_ERROR;
}

//////////////////////////////////////////////////////////////////////////////
//SprdCameraHardware: private functions
//////////////////////////////////////////////////////////////////////////////
const char* SprdCameraHardware::getCameraStateStr(
        SprdCameraHardware::Sprd_camera_state s)
{
        static const char* states[] = {
#define STATE_STR(x) #x
            STATE_STR(SPRD_INIT),
            STATE_STR(SPRD_IDLE),
            STATE_STR(SPRD_ERROR),
            STATE_STR(SPRD_PREVIEW_IN_PROGRESS),
            STATE_STR(SPRD_FOCUS_IN_PROGRESS),
            STATE_STR(SPRD_SET_PARAMS_IN_PROGRESS),
            STATE_STR(SPRD_WAITING_RAW),
            STATE_STR(SPRD_WAITING_JPEG),
            STATE_STR(SPRD_INTERNAL_PREVIEW_STOPPING),
            STATE_STR(SPRD_INTERNAL_CAPTURE_STOPPING),
            STATE_STR(SPRD_INTERNAL_PREVIEW_REQUESTED),
            STATE_STR(SPRD_INTERNAL_RAW_REQUESTED),
            STATE_STR(SPRD_INTERNAL_STOPPING),
#undef STATE_STR
        };
        return states[s];
}

void SprdCameraHardware::print_time()
{
#if PRINT_TIME
	struct timeval time;
	gettimeofday(&time, NULL);
	LOGV("time: %lld us.", time.tv_sec * 1000000LL + time.tv_usec);
#endif
}

// Called with mStateLock held!
bool SprdCameraHardware::setCameraDimensions()
{
	if (isPreviewing() || isCapturing()) {
		LOGE("setCameraDimensions: expecting state SPRD_IDLE, not %s, %s",
				getCameraStateStr(getPreviewState()),
				getCameraStateStr(getCaptureState()));
		return false;
	}

	if (0 != camera_set_dimensions(mRawWidth, mRawHeight, mPreviewWidth,
								mPreviewHeight, NULL, NULL, mCaptureMode != CAMERA_RAW_MODE)) {
		return false;
	}

	return true;
}

void SprdCameraHardware::setCameraPreviewMode(bool isRecordMode)
{
/*	if (isRecordingMode()) {
		SET_PARM(CAMERA_PARM_PREVIEW_MODE, CAMERA_PREVIEW_MODE_MOVIE);
	} else {
		SET_PARM(CAMERA_PARM_PREVIEW_MODE, CAMERA_PREVIEW_MODE_SNAPSHOT);
	}*/
	mParamLock.lock();
	mUseParameters = mParameters;
	mParamLock.unlock();

	if (isRecordMode) {
		SET_PARM(CAMERA_PARM_PREVIEW_MODE, mUseParameters.getPreviewFameRate());
	} else {
		SET_PARM(CAMERA_PARM_PREVIEW_MODE, CAMERA_PREVIEW_MODE_SNAPSHOT);
		if (mUseParameters.getPreviewEnv()) {
			SET_PARM(CAMERA_PARM_PREVIEW_ENV, mUseParameters.getPreviewFameRate());
			mIsDvPreview = 1;
		} else {
			SET_PARM(CAMERA_PARM_PREVIEW_ENV, CAMERA_PREVIEW_MODE_SNAPSHOT);
			mIsDvPreview = 0;
			}
	}
}

bool SprdCameraHardware::isRecordingMode()
{
	return mRecordingMode;
}

void SprdCameraHardware::setRecordingMode(bool enable)
{
	mRecordingMode = enable;
}

void SprdCameraHardware::setCameraState(Sprd_camera_state state, state_owner owner)
{

	LOGV("setCameraState:state: %s, owner: %d", getCameraStateStr(state), owner);
	Mutex::Autolock stateLock(&mStateLock);

	switch (state) {
	//camera state
	case SPRD_INIT:
		mCameraState.camera_state = SPRD_INIT;
		mCameraState.preview_state = SPRD_IDLE;
		mCameraState.capture_state = SPRD_IDLE;
		mCameraState.focus_state = SPRD_IDLE;
		mCameraState.setParam_state = SPRD_IDLE;
		break;

	case SPRD_IDLE:
		switch (owner) {
		case STATE_CAMERA:
			mCameraState.camera_state = SPRD_IDLE;
			break;

		case STATE_PREVIEW:
			mCameraState.preview_state = SPRD_IDLE;
			break;

		case STATE_CAPTURE:
			mCameraState.capture_state = SPRD_IDLE;
			break;

		case STATE_FOCUS:
			mCameraState.focus_state = SPRD_IDLE;
			break;

		case STATE_SET_PARAMS:
			mCameraState.setParam_state = SPRD_IDLE;
			break;
		}
		break;

	case SPRD_INTERNAL_STOPPING:
		mCameraState.camera_state = SPRD_INTERNAL_STOPPING;
		break;

	case SPRD_ERROR:
		switch (owner) {
		case STATE_CAMERA:
			mCameraState.camera_state = SPRD_ERROR;
			break;

		case STATE_PREVIEW:
			mCameraState.preview_state = SPRD_ERROR;
			break;

		case STATE_CAPTURE:
			mCameraState.capture_state = SPRD_ERROR;
			break;

		case STATE_FOCUS:
			mCameraState.focus_state = SPRD_ERROR;
			break;

		default:
			break;
		}
		break;

	//preview state
	case SPRD_PREVIEW_IN_PROGRESS:
		mCameraState.preview_state = SPRD_PREVIEW_IN_PROGRESS;
		break;

	case SPRD_INTERNAL_PREVIEW_STOPPING:
		mCameraState.preview_state = SPRD_INTERNAL_PREVIEW_STOPPING;
		break;

	case SPRD_INTERNAL_PREVIEW_REQUESTED:
		mCameraState.preview_state = SPRD_INTERNAL_PREVIEW_REQUESTED;
		break;

	//capture state
	case SPRD_INTERNAL_RAW_REQUESTED:
		mCameraState.capture_state = SPRD_INTERNAL_RAW_REQUESTED;
		break;

	case SPRD_WAITING_RAW:
		mCameraState.capture_state = SPRD_WAITING_RAW;
		break;

	case SPRD_WAITING_JPEG:
		mCameraState.capture_state = SPRD_WAITING_JPEG;
		break;

	case SPRD_INTERNAL_CAPTURE_STOPPING:
		mCameraState.capture_state = SPRD_INTERNAL_CAPTURE_STOPPING;
		break;

	//focus state
	case SPRD_FOCUS_IN_PROGRESS:
		mCameraState.focus_state = SPRD_FOCUS_IN_PROGRESS;
		break;

	//set_param state
	case SPRD_SET_PARAMS_IN_PROGRESS:
		mCameraState.setParam_state = SPRD_SET_PARAMS_IN_PROGRESS;
		break;

	default:
		LOGD("setCameraState: error");
		break;
	}

	LOGV("setCameraState:camera state = %s, preview state = %s, capture state = %s focus state = %s set param state = %s",
				getCameraStateStr(mCameraState.camera_state),
				getCameraStateStr(mCameraState.preview_state),
				getCameraStateStr(mCameraState.capture_state),
				getCameraStateStr(mCameraState.focus_state),
				getCameraStateStr(mCameraState.setParam_state));
}

SprdCameraHardware::Sprd_camera_state SprdCameraHardware::getCameraState()
{
	LOGV("getCameraState: %s", getCameraStateStr(mCameraState.camera_state));
	return mCameraState.camera_state;
}

SprdCameraHardware::Sprd_camera_state SprdCameraHardware::getPreviewState()
{
	LOGV("getPreviewState: %s", getCameraStateStr(mCameraState.preview_state));
	return mCameraState.preview_state;
}

SprdCameraHardware::Sprd_camera_state SprdCameraHardware::getCaptureState()
{
	LOGV("getCaptureState: %s", getCameraStateStr(mCameraState.capture_state));
	return mCameraState.capture_state;
}

SprdCameraHardware::Sprd_camera_state SprdCameraHardware::getFocusState()
{
	LOGV("getFocusState: %s", getCameraStateStr(mCameraState.focus_state));
	return mCameraState.focus_state;
}

SprdCameraHardware::Sprd_camera_state SprdCameraHardware::getSetParamsState()
{
	LOGV("getSetParamsState: %s", getCameraStateStr(mCameraState.setParam_state));
	return mCameraState.setParam_state;
}

bool SprdCameraHardware::isCameraInit()
{
	LOGV("isCameraInit: %s", getCameraStateStr(mCameraState.camera_state));
	return (SPRD_IDLE == mCameraState.camera_state);
}

bool SprdCameraHardware::isCameraIdle()
{
	return (SPRD_IDLE == mCameraState.preview_state
			&& SPRD_IDLE == mCameraState.capture_state);
}

bool SprdCameraHardware::isPreviewing()
{
	LOGV("isPreviewing: %s", getCameraStateStr(mCameraState.preview_state));
	return (SPRD_PREVIEW_IN_PROGRESS == mCameraState.preview_state);
}

bool SprdCameraHardware::isCapturing()
{
	LOGV("isCapturing: %s", getCameraStateStr(mCameraState.capture_state));
#if 1
	return (SPRD_WAITING_RAW == mCameraState.capture_state
			|| SPRD_WAITING_JPEG == mCameraState.capture_state);
#else
	return (SPRD_IDLE != mCameraState.capture_state);
#endif
}

bool SprdCameraHardware::checkPreviewStateForCapture()
{
	bool ret = true;
	Sprd_camera_state tmpState = SPRD_IDLE;

	tmpState = getPreviewState();
	if (iSZslMode()) {
		if (SPRD_PREVIEW_IN_PROGRESS != tmpState) {
			LOGV("incorrect preview status %d of ZSL capture mode", (uint32_t)tmpState);
			ret = false;
		}
	} else {
		if (SPRD_IDLE != tmpState) {
			LOGV("incorrect preview status %d of normal capture mode", (uint32_t)tmpState);
			ret = false;
		}
	}

	return ret;
}

bool SprdCameraHardware::WaitForCameraStart()
{
	Mutex::Autolock stateLock(&mStateLock);

    while(SPRD_IDLE != mCameraState.camera_state
			&& SPRD_ERROR != mCameraState.camera_state) {
        LOGV("WaitForCameraStart: waiting for SPRD_IDLE");
        mStateWait.wait(mStateLock);
        LOGV("WaitForCameraStart: woke up");
    }

	return SPRD_IDLE == mCameraState.camera_state;
}

bool SprdCameraHardware::WaitForCameraStop()
{
	Mutex::Autolock stateLock(&mStateLock);

	if (SPRD_INTERNAL_STOPPING == mCameraState.camera_state)
	{
	    while(SPRD_INIT != mCameraState.camera_state
				&& SPRD_ERROR != mCameraState.camera_state) {
	        LOGV("WaitForCameraStop: waiting for SPRD_IDLE");
	        mStateWait.wait(mStateLock);
	        LOGV("WaitForCameraStop: woke up");
	    }
	}

	return SPRD_INIT == mCameraState.camera_state;
}

bool SprdCameraHardware::WaitForPreviewStart()
{
	Mutex::Autolock stateLock(&mStateLock);
	while(SPRD_PREVIEW_IN_PROGRESS != mCameraState.preview_state
		&& SPRD_ERROR != mCameraState.preview_state) {
		LOGV("WaitForPreviewStart: waiting for SPRD_PREVIEW_IN_PROGRESS");
		mStateWait.wait(mStateLock);
		LOGV("WaitForPreviewStart: woke up");
	}

	return SPRD_PREVIEW_IN_PROGRESS == mCameraState.preview_state;
}

bool SprdCameraHardware::WaitForPreviewStop()
{
	Mutex::Autolock statelock(&mStateLock);
    while (SPRD_IDLE != mCameraState.preview_state
			&& SPRD_ERROR != mCameraState.preview_state) {
		LOGV("WaitForPreviewStop: waiting for SPRD_IDLE");
		mStateWait.wait(mStateLock);
		LOGV("WaitForPreviewStop: woke up");
    }

	return SPRD_IDLE == mCameraState.preview_state;
}

bool SprdCameraHardware::WaitForCaptureStart()
{
	Mutex::Autolock stateLock(&mStateLock);

    // It's possible for the YUV callback as well as the JPEG callbacks
    // to be invoked before we even make it here, so we check for all
    // possible result states from takePicture.
	while (SPRD_WAITING_RAW != mCameraState.capture_state
		 && SPRD_WAITING_JPEG != mCameraState.capture_state
		 && SPRD_IDLE != mCameraState.capture_state
		 && SPRD_ERROR != mCameraState.camera_state) {
		LOGV("WaitForCaptureStart: waiting for SPRD_WAITING_RAW or SPRD_WAITING_JPEG");
		mStateWait.wait(mStateLock);
		LOGV("WaitForCaptureStart: woke up, state is %s",
				getCameraStateStr(mCameraState.capture_state));
	}

	return (SPRD_WAITING_RAW == mCameraState.capture_state
			|| SPRD_WAITING_JPEG == mCameraState.capture_state
			|| SPRD_IDLE == mCameraState.capture_state);
}

bool SprdCameraHardware::WaitForCaptureDone()
{
	Mutex::Autolock stateLock(&mStateLock);
	while (SPRD_IDLE != mCameraState.capture_state
		 && SPRD_ERROR != mCameraState.capture_state) {
		LOGV("WaitForCaptureDone: waiting for SPRD_IDLE");
		mStateWait.wait(mStateLock);
		LOGV("WaitForCaptureDone: woke up");
	}

	return SPRD_IDLE == mCameraState.capture_state;
}

bool SprdCameraHardware::WaitForFocusCancelDone()
{
	Mutex::Autolock stateLock(&mStateLock);
	while (SPRD_IDLE != mCameraState.focus_state
		 && SPRD_ERROR != mCameraState.focus_state) {
		LOGV("WaitForFocusCancelDone: waiting for SPRD_IDLE from %d", getFocusState());
		mStateWait.waitRelative(mStateLock, 1000000000);
		LOGV("WaitForFocusCancelDone: woke up");
	}

	return SPRD_IDLE == mCameraState.focus_state;
}

// Called with mLock held!
bool SprdCameraHardware::startCameraIfNecessary()
{
	mParamLock.lock();
	mUseParameters = mParameters;
	mParamLock.unlock();

	if (!isCameraInit()) {
		LOGV("waiting for camera_init to initialize.startCameraIfNecessary");
		if(CAMERA_SUCCESS != camera_init(mCameraId)){
			setCameraState(SPRD_INIT, STATE_CAMERA);
			LOGE("CameraIfNecessary: fail to camera_init().");
			return false;
		}

		if (!camera_is_sensor_support_zsl()) {
			mUseParameters.setZSLSupport("false");
		}

		LOGV("waiting for camera_start.g_camera_id: %d.", mCameraId);
		if(CAMERA_SUCCESS != camera_start(camera_cb, this, mPreviewHeight, mPreviewWidth)){
			setCameraState(SPRD_ERROR, STATE_CAMERA);
			LOGE("CameraIfNecessary: fail to camera_start().");
			return false;
		}

		LOGV("OK to camera_start.");
		WaitForCameraStart();

		LOGV("init camera: initializing parameters");
	}
	else
		LOGV("camera hardware has been started already");

	return true;
}



int SprdCameraHardware::Callback_AllocCapturePmem(void* handle, unsigned int size, unsigned int *addr_phy, unsigned int *addr_vir)
{
	LOGD("Callback_AllocCapturePmem size = %d", size);

	SprdCameraHardware* camera = (SprdCameraHardware*)handle;
	if (camera == NULL) {
		return -1;
	}
	if (camera->mMiscHeapNum >= MAX_MISCHEAP_NUM) {
		return -1;
	}

	sp<MemoryHeapIon> pHeapIon = new MemoryHeapIon("/dev/ion", size , MemoryHeapBase::NO_CACHING, ION_HEAP_CARVEOUT_MASK);
	if (pHeapIon == NULL) {
		return -1;
	}
	if (pHeapIon->getHeapID() < 0) {
		return -1;
	}

	pHeapIon->get_phy_addr_from_ion((int*)addr_phy, (int*)&size);
	*addr_vir = (int)(pHeapIon->base());
	camera->mMiscHeapArray[camera->mMiscHeapNum++] = pHeapIon;

	LOGD("Callback_AllocCapturePmem mMiscHeapNum = %d", camera->mMiscHeapNum);

	return 0;
}

int SprdCameraHardware::Callback_FreeCapturePmem(void* handle)
{
	SprdCameraHardware* camera = (SprdCameraHardware*)handle;
	if (camera == NULL) {
		return -1;
	}

	LOGD("Callback_FreePmem mMiscHeapNum = %d", camera->mMiscHeapNum);

	uint32_t i;
	for (i=0; i<camera->mMiscHeapNum; i++) {
		sp<MemoryHeapIon> pHeapIon = camera->mMiscHeapArray[i];
		if (pHeapIon != NULL) {
			pHeapIon.clear();
		}
		camera->mMiscHeapArray[i] = NULL;
	}
	camera->mMiscHeapNum = 0;

	return 0;
}

sprd_camera_memory_t* SprdCameraHardware::GetCachePmem(int buf_size, int num_bufs)
{
	sprd_camera_memory_t *memory = (sprd_camera_memory_t *)malloc(sizeof(*memory));
	if(NULL == memory) {
		LOGE("wxz: Fail to GetCachePmem, memory is NULL.");
		return NULL;
	}
	memset(memory, 0, sizeof(*memory));
	memory->busy_flag = false;

	camera_memory_t *camera_memory;
	int paddr, psize;
	int  acc = buf_size *num_bufs ;

	MemoryHeapIon *pHeapIon = new MemoryHeapIon("/dev/ion", acc ,0 , (1<<31) | ION_HEAP_CARVEOUT_MASK);

	if (NULL == pHeapIon) {
		goto getpmem_end;
	}
	if (NULL == pHeapIon->getBase()
		|| 0xffffffff == (uint32_t)pHeapIon->getBase()) {
		goto getpmem_end;
	}

	camera_memory = mGetMemory_cb(pHeapIon->getHeapID(), acc/num_bufs, num_bufs, NULL);

        if(NULL == camera_memory) {
                   goto getpmem_end;
        }
        if(0xFFFFFFFF == (uint32_t)camera_memory->data) {
                 camera_memory = NULL;
                 LOGE("Fail to GetPmem().");
                 goto getpmem_end;
       }
	pHeapIon->get_phy_addr_from_ion(&paddr, &psize);
	memory->ion_heap = pHeapIon;
	memory->camera_memory = camera_memory;
	memory->phys_addr = paddr;
	memory->phys_size = psize;
	memory->handle = camera_memory->handle;
	//memory->data = camera_memory->data;
	memory->data = pHeapIon->getBase();

       LOGV("GetCachePmem: phys_addr 0x%x, data: 0x%x, size: 0x%x, phys_size: 0x%x.",
                            memory->phys_addr, (uint32_t)camera_memory->data,
                            camera_memory->size, memory->phys_size);

getpmem_end:
	return memory;
}

sprd_camera_memory_t* SprdCameraHardware::GetPmem(int buf_size, int num_bufs)
{
	sprd_camera_memory_t *memory = (sprd_camera_memory_t *)malloc(sizeof(*memory));
	if(NULL == memory) {
		LOGE("wxz: Fail to GetPmem, memory is NULL.");
		return NULL;
	}

	camera_memory_t *camera_memory;
	int paddr, psize;
	int order = 0, acc = buf_size *num_bufs ;
	acc = camera_get_size_align_page(acc);
	MemoryHeapIon *pHeapIon = new MemoryHeapIon("/dev/ion", acc , MemoryHeapBase::NO_CACHING, ION_HEAP_CARVEOUT_MASK);

	camera_memory = mGetMemory_cb(pHeapIon->getHeapID(), acc/num_bufs, num_bufs, NULL);

	if(NULL == camera_memory) {
		goto getpmem_end;
	}

	if(0xFFFFFFFF == (uint32_t)camera_memory->data) {
		camera_memory = NULL;
		LOGE("Fail to GetPmem().");
		goto getpmem_end;
	}

	pHeapIon->get_phy_addr_from_ion(&paddr, &psize);
	memory->ion_heap = pHeapIon;
	memory->camera_memory = camera_memory;
	memory->phys_addr = paddr;
	memory->phys_size = psize;
	memory->handle = camera_memory->handle;
	//memory->data = camera_memory->data;
	memory->data = pHeapIon->getBase();

	LOGV("GetPmem: phys_addr 0x%x, data: 0x%x, size: 0x%x, phys_size: 0x%x.",
			memory->phys_addr, (uint32_t)camera_memory->data,
			camera_memory->size, memory->phys_size);

getpmem_end:
	return memory;
}

void SprdCameraHardware::FreePmem(sprd_camera_memory_t* memory)
{
	if(memory){
		if (NULL == memory->camera_memory) {
			LOGV("FreePmem memory->camera_memory is NULL");
		} else if (memory->camera_memory->release) {
			memory->camera_memory->release(memory->camera_memory);
			memory->camera_memory = NULL;
		} else {
			LOGE("fail to FreePmem: NULL is camera_memory->release.");
		}

		if(memory->ion_heap) {
			delete memory->ion_heap;
			memory->ion_heap = NULL;
		}

		free(memory);
	} else {
		LOGV("FreePmem: NULL");
	}
}

void SprdCameraHardware::clearPmem(sprd_camera_memory_t* memory)
{
	if(memory){
		if (NULL == memory->camera_memory) {
			LOGV("clearPmem memory->camera_memory is NULL");
		} else if (memory->camera_memory->release) {
			memory->camera_memory->release(memory->camera_memory);
			memory->camera_memory = NULL;
		} else {
			LOGE("fail to clearPmem: NULL is camera_memory->release.");
		}

		if(memory->ion_heap) {
			delete memory->ion_heap;
			memory->ion_heap = NULL;
		}
	} else {
		LOGV("clearPmem: NULL");
	}
}

void SprdCameraHardware::setFdmem(uint32_t size)
{
	if (mFDAddr) {
		free((void*)mFDAddr);
		mFDAddr = 0;
	}

	uint32_t addr = (uint32_t)malloc(size);
	mFDAddr = addr;
	camera_set_fd_mem(0, addr, size);
}

void SprdCameraHardware::FreeFdmem(void)
{
	if (mFDAddr) {
		camera_set_fd_mem(0,0,0);
		free((void*)mFDAddr);
		mFDAddr = 0;
	}
}

uint32_t SprdCameraHardware::getPreviewBufferID(buffer_handle_t *buffer_handle)
{
	uint32_t id = 0xffffffff;

	if (PREVIEW_BUFFER_USAGE_GRAPHICS == mPreviewBufferUsage) {
		int i = 0;
		for (i = 0; i < kPreviewBufferCount; i++) {
			if ((NULL != mPreviewBufferHandle[i]) &&
				(mPreviewBufferHandle[i] == buffer_handle)) {
				id = i;
				break;
			}
		}
	}

	return id;
}

void SprdCameraHardware::canclePreviewMem()
{
	if (PREVIEW_BUFFER_USAGE_GRAPHICS == mPreviewBufferUsage && mPreviewWindow) {
		int i = 0;
		for (i = 0; i < kPreviewBufferCount; i++) {
			if (mPreviewBufferHandle[i]) {
				if (0 != mPreviewWindow->cancel_buffer(mPreviewWindow, mPreviewBufferHandle[i])) {
					LOGE("canclePreviewMem: cancel_buffer error id = %d",i);
				}
				mPreviewBufferHandle[i] = NULL;
			}
		}
	}

}

int SprdCameraHardware::releasePreviewFrame()
{
	int ret = 0;

	if (PREVIEW_BUFFER_USAGE_GRAPHICS == mPreviewBufferUsage) {
		int stride = 0;
		uint32_t free_buffer_id = 0xffffffff;
		buffer_handle_t *buffer_handle;

		if (0 != mPreviewWindow->dequeue_buffer(mPreviewWindow, &buffer_handle, &stride)) {
			ret = -1;
			LOGE("releasePreviewFrame fail: Could not dequeue gralloc buffer!\n");
		} else {
			free_buffer_id = getPreviewBufferID(buffer_handle);
			if (mPreviewCancelBufHandle[free_buffer_id]  == mPreviewBufferHandle[free_buffer_id]) {
				mPreviewCancelBufHandle[free_buffer_id] = NULL;
			} else {
				if (CAMERA_SUCCESS != camera_release_frame(free_buffer_id)) {
					ret = -1;
				}
			}
		}
	}
	return ret;
}

bool SprdCameraHardware::allocatePreviewMemByGraphics()
{
	if (PREVIEW_BUFFER_USAGE_GRAPHICS == mPreviewBufferUsage) {
		int i = 0, usage = 0, stride = 0, miniUndequeued = 0;
		buffer_handle_t *buffer_handle = NULL;
		struct private_handle_t *private_h = NULL;

		if (0 != mPreviewWindow->set_buffer_count(mPreviewWindow, kPreviewBufferCount)) {
			LOGE("allocatePreviewMemByGraphics: could not set buffer count");
			return -1;
		}

		if (0 != mPreviewWindow->get_min_undequeued_buffer_count(mPreviewWindow, &miniUndequeued)) {
			LOGE("allocatePreviewMemByGraphics: minUndequeued error");
		}

		miniUndequeued = (miniUndequeued >= 3) ? miniUndequeued : 3;
		if (miniUndequeued >= kPreviewBufferCount) {
			LOGE("allocatePreviewMemByGraphics: minUndequeued value error: %d",miniUndequeued);
			return -1;
		}

		for (i = 0; i < kPreviewBufferCount; i++ ) {
			if (0 != mPreviewWindow->dequeue_buffer(mPreviewWindow, &buffer_handle, &stride)) {
				LOGE("allocatePreviewMemByGraphics: dequeue_buffer error");
				return -1;
			}

			private_h=(struct private_handle_t*) (*buffer_handle);
			mPreviewBufferHandle[i] = buffer_handle;
			mPreviewHeapArray_phy[i] = (uint32_t)private_h->phyaddr;
			mPreviewHeapArray_vir[i] = (uint32_t)private_h->base;

			LOGD("allocatePreviewMemByGraphics: phyaddr:0x%x, base:0x%x, size:0x%x, stride:0x%x ",
					private_h->phyaddr,private_h->base,private_h->size, stride);
		}

		for (i = (kPreviewBufferCount -miniUndequeued); i < kPreviewBufferCount; i++ ) {
			if (0 != mPreviewWindow->cancel_buffer(mPreviewWindow, mPreviewBufferHandle[i])) {
				LOGE("allocatePreviewMemByGraphics: cancel_buffer error: %d",i);
			}
			mPreviewCancelBufHandle[i] = mPreviewBufferHandle[i];
		}
		mCancelBufferEb = 1;
	}
	return 0;
}

bool SprdCameraHardware::allocatePreviewMem()
{
	uint32_t i = 0, j = 0, buffer_start_id = 0, buffer_end_id = 0;
	uint32_t buffer_size = camera_get_size_align_page(mPreviewHeapSize);

	mPreviewHeapNum = kPreviewBufferCount;
	if (camera_get_rot_set()) {
		/* allocate more buffer for rotation */
		mPreviewHeapNum += kPreviewRotBufferCount;
		LOGV("initPreview: rotation, increase buffer: %d \n", mPreviewHeapNum);
	}

	if (allocatePreviewMemByGraphics())
		return false;

	if (PREVIEW_BUFFER_USAGE_DCAM == mPreviewBufferUsage) {
		mPreviewDcamAllocBufferCnt = mPreviewHeapNum;
		buffer_start_id = 0;
		buffer_end_id = mPreviewHeapNum;
	} else {
		mPreviewDcamAllocBufferCnt = 0;
		buffer_start_id = kPreviewBufferCount;
		buffer_end_id   = buffer_start_id;

		if (camera_get_rot_set()) {
			mPreviewDcamAllocBufferCnt = kPreviewRotBufferCount;
			buffer_end_id = kPreviewBufferCount + kPreviewRotBufferCount;
		}

		/*add one node, specially used for mData_cb when receive preview frame*/
		mPreviewDcamAllocBufferCnt += 1;
		buffer_end_id += 1;
	}

	if (mPreviewDcamAllocBufferCnt > 0) {
		mPreviewHeapArray = (sprd_camera_memory_t**)malloc(mPreviewDcamAllocBufferCnt * sizeof(sprd_camera_memory_t*));
		if (mPreviewHeapArray == NULL) {
			return false;
		} else {
			memset(&mPreviewHeapArray[0], 0, mPreviewDcamAllocBufferCnt * sizeof(sprd_camera_memory_t*));
		}

		for (i = buffer_start_id; i < buffer_end_id; i++) {
			sprd_camera_memory_t* PreviewHeap = GetCachePmem(buffer_size, 1);
			if(NULL == PreviewHeap)
				return false;

			if(NULL == PreviewHeap->handle) {
				LOGE("Fail to GetPmem mPreviewHeap. buffer_size: 0x%x.", buffer_size);
				FreePmem(PreviewHeap);
				freePreviewMem();
				return false;
			}

			if(PreviewHeap->phys_addr & 0xFF) {
				LOGE("error: the mPreviewHeap is not 256 bytes aligned.");
				FreePmem(PreviewHeap);
				freePreviewMem();
				return false;
			}

			mPreviewHeapArray[j++] = PreviewHeap;
			mPreviewHeapArray_phy[i] = PreviewHeap->phys_addr;
			mPreviewHeapArray_vir[i] = (uint32_t)PreviewHeap->data;
		}
	}
	return true;
}

uint32_t SprdCameraHardware::getRedisplayMem()
{
	uint32_t buffer_size = camera_get_size_align_page(SIZE_ALIGN(mPreviewWidth) * (SIZE_ALIGN(mPreviewHeight)) * 3 / 2);

	if (mIsRotCapture) {
		buffer_size <<= 1 ;
	}

	mReDisplayHeap = GetPmem(buffer_size, 1);

	if(NULL == mReDisplayHeap)
		return 0;

	if(NULL == mReDisplayHeap->handle) {
		LOGE("Fail to GetPmem mReDisplayHeap. buffer_size: 0x%x.", buffer_size);
		return 0;
	}

	if(mReDisplayHeap->phys_addr & 0xFF) {
		LOGE("error: the mReDisplayHeap is not 256 bytes aligned.");
		return 0;
	}
	LOGV("mReDisplayHeap addr:0x%x.",(uint32_t)mReDisplayHeap->data);
	return mReDisplayHeap->phys_addr;
}

void SprdCameraHardware::FreeReDisplayMem()
{
	LOGI("free redisplay mem.");
	FreePmem(mReDisplayHeap);
	mReDisplayHeap = NULL;
}

void SprdCameraHardware::freePreviewMem()
{
	uint32_t i;
	LOGV("freePreviewMem E.");
	FreeFdmem();

	if (mPreviewHeapArray != NULL) {
		for (i = 0; i < mPreviewDcamAllocBufferCnt; i++) {
#if FREE_PMEM_BAK
			mCbPrevDataBusyLock.lock();
			if ((mPreviewHeapArray[i]) &&
				(true == mPreviewHeapArray[i]->busy_flag)) {
				LOGE("preview buffer is busy, skip, bakup and free later!");
				if (NULL == mPreviewHeapInfoBak.camera_memory) {
					memcpy(&mPreviewHeapInfoBak, mPreviewHeapArray[i], sizeof(sprd_camera_memory));
				} else {
					LOGE("preview buffer not clear, unknown error!!!");
					memcpy(&mPreviewHeapInfoBak, mPreviewHeapArray[i], sizeof(sprd_camera_memory));
				}
				mPreviewHeapBakUseFlag = 1;
				memset(mPreviewHeapArray[i], 0, sizeof(*mPreviewHeapArray[i]));
				free(mPreviewHeapArray[i]);
			} else {
				FreePmem(mPreviewHeapArray[i]);
			}
			mPreviewHeapArray[i] = NULL;
			mCbPrevDataBusyLock.unlock();
#else
			FreePmem(mPreviewHeapArray[i]);
			mPreviewHeapArray[i] = NULL;
#endif
		}
		free(mPreviewHeapArray);
		mPreviewHeapArray = NULL;
	}

	canclePreviewMem();
	mPreviewHeapSize = 0;
	mPreviewHeapNum = 0;
	LOGV("freePreviewMem X.");
}

bool SprdCameraHardware::initPreview()
{
	uint32_t page_size, buffer_size;
	uint32_t preview_buff_cnt = kPreviewBufferCount;
	mParamLock.lock();
	mUseParameters = mParameters;
	mParamLock.unlock();
	if (!startCameraIfNecessary())
		return false;

	setCameraPreviewMode(isRecordingMode());

	// Tell libqcamera what the preview and raw dimensions are.  We
	// call this method even if the preview dimensions have not changed,
	// because the picture ones may have.
	// NOTE: if this errors out, mCameraState != SPRD_IDLE, which will be
	//       checked by the caller of this method.
	if (!setCameraDimensions()) {
		LOGE("initPreview: setCameraDimensions failed");
		return false;
	}

	LOGV("initPreview: preview size=%dx%d", mPreviewWidth, mPreviewHeight);

	camerea_set_preview_format(mPreviewFormat);

	switch (mPreviewFormat) {
	case 0:
		case 1:	//yuv420
		if (mIsDvPreview) {
			mPreviewHeapSize = SIZE_ALIGN(mPreviewWidth) * SIZE_ALIGN(mPreviewHeight) * 3 / 2;
		} else {
			mPreviewHeapSize = mPreviewWidth * mPreviewHeight * 3 / 2;
		}
		break;

	default:
		return false;
	}

	if (!allocatePreviewMem()) {
		freePreviewMem();
		return false;
	}

	if (camera_set_preview_mem((uint32_t)mPreviewHeapArray_phy,
								(uint32_t)mPreviewHeapArray_vir,
								camera_get_size_align_page(mPreviewHeapSize),
								(uint32_t)mPreviewHeapNum))
		return false;

	setPreviewFreq();

	return true;
}

void SprdCameraHardware::deinitPreview()
{
	freePreviewMem();
	camera_set_preview_mem(0, 0, 0, 0);
	restoreFreq();
}

//mJpegHeapSize/mRawHeapSize/mMiscHeapSize must be set before this function being called
bool SprdCameraHardware::allocateCaptureMem(bool initJpegHeap)
{
	uint32_t buffer_size = 0;

	LOGV("allocateCaptureMem, mJpegHeapSize = %d, mRawHeapSize = %d, mMiscHeapSize %d",
			mJpegHeapSize, mRawHeapSize, mMiscHeapSize);

	buffer_size = camera_get_size_align_page(mRawHeapSize);
	LOGV("allocateCaptureMem:mRawHeap align size = %d . count %d ",buffer_size, kRawBufferCount);

	mRawHeap = GetCachePmem(buffer_size, kRawBufferCount);
	if(NULL == mRawHeap)
		goto allocate_capture_mem_failed;

	if(NULL == mRawHeap->handle){
		LOGE("allocateCaptureMem: Fail to GetPmem mRawHeap.");
		goto allocate_capture_mem_failed;
	}

	if (mMiscHeapSize > 0) {
		buffer_size = camera_get_size_align_page(mMiscHeapSize);
		mMiscHeap = GetCachePmem(buffer_size, kRawBufferCount);
		if(NULL == mMiscHeap) {
			goto allocate_capture_mem_failed;
		}

		if(NULL == mMiscHeap->handle){
			LOGE("allocateCaptureMem: Fail to GetPmem mMiscHeap.");
			goto allocate_capture_mem_failed;
		}
	}

	if (initJpegHeap) {
		// ???
		mJpegHeap = NULL;

		buffer_size = camera_get_size_align_page(mJpegHeapSize);
		mJpegHeap = new AshmemPool(buffer_size,
		                           kJpegBufferCount,
		                           0,
		                           0,
		                           "jpeg");

		if (!mJpegHeap->initialized()) {
			LOGE("allocateCaptureMem: initializing mJpegHeap failed.");
			goto allocate_capture_mem_failed;
		}

		LOGV("allocateCaptureMem: initJpeg success");
	}

	LOGV("allocateCaptureMem: X");
	return true;

allocate_capture_mem_failed:
	freeCaptureMem();

	mJpegHeap = NULL;
	mJpegHeapSize = 0;

	return false;
}

void SprdCameraHardware::freeCaptureMem()
{
#if FREE_PMEM_BAK
	mCbCapDataBusyLock.lock();
	if (mRawHeap) {
		if (true == mRawHeap->busy_flag) {
			LOGE("freeCaptureMem, raw buffer is busy, skip, bakup and free later!");
			if (NULL == mRawHeapInfoBak.camera_memory) {
				memcpy(&mRawHeapInfoBak, mRawHeap, sizeof(*mRawHeap));
			} else {
				LOGE("preview buffer not clear, unknown error!!!");
				memcpy(&mRawHeapInfoBak, mRawHeap, sizeof(*mRawHeap));
			}
			mRawHeapBakUseFlag = 1;
			memset(mRawHeap, 0, sizeof(*mRawHeap));
			free(mRawHeap);
		} else {
			FreePmem(mRawHeap);
		}
	}
	mRawHeap = NULL;
	mCbCapDataBusyLock.unlock();
#else
	FreePmem(mRawHeap);
	mRawHeap = NULL;
#endif

    mRawHeapSize = 0;

    if (mMiscHeapSize > 0) {
        FreePmem(mMiscHeap);
        mMiscHeap = NULL;
        mMiscHeapSize = 0;
    } else {
        uint32_t i;
        for (i=0; i<mMiscHeapNum; i++) {
            sp<MemoryHeapIon> pHeapIon = mMiscHeapArray[i];
            if (pHeapIon != NULL) {
                pHeapIon.clear();
            }
            mMiscHeapArray[i] = NULL;
        }
        mMiscHeapNum = 0;
    }
     //mJpegHeap = NULL;
}


// Called with mLock held!
bool SprdCameraHardware::initCapture(bool initJpegHeap)
{
	uint32_t local_width = 0, local_height = 0;
	uint32_t mem_size0 = 0, mem_size1 = 0;

	LOGV("initCapture E, %d", initJpegHeap);

	if(!startCameraIfNecessary())
		return false;

    camera_set_dimensions(mRawWidth,
                         mRawHeight,
                         mPreviewWidth,
                         mPreviewHeight,
                         NULL,
                         NULL,
                         mCaptureMode != CAMERA_RAW_MODE);

	if (camera_capture_max_img_size(&local_width, &local_height))
		return false;

	if (camera_capture_get_buffer_size(mCameraId, local_width, local_height, &mem_size0, &mem_size1))
		return false;

	mRawHeapSize = mem_size0;
	mMiscHeapSize = mem_size1;
	mJpegHeapSize = mRawHeapSize;
	mJpegHeap = NULL;

	if (!allocateCaptureMem(initJpegHeap)) {
		return false;
	}

	if (NULL != mMiscHeap) {
		if (camera_set_capture_mem(0,
								(uint32_t)mRawHeap->phys_addr,
								(uint32_t)mRawHeap->data,
								(uint32_t)mRawHeap->phys_size,
								(uint32_t)mMiscHeap->phys_addr,
								(uint32_t)mMiscHeap->data,
								(uint32_t)mMiscHeap->phys_size))
			return false;
	} else {
		if (camera_set_capture_mem2(0,
								(uint32_t)mRawHeap->phys_addr,
								(uint32_t)mRawHeap->data,
								(uint32_t)mRawHeap->phys_size,
								(uint32_t)Callback_AllocCapturePmem,
								(uint32_t)Callback_FreeCapturePmem,
								(uint32_t)this))
			return false;
	}

	LOGV("initCapture X success");
	return true;
}

void SprdCameraHardware::deinitCapture()
{
    if (NULL != mMiscHeap) {
        camera_set_capture_mem(0, 0, 0, 0, 0, 0, 0);
    }

    freeCaptureMem();
}

void SprdCameraHardware::changeEmcFreq(char flag)
{
	int fp_emc_freq = -1;
	fp_emc_freq = open("/sys/emc/emc_freq", O_WRONLY);

	if (fp_emc_freq >= 0) {
		write(fp_emc_freq, &flag, sizeof(flag));
		close(fp_emc_freq);
		LOGV("changeEmcFreq: %c \n", flag);
	} else {
		LOGV("changeEmcFreq: changeEmcFreq failed \n");
	}
}

void SprdCameraHardware::setPreviewFreq()
{

}

void SprdCameraHardware::restoreFreq()
{

}

void SprdCameraHardware::set_ddr_freq(const char* freq_in_khz)
{
	const char* const set_freq = "/sys/devices/platform/scxx30-dmcfreq.0/devfreq/scxx30-dmcfreq.0/ondemand/set_freq";
	FILE* fp = fopen(set_freq, "wb");
	if (fp != NULL) {
		fprintf(fp, "%s", freq_in_khz);
		ALOGE("set ddr freq to %skhz", freq_in_khz);
		fclose(fp);
	} else {
		ALOGE("Failed to open %s", set_freq);
	}
}

status_t SprdCameraHardware::startPreviewInternal(bool isRecording)
{
	takepicture_mode mode = getCaptureMode();
	LOGV("startPreviewInternal E isRecording=%d.captureMode=%d",isRecording, mCaptureMode);

	if (isPreviewing()) {
		LOGE("startPreviewInternal: already in progress, doing nothing.X");
		setRecordingMode(isRecording);
		setCameraPreviewMode(isRecordingMode());
		return NO_ERROR;
	}
	//to do it
	if (!iSZslMode()) {
		// We check for these two states explicitly because it is possible
		// for startPreview() to be called in response to a raw or JPEG
		// callback, but before we've updated the state from SPRD_WAITING_RAW
		// or SPRD_WAITING_JPEG to SPRD_IDLE.  This is because in camera_cb(),
		// we update the state *after* we've made the callback.  See that
		// function for an explanation.
		if (isCapturing()) {
			WaitForCaptureDone();
		}

		if (isCapturing() || isPreviewing()) {
			LOGE("startPreviewInternal X Capture state is %s, Preview state is %s, expecting SPRD_IDLE!",
			getCameraStateStr(mCameraState.capture_state), getCameraStateStr(mCameraState.preview_state));
			return INVALID_OPERATION;
		}
	}
	setRecordingMode(isRecording);

#if FREE_PMEM_BAK
	cameraBakMemCheckAndFree();
#endif
	if (!initPreview()) {
		LOGE("startPreviewInternal X initPreview failed.  Not starting preview.");
		deinitPreview();
		return UNKNOWN_ERROR;
	}
	if (iSZslMode()) {
		set_ddr_freq("500000");
		mSetFreqCount++;
		if (!initCapture(mData_cb != NULL)) {
			deinitCapture();
			set_ddr_freq("0");
			mSetFreqCount--;
			LOGE("startPreviewInternal X initCapture failed. Not taking picture.");
			return UNKNOWN_ERROR;
		}
	}

	setCameraState(SPRD_INTERNAL_PREVIEW_REQUESTED, STATE_PREVIEW);
	camera_ret_code_type qret = camera_start_preview(camera_cb, this,mode);
	LOGV("startPreviewInternal X");
	if (qret != CAMERA_SUCCESS) {
		LOGE("startPreviewInternal failed: sensor error.");
		setCameraState(SPRD_ERROR, STATE_PREVIEW);
		deinitPreview();
		if (iSZslMode()) {
			set_ddr_freq("0");
			mSetFreqCount--;
		}
		return UNKNOWN_ERROR;
	}

	bool result = WaitForPreviewStart();

	LOGV("startPreviewInternal X,mRecordingMode=%d.",isRecordingMode());

	return result ? NO_ERROR : UNKNOWN_ERROR;
}

void SprdCameraHardware::stopPreviewInternal()
{
	nsecs_t start_timestamp = systemTime();
	nsecs_t end_timestamp;
	LOGV("stopPreviewInternal E");

	if (!isPreviewing()) {
		LOGE("Preview not in progress! stopPreviewInternal X");
		return;
	}

	setCameraState(SPRD_INTERNAL_PREVIEW_STOPPING, STATE_PREVIEW);

	if (isCapturing()) {
		cancelPictureInternal();
	}

	if(CAMERA_SUCCESS != camera_stop_preview()) {
		setCameraState(SPRD_ERROR, STATE_PREVIEW);
		LOGE("stopPreviewInternal X: fail to camera_stop_preview().");
	}

	if (iSZslMode()) {
		while (0 < mSetFreqCount) {
			set_ddr_freq("0");
			mSetFreqCount--;
		}
	}

	WaitForPreviewStop();
#if FREE_PMEM_BAK
	cameraBakMemCheckAndFree();
#endif
	deinitPreview();


#if FREE_PMEM_BAK
	if (iSZslMode()) {
		deinitCapture();
	}
#else
	if (isCapturing()) {
		WaitForCaptureDone();
	}
	deinitCapture();
#endif
	end_timestamp = systemTime();
	LOGV("stopPreviewInternal X Time:%lld(ms).",(end_timestamp - start_timestamp)/1000000);
	LOGV("stopPreviewInternal X Preview has stopped.");
}

takepicture_mode SprdCameraHardware::getCaptureMode()
{
	Mutex::Autolock          paramLock(&mParamLock);
	if (1 == mParameters.getInt("hdr")) {
        mCaptureMode = CAMERA_HDR_MODE;
    } else if ((1 == mParameters.getInt("zsl"))&&(1 != mParameters.getInt("capture-mode"))) {
		mCaptureMode = CAMERA_ZSL_CONTINUE_SHOT_MODE;
    } else if ((1 != mParameters.getInt("zsl"))&&(1 != mParameters.getInt("capture-mode"))) {
		mCaptureMode = CAMERA_NORMAL_CONTINUE_SHOT_MODE;
    } else if (1 == mParameters.getInt("zsl")) {
		mCaptureMode = CAMERA_ZSL_MODE;
    } else if (1 != mParameters.getInt("zsl")) {
		mCaptureMode = CAMERA_NORMAL_MODE;
    } else {
		mCaptureMode = CAMERA_NORMAL_MODE;
    }
	if (1 == mCaptureRawMode) {
		mCaptureMode = CAMERA_RAW_MODE;
	}
/*	mCaptureMode = CAMERA_HDR_MODE;*/
	LOGI("cap mode %d.\n", mCaptureMode);

	return mCaptureMode;
}

bool SprdCameraHardware::iSDisplayCaptureFrame()
{
	bool ret = true;

	if ((CAMERA_ZSL_MODE == mCaptureMode)
		|| (CAMERA_ZSL_CONTINUE_SHOT_MODE == mCaptureMode)) {
		ret = false;
	}
	LOGI("display capture flag is %d.",ret);
	return ret;
}

bool SprdCameraHardware::iSZslMode()
{
	bool ret = true;
	if ((CAMERA_ZSL_MODE != mCaptureMode)
		&& (CAMERA_ZSL_CONTINUE_SHOT_MODE != mCaptureMode)) {
		ret = false;
	}
	return ret;
}


status_t SprdCameraHardware::cancelPictureInternal()
{
	bool result = true;

	LOGV("cancelPictureInternal: E, state = %s", getCameraStateStr(getCaptureState()));

	switch (getCaptureState()) {
	case SPRD_INTERNAL_RAW_REQUESTED:
	case SPRD_WAITING_RAW:
	case SPRD_WAITING_JPEG:
		LOGD("camera state is %s, stopping picture.", getCameraStateStr(getCaptureState()));
		setCameraState(SPRD_INTERNAL_CAPTURE_STOPPING, STATE_CAPTURE);
		if (0 != camera_stop_capture()) {
			LOGE("cancelPictureInternal: camera_stop_capture failed!");
			return UNKNOWN_ERROR;
		}

		result = WaitForCaptureDone();
		break;

	default:
		LOGV("not taking a picture (state %s)", getCameraStateStr(getCaptureState()));
		break;
	}

	LOGV("cancelPictureInternal: X");
	return result ? NO_ERROR : UNKNOWN_ERROR;
}

status_t SprdCameraHardware::initDefaultParameters()
{
	uint32_t lcd_w = 0, lcd_h = 0;

	LOGV("initDefaultParameters E");
	SprdCameraParameters p;

	SprdCameraParameters::ConfigType config = (1 == mCameraId)
			? SprdCameraParameters::kFrontCameraConfig
			: SprdCameraParameters::kBackCameraConfig;

	p.setDefault(config);

	if (getLcdSize(&lcd_w, &lcd_h))
	{
		/*update preivew size by lcd*/
		p.updateSupportedPreviewSizes(lcd_w, lcd_h);
	}

	if (setParametersInternal(p) != NO_ERROR) {
		LOGE("Failed to set default parameters?!");
		return UNKNOWN_ERROR;
    }

    LOGV("initDefaultParameters X.");
    return NO_ERROR;
}

bool SprdCameraHardware::getLcdSize(uint32_t *width, uint32_t *height)
{
    char const * const device_template[] = {
        "/dev/graphics/fb%u",
        "/dev/fb%u",
        NULL
    };

    int fd = -1;
    int i = 0;
    char name[64];

    if (NULL == width || NULL == height)
        return false;

    while ((fd == -1) && device_template[i]) {
        snprintf(name, 64, device_template[i], 0);
        fd = open(name, O_RDONLY, 0);
        i++;
    }
	LOGV("getLcdSize dev is %s", name);

    if (fd < 0) {
        LOGE("getLcdSize fail to open fb");
        return false;
    }

    struct fb_var_screeninfo info;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &info) == -1) {
        LOGE("getLcdSize fail to get fb info");
		close(fd);
        return false;
    }

	LOGV("getLcdSize w h %d %d", info.yres, info.xres);
    *width  = info.yres;
    *height = info.xres;

	close(fd);
	return true;
}


status_t SprdCameraHardware::setCameraParameters()
{
	LOGV("setCameraParameters: E");

	// Because libqcamera is broken, for the camera_set_parm() calls
	// SprdCameraHardware camera_cb() is called synchronously,
	// so we cannot wait on a state change.  Also, we have to unlock
	// the mStateLock, because camera_cb() acquires it.

	if(true != startCameraIfNecessary())
	    return UNKNOWN_ERROR;

	//wxz20120316: check the value of preview-fps-range. //CR168435
	int min,max;
	mParameters.getPreviewFpsRange(&min, &max);
	if((min > max) || (min < 0) || (max < 0)){
		LOGE("Error to FPS range: mix: %d, max: %d.", min, max);
		return UNKNOWN_ERROR;
	}
	LOGV("setCameraParameters: preview fps range: min = %d, max = %d", min, max);

	//check preview size
	int w,h;
	mParameters.getPreviewSize(&w, &h);
	if((w < 0) || (h < 0)){
		mParameters.setPreviewSize(640, 480);
		return UNKNOWN_ERROR;
	}
	LOGV("setCameraParameters: preview size: %dx%d", w, h);

	LOGV("mIsRotCapture:%d.",mIsRotCapture);
	if (mIsRotCapture) {
		SET_PARM(CAMERA_PARAM_ROTATION_CAPTURE, 1);
	} else {
		SET_PARM(CAMERA_PARAM_ROTATION_CAPTURE, 0);
	}

	// Default Rotation - none
	int rotation = mParameters.getInt("rotation");

    // Rotation may be negative, but may not be -1, because it has to be a
    // multiple of 90.  That's why we can still interpret -1 as an error,
    if (rotation == -1) {
        LOGV("rotation not specified or is invalid, defaulting to 0");
        rotation = 0;
    }
    else if (rotation % 90) {
        LOGV("rotation %d is not a multiple of 90 degrees!  Defaulting to zero.",
             rotation);
        rotation = 0;
    }
    else {
        // normalize to [0 - 270] degrees
        rotation %= 360;
        if (rotation < 0) rotation += 360;
    }

    SET_PARM(CAMERA_PARM_ENCODE_ROTATION, rotation);
    if (1 == mParameters.getInt("sensororientation")){
    	SET_PARM(CAMERA_PARM_ORIENTATION, 1); //for portrait
	}
	else {
    	SET_PARM(CAMERA_PARM_ORIENTATION, 0); //for landscape
	}

    rotation = mParameters.getInt("sensorrotation");
    if (-1 == rotation)
        rotation = 0;

    SET_PARM(CAMERA_PARM_SENSOR_ROTATION, rotation);
    if (0 != rotation) {
        mPreviewBufferUsage = PREVIEW_BUFFER_USAGE_DCAM;
    }

    SET_PARM(CAMERA_PARM_SHOT_NUM, mParameters.getInt("capture-mode"));
/*	if (1 == mParameters.getInt("hdr")) {
		SET_PARM(CAMERA_PARM_SHOT_NUM, HDR_CAP_NUM);
	}*/

	int is_mirror = (mCameraId == 1) ? 1 : 0;
	int ret = 0;

	SprdCameraParameters::Size preview_size = {0, 0};
	SprdCameraParameters::Rect preview_rect = {0, 0, 0, 0};
	int area[4 * SprdCameraParameters::kFocusZoneMax + 1] = {0};

	preview_size.width = mPreviewWidth;
	preview_size.height = mPreviewHeight;

	ret = camera_get_preview_rect(&preview_rect.x, &preview_rect.y,
								&preview_rect.width, &preview_rect.height);
	if(ret) {
		LOGV("coordinate_convert: camera_get_preview_rect failed, return \n");
		return UNKNOWN_ERROR;
	}

	mParameters.getFocusAreas(&area[1], &area[0], &preview_size, &preview_rect,
					kCameraInfo[mCameraId].orientation, is_mirror);
	SET_PARM(CAMERA_PARM_FOCUS_RECT, (int32_t)area);

	int ae_mode = mParameters.getAutoExposureMode();
	if (2 == ae_mode) { // CAMERA_AE_SPOT_METERING
		mParameters.getMeteringAreas(&area[1], &area[0], &preview_size, &preview_rect,
						kCameraInfo[mCameraId].orientation, is_mirror);
		SET_PARM(CAMERA_PARM_EXPOSURE_METERING, (int32_t)area);
	} else {
		SET_PARM(CAMERA_PARM_AUTO_EXPOSURE_MODE, ae_mode);
	}


	if (0 == mCameraId) {
		SET_PARM(CAMERA_PARM_AF_MODE, mParameters.getFocusMode());
#ifndef CONFIG_CAMERA_788
		SET_PARM(CAMERA_PARM_FLASH, mParameters.getFlashMode());
#endif
	}

/*  SET_PARM(CAMERA_PARAM_SLOWMOTION, mParameters.getSlowmotion());*/
	mTimeCoeff = mParameters.getSlowmotion();
	LOGI("mTimeCoeff:%d",mTimeCoeff);
	SET_PARM(CAMERA_PARM_WB, mParameters.getWhiteBalance());
	SET_PARM(CAMERA_PARM_CAMERA_ID, mParameters.getCameraId());
	SET_PARM(CAMERA_PARM_JPEGCOMP, mParameters.getJpegQuality());
	SET_PARM(CAMERA_PARM_THUMBCOMP, mParameters.getJpegThumbnailQuality());
	SET_PARM(CAMERA_PARM_EFFECT, mParameters.getEffect());
	SET_PARM(CAMERA_PARM_SCENE_MODE, mParameters.getSceneMode());

	mZoomLevel = mParameters.getZoom();
	SET_PARM(CAMERA_PARM_ZOOM, mZoomLevel);
	SET_PARM(CAMERA_PARM_BRIGHTNESS, mParameters.getBrightness());
	SET_PARM(CAMERA_PARM_SHARPNESS, mParameters.getSharpness());
	SET_PARM(CAMERA_PARM_CONTRAST, mParameters.getContrast());
	SET_PARM(CAMERA_PARM_SATURATION, mParameters.getSaturation());
	SET_PARM(CAMERA_PARM_EXPOSURE_COMPENSATION, mParameters.getExposureCompensation());
	SET_PARM(CAMERA_PARM_ANTIBANDING, mParameters.getAntiBanding());
	SET_PARM(CAMERA_PARM_ISO, mParameters.getIso());
	SET_PARM(CAMERA_PARM_DCDV_MODE, mParameters.getRecordingHint());

	int ns_mode = mParameters.getInt("nightshot-mode");
	if (ns_mode < 0) ns_mode = 0;
	SET_PARM(CAMERA_PARM_NIGHTSHOT_MODE, ns_mode);

	int luma_adaptation = mParameters.getInt("luma-adaptation");
	if (luma_adaptation < 0) luma_adaptation = 0;
	SET_PARM(CAMERA_PARM_LUMA_ADAPTATION, luma_adaptation);

	double focal_len = atof(mParameters.get("focal-length")) * 1000;
	SET_PARM(CAMERA_PARM_FOCAL_LENGTH,  (int32_t)focal_len);

	int th_w, th_h, th_q;
	th_w = mParameters.getInt("jpeg-thumbnail-width");
	if (th_w < 0) LOGW("property jpeg-thumbnail-width not specified");

	th_h = mParameters.getInt("jpeg-thumbnail-height");
	if (th_h < 0) LOGW("property jpeg-thumbnail-height not specified");

	th_q = mParameters.getInt("jpeg-thumbnail-quality");
	if (th_q < 0) LOGW("property jpeg-thumbnail-quality not specified");

	if (th_w >= 0 && th_h >= 0 && th_q >= 0) {
		LOGI("setting thumbnail dimensions to %dx%d, quality %d", th_w, th_h, th_q);

		int ret = camera_set_thumbnail_properties(th_w, th_h, th_q);

		if (ret != CAMERA_SUCCESS) {
			LOGE("camera_set_thumbnail_properties returned %d", ret);
			}
	}

	camera_encode_properties_type encode_properties = {0, CAMERA_JPEG, 0};
	// Set Default JPEG encoding--this does not cause a callback
	encode_properties.quality = mParameters.getInt("jpeg-quality");

	if (encode_properties.quality < 0) {
		LOGW("JPEG-image quality is not specified "
		"or is negative, defaulting to %d",
		encode_properties.quality);
		encode_properties.quality = 100;
	}
	else LOGV("Setting JPEG-image quality to %d",
			encode_properties.quality);

	encode_properties.format = CAMERA_JPEG;
	encode_properties.file_size = 0x0;
	camera_set_encode_properties(&encode_properties);

	LOGV("setCameraParameters: X");
	return NO_ERROR;
}

void SprdCameraHardware::getPictureFormat(int * format)
{
	*format = mPictureFormat;
}

////////////////////////////////////////////////////////////////////////////
// message handler
/////////////////////////////////////////////////////////////////////////////
#define PARSE_LOCATION(what,type,fmt,desc) do {                                           \
                pt->what = 0;                                                              \
                const char *what##_str = mParameters.get("gps-"#what);                    \
                LOGV("%s: GPS PARM %s --> [%s]", __func__, "gps-"#what, what##_str); \
                if (what##_str) {                                                         \
                    type what = 0;                                                        \
                    if (sscanf(what##_str, fmt, &what) == 1)                              \
                        pt->what = what;                                                   \
                    else {                                                                \
                        LOGE("GPS " #what " %s could not"                                 \
                              " be parsed as a " #desc,                                   \
                              what##_str);                                                \
                        result = false;                                          \
                    }                                                                     \
                }                                                                         \
                else {                                                                    \
                    LOGW("%s: GPS " #what " not specified: "               \
                          "defaulting to zero in EXIF header.", __func__);                          \
                    result = false;                                              \
               }                                                                          \
            }while(0)

bool SprdCameraHardware::getCameraLocation(camera_position_type *pt)
{
	bool result = true;
	mParamLock.lock();
	mUseParameters = mParameters;
	mParamLock.unlock();

	PARSE_LOCATION(timestamp, long, "%ld", "long");
	if (0 == pt->timestamp)
		pt->timestamp = time(NULL);

	PARSE_LOCATION(altitude, double, "%lf", "double float");
	PARSE_LOCATION(latitude, double, "%lf", "double float");
	PARSE_LOCATION(longitude, double, "%lf", "double float");

	pt->process_method = mUseParameters.get("gps-processing-method");
/*
	LOGV("%s: setting image location result %d,  ALT %lf LAT %lf LON %lf",
			__func__, result, pt->altitude, pt->latitude, pt->longitude);
*/
	return result;
}

int SprdCameraHardware::uv420CopyTrim(struct _dma_copy_cfg_tag dma_copy_cfg)
{
	uint32_t i = 0;
	uint32_t src_y_addr = 0, src_uv_addr = 0,  dst_y_addr = 0,  dst_uv_addr = 0;

	if (DMA_COPY_YUV400 <= dma_copy_cfg.format ||
		(dma_copy_cfg.src_size.w & 0x01) || (dma_copy_cfg.src_size.h & 0x01) ||
		(dma_copy_cfg.src_rec.x & 0x01) || (dma_copy_cfg.src_rec.y & 0x01) ||
		(dma_copy_cfg.src_rec.w & 0x01) || (dma_copy_cfg.src_rec.h & 0x01) ||
		0 == dma_copy_cfg.src_addr.y_addr || 0 == dma_copy_cfg.src_addr.uv_addr ||
		0 == dma_copy_cfg.dst_addr.y_addr || 0 == dma_copy_cfg.dst_addr.uv_addr ||
		0 == dma_copy_cfg.src_rec.w || 0 == dma_copy_cfg.src_rec.h ||
		0 == dma_copy_cfg.src_size.w|| 0 == dma_copy_cfg.src_size.h ||
		(dma_copy_cfg.src_rec.x + dma_copy_cfg.src_rec.w > dma_copy_cfg.src_size.w) ||
		(dma_copy_cfg.src_rec.y + dma_copy_cfg.src_rec.h > dma_copy_cfg.src_size.h)) {
		LOGE("uv420CopyTrim: param is error. \n");
		return -1;
	}

	src_y_addr = dma_copy_cfg.src_addr.y_addr + dma_copy_cfg.src_rec.y *
				dma_copy_cfg.src_size.w + dma_copy_cfg.src_rec.x;
	src_uv_addr = dma_copy_cfg.src_addr.uv_addr + ((dma_copy_cfg.src_rec.y * dma_copy_cfg.src_size.w) >> 1) +
				dma_copy_cfg.src_rec.x;
	dst_y_addr = dma_copy_cfg.dst_addr.y_addr;
	dst_uv_addr = dma_copy_cfg.dst_addr.uv_addr;

	for (i = 0; i < dma_copy_cfg.src_rec.h; i++) {
		memcpy((void *)dst_y_addr, (void *)src_y_addr, dma_copy_cfg.src_rec.w);
		src_y_addr += dma_copy_cfg.src_size.w;
		dst_y_addr += dma_copy_cfg.src_rec.w;

		if (0 == (i & 0x01)) {
			memcpy((void *)dst_uv_addr, (void *)src_uv_addr, dma_copy_cfg.src_rec.w);
			src_uv_addr += dma_copy_cfg.src_size.w;
			dst_uv_addr += dma_copy_cfg.src_rec.w;
		}
	}

	return 0;
}

int SprdCameraHardware::displayCopy(uint32_t dst_phy_addr, uint32_t dst_virtual_addr,
								uint32_t src_phy_addr, uint32_t src_virtual_addr, uint32_t src_w, uint32_t src_h)
{
	int ret = 0;
	struct _dma_copy_cfg_tag dma_copy_cfg;

	mParamLock.lock();
	mUseParameters = mParameters;
	mParamLock.unlock();

#ifdef CONFIG_CAMERA_DMA_COPY
	dma_copy_cfg.format = DMA_COPY_YUV420;
	dma_copy_cfg.src_size.w = src_w;
	dma_copy_cfg.src_size.h = src_h;
	dma_copy_cfg.src_rec.x = mPreviewWidth_trimx;
	dma_copy_cfg.src_rec.y = mPreviewHeight_trimy;
	dma_copy_cfg.src_rec.w = mPreviewWidth_backup;
	dma_copy_cfg.src_rec.h = mPreviewHeight_backup;
	dma_copy_cfg.src_addr.y_addr = src_phy_addr;
	dma_copy_cfg.src_addr.uv_addr = src_phy_addr + dma_copy_cfg.src_size.w * dma_copy_cfg.src_size.h;
	dma_copy_cfg.dst_addr.y_addr = dst_phy_addr;
	if ((0 == dma_copy_cfg.src_rec.x) && (0 == dma_copy_cfg.src_rec.y) &&
		(dma_copy_cfg.src_size.w == dma_copy_cfg.src_rec.w) &&
		(dma_copy_cfg.src_size.h == dma_copy_cfg.src_rec.h)) {
		dma_copy_cfg.dst_addr.uv_addr = dst_phy_addr + dma_copy_cfg.src_size.w * dma_copy_cfg.src_size.h ;
	} else {
		dma_copy_cfg.dst_addr.uv_addr = dst_phy_addr + dma_copy_cfg.src_rec.w * dma_copy_cfg.src_rec.h ;
	}
	ret = camera_dma_copy_data(dma_copy_cfg);
#else

#ifdef CONFIG_CAMERA_ANTI_SHAKE
	dma_copy_cfg.format = DMA_COPY_YUV420;
	dma_copy_cfg.src_size.w = src_w;
	dma_copy_cfg.src_size.h = src_h;
	dma_copy_cfg.src_rec.x = mPreviewWidth_trimx;
	dma_copy_cfg.src_rec.y = mPreviewHeight_trimy;
	dma_copy_cfg.src_rec.w = mPreviewWidth_backup;
	dma_copy_cfg.src_rec.h = mPreviewHeight_backup;
	dma_copy_cfg.src_addr.y_addr = src_virtual_addr;
	dma_copy_cfg.src_addr.uv_addr = src_virtual_addr + dma_copy_cfg.src_size.w * dma_copy_cfg.src_size.h;
	dma_copy_cfg.dst_addr.y_addr = dst_virtual_addr;
	if ((0 == dma_copy_cfg.src_rec.x) && (0 == dma_copy_cfg.src_rec.y) &&
		(dma_copy_cfg.src_size.w == dma_copy_cfg.src_rec.w) &&
		(dma_copy_cfg.src_size.h == dma_copy_cfg.src_rec.h)) {
		dma_copy_cfg.dst_addr.uv_addr = dst_virtual_addr + dma_copy_cfg.src_size.w * dma_copy_cfg.src_size.h ;
	} else {
		dma_copy_cfg.dst_addr.uv_addr = dst_virtual_addr + dma_copy_cfg.src_rec.w * dma_copy_cfg.src_rec.h ;
	}
	ret = uv420CopyTrim(dma_copy_cfg);
#else
    if (mIsDvPreview) {
		memcpy((void *)dst_virtual_addr, (void *)src_virtual_addr, SIZE_ALIGN(src_w)*SIZE_ALIGN(src_h)*3/2);
    } else {
		memcpy((void *)dst_virtual_addr, (void *)src_virtual_addr, src_w*src_h*3/2);
    }
#endif

#endif
	return ret;
}

bool SprdCameraHardware::displayOneFrameForCapture(uint32_t width, uint32_t height, uint32_t phy_addr, char *virtual_addr)
{
	if (!mPreviewWindow || !mGrallocHal || 0 == phy_addr) {
		return false;
	}

	LOGV("%s: size = %dx%d, addr = %d", __func__, width, height, phy_addr);

	buffer_handle_t 	*buf_handle = NULL;
	int 				stride = 0;
	void 				*vaddr = NULL;
	int					ret = 0;
	struct _dma_copy_cfg_tag dma_copy_cfg;
	struct private_handle_t *private_h = NULL;
	uint32_t dst_phy_addr = 0;

	ret = mPreviewWindow->dequeue_buffer(mPreviewWindow, &buf_handle, &stride);
	if (0 != ret) {
		LOGE("%s: failed to dequeue gralloc buffer!", __func__);
		return false;
	}

	ret = mGrallocHal->lock(mGrallocHal, *buf_handle, GRALLOC_USAGE_SW_WRITE_OFTEN,
							0, 0, SIZE_ALIGN(width), SIZE_ALIGN(height), &vaddr);

	if (0 != ret || NULL == vaddr) {
		LOGE("%s: failed to lock gralloc buffer", __func__);
		return false;
	}

	private_h = (struct private_handle_t *)(*buf_handle);
	dst_phy_addr =  (uint32_t)(private_h->phyaddr);
	LOGV("displayOneFrameForCapture,0x%x.",(uint32_t)virtual_addr);
	ret = displayCopy(dst_phy_addr, (uint32_t)vaddr, phy_addr, (uint32_t)virtual_addr, width, height);

	mGrallocHal->unlock(mGrallocHal, *buf_handle);

	if (0 != ret) {
		LOGE("%s: camera copy data failed.", __func__);
		return false;
	}

	ret = mPreviewWindow->enqueue_buffer(mPreviewWindow, buf_handle);
	if (0 != ret) {
		LOGE("%s: enqueue_buffer() failed.", __func__);
		return false;
	}

	return true;
}


bool SprdCameraHardware::displayOneFrame(uint32_t width, uint32_t height, uint32_t phy_addr, char *virtual_addr, uint32_t id)
{
	mParamLock.lock();
	mUseParameters = mParameters;
	mParamLock.unlock();

	if (PREVIEW_BUFFER_USAGE_DCAM == mPreviewBufferUsage) {
		if (!mPreviewWindow || !mGrallocHal || 0 == phy_addr) {
			return false;
		}

		LOGV("%s: size = %dx%d, addr = %d", __func__, width, height, phy_addr);

		buffer_handle_t 	*buf_handle = NULL;
		int 				stride = 0;
		void 				*vaddr = NULL;
		int					ret = 0;
		struct _dma_copy_cfg_tag dma_copy_cfg;
		struct private_handle_t *private_h = NULL;
		uint32_t dst_phy_addr = 0;

		ret = mPreviewWindow->dequeue_buffer(mPreviewWindow, &buf_handle, &stride);
		if (0 != ret) {
			LOGE("%s: failed to dequeue gralloc buffer!", __func__);
			return false;
		}
		if (mIsDvPreview) {
			ret = mGrallocHal->lock(mGrallocHal, *buf_handle, GRALLOC_USAGE_SW_WRITE_OFTEN,
									0, 0, SIZE_ALIGN(width), SIZE_ALIGN(height), &vaddr);
		} else {
			ret = mGrallocHal->lock(mGrallocHal, *buf_handle, GRALLOC_USAGE_SW_WRITE_OFTEN,
									0, 0, width, height, &vaddr);
		}

		if (0 != ret || NULL == vaddr) {
			LOGE("%s: failed to lock gralloc buffer", __func__);
			return false;
		}

		private_h = (struct private_handle_t *)(*buf_handle);
		dst_phy_addr =  (uint32_t)(private_h->phyaddr);
		ret = displayCopy(dst_phy_addr, (uint32_t)vaddr, phy_addr, (uint32_t)virtual_addr, width, height);

		mGrallocHal->unlock(mGrallocHal, *buf_handle);

		if (0 != ret) {
			LOGE("%s: camera copy data failed.", __func__);
			return false;
		}

		ret = mPreviewWindow->enqueue_buffer(mPreviewWindow, buf_handle);
		if (0 != ret) {
			LOGE("%s: enqueue_buffer() failed.", __func__);
			return false;
		}
	} else {
		if (mCancelBufferEb && (mPreviewCancelBufHandle[id] == mPreviewBufferHandle[id])) {
			LOGE("displayOneFrame fail: Could not enqueue cancel buffer!\n");
			camera_release_frame(id);
			return true;
		} else {
			if (0 != mPreviewWindow->enqueue_buffer(mPreviewWindow, mPreviewBufferHandle[id])) {
				LOGE("displayOneFrame fail: Could not enqueue gralloc buffer!\n");
				return false;
			}
			mCancelBufferEb = 0;
		}

		if (!isRecordingMode()) {
			if (releasePreviewFrame())
				return false;
		}

	}
	return true;
}

void SprdCameraHardware::receivePreviewFDFrame(camera_frame_type *frame)
{
    Mutex::Autolock cbLock(&mPreviewCbLock);

	if (NULL == frame) {
		LOGE("receivePreviewFDFrame: invalid frame pointer");
		return;
	}

    ssize_t offset = frame->buf_id;
    camera_frame_metadata_t metadata;
    camera_face_t face_info[FACE_DETECT_NUM];
    int32_t k = 0;

   /* if(1 == mParameters.getInt("smile-snap-mode")){*/
    LOGV("receive face_num %d.",frame->face_num);
   metadata.number_of_faces = frame->face_num <= FACE_DETECT_NUM ? frame->face_num:FACE_DETECT_NUM;
    if(0 != metadata.number_of_faces) {
       for(k=0 ; k< metadata.number_of_faces ; k++) {
            face_info[k].id = k;
            face_info[k].rect[0] = (frame->face_ptr->sx*2000/mPreviewWidth)-1000;
            face_info[k].rect[1] = (frame->face_ptr->sy*2000/mPreviewHeight)-1000;
            face_info[k].rect[2] = (frame->face_ptr->ex*2000/mPreviewWidth)-1000;
            face_info[k].rect[3] = (frame->face_ptr->ey*2000/mPreviewHeight)-1000;
            LOGV("smile level %d.\n",frame->face_ptr->smile_level);

            face_info[k].score = frame->face_ptr->smile_level;
            frame->face_ptr++;
       }
    }

    metadata.faces = &face_info[0];
    if(mMsgEnabled&CAMERA_MSG_PREVIEW_METADATA) {
        LOGV("smile capture msg is enabled.");
		if (PREVIEW_BUFFER_USAGE_DCAM == mPreviewBufferUsage) {
			uint32_t tmpIndex = offset;
			if(camera_get_rot_set()) {
				tmpIndex += kPreviewBufferCount;
			}

#if FREE_PMEM_BAK
			handleDataCallback(CAMERA_MSG_PREVIEW_METADATA,
				mPreviewHeapArray[tmpIndex],
				0, &metadata, mUser, 1);
#else
			mData_cb(CAMERA_MSG_PREVIEW_METADATA,
				mPreviewHeapArray[tmpIndex]->camera_memory,
				0,&metadata,mUser);
#endif
		} else {
			uint32_t dataSize = frame->dx * frame->dy * 3 / 2;
			memcpy(mPreviewHeapArray[mPreviewDcamAllocBufferCnt -1]->camera_memory->data,
				frame->buf_Virt_Addr, dataSize);
#if FREE_PMEM_BAK
			handleDataCallback(CAMERA_MSG_PREVIEW_METADATA,
				mPreviewHeapArray[mPreviewDcamAllocBufferCnt -1],
				0, &metadata, mUser, 1);
#else
			mData_cb(CAMERA_MSG_PREVIEW_METADATA,
				mPreviewHeapArray[mPreviewDcamAllocBufferCnt -1]->camera_memory,
				0,&metadata,mUser);
#endif
		}
	} else {
		LOGV("smile capture msg is disabled.");
	}
}

void SprdCameraHardware::handleDataCallback(int32_t msg_type,
		sprd_camera_memory_t *data, unsigned int index,
		camera_frame_metadata_t *metadata, void *user,
		uint32_t isPrev)
{
	if (isPrev) {
		Mutex::Autolock l(&mCbPrevDataBusyLock);
	} else {
		Mutex::Autolock cl(&mCbCapDataBusyLock);
	}
	LOGV("handleDataCallback E");
	data->busy_flag = true;
	mData_cb(msg_type, data->camera_memory, index, metadata, user);
	data->busy_flag = false;
	LOGV("handleDataCallback X");
}


void SprdCameraHardware::handleDataCallbackTimestamp(int64_t timestamp,
		int32_t msg_type,
		sprd_camera_memory_t *data, unsigned int index,
		void *user)
{
	Mutex::Autolock l(&mCbPrevDataBusyLock);
	LOGV("handleDataCallbackTimestamp E");
	data->busy_flag = true;
	mData_cb_timestamp(timestamp, msg_type, data->camera_memory, index, user);
	data->busy_flag = false;
	LOGV("handleDataCallbackTimestamp X");
}

void SprdCameraHardware::cameraBakMemCheckAndFree()
{
	LOGV("cameraBakMemCheckkAndFree E");
	if(NO_ERROR == mCbPrevDataBusyLock.tryLock()) {
		/*preview bak heap check and free*/
		if ((false == mPreviewHeapInfoBak.busy_flag) &&
			(1 == mPreviewHeapBakUseFlag)) {
			LOGV("cameraBakMemCheckkAndFree free prev bak mem");
			clearPmem(&mPreviewHeapInfoBak);
			mPreviewHeapBakUseFlag = 0;
			LOGV("cameraBakMemCheckkAndFree previewHeapBak free OK");
		}
		mCbPrevDataBusyLock.unlock();
	}
	LOGV("cameraBakMemCheckkAndFree prev pass");

	if(NO_ERROR == mCbCapDataBusyLock.tryLock()) {
		/* capture head check and free*/
		if ((false == mRawHeapInfoBak.busy_flag) &&
			(1 == mRawHeapBakUseFlag)) {
			LOGV("cameraBakMemCheckkAndFree free cap bak mem");
			clearPmem(&mRawHeapInfoBak);
			mRawHeapBakUseFlag = 0;
			LOGV("cameraBakMemCheckkAndFree rawHeapBak free OK");
		}
		mCbCapDataBusyLock.unlock();
	}

	LOGV("cameraBakMemCheckkAndFree X");
}

void SprdCameraHardware::receivePreviewFrame(camera_frame_type *frame)
{
	Mutex::Autolock cbLock(&mPreviewCbLock);

	if (NULL == frame) {
		LOGE("receivePreviewFrame: invalid frame pointer");
		return;
	}

	ssize_t offset = frame->buf_id;
	camera_frame_metadata_t metadata;
	camera_face_t face_info[FACE_DETECT_NUM];
	uint32_t k = 0;

	int width, height, frame_size, offset_size;

	width = frame->dx;/*mPreviewWidth;*/
	height = frame->dy;/*mPreviewHeight;*/
	LOGV("receivePreviewFrame: width=%d, height=%d \n",width, height);
	if (miSPreviewFirstFrame) {
		GET_END_TIME;
		GET_USE_TIME;
		LOGE("Launch Camera Time:%d(ms).",s_use_time);
		miSPreviewFirstFrame = 0;
	}

	if (!displayOneFrame(width, height, frame->buffer_phy_addr, (char *)frame->buf_Virt_Addr, frame->buf_id)) {
		LOGE("%s: displayOneFrame failed!", __func__);
	}

#ifdef CONFIG_CAMERA_ISP
	send_img_data(2, mPreviewWidth, mPreviewHeight, (char *)frame->buf_Virt_Addr, frame->dx * frame->dy * 3 /2);
#endif

	if(mData_cb != NULL)
	{
		LOGV("receivePreviewFrame mMsgEnabled: 0x%x",mMsgEnabled);
		if (mMsgEnabled & CAMERA_MSG_PREVIEW_FRAME) {
			if (PREVIEW_BUFFER_USAGE_DCAM == mPreviewBufferUsage) {
				uint32_t tmpIndex = offset;
				if(camera_get_rot_set()) {
					tmpIndex += kPreviewBufferCount;
				}
#if FREE_PMEM_BAK
				handleDataCallback(CAMERA_MSG_PREVIEW_FRAME,
					mPreviewHeapArray[tmpIndex],
					0, NULL, mUser, 1);
#else
				mData_cb(CAMERA_MSG_PREVIEW_FRAME,
					mPreviewHeapArray[tmpIndex]->camera_memory,
					0,NULL,mUser);
#endif
			} else {
				uint32_t dataSize = frame->dx * frame->dy * 3 / 2;
				memcpy(mPreviewHeapArray[mPreviewDcamAllocBufferCnt -1]->camera_memory->data,
					frame->buf_Virt_Addr, dataSize);
#if FREE_PMEM_BAK
				handleDataCallback(CAMERA_MSG_PREVIEW_FRAME,
					mPreviewHeapArray[mPreviewDcamAllocBufferCnt - 1],
					0, NULL, mUser, 1);
#else
				mData_cb(CAMERA_MSG_PREVIEW_FRAME,
				mPreviewHeapArray[mPreviewDcamAllocBufferCnt - 1]->camera_memory,
				0,NULL,mUser);
#endif
			}
		}

		if ((mMsgEnabled & CAMERA_MSG_VIDEO_FRAME) && isRecordingMode()) {
			nsecs_t timestamp = systemTime();/*frame->timestamp;*/
			LOGV("test timestamp = %lld, mIsStoreMetaData: %d.",timestamp, mIsStoreMetaData);
			if (mTimeCoeff > 1) {
				if (0 != mRecordingFirstFrameTime) {
					timestamp = mRecordingFirstFrameTime + (timestamp - mRecordingFirstFrameTime)*mTimeCoeff;
				} else {
					mRecordingFirstFrameTime = timestamp;
					LOGV("first frame.");
				}
			}
			/*LOGV("test slowmotion:%lld.",timestamp);*/
			if (mIsStoreMetaData) {
				uint32_t *data = (uint32_t *)mMetadataHeap->data + offset * METADATA_SIZE / 4;
				*data++ = kMetadataBufferTypeCameraSource;
				*data++ = frame->buffer_phy_addr;
				*data++ = (uint32_t)frame->buf_Virt_Addr;
				*data++ = width;
				*data++ = height;
				*data++ = mPreviewWidth_trimx;
				*data     = mPreviewHeight_trimy;
				mData_cb_timestamp(timestamp, CAMERA_MSG_VIDEO_FRAME, mMetadataHeap, offset, mUser);
			} else {
				//mData_cb_timestamp(timestamp, CAMERA_MSG_VIDEO_FRAME, mPreviewHeap->mBuffers[offset], mUser);
				uint32_t tmpIndex = offset;
				if(camera_get_rot_set()) {
					tmpIndex += kPreviewBufferCount;
				}
#if FREE_PMEM_BAK
				handleDataCallbackTimestamp(timestamp,
					CAMERA_MSG_VIDEO_FRAME,
					mPreviewHeapArray[tmpIndex],
					0, mUser);
#else
				mData_cb_timestamp(timestamp,
					CAMERA_MSG_VIDEO_FRAME,
					mPreviewHeapArray[tmpIndex]->camera_memory,
					0, mUser);
#endif
			}
		//LOGV("receivePreviewFrame: record index: %d, offset: %x, size: %x, frame->buf_Virt_Addr: 0x%x.", offset, off, size, (uint32_t)frame->buf_Virt_Addr);
		} else {
			if (PREVIEW_BUFFER_USAGE_DCAM == mPreviewBufferUsage) {
				flush_buffer(CAMERA_FLUSH_PREVIEW_HEAP, offset,
							(void*)frame->buf_Virt_Addr,
							(void*)frame->buffer_phy_addr,
							(int)frame->dx * frame->dy * 3 /2);

				if (CAMERA_SUCCESS != camera_release_frame(offset)) {
					LOGE("receivePreviewFrame: fail to camera_release_frame().offset: %d.", (int)offset);
				}
			}
		}
		// When we are doing preview but not recording, we need to
		// release every preview frame immediately so that the next
		// preview frame is delivered.  However, when we are recording
		// (whether or not we are also streaming the preview frames to
		// the screen), we have the user explicitly release a preview
		// frame via method releaseRecordingFrame().  In this way we
		// allow a video encoder which is potentially slower than the
		// preview stream to skip frames.  Note that we call
		// camera_release_frame() in this method because we first
		// need to check to see if mPreviewCallback != NULL, which
		// requires holding mCallbackLock.

	}
	else
		LOGE("receivePreviewFrame: mData_cb is null.");

}

//can not use callback lock?
void SprdCameraHardware::notifyShutter()
{
	LOGV("notifyShutter: E");
	print_time();

	LOGV("notifyShutter mMsgEnabled: 0x%x.", mMsgEnabled);

	if ((CAMERA_ZSL_CONTINUE_SHOT_MODE != mCaptureMode)
		&& (CAMERA_NORMAL_CONTINUE_SHOT_MODE != mCaptureMode)) {
		if (mMsgEnabled & CAMERA_MSG_SHUTTER)
			mNotify_cb(CAMERA_MSG_SHUTTER, 0, 0, mUser);
	} else {
			mNotify_cb(CAMERA_MSG_SHUTTER, 0, 0, mUser);
	}

	print_time();
	LOGV("notifyShutter: X");
}


// Pass the pre-LPM raw picture to raw picture callback.
// This method is called by a libqcamera thread, different from the one on
// which startPreview() or takePicture() are called.
void SprdCameraHardware::receiveRawPicture(camera_frame_type *frame)
{
	LOGV("receiveRawPicture: E");

	print_time();

	Mutex::Autolock cbLock(&mCaptureCbLock);

	if (NULL == frame) {
		LOGE("receiveRawPicture: invalid frame pointer");
		return;
	}

	if(SPRD_INTERNAL_CAPTURE_STOPPING == getCaptureState()) {
		LOGV("receiveRawPicture: warning: capture state = SPRD_INTERNAL_CAPTURE_STOPPING, return \n");
		return;
	}

	void *vaddr = NULL;
	uint32_t phy_addr = 0;
	uint32_t width = mPreviewWidth;
	uint32_t height = mPreviewHeight;

	if (iSDisplayCaptureFrame()) {
		phy_addr = getRedisplayMem();

		if (0 == phy_addr) {
			LOGE("%s: get review memory failed", __func__);
			goto callbackraw;
		}

		if ( 0 != camera_get_data_redisplay(phy_addr, width, height, frame->buffer_phy_addr,
										frame->buffer_uv_phy_addr, frame->dx, frame->dy)) {
			LOGE("%s: Fail to camera_get_data_redisplay.", __func__);
			FreeReDisplayMem();
			goto callbackraw;
		}

		if (!displayOneFrameForCapture(width, height, phy_addr, (char *)mReDisplayHeap->data)) {
			LOGE("%s: displayOneFrame failed", __func__);
		}

		FreeReDisplayMem();
	}
callbackraw:
	if (mData_cb!= NULL) {
		// Find the offset within the heap of the current buffer.
		ssize_t offset = (uint32_t)frame->buf_Virt_Addr;
		offset -= (uint32_t)mRawHeap->data;
		ssize_t frame_size = 0;

		if(CAMERA_RGB565 == frame->format)
			frame_size = frame->dx * frame->dy * 2;        // for RGB565
		else if(CAMERA_YCBCR_4_2_2 == frame->format)
			frame_size = frame->dx * frame->dy * 2;        //for YUV422
		else if(CAMERA_YCBCR_4_2_0 == frame->format)
			frame_size = frame->dx * frame->dy * 3 / 2;          //for YUV420
		else
			frame_size = frame->dx * frame->dy * 2;

		if (offset + frame_size <= (ssize_t)mRawHeap->phys_size) {
			offset /= frame_size;

			LOGV("mMsgEnabled: 0x%x, offset: %d.",mMsgEnabled, (uint32_t)offset);

			if (mMsgEnabled & CAMERA_MSG_RAW_IMAGE) {
#if FREE_PMEM_BAK
				handleDataCallback(CAMERA_MSG_RAW_IMAGE, mRawHeap, offset, NULL, mUser, 0);
#else
				mData_cb(CAMERA_MSG_RAW_IMAGE, mRawHeap->camera_memory, offset, NULL, mUser);
#endif
			}

			if (mMsgEnabled & CAMERA_MSG_RAW_IMAGE_NOTIFY) {
				LOGV("mMsgEnabled & CAMERA_MSG_RAW_IMAGE_NOTIFY");
				mNotify_cb(CAMERA_MSG_RAW_IMAGE_NOTIFY, 0,0,mUser);
			}
		}
		else
			LOGE("receiveRawPicture: virtual address %p is out of range!", frame->buf_Virt_Addr);
	}
	else
		LOGV("Raw-picture callback was canceled--skipping.");

	print_time();
	LOGV("receiveRawPicture: X");
}

// Encode the post-LPM raw picture.

// This method is called by a libqcamera thread, different from the one on
// which startPreview() or takePicture() are called.
void SprdCameraHardware::receivePostLpmRawPicture(camera_frame_type *frame)
{
	LOGV("receivePostLpmRawPicture: E");
	print_time();

	Mutex::Autolock cbLock(&mCaptureCbLock);

	if (NULL == frame) {
		LOGE("receivePostLpmRawPicture: invalid frame pointer");
		return;
	}

	if (mData_cb!= NULL) {
		mJpegSize = 0;
		camera_handle_type camera_handle;
		if(CAMERA_SUCCESS != camera_encode_picture(frame, &camera_handle, camera_cb, this)) {
			setCameraState(SPRD_ERROR, STATE_CAPTURE);
			FreePmem(mRawHeap);
			mRawHeap = NULL;
			LOGE("receivePostLpmRawPicture: fail to camera_encode_picture().");
		}
	}
	else {
		LOGV("JPEG callback was cancelled--not encoding image.");
		// We need to keep the raw heap around until the JPEG is fully
		// encoded, because the JPEG encode uses the raw image contained in
		// that heap.
	}

	print_time();
	LOGV("receivePostLpmRawPicture: X");
}


void SprdCameraHardware::receiveJpegPictureFragment( JPEGENC_CBrtnType *encInfo)
{
	Mutex::Autolock cbLock(&mCaptureCbLock);

	if (NULL == encInfo) {
		LOGE("receiveJpegPictureFragment: invalid enc info pointer");
		return;
	}

    camera_encode_mem_type *enc = (camera_encode_mem_type *)encInfo->outPtr;

    uint8_t *base = (uint8_t *)mJpegHeap->mHeap->base();
    uint32_t size = encInfo->size;
    uint32_t remaining = mJpegHeap->mHeap->virtualSize();
    uint32_t i = 0;
    uint8_t *temp_ptr,*src_ptr;
    remaining -= mJpegSize;

	LOGV("receiveJpegPictureFragment E.");
    LOGV("receiveJpegPictureFragment: (status %d size %d remaining %d mJpegSize %d)",
         encInfo->status,
         size, remaining,mJpegSize);

    if (size > remaining) {
		LOGE("receiveJpegPictureFragment: size %d exceeds what "
		 	"remains in JPEG heap (%d), truncating",
		 	size,
		 	remaining);
		size = remaining;
    }

    //camera_handle.mem.encBuf[index].used_len = 0;
	LOGV("receiveJpegPictureFragment : base + mJpegSize: %x, enc->buffer: %x, size: %x", (uint32_t)(base + mJpegSize), (uint32_t)enc->buffer, size) ;

//	memcpy(base + mJpegSize, enc->buffer, size);
    mJpegSize += size;

	LOGV("receiveJpegPictureFragment X.");
}

void SprdCameraHardware::receiveJpegPosPicture(void)//(camera_frame_type *frame)
{
	LOGV("receiveJpegPosPicture: E");
	print_time();

	Mutex::Autolock cbLock(&mCaptureCbLock);

	if (mData_cb!= NULL) {
		bool encode_location = true;
		camera_position_type pt = {0, 0, 0, 0, NULL};

		encode_location = getCameraLocation(&pt);
		if (encode_location) {
			if (camera_set_position(&pt, NULL, NULL) != CAMERA_SUCCESS) {
				LOGE("%s: camera_set_position: error", __func__);
				// return;  // not a big deal
			}
		}
		else
			LOGV("%s: not setting image location", __func__);

		mJpegSize = 0;
	}
	else {
		LOGV("%s: JPEG callback was cancelled--not encoding image.", __func__);
		// We need to keep the raw heap around until the JPEG is fully
		// encoded, because the JPEG encode uses the raw image contained in
		// that heap.
		if (!iSZslMode()) {
			deinitCapture();
		}
	}

	print_time();
	LOGV("%s: receiveJpegPosPicture: free mCallbackLock!", __func__);
}

// This method is called by a libqcamera thread, different from the one on
// which startPreview() or takePicture() are called.
void SprdCameraHardware::receiveJpegPicture(JPEGENC_CBrtnType *encInfo)
{
    GET_END_TIME;
    GET_USE_TIME;
	camera_encode_mem_type *enc = (camera_encode_mem_type *)encInfo->outPtr;
    LOGE("Capture Time:%d(ms).",s_use_time);

	LOGV("receiveJpegPicture: E image (%d bytes out of %d)",
				mJpegSize, mJpegHeap->mBufferSize);
	print_time();
	Mutex::Autolock cbLock(&mCaptureCbLock);

	int index = 0;

	//if (mJpegPictureCallback) {
	if (mData_cb) {
		LOGV("receiveJpegPicture: mData_cb.");
		// The reason we do not allocate into mJpegHeap->mBuffers[offset] is
		// that the JPEG image's size will probably change from one snapshot
		// to the next, so we cannot reuse the MemoryBase object.
		LOGV("receiveJpegPicture: mMsgEnabled: 0x%x.", mMsgEnabled);

		// mJpegPictureCallback(buffer, mPictureCallbackCookie);
		if ((CAMERA_ZSL_CONTINUE_SHOT_MODE != mCaptureMode)
			&& (CAMERA_NORMAL_CONTINUE_SHOT_MODE != mCaptureMode)) {
			if (mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE){
				camera_memory_t *mem = mGetMemory_cb(-1, mJpegSize, 1, 0);
				memcpy(mem->data, enc->buffer, mJpegSize);
				//mData_cb(CAMERA_MSG_COMPRESSED_IMAGE,buffer, mUser );
				mData_cb(CAMERA_MSG_COMPRESSED_IMAGE,mem, 0, NULL, mUser );
				mem->release(mem);
			}
		} else {
			camera_memory_t *mem = mGetMemory_cb(-1, mJpegSize, 1, 0);
			memcpy(mem->data, enc->buffer, mJpegSize);
			mData_cb(CAMERA_MSG_COMPRESSED_IMAGE,mem, 0, NULL, mUser );
			mem->release(mem);
		}
	}
	else
		LOGV("JPEG callback was cancelled--not delivering image.");

	// NOTE: the JPEG encoder uses the raw image contained in mRawHeap, so we need
	// to keep the heap around until the encoding is complete.
	LOGV("receiveJpegPicture: free the Raw and Jpeg mem. 0x%p, 0x%p", mRawHeap, mMiscHeap);

	if (!iSZslMode()) {
		if (encInfo->need_free) {
			deinitCapture();
		}
	} else {
		flush_buffer(CAMERA_FLUSH_RAW_HEAP_ALL, 0,(void*)0,(void*)0,0);
	}
	print_time();
	LOGV("receiveJpegPicture: X callback done.");
}

void SprdCameraHardware::receiveJpegPictureError(void)
{
	LOGV("receiveJpegPictureError.");
	print_time();
	Mutex::Autolock cbLock(&mCaptureCbLock);
	if (!checkPreviewStateForCapture()) {
		LOGE("drop current jpegPictureError msg");
		return;
	}

	int index = 0;
	if (mData_cb) {
		LOGV("receiveJpegPicture: mData_cb.");
		if (mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE){
			mData_cb(CAMERA_MSG_COMPRESSED_IMAGE,NULL, 0, NULL, mUser );
		}
	} else
		LOGV("JPEG callback was cancelled--not delivering image.");

	// NOTE: the JPEG encoder uses the raw image contained in mRawHeap, so we need
	// to keep the heap around until the encoding is complete.

	print_time();
	LOGV("receiveJpegPictureError: X callback done.");
}

void SprdCameraHardware::receiveCameraExitError(void)
{
	Mutex::Autolock cbPreviewLock(&mPreviewCbLock);
	Mutex::Autolock cbCaptureLock(&mCaptureCbLock);
	if (!checkPreviewStateForCapture()) {
		LOGE("drop current cameraExit msg");
		return;
	}
	if((mMsgEnabled & CAMERA_MSG_ERROR) && (mData_cb != NULL)) {
		LOGE("HandleErrorState");
		mNotify_cb(CAMERA_MSG_ERROR, 0,0,mUser);
	}

	LOGE("HandleErrorState:don't enable error msg!");
}

void SprdCameraHardware::receiveTakePictureError(void)
{
	Mutex::Autolock cbLock(&mCaptureCbLock);
	if (!checkPreviewStateForCapture()) {
		LOGE("drop current takePictureError msg");
		return;
	}

	LOGE("camera cb: invalid state %s for taking a picture!",
		 getCameraStateStr(getCaptureState()));

	if ((mMsgEnabled & CAMERA_MSG_RAW_IMAGE) && (NULL != mData_cb))
		mData_cb(CAMERA_MSG_RAW_IMAGE, NULL, 0 , NULL, mUser);

	if ((mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE) && (NULL != mData_cb))
		mData_cb(CAMERA_MSG_COMPRESSED_IMAGE, NULL, 0, NULL, mUser);
}


//transite from 'from' state to 'to' state and signal the waitting thread. if the current state is not 'from', transite to SPRD_ERROR state
//should be called from the callback
SprdCameraHardware::Sprd_camera_state
SprdCameraHardware::transitionState(SprdCameraHardware::Sprd_camera_state from,
									SprdCameraHardware::Sprd_camera_state to,
									SprdCameraHardware::state_owner owner, bool lock)
{
	volatile SprdCameraHardware::Sprd_camera_state *which_ptr = NULL;
	LOGV("transitionState: owner = %d, lock = %d", owner, lock);

	if (lock) mStateLock.lock();

	switch (owner) {
	case STATE_CAMERA:
		which_ptr = &mCameraState.camera_state;
		break;

	case STATE_PREVIEW:
		which_ptr = &mCameraState.preview_state;
		break;

	case STATE_CAPTURE:
		which_ptr = &mCameraState.capture_state;
		break;

	case STATE_FOCUS:
		which_ptr = &mCameraState.focus_state;
		break;

	default:
		LOGV("changeState: error owner");
		break;
	}

	if (NULL != which_ptr) {
		if (from != *which_ptr) {
			to = SPRD_ERROR;
		}

		LOGV("changeState: %s --> %s", getCameraStateStr(from),
								   getCameraStateStr(to));

		if (*which_ptr != to) {
			*which_ptr = to;
			mStateWait.signal();
		}
	}

	if (lock) mStateLock.unlock();

	return to;
}

void SprdCameraHardware::HandleStartPreview(camera_cb_type cb,
											 int32_t parm4)
{

	LOGV("HandleStartPreview in: cb = %d, parm4 = 0x%x, state = %s",
				cb, parm4, getCameraStateStr(getPreviewState()));

	switch(cb) {
	case CAMERA_RSP_CB_SUCCESS:
		transitionState(SPRD_INTERNAL_PREVIEW_REQUESTED,
					SPRD_PREVIEW_IN_PROGRESS,
					STATE_PREVIEW);
		break;

	case CAMERA_EVT_CB_FRAME:
		LOGV("CAMERA_EVT_CB_FRAME");
		switch (getPreviewState()) {
		case SPRD_PREVIEW_IN_PROGRESS:
			receivePreviewFrame((camera_frame_type *)parm4);
			break;

		case SPRD_INTERNAL_PREVIEW_STOPPING:
			LOGV("camera cb: discarding preview frame "
			"while stopping preview");
			break;

		default:
			LOGV("HandleStartPreview: invalid state");
			break;
			}
		break;

	case CAMERA_EVT_CB_FD:
		LOGV("CAMERA_EVT_CB_FD");
		if (isPreviewing()) {
			receivePreviewFDFrame((camera_frame_type *)parm4);
		}
		break;

	case CAMERA_EXIT_CB_FAILED:			//Execution failed or rejected
		LOGE("SprdCameraHardware::camera_cb: @CAMERA_EXIT_CB_FAILURE(%d) in state %s.",
				parm4, getCameraStateStr(getPreviewState()));
		transitionState(getPreviewState(), SPRD_ERROR, STATE_PREVIEW);
		receiveCameraExitError();
		break;

	default:
		transitionState(getPreviewState(), SPRD_ERROR, STATE_PREVIEW);
		LOGE("unexpected cb %d for CAMERA_FUNC_START_PREVIEW.", cb);
		break;
	}

	LOGV("HandleStartPreview out, state = %s", getCameraStateStr(getPreviewState()));
}

void SprdCameraHardware::HandleStopPreview(camera_cb_type cb,
										  int32_t parm4)
{
	LOGV("HandleStopPreview in: cb = %d, parm4 = 0x%x, state = %s",
				cb, parm4, getCameraStateStr(getPreviewState()));

	transitionState(SPRD_INTERNAL_PREVIEW_STOPPING,
				SPRD_IDLE,
				STATE_PREVIEW);
	/*freePreviewMem();*/

	LOGV("HandleStopPreview out, state = %s", getCameraStateStr(getPreviewState()));
}

void SprdCameraHardware::HandleTakePicture(camera_cb_type cb,
										 	 int32_t parm4)
{
	LOGV("HandleTakePicture in: cb = %d, parm4 = 0x%x, state = %s",
				cb, parm4, getCameraStateStr(getCaptureState()));
	bool encode_location = true;
	camera_position_type pt = {0, 0, 0, 0, NULL};
	encode_location = getCameraLocation(&pt);

	switch (cb) {
	case CAMERA_EVT_CB_FLUSH:
		LOGV("capture:flush.");
		flush_buffer(CAMERA_FLUSH_RAW_HEAP_ALL, 0,(void*)0,(void*)0,0);
		break;
	case CAMERA_RSP_CB_SUCCESS:
		LOGV("HandleTakePicture: CAMERA_RSP_CB_SUCCESS");
		transitionState(SPRD_INTERNAL_RAW_REQUESTED,
					SPRD_WAITING_RAW,
					STATE_CAPTURE);
		break;

	case CAMERA_EVT_CB_SNAPSHOT_DONE:
		LOGV("HandleTakePicture: CAMERA_EVT_CB_SNAPSHOT_DONE");
		if (encode_location) {
			if (camera_set_position(&pt, NULL, NULL) != CAMERA_SUCCESS) {
			LOGE("receiveRawPicture: camera_set_position: error");
			// return;	// not a big deal
			}
		}
		else
			LOGV("receiveRawPicture: not setting image location");
		if (checkPreviewStateForCapture()) {
			notifyShutter();
			receiveRawPicture((camera_frame_type *)parm4);
		} else {
			LOGE("HandleTakePicture: drop current rawPicture");
		}
		break;

	case CAMERA_EXIT_CB_DONE:
		LOGV("HandleTakePicture: CAMERA_EXIT_CB_DONE");
		if (SPRD_WAITING_RAW == getCaptureState())
		{
			transitionState(SPRD_WAITING_RAW,
						((NULL != mData_cb) ? SPRD_WAITING_JPEG : SPRD_IDLE),
						STATE_CAPTURE);
	        // It's important that we call receiveRawPicture() before
	        // we transition the state because another thread may be
	        // waiting in cancelPicture(), and then delete this object.
	        // If the order were reversed, we might call
	        // receiveRawPicture on a dead object.
			if (checkPreviewStateForCapture()) {
				receivePostLpmRawPicture((camera_frame_type *)parm4);
			} else {
				LOGE("HandleTakePicture drop current LpmRawPicture");
			}
		}
		break;

	case CAMERA_EXIT_CB_FAILED:			//Execution failed or rejected
		LOGE("SprdCameraHardware::camera_cb: @CAMERA_EXIT_CB_FAILURE(%d) in state %s.",
				parm4, getCameraStateStr(getCaptureState()));
		transitionState(getCaptureState(), SPRD_ERROR, STATE_CAPTURE);
		receiveCameraExitError();
		break;

	default:
		LOGE("HandleTakePicture: unkown cb = %d", cb);
		transitionState(getCaptureState(), SPRD_ERROR, STATE_CAPTURE);
		receiveTakePictureError();
		break;
	}

	LOGV("HandleTakePicture out, state = %s", getCameraStateStr(getCaptureState()));
}

void SprdCameraHardware::HandleCancelPicture(camera_cb_type cb,
										 	 int32_t parm4)
{
	LOGV("HandleCancelPicture in: cb = %d, parm4 = 0x%x, state = %s",
				cb, parm4, getCameraStateStr(getCaptureState()));

	if (checkPreviewStateForCapture()) {
		Mutex::Autolock cbLock(&mCaptureCbLock);
	}

	transitionState(SPRD_INTERNAL_CAPTURE_STOPPING,
				SPRD_IDLE,
				STATE_CAPTURE);



	LOGV("HandleCancelPicture out, state = %s", getCameraStateStr(getCaptureState()));
}

void SprdCameraHardware::HandleEncode(camera_cb_type cb,
								  		int32_t parm4)
{
	LOGV("HandleEncode in: cb = %d, parm4 = 0x%x, state = %s",
				cb, parm4, getCameraStateStr(getCaptureState()));

	switch (cb) {
	case CAMERA_RSP_CB_SUCCESS:
        // We already transitioned the camera state to
        // SPRD_WAITING_JPEG when we called
        // camera_encode_picture().
		break;

	case CAMERA_EXIT_CB_DONE:
		LOGV("HandleEncode: CAMERA_EXIT_CB_DONE");
		if ((SPRD_WAITING_JPEG == getCaptureState())) {
			// Receive the last fragment of the image.
			receiveJpegPictureFragment((JPEGENC_CBrtnType *)parm4);
			LOGV("CAMERA_EXIT_CB_DONE MID.");
			if (checkPreviewStateForCapture()) {
				receiveJpegPicture((JPEGENC_CBrtnType *)parm4);
			} else {
				LOGE("HandleEncode: drop current jpgPicture");
			}
#if 1//to do it
			if (((JPEGENC_CBrtnType *)parm4)->need_free) {
				transitionState(SPRD_WAITING_JPEG,
						SPRD_IDLE,
						STATE_CAPTURE);
			} else {
				transitionState(SPRD_WAITING_JPEG,
						SPRD_INTERNAL_RAW_REQUESTED,
						STATE_CAPTURE);
			}
#else
			transitionState(SPRD_WAITING_JPEG,
						SPRD_IDLE,
						STATE_CAPTURE);
#endif
		}
		break;

	case CAMERA_EXIT_CB_FAILED:
		LOGV("HandleEncode: CAMERA_EXIT_CB_FAILED");
		transitionState(getCaptureState(), SPRD_ERROR, STATE_CAPTURE);
		receiveCameraExitError();
		break;

	default:
		LOGV("HandleEncode: unkown error = %d", cb);
		transitionState(getCaptureState(), SPRD_ERROR, STATE_CAPTURE);
		receiveJpegPictureError();
		break;
	}

	LOGV("HandleEncode out, state = %s", getCameraStateStr(getCaptureState()));
}

void SprdCameraHardware::HandleFocus(camera_cb_type cb,
								  int32_t parm4)
{
	LOGV("HandleFocus in: cb = %d, parm4 = 0x%x, state = %s",
				cb, parm4, getCameraStateStr(getPreviewState()));

	if (NULL == mNotify_cb) {
		LOGE("HandleFocus: mNotify_cb is NULL");
		setCameraState(SPRD_IDLE, STATE_FOCUS);
		return;
	}

	switch (cb) {
	case CAMERA_RSP_CB_SUCCESS:
		LOGV("camera cb: autofocus has started.");
		break;

	case CAMERA_EXIT_CB_DONE:
		LOGV("camera cb: autofocus succeeded.");
		LOGV("camera cb: autofocus mNotify_cb start.");
		if (mMsgEnabled & CAMERA_MSG_FOCUS)
			mNotify_cb(CAMERA_MSG_FOCUS, 1, 0, mUser);
		else
			LOGE("camera cb: mNotify_cb is null.");

		LOGV("camera cb: autofocus mNotify_cb ok.");
		break;
	case CAMERA_EXIT_CB_ABORT:
		LOGE("camera cb: autofocus aborted");
		break;

	case CAMERA_EXIT_CB_FAILED:
		LOGE("camera cb: autofocus failed");
		if (mMsgEnabled & CAMERA_MSG_FOCUS)
		mNotify_cb(CAMERA_MSG_FOCUS, 0, 0, mUser);
		break;

	default:
		LOGE("camera cb: unknown cb %d for CAMERA_FUNC_START_FOCUS!", cb);
		if (mMsgEnabled & CAMERA_MSG_FOCUS)
			mNotify_cb(CAMERA_MSG_FOCUS, 0, 0, mUser);
		break;
	}

	transitionState(getFocusState(), SPRD_IDLE, STATE_FOCUS);

	LOGV("HandleFocus out, state = %s", getCameraStateStr(getCaptureState()));
}

void SprdCameraHardware::HandleStartCamera(camera_cb_type cb,
								  		int32_t parm4)
{
	LOGV("HandleCameraStart in: cb = %d, parm4 = 0x%x, state = %s",
				cb, parm4, getCameraStateStr(getCameraState()));

	transitionState(SPRD_INIT, SPRD_IDLE, STATE_CAMERA);

	LOGV("HandleCameraStart out, state = %s", getCameraStateStr(getCameraState()));
}

void SprdCameraHardware::HandleStopCamera(camera_cb_type cb, int32_t parm4)
{
	LOGV("HandleStopCamera in: cb = %d, parm4 = 0x%x, state = %s",
				cb, parm4, getCameraStateStr(getCameraState()));

	transitionState(SPRD_INTERNAL_STOPPING, SPRD_INIT, STATE_CAMERA);

	LOGV("HandleStopCamera out, state = %s", getCameraStateStr(getCameraState()));
}

void SprdCameraHardware::camera_cb(camera_cb_type cb,
                                           const void *client_data,
                                           camera_func_type func,
                                           int32_t parm4)
{
	SprdCameraHardware *obj = (SprdCameraHardware *)client_data;

    switch(func) {
    // This is the commonest case.
	case CAMERA_FUNC_START_PREVIEW:
	    obj->HandleStartPreview(cb, parm4);
		break;

	case CAMERA_FUNC_STOP_PREVIEW:
	    obj->HandleStopPreview(cb, parm4);
	    break;

	case CAMERA_FUNC_RELEASE_PICTURE:
		obj->HandleCancelPicture(cb, parm4);
	    break;

	case CAMERA_FUNC_TAKE_PICTURE:
		obj->HandleTakePicture(cb, parm4);
	    break;

	case CAMERA_FUNC_ENCODE_PICTURE:
	    obj->HandleEncode(cb, parm4);
	    break;

	case CAMERA_FUNC_START_FOCUS:
		obj->HandleFocus(cb, parm4);
		break;

	case CAMERA_FUNC_START:
		obj->HandleStartCamera(cb, parm4);
	    break;

	case CAMERA_FUNC_STOP:
		obj->HandleStopCamera(cb, parm4);
		break;

    default:
        // transition to SPRD_ERROR ?
        LOGE("Unknown camera-callback status %d", cb);
		break;
	}
}

int SprdCameraHardware::switch_monitor_thread_init(void *p_data)
{
	struct cmr_msg           message = {0, 0, 0, 0};
	int                      ret = NO_ERROR;
	pthread_attr_t           attr;

	SprdCameraHardware *obj = (SprdCameraHardware *)p_data;

	LOGV("switch monitor thread init, %d", obj->mSwitchMonitorInited);

	if (!obj->mSwitchMonitorInited) {
		ret = cmr_msg_queue_create(SWITCH_MONITOR_QUEUE_SIZE, &obj->mSwitchMonitorMsgQueHandle);
		if (ret) {
			LOGE("NO Memory, Failed to create switch monitor message queue\n");
			return ret;
		}
		sem_init(&obj->mSwitchMonitorSyncSem, 0, 0);
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		ret = pthread_create(&obj->mSwitchMonitorThread,
			&attr,
			switch_monitor_thread_proc,
			(void *)obj);
		obj->mSwitchMonitorInited = 1;
		message.msg_type = CMR_EVT_SW_MON_INIT;
		message.data = NULL;
		ret = cmr_msg_post(obj->mSwitchMonitorMsgQueHandle, &message);
		if (ret) {
			LOGE("switch_monitor_thread_init Fail to send one msg!");
		}
		sem_wait(&obj->mSwitchMonitorSyncSem);
	}
	return ret;
}

int SprdCameraHardware::switch_monitor_thread_deinit(void *p_data)
{
	struct cmr_msg           message = {0, 0, 0, 0};
	int                      ret = NO_ERROR;
	SprdCameraHardware *     obj = (SprdCameraHardware *)p_data;

	LOGV("switch monitor thread deinit inited, %d", obj->mSwitchMonitorInited);

	if (obj->mSwitchMonitorInited) {
		message.msg_type = CMR_EVT_SW_MON_EXIT;
		ret = cmr_msg_post(obj->mSwitchMonitorMsgQueHandle, &message);
		if (ret) {
			LOGE("Fail to send one msg to camera callback thread");
		}
		sem_wait(&obj->mSwitchMonitorSyncSem);
		sem_destroy(&obj->mSwitchMonitorSyncSem);
		cmr_msg_queue_destroy(obj->mSwitchMonitorMsgQueHandle);
		obj->mSwitchMonitorMsgQueHandle = 0;
		obj->mSwitchMonitorInited = 0;
	}
	return ret ;
}

void * SprdCameraHardware::switch_monitor_thread_proc(void *p_data)
{
	struct cmr_msg           message = {0, 0, 0, 0};
	int                      exit_flag = 0;
	int                      ret = NO_ERROR;
	SprdCameraHardware *     obj = (SprdCameraHardware *)p_data;

	while (1) {
		ret = cmr_msg_timedget(obj->mSwitchMonitorMsgQueHandle, &message);
		if (CMR_MSG_NO_OTHER_MSG == ret) {
			if (obj->checkSetParameters(obj->mParameters, obj->mSetParametersBak) &&
				obj->mBakParamFlag) {
				obj->setCameraState(SPRD_SET_PARAMS_IN_PROGRESS, STATE_SET_PARAMS);
				LOGV("switch_monitor_thread_proc, bak set");
				obj->setParametersInternal(obj->mSetParametersBak);
				obj->setCameraState(SPRD_IDLE, STATE_SET_PARAMS);
			}
#if FREE_PMEM_BAK
			obj->cameraBakMemCheckAndFree();
#endif
		} else if (NO_ERROR != ret) {
			CMR_LOGE("Message queue destroyed");
			break;
		} else {
			CMR_LOGE("message.msg_type 0x%x, sub-type 0x%x",
				message.msg_type,
				message.sub_msg_type);

			switch (message.msg_type) {
			case CMR_EVT_SW_MON_INIT:
				LOGV("switch monitor thread msg INITED!");
				obj->setCameraState(SPRD_IDLE, STATE_SET_PARAMS);
				sem_post(&obj->mSwitchMonitorSyncSem);
				break;

			case CMR_EVT_SW_MON_SET_PARA:
				LOGV("switch monitor thread msg SET_PARA!");
				obj->setCameraState(SPRD_SET_PARAMS_IN_PROGRESS, STATE_SET_PARAMS);
				obj->setParametersInternal(obj->mSetParameters);
				obj->setCameraState(SPRD_IDLE, STATE_SET_PARAMS);
				break;

			case CMR_EVT_SW_MON_EXIT:
				LOGV("switch monitor thread msg EXIT!\n");
				exit_flag = 1;
				sem_post(&obj->mSwitchMonitorSyncSem);
				CMR_PRINT_TIME;
				break;

			default:
				LOGE("Unsupported switch monitorMSG");
				break;
			}

			if (1 == message.alloc_flag) {
				if (message.data) {
					free(message.data);
					message.data = 0;
				}
			}
		}
		if (exit_flag) {
			CMR_LOGI("switch monitor thread exit ");
			break;
		}
	}
	return NULL;
}

////////////////////////////////////////////////////////////////////////////////////////
//memory related
////////////////////////////////////////////////////////////////////////////////////////
SprdCameraHardware::MemPool::MemPool(int buffer_size, int num_buffers,
                                         int frame_size,
                                         int frame_offset,
                                         const char *name) :
    mBufferSize(buffer_size),
    mNumBuffers(num_buffers),
    mFrameSize(frame_size),
    mFrameOffset(frame_offset),
    mBuffers(NULL), mName(name)
{
    // empty
}

void SprdCameraHardware::MemPool::completeInitialization()
{
    // If we do not know how big the frame will be, we wait to allocate
    // the buffers describing the individual frames until we do know their
    // size.

    if (mFrameSize > 0) {
        mBuffers = new sp<MemoryBase>[mNumBuffers];
        for (int i = 0; i < mNumBuffers; i++) {
            mBuffers[i] = new
                MemoryBase(mHeap,
                           i * mBufferSize + mFrameOffset,
                           mFrameSize);
        }
    }
}

SprdCameraHardware::AshmemPool::AshmemPool(int buffer_size, int num_buffers,
                                               int frame_size,
                                               int frame_offset,
                                               const char *name) :
    SprdCameraHardware::MemPool(buffer_size,
                                    num_buffers,
                                    frame_size,
                                    frame_offset,
                                    name)
{
        LOGV("constructing MemPool %s backed by ashmem: "
             "%d frames @ %d bytes, offset %d, "
             "buffer size %d",
             mName,
             num_buffers, frame_size, frame_offset, buffer_size);

        int page_mask = getpagesize() - 1;
        int ashmem_size = buffer_size * num_buffers;
        ashmem_size += page_mask;
        ashmem_size &= ~page_mask;

        mHeap = new MemoryHeapBase(ashmem_size);

        completeInitialization();
}

SprdCameraHardware::MemPool::~MemPool()
{
    LOGV("destroying MemPool %s", mName);
    if (mFrameSize > 0)
        delete [] mBuffers;
    mHeap.clear();
    LOGV("destroying MemPool %s completed", mName);
}

status_t SprdCameraHardware::MemPool::dump(int fd, const Vector<String16> &args) const
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;

    snprintf(buffer, 255, "SprdCameraHardware::AshmemPool::dump\n");
    result.append(buffer);
    if (mName) {
        snprintf(buffer, 255, "mem pool name (%s)\n", mName);
        result.append(buffer);
    }
    if (mHeap != 0) {
        snprintf(buffer, 255, "heap base(%p), size(%d), flags(%d), device(%s)\n",
                 mHeap->getBase(), mHeap->getSize(),
                 mHeap->getFlags(), mHeap->getDevice());
        result.append(buffer);
    }
    snprintf(buffer, 255, "buffer size (%d), number of buffers (%d),"
             " frame size(%d), and frame offset(%d)\n",
             mBufferSize, mNumBuffers, mFrameSize, mFrameOffset);
    result.append(buffer);
    write(fd, result.string(), result.size());
    return NO_ERROR;
}

int SprdCameraHardware::flush_buffer(camera_flush_mem_type_e  type, int index, void *v_addr, void *p_addr, int size)
{
	int ret = 0;
	sprd_camera_memory_t  *pmem = NULL;
	MemoryHeapIon *pHeapIon;


	switch(type)
	{
	case CAMERA_FLUSH_RAW_HEAP:
		pmem = mRawHeap;
		break;
	case CAMERA_FLUSH_RAW_HEAP_ALL:
		pmem = mRawHeap;
		v_addr = (void*)pmem->data;
		p_addr = (void*)pmem->phys_addr;
		size = (int)pmem->phys_size;
		break;
	case CAMERA_FLUSH_PREVIEW_HEAP:
		if (index < kPreviewBufferCount) {
			pmem = mPreviewHeapArray[index];
		}
		break;
	default:
		break;
	}


	if (pmem) {
		pHeapIon = pmem->ion_heap;
		ret = pHeapIon->flush_ion_buffer(v_addr, p_addr, size);
		if (ret) {
			LOGV("flush_buffer error ret=%d", ret);

			LOGE("flush_buffer index=%d,vaddr=0x%x, paddr=0x%x", index, (uint32_t)v_addr, (uint32_t)p_addr);
		}
	}

	return ret;
}

/////////////////////////////////////////////////////////////////////////////////

/** Close this device */

static camera_device_t *g_cam_device;

static int HAL_camera_device_close(struct hw_device_t* device)
{
    LOGI("%s", __func__);
    if (device) {
        camera_device_t *cam_device = (camera_device_t *)device;
        delete static_cast<SprdCameraHardware *>(cam_device->priv);
        free(cam_device);
        g_cam_device = 0;
    }
#ifdef CONFIG_CAMERA_ISP
	stopispserver();
	ispvideo_RegCameraFunc(1, NULL);
	ispvideo_RegCameraFunc(2, NULL);
	ispvideo_RegCameraFunc(3, NULL);
	ispvideo_RegCameraFunc(4, NULL);
#endif

    return 0;
}

static inline SprdCameraHardware *obj(struct camera_device *dev)
{
    return reinterpret_cast<SprdCameraHardware *>(dev->priv);
}

/** Set the preview_stream_ops to which preview frames are sent */
static int HAL_camera_device_set_preview_window(struct camera_device *dev,
                                                struct preview_stream_ops *buf)
{
    LOGV("%s", __func__);
    return obj(dev)->setPreviewWindow(buf);
}

/** Set the notification and data callbacks */
static void HAL_camera_device_set_callbacks(struct camera_device *dev,
        camera_notify_callback notify_cb,
        camera_data_callback data_cb,
        camera_data_timestamp_callback data_cb_timestamp,
        camera_request_memory get_memory,
        void* user)
{
    LOGV("%s", __func__);
    obj(dev)->setCallbacks(notify_cb, data_cb, data_cb_timestamp,
                           get_memory,
                           user);
}

/**
 * The following three functions all take a msg_type, which is a bitmask of
 * the messages defined in include/ui/Camera.h
 */

/**
 * Enable a message, or set of messages.
 */
static void HAL_camera_device_enable_msg_type(struct camera_device *dev, int32_t msg_type)
{
    LOGV("%s", __func__);
    obj(dev)->enableMsgType(msg_type);
}

/**
 * Disable a message, or a set of messages.
 *
 * Once received a call to disableMsgType(CAMERA_MSG_VIDEO_FRAME), camera
 * HAL should not rely on its client to call releaseRecordingFrame() to
 * release video recording frames sent out by the cameral HAL before and
 * after the disableMsgType(CAMERA_MSG_VIDEO_FRAME) call. Camera HAL
 * clients must not modify/access any video recording frame after calling
 * disableMsgType(CAMERA_MSG_VIDEO_FRAME).
 */
static void HAL_camera_device_disable_msg_type(struct camera_device *dev, int32_t msg_type)
{
    LOGV("%s", __func__);
    obj(dev)->disableMsgType(msg_type);
}

/**
 * Query whether a message, or a set of messages, is enabled.  Note that
 * this is operates as an AND, if any of the messages queried are off, this
 * will return false.
 */
static int HAL_camera_device_msg_type_enabled(struct camera_device *dev, int32_t msg_type)
{
    LOGV("%s", __func__);
    return obj(dev)->msgTypeEnabled(msg_type);
}

/**
 * Start preview mode.
 */
static int HAL_camera_device_start_preview(struct camera_device *dev)
{
    LOGV("%s", __func__);
    return obj(dev)->startPreview();
}

/**
 * Stop a previously started preview.
 */
static void HAL_camera_device_stop_preview(struct camera_device *dev)
{
    LOGV("%s", __func__);
    obj(dev)->stopPreview();
}

/**
 * Returns true if preview is enabled.
 */
static int HAL_camera_device_preview_enabled(struct camera_device *dev)
{
    LOGV("%s", __func__);
    return obj(dev)->previewEnabled();
}

/**
 * Request the camera HAL to store meta data or real YUV data in the video
 * buffers sent out via CAMERA_MSG_VIDEO_FRAME for a recording session. If
 * it is not called, the default camera HAL behavior is to store real YUV
 * data in the video buffers.
 *
 * This method should be called before startRecording() in order to be
 * effective.
 *
 * If meta data is stored in the video buffers, it is up to the receiver of
 * the video buffers to interpret the contents and to find the actual frame
 * data with the help of the meta data in the buffer. How this is done is
 * outside of the scope of this method.
 *
 * Some camera HALs may not support storing meta data in the video buffers,
 * but all camera HALs should support storing real YUV data in the video
 * buffers. If the camera HAL does not support storing the meta data in the
 * video buffers when it is requested to do do, INVALID_OPERATION must be
 * returned. It is very useful for the camera HAL to pass meta data rather
 * than the actual frame data directly to the video encoder, since the
 * amount of the uncompressed frame data can be very large if video size is
 * large.
 *
 * @param enable if true to instruct the camera HAL to store
 *      meta data in the video buffers; false to instruct
 *      the camera HAL to store real YUV data in the video
 *      buffers.
 *
 * @return OK on success.
 */
static int HAL_camera_device_store_meta_data_in_buffers(struct camera_device *dev, int enable)
{
    LOGV("%s", __func__);
    return obj(dev)->storeMetaDataInBuffers(enable);
}

/**
 * Start record mode. When a record image is available, a
 * CAMERA_MSG_VIDEO_FRAME message is sent with the corresponding
 * frame. Every record frame must be released by a camera HAL client via
 * releaseRecordingFrame() before the client calls
 * disableMsgType(CAMERA_MSG_VIDEO_FRAME). After the client calls
 * disableMsgType(CAMERA_MSG_VIDEO_FRAME), it is the camera HAL's
 * responsibility to manage the life-cycle of the video recording frames,
 * and the client must not modify/access any video recording frames.
 */
static int HAL_camera_device_start_recording(struct camera_device *dev)
{
    LOGV("%s", __func__);
    return obj(dev)->startRecording();
}

/**
 * Stop a previously started recording.
 */
static void HAL_camera_device_stop_recording(struct camera_device *dev)
{
    LOGV("%s", __func__);
    obj(dev)->stopRecording();
}

/**
 * Returns true if recording is enabled.
 */
static int HAL_camera_device_recording_enabled(struct camera_device *dev)
{
    LOGV("%s", __func__);
    return obj(dev)->recordingEnabled();
}

/**
 * Release a record frame previously returned by CAMERA_MSG_VIDEO_FRAME.
 *
 * It is camera HAL client's responsibility to release video recording
 * frames sent out by the camera HAL before the camera HAL receives a call
 * to disableMsgType(CAMERA_MSG_VIDEO_FRAME). After it receives the call to
 * disableMsgType(CAMERA_MSG_VIDEO_FRAME), it is the camera HAL's
 * responsibility to manage the life-cycle of the video recording frames.
 */
static void HAL_camera_device_release_recording_frame(struct camera_device *dev,
                                const void *opaque)
{
    LOGV("%s", __func__);
    obj(dev)->releaseRecordingFrame(opaque);
}

/**
 * Start auto focus, the notification callback routine is called with
 * CAMERA_MSG_FOCUS once when focusing is complete. autoFocus() will be
 * called again if another auto focus is needed.
 */
static int HAL_camera_device_auto_focus(struct camera_device *dev)
{
    LOGV("%s", __func__);
    return obj(dev)->autoFocus();
}

/**
 * Cancels auto-focus function. If the auto-focus is still in progress,
 * this function will cancel it. Whether the auto-focus is in progress or
 * not, this function will return the focus position to the default.  If
 * the camera does not support auto-focus, this is a no-op.
 */
static int HAL_camera_device_cancel_auto_focus(struct camera_device *dev)
{
    LOGV("%s", __func__);
    return obj(dev)->cancelAutoFocus();
}

/**
 * Take a picture.
 */
static int HAL_camera_device_take_picture(struct camera_device *dev)
{
    LOGV("%s", __func__);
    return obj(dev)->takePicture();
}

/**
 * Cancel a picture that was started with takePicture. Calling this method
 * when no picture is being taken is a no-op.
 */
static int HAL_camera_device_cancel_picture(struct camera_device *dev)
{
    LOGV("%s", __func__);
    return obj(dev)->cancelPicture();
}

/**
 * Set the camera parameters. This returns BAD_VALUE if any parameter is
 * invalid or not supported.
 */
static int HAL_camera_device_set_parameters(struct camera_device *dev,
                                            const char *parms)
{
    LOGV("%s", __func__);
    String8 str(parms);
    SprdCameraParameters p(str);
    return obj(dev)->setParameters(p);
}

/** Return the camera parameters. */
char *HAL_camera_device_get_parameters(struct camera_device *dev)
{
    LOGV("%s", __func__);
    String8 str;
    SprdCameraParameters parms = obj(dev)->getParameters();
    str = parms.flatten();
    return strdup(str.string());
}

void HAL_camera_device_put_parameters(struct camera_device *dev, char *parms)
{
    LOGV("%s", __func__);
    free(parms);
}

/**
 * Send command to camera driver.
 */
static int HAL_camera_device_send_command(struct camera_device *dev,
                    int32_t cmd, int32_t arg1, int32_t arg2)
{
    LOGV("%s", __func__);
    return obj(dev)->sendCommand(cmd, arg1, arg2);
}

/**
 * Release the hardware resources owned by this object.  Note that this is
 * *not* done in the destructor.
 */
static void HAL_camera_device_release(struct camera_device *dev)
{
    LOGV("%s", __func__);
    obj(dev)->release();
}

/**
 * Dump state of the camera hardware
 */
static int HAL_camera_device_dump(struct camera_device *dev, int fd)
{
    LOGV("%s", __func__);
    return obj(dev)->dump(fd);
}

static int HAL_getNumberOfCameras()
{
	return SprdCameraHardware::getNumberOfCameras();
}

static int HAL_getCameraInfo(int cameraId, struct camera_info *cameraInfo)
{
	return SprdCameraHardware::getCameraInfo(cameraId, cameraInfo);
}

#define SET_METHOD(m) m : HAL_camera_device_##m

static camera_device_ops_t camera_device_ops = {
        SET_METHOD(set_preview_window),
        SET_METHOD(set_callbacks),
        SET_METHOD(enable_msg_type),
        SET_METHOD(disable_msg_type),
        SET_METHOD(msg_type_enabled),
        SET_METHOD(start_preview),
        SET_METHOD(stop_preview),
        SET_METHOD(preview_enabled),
        SET_METHOD(store_meta_data_in_buffers),
        SET_METHOD(start_recording),
        SET_METHOD(stop_recording),
        SET_METHOD(recording_enabled),
        SET_METHOD(release_recording_frame),
        SET_METHOD(auto_focus),
        SET_METHOD(cancel_auto_focus),
        SET_METHOD(take_picture),
        SET_METHOD(cancel_picture),
        SET_METHOD(set_parameters),
        SET_METHOD(get_parameters),
        SET_METHOD(put_parameters),
        SET_METHOD(send_command),
        SET_METHOD(release),
        SET_METHOD(dump),
};

#undef SET_METHOD

static int HAL_IspVideoStartPreview(uint32_t param1, uint32_t param2)
{
	int rtn=0x00;
	SprdCameraHardware * fun_ptr = dynamic_cast<SprdCameraHardware *>((SprdCameraHardware *)g_cam_device->priv);
	if (NULL != fun_ptr)
	{
		rtn=fun_ptr->startPreview();
	}
	return rtn;
}

static int HAL_IspVideoStopPreview(uint32_t param1, uint32_t param2)
{
	int rtn=0x00;
	SprdCameraHardware * fun_ptr = dynamic_cast<SprdCameraHardware *>((SprdCameraHardware *)g_cam_device->priv);
	if (NULL != fun_ptr)
	{
		fun_ptr->stopPreview();
	}
	return rtn;
}

static int HAL_IspVideoSetParam(uint32_t width, uint32_t height)
{
	int rtn=0x00;
	SprdCameraHardware * fun_ptr = dynamic_cast<SprdCameraHardware *>((SprdCameraHardware *)g_cam_device->priv);

	if (NULL != fun_ptr)
	{
		LOGE("ISP_TOOL: HAL_IspVideoSetParam width:%d, height:%d", width, height);
		fun_ptr->setCaptureRawMode(1);
		rtn=fun_ptr->setTakePictureSize(width,height);
	}
	return rtn;
}

static int HAL_IspVideoTakePicture(uint32_t param1, uint32_t param2)
{
	int rtn=0x00;
	SprdCameraHardware * fun_ptr = dynamic_cast<SprdCameraHardware *>((SprdCameraHardware *)g_cam_device->priv);
	if (NULL != fun_ptr)
	{
		rtn=fun_ptr->takePicture();
	}
	return rtn;
}

static int HAL_camera_device_open(const struct hw_module_t* module,
                                  const char *id,
                                  struct hw_device_t** device)
{
    LOGV("%s", __func__);
    GET_START_TIME;

    int cameraId = atoi(id);
    if (cameraId < 0 || cameraId >= HAL_getNumberOfCameras()) {
        LOGE("Invalid camera ID %s", id);
        return -EINVAL;
    }

    if (g_cam_device) {
        if (obj(g_cam_device)->getCameraId() == cameraId) {
            LOGV("returning existing camera ID %s", id);
            goto done;
        } else {
            LOGE("Cannot open camera %d. camera %d is already running!",
                    cameraId, obj(g_cam_device)->getCameraId());
            return -ENOSYS;
        }
    }

    g_cam_device = (camera_device_t *)malloc(sizeof(camera_device_t));
    if (!g_cam_device)
        return -ENOMEM;

    g_cam_device->common.tag     = HARDWARE_DEVICE_TAG;
    g_cam_device->common.version = 1;
    g_cam_device->common.module  = const_cast<hw_module_t *>(module);
    g_cam_device->common.close   = HAL_camera_device_close;

    g_cam_device->ops = &camera_device_ops;

    LOGI("%s: open camera %s", __func__, id);

    //g_cam_device->priv = new SprdCameraHardware(cameraId, g_cam_device);
    g_cam_device->priv = new SprdCameraHardware(cameraId);


#ifdef CONFIG_CAMERA_ISP
	startispserver();
	ispvideo_RegCameraFunc(1, HAL_IspVideoStartPreview);
	ispvideo_RegCameraFunc(2, HAL_IspVideoStopPreview);
	ispvideo_RegCameraFunc(3, HAL_IspVideoTakePicture);
	ispvideo_RegCameraFunc(4, HAL_IspVideoSetParam);
#endif


done:
    *device = (hw_device_t *)g_cam_device;

    LOGI("%s: opened camera %s (%p)", __func__, id, *device);

    return 0;
}

static hw_module_methods_t camera_module_methods = {
            open : HAL_camera_device_open
};

extern "C" {
    struct camera_module HAL_MODULE_INFO_SYM = {
      common : {
          tag           : HARDWARE_MODULE_TAG,
          version_major : 1,
          version_minor : 0,
          id            : CAMERA_HARDWARE_MODULE_ID,
          name          : "Sprd camera HAL",
          author        : "Spreadtrum Corporation",
          methods       : &camera_module_methods,
          dso		:NULL,
          reserved	:{0},
      },
      get_number_of_cameras : HAL_getNumberOfCameras,
      get_camera_info       : HAL_getCameraInfo
    };
}

}

