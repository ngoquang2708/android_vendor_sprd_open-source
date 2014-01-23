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

#define LOG_TAG "SprdCameraHAL2"
#include <utils/Log.h>
#include <math.h>
#include "SprdCameraHWInterface2.h"
#include "SprdOEMCamera.h"
#include <binder/MemoryHeapIon.h>
#include <binder/MemoryHeapBase.h>
#include "ion_sprd.h"

namespace android {

#define HAL_DEBUG_STR     "L %d, %s: "
#define HAL_DEBUG_ARGS    __LINE__,__FUNCTION__

#define HAL_LOGV(format,...) ALOGE(HAL_DEBUG_STR format, HAL_DEBUG_ARGS, ##__VA_ARGS__)
#define HAL_LOGD(format,...) ALOGE(HAL_DEBUG_STR format, HAL_DEBUG_ARGS, ##__VA_ARGS__)
#define HAL_LOGE(format,...) ALOGE(HAL_DEBUG_STR format, HAL_DEBUG_ARGS, ##__VA_ARGS__)

#define SET_PARM(x,y) 	do { \
							camera_set_parm (x, y, NULL, NULL); \
						} while(0)

#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))

static  int s_mem_method ;	 //   0=  physical	address,1=iommu  address

gralloc_module_t const* SprdCameraHWInterface2::m_grallocHal;
SprdCamera2Info * g_Camera2[2] = { NULL, NULL };

status_t addOrSize(camera_metadata_t *request,
        bool sizeRequest,
        size_t *entryCount,
        size_t *dataCount,
        uint32_t tag,
        const void *entryData,
        size_t entryDataCount) {
    status_t res;
    if (!sizeRequest) {
        return add_camera_metadata_entry(request, tag, entryData,
                entryDataCount);
    } else {
        int type = get_camera_metadata_tag_type(tag);
        if (type < 0 ) return BAD_VALUE;
        (*entryCount)++;
        (*dataCount) += calculate_camera_metadata_entry_data_size(type,
                entryDataCount);
//		HAL_LOGD("addorsize datasize=%d",*dataCount);
        return OK;
    }
}

int SprdCameraHWInterface2::ConstructProduceReq(camera_metadata_t **request, bool sizeRequest)
{
	size_t entryCount = 0;
	size_t dataCount = 0;
	status_t ret;

#define ADD_OR_SIZE( tag, data, count ) \
	if ( ( ret = addOrSize(*request, sizeRequest, &entryCount, &dataCount, \
			tag, data, count) ) != OK ) return ret

	/** android.request */
	static const uint8_t afStat = ANDROID_CONTROL_AF_STATE_INACTIVE;
	ADD_OR_SIZE(ANDROID_CONTROL_AF_STATE, &afStat, 1);
	static const int32_t id1 = 0;
	ADD_OR_SIZE(ANDROID_REQUEST_ID, &id1, 1);

	static const int32_t frameCount1 = 0;
	ADD_OR_SIZE(ANDROID_REQUEST_FRAME_COUNT, &frameCount1, 1);

	/** android.control */
	static const uint8_t controlIntent1 = ANDROID_CONTROL_CAPTURE_INTENT_PREVIEW;
	ADD_OR_SIZE(ANDROID_CONTROL_CAPTURE_INTENT, &controlIntent1, 1);

	static const int64_t sensorTime1 = 0;
	ADD_OR_SIZE(ANDROID_SENSOR_TIMESTAMP, &sensorTime1, 1);

	if (sizeRequest) {
		*request = allocate_camera_metadata(entryCount, dataCount);
		if (*request == NULL) {
			HAL_LOGE("Unable to allocate produc req(%d entries, %d bytes extra data)",entryCount, dataCount);
			return NO_MEMORY;
		}
	}
	return OK;
#undef ADD_OR_SIZE
}

SprdCameraHWInterface2::SprdCameraHWInterface2(int cameraId, camera2_device_t *dev, SprdCamera2Info * camera, int *openInvalid):
            m_requestQueueOps(NULL),
            m_frameQueueOps(NULL),
            m_callbackClient(NULL),
            m_halDevice(dev),
            m_focusStat(FOCUS_STAT_INACTIVE),
            mRawHeap(NULL),
            mRawHeapSize(0),
            mPreviewHeapNum(0),
	        m_reqIsProcess(false),
	        m_IsPrvAftPic(false),
	        m_recStopMsg(false),
	        m_dcDircToDvSnap(false),
	        m_halRefreshReq(NULL),
	        mMiscHeapNum(0),
            m_CameraId(cameraId)
{
    int res = 0;

    s_mem_method = MemoryHeapIon::Mm_iommu_is_enabled();
    memset(mPreviewHeapArray_phy, 0, sizeof(mPreviewHeapArray_phy));
	memset(mPreviewHeapArray_vir, 0, sizeof(mPreviewHeapArray_vir));
	memset(mMiscHeapArray, 0, sizeof(mMiscHeapArray));
	memset(mPreviewHeapArray, 0, sizeof(mPreviewHeapArray));
	memset(&m_staticReqInfo,0,sizeof(camera_req_info));
	memset(&m_camCtlInfo,0,sizeof(cam_hal_ctl));
    if (!m_grallocHal) {
        res = hw_get_module(GRALLOC_HARDWARE_MODULE_ID, (const hw_module_t **)&m_grallocHal);
        if (res) {
            HAL_LOGE("ERR(%s):Fail on loading gralloc HAL", __FUNCTION__);
			*openInvalid = 1;
			return;
        }
    }

    m_Camera2 = camera;
	if (CAMERA_SUCCESS != camera_init(cameraId)) {
		HAL_LOGE("(%s): camera init failed. exiting", __FUNCTION__);
		*openInvalid = 1;
		return;
	}
    m_RequestQueueThread = new RequestQueueThread(this);

	if(m_RequestQueueThread == NULL) {
        HAL_LOGE("ERR(%s):create req thread No mem", __FUNCTION__);
		*openInvalid = 1;
		return;
	}

    // Pass 1, calculate size and allocate
    res = ConstructProduceReq(&m_halRefreshReq, true);
    if (res != OK) {
		*openInvalid = 1;
        return ;
    }
    // Pass 2, build request
    res = ConstructProduceReq(&m_halRefreshReq, false);
    if (res != OK) {
        HAL_LOGE("Unable to populate produce req res=%d", res);
		*openInvalid = 1;
        return ;
    }
    for (int i = 0 ; i < STREAM_ID_LAST+1 ; i++) {
         m_subStreams[i].type =  SUBSTREAM_TYPE_NONE;
    }

	mCameraState.camera_state = SPRD_IDLE;
	mCameraState.preview_state = SPRD_INIT;
    mCameraState.capture_state = SPRD_INIT;
	mCameraState.focus_state = SPRD_IDLE;
    m_RequestQueueThread->Start("RequestThread", PRIORITY_DEFAULT, 0);
    HAL_LOGD("(%s):mem=%d EXIT", __FUNCTION__,s_mem_method);
}

SprdCameraHWInterface2::~SprdCameraHWInterface2()
{
    HAL_LOGD("(%s): ENTER", __FUNCTION__);
    this->release();
    HAL_LOGD("(%s): EXIT", __FUNCTION__);
}

SprdCameraHWInterface2::Stream::~Stream()
{
	HAL_LOGD("(%s): ENTER", __FUNCTION__);
	releaseBufQ();
}

void SprdCameraHWInterface2::RequestQueueThread::release()
{
    SetSignal(SIGNAL_THREAD_RELEASE);
}

bool SprdCameraHWInterface2::WaitForCameraStop()
{
	Mutex::Autolock stateLock(&mStateLock);

	if (SPRD_INTERNAL_STOPPING == mCameraState.camera_state) {
	    while(SPRD_INIT != mCameraState.camera_state
				&& SPRD_ERROR != mCameraState.camera_state) {
			HAL_LOGD("WaitForCameraStop: waiting for SPRD_IDLE");
			mStateWait.wait(mStateLock);
			HAL_LOGD("WaitForCameraStop: woke up");
	    }
	}
	return SPRD_INIT == mCameraState.camera_state;
}

void SprdCameraHWInterface2::release()
{
    int i = 0, res;
	Sprd_camera_state  camStatus = (Sprd_camera_state)0;
    HAL_LOGD("ENTER");

	if (m_RequestQueueThread != NULL) {
        m_RequestQueueThread->release();
    }
	if (m_RequestQueueThread != NULL) {
        HAL_LOGD("START Waiting for m_RequestQueueThread termination");
        while (!m_RequestQueueThread->IsTerminated())
            usleep(SIG_WAITING_TICK);
        HAL_LOGD("END Waiting for m_RequestQueueThread termination");
        m_RequestQueueThread = NULL;
    }

	camStatus = getPreviewState();
	if (camStatus != SPRD_IDLE) {
        HAL_LOGE("preview sta=%d",camStatus);
		if (camStatus == SPRD_PREVIEW_IN_PROGRESS) {
	        res = releaseStream(STREAM_ID_PREVIEW);
			if (res != CAMERA_SUCCESS) {
				HAL_LOGE("releaseStream error.");
			}
		}
	}
    if (m_Stream[STREAM_ID_CAPTURE - 1] != NULL) {
		freeCaptureMem();
		memset(&m_subStreams[STREAM_ID_JPEG], 0, sizeof(substream_parameters_t));
		if (m_Stream[STREAM_ID_CAPTURE - 1]->detachSubStream(STREAM_ID_JPEG) != NO_ERROR) {
			HAL_LOGE(" substream detach failed.");
		}
		if (m_Stream[STREAM_ID_CAPTURE - 1]->m_numRegisteredStream > 1) {
		   HAL_LOGE("substream detach not over. num(%d)",
					m_Stream[STREAM_ID_CAPTURE - 1]->m_numRegisteredStream);
		}
		m_Stream[STREAM_ID_CAPTURE - 1] = NULL;
    }
	camStatus = getCameraState();
	if (camStatus != SPRD_IDLE) {
        HAL_LOGE("ERR stopping camera sta=%d",camStatus);
		return;
	}
    setCameraState(SPRD_INTERNAL_STOPPING, STATE_CAMERA);

	HAL_LOGD("stopping camera.");
	if (CAMERA_SUCCESS != camera_stop(camera_cb, this)) {
		setCameraState(SPRD_ERROR, STATE_CAMERA);
		HAL_LOGE("fail to camera_stop().");
		return;
	}

	WaitForCameraStop();
	if (m_halRefreshReq) {
		free(m_halRefreshReq);
		m_halRefreshReq = NULL;
	}
	if(g_Camera2[m_CameraId]) {
		free(g_Camera2[m_CameraId]);
		g_Camera2[m_CameraId] = NULL;
	}
    HAL_LOGD("EXIT");
}
int SprdCameraHWInterface2::setRequestQueueSrcOps(const camera2_request_queue_src_ops_t *request_src_ops)
{
    HAL_LOGV("start");
    if ((NULL != request_src_ops) && (NULL != request_src_ops->dequeue_request)
            && (NULL != request_src_ops->free_request) && (NULL != request_src_ops->request_count)) {
        m_requestQueueOps = (camera2_request_queue_src_ops_t*)request_src_ops;
		HAL_LOGV("end");
        return 0;
    } else {
        HAL_LOGE("NULL arguments");
        return 1;
    }
}
//put reqs frm srv into hal req_queue
int SprdCameraHWInterface2::notifyRequestQueueNotEmpty()
{
	int reqNum = 0;
	camera_metadata_t *curReq = NULL;
	camera_metadata_t *tmpReq = NULL;
	int ret;
	Mutex::Autolock lock(m_requestMutex);

    if ((NULL == m_frameQueueOps)|| (NULL == m_requestQueueOps)) {
        HAL_LOGE("queue ops NULL.ignoring request");
        return 0;
    }
    reqNum = m_ReqQueue.size();

	if (m_reqIsProcess == true) {
		reqNum++;
	}
    if (reqNum >= MAX_REQUEST_NUM) {
		HAL_LOGD("queue is full(%d). ignoring request", reqNum);
		return 0;
	} else {
		m_requestQueueOps->dequeue_request(m_requestQueueOps, &curReq);
		if (!curReq) {
            HAL_LOGD("req is NULL");
			return 0;
	    }
	    reqNum++;
	    HAL_LOGD("currep=%p reqnum=%d",curReq,reqNum);
        m_ReqQueue.push_back(curReq);
		while (curReq) {
		   m_requestQueueOps->dequeue_request(m_requestQueueOps, &curReq);
		   if (curReq) {
				if (reqNum >= MAX_REQUEST_NUM) {
					HAL_LOGD("request queue is full,discard this request.");
				} else {
					reqNum++;
					m_ReqQueue.push_back(curReq);
					HAL_LOGD("push a request to queue.");
				}
		   }
		   HAL_LOGD("%s Srv req %p",__FUNCTION__,curReq);
		}
		HAL_LOGD("srv clear flag processing=%d",m_reqIsProcess);
		if (!m_reqIsProcess) {
            m_RequestQueueThread->SetSignal(SIGNAL_REQ_THREAD_REQ_Q_NOT_EMPTY);
		}
		return 0;
	}
}

int SprdCameraHWInterface2::setFrameQueueDstOps(const camera2_frame_queue_dst_ops_t *frame_dst_ops)
{
    HAL_LOGV("start");
    if ((NULL != frame_dst_ops) && (NULL != frame_dst_ops->dequeue_frame)
            && (NULL != frame_dst_ops->cancel_frame) && (NULL !=frame_dst_ops->enqueue_frame)) {
        m_frameQueueOps = (camera2_frame_queue_dst_ops_t *)frame_dst_ops;
    HAL_LOGV("end");
        return 0;
    } else {
        HAL_LOGE("NULL arguments");
        return 1;
    }
}

int SprdCameraHWInterface2::getInProgressCount()
{
	uint8_t ProcNum = 0;
	Mutex::Autolock lock(m_requestMutex);

	if (m_reqIsProcess) {
	   ProcNum++;
	}

	ProcNum += m_ReqQueue.size();
	if (SPRD_WAITING_RAW == getCaptureState() || SPRD_WAITING_JPEG == getCaptureState()) {
	   ProcNum++ ;
	}
	HAL_LOGV("ProcNum=%d.",ProcNum);
    return ProcNum;
}

int SprdCameraHWInterface2::flushCapturesInProgress()
{
    return 0;
}

static int initCamera2Info(int cameraId)
{
    if (cameraId == 0) {
	    if (!g_Camera2[0]) {
            HAL_LOGE("back not alloc!");
			return 1;
		}

		g_Camera2[0]->sensorW             = BACK_SENSOR_ORIG_WIDTH;
	    g_Camera2[0]->sensorH             = BACK_SENSOR_ORIG_HEIGHT;
	    g_Camera2[0]->sensorRawW          = BACK_SENSOR_ORIG_WIDTH;
	    g_Camera2[0]->sensorRawH          = BACK_SENSOR_ORIG_HEIGHT;
	    g_Camera2[0]->numPreviewResolution = ARRAY_SIZE(PreviewResolutionSensorBack)/2;
	    g_Camera2[0]->PreviewResolutions   = PreviewResolutionSensorBack;
	    g_Camera2[0]->numJpegResolution   = ARRAY_SIZE(jpegResolutionSensorBack)/2;
	    g_Camera2[0]->jpegResolutions     = jpegResolutionSensorBack;
	    g_Camera2[0]->minFocusDistance    = 0.1f;
	    g_Camera2[0]->focalLength         = 3.43f;
	    g_Camera2[0]->aperture            = 2.7f;
	    g_Camera2[0]->fnumber             = 2.7f;
	    g_Camera2[0]->availableAfModes    = availableAfModesSensorBack;
	    g_Camera2[0]->numAvailableAfModes = ARRAY_SIZE(availableAfModesSensorBack);
	    g_Camera2[0]->sceneModeOverrides  = sceneModeOverridesSensorBack;
	    g_Camera2[0]->numSceneModeOverrides = ARRAY_SIZE(sceneModeOverridesSensorBack);
	    g_Camera2[0]->availableAeModes    = availableAeModesSensorBack;
	    g_Camera2[0]->numAvailableAeModes = ARRAY_SIZE(availableAeModesSensorBack);
	} else if (cameraId == 1) {
	    if (!g_Camera2[1]) {
            HAL_LOGE("%s back not alloc!",__FUNCTION__);
			return 1;
		}

		g_Camera2[1]->sensorW             = FRONT_SENSOR_ORIG_WIDTH;
	    g_Camera2[1]->sensorH             = FRONT_SENSOR_ORIG_HEIGHT;
	    g_Camera2[1]->sensorRawW          = FRONT_SENSOR_ORIG_WIDTH;
	    g_Camera2[1]->sensorRawH          = FRONT_SENSOR_ORIG_HEIGHT;
	    g_Camera2[1]->numPreviewResolution = ARRAY_SIZE(PreviewResolutionSensorFront)/2;
	    g_Camera2[1]->PreviewResolutions   = PreviewResolutionSensorFront;
	    g_Camera2[1]->numJpegResolution   = ARRAY_SIZE(jpegResolutionSensorFront)/2;
	    g_Camera2[1]->jpegResolutions     = jpegResolutionSensorFront;
	    g_Camera2[1]->minFocusDistance    = 0.1f;
	    g_Camera2[1]->focalLength         = 3.43f;
	    g_Camera2[1]->aperture            = 2.7f;
	    g_Camera2[1]->fnumber             = 2.7f;
	    g_Camera2[1]->availableAfModes    = availableAfModesSensorBack;
	    g_Camera2[1]->numAvailableAfModes = ARRAY_SIZE(availableAfModesSensorBack);
	    g_Camera2[1]->sceneModeOverrides  = sceneModeOverridesSensorBack;
	    g_Camera2[1]->numSceneModeOverrides = ARRAY_SIZE(sceneModeOverridesSensorBack);
	    g_Camera2[1]->availableAeModes    = availableAeModesSensorBack;
	    g_Camera2[1]->numAvailableAeModes = ARRAY_SIZE(availableAeModesSensorBack);
	}
	return 0;
}

status_t SprdCameraHWInterface2::CamconstructDefaultRequest(
	    SprdCamera2Info *camHal,
        int request_template,
        camera_metadata_t **request,
        bool sizeRequest) {

    size_t entryCount = 0;
    size_t dataCount = 0;
    status_t ret;

#define ADD_OR_SIZE( tag, data, count ) \
    if ( ( ret = addOrSize(*request, sizeRequest, &entryCount, &dataCount, \
            tag, data, count) ) != OK ) return ret

    static const int64_t USEC = 1000LL;
    static const int64_t MSEC = USEC * 1000LL;
    static const int64_t SEC = MSEC * 1000LL;

    /** android.request */

    static const uint8_t metadataMode = ANDROID_REQUEST_METADATA_MODE_NONE;

    ADD_OR_SIZE(ANDROID_REQUEST_METADATA_MODE, &metadataMode, 1);

    static const int32_t id = 0;
    ADD_OR_SIZE(ANDROID_REQUEST_ID, &id, 1);

    static const int32_t frameCount = 0;
    ADD_OR_SIZE(ANDROID_REQUEST_FRAME_COUNT, &frameCount, 1);

    // OUTPUT_STREAMS set by user
    entryCount += 1;
    dataCount += 5; // TODO: Should be maximum stream number

    /** android.lens */

    static const float focusDistance = 0;
    ADD_OR_SIZE(ANDROID_LENS_FOCUS_DISTANCE, &focusDistance, 1);

    ADD_OR_SIZE(ANDROID_LENS_APERTURE, &camHal->aperture, 1);

    ADD_OR_SIZE(ANDROID_LENS_FOCAL_LENGTH, &camHal->focalLength, 1);

    static const float filterDensity = 0;
    ADD_OR_SIZE(ANDROID_LENS_FILTER_DENSITY, &filterDensity, 1);

    static const uint8_t opticalStabilizationMode =
            ANDROID_LENS_OPTICAL_STABILIZATION_MODE_OFF;
    ADD_OR_SIZE(ANDROID_LENS_OPTICAL_STABILIZATION_MODE,
            &opticalStabilizationMode, 1);

    /** android.sensor */
    static const int64_t frameDuration = 33333333L; // 1/30 s
    ADD_OR_SIZE(ANDROID_SENSOR_FRAME_DURATION, &frameDuration, 1);


    /** android.flash */
    static const uint8_t flashMode = ANDROID_FLASH_MODE_OFF;
    ADD_OR_SIZE(ANDROID_FLASH_MODE, &flashMode, 1);

    static const uint8_t flashPower = 10;
    ADD_OR_SIZE(ANDROID_FLASH_FIRING_POWER, &flashPower, 1);

    static const int64_t firingTime = 0;
    ADD_OR_SIZE(ANDROID_FLASH_FIRING_TIME, &firingTime, 1);

    /** Processing block modes */
    uint8_t hotPixelMode = 0;
    uint8_t demosaicMode = 0;
    uint8_t noiseMode = 0;
    uint8_t shadingMode = 0;
    uint8_t geometricMode = 0;
    uint8_t colorMode = 0;
    uint8_t tonemapMode = 0;
    uint8_t edgeMode = 0;
    uint8_t vstabMode = ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_OFF;

	switch (request_template) {
    case CAMERA2_TEMPLATE_VIDEO_SNAPSHOT:
        vstabMode = ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_ON;
        // fall-through
    case CAMERA2_TEMPLATE_STILL_CAPTURE:
        // fall-through
    case CAMERA2_TEMPLATE_ZERO_SHUTTER_LAG:
        hotPixelMode = ANDROID_HOT_PIXEL_MODE_HIGH_QUALITY;
        demosaicMode = ANDROID_DEMOSAIC_MODE_HIGH_QUALITY;
        noiseMode = ANDROID_NOISE_REDUCTION_MODE_HIGH_QUALITY;
        shadingMode = ANDROID_SHADING_MODE_HIGH_QUALITY;
        geometricMode = ANDROID_GEOMETRIC_MODE_HIGH_QUALITY;
        colorMode = ANDROID_COLOR_CORRECTION_MODE_HIGH_QUALITY;
        tonemapMode = ANDROID_TONEMAP_MODE_HIGH_QUALITY;
        edgeMode = ANDROID_EDGE_MODE_HIGH_QUALITY;
        break;
    case CAMERA2_TEMPLATE_VIDEO_RECORD:
        vstabMode = ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_ON;
        // fall-through
    case CAMERA2_TEMPLATE_PREVIEW:
        // fall-through
    default:
        hotPixelMode = ANDROID_HOT_PIXEL_MODE_FAST;
        demosaicMode = ANDROID_DEMOSAIC_MODE_FAST;
        noiseMode = ANDROID_NOISE_REDUCTION_MODE_FAST;
        shadingMode = ANDROID_SHADING_MODE_FAST;
        geometricMode = ANDROID_GEOMETRIC_MODE_FAST;
        colorMode = ANDROID_COLOR_CORRECTION_MODE_FAST;
        tonemapMode = ANDROID_TONEMAP_MODE_FAST;
        edgeMode = ANDROID_EDGE_MODE_FAST;
        break;
    }
    ADD_OR_SIZE(ANDROID_HOT_PIXEL_MODE, &hotPixelMode, 1);
    ADD_OR_SIZE(ANDROID_DEMOSAIC_MODE, &demosaicMode, 1);
    ADD_OR_SIZE(ANDROID_NOISE_REDUCTION_MODE, &noiseMode, 1);
    ADD_OR_SIZE(ANDROID_SHADING_MODE, &shadingMode, 1);
    ADD_OR_SIZE(ANDROID_GEOMETRIC_MODE, &geometricMode, 1);
    ADD_OR_SIZE(ANDROID_COLOR_CORRECTION_MODE, &colorMode, 1);
    ADD_OR_SIZE(ANDROID_TONEMAP_MODE, &tonemapMode, 1);
    ADD_OR_SIZE(ANDROID_EDGE_MODE, &edgeMode, 1);
    ADD_OR_SIZE(ANDROID_CONTROL_VIDEO_STABILIZATION_MODE, &vstabMode, 1);

    /** android.noise */
    static const uint8_t noiseStrength = 5;
    ADD_OR_SIZE(ANDROID_NOISE_REDUCTION_STRENGTH, &noiseStrength, 1);

    /** android.color */
    static const float colorTransform[9] = {
        1.0f, 0.f, 0.f,
        0.f, 1.f, 0.f,
        0.f, 0.f, 1.f
    };
    ADD_OR_SIZE(ANDROID_COLOR_CORRECTION_TRANSFORM, colorTransform, 9);

    /** android.tonemap */
    static const float tonemapCurve[4] = {
        0.f, 0.f,
        1.f, 1.f
    };
    ADD_OR_SIZE(ANDROID_TONEMAP_CURVE_RED, tonemapCurve, 32); // sungjoong
    ADD_OR_SIZE(ANDROID_TONEMAP_CURVE_GREEN, tonemapCurve, 32);
    ADD_OR_SIZE(ANDROID_TONEMAP_CURVE_BLUE, tonemapCurve, 32);

    /** android.edge */
    static const uint8_t edgeStrength = 5;
    ADD_OR_SIZE(ANDROID_EDGE_STRENGTH, &edgeStrength, 1);

    /** android.scaler */
    int32_t cropRegion[3] = {
        0, 0, camHal->sensorW
    };
    ADD_OR_SIZE(ANDROID_SCALER_CROP_REGION, cropRegion, 3);

    /** android.jpeg */
    static const int32_t jpegQuality = 100;
    ADD_OR_SIZE(ANDROID_JPEG_QUALITY, &jpegQuality, 1);

    static const int32_t thumbnailSize[2] = {
        JPEG_THUMBNAIL_WIDTH, JPEG_THUMBNAIL_HEIGHT
    };
    ADD_OR_SIZE(ANDROID_JPEG_THUMBNAIL_SIZE, thumbnailSize, 2);

    static const int32_t thumbnailQuality = 100;
    ADD_OR_SIZE(ANDROID_JPEG_THUMBNAIL_QUALITY, &thumbnailQuality, 1);

    static const double gpsCoordinates[3] = {
        0, 0, 0
    };
    ADD_OR_SIZE(ANDROID_JPEG_GPS_COORDINATES, gpsCoordinates, 3);

    static const uint8_t gpsProcessingMethod[32] = "None";
    ADD_OR_SIZE(ANDROID_JPEG_GPS_PROCESSING_METHOD, gpsProcessingMethod, 32);

    static const int64_t gpsTimestamp = 0;
    ADD_OR_SIZE(ANDROID_JPEG_GPS_TIMESTAMP, &gpsTimestamp, 1);

    static const int32_t jpegOrientation = 0;
    ADD_OR_SIZE(ANDROID_JPEG_ORIENTATION, &jpegOrientation, 1);

    /** android.stats */
    static const uint8_t faceDetectMode = ANDROID_STATISTICS_FACE_DETECT_MODE_FULL;
    ADD_OR_SIZE(ANDROID_STATISTICS_FACE_DETECT_MODE, &faceDetectMode, 1);

    static const uint8_t histogramMode = ANDROID_STATISTICS_HISTOGRAM_MODE_OFF;
    ADD_OR_SIZE(ANDROID_STATISTICS_HISTOGRAM_MODE, &histogramMode, 1);

    static const uint8_t sharpnessMapMode = ANDROID_STATISTICS_HISTOGRAM_MODE_OFF;
    ADD_OR_SIZE(ANDROID_STATISTICS_SHARPNESS_MAP_MODE, &sharpnessMapMode, 1);

    /** android.control */
    uint8_t controlIntent = 0;
    switch (request_template) {
    case CAMERA2_TEMPLATE_PREVIEW:
        controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_PREVIEW;
        break;
    case CAMERA2_TEMPLATE_STILL_CAPTURE:
        controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_STILL_CAPTURE;
        break;
    case CAMERA2_TEMPLATE_VIDEO_RECORD:
        controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_RECORD;
        break;
    case CAMERA2_TEMPLATE_VIDEO_SNAPSHOT:
        controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_SNAPSHOT;
        break;
    case CAMERA2_TEMPLATE_ZERO_SHUTTER_LAG:
        controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_ZERO_SHUTTER_LAG;
        break;
    default:
        controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_CUSTOM;
        break;
    }
    ADD_OR_SIZE(ANDROID_CONTROL_CAPTURE_INTENT, &controlIntent, 1);

    static const uint8_t controlMode = ANDROID_CONTROL_MODE_AUTO;
    ADD_OR_SIZE(ANDROID_CONTROL_MODE, &controlMode, 1);

    static const uint8_t effectMode = ANDROID_CONTROL_EFFECT_MODE_OFF;
    ADD_OR_SIZE(ANDROID_CONTROL_EFFECT_MODE, &effectMode, 1);

    static const uint8_t sceneMode = ANDROID_CONTROL_SCENE_MODE_ACTION;//ANDROID_CONTROL_SCENE_MODE_UNSUPPORTED
    ADD_OR_SIZE(ANDROID_CONTROL_SCENE_MODE, &sceneMode, 1);

    static const uint8_t aeMode = ANDROID_CONTROL_AE_MODE_ON;
    ADD_OR_SIZE(ANDROID_CONTROL_AE_MODE, &aeMode, 1);

    int32_t controlRegions[5] = {
        0, 0, camHal->sensorW, camHal->sensorH, 1000
    };
    ADD_OR_SIZE(ANDROID_CONTROL_AE_REGIONS, controlRegions, 5);

    static const int32_t aeExpCompensation = 0;
    ADD_OR_SIZE(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION, &aeExpCompensation, 1);

    static const int32_t aeTargetFpsRange[2] = {
        15, 30
    };
    ADD_OR_SIZE(ANDROID_CONTROL_AE_TARGET_FPS_RANGE, aeTargetFpsRange, 2);

    static const uint8_t aeAntibandingMode =
            ANDROID_CONTROL_AE_ANTIBANDING_MODE_AUTO;
    ADD_OR_SIZE(ANDROID_CONTROL_AE_ANTIBANDING_MODE, &aeAntibandingMode, 1);

    static const uint8_t awbMode =
            ANDROID_CONTROL_AWB_MODE_AUTO;
    ADD_OR_SIZE(ANDROID_CONTROL_AWB_MODE, &awbMode, 1);

    ADD_OR_SIZE(ANDROID_CONTROL_AWB_REGIONS, controlRegions, 5);

    uint8_t afMode = 0;
    switch (request_template) {
    case CAMERA2_TEMPLATE_PREVIEW:
        afMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE;
        break;
    case CAMERA2_TEMPLATE_STILL_CAPTURE:
        afMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE;
        break;
    case CAMERA2_TEMPLATE_VIDEO_RECORD:
        afMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO;
        break;
    case CAMERA2_TEMPLATE_VIDEO_SNAPSHOT:
        afMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO;
        break;
    case CAMERA2_TEMPLATE_ZERO_SHUTTER_LAG:
        afMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE;
        break;
    default:
        afMode = ANDROID_CONTROL_AF_MODE_AUTO;
        break;
    }
    ADD_OR_SIZE(ANDROID_CONTROL_AF_MODE, &afMode, 1);
    ADD_OR_SIZE(ANDROID_CONTROL_AF_REGIONS, controlRegions, 5);

    if (sizeRequest) {
        HAL_LOGV("Allocating %d entries, %d extra bytes for "
                "request template type %d",
                entryCount, dataCount, request_template);
        *request = allocate_camera_metadata(entryCount, dataCount);
        if (*request == NULL) {
            HAL_LOGE("Unable to allocate new request template type %d "
                    "(%d entries, %d bytes extra data)", request_template,
                    entryCount, dataCount);
            return NO_MEMORY;
        }
    }
    return OK;
#undef ADD_OR_SIZE
}

int SprdCameraHWInterface2::ConstructDefaultRequest(int request_template, camera_metadata_t **request)
{
    HAL_LOGD("making template (%d) ", request_template);

    if (request == NULL) return BAD_VALUE;
    if (request_template < 0 || request_template >= CAMERA2_TEMPLATE_COUNT) {
        return BAD_VALUE;
    }
    status_t res;
    // Pass 1, calculate size and allocate
    res = CamconstructDefaultRequest(m_Camera2, request_template,
            request,
            true);
    if (res != OK) {
        return res;
    }
    // Pass 2, build request
    res = CamconstructDefaultRequest(m_Camera2, request_template,
            request,
            false);
    if (res != OK) {
        HAL_LOGE("Unable to populate new request for template %d",
                request_template);
    }

    return res;
}

bool SprdCameraHWInterface2::isSupportedResolution(SprdCamera2Info *camHal, int width, int height)
{
    int i;
    for (i = 0 ; i < camHal->numPreviewResolution ; i++) {
        if (camHal->PreviewResolutions[2*i] == width
                && camHal->PreviewResolutions[2*i+1] == height) {
            return true;
        }
    }
    return false;
}

bool SprdCameraHWInterface2::isSupportedJpegResolution(SprdCamera2Info *camHal, int width, int height)
{
    int i;
    for (i = 0 ; i < camHal->numJpegResolution ; i++) {
        if (camHal->jpegResolutions[2*i] == width
                && camHal->jpegResolutions[2*i+1] == height) {
            return true;
        }
    }
    return false;
}

status_t SprdCameraHWInterface2::Stream::attachSubStream(int stream_id, int priority)
{
    HAL_LOGV("substream_id(%d)", stream_id);
    int index;
    bool found = false;

    for (index = 0 ; index < NUM_MAX_SUBSTREAM ; index++) {
        if (m_attachedSubStreams[index].streamId == -1) {
            found = true;
            break;
        }
    }
    if (!found)
    {
        HAL_LOGD("No idle index (%d)", index);
        return NO_MEMORY;
    }
    m_attachedSubStreams[index].streamId = stream_id;
    m_attachedSubStreams[index].priority = priority;
    m_numRegisteredStream++;
    return NO_ERROR;
}

status_t SprdCameraHWInterface2::Stream::detachSubStream(int stream_id)
{
    HAL_LOGV("substream_id(%d)", stream_id);
    int index;
    bool found = false;

    for (index = 0 ; index < NUM_MAX_SUBSTREAM ; index++) {
        if (m_attachedSubStreams[index].streamId == stream_id) {
            found = true;
            break;
        }
    }
    if (!found) {
        return BAD_VALUE;
    }
    m_attachedSubStreams[index].streamId = -1;
    m_attachedSubStreams[index].priority = 0;
    m_numRegisteredStream--;
    return NO_ERROR;
}

int SprdCameraHWInterface2::Callback_AllocCapturePmem(void* handle, unsigned int size, unsigned int *addr_phy, unsigned int *addr_vir)
{
	HAL_LOGD("Callback_AllocCapturePmem size = %d", size);
	MemoryHeapIon *pHeapIon = NULL;
	SprdCameraHWInterface2* camera = (SprdCameraHWInterface2*)handle;
	if (camera == NULL) {
		return -1;
	}
	if (camera->mMiscHeapNum >= MAX_MISCHEAP_NUM) {
		return -1;
	}
	if(s_mem_method == 0)
		pHeapIon = new MemoryHeapIon("/dev/ion", size ,0 , (1<<31) | ION_HEAP_ID_MASK_MM);
	else
		pHeapIon = new MemoryHeapIon("/dev/ion", size ,0 , (1<<31) | ION_HEAP_ID_MASK_SYSTEM);
	if (pHeapIon == NULL) {
		return -1;
	}
	if (NULL == pHeapIon->getBase() || 0xffffffff == (uint32_t)pHeapIon->getBase()) {
		return -1;
	}
	if(s_mem_method == 0){
		pHeapIon->get_phy_addr_from_ion((int*)addr_phy, (int*)&size);
	} else {
		pHeapIon->get_mm_iova((int*)addr_phy, (int*)&size);
	}
	*addr_vir = (int)(pHeapIon->getBase());
	camera->mMiscHeapArray[camera->mMiscHeapNum]->phys_addr = *addr_phy;
	camera->mMiscHeapArray[camera->mMiscHeapNum]->phys_size = size;
	camera->mMiscHeapArray[camera->mMiscHeapNum++]->ion_heap = pHeapIon;

	HAL_LOGD("mMiscHeapNum = %d", camera->mMiscHeapNum);

	return 0;
}

int SprdCameraHWInterface2::Callback_FreeCapturePmem(void* handle)
{
	SprdCameraHWInterface2* camera = (SprdCameraHWInterface2*)handle;
	if (camera == NULL) {
		return -1;
	}

	HAL_LOGD("mMiscHeapNum = %d", camera->mMiscHeapNum);

	uint32_t i;
	for (i=0; i<camera->mMiscHeapNum; i++) {
		MemoryHeapIon *pHeapIon = camera->mMiscHeapArray[i]->ion_heap;
		if (pHeapIon != NULL) {
			if (s_mem_method != 0){
				pHeapIon->free_mm_iova(camera->mMiscHeapArray[i]->phys_addr, camera->mMiscHeapArray[i]->phys_size);
			}
			delete pHeapIon;
		}
		camera->mMiscHeapArray[i] = NULL;
	}
	camera->mMiscHeapNum = 0;

	return 0;
}

sprd_camera_memory_t* SprdCameraHWInterface2::GetCachePmem(int buf_size, int num_bufs)
{
    int paddr, psize;
    int  acc = buf_size * num_bufs;
	MemoryHeapIon *pHeapIon = NULL;
	uint8_t tmp = 0;
	sprd_camera_memory_t *memory = (sprd_camera_memory_t *)malloc(sizeof(*memory));
	if(NULL == memory) {
		HAL_LOGE("Fail to GetCachePmem, memory is NULL.");
		return NULL;
	}
	memset(memory, 0, sizeof(*memory));
	if(s_mem_method == 0)
		pHeapIon = new MemoryHeapIon("/dev/ion", acc ,0 , (1<<31) | ION_HEAP_ID_MASK_MM);//ION_HEAP_CARVEOUT_MASK
	else
		pHeapIon = new MemoryHeapIon("/dev/ion", acc ,0 , (1<<31) | ION_HEAP_ID_MASK_SYSTEM);//ION_HEAP_SYSTEM_MASK
    if (pHeapIon == NULL) {
        HAL_LOGE("Failed to alloc cap pmem (%d)", acc);
        goto getpmem_end;
    }
	if (s_mem_method == 0){
		pHeapIon->get_phy_addr_from_ion(&paddr, &psize);
	} else {
		pHeapIon->get_mm_iova(&paddr, &psize);
	}
	memory->ion_heap = pHeapIon;
	memory->phys_addr = paddr;
	memory->phys_size = psize;
	memory->data = pHeapIon->getBase();
	if (memory->data == NULL || (uint32_t)(memory->data) == 0xffffffff) {
       HAL_LOGE("Failed to alloc cap virtadd");
       goto getpmem_end;
    }
    HAL_LOGD("phys_addr 0x%x, data: %p,phys_size: 0x%x.",
                            memory->phys_addr,memory->data, memory->phys_size);
getpmem_end:
	return memory;
}

bool SprdCameraHWInterface2::allocateCaptureMem(void)
{
	uint32_t buffer_size = 0;

	buffer_size = camera_get_size_align_page(mRawHeapSize);
	HAL_LOGD("Heapsize=%d align size = %d . count %d ",mRawHeapSize, buffer_size, kRawBufferCount);

	mRawHeap = GetCachePmem(buffer_size, kRawBufferCount);
	if (NULL == mRawHeap) {
		HAL_LOGE("allocateCaptureMem: Fail to GetPmem mRawHeap.");
		goto allocate_capture_mem_failed;
	}
	if (NULL == mRawHeap->data) {
		HAL_LOGE("allocateCaptureMem: Fail to GetPmem mRawHeap virtadd.");
		goto allocate_capture_mem_failed;
	}

	HAL_LOGD("allocateCaptureMem: X");
	return true;

allocate_capture_mem_failed:
	freeCaptureMem();

	return false;
}

void SprdCameraHWInterface2::freeCaptureMem()
{
    uint32_t i;
	HAL_LOGD("mRawHeap %p",mRawHeap);
	if (mRawHeap) {
		if(mRawHeap->ion_heap != NULL) {
			//mRawHeap->ion_heap.clear();
			if(s_mem_method!=0){
				mRawHeap->ion_heap->free_mm_iova(mRawHeap->phys_addr, mRawHeap->phys_size);
			}
			delete mRawHeap->ion_heap;
			mRawHeap->ion_heap = NULL;
		}
		free(mRawHeap);
	    mRawHeap = NULL;
	}
    mRawHeapSize = 0;

    for (i=0; i<mMiscHeapNum; i++) {
        MemoryHeapIon *pHeapIon = mMiscHeapArray[i]->ion_heap;
        if (pHeapIon != NULL) {
            //pHeapIon.clear();
            if (s_mem_method != 0) {
				pHeapIon->free_mm_iova(mMiscHeapArray[i]->phys_addr, mMiscHeapArray[i]->phys_size);
			}
			delete pHeapIon;
        }
        mMiscHeapArray[i] = NULL;
    }
    mMiscHeapNum = 0;
}

bool SprdCameraHWInterface2::allocatePreviewMem()
{
	uint32_t i = 0;
	stream_parameters_t     *targetStreamParms = &(m_Stream[STREAM_ID_PREVIEW - 1]->m_parameters);
	uint32_t buffer_size = camera_get_size_align_page((targetStreamParms->width * targetStreamParms->height * 3)/2);
	int PreviewHeapNum = 0;
	mPreviewHeapNum = kPreviewBufferCount;
	SET_PARM(CAMERA_PARM_SENSOR_ROTATION, 0);
	if (camera_get_rot_set()) {
		/* allocate more buffer for rotation */
		mPreviewHeapNum += kPreviewRotBufferCount;
	}

	for (; i < mPreviewHeapNum; i++) {
		mPreviewHeapArray[i] = GetCachePmem(buffer_size, 1);
		if (NULL == mPreviewHeapArray[i]) {
			HAL_LOGE("%s: Fail to GetPmem index=%d",__FUNCTION__,i);
			goto allocate_preview_mem_failed;
		}
		if (NULL == mPreviewHeapArray[i]->data) {
			HAL_LOGE("Fail to GetPmem index=%d virtadd",i);
			goto allocate_preview_mem_failed;
		}
		mPreviewHeapArray_phy[i] = (int32_t)(mPreviewHeapArray[i]->phys_addr);
		mPreviewHeapArray_vir[i] = (int32_t)(mPreviewHeapArray[i]->data);
	}
	return true;
allocate_preview_mem_failed:
	freePreviewMem(i+1);

	return false;
}

void SprdCameraHWInterface2::freePreviewMem(int num)
{
	int i = 0;
	HAL_LOGD("bufnum=%d",num);
	for(;i < num; i++) {
		if (mPreviewHeapArray[i]) {
			if(mPreviewHeapArray[i]->ion_heap != NULL) {
				if(s_mem_method != 0){
					mPreviewHeapArray[i]->ion_heap->free_mm_iova(mPreviewHeapArray[i]->phys_addr, mPreviewHeapArray[i]->phys_size);
				}
				//mPreviewHeapArray[i]->ion_heap.clear();
				delete mPreviewHeapArray[i]->ion_heap;
				mPreviewHeapArray[i]->ion_heap = NULL;
			}
			free(mPreviewHeapArray[i]);
			mPreviewHeapArray[i] = NULL;
		}
	}
	mPreviewHeapNum = 0;
}

int SprdCameraHWInterface2::allocateStream(uint32_t width, uint32_t height, int format, const camera2_stream_ops_t *stream_ops,
                                    uint32_t *stream_id, uint32_t *format_actual, uint32_t *usage, uint32_t *max_buffers)
{
    HAL_LOGD("stream width(%d) height(%d) format(%x)", width, height, format);
    stream_parameters_t *newParameters;
    substream_parameters_t *subParameters;
    status_t res;

    if ((format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED || format == CAMERA2_HAL_PIXEL_FORMAT_OPAQUE
		|| format == HAL_PIXEL_FORMAT_YCrCb_420_SP)  &&
            isSupportedResolution(m_Camera2, width, height)) {
		if (m_Stream[STREAM_ID_PREVIEW - 1] == NULL) {
            *stream_id = STREAM_ID_PREVIEW;//temporate preview thread created on oem, so new streamthread object,but not run.

            m_Stream[STREAM_ID_PREVIEW - 1]  = new Stream(this, *stream_id);
            if (m_Stream[STREAM_ID_PREVIEW - 1] == NULL) {
                HAL_LOGD("new preview stream fail due to no mem");
				return 1;
			}

            *format_actual                      = HAL_PIXEL_FORMAT_YCrCb_420_SP;
            if(s_mem_method == 0) {
				*usage = GRALLOC_USAGE_SW_WRITE_OFTEN | GRALLOC_USAGE_CAMERA_BUFFER;
			  } else {
					*usage = GRALLOC_USAGE_SW_WRITE_OFTEN;
			  }
            #ifdef PREVIEW_USE_DCAM_BUF
			*max_buffers                        = 2;
			#else
            *max_buffers                        = 6;
			#endif
            newParameters = &m_Stream[STREAM_ID_PREVIEW - 1]->m_parameters;
            newParameters->width                 = width;
            newParameters->height                = height;
            newParameters->format                = *format_actual;
            newParameters->streamOps             = stream_ops;
            newParameters->usage                 = *usage;
            newParameters->numSvcBufsInHal       = 0;
            newParameters->minUndequedBuffer     = 2;// 2

            m_Stream[STREAM_ID_PREVIEW - 1]->m_numRegisteredStream = 1;//parent stream total the num of stream
            #ifdef PREVIEW_USE_DCAM_BUF
            if(!allocatePreviewMem()) {
				HAL_LOGE("hal alloc preview mem fail!");
				return 1;
            }
			#endif
            HAL_LOGD("m_numRegisteredStream preview format=0x%x", *format_actual);
            return 0;
		} else {
			*stream_id = STREAM_ID_RECORD;
			HAL_LOGD("stream_id (%d) stream_ops(0x%x)", *stream_id,(uint32_t)stream_ops);
            subParameters = &m_subStreams[STREAM_ID_RECORD];
            memset(subParameters, 0, sizeof(substream_parameters_t));
            if (m_Stream[STREAM_ID_PREVIEW - 1] == NULL) {//preview parent stream
	            return 1;
	        }
            *format_actual = HAL_PIXEL_FORMAT_YCrCb_420_SP;
            if(s_mem_method == 0) {
				*usage  = GRALLOC_USAGE_SW_WRITE_OFTEN | GRALLOC_USAGE_CAMERA_BUFFER;
	  } else {
			*usage  = GRALLOC_USAGE_SW_WRITE_OFTEN;
	  }
            *max_buffers = 6;

            subParameters->type         = SUBSTREAM_TYPE_RECORD;
            subParameters->width        = width;
            subParameters->height       = height;
            subParameters->format       = *format_actual;
            subParameters->streamOps     = stream_ops;
            subParameters->usage         = *usage;
            subParameters->numOwnSvcBuffers = *max_buffers;
            subParameters->numSvcBufsInHal  = 0;
            subParameters->minUndequedBuffer = 2;
			SetRecStopMsg(false);
            HAL_LOGD("subParameters->streamOps 0x%x",(uint32_t)subParameters->streamOps);
            res = m_Stream[STREAM_ID_PREVIEW - 1]->attachSubStream(STREAM_ID_RECORD, 20);
            if (res != NO_ERROR) {
                HAL_LOGE("substream attach failed. res(%d)", res);
                return 1;
            }
            mRecordingTimeOffset = systemTime(SYSTEM_TIME_REALTIME) - systemTime();
            HAL_LOGD("Enabling Record");
            return 0;
		}
    }
	else if (format == HAL_PIXEL_FORMAT_YCrCb_420_SP || format == HAL_PIXEL_FORMAT_YV12) {
        *stream_id = STREAM_ID_PRVCB;

        subParameters = &m_subStreams[STREAM_ID_PRVCB];
        memset(subParameters, 0, sizeof(substream_parameters_t));

        if (m_Stream[STREAM_ID_PREVIEW - 1] == NULL) {//preview parent stream
            return 1;
        }

        *format_actual = format;
		*usage   = GRALLOC_USAGE_SW_WRITE_OFTEN;
        *max_buffers = 6;

        subParameters->type         = SUBSTREAM_TYPE_PRVCB;
        subParameters->width        = width;
        subParameters->height       = height;
        subParameters->format       = *format_actual;
        subParameters->streamOps     = stream_ops;
        subParameters->usage         = *usage;
        subParameters->numOwnSvcBuffers = *max_buffers;
        subParameters->numSvcBufsInHal  = 0;
        subParameters->minUndequedBuffer = 2;//graphic may have a lot bufs than hal

        res = m_Stream[STREAM_ID_PREVIEW - 1]->attachSubStream(STREAM_ID_PRVCB, 20);
        if (res != NO_ERROR) {
            HAL_LOGE("substream attach failed. res(%d)", res);
            return 1;
        }
        HAL_LOGD("Enabling previewcb m_numRegisteredStream = %d", m_Stream[STREAM_ID_PREVIEW - 1]->m_numRegisteredStream);
        return 0;
    }
    else if (format == HAL_PIXEL_FORMAT_BLOB //first create jpeg substream
            && isSupportedJpegResolution(m_Camera2, width, height)) {
        *stream_id = STREAM_ID_JPEG;

        subParameters = &m_subStreams[*stream_id];
        memset(subParameters, 0, sizeof(substream_parameters_t));
		if (m_Stream[STREAM_ID_CAPTURE - 1] == NULL)
               m_Stream[STREAM_ID_CAPTURE - 1] = new Stream(this, STREAM_ID_CAPTURE);
		newParameters = &(m_Stream[STREAM_ID_CAPTURE - 1]->m_parameters);
        newParameters->width                 = width;
        newParameters->height                = height;
        newParameters->format                = HAL_PIXEL_FORMAT_YCrCb_420_SP;
        newParameters->numSvcBufsInHal       = 0;
        newParameters->minUndequedBuffer     = 2;//v4l2 around buffers num
        newParameters->numSvcBuffers         = 4;
        m_Stream[STREAM_ID_CAPTURE - 1]->m_numRegisteredStream = 1;
        *format_actual = HAL_PIXEL_FORMAT_BLOB;
		*usage   = GRALLOC_USAGE_SW_WRITE_OFTEN;
        *max_buffers = 1;
        subParameters->type          = SUBSTREAM_TYPE_JPEG;
        subParameters->width         = width;
        subParameters->height        = height;
        subParameters->format        = *format_actual;
        subParameters->streamOps     = stream_ops;
        subParameters->usage         = *usage;
        subParameters->numOwnSvcBuffers = *max_buffers;
        subParameters->numSvcBufsInHal  = 0;
        subParameters->minUndequedBuffer = 2;
        res = m_Stream[STREAM_ID_CAPTURE - 1]->attachSubStream(STREAM_ID_JPEG, 20);
        if (res != NO_ERROR) {
            HAL_LOGE("substream attach failed. jpeg res(%d)", res);
            return 1;
        }
        HAL_LOGD("substream jpeg");
        return 0;
    } else if ((format == CAMERA2_HAL_PIXEL_FORMAT_ZSL)
												&& (width == (uint32_t)m_Camera2->sensorW)
												&& (height == (uint32_t)m_Camera2->sensorH)) {
		*stream_id = STREAM_ID_ZSL;

		subParameters = &m_subStreams[*stream_id];
		memset(subParameters, 0, sizeof(substream_parameters_t));

		if (m_Stream[STREAM_ID_CAPTURE - 1] == NULL) {
			m_Stream[STREAM_ID_CAPTURE - 1] = new Stream(this, STREAM_ID_CAPTURE);
		}
		newParameters = &(m_Stream[STREAM_ID_CAPTURE - 1]->m_parameters);
        newParameters->width                 = width;
        newParameters->height                = height;
        newParameters->format                = HAL_PIXEL_FORMAT_YCrCb_420_SP;
        newParameters->numSvcBufsInHal       = 0;
        newParameters->minUndequedBuffer     = 2;
        newParameters->numSvcBuffers         = 4;
        m_Stream[STREAM_ID_CAPTURE - 1]->m_numRegisteredStream = 1;

        *format_actual = HAL_PIXEL_FORMAT_YCrCb_420_SP;//HAL_PIXEL_FORMAT_BLOB
		*usage  = GRALLOC_USAGE_SW_WRITE_OFTEN;
        *max_buffers = 1;
        subParameters->type          = SUBSTREAM_TYPE_JPEG;
        subParameters->width         = width;
        subParameters->height        = height;
        subParameters->format        = *format_actual;
        subParameters->streamOps     = stream_ops;
        subParameters->usage         = *usage;
        subParameters->numOwnSvcBuffers = *max_buffers;
        subParameters->numSvcBufsInHal  = 0;
        subParameters->minUndequedBuffer = 2;
        res = m_Stream[STREAM_ID_CAPTURE - 1]->attachSubStream(STREAM_ID_JPEG, 20);
        if (res != NO_ERROR) {
            HAL_LOGE("substream attach failed. jpeg res(%d)", res);
            return 1;
        }
        HAL_LOGD("substream zsl");
		return 0;
    }

    HAL_LOGE("Unsupported Pixel Format");
    return 1;
}

const char* SprdCameraHWInterface2::getCameraStateStr(
        SprdCameraHWInterface2::Sprd_camera_state s)
{
        static const char* states[] = {
#define STATE_STR(x) #x
            STATE_STR(SPRD_INIT),
            STATE_STR(SPRD_IDLE),
            STATE_STR(SPRD_ERROR),
            STATE_STR(SPRD_PREVIEW_IN_PROGRESS),
            STATE_STR(SPRD_FOCUS_IN_PROGRESS),
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

bool SprdCameraHWInterface2::WaitForPreviewStart()
{
	Mutex::Autolock stateLock(&mStateLock);
	while(SPRD_PREVIEW_IN_PROGRESS != mCameraState.preview_state
		&& SPRD_ERROR != mCameraState.preview_state) {
		HAL_LOGV("WaitForPreviewStart: waiting for SPRD_PREVIEW_IN_PROGRESS");
		mStateWait.wait(mStateLock);
		HAL_LOGV("WaitForPreviewStart: woke up");
	}

	return SPRD_PREVIEW_IN_PROGRESS == mCameraState.preview_state;
}

bool SprdCameraHWInterface2::WaitForCaptureStart()
{
	Mutex::Autolock stateLock(&mStateLock);

    // It's possible for the YUV callback as well as the JPEG callbacks
    // to be invoked before we even make it here, so we check for all
    // possible result states from takePicture.
	while (SPRD_WAITING_RAW != mCameraState.capture_state
		 && SPRD_WAITING_JPEG != mCameraState.capture_state
		 && SPRD_IDLE != mCameraState.capture_state
		 && SPRD_ERROR != mCameraState.camera_state) {
		HAL_LOGD("waiting for SPRD_WAITING_RAW or SPRD_WAITING_JPEG");
		mStateWait.wait(mStateLock);
		HAL_LOGD("woke up, state is %s",
				getCameraStateStr(mCameraState.capture_state));
	}

	return (SPRD_WAITING_RAW == mCameraState.capture_state
			|| SPRD_WAITING_JPEG == mCameraState.capture_state
			|| SPRD_IDLE == mCameraState.capture_state);
}

bool SprdCameraHWInterface2::WaitForPreviewStop()
{
	Mutex::Autolock statelock(&mStateLock);
    while (SPRD_IDLE != mCameraState.preview_state
			&& SPRD_ERROR != mCameraState.preview_state) {
		HAL_LOGD("waiting for SPRD_IDLE");
		mStateWait.wait(mStateLock);
		HAL_LOGD("woke up");
    }

	return SPRD_IDLE == mCameraState.preview_state;
}

void SprdCameraHWInterface2::setCameraState(Sprd_camera_state state, state_owner owner)
{

	HAL_LOGV("state: %s, owner: %d", getCameraStateStr(state), owner);
	Mutex::Autolock stateLock(&mStateLock);

	switch (state) {
	//camera state
	case SPRD_INIT:
		switch (owner) {
		case STATE_CAMERA:
			mCameraState.camera_state = SPRD_INIT;
			break;

		case STATE_PREVIEW:
			mCameraState.preview_state = SPRD_INIT;
			break;

		case STATE_CAPTURE:
			mCameraState.capture_state = SPRD_INIT;
			break;

		case STATE_FOCUS:
			mCameraState.focus_state = SPRD_INIT;
			break;
		}

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

	default:
		HAL_LOGV("setCameraState: error");
		break;
	}

	HAL_LOGV("camera state = %s, preview state = %s, capture state = %s focus state = %s",
				getCameraStateStr(mCameraState.camera_state),
				getCameraStateStr(mCameraState.preview_state),
				getCameraStateStr(mCameraState.capture_state),
				getCameraStateStr(mCameraState.focus_state));
}

void SprdCameraHWInterface2::HandleStopPreview(camera_cb_type cb,
										  int32_t parm4)
{
	HAL_LOGD("in,cb = %d, parm4 = 0x%x, state = %s",
				cb, parm4, getCameraStateStr(getPreviewState()));

	transitionState(SPRD_INTERNAL_PREVIEW_STOPPING,
				SPRD_IDLE,
				STATE_PREVIEW);
	HAL_LOGD("out, state = %s", getCameraStateStr(getPreviewState()));
}

int SprdCameraHWInterface2::flush_buffer(camera_flush_mem_type_e  type, int index, void *v_addr, void *p_addr, int size)
{
	int ret = 0;
	sprd_camera_memory_t  *pmem = NULL;
	MemoryHeapIon *pHeapIon;
	#ifndef PREVIEW_USE_DCAM_BUF
	const private_handle_t *priv_handle = NULL;
	stream_parameters_t     *targetStreamParms;
	#endif

	HAL_LOGV("type %d, index %d, v_addr:0x%x, p_addr:0x%x, size %d",
		type, index, (uint32_t)v_addr, (uint32_t)p_addr, size);

	switch(type) {
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
		#ifdef PREVIEW_USE_DCAM_BUF
		if (index < kPreviewBufferCount && mPreviewHeapArray != NULL) {
			pmem = mPreviewHeapArray[index];
		}
		#else
		targetStreamParms = &(m_Stream[STREAM_ID_PREVIEW - 1]->m_parameters);
		priv_handle = reinterpret_cast<const private_handle_t *>(targetStreamParms->svcBufHandle[index]);
		MemoryHeapIon::Flush_ion_buffer(priv_handle->share_fd,v_addr, p_addr, size);
		#endif
		break;
	default:
		break;
	}

	if (pmem) {
		pHeapIon = pmem->ion_heap;
		if (pHeapIon != NULL) {
			ret = pHeapIon->flush_ion_buffer(v_addr, p_addr, size);
			HAL_LOGV("ret = %d", ret);
			if (ret) {
				HAL_LOGE("error ret=%d", ret);
			}
		}
	}
	return ret;
}
int SprdCameraHWInterface2::flush_preview_buffers()
{
	int                     i;
    stream_parameters_t     *targetStreamParms;
	int phyaddr = 0;
	int size =0;
	int ret = 0;

	HAL_LOGV("start");

    targetStreamParms = &(m_Stream[STREAM_ID_PREVIEW - 1]->m_parameters);

    for (i = 0; i < targetStreamParms->numSvcBuffers; i++) {
		const private_handle_t *priv_handle = reinterpret_cast<const private_handle_t *>(targetStreamParms->svcBufHandle[i]);

		flush_buffer(CAMERA_FLUSH_PREVIEW_HEAP,
			 priv_handle->share_fd,
			 (void*)mPreviewHeapArray_vir[i],
			 (void*)mPreviewHeapArray_phy[i],
			 priv_handle->size);
	}
	HAL_LOGV("end");
	return ret;
}


void SprdCameraHWInterface2::HandleEncode(camera_cb_type cb, int32_t parm4)
{
	HAL_LOGD("in: cb = %d, parm4 = 0x%x, state = %s",
				cb, parm4, getCameraStateStr(getCaptureState()));

	switch (cb) {
	case CAMERA_RSP_CB_SUCCESS:
        // We already transitioned the camera state to
        // SPRD_WAITING_JPEG when we called
        // camera_encode_picture().
		break;
	case CAMERA_EXIT_CB_DONE:
		if ((SPRD_WAITING_JPEG == getCaptureState())) {
			//data callback
			substream_parameters_t  *subParms = &m_subStreams[STREAM_ID_JPEG];
			sp<Stream> StreamSP = m_Stream[STREAM_ID_CAPTURE - 1];
			JPEGENC_CBrtnType *tmpCBpara = NULL;
			int64_t timeStamp = 0;
			if (GetOutputStreamMask() & STREAM_MASK_JPEG) {
	            tmpCBpara = (JPEGENC_CBrtnType *)parm4;
				subParms->dataSize = tmpCBpara->size;
				timeStamp = systemTime();
				displaySubStream(StreamSP, (int32_t *)(((camera_encode_mem_type *)(tmpCBpara->outPtr))->buffer),
								timeStamp,(uint16_t)STREAM_ID_JPEG);
			}
			if (tmpCBpara->need_free) {
				if(GetCameraPictureMode() == CAMERA_NORMAL_MODE) {
					freeCaptureMem();
				}
				transitionState(SPRD_WAITING_JPEG,
						SPRD_IDLE,
						STATE_CAPTURE);
			} else {
				transitionState(SPRD_WAITING_JPEG,
						SPRD_INTERNAL_RAW_REQUESTED,
						STATE_CAPTURE);
			}
		}
		break;
	case CAMERA_EXIT_CB_FAILED:
		transitionState(getCaptureState(), SPRD_ERROR, STATE_CAPTURE);
		//receiveCameraExitError();
		break;
	default:
		transitionState(getCaptureState(), SPRD_ERROR, STATE_CAPTURE);
		//receiveJpegPictureError();
		break;
	}

	HAL_LOGD("out, state = %s", getCameraStateStr(getCaptureState()));
}

void SprdCameraHWInterface2::HandleCancelPicture(camera_cb_type cb, int32_t parm4)
{
	transitionState(SPRD_INTERNAL_CAPTURE_STOPPING,
				SPRD_IDLE,
				STATE_CAPTURE);
}

void SprdCameraHWInterface2::DisplayPictureImg(camera_frame_type *frame)
{
    status_t res = 0;
    stream_parameters_t     *targetStreamParms = NULL;
	buffer_handle_t * buf = NULL;
	buffer_handle_t disPic = 0;
	const private_handle_t *priv_handle = NULL;
    bool found = false;
    int Index = 0;
	void *VirtBuf = NULL;
	sp<Stream> StreamSP = NULL;
	int phyaddr = 0;
	int size =0;
	uint32_t buffer_size = 0;
	sprd_camera_memory_t *cam_Add = NULL;
    if (NULL == frame) {
		HAL_LOGE("invalid frame pointer");
		return;
	}

	if(SPRD_INTERNAL_CAPTURE_STOPPING == getCaptureState()) {
		HAL_LOGD("warning: capture state = SPRD_INTERNAL_CAPTURE_STOPPING, return \n");
		return;
	}
    //deq one graphic buf(2 bufs)
    StreamSP = m_Stream[STREAM_ID_PREVIEW - 1];
	targetStreamParms = &(StreamSP->m_parameters);
	buffer_size = camera_get_size_align_page((SIZE_ALIGN(targetStreamParms->width) * SIZE_ALIGN(targetStreamParms->height) * 3)/2);
	#ifdef CONFIG_CAMERA_ROTATION_CAPTURE
	buffer_size <<= 1;
	#endif
	cam_Add = GetCachePmem(buffer_size, 1);
	if (cam_Add == NULL) {
		HAL_LOGE("cannot allocate buf");
		goto allocate_buf_free;
	}
	if (cam_Add->data == NULL) {
		HAL_LOGE("cannot get virt add");
		goto allocate_buf_free;
	}
	if ( 0 != camera_get_data_redisplay(cam_Add->phys_addr, targetStreamParms->width, targetStreamParms->height, frame->buffer_phy_addr,
									frame->buffer_uv_phy_addr, frame->dx, frame->dy)) {
		HAL_LOGE("Fail to camera_get_data_redisplay.");
		goto allocate_buf_free;
	}
	#ifdef PREVIEW_USE_DCAM_BUF
	res = targetStreamParms->streamOps->dequeue_buffer(targetStreamParms->streamOps, &buf);
	if (res != NO_ERROR || buf == NULL) {
		HAL_LOGD("dequeue_buffer fail");
		goto allocate_buf_free;
    }
	disPic = *buf;
	#else
	Index = StreamSP->popBufQ();
	HAL_LOGD("DisplayPictureImg index=%d",Index);
	if (Index == -1) {
		for (Index = 0; Index < targetStreamParms->numSvcBuffers ; Index++) {
            if (targetStreamParms->svcBufStatus[Index] == ON_HAL_DRIVER) {
                found = true;
				HAL_LOGD("Index=%d",Index);
                break;
            }
        }
		if (!found) {
			HAL_LOGE("ERR cannot found buf");
			goto allocate_buf_free;
		}
	}
	disPic = targetStreamParms->svcBufHandle[Index];
	#endif
    //lock
	if (m_grallocHal->lock(m_grallocHal, disPic, targetStreamParms->usage, 0, 0,
                   targetStreamParms->width, targetStreamParms->height, &VirtBuf) != 0) {
		HAL_LOGE("ERR could not obtain gralloc buffer");
	}

	priv_handle = reinterpret_cast<const private_handle_t *>(disPic);
	memcpy((char *)(priv_handle->base),(char *)(cam_Add->data),
							(targetStreamParms->width * targetStreamParms->height * 3)/2);
	//unlock
    if (m_grallocHal) {
        m_grallocHal->unlock(m_grallocHal, disPic);
    } else {
        HAL_LOGD("ERR displaySubStream gralloc is NULL");
	}
	res = targetStreamParms->streamOps->enqueue_buffer(targetStreamParms->streamOps,
                                               frame->timestamp,
                                               &disPic);
	allocate_buf_free:
	if (cam_Add) {
		if(cam_Add->ion_heap != NULL) {
			if(s_mem_method != 0){
				cam_Add->ion_heap->free_mm_iova(cam_Add->phys_addr, cam_Add->phys_size);
			}
			//mPreviewHeapArray[i]->ion_heap.clear();
			delete cam_Add->ion_heap;
			cam_Add->ion_heap = NULL;
		}
		free(cam_Add);
	}
    HAL_LOGD("return %d",res);
}
void SprdCameraHWInterface2::HandleTakePicture(camera_cb_type cb, int32_t parm4)
{
	HAL_LOGD("in: cb = %d, parm4 = 0x%x, state = %s",
				cb, parm4, getCameraStateStr(getCaptureState()));
	bool encode_location = true;
	camera_position_type pt = {0, 0, 0, 0, NULL};
	if (!(m_staticReqInfo.gpsLat == 0 && m_staticReqInfo.gpsLon == 0 && m_staticReqInfo.gpsAlt == 0)) {
		pt.altitude = m_staticReqInfo.gpsAlt;
		pt.latitude = m_staticReqInfo.gpsLat;
		pt.longitude = m_staticReqInfo.gpsLon;
		pt.process_method = (char *)(&m_staticReqInfo.gpsProcMethod[0]);
		pt.timestamp = m_staticReqInfo.gpsTimestamp;
	} else {
	    encode_location = false;
	}
	switch (cb) {
	case CAMERA_EVT_CB_FLUSH:
		HAL_LOGD("CAMERA_EVT_CB_FLUSH");
		//#ifdef PREVIEW_USE_DCAM_BUF
		flush_buffer(CAMERA_FLUSH_RAW_HEAP_ALL, 0,(void*)0,(void*)0,0);
		//#endif
		break;
	case CAMERA_RSP_CB_SUCCESS:
		if (GetOutputStreamMask() & STREAM_MASK_JPEG) {
            m_RequestQueueThread->SetSignal(SIGNAL_REQ_THREAD_REQ_DONE);
		}
		transitionState(SPRD_INTERNAL_RAW_REQUESTED,
					SPRD_WAITING_RAW,
					STATE_CAPTURE);
		break;
	case CAMERA_EVT_CB_SNAPSHOT_DONE:
		if (encode_location) {
			if (camera_set_position(&pt, NULL, NULL) != CAMERA_SUCCESS) {
				HAL_LOGE("receiveRawPicture: camera_set_position: error");
			}
		}
		if (GetCameraPictureMode() != CAMERA_ZSL_MODE) {
			DisplayPictureImg((camera_frame_type *)parm4);
		}
		break;
	case CAMERA_EXIT_CB_DONE:
		if (SPRD_WAITING_RAW == getCaptureState()) {
			transitionState(SPRD_WAITING_RAW,SPRD_WAITING_JPEG,STATE_CAPTURE);
		}
		break;
	case CAMERA_EXIT_CB_FAILED:			//Execution failed or rejected
		HAL_LOGE("SprdCameraHardware::camera_cb: @CAMERA_EXIT_CB_FAILURE(%d) in state %s.",
				parm4, getCameraStateStr(getCaptureState()));
		transitionState(getCaptureState(), SPRD_ERROR, STATE_CAPTURE);
		//receiveCameraExitError();
		break;
	default:
		HAL_LOGE("HandleTakePicture: unkown cb = %d", cb);
		transitionState(getCaptureState(), SPRD_ERROR, STATE_CAPTURE);
		//receiveTakePictureError();
		break;
	}

	HAL_LOGD("out, state = %s", getCameraStateStr(getCaptureState()));
}

void SprdCameraHWInterface2::HandleStopCamera(camera_cb_type cb, int32_t parm4)
{
	HAL_LOGD("in: cb = %d, parm4 = 0x%x, state = %s",
				cb, parm4, getCameraStateStr(getCameraState()));

	transitionState(SPRD_INTERNAL_STOPPING, SPRD_INIT, STATE_CAMERA);

	HAL_LOGD("out, state = %s", getCameraStateStr(getCameraState()));
}

void SprdCameraHWInterface2::HandleFocus(camera_cb_type cb,
								  int32_t parm4)
{
    Mutex::Autolock lock(m_afTrigLock);

	switch (cb) {
	case CAMERA_EXIT_CB_DONE:
		HAL_LOGD("camera cb: autofocus mNotify_cb start.");
		m_focusStat = FOCUS_STAT_FOCUS_LOCKED;
		m_notifyCb(CAMERA2_MSG_AUTOFOCUS, ANDROID_CONTROL_AF_STATE_FOCUSED_LOCKED, m_camCtlInfo.afTrigID, 0, m_callbackClient);
		break;
	case CAMERA_EXIT_CB_ABORT:
		HAL_LOGE("camera cb: autofocus aborted");
		break;

	case CAMERA_EXIT_CB_FAILED:
		HAL_LOGE("camera cb: autofocus failed");
		m_focusStat = FOCUS_STAT_FOCUS_NOT_LOCKED;
		m_notifyCb(CAMERA2_MSG_AUTOFOCUS, ANDROID_CONTROL_AF_STATE_NOT_FOCUSED_LOCKED, m_camCtlInfo.afTrigID, 0, m_callbackClient);
		break;

	default:
		HAL_LOGE("camera cb: unknown cb %d for CAMERA_FUNC_START_FOCUS!", cb);
		m_focusStat = FOCUS_STAT_FOCUS_NOT_LOCKED;
		m_notifyCb(CAMERA2_MSG_AUTOFOCUS, ANDROID_CONTROL_AF_STATE_NOT_FOCUSED_LOCKED, m_camCtlInfo.afTrigID, 0, m_callbackClient);
		break;
	}
}

void SprdCameraHWInterface2::camera_cb(camera_cb_type cb,
                                           const void *client_data,
                                           camera_func_type func,
                                           int32_t parm4)
{
	SprdCameraHWInterface2 *obj = (SprdCameraHWInterface2 *)client_data;

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
        HAL_LOGE("Unknown camera-callback status %d", cb);
		break;
	}
}
void SprdCameraHWInterface2::getPreviewBuffer(void)
{
    stream_parameters_t     *targetStreamParms = &(m_Stream[STREAM_ID_PREVIEW - 1]->m_parameters);
	buffer_handle_t         *buf = NULL;
	const private_handle_t  *priv_handle = NULL;
	bool found = false;
	int Index = 0;
	int phyaddr =0;
	int size =0;
    int ret = 0;
	void *vaddr= 0;

	for(int j=0;j < targetStreamParms->numSvcBuffers;j++) {
		HAL_LOGD("@@@ statue0=%d",targetStreamParms->svcBufStatus[j]);
	}
	for(int j=0;j < (targetStreamParms->numSvcBuffers - targetStreamParms->minUndequedBuffer);j++) {
		found = false;
		ret = targetStreamParms->streamOps->dequeue_buffer(targetStreamParms->streamOps, &buf);
		if (ret != NO_ERROR || buf == NULL) {
			HAL_LOGD("first dequeue_buffer fail");
			return;
		}
		priv_handle = reinterpret_cast<const private_handle_t *>(*buf);
		if(s_mem_method == 0){
			MemoryHeapIon::Get_phy_addr_from_ion(priv_handle->share_fd,&phyaddr,&size);
		} else {
			MemoryHeapIon::Get_mm_iova(priv_handle->share_fd,&phyaddr,&size);
		}
		for (Index = 0; Index < targetStreamParms->numSvcBuffers ; Index++) {
			if ((phyaddr == mPreviewHeapArray_phy[Index])
				&& (targetStreamParms->svcBufStatus[Index] == ON_HAL_INIT)) {
				found = true;
				HAL_LOGD("@@@ Index=%d",Index);
				targetStreamParms->svcBufStatus[Index] = ON_HAL_DRIVER;
				vaddr = (void*)mPreviewHeapArray_vir[Index];
				HAL_LOGV("@@@ lock ret = %d.",ret);
				break;
			}
		}
		if (!found) {
			HAL_LOGD("ERR cannot found buf=0x%x ",phyaddr);
			if(s_mem_method != 0) {
				 MemoryHeapIon::Free_mm_iova(priv_handle->share_fd,phyaddr, size);
			}
			return;
		}
		if(s_mem_method != 0) {
			 MemoryHeapIon::Free_mm_iova(priv_handle->share_fd,phyaddr, size);
		}
	}

	for(int j=0;j < targetStreamParms->numSvcBuffers;j++) {
		HAL_LOGD("@@@ statue=%d",targetStreamParms->svcBufStatus[j]);
	}
}
int SprdCameraHWInterface2::registerStreamBuffers(uint32_t stream_id,
        int num_buffers, buffer_handle_t *registeringBuffers)
{
    int                     i,j;
    void                    *virtAddr[3];
    int                     plane_index = 0;
    Sprd_camera_state       camStatus = (Sprd_camera_state)0;
    stream_parameters_t     *targetStreamParms;
	int phyaddr = 0;
	int size =0;

    HAL_LOGD("stream_id(%d), num_buff(%d), handle(%x) ",
        stream_id, num_buffers, (uint32_t)registeringBuffers);

    if (stream_id == STREAM_ID_PREVIEW && m_Stream[STREAM_ID_PREVIEW - 1] != NULL) {
        targetStreamParms = &(m_Stream[STREAM_ID_PREVIEW - 1]->m_parameters);
		camStatus = getCameraState();
	    if (camStatus != SPRD_IDLE) {
	        HAL_LOGD("ERR Sta=%d",camStatus);
			return UNKNOWN_ERROR;
		}

		HAL_LOGD("format(%x) width(%d), height(%d)",
				targetStreamParms->format, targetStreamParms->width,
				targetStreamParms->height);
	    targetStreamParms->numSvcBuffers = num_buffers;

	    //use graphic buffers, firstly cancel buffers dequeued on framework
	    for (i = 0; i < targetStreamParms->numSvcBuffers; i++) {
	         const private_handle_t *priv_handle = reinterpret_cast<const private_handle_t *>(registeringBuffers[i]);

			 //MemoryHeapIon::Get_phy_addr_from_ion(priv_handle->share_fd,&phyaddr,&size);
			 if(s_mem_method == 0){
				 MemoryHeapIon::Get_phy_addr_from_ion(priv_handle->share_fd,&phyaddr,&size);
			 } else {
				 MemoryHeapIon::Get_mm_iova(priv_handle->share_fd,&phyaddr,&size);//in fact map to virtual add
				 targetStreamParms->phySize[i] = size;
				 #ifdef PREVIEW_USE_DCAM_BUF
				 targetStreamParms->phyAdd[i] = phyaddr;
				 #endif
			 }

			 #ifndef PREVIEW_USE_DCAM_BUF
			 mPreviewHeapArray_phy[i] = phyaddr;
			 mPreviewHeapArray_vir[i] = priv_handle->base;
			 #endif

			 targetStreamParms->svcBufHandle[i] = registeringBuffers[i];//important
			 targetStreamParms->svcBufStatus[i] = ON_HAL_INIT;
			 HAL_LOGD("index=%d Preview phyadd=0x%x,virtadd=0x%x,srv_add=0x%x,size %d",
					i, (uint32_t)phyaddr,priv_handle->base,(uint32_t)targetStreamParms->svcBufHandle[i],size);
		}

	    targetStreamParms->cancelBufNum = targetStreamParms->minUndequedBuffer;
	    HAL_LOGD("END registerStreamBuffers");
	    return NO_ERROR;

    } else if ((stream_id == STREAM_ID_PRVCB) || (stream_id == STREAM_ID_JPEG) || (stream_id == STREAM_ID_ZSL) || (stream_id == STREAM_ID_RECORD)) {
        substream_parameters_t  *targetParms;
        targetParms = &m_subStreams[stream_id];
        targetParms->numSvcBuffers = num_buffers;

        for (int i = 0 ; i < targetParms->numSvcBuffers ; i++) {
            if (m_grallocHal) {
                const private_handle_t *priv_handle = reinterpret_cast<const private_handle_t *>(registeringBuffers[i]);
                //targetParms->subStreamGraphicFd[i] = priv_handle->fd;
                if(s_mem_method == 0){
				 MemoryHeapIon::Get_phy_addr_from_ion(priv_handle->share_fd,&phyaddr,&size);
			 } else {
				 MemoryHeapIon::Get_mm_iova(priv_handle->share_fd,&phyaddr,&size);
				 targetParms->phySize[i] = size;
			 }
				targetParms->subStreamGraphicFd[i] = phyaddr;
				targetParms->subStreamAddVirt[i]   = (uint32_t)priv_handle->base;
                targetParms->svcBufHandle[i]       = registeringBuffers[i];
				targetParms->svcBufStatus[i] = ON_HAL_INIT;
				targetParms->numSvcBufsInHal++;
				HAL_LOGD("registering substream(%d) size[%d] phyAdd0x%x vAdd(%x) ",
                         i, size,phyaddr, (uint32_t)(priv_handle->base));
            }
        }
        return 0;
    } else {
        HAL_LOGE("unregistered stream id (%d)", stream_id);
        return 1;
    }

}

int SprdCameraHWInterface2::releaseStream(uint32_t stream_id)
{
    status_t ret = NO_ERROR;
	int i = 0;
    stream_parameters_t     *targetStreamParms;
	substream_parameters_t  *subParms    = &m_subStreams[stream_id];
	Sprd_camera_state       camStatus = (Sprd_camera_state)0;
	const private_handle_t *priv_handle = NULL;

	HAL_LOGD("stream_id(%d)", stream_id);

    if (stream_id == STREAM_ID_PREVIEW) {
		camStatus = getPreviewState();

	/*	if (camStatus != SPRD_PREVIEW_IN_PROGRESS) {
		   HAL_LOGE("camera isn't in previewing,%d",camStatus);
		   return ret;
		}*/
		if (camStatus == SPRD_PREVIEW_IN_PROGRESS) {
			m_Stream[STREAM_ID_PREVIEW - 1]->setRecevStopMsg(true);//sync receive preview frame
        	stopPreviewSimple();
		}

		targetStreamParms = &(m_Stream[STREAM_ID_PREVIEW - 1]->m_parameters);
		for (;i < targetStreamParms->numSvcBuffers;i++) {
		  if (s_mem_method == 1) {
			  priv_handle = reinterpret_cast<const private_handle_t *>(targetStreamParms->svcBufHandle[i]);
			  #ifdef PREVIEW_USE_DCAM_BUF
			  MemoryHeapIon::Free_mm_iova(priv_handle->share_fd,targetStreamParms->phyAdd[i], targetStreamParms->phySize[i] );
			  #else
			  MemoryHeapIon::Free_mm_iova(priv_handle->share_fd,mPreviewHeapArray_phy[i], targetStreamParms->phySize[i] );
			  #endif
		  }
	           ret = targetStreamParms->streamOps->cancel_buffer(targetStreamParms->streamOps, &(targetStreamParms->svcBufHandle[i]));
		   if (ret) {
              HAL_LOGE("cancelbuf res=%d",ret);
		   }
		}
		#ifdef PREVIEW_USE_DCAM_BUF
		freePreviewMem(mPreviewHeapNum);
		#endif
		if (m_CameraId == 1 && GetCameraCaptureIntent(&m_staticReqInfo) == CAPTURE_INTENT_VIDEO_RECORD) {
			freeCaptureMem();
		}
		camera_set_preview_mem(0, 0, 0, 0);
        if (m_Stream[STREAM_ID_PREVIEW - 1] != NULL) {
			m_Stream[STREAM_ID_PREVIEW - 1]->detachSubStream(STREAM_ID_RECORD);
			memset(&m_subStreams[STREAM_ID_PRVCB], 0, sizeof(substream_parameters_t));
			m_Stream[STREAM_ID_PREVIEW - 1]->detachSubStream(STREAM_ID_PRVCB);
			m_Stream[STREAM_ID_PREVIEW - 1] = NULL;
        }
    }
	else if (stream_id == STREAM_ID_JPEG) {
		for (;i < subParms->numSvcBuffers;i++) {
		  if(s_mem_method == 1) {
			  priv_handle = reinterpret_cast<const private_handle_t *>(subParms->svcBufHandle[i]);
			  MemoryHeapIon::Free_mm_iova(priv_handle->share_fd,subParms->subStreamGraphicFd[i], subParms->phySize[i] );
		  }
			}
		freeCaptureMem();
        memset(&m_subStreams[stream_id], 0, sizeof(substream_parameters_t));
		if (m_Stream[STREAM_ID_CAPTURE - 1] != NULL) {
			if (m_Stream[STREAM_ID_CAPTURE - 1]->detachSubStream(stream_id) != NO_ERROR) {
	            HAL_LOGE("substream detach failed. res(%d)", ret);
	            return 1;
	        }
            m_Stream[STREAM_ID_CAPTURE - 1]->m_numRegisteredStream = 1;
			memset(&(m_Stream[STREAM_ID_CAPTURE - 1]->m_parameters), 0, sizeof(stream_parameters_t));
		}
        HAL_LOGD("jpg stream release!");
        return 0;
    }
	else if (stream_id == STREAM_ID_RECORD) {//release old record
		SetRecStopMsg(true);
		for (;i < subParms->numSvcBuffers;i++) {
		  if(s_mem_method == 1) {
			  priv_handle = reinterpret_cast<const private_handle_t *>(subParms->svcBufHandle[i]);
			  MemoryHeapIon::Free_mm_iova(priv_handle->share_fd,subParms->subStreamGraphicFd[i], subParms->phySize[i] );
		  }
			}
        memset(&m_subStreams[STREAM_ID_RECORD], 0, sizeof(substream_parameters_t));
		if (m_Stream[STREAM_ID_PREVIEW - 1] != NULL) {
		   if (m_Stream[STREAM_ID_PREVIEW - 1]->m_numRegisteredStream > 1)
              m_Stream[STREAM_ID_PREVIEW - 1]->detachSubStream(stream_id);
		}
		return 0;
	} else if (stream_id == STREAM_ID_PRVCB) {
        memset(&m_subStreams[STREAM_ID_PRVCB], 0, sizeof(substream_parameters_t));
		if (m_Stream[STREAM_ID_PREVIEW - 1] != NULL) {
		   if (m_Stream[STREAM_ID_PREVIEW - 1]->m_numRegisteredStream > 1)
              m_Stream[STREAM_ID_PREVIEW - 1]->detachSubStream(stream_id);
		}
		return 0;
	} else {
        HAL_LOGE("wrong stream id (%d)", stream_id);
        return 1;
    }

    HAL_LOGD("END");
    return ret;
}

int SprdCameraHWInterface2::allocateReprocessStream(
    uint32_t width, uint32_t height, uint32_t format,
    const camera2_stream_in_ops_t *reprocess_stream_ops,
    uint32_t *stream_id, uint32_t *consumer_usage, uint32_t *max_buffers)
{
	HAL_LOGV("dont handle");
	return 0;
}

int SprdCameraHWInterface2::allocateReprocessStreamFromStream(
            uint32_t output_stream_id,
            const camera2_stream_in_ops_t *reprocess_stream_ops,
            uint32_t *stream_id)
{
    HAL_LOGD("output_stream_id(%d)", output_stream_id);
    if (output_stream_id == STREAM_ID_ZSL) {
	    *stream_id = STREAM_ID_JPEG;
    }
    return 0;
}

int SprdCameraHWInterface2::releaseReprocessStream(uint32_t stream_id)
{
    HAL_LOGD("stream_id(%d)", stream_id);
	return 0;
}

int SprdCameraHWInterface2::triggerAction(uint32_t trigger_id, int ext1, int ext2)
{
    Mutex::Autolock lock(m_afTrigLock);
    HAL_LOGD("id(%x), %d, %d afmode=%d", trigger_id, ext1, ext2, m_staticReqInfo.afMode);
	if (CAMERA2_TRIGGER_CANCEL_AUTOFOCUS != trigger_id) {
		WaitForPreviewStart();
	}

    switch (trigger_id) {
    case CAMERA2_TRIGGER_AUTOFOCUS:
		m_camCtlInfo.afTrigID = ext1;
		switch(m_staticReqInfo.afMode)
		{
			case CAMERA_FOCUS_MODE_AUTO:
			case CAMERA_FOCUS_MODE_MACRO:
			switch(m_focusStat) {
			case FOCUS_STAT_INACTIVE:
			case FOCUS_STAT_FOCUS_LOCKED:
			case FOCUS_STAT_FOCUS_NOT_LOCKED:
				if (0 != camera_start_autofocus(CAMERA_AUTO_FOCUS, camera_cb, this)) {
					HAL_LOGE("auto foucs fail.");
					return 1;
				}
				m_focusStat = FOCUS_STAT_ACTIVE_SCAN;
				m_notifyCb(CAMERA2_MSG_AUTOFOCUS, ANDROID_CONTROL_AF_STATE_ACTIVE_SCAN, ext1, 0, m_callbackClient);
				break;
			case FOCUS_STAT_ACTIVE_SCAN:
				HAL_LOGV("don't handle.");
				break;
			case FOCUS_STAT_PASSIVE_SCAN:
				HAL_LOGV("don't handle.");
				break;
			case FOCUS_STAT_PASSIVE_LOCKED:
				HAL_LOGV("don't handle.");
				break;
			default:
				HAL_LOGV("don't handle.");
				break;
			}
	        break;
		}
        break;
    case CAMERA2_TRIGGER_CANCEL_AUTOFOCUS:
		m_camCtlInfo.afTrigID = ext1;
        switch(m_staticReqInfo.afMode) {
	    case CAMERA_FOCUS_MODE_AUTO:
		case CAMERA_FOCUS_MODE_MACRO:
			if (m_focusStat == FOCUS_STAT_ACTIVE_SCAN) {
                if(camera_cancel_autofocus())
				   HAL_LOGE("cancel focus fail");
			}
            m_focusStat = FOCUS_STAT_INACTIVE;
			m_notifyCb(CAMERA2_MSG_AUTOFOCUS, ANDROID_CONTROL_AF_STATE_INACTIVE, ext1, 0, m_callbackClient);
			break;
		}
        break;
    case CAMERA2_TRIGGER_PRECAPTURE_METERING:
		//notify start
		m_camCtlInfo.precaptureTrigID = ext1;
		m_notifyCb(CAMERA2_MSG_AUTOEXPOSURE,
                        ANDROID_CONTROL_AE_STATE_PRECAPTURE,
                        m_camCtlInfo.precaptureTrigID, 0, m_callbackClient);
        m_notifyCb(CAMERA2_MSG_AUTOWB,
                    ANDROID_CONTROL_AWB_STATE_CONVERGED,
                    m_camCtlInfo.precaptureTrigID, 0, m_callbackClient);
		//to do oem add callback
		m_RequestQueueThread->SetSignal(SIGNAL_REQ_THREAD_PRECAPTURE_METERING_DONE);
        break;
    default:
        break;
    }
    return 0;
}

int SprdCameraHWInterface2::setNotifyCallback(camera2_notify_callback notify_cb, void *user)
{
    HAL_LOGV("cb_addr(%x)", (unsigned int)notify_cb);
    m_notifyCb = notify_cb;
    m_callbackClient = user;
    return 0;
}

int SprdCameraHWInterface2::getMetadataVendorTagOps(vendor_tag_query_ops_t **ops)
{
    HAL_LOGV("dont handle");
    return 0;
}

int SprdCameraHWInterface2::dump(int fd)
{
    HAL_LOGV("don't handle");
    return 0;
}

void  SprdCameraHWInterface2::Stream::pushBufQ(int index)
{
    Mutex::Autolock lock(m_BufQLock);
    m_bufQueue.push_back(index);
}

void  SprdCameraHWInterface2::Stream::setRecevStopMsg(bool IsRecevMsg)
{
    Mutex::Autolock lock(m_stateLock);
	m_IsRecevStopMsg = IsRecevMsg;
}

bool  SprdCameraHWInterface2::Stream::getRecevStopMsg(void)
{
    Mutex::Autolock lock(m_stateLock);
	return m_IsRecevStopMsg;
}

void  SprdCameraHWInterface2::Stream::setHalStopMsg(bool IsStopMsg)
{
    Mutex::Autolock lock(m_stateLock);
	m_halStopMsg = IsStopMsg;
}

bool  SprdCameraHWInterface2::Stream::getHalStopMsg(void)
{
    Mutex::Autolock lock(m_stateLock);
	return m_halStopMsg;
}

int SprdCameraHWInterface2::Stream::popBufQ()
{
   List<int>::iterator buf;
   int index;

    Mutex::Autolock lock(m_BufQLock);

    if(m_bufQueue.size() == 0)
        return -1;

    buf = m_bufQueue.begin()++;
    index = *buf;
    m_bufQueue.erase(buf);

    return (index);
}

void SprdCameraHWInterface2::Stream::releaseBufQ()
{
    List<int>::iterator round;

    Mutex::Autolock lock(m_BufQLock);
    HAL_LOGD("bufqueue.size : %d", m_bufQueue.size());

    while (m_bufQueue.size() > 0) {
        round  = m_bufQueue.begin()++;
        m_bufQueue.erase(round);
    }
    return;
}

void SprdCameraHWInterface2::SetReqProcessing(bool IsProc)
{
	Mutex::Autolock lock(m_requestMutex);
	m_reqIsProcess = IsProc;
}

bool SprdCameraHWInterface2::GetReqProcessStatus()
{
    Mutex::Autolock lock(m_requestMutex);
	return m_reqIsProcess;
}

int SprdCameraHWInterface2::GetReqQueueSize()
{
	Mutex::Autolock lock(m_requestMutex);
	return m_ReqQueue.size();
}

void SprdCameraHWInterface2::PushReqQ(camera_metadata_t *req)
{
    Mutex::Autolock lock(m_requestMutex);
    m_ReqQueue.push_back(req);
}

camera_metadata_t *SprdCameraHWInterface2::PopReqQ()
{
	List<camera_metadata_t *>::iterator req;
	camera_metadata_t *ret_req = NULL;

    Mutex::Autolock lock(m_requestMutex);

    if(m_ReqQueue.size() == 0)
        return NULL;

    req = m_ReqQueue.begin()++;
    ret_req = *req;
    m_ReqQueue.erase(req);

    return ret_req;
}

void SprdCameraHWInterface2::ClearReqQ()
{
    List<camera_metadata_t *>::iterator round;

    Mutex::Autolock lock(m_requestMutex);
    HAL_LOGD("m_ReqQueue.size : %d", m_ReqQueue.size());

    while (m_ReqQueue.size() > 0) {
        round  = m_ReqQueue.begin()++;
        m_ReqQueue.erase(round);
    }
    return;
}

bool SprdCameraHWInterface2::GetRecStopMsg()
{
   Mutex::Autolock lock(m_halCBMutex);

   return m_recStopMsg;
}

void SprdCameraHWInterface2::SetRecStopMsg(bool recStop)
{
	Mutex::Autolock lock(m_halCBMutex);

	m_recStopMsg = recStop;
}

bool SprdCameraHWInterface2::GetDcDircToDvSnap()
{
   Mutex::Autolock lock(m_halCBMutex);

   return m_dcDircToDvSnap;
}

void SprdCameraHWInterface2::SetDcDircToDvSnap(bool dcDircToSnap)
{
	Mutex::Autolock lock(m_halCBMutex);

	m_dcDircToDvSnap = dcDircToSnap;
}

bool SprdCameraHWInterface2::GetStartPreviewAftPic()
{
	Mutex::Autolock lock(m_halCBMutex);
	return m_IsPrvAftPic;
}

void SprdCameraHWInterface2::SetStartPreviewAftPic(bool IsPicPreview)
{
	Mutex::Autolock lock(m_halCBMutex);
	m_IsPrvAftPic = IsPicPreview;
}

int32_t SprdCameraHWInterface2::GetOutputStreamMask()
{
	Mutex::Autolock lock(m_halCBMutex);
	return m_staticReqInfo.outputStreamMask;
}

void SprdCameraHWInterface2::SetOutputStreamMask(int32_t mask)
{
	Mutex::Autolock lock(m_halCBMutex);
	m_staticReqInfo.outputStreamMask = mask;
}

takepicture_mode SprdCameraHWInterface2::GetCameraPictureMode()
{
   Mutex::Autolock lock(m_halCBMutex);
   return m_camCtlInfo.pictureMode;
}

void SprdCameraHWInterface2::SetCameraPictureMode(takepicture_mode mode)
{
	Mutex::Autolock lock(m_halCBMutex);
	m_camCtlInfo.pictureMode = mode;
}

capture_intent SprdCameraHWInterface2::GetCameraCaptureIntent(camera_req_info *reqInfo)
{
   Mutex::Autolock lock(m_halCBMutex);
   return reqInfo->captureIntent;
}

void SprdCameraHWInterface2::SetCameraCaptureIntent(camera_req_info *reqInfo, capture_intent intent)
{
	Mutex::Autolock lock(m_halCBMutex);
	reqInfo->captureIntent = intent;
}

int64_t SprdCameraHWInterface2::GetSensorTimeStamp(camera_req_info *reqInfo){
   Mutex::Autolock lock(m_halCBMutex);
   return reqInfo->sensorTimeStamp;
}

void SprdCameraHWInterface2::SetSensorTimeStamp(camera_req_info *reqInfo, int64_t timestamp){
	Mutex::Autolock lock(m_halCBMutex);
	reqInfo->sensorTimeStamp = timestamp;
}

status_t SprdCameraHWInterface2::Camera2RefreshSrvReq(camera_req_info *srcreq, camera_metadata_t *dstreq)
{
    status_t res = 0;
	Mutex::Autolock lock(m_metaDataMutex);
	camera_metadata_entry_t requestId;
	camera_metadata_entry_t entry;
	if (!srcreq || !dstreq) {
        HAL_LOGE("para is null");
		return BAD_VALUE;
	}
	HAL_LOGD("befset reqid=%d",srcreq->requestID);
    {
		Mutex::Autolock lock(m_afTrigLock);
		uint8_t afStat = (uint8_t)m_focusStat;
		res = find_camera_metadata_entry(dstreq, ANDROID_CONTROL_AF_STATE, &entry);
	    if (res == NAME_NOT_FOUND) {
	        HAL_LOGE("%s not found!",__FUNCTION__);
	    } else if (res == OK) {
	        res = update_camera_metadata_entry(dstreq,entry.index, &afStat, 1, NULL);
	    }
	}
    res = find_camera_metadata_entry(dstreq, ANDROID_REQUEST_ID, &entry);
    if (res == NAME_NOT_FOUND) {
        HAL_LOGE("not found!");
    } else if (res == OK) {
        res = update_camera_metadata_entry(dstreq,entry.index, &(srcreq->requestID), 1, NULL);
    }

	res = find_camera_metadata_entry(dstreq, ANDROID_SENSOR_TIMESTAMP, &entry);
    if (res == NAME_NOT_FOUND) {
        HAL_LOGE("not found!");
    } else if (res == OK) {
		int64_t Time = GetSensorTimeStamp(srcreq);
        res = update_camera_metadata_entry(dstreq,entry.index, &Time, 1, NULL);
    }
	res = find_camera_metadata_entry(dstreq, ANDROID_REQUEST_FRAME_COUNT, &entry);
    if (res == NAME_NOT_FOUND) {
        HAL_LOGE("not found!");
    } else if (res == OK) {
        res = update_camera_metadata_entry(dstreq,entry.index, &(srcreq->frmCnt), 1, NULL);
    }

	{
		uint8_t capintent = 0;
		res = find_camera_metadata_entry(dstreq, ANDROID_CONTROL_CAPTURE_INTENT, &entry);
	    if (res == NAME_NOT_FOUND) {
	        HAL_LOGE("not found!");
	    } else if (res == OK) {
	        capintent = (uint8_t)GetCameraCaptureIntent(srcreq);
	        res = update_camera_metadata_entry(dstreq,entry.index, &capintent, 1, NULL);//note type of data
	    }
	}
	return res;
}

int SprdCameraHWInterface2::coordinate_convert(int *rect_arr,int arr_size,int angle,int is_mirror, cam_size *preview_size, cropZoom *preview_rect)
{
	int i;
	int x1,x2,y1,y2;
	int temp;
	int recHalfWidth;
	int recHalfHeight;
	int centre_x;
	int centre_y;
	int ret = 0;
	int width = preview_size->width;
	int height = preview_size->height;

	HAL_LOGD("coordinate_convert: mPreviewWidth=%d, mPreviewHeight=%d, arr_size=%d, angle=%d, is_mirror=%d \n",
		width, height, arr_size, angle, is_mirror);

	for (i=0;i<arr_size*2;i++) {
		x1 = rect_arr[i*2];
		y1 = rect_arr[i*2+1];
		if(is_mirror)
			x1 = -x1;
	}

	for (i=0;i<arr_size;i++) {
		if (rect_arr[i*4] > rect_arr[i*4+2]) {
			temp					= rect_arr[i*4];
			rect_arr[i*4]		= rect_arr[i*4+2];
			rect_arr[i*4+2] 	= temp;
		}

		if (rect_arr[i*4+1] > rect_arr[i*4+3]) {
			temp					= rect_arr[i*4+1];
			rect_arr[i*4+1] 	  = rect_arr[i*4+3];
			rect_arr[i*4+3] 	= temp;
		}
	}

	for(i=0; i<arr_size*4; i+=2) {
		if(rect_arr[i] < 0) {
			rect_arr[i]= 0;
		}
		if(rect_arr[i] > width) {
			rect_arr[i] = width;
		}

		if(rect_arr[i+1] < 0) {
			rect_arr[i+1]= 0;
		}

		if(rect_arr[i+1] > height) {
			rect_arr[i+1] = height;
		}
	}

	int preview_x = preview_rect->crop_x;
	int preview_y = preview_rect->crop_y;
	int preview_w = preview_rect->crop_w;
	int preview_h = preview_rect->crop_h;

	for (i=0;i<arr_size;i++) {
		int point_x, point_y;

		HAL_LOGD("%d: org: %d, %d, %d, %d.\n",
					i,rect_arr[i*4],rect_arr[i*4+1],
					rect_arr[i*4+2],rect_arr[i*4+3]);

		// only for angle 90/270
		// calculate the centre point
		recHalfHeight	= (rect_arr[i*4+2] - rect_arr[i*4])/2;
		recHalfWidth	= (rect_arr[i*4+3] - rect_arr[i*4+1])/2;
		centre_y		= rect_arr[i*4+2] - recHalfHeight;
		centre_x		= rect_arr[i*4+3] - recHalfWidth;

        recHalfWidth	= (rect_arr[i*4+2] - rect_arr[i*4])/2;
		recHalfHeight	= (rect_arr[i*4+3] - rect_arr[i*4+1])/2;
		centre_y		= rect_arr[i*4+3] - recHalfHeight;
		centre_x		= rect_arr[i*4+2] - recHalfWidth;
		//HAL_LOGV("%d: center point: x=%d, y=%d\n", i, centre_x, centre_y);

		// map to sensor coordinate
		//centre_y		= height - centre_y;
		//HAL_LOGV("%d: sensor centre pointer: x=%d, y=%d, half_w=%d, half_h=%d.\n",
				//i, centre_x, centre_y, recHalfWidth, recHalfHeight);

		point_x = preview_x + centre_x*preview_w/width;
		point_y = preview_y + centre_y*preview_h/height;
		//HAL_LOGV("%d: out point: x=%d, y=%d\n", i, point_x, point_y);

		rect_arr[i*4]		= point_x - recHalfWidth;
		rect_arr[i*4+1] 	= point_y - recHalfHeight;
		rect_arr[i*4+2] 	= point_x + recHalfWidth;
		rect_arr[i*4+3] 	= point_y + recHalfHeight;

		HAL_LOGD("%d: final: %d, %d, %d, %d.\n",i,rect_arr[i*4],rect_arr[i*4+1],rect_arr[i*4+2],rect_arr[i*4+3]);
		rect_arr[i*4+2] = (((rect_arr[i*4+2] - rect_arr[i*4])+ 3)>> 2) << 2;
		rect_arr[i*4+3] = (((rect_arr[i*4+3] - rect_arr[i*4+1])+ 3)>> 2) << 2;
	}

	return ret;
}


int SprdCameraHWInterface2::CameraConvertCropRegion(uint32_t sensorWidth, uint32_t sensorHeight, cropZoom *cropRegion)
{
	float    minOutputRatio;
	float    zoomWidth,zoomHeight,zoomRatio;
	uint32_t i = 0;
	int ret = 0;
	uint32_t      sensorOrgW = 0, sensorOrgH = 0, dstW = 0, dstH = 0;
	uint32_t IsRotate = 0;
	substream_parameters_t *subParameters = &m_subStreams[STREAM_ID_JPEG];
	HAL_LOGD("crop %d %d %d %d sens w/h %d %d.",
		  cropRegion->crop_x, cropRegion->crop_y, cropRegion->crop_w ,cropRegion->crop_h,sensorWidth,sensorHeight);
	if(m_CameraId == 0) {
		sensorOrgW = BACK_SENSOR_ORIG_WIDTH;
		sensorOrgH = BACK_SENSOR_ORIG_HEIGHT;
	} else if(m_CameraId == 1) {
		sensorOrgW = FRONT_SENSOR_ORIG_WIDTH;
		sensorOrgH = FRONT_SENSOR_ORIG_HEIGHT;
	}
    if (sensorWidth == 0 || sensorHeight == 0 || cropRegion->crop_w == 0
		|| cropRegion->crop_h == 0){
        HAL_LOGE("parameters error.");
		return 1;
    }
	IsRotate = camera_get_rot_set();
	if (sensorWidth == sensorOrgW && sensorHeight == sensorOrgH && !IsRotate) {
		HAL_LOGE("dont' need to convert.");
		return 0;
	}
	zoomWidth = (float)cropRegion->crop_w;
	zoomHeight = (float)cropRegion->crop_h;
	//get dstRatio and zoomRatio frm framework
	minOutputRatio = zoomWidth / zoomHeight;
	if (minOutputRatio > (float)(sensorOrgW / sensorOrgH)) {
		zoomRatio = sensorOrgW / zoomWidth;
	} else {
		zoomRatio = sensorOrgH / zoomHeight;
	}
	if(IsRotate) {
		minOutputRatio = 1 / minOutputRatio;
	}
	if (minOutputRatio > (float)(sensorWidth / sensorHeight)) {
		zoomWidth = sensorWidth / zoomRatio;
		zoomHeight = zoomWidth / minOutputRatio;
	} else {
		zoomHeight = sensorHeight / zoomRatio;
		zoomWidth = zoomHeight * minOutputRatio;
	}
	cropRegion->crop_x = ((uint32_t)(sensorWidth - zoomWidth) >> 1) & ALIGN_ZOOM_CROP_BITS;
    cropRegion->crop_y = ((uint32_t)(sensorHeight - zoomHeight) >> 1) & ALIGN_ZOOM_CROP_BITS;

	cropRegion->crop_w = ((uint32_t)zoomWidth) & ALIGN_ZOOM_CROP_BITS;
	cropRegion->crop_h = ((uint32_t)zoomHeight) & ALIGN_ZOOM_CROP_BITS;
	if(IsRotate) {
		dstW = subParameters->height;
		dstH = subParameters->width;
	} else {
		dstW = subParameters->width;
		dstH = subParameters->height;
	}
	if((dstW > (cropRegion->crop_w * 4)) || (dstH > (cropRegion->crop_h * 4))) {
		HAL_LOGE("%s:Not scaleup over 4times (src=%d %d,dst=%d %d)",__FUNCTION__ ,
		cropRegion->crop_w, cropRegion->crop_h, dstW, dstH);
		return 1;
	}
    HAL_LOGD("Crop calculated (x=%d,y=%d,w=%d,h=%d ratio=%f)",
        cropRegion->crop_x, cropRegion->crop_y, cropRegion->crop_w, cropRegion->crop_h, zoomRatio);
	return ret;
}

void SprdCameraHWInterface2::Camera2GetSrvReqInfo( camera_req_info *srcreq, camera_metadata_t *orireq)
{
	status_t res = 0;
	camera_metadata_entry_t entry;
	uint32_t reqCount = 0;
	camera_parm_type drvTag = (camera_parm_type)0;
	uint32_t index = 0;
	cropZoom zoom1 = {0,0,0,0};
	cropZoom zoom = {0,0,0,0};
	cropZoom zoom0 = {0,0,0,0};
	uint16_t wid = 0, height = 0;
	size_t i = 0;
	bool IsSetPara = true;
	bool IsCapIntChange = false;
	float focal_len = 3.43f;
	stream_parameters_t     *targetStreamParms = NULL;
	char value[PROPERTY_VALUE_MAX];
	int32_t frameRate = 0;
	int32_t th_q = 0, jpeg_q = 0;
	int32_t tmpMask = 0;
	takepicture_mode picMode = (takepicture_mode)0;
	Mutex::Autolock lock(m_requestMutex);
    if(!orireq || !srcreq) {
	    HAL_LOGD("Err para is NULL!");
        return;
	}
	#define ASIGNIFNOTEQUAL(x, y, flag) if((x) != (y) || ((x) == (y) && (x) == 0))\
		                            {\
			                            (x) = (y);\
			                            if((void *)flag != NULL)\
			                            {\
											SET_PARM(flag,x);\
										}\
									}

	m_reqIsProcess = true;
	srcreq->ori_req = orireq;
    reqCount = (uint32_t)get_camera_metadata_entry_count(srcreq->ori_req);
	camera_cfg_rot_cap_param_reset();
#ifdef CONFIG_CAMERA_ROTATION_CAPTURE
	SET_PARM(CAMERA_PARAM_ROTATION_CAPTURE, 1);
#else
	SET_PARM(CAMERA_PARAM_ROTATION_CAPTURE, 0);
#endif
	//first get metadata struct
    for (; index < reqCount ; index++) {
        if (get_camera_metadata_entry(srcreq->ori_req, index, &entry)==0) {
            switch (entry.tag) {
			case ANDROID_LENS_FOCAL_LENGTH:
				if (0 != entry.count) {
					HAL_LOGV("focal %f.",entry.data.f[0]);
					SET_PARM(CAMERA_PARM_FOCAL_LENGTH,  (int32_t)(entry.data.f[0]*1000));
				}
				break;
			case ANDROID_JPEG_QUALITY:
				if (0 != entry.count) {
					jpeg_q = entry.data.i32[0];
					HAL_LOGV("jpeg quality %d.",jpeg_q);
					SET_PARM(CAMERA_PARM_JPEGCOMP, jpeg_q);
				}
				break;
			case ANDROID_JPEG_THUMBNAIL_QUALITY:
				if (0 != entry.count) {
					th_q = entry.data.i32[0];
					HAL_LOGV("jpeg thumbnail quality %d.",th_q);
					SET_PARM(CAMERA_PARM_THUMBCOMP, th_q);
				}
				break;
			case ANDROID_CONTROL_AE_TARGET_FPS_RANGE:
                for (i=0 ; i<entry.count ; i++) {
                    HAL_LOGV("frame rate:%d.",entry.data.i32[i]);
                }
				frameRate = entry.data.i32[entry.count-1];
                break;
            case ANDROID_SENSOR_TIMESTAMP:
				if(srcreq->sensorTimeStamp != entry.data.i64[0]) {
					SetSensorTimeStamp(srcreq,entry.data.i64[0]);
				}
				HAL_LOGD("ANDROID_SENSOR_TIMESTAMP (%lld)", entry.data.i64[0]);
                break;
            case ANDROID_REQUEST_TYPE:
				ASIGNIFNOTEQUAL(srcreq->isReprocessing, entry.data.u8[0], (camera_parm_type)NULL)
				HAL_LOGD("ANDROID_REQUEST_TYPE (%d)", entry.data.u8[0]);
				break;
			case ANDROID_JPEG_GPS_COORDINATES:
				if(srcreq->gpsLat != entry.data.d[0] || srcreq->gpsLon != entry.data.d[1] || srcreq->gpsAlt != entry.data.d[2]) {
                    srcreq->gpsLat = entry.data.d[0];
					srcreq->gpsLon = entry.data.d[1];
					srcreq->gpsAlt = entry.data.d[2];
				}
				HAL_LOGD("ANDROID_JPEG_GPS_COORDINATES (%lf %lf %lf)",
						entry.data.d[0], entry.data.d[1],entry.data.d[2]);
				break;
			case ANDROID_JPEG_GPS_TIMESTAMP:
				ASIGNIFNOTEQUAL(srcreq->gpsTimestamp, entry.data.i64[0], (camera_parm_type)NULL);
				break;
			case ANDROID_JPEG_GPS_PROCESSING_METHOD:
			{
				size_t cnt = 0;
				HAL_LOGV("GPS processing method cnt %d %s.",entry.count, (char*)(&entry.data.u8[0]));
				memset(&srcreq->gpsProcMethod[0], 0, 32);
				if (entry.count > 32) {
                    cnt = 32;
                } else {
                    cnt = entry.count;
                }
				memcpy(&srcreq->gpsProcMethod[0],&entry.data.u8[0],cnt);
			}
				break;
			case ANDROID_JPEG_THUMBNAIL_SIZE:
                srcreq->thumbnailJpgSize.width = entry.data.i32[0];
				srcreq->thumbnailJpgSize.height = entry.data.i32[1];
				camera_set_thumbnail_properties(srcreq->thumbnailJpgSize.width,
												srcreq->thumbnailJpgSize.height, th_q);
				HAL_LOGD("ANDROID_JPEG_THUMBNAIL_SIZE (%d %d)",
						srcreq->thumbnailJpgSize.width, srcreq->thumbnailJpgSize.height);
                break;
			case ANDROID_JPEG_ORIENTATION:
			{
				int jpeg_orientation = 0;
				jpeg_orientation = entry.data.i32[0];
				HAL_LOGV("ANDROID_JPEG_ORIENTATION %d.",jpeg_orientation);
				if (jpeg_orientation == -1) {
					HAL_LOGV("rotation not specified or is invalid, defaulting to 0");
					jpeg_orientation = 0;
				} else if (jpeg_orientation % 90) {
					HAL_LOGV("rotation %d is not a multiple of 90 degrees!  Defaulting to zero.",
					jpeg_orientation);
					jpeg_orientation = 0;
				} else {
					// normalize to [0 - 270] degrees
					jpeg_orientation %= 360;
					if (jpeg_orientation < 0) jpeg_orientation += 360;
				}

				SET_PARM(CAMERA_PARM_ENCODE_ROTATION, jpeg_orientation);
			}
				break;
			case ANDROID_CONTROL_MODE:
				ASIGNIFNOTEQUAL(srcreq->ctlMode, (ctl_mode)(entry.data.u8[0] + 1),(camera_parm_type)NULL)
				HAL_LOGD("ANDROID_CONTROL_MODE (%d)", (ctl_mode)(entry.data.u8[0] + 1));
				break;

			case ANDROID_CONTROL_CAPTURE_INTENT:
				{
					capture_intent tmpIntent = (capture_intent)entry.data.u8[0];
					if(srcreq->captureIntent != tmpIntent) {
						if(!(tmpIntent == CAPTURE_INTENT_VIDEO_SNAPSHOT || (srcreq->captureIntent == CAPTURE_INTENT_VIDEO_SNAPSHOT && tmpIntent == CAPTURE_INTENT_VIDEO_RECORD))) {
							IsCapIntChange = true;//for cts testImmediateZoom
						}
						//if (srcreq->captureIntent == CAPTURE_INTENT_VIDEO_SNAPSHOT && tmpIntent == CAPTURE_INTENT_PREVIEW && GetDcDircToDvSnap()) {////for cts testVideoSnapshot
							//SetDcDircToDvSnap(false);
						//}
						SetCameraCaptureIntent(srcreq, tmpIntent);
					}
					HAL_LOGD("ANDROID_CONTROL_CAPTURE_INTENT (%d)", tmpIntent);
				}
				break;

			case ANDROID_SCALER_CROP_REGION:
				zoom0.crop_x = entry.data.i32[0] & ALIGN_ZOOM_CROP_BITS;
				zoom0.crop_y = entry.data.i32[1] & ALIGN_ZOOM_CROP_BITS;
				zoom0.crop_w = entry.data.i32[2] & ALIGN_ZOOM_CROP_BITS;
				zoom0.crop_h = entry.data.i32[3] & ALIGN_ZOOM_CROP_BITS;
				if (zoom0.crop_x != srcreq->cropRegion0 || zoom0.crop_y != srcreq->cropRegion1\
					  || zoom0.crop_w != srcreq->cropRegion2 || zoom0.crop_h != srcreq->cropRegion3) {
					  //|| m_Stream[STREAM_ID_PREVIEW - 1]->getHalStopMsg()) {
				    srcreq->cropRegion0 = zoom0.crop_x;
					srcreq->cropRegion1 = zoom0.crop_y;
					srcreq->cropRegion2 = zoom0.crop_w;
					srcreq->cropRegion3 = zoom0.crop_h;
					srcreq->isCropSet = true;
				}

				HAL_LOGD("ANDROID_SCALER_CROP_REGION (%d %d %d %d)",
						zoom0.crop_x,zoom0.crop_y,zoom0.crop_w,zoom0.crop_h);
				break;
		    case ANDROID_CONTROL_AF_MODE:
				{
                    int8_t AfMode = 0;
					res = androidAfModeToDrvAfMode((camera_metadata_enum_android_control_af_mode_t)entry.data.u8[0], &AfMode);
					HAL_LOGD("af mode (%d) ret=%d",AfMode,res);
					if (res) {
						HAL_LOGE("ERR: af not support");
					}
					res = androidParametTagToDrvParaTag(ANDROID_CONTROL_AF_MODE, &drvTag);
					if (res) {
						HAL_LOGE("ERR: drv not support af mode");
					}
					if (m_CameraId == 0) {
					    ASIGNIFNOTEQUAL(srcreq->afMode, AfMode,drvTag)
					}
				}
                break;
            case ANDROID_CONTROL_AF_REGIONS:
				{
					int area[5 + 1] = {0};

					if (entry.count == 5) {
						area[0] = 1;
		                for (i=1 ; i < entry.count; i++)
		                    area[i] = entry.data.i32[i - 1];
					}
					HAL_LOGD("ANDROID_CONTROL_AF_REGIONS (%d %d %d %d %d cnt=%d)",
							area[1],area[2],area[3],area[4],entry.data.i32[4],entry.count);
					area[3] = area[3] - area[1];
					area[4] = area[4] - area[2];
					if (mCameraState.preview_state == SPRD_PREVIEW_IN_PROGRESS) {
						zoom.crop_x = area[1];
						zoom.crop_y = area[2];
						zoom.crop_w = area[3];
						zoom.crop_h = area[4];
						camera_get_sensor_mode_trim(1, &zoom1, &wid, &height);
						CameraConvertCropRegion(zoom1.crop_w,zoom1.crop_h,&zoom);
						area[1] = zoom.crop_x;
						area[2] = zoom.crop_y;
						area[3] = zoom.crop_w;
						area[4] = zoom.crop_h;
					}
					SET_PARM(CAMERA_PARM_FOCUS_RECT, (int32_t)area);
				}
                break;
            case ANDROID_CONTROL_AE_REGIONS:
				{
					int area[5 + 1] = {0};//single point
					if (entry.count == 5) {
						area[0] = 1;
		                for (i=1 ; i < entry.count; i++)
		                    area[i] = entry.data.i32[i - 1];
					}
					HAL_LOGD("ANDROID_CONTROL_AE_REGIONS (%d %d %d %d %d cnt=%d)",
							area[1],area[2],area[3],area[4],entry.data.i32[4],entry.count);
				}
                break;
			case ANDROID_FLASH_MODE:
				{
					int8_t FlashMode = 0;

					res = androidFlashModeToDrvFlashMode((camera_metadata_enum_android_flash_mode_t)entry.data.u8[0], &FlashMode);
					HAL_LOGD("flash mode (%d) ret=%d", FlashMode,res);
					if (CAMERA_FLASH_MODE_TORCH == FlashMode) {
						res = androidParametTagToDrvParaTag(ANDROID_FLASH_MODE, &drvTag);
						if (res) {
							HAL_LOGE("ERR: drv not support flash mode");
						}
						if (m_CameraId == 0) {
						    ASIGNIFNOTEQUAL(srcreq->flashMode, FlashMode, drvTag)
						}
					}
				}
                break;
			case ANDROID_CONTROL_AE_MODE:
                {
					int8_t FlashMode = 0;

					res = androidAeModeToDrvAeMode((camera_metadata_enum_android_control_ae_mode_t)entry.data.u8[0], &FlashMode);
					HAL_LOGD("ae flash mode (%d) ret=%d", FlashMode,res);
					res = androidParametTagToDrvParaTag(ANDROID_CONTROL_AE_MODE, &drvTag);
					if (res) {
						HAL_LOGE("ERR: drv not support ae flash mode");
					}
					if (m_CameraId == 0) {
					    ASIGNIFNOTEQUAL(srcreq->aeFlashMode, FlashMode, drvTag)
					}
					//SET_PARM(CAMERA_PARM_AUTO_EXPOSURE_MODE, CAMERA_AE_FRAME_AVG);
				}
                break;
			case ANDROID_CONTROL_AE_LOCK:
				ASIGNIFNOTEQUAL(srcreq->aeLock, (ae_lock)entry.data.u8[0],(camera_parm_type)NULL)
                break;
			case ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION:
				res = androidParametTagToDrvParaTag(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION, &drvTag);
				if (res) {
					HAL_LOGE("ERR: drv not support ae flash mode");
				}
				ASIGNIFNOTEQUAL(srcreq->aeCompensation, entry.data.i32[0] + 3, drvTag)
                HAL_LOGD("DEBUG: ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION (%d)", entry.data.i32[0]);
                break;
            case ANDROID_CONTROL_AWB_MODE:
                {
					int8_t AwbMode = 0;

					res = androidAwbModeToDrvAwbMode((camera_metadata_enum_android_control_awb_mode_t)entry.data.u8[0], &AwbMode);
					HAL_LOGD("ae flash mode (%d) ret=%d", AwbMode,res);
					res = androidParametTagToDrvParaTag(ANDROID_CONTROL_AWB_MODE, &drvTag);
					if (res) {
						HAL_LOGE("ERR: drv not support ae flash mode");
					}
					ASIGNIFNOTEQUAL(srcreq->awbMode, AwbMode, drvTag)
				}
                break;
            case ANDROID_CONTROL_AWB_LOCK:
                ASIGNIFNOTEQUAL(srcreq->awbLock, (awb_lock)entry.data.u8[0],(camera_parm_type)NULL)
                break;
            case ANDROID_REQUEST_ID:
				ASIGNIFNOTEQUAL(srcreq->requestID, entry.data.i32[0],(camera_parm_type)NULL)
                HAL_LOGD("ANDROID_REQUEST_ID (%d)", entry.data.i32[0]);
                break;
            case ANDROID_REQUEST_METADATA_MODE:
				ASIGNIFNOTEQUAL(srcreq->metadataMode, (metadata_mode)entry.data.u8[0],(camera_parm_type)NULL)
                HAL_LOGD("ANDROID_REQUEST_METADATA_MODE (%d)", (metadata_mode)entry.data.u8[0]);
                break;
            case ANDROID_REQUEST_OUTPUT_STREAMS:
				for(i = 0; i < entry.count; i++){
                    tmpMask |= 1 << entry.data.i32[i];//int32_t data type
                    HAL_LOGD("ANDROID_REQUEST_OUTPUT_STREAMS (0x%x)", tmpMask);
				}
                break;
            case ANDROID_REQUEST_FRAME_COUNT:
				ASIGNIFNOTEQUAL(srcreq->frmCnt,entry.data.i32[0],(camera_parm_type)NULL)
                HAL_LOGD("ANDROID_REQUEST_FRAME_COUNT (%d)", entry.data.i32[0]);
                break;
            case ANDROID_CONTROL_SCENE_MODE:
				{
					int8_t   sceneMode;
					res = androidSceneModeToDrvMode((camera_metadata_enum_android_control_scene_mode_t)entry.data.u8[0], &sceneMode);
					HAL_LOGD("drv mode (%d) ret=%d", sceneMode,res);
					if (res) {
						HAL_LOGE("ERR: Scene not support");
					}
					res = androidParametTagToDrvParaTag(ANDROID_CONTROL_SCENE_MODE, &drvTag);
					if (res) {
						HAL_LOGE("ERR: drv not support scene mode");
					}
					ASIGNIFNOTEQUAL(srcreq->sceneMode, sceneMode,drvTag)
				}
                break;
            default:
                //HAL_LOGD("DEBUG(%s):Bad Metadata tag (%d)",  __FUNCTION__, entry.tag);
                break;
            }
        }
    }
	if (tmpMask) {
		Mutex::Autolock lock(m_halCBMutex);
		if (srcreq->outputStreamMask != tmpMask) {
			srcreq->outputStreamMask = tmpMask;
		}
	}
    if (((mCameraState.preview_state == SPRD_IDLE && srcreq->captureIntent == CAPTURE_INTENT_VIDEO_RECORD) || \
		(srcreq->captureIntent == CAPTURE_INTENT_STILL_CAPTURE) || IsCapIntChange) && srcreq->isCropSet == false){
		srcreq->cropRegion0 = zoom0.crop_x;
		srcreq->cropRegion1 = zoom0.crop_y;
		srcreq->cropRegion2 = zoom0.crop_w;
		srcreq->cropRegion3 = zoom0.crop_h;
		srcreq->isCropSet = true;
    }
	if (srcreq->isCropSet) {
		res = androidParametTagToDrvParaTag(ANDROID_SCALER_CROP_REGION, &drvTag);
		if (res) {
			HAL_LOGE("ERR: drv not support zoom");
		}
		zoom.crop_x = srcreq->cropRegion0;
		zoom.crop_y = srcreq->cropRegion1;
		zoom.crop_w = srcreq->cropRegion2;
		zoom.crop_h = srcreq->cropRegion3;
		if (mCameraState.preview_state == SPRD_PREVIEW_IN_PROGRESS
			&& srcreq->captureIntent != CAPTURE_INTENT_STILL_CAPTURE) {
			if (!(srcreq->captureIntent == CAPTURE_INTENT_VIDEO_RECORD && IsCapIntChange)) {//for cts testVideoSnapshot
				camera_get_sensor_mode_trim(1, &zoom1, &wid, &height);
				CameraConvertCropRegion(zoom1.crop_w,zoom1.crop_h,&zoom);
				SET_PARM(drvTag,(uint32_t)&zoom);
				srcreq->isCropSet = false;
			}
		}
	}
	property_get("camera.disable_zsl_mode", value, "0");
    if (!strcmp(value,"1")) {
		picMode = CAMERA_NORMAL_MODE;
    } else {
		picMode = CAMERA_ZSL_MODE;
    }
	if (CAPTURE_INTENT_VIDEO_RECORD == srcreq->captureIntent
		|| CAPTURE_INTENT_VIDEO_SNAPSHOT == srcreq->captureIntent) {
		picMode = CAMERA_ZSL_MODE;
		SET_PARM(CAMERA_PARM_PREVIEW_ENV, frameRate);
	} else {
		SET_PARM(CAMERA_PARM_PREVIEW_ENV, CAMERA_PREVIEW_MODE_SNAPSHOT);
	}
	SetCameraPictureMode(picMode);
	HAL_LOGD("picmode=%d", picMode);
    //stream process later
    if ((srcreq->outputStreamMask & STREAM_MASK_PREVIEW || srcreq->outputStreamMask & STREAM_MASK_PRVCB) && \
		    (srcreq->captureIntent == CAPTURE_INTENT_VIDEO_RECORD || srcreq->captureIntent == CAPTURE_INTENT_PREVIEW)) {
	    if (m_Stream[STREAM_ID_PREVIEW - 1] != NULL) {
	        targetStreamParms = &(m_Stream[STREAM_ID_PREVIEW - 1]->m_parameters);
	    }
        if (m_Stream[STREAM_ID_PREVIEW - 1]->getHalStopMsg()) {
		    int res = 0;
			m_Stream[STREAM_ID_PREVIEW - 1]->setHalStopMsg(false);
			if (mCameraState.preview_state == SPRD_IDLE) {
				IsSetPara = false;
				m_IsPrvAftPic = true;
				if (srcreq->isCropSet) {
					if (CAMERA_ZSL_MODE == GetCameraPictureMode()) {
						camera_get_sensor_mode_trim(2, &zoom1, &wid, &height);
					} else {
						camera_get_sensor_mode_trim(0, &zoom1, &wid, &height);
					}
					CameraConvertCropRegion(zoom1.crop_w,zoom1.crop_h,&zoom);
					SET_PARM(drvTag,(uint32_t)&zoom);
					srcreq->isCropSet = false;
				}
	            setCameraState(SPRD_INTERNAL_PREVIEW_REQUESTED, STATE_PREVIEW);
				#ifndef PREVIEW_USE_DCAM_BUF
				for (i=0 ;i < (size_t)targetStreamParms->numSvcBuffers; i++) {
		           res = targetStreamParms->streamOps->cancel_buffer(targetStreamParms->streamOps, &(targetStreamParms->svcBufHandle[i]));
				   if (res) {
		              HAL_LOGE("cancelbuf res=%d",res);
				   }
				   targetStreamParms->svcBufStatus[i] = ON_HAL_INIT;
				}
				m_Stream[STREAM_ID_PREVIEW-1]->releaseBufQ();
				//#ifndef PREVIEW_USE_DCAM_BUF
				getPreviewBuffer();
				if (camera_set_preview_mem((uint32_t)mPreviewHeapArray_phy,
							(uint32_t)mPreviewHeapArray_vir,
							(targetStreamParms->width * targetStreamParms->height * 3)/2,
							(uint32_t)targetStreamParms->numSvcBuffers)) {
						HAL_LOGE("set preview mem error.");
						return ;
				}
				#else
				if (camera_set_preview_mem((uint32_t)mPreviewHeapArray_phy,
							(uint32_t)mPreviewHeapArray_vir,
							(targetStreamParms->width * targetStreamParms->height * 3)/2,
							mPreviewHeapNum)) {
						HAL_LOGE("set preview mem error.");
						return ;
				}
				#endif
			    camera_ret_code_type qret = camera_start_preview(camera_cb, this,GetCameraPictureMode());
				if (qret != CAMERA_SUCCESS) {
					HAL_LOGE("startPreview failed: sensor error.");
					setCameraState(SPRD_ERROR, STATE_PREVIEW);
					return ;
				}

				res = WaitForPreviewStart();
				HAL_LOGD("camera_start_preview X ret=%d",res);
			}
		} else {
			if (mCameraState.preview_state == SPRD_INIT || mCameraState.preview_state == SPRD_IDLE) {
				stream_parameters_t *StreamParameter = NULL;
				IsSetPara = false;
				if (m_Stream[STREAM_ID_CAPTURE - 1] == NULL) {
					HAL_LOGV("JPEG stream is NULL.");
					return;
				} else {
					StreamParameter = &m_Stream[STREAM_ID_CAPTURE - 1]->m_parameters;
					HAL_LOGV("capture width=%d.height=%d.",StreamParameter->width,StreamParameter->height);
				}
				if (camera_set_dimensions(StreamParameter->width, StreamParameter->height,
		                     targetStreamParms->width,targetStreamParms->height,
		                     NULL,NULL,true) != 0) {
					HAL_LOGE("set pic size fail");
				}
				if (srcreq->isCropSet) {
					if (CAMERA_ZSL_MODE == GetCameraPictureMode()) {
						camera_get_sensor_mode_trim(2, &zoom1, &wid, &height);
					} else {
						camera_get_sensor_mode_trim(0, &zoom1, &wid, &height);
					}
					CameraConvertCropRegion(zoom1.crop_w,zoom1.crop_h,&zoom);
					SET_PARM(drvTag,(uint32_t)&zoom);
					srcreq->isCropSet = false;
				}
				#ifndef PREVIEW_USE_DCAM_BUF
				getPreviewBuffer();
				#endif
				startPreviewInternal(0);
			} else {
				if ((GetCameraPictureMode() == CAMERA_ZSL_MODE)
					&& (mCameraState.preview_state == SPRD_PREVIEW_IN_PROGRESS)) {
					stream_parameters_t *StreamParameter = NULL;
					uint32_t capW = 0, capH = 0;
					if (m_Stream[STREAM_ID_CAPTURE - 1] == NULL) {
						HAL_LOGV("JPEG stream is NULL.");
						return;
					} else {
						StreamParameter = &m_Stream[STREAM_ID_CAPTURE - 1]->m_parameters;
						HAL_LOGV("capture width=%d.height=%d.",StreamParameter->width,StreamParameter->height);
					}
					if (m_Stream[STREAM_ID_PREVIEW - 1] != NULL) {
						targetStreamParms = &(m_Stream[STREAM_ID_PREVIEW - 1]->m_parameters);
					} else {
						HAL_LOGV("preview stream is NULL.");
						return;
					}
					if ((camera_set_change_size(StreamParameter->width, StreamParameter->height, targetStreamParms->width, targetStreamParms->height) && GetDcDircToDvSnap() == false) || IsCapIntChange) {
						HAL_LOGV("need restart preview.");
						camera_set_stop_preview_mode(1);
						stopPreviewInternal();
						if (!IsCapIntChange) {//for cts testVideoSnapshot start
							if (camera_set_dimensions(StreamParameter->width, StreamParameter->height,
					                     targetStreamParms->width,targetStreamParms->height,
					                     NULL,NULL,true) != 0) {
								HAL_LOGE("set pic size fail");
							}
						} else {//for cts testVideoSnapshot start
							#if 0
							if (camera_set_dimensions(targetStreamParms->width, targetStreamParms->height,
					                     targetStreamParms->width,targetStreamParms->height,
					                     NULL,NULL,true) != 0) {
								HAL_LOGE("%s set pic size fail cts",__FUNCTION__);
							}
							SetDcDircToDvSnap(true);
							#endif
							HAL_LOGV("ent cts testVideoSnapshot scene!");
						}
						if (srcreq->isCropSet) {
							if (CAMERA_ZSL_MODE == GetCameraPictureMode()) {
								camera_get_sensor_mode_trim(2, &zoom1, &wid, &height);
							} else {
								camera_get_sensor_mode_trim(0, &zoom1, &wid, &height);
							}
							CameraConvertCropRegion(zoom1.crop_w,zoom1.crop_h,&zoom);
							SET_PARM(drvTag,(uint32_t)&zoom);
							srcreq->isCropSet = false;
						}
						#ifndef PREVIEW_USE_DCAM_BUF
						getPreviewBuffer();
						#endif
						startPreviewInternal(0);
					}
				}
			}
		}
	} else if((srcreq->captureIntent == CAPTURE_INTENT_STILL_CAPTURE
											|| srcreq->captureIntent == CAPTURE_INTENT_VIDEO_SNAPSHOT)
											&& srcreq->outputStreamMask & STREAM_MASK_JPEG) {
        stream_parameters_t     *targetStreamParms = &(m_Stream[STREAM_ID_CAPTURE - 1]->m_parameters);
		substream_parameters_t  *subParameters = &m_subStreams[STREAM_ID_JPEG];
		stream_parameters_t     *previewStreamParms = &(m_Stream[STREAM_ID_PREVIEW - 1]->m_parameters);
		uint32_t local_width = 0, local_height = 0;
	    uint32_t mem_size0 = 0, mem_size1 = 0;

		HAL_LOGD("mCameraState.capture_state=%d.",mCameraState.capture_state);
		camera_encode_properties_type encode_properties = {0, CAMERA_JPEG, 0};
		// Set Default JPEG encoding--this does not cause a callback
		encode_properties.quality = jpeg_q;

		if (encode_properties.quality < 0) {
			HAL_LOGV("JPEG-image quality %d", encode_properties.quality);
			encode_properties.quality = 100;
		} else {
			HAL_LOGV("Setting JPEG-image quality to %d",encode_properties.quality);
		}

		encode_properties.format = CAMERA_JPEG;
		encode_properties.file_size = 0x0;
		camera_set_encode_properties(&encode_properties);

		if (mCameraState.capture_state == SPRD_INIT || mCameraState.capture_state == SPRD_IDLE) {
            IsSetPara = false;
			if(GetCameraPictureMode() == CAMERA_NORMAL_MODE) {
                if(mCameraState.preview_state == SPRD_PREVIEW_IN_PROGRESS) {
					m_Stream[STREAM_ID_PREVIEW - 1]->setHalStopMsg(true);
					camera_set_stop_preview_mode(0);
					HAL_LOGD("stop preview bef picture");
			        stopPreviewSimple();
				}
				if (camera_set_dimensions(subParameters->width,subParameters->height,
									m_Stream[STREAM_ID_PREVIEW - 1]->m_parameters.width,\
									m_Stream[STREAM_ID_PREVIEW - 1]->m_parameters.height,NULL,NULL,true) != 0) {
					HAL_LOGE("set pic size fail");
				}
				if (camera_capture_max_img_size(&local_width, &local_height)) {
					HAL_LOGE("camera_capture_max_img_size fail");
					return ;
				}
				if (camera_capture_get_buffer_size(m_CameraId, local_width, local_height, &mem_size0, &mem_size1)) {
					HAL_LOGE("camera_capture_get_buffer_size fail");
					return ;
				}
				mRawHeapSize = mem_size0;
				if (!allocateCaptureMem()) {
					HAL_LOGE("allocateCaptureMem fail");
					return ;
				}
				if (camera_set_capture_mem2(0,
										(uint32_t)mRawHeap->phys_addr,
										(uint32_t)mRawHeap->data,
										(uint32_t)mRawHeap->phys_size,
										(uint32_t)Callback_AllocCapturePmem,
										(uint32_t)Callback_FreeCapturePmem,
										(uint32_t)this)) {
					HAL_LOGE("camera_set_capture_mem2 fail");
					freeCaptureMem();
					return ;
				}
			}
			//if (!GetDcDircToDvSnap()) {
				//must set dimensions again
				if (camera_set_dimensions(subParameters->width,subParameters->height,
									m_Stream[STREAM_ID_PREVIEW - 1]->m_parameters.width,\
									m_Stream[STREAM_ID_PREVIEW - 1]->m_parameters.height,NULL,NULL,true) != 0) {
					HAL_LOGE("set pic size fail");
				}
			//}
			SET_PARM(CAMERA_PARM_SHOT_NUM, 1);
			if (srcreq->isCropSet) {
				camera_get_sensor_mode_trim(2, &zoom1, &wid, &height);
				if(CameraConvertCropRegion(zoom1.crop_w,zoom1.crop_h,&zoom)) {
					HAL_LOGE("err: scale up over 4times!");
					IsSetPara = true;
					goto out;
				}
				SET_PARM(drvTag,(uint32_t)&zoom);
				srcreq->isCropSet = false;
			}
			setCameraState(SPRD_INTERNAL_RAW_REQUESTED, STATE_CAPTURE);
			if (CAMERA_SUCCESS != camera_take_picture(camera_cb, this, GetCameraPictureMode())) {
				setCameraState(SPRD_ERROR, STATE_CAPTURE);
				freeCaptureMem();
				HAL_LOGE("takePicture: fail to camera_take_picture.");
				return ;
		    }

			res = WaitForCaptureStart();
		    HAL_LOGD("takePicture: X res=%d",res);
		}
	}
	out:
	if (IsSetPara){
		m_RequestQueueThread->SetSignal(SIGNAL_REQ_THREAD_REQ_DONE);
	}
	srcreq->isCropSet = false;
}
bool SprdCameraHWInterface2::WaitForCaptureDone(void)
{
	Mutex::Autolock stateLock(&mStateLock);
	while (SPRD_IDLE != mCameraState.capture_state
		 && SPRD_ERROR != mCameraState.capture_state) {
		HAL_LOGV("waiting for SPRD_IDLE");
		mStateWait.wait(mStateLock);
		HAL_LOGV("woke up");
	}
	return SPRD_IDLE == mCameraState.capture_state;
}

status_t SprdCameraHWInterface2::cancelPictureInternal(void)
{
	bool result = true;

	HAL_LOGV("start, state = %s", getCameraStateStr(getCaptureState()));

	switch (getCaptureState()) {
	case SPRD_INTERNAL_RAW_REQUESTED:
	case SPRD_WAITING_RAW:
	case SPRD_WAITING_JPEG:
		HAL_LOGV("camera state is %s, stopping picture.", getCameraStateStr(getCaptureState()));
		setCameraState(SPRD_INTERNAL_CAPTURE_STOPPING, STATE_CAPTURE);
		if (0 != camera_stop_capture()) {
			HAL_LOGV("cancelPictureInternal: camera_stop_capture failed!");
			return UNKNOWN_ERROR;
		}
		result = WaitForCaptureDone();
		break;

	default:
		HAL_LOGV("not taking a picture (state %s)", getCameraStateStr(getCaptureState()));
		break;
	}

	HAL_LOGV("end");
	return result ? NO_ERROR : UNKNOWN_ERROR;
}

bool SprdCameraHWInterface2::iSZslMode(void)
{
	Mutex::Autolock lock(m_halCBMutex);
	bool ret = true;
	if ((CAMERA_ZSL_MODE != m_camCtlInfo.pictureMode)
		&& (CAMERA_ZSL_CONTINUE_SHOT_MODE != m_camCtlInfo.pictureMode)) {
		ret = false;
	}
	return ret;
}

bool SprdCameraHWInterface2::isCapturing(void)
{
	HAL_LOGV("state: %s", getCameraStateStr(mCameraState.capture_state));

	return (SPRD_WAITING_RAW == mCameraState.capture_state
			|| SPRD_WAITING_JPEG == mCameraState.capture_state);
}

bool SprdCameraHWInterface2::isPreviewing(void)
{
	HAL_LOGV("state: %s", getCameraStateStr(mCameraState.preview_state));
	return (SPRD_PREVIEW_IN_PROGRESS == mCameraState.preview_state);
}

status_t SprdCameraHWInterface2::startPreviewInternal(bool isRecording)
{
	stream_parameters_t     *StreamParameter = NULL;
	stream_parameters_t     *targetStreamParms = NULL;
	bool  ret = true;
	HAL_LOGV("start isRecording=%d.",isRecording);

	if (isPreviewing()) {
		HAL_LOGE("startPreviewInternal: already in progress, doing nothing.X");
/*		setRecordingMode(isRecording);
		setCameraPreviewMode(isRecordingMode());*/
		return NO_ERROR;
	}

	if (!iSZslMode()) {
		if (isCapturing()) {
			WaitForCaptureDone();
		}

		if (isCapturing() || isPreviewing()) {
			HAL_LOGE("X Capture state is %s, Preview state is %s, expecting SPRD_IDLE!",
			getCameraStateStr(mCameraState.capture_state), getCameraStateStr(mCameraState.preview_state));
			return INVALID_OPERATION;
		}
	}
/*	setRecordingMode(isRecording);*/
	if(m_Stream[STREAM_ID_PREVIEW - 1] != NULL) {
		targetStreamParms = &(m_Stream[STREAM_ID_PREVIEW - 1]->m_parameters);
	} else {
		HAL_LOGE("preview stream is NULL.");
		return SPRD_ERROR;
	}
    //hal parameters set
	SET_PARM(CAMERA_PARM_PREVIEW_MODE, CAMERA_PREVIEW_MODE_SNAPSHOT);
    camerea_set_preview_format(targetStreamParms->format);
	#ifdef PREVIEW_USE_DCAM_BUF
	if (camera_set_preview_mem((uint32_t)mPreviewHeapArray_phy,
				(uint32_t)mPreviewHeapArray_vir,
				(targetStreamParms->width * targetStreamParms->height * 3)/2,
				mPreviewHeapNum)) {
			return SPRD_ERROR;
	}
	#else
	if (camera_set_preview_mem((uint32_t)mPreviewHeapArray_phy,
				(uint32_t)mPreviewHeapArray_vir,
				(targetStreamParms->width * targetStreamParms->height * 3)/2,
				(uint32_t)targetStreamParms->numSvcBuffers)) {
			return SPRD_ERROR;
	}
	#endif
    if (CAMERA_ZSL_MODE == GetCameraPictureMode()) {
        uint32_t local_width = 0, local_height = 0;
        uint32_t mem_size0 = 0, mem_size1 = 0;

        if (camera_capture_max_img_size(&local_width, &local_height)) {
	        HAL_LOGE("camera_capture_max_img_size fail");
	        return SPRD_ERROR;
        }

        if (camera_capture_get_buffer_size(m_CameraId, local_width, local_height, &mem_size0, &mem_size1)) {
	        HAL_LOGE("camera_capture_get_buffer_size fail");
	        return SPRD_ERROR;
        }

        mRawHeapSize = mem_size0;
        if (!allocateCaptureMem()) {
	        HAL_LOGE("allocateCaptureMem fail");
	        return SPRD_ERROR;
        }

        HAL_LOGE("to call camera_set_capture_mem2");
        if (camera_set_capture_mem2(0,
						(uint32_t)mRawHeap->phys_addr,
						(uint32_t)mRawHeap->data,
						(uint32_t)mRawHeap->phys_size,
						(uint32_t)Callback_AllocCapturePmem,
						(uint32_t)Callback_FreeCapturePmem,
						(uint32_t)this)) {
	        HAL_LOGE("camera_set_capture_mem2 fail");
	        freeCaptureMem();
	        return SPRD_ERROR;
        }
    }

    setCameraState(SPRD_INTERNAL_PREVIEW_REQUESTED, STATE_PREVIEW);
    camera_ret_code_type qret = camera_start_preview(camera_cb, this,GetCameraPictureMode());//mode
	HAL_LOGV("camera_start_preview X");
	if (qret != CAMERA_SUCCESS) {
		HAL_LOGE("startPreview failed: sensor error.");
		setCameraState(SPRD_ERROR, STATE_PREVIEW);
		return SPRD_ERROR;
	}

	ret = WaitForPreviewStart();
	HAL_LOGD("end ret=%d",ret);
	return ret ? NO_ERROR : UNKNOWN_ERROR;
}

void SprdCameraHWInterface2::stopPreviewSimple(void)
{
	Mutex::Autolock lock(m_stopPrvFrmCBMutex);
	setCameraState(SPRD_INTERNAL_PREVIEW_STOPPING, STATE_PREVIEW);
	if(CAMERA_SUCCESS != camera_stop_preview()) {
		setCameraState(SPRD_ERROR, STATE_PREVIEW);
		HAL_LOGE("fail to camera_stop_preview()");
	}
	WaitForPreviewStop();
}

void SprdCameraHWInterface2::stopPreviewInternal(void)
{
	nsecs_t start_timestamp = systemTime();
	nsecs_t end_timestamp;
	stream_parameters_t *targetStreamParms = &(m_Stream[STREAM_ID_PREVIEW - 1]->m_parameters);
	status_t res = 0;
	size_t i = 0;
	HAL_LOGV("preview state %s", getCameraStateStr(mCameraState.preview_state));
	if (SPRD_PREVIEW_IN_PROGRESS != mCameraState.preview_state) {
		HAL_LOGV("Preview not in progress!");
		return;
	}
	HAL_LOGV("capture state %s", getCameraStateStr(mCameraState.capture_state));

	if (SPRD_WAITING_RAW == mCameraState.capture_state
		|| SPRD_WAITING_JPEG == mCameraState.capture_state) {
		cancelPictureInternal();
	}
	stopPreviewSimple();
	if (isCapturing()) {
		WaitForCaptureDone();
	}
	if (iSZslMode()) {
		freeCaptureMem();
	}
	if (targetStreamParms != NULL) {
		for (;i < (size_t)targetStreamParms->numSvcBuffers; i++) {
	       res = targetStreamParms->streamOps->cancel_buffer(targetStreamParms->streamOps, &(targetStreamParms->svcBufHandle[i]));
		   if (res) {
	          HAL_LOGE("cancelbuf res=%d",res);
		   }
		   targetStreamParms->svcBufStatus[i] = ON_HAL_INIT;
		}
		m_Stream[STREAM_ID_PREVIEW-1]->releaseBufQ();
	}
	end_timestamp = systemTime();
	HAL_LOGV("end time:%lld(ms).",(end_timestamp - start_timestamp)/1000000);
}

int SprdCameraHWInterface2::displaySubStream(sp<Stream> stream, int32_t *srcBufVirt, int64_t frameTimeStamp, uint16_t subStream)
{
    stream_parameters_t     *StreamParms = &(stream->m_parameters);
    substream_parameters_t  *subParms    = &m_subStreams[subStream];
	void *VirtBuf = NULL;
    int  ret = 0;
    buffer_handle_t * buf = NULL;
	const private_handle_t *priv_handle = NULL;

	if ((NULL == subParms->streamOps) || (NULL == subParms->streamOps->dequeue_buffer)) {
		HAL_LOGE("ERR: haven't stream ops");
		return 0;
	}

	ret = subParms->streamOps->dequeue_buffer(subParms->streamOps, &buf);
    if (ret != NO_ERROR || buf == NULL) {
        HAL_LOGD("prvcb stream(%d) dequeue_buffer fail res(%d)",stream->m_index,  ret);
		return ret;
    }
	//lock
	if (m_grallocHal->lock(m_grallocHal, *buf, subParms->usage, 0, 0,
                   subParms->width, subParms->height, &VirtBuf) != 0) {
		HAL_LOGE("ERRcould not obtain gralloc buffer");
	}
	priv_handle = reinterpret_cast<const private_handle_t *>(*buf);
    switch(subStream)
	{
	case STREAM_ID_RECORD:
	case STREAM_ID_PRVCB:
		if (subParms->format == HAL_PIXEL_FORMAT_YCrCb_420_SP) {
			memcpy((char *)(priv_handle->base),
                    srcBufVirt, (subParms->width * subParms->height * 3)/2);
        }
		break;
	case STREAM_ID_JPEG:
		{
			camera2_jpeg_blob * jpegBlob = NULL;
			memcpy((char *)(priv_handle->base),srcBufVirt, subParms->dataSize);
			jpegBlob = (camera2_jpeg_blob*)((char *)(priv_handle->base) + (priv_handle->size - sizeof(camera2_jpeg_blob)));
	        jpegBlob->jpeg_size = subParms->dataSize;
	        jpegBlob->jpeg_blob_id = CAMERA2_JPEG_BLOB_ID;
		}
		break;
	}
    //unlock
    if (m_grallocHal) {
        m_grallocHal->unlock(m_grallocHal, *buf);
    } else {
        HAL_LOGD("ERR displaySubStream gralloc is NULL");
	}

    ret = subParms->streamOps->enqueue_buffer(subParms->streamOps,
                                               frameTimeStamp,
                                               buf);
    HAL_LOGD("return %d",ret);

    return ret;
}


//transite from 'from' state to 'to' state and signal the waitting thread. if the current state is not 'from', transite to SPRD_ERROR state
//should be called from the callback
SprdCameraHWInterface2::Sprd_camera_state
SprdCameraHWInterface2::transitionState(SprdCameraHWInterface2::Sprd_camera_state from,
									SprdCameraHWInterface2::Sprd_camera_state to,
									SprdCameraHWInterface2::state_owner owner, bool lock)
{
	volatile SprdCameraHWInterface2::Sprd_camera_state *which_ptr = NULL;
	HAL_LOGV("owner = %d, lock = %d", owner, lock);

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
		HAL_LOGV("changeState: error owner");
		break;
	}

	if (NULL != which_ptr) {
		if (from != *which_ptr) {
			to = SPRD_ERROR;
		}
		HAL_LOGV("changeState: %s --> %s", getCameraStateStr(from),
								   getCameraStateStr(to));
		if (*which_ptr != to) {
			*which_ptr = to;
			mStateWait.signal();
		}
	}
	if (lock) mStateLock.unlock();

	return to;
}

void SprdCameraHWInterface2::HandleStartCamera(camera_cb_type cb,
								  		int32_t parm4)
{
	HAL_LOGV("in: cb = %d, parm4 = 0x%x, state = %s",
				cb, parm4, getCameraStateStr(getCameraState()));

	transitionState(SPRD_INIT, SPRD_IDLE, STATE_CAMERA);

	HAL_LOGV("out, state = %s", getCameraStateStr(getCameraState()));
}

void SprdCameraHWInterface2::HandleStartPreview(camera_cb_type cb,
											 int32_t parm4)
{

	HAL_LOGV("in: cb = %d, parm4 = 0x%x, state = %s",
				cb, parm4, getCameraStateStr(getPreviewState()));

	switch(cb) {
	case CAMERA_RSP_CB_SUCCESS:
		transitionState(SPRD_INTERNAL_PREVIEW_REQUESTED,
					SPRD_PREVIEW_IN_PROGRESS,
					STATE_PREVIEW);
		break;

	case CAMERA_EVT_CB_FRAME:
		HAL_LOGV("CAMERA_EVT_CB_FRAME");
		switch (getPreviewState()) {
		case SPRD_PREVIEW_IN_PROGRESS:
			receivePreviewFrame((camera_frame_type *)parm4);
			break;

		case SPRD_INTERNAL_PREVIEW_STOPPING:
			HAL_LOGV("camera cb: discarding preview frame "
			"while stopping preview");
			break;

		default:
			HAL_LOGV("invalid state");
			break;
			}
		break;

	case CAMERA_EXIT_CB_FAILED:
		HAL_LOGE("camera_cb: @CAMERA_EXIT_CB_FAILURE(%d) in state %s.",
				parm4, getCameraStateStr(getPreviewState()));
		transitionState(getPreviewState(), SPRD_ERROR, STATE_PREVIEW);
		//receiveCameraExitError();
		break;
	case CAMERA_EVT_CB_FLUSH:
		{
			if (getPreviewState() != SPRD_PREVIEW_IN_PROGRESS) {
				HAL_LOGD("prv is not previewing!");
				return;
			}
			camera_frame_type *frame = (camera_frame_type *)parm4;
			stream_parameters_t     *targetStreamParms = &(m_Stream[STREAM_ID_PREVIEW - 1]->m_parameters);
			flush_buffer(CAMERA_FLUSH_PREVIEW_HEAP, frame->buf_id,
					(void*)frame->buf_Virt_Addr,
					(void*)frame->buffer_phy_addr,
					(targetStreamParms->width * targetStreamParms->height * 3)/2);
		}
		break;
	default:
		transitionState(getPreviewState(), SPRD_ERROR, STATE_PREVIEW);
		HAL_LOGE("unexpected cb %d for CAMERA_FUNC_START_PREVIEW.", cb);
		break;
	}

	HAL_LOGV("out, state = %s", getCameraStateStr(getPreviewState()));
}

void SprdCameraHWInterface2::receivePreviewFrame(camera_frame_type *frame)
{
	status_t res = 0;
	stream_parameters_t     *targetStreamParms;
	buffer_handle_t * buf = NULL;
	const private_handle_t *priv_handle = NULL;
	bool found = false;
	int Index = 0;
	sp<Stream> StreamSP = NULL;
	#ifndef PREVIEW_USE_DCAM_BUF
	Mutex::Autolock lock(m_stopPrvFrmCBMutex);
	#endif
	if (NULL == frame) {
		HAL_LOGE("invalid frame pointer");
		return;
	}
	#ifdef PREVIEW_USE_DCAM_BUF
	receivePrevFrmWithCacheMem(frame);
	#else
    StreamSP = m_Stream[STREAM_ID_PREVIEW - 1];
	if (StreamSP == NULL) {
		HAL_LOGE("preview stream is NULL.");
		return;
	}

    targetStreamParms = &(m_Stream[STREAM_ID_PREVIEW - 1]->m_parameters);
	targetStreamParms->bufIndex = frame->buf_id;
	targetStreamParms->m_timestamp = frame->timestamp - mRecordingTimeOffset;
	HAL_LOGD("@@@ Index=%d status=%d Firstfrm=%d",targetStreamParms->bufIndex,\
		   targetStreamParms->svcBufStatus[targetStreamParms->bufIndex], StreamSP->m_IsFirstFrm);
    if (GetStartPreviewAftPic()) {
		SetStartPreviewAftPic(false);
            if (StreamSP->m_IsFirstFrm)
			    StreamSP->m_IsFirstFrm = false;
        }
		//check if kick cancel key first
		if (StreamSP->getRecevStopMsg()) {
		    HAL_LOGD("receivePreviewFrame recev stop msg!");
            if (!StreamSP->m_IsFirstFrm) {
                StreamSP->m_IsFirstFrm = true;
				m_RequestQueueThread->SetSignal(SIGNAL_REQ_THREAD_REQ_DONE);
			}
			return;
		}
		//check buf status
		if (targetStreamParms->svcBufStatus[targetStreamParms->bufIndex] != ON_HAL_DRIVER
			&& targetStreamParms->svcBufStatus[targetStreamParms->bufIndex] != ON_HAL_INIT) {
        HAL_LOGE("BufStatus ERR(%d)",
				targetStreamParms->svcBufStatus[targetStreamParms->bufIndex]);
		//targetStreamParms->svcBufStatus[targetStreamParms->bufIndex] = ON_HAL_BUFERR;
			if (!StreamSP->m_IsFirstFrm) {
			    StreamSP->m_IsFirstFrm = true;
				m_RequestQueueThread->SetSignal(SIGNAL_REQ_THREAD_REQ_DONE);
			}
			return;
		}
		//oem stop preview for picture
		if (StreamSP->getHalStopMsg()) {
            HAL_LOGD("receivePreviewFrame hal stop msg!");
			targetStreamParms->svcBufStatus[targetStreamParms->bufIndex] = ON_HAL_BUFQ;
		    StreamSP->pushBufQ(targetStreamParms->bufIndex);
			return;
	}
	if (getPreviewState() != SPRD_PREVIEW_IN_PROGRESS) {
		HAL_LOGD("prv is not previewing!");
		return;
	}
//graphic deq 6 buf
	HAL_LOGD("m_staticReqInfo.outputStreamMask=0x%x.",GetOutputStreamMask());
    if (GetOutputStreamMask() & STREAM_MASK_PRVCB) {
	    int i = 0;
        for (; i < NUM_MAX_SUBSTREAM ; i++) {
            if (StreamSP->m_attachedSubStreams[i].streamId == STREAM_ID_PRVCB) {
				displaySubStream(StreamSP, (int32_t *)(mPreviewHeapArray_vir[targetStreamParms->bufIndex]),\
					      targetStreamParms->m_timestamp,STREAM_ID_PRVCB);
                break;
			}
		}
		if (i == NUM_MAX_SUBSTREAM) {
			HAL_LOGD("err receivepreviewframe cb substream not regist");
		}
	}
	if (GetOutputStreamMask() & STREAM_MASK_RECORD && !GetRecStopMsg()) {
		substream_parameters_t *subParameters;
        subParameters = &m_subStreams[STREAM_ID_RECORD];
	    if (SUBSTREAM_TYPE_RECORD == subParameters->type) {
			displaySubStream(StreamSP, (int32_t *)(mPreviewHeapArray_vir[targetStreamParms->bufIndex]),\
					      targetStreamParms->m_timestamp,STREAM_ID_RECORD);
		}
	}

    if (GetOutputStreamMask() & STREAM_MASK_PREVIEW) {
		HAL_LOGD("@@@ Display Preview,add=0x%x,enqueue index=%d",
			 (uint32_t)targetStreamParms->svcBufHandle[targetStreamParms->bufIndex],targetStreamParms->bufIndex);

		if (targetStreamParms->svcBufStatus[targetStreamParms->bufIndex] == ON_HAL_DRIVER) {
			priv_handle = reinterpret_cast<const private_handle_t *>(targetStreamParms->svcBufHandle[targetStreamParms->bufIndex]);
			/*flush_buffer(CAMERA_FLUSH_PREVIEW_HEAP,
						 priv_handle->share_fd,
						 (void*)mPreviewHeapArray_vir[targetStreamParms->bufIndex],
						 (void*)mPreviewHeapArray_phy[targetStreamParms->bufIndex],
						 priv_handle->size);*/
            res = targetStreamParms->streamOps->enqueue_buffer(targetStreamParms->streamOps,
																targetStreamParms->m_timestamp,
																&(targetStreamParms->svcBufHandle[targetStreamParms->bufIndex]));
			if (res) {
				HAL_LOGD("enqueue fail, ret=%d,buf status=%d.",res, targetStreamParms->svcBufStatus[targetStreamParms->bufIndex]);
				StreamSP->pushBufQ(targetStreamParms->bufIndex);
			    targetStreamParms->svcBufStatus[targetStreamParms->bufIndex] = ON_HAL_BUFQ;
				if (!StreamSP->m_IsFirstFrm) {
					StreamSP->m_IsFirstFrm = true;
					m_RequestQueueThread->SetSignal(SIGNAL_REQ_THREAD_REQ_DONE);
				}
				return;
			} else {
				targetStreamParms->svcBufStatus[targetStreamParms->bufIndex] = ON_SERVICE;
			}

	        if (!StreamSP->m_IsFirstFrm) {
	            StreamSP->m_IsFirstFrm = true;
				m_RequestQueueThread->SetSignal(SIGNAL_REQ_THREAD_REQ_DONE);//important
	        }
		//deq one
			found = false;
			res = targetStreamParms->streamOps->dequeue_buffer(targetStreamParms->streamOps, &buf);
			if (res != NO_ERROR || buf == NULL) {
				HAL_LOGD("dequeue_buffer fail");
				return;
	        }
			priv_handle = reinterpret_cast<const private_handle_t *>(*buf);
			int phyaddr = 0;
			int size = 0;
			int ret = 0;
//			MemoryHeapIon::Get_phy_addr_from_ion(priv_handle->share_fd,&phyaddr,&size);
			if(s_mem_method == 0){
				MemoryHeapIon::Get_phy_addr_from_ion(priv_handle->share_fd,&phyaddr,&size);
			} else {
				MemoryHeapIon::Get_mm_iova(priv_handle->share_fd,&phyaddr,&size);
			}

			for (Index = 0; Index < targetStreamParms->numSvcBuffers ; Index++) {
	            if (phyaddr == mPreviewHeapArray_phy[Index] &&
					(targetStreamParms->svcBufStatus[Index] == ON_SERVICE || targetStreamParms->svcBufStatus[Index] == ON_HAL_INIT \
	                || targetStreamParms->svcBufStatus[Index] == ON_HAL_BUFQ)) {
	                found = true;
					HAL_LOGD("receivePreviewFrame,Index=%d sta=%d",Index,targetStreamParms->svcBufStatus[Index]);
	                break;
	            }
	        }
			if (!found) {
				HAL_LOGE("ERR receivepreviewframe cannot found buf=0x%x ",phyaddr);
				if(s_mem_method != 0) {
					 MemoryHeapIon::Free_mm_iova(priv_handle->share_fd,phyaddr, size);
				}
				return;
			}
			if(s_mem_method != 0) {
				 MemoryHeapIon::Free_mm_iova(priv_handle->share_fd,phyaddr, size);
			}
			if (targetStreamParms->svcBufStatus[Index] != ON_HAL_BUFQ) {
				StreamSP->pushBufQ(Index);
				targetStreamParms->svcBufStatus[Index] = ON_HAL_BUFQ;
			} else {
				targetStreamParms->svcBufStatus[Index] = ON_HAL_BUFQ;
			}
			Index = StreamSP->popBufQ();
			HAL_LOGD("@@@ receivePreviewFrame newindex=%d",Index);
			res = camera_release_frame(Index);
			if (res) {
               HAL_LOGD("ERR receivepreviewframe release buf deq from graphic");
			   StreamSP->pushBufQ(Index);
			} else {
			   targetStreamParms->svcBufStatus[Index] = ON_HAL_DRIVER;
			}
		} else {
		    StreamSP->pushBufQ(targetStreamParms->bufIndex);
			targetStreamParms->svcBufStatus[targetStreamParms->bufIndex] = ON_HAL_BUFQ;
			Index = StreamSP->popBufQ();
			HAL_LOGD("receivepreviewframe not equal srv bufq, index=%d",Index);
            res = camera_release_frame(Index);
			if (res) {
               HAL_LOGD("ERR receivepreviewframe release buf deq from graphic");
			   StreamSP->pushBufQ(Index);
			} else {
			   targetStreamParms->svcBufStatus[Index] = ON_HAL_DRIVER;
			}

			if (!StreamSP->m_IsFirstFrm) {
			    StreamSP->m_IsFirstFrm = true;
				m_RequestQueueThread->SetSignal(SIGNAL_REQ_THREAD_REQ_DONE);//important
			}
		}
    } else {
		StreamSP->pushBufQ(targetStreamParms->bufIndex);
		targetStreamParms->svcBufStatus[targetStreamParms->bufIndex] = ON_HAL_BUFQ;
		Index = StreamSP->popBufQ();
		HAL_LOGD("stream not output,popIndex=%d",Index);
		res = camera_release_frame(Index);
		if (res) {
             HAL_LOGD("ERR receivepreviewframe2 release frame!");
			 StreamSP->pushBufQ(Index);
		} else {
		    targetStreamParms->svcBufStatus[Index] = ON_HAL_DRIVER;
		}
		if (!StreamSP->m_IsFirstFrm) {
			StreamSP->m_IsFirstFrm = true;
			m_RequestQueueThread->SetSignal(SIGNAL_REQ_THREAD_REQ_DONE);//important
		}
	}
	#endif
}

#ifdef PREVIEW_USE_DCAM_BUF
void SprdCameraHWInterface2::receivePrevFrmWithCacheMem(camera_frame_type *frame)
{
	status_t res = 0;
	stream_parameters_t     *targetStreamParms;
	buffer_handle_t * buf = NULL;
	const private_handle_t *priv_handle = NULL;
	sp<Stream> StreamSP = NULL;
	void *VirtBuf = NULL;
	Mutex::Autolock lock(m_stopPrvFrmCBMutex);
	if (NULL == frame) {
		HAL_LOGE("invalid frame pointer");
		return;
	}

    StreamSP = m_Stream[STREAM_ID_PREVIEW - 1];
	if (StreamSP == NULL) {
		HAL_LOGE("preview stream is NULL.");
		return;
	}
	if (GetStartPreviewAftPic()) {
		SetStartPreviewAftPic(false);
            if (StreamSP->m_IsFirstFrm)
			    StreamSP->m_IsFirstFrm = false;
        }
		//check if kick cancel key first
		if (StreamSP->getRecevStopMsg()) {
		    HAL_LOGD("recev stop msg!");
            if (!StreamSP->m_IsFirstFrm) {
                StreamSP->m_IsFirstFrm = true;
				m_RequestQueueThread->SetSignal(SIGNAL_REQ_THREAD_REQ_DONE);
			}
			return;
		}
		//oem stop preview for picture
		if (StreamSP->getHalStopMsg()) {
            HAL_LOGD("hal stop msg!");
			return;
	}
	if (getPreviewState() != SPRD_PREVIEW_IN_PROGRESS) {
		HAL_LOGD("prv is not previewing!");
		return;
	}
    targetStreamParms = &(m_Stream[STREAM_ID_PREVIEW - 1]->m_parameters);
	targetStreamParms->bufIndex = frame->buf_id;
	targetStreamParms->m_timestamp = frame->timestamp - mRecordingTimeOffset;
	HAL_LOGD("%s@@@ Index=%d status=%d Firstfrm=%d outMask=0x%x",__FUNCTION__,targetStreamParms->bufIndex,\
		   targetStreamParms->svcBufStatus[targetStreamParms->bufIndex], StreamSP->m_IsFirstFrm, GetOutputStreamMask());
    if (GetOutputStreamMask() & STREAM_MASK_PRVCB) {
	    int i = 0;
        for (; i < NUM_MAX_SUBSTREAM ; i++) {
            if (StreamSP->m_attachedSubStreams[i].streamId == STREAM_ID_PRVCB) {
				if (camera_get_rot_set()) {
					displaySubStream(StreamSP, (int32_t *)(frame->buf_Virt_Addr),\
						      targetStreamParms->m_timestamp,STREAM_ID_PRVCB);
				} else {
					displaySubStream(StreamSP, (int32_t *)(mPreviewHeapArray_vir[targetStreamParms->bufIndex]),\
						      targetStreamParms->m_timestamp,STREAM_ID_PRVCB);
				}
                break;
			}
		}
		if (i == NUM_MAX_SUBSTREAM) {
			HAL_LOGD("err cb substream not regist");
		}
	}
	if (GetOutputStreamMask() & STREAM_MASK_RECORD && !GetRecStopMsg()) {
		substream_parameters_t *subParameters;
        subParameters = &m_subStreams[STREAM_ID_RECORD];
	    if (SUBSTREAM_TYPE_RECORD == subParameters->type) {
			if (camera_get_rot_set()) {
				displaySubStream(StreamSP, (int32_t *)(frame->buf_Virt_Addr),\
						      targetStreamParms->m_timestamp,STREAM_ID_RECORD);
			} else {
				displaySubStream(StreamSP, (int32_t *)(mPreviewHeapArray_vir[targetStreamParms->bufIndex]),\
						      targetStreamParms->m_timestamp,STREAM_ID_RECORD);
			}
		}
	}

    if (GetOutputStreamMask() & STREAM_MASK_PREVIEW) {
		res = targetStreamParms->streamOps->dequeue_buffer(targetStreamParms->streamOps, &buf);
		if (res != NO_ERROR || buf == NULL) {
			HAL_LOGD("DEBUG(%s): dequeue_buffer fail",__FUNCTION__);
			if (!StreamSP->m_IsFirstFrm) {
				StreamSP->m_IsFirstFrm = true;
				m_RequestQueueThread->SetSignal(SIGNAL_REQ_THREAD_REQ_DONE);
			}
			return;
        }
		if (m_grallocHal->lock(m_grallocHal, *buf, targetStreamParms->usage, 0, 0,
                   targetStreamParms->width, targetStreamParms->height, &VirtBuf) != 0) {
			HAL_LOGE("ERR: could not obtain gralloc buffer");
		}
		priv_handle = reinterpret_cast<const private_handle_t *>(*buf);
		if (camera_get_rot_set()) {
			memcpy((char *)(priv_handle->base),(char *)(frame->buf_Virt_Addr),
							(targetStreamParms->width * targetStreamParms->height * 3)/2);
		} else {
			memcpy((char *)(priv_handle->base),(char *)(mPreviewHeapArray_vir[targetStreamParms->bufIndex]),
							(targetStreamParms->width * targetStreamParms->height * 3)/2);
		}
		if (m_grallocHal) {
			m_grallocHal->unlock(m_grallocHal, *buf);
		} else {
			HAL_LOGD("ERR gralloc is NULL");
		}
		res = targetStreamParms->streamOps->enqueue_buffer(targetStreamParms->streamOps,
												   targetStreamParms->m_timestamp,
												   buf);
		if(res) {
			HAL_LOGE("ERR could not enq gralloc buffer,res=%d", res);
			if (!StreamSP->m_IsFirstFrm) {
				StreamSP->m_IsFirstFrm = true;
				m_RequestQueueThread->SetSignal(SIGNAL_REQ_THREAD_REQ_DONE);
			}
			return;
		} else {
			HAL_LOGD("release frame start");
			res = camera_release_frame(targetStreamParms->bufIndex);
			if (res) {
	             HAL_LOGD("ERR release frame!");
			}
			if (!StreamSP->m_IsFirstFrm) {
				StreamSP->m_IsFirstFrm = true;
				m_RequestQueueThread->SetSignal(SIGNAL_REQ_THREAD_REQ_DONE);//important
			}
		}
    } else {
		HAL_LOGD("stream not output");
		res = camera_release_frame(targetStreamParms->bufIndex);
		if (res) {
             HAL_LOGD("ERR release frame!");
		}
		if (!StreamSP->m_IsFirstFrm) {
			StreamSP->m_IsFirstFrm = true;
			m_RequestQueueThread->SetSignal(SIGNAL_REQ_THREAD_REQ_DONE);//important
		}
	}
}
#endif

void SprdCameraHWInterface2::RequestQueueThreadFunc(SprdBaseThread * self)
{
	camera_metadata_t *curReq = NULL;
	camera_metadata_t *newReq = NULL;
	size_t numEntries = 0;
	size_t frameSize = 0;
	uint32_t currentSignal = self->GetProcessingSignal();
	RequestQueueThread *  selfThread      = ((RequestQueueThread*)self);
	int ret;

    HAL_LOGD("RequestQueueThread (%x)", currentSignal);

    if (currentSignal & SIGNAL_THREAD_RELEASE) {
        ClearReqQ();
        HAL_LOGD("processing SIGNAL_THREAD_RELEASE DONE");
        selfThread->SetSignal(SIGNAL_THREAD_TERMINATE);
        return;
    }

    if (currentSignal & SIGNAL_REQ_THREAD_REQ_Q_NOT_EMPTY) {
        HAL_LOGD("RequestQueueThread processing SIGNAL_REQ_THREAD_REQ_Q_NOT_EMPTY");
		//app send req faster than oem process req
		if(!GetReqProcessStatus()){
	        curReq = PopReqQ();
	        if (!curReq) {
	            HAL_LOGD("No more service requests left in the queue!");
	        }
	        else
			{
			    HAL_LOGD("bef curreq=0x%x", (uint32_t)curReq);
			    Camera2GetSrvReqInfo(&m_staticReqInfo, curReq);
	            HAL_LOGD("aft");
	        }
		}
    }

    if (currentSignal & SIGNAL_REQ_THREAD_REQ_DONE) {
		bool IsProcessing = false;
        IsProcessing = GetReqProcessStatus();
		if (!IsProcessing) {
           HAL_LOGE("ERR status should processing");
		}
		ret = Camera2RefreshSrvReq(&m_staticReqInfo,m_halRefreshReq);
		if (ret) {
           HAL_LOGE("ERR refresh req");
		}
		HAL_LOGD("processing SIGNAL_REQ_THREAD_REQ_DONE,free add=0x%x",
				(uint32_t)m_staticReqInfo.ori_req);
        ret = m_requestQueueOps->free_request(m_requestQueueOps, m_staticReqInfo.ori_req);
        if (ret < 0)
            HAL_LOGE("ERR: free_request ret = %d", ret);
        {
			Mutex::Autolock lock(m_requestMutex);
	        m_staticReqInfo.ori_req = NULL;
	        m_reqIsProcess = false;
        }
		numEntries = get_camera_metadata_entry_count(m_halRefreshReq);//count of m_tmpReq changed impossibly
		if(numEntries <= 0)//for test
			numEntries = 10;//for test
		frameSize = get_camera_metadata_data_count(m_halRefreshReq);
		if(frameSize <= 0)//for test
			frameSize = 100;
        //for currentFrame alloc buf(metadata struct)
        ret = m_frameQueueOps->dequeue_frame(m_frameQueueOps, numEntries, frameSize, &newReq);//numEntries mean parameters num
        if (ret < 0)
            HAL_LOGE("ERR dequeue_frame ret = %d", ret);

        if (newReq==NULL) {
            HAL_LOGV("frame dequeue returned NULL" );
        } else {
            HAL_LOGV("frame dequeue done. numEntries(%d) frameSize(%d)",numEntries, frameSize);
        }
        ret = append_camera_metadata(newReq, m_halRefreshReq);
        if (ret == 0) {
            HAL_LOGD("frame metadata append success add=0x%x",(uint32_t)newReq);
            m_frameQueueOps->enqueue_frame(m_frameQueueOps, newReq);
        } else {
            HAL_LOGE("ERR frame metadata append fail (%d)",ret);
        }
		if (GetReqQueueSize() > 0)
			selfThread->SetSignal(SIGNAL_REQ_THREAD_REQ_Q_NOT_EMPTY);
    }

	if (currentSignal & SIGNAL_REQ_THREAD_PRECAPTURE_METERING_DONE) {
		Mutex::Autolock lock(m_afTrigLock);
        m_notifyCb(CAMERA2_MSG_AUTOEXPOSURE,
                        ANDROID_CONTROL_AE_STATE_CONVERGED,
                        m_camCtlInfo.precaptureTrigID, 0, m_callbackClient);
        m_notifyCb(CAMERA2_MSG_AUTOWB,
                        ANDROID_CONTROL_AWB_STATE_CONVERGED,
                        m_camCtlInfo.precaptureTrigID, 0, m_callbackClient);
		m_camCtlInfo.precaptureTrigID = 0;
		HAL_LOGD("precap meter done");
	}
    HAL_LOGV("Exit");
    return;
}

SprdCameraHWInterface2::RequestQueueThread::~RequestQueueThread()
{
	HAL_LOGV("dont' handle");
}

static camera2_device_t *g_cam2_device = NULL;
static bool g_camera_vaild = false;
static Mutex g_camera_mutex;

static int HAL2_camera_device_close(struct hw_device_t* device)
{
    Mutex::Autolock lock(g_camera_mutex);
    HAL_LOGD("start");
    if (device) {
        camera2_device_t *cam_device = (camera2_device_t *)device;
        HAL_LOGV("cam_device(0x%08x):", (unsigned int)cam_device);
        HAL_LOGV("g_cam2_device(0x%08x):", (unsigned int)g_cam2_device);
        delete static_cast<SprdCameraHWInterface2 *>(cam_device->priv);
        free(cam_device);
        g_camera_vaild = false;
        g_cam2_device = NULL;
    }
    HAL_LOGD("end");
    return 0;
}

static inline SprdCameraHWInterface2 *obj(const struct camera2_device *dev)
{
    return reinterpret_cast<SprdCameraHWInterface2 *>(dev->priv);
}

static int HAL2_device_set_request_queue_src_ops(const struct camera2_device *dev,
            const camera2_request_queue_src_ops_t *request_src_ops)
{
    HAL_LOGV("start");
    return obj(dev)->setRequestQueueSrcOps(request_src_ops);
}

static int HAL2_device_notify_request_queue_not_empty(const struct camera2_device *dev)
{
    HAL_LOGV("start");
    return obj(dev)->notifyRequestQueueNotEmpty();
}

static int HAL2_device_set_frame_queue_dst_ops(const struct camera2_device *dev,
            const camera2_frame_queue_dst_ops_t *frame_dst_ops)
{
    HAL_LOGV("start");
    return obj(dev)->setFrameQueueDstOps(frame_dst_ops);
}

static int HAL2_device_get_in_progress_count(const struct camera2_device *dev)
{
    HAL_LOGV("start");
    return obj(dev)->getInProgressCount();
}

static int HAL2_device_flush_captures_in_progress(const struct camera2_device *dev)
{
    HAL_LOGV("start");
    return obj(dev)->flushCapturesInProgress();
}

static int HAL2_device_construct_default_request(const struct camera2_device *dev,
            int request_template, camera_metadata_t **request)
{
    HAL_LOGV("start");
    return obj(dev)->ConstructDefaultRequest(request_template, request);
}

static int HAL2_device_allocate_stream(
            const struct camera2_device *dev,
            // inputs
            uint32_t width,
            uint32_t height,
            int      format,
            const camera2_stream_ops_t *stream_ops,
            // outputs
            uint32_t *stream_id,
            uint32_t *format_actual,
            uint32_t *usage,
            uint32_t *max_buffers)
{
    HAL_LOGV("start ");
    return obj(dev)->allocateStream(width, height, format, stream_ops,
                                    stream_id, format_actual, usage, max_buffers);
}

static int HAL2_device_register_stream_buffers(const struct camera2_device *dev,
            uint32_t stream_id,
            int num_buffers,
            buffer_handle_t *buffers)
{
    HAL_LOGV("start");
    return obj(dev)->registerStreamBuffers(stream_id, num_buffers, buffers);
}

static int HAL2_device_release_stream(
        const struct camera2_device *dev,
            uint32_t stream_id)
{
    HAL_LOGV("start id: %d", stream_id);
    if (!g_camera_vaild)
        return 0;
    return obj(dev)->releaseStream(stream_id);
}

static int HAL2_device_allocate_reprocess_stream(
           const struct camera2_device *dev,
            uint32_t width,
            uint32_t height,
            uint32_t format,
            const camera2_stream_in_ops_t *reprocess_stream_ops,
            // outputs
            uint32_t *stream_id,
            uint32_t *consumer_usage,
            uint32_t *max_buffers)
{
    HAL_LOGV("start");
    return obj(dev)->allocateReprocessStream(width, height, format, reprocess_stream_ops,
                                    stream_id, consumer_usage, max_buffers);
}

static int HAL2_device_allocate_reprocess_stream_from_stream(
           const struct camera2_device *dev,
            uint32_t output_stream_id,
            const camera2_stream_in_ops_t *reprocess_stream_ops,
            // outputs
            uint32_t *stream_id)
{
    HAL_LOGV("start");
    return obj(dev)->allocateReprocessStreamFromStream(output_stream_id,
                                    reprocess_stream_ops, stream_id);
}

static int HAL2_device_release_reprocess_stream(
        const struct camera2_device *dev,
            uint32_t stream_id)
{
	HAL_LOGV("start");
	return obj(dev)->releaseReprocessStream(stream_id);
}

static int HAL2_device_trigger_action(const struct camera2_device *dev,
           uint32_t trigger_id,
            int ext1,
            int ext2)
{
    HAL_LOGV("start");
    if (!g_camera_vaild)
        return 0;
    return obj(dev)->triggerAction(trigger_id, ext1, ext2);
}

static int HAL2_device_set_notify_callback(const struct camera2_device *dev,
            camera2_notify_callback notify_cb,
            void *user)
{
    HAL_LOGV("start");
    return obj(dev)->setNotifyCallback(notify_cb, user);
}

static int HAL2_device_get_metadata_vendor_tag_ops(const struct camera2_device*dev,
            vendor_tag_query_ops_t **ops)
{
    HAL_LOGV("start");
    return obj(dev)->getMetadataVendorTagOps(ops);
}

static int HAL2_device_dump(const struct camera2_device *dev, int fd)
{
    HAL_LOGV("start");
    return obj(dev)->dump(fd);
}

static int HAL2_getNumberOfCameras()
{
    HAL_LOGV("start");
#ifndef CONFIG_DCAM_SENSOR_NO_FRONT_SUPPORT
    return 2;
#else
	return 1;
#endif
}

static status_t ConstructStaticInfo(SprdCamera2Info *camerahal, camera_metadata_t **info,
        int cameraId, bool sizeRequest) {

    size_t entryCount = 0;
    size_t dataCount = 0;
    status_t ret;

    HAL_LOGD("info_add=0x%x,sizereq=%d",
                (int)*info, (int)sizeRequest);
#define ADD_OR_SIZE( tag, data, count ) \
    if ( ( ret = addOrSize(*info, sizeRequest, &entryCount, &dataCount, \
            tag, data, count) ) != OK ) return ret

    // android.info

    int32_t hardwareLevel = ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_FULL;//ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_LIMITED
    ADD_OR_SIZE(ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL,
            &hardwareLevel, 1);

    // android.lens

    ADD_OR_SIZE(ANDROID_LENS_INFO_MINIMUM_FOCUS_DISTANCE,
            &(camerahal->minFocusDistance), 1);
    ADD_OR_SIZE(ANDROID_LENS_INFO_HYPERFOCAL_DISTANCE,
            &(camerahal->minFocusDistance), 1);

    ADD_OR_SIZE(ANDROID_LENS_INFO_AVAILABLE_FOCAL_LENGTHS,
            &camerahal->focalLength, 1);
    ADD_OR_SIZE(ANDROID_LENS_INFO_AVAILABLE_APERTURES,
            &camerahal->aperture, 1);

    static const float filterDensity = 0;
    ADD_OR_SIZE(ANDROID_LENS_INFO_AVAILABLE_FILTER_DENSITIES,
            &filterDensity, 1);
    static const uint8_t availableOpticalStabilization =
            ANDROID_LENS_OPTICAL_STABILIZATION_MODE_OFF;
    ADD_OR_SIZE(ANDROID_LENS_INFO_AVAILABLE_OPTICAL_STABILIZATION,
            &availableOpticalStabilization, 1);

    static const int32_t lensShadingMapSize[] = {1, 1};
    ADD_OR_SIZE(ANDROID_LENS_INFO_SHADING_MAP_SIZE, lensShadingMapSize,
            sizeof(lensShadingMapSize)/sizeof(int32_t));

/*    static const float lensShadingMap[3 * 1 * 1 ] =
            { 1.f, 1.f, 1.f };
    ADD_OR_SIZE(ANDROID_LENS_INFO_SHADING_MAP, lensShadingMap,
            sizeof(lensShadingMap)/sizeof(float));*///diff with android4.3 and android4.4

    int32_t lensFacing = cameraId ?
            ANDROID_LENS_FACING_FRONT : ANDROID_LENS_FACING_BACK;
    ADD_OR_SIZE(ANDROID_LENS_FACING, &lensFacing, 1);

    // android.sensor
    ADD_OR_SIZE(ANDROID_SENSOR_INFO_EXPOSURE_TIME_RANGE,
            kExposureTimeRange, 2);

    ADD_OR_SIZE(ANDROID_SENSOR_INFO_MAX_FRAME_DURATION,
            &kFrameDurationRange[1], 1);

   /* ADD_OR_SIZE(ANDROID_SENSOR_INFO_SENSITIVITY_RANGE,
            Sensor::kSensitivityRange,
            sizeof(Sensor::kSensitivityRange)
            /sizeof(int32_t));*///diff with android4.3 and android4.4


    static const uint8_t kColorFilterArrangement = ANDROID_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT_RGGB;
    ADD_OR_SIZE(ANDROID_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT,
            &kColorFilterArrangement, 1);

    // Empirically derived to get correct FOV measurements
    static const float sensorPhysicalSize[2] = {3.50f, 2.625f};
    ADD_OR_SIZE(ANDROID_SENSOR_INFO_PHYSICAL_SIZE,
            sensorPhysicalSize, 2);

    int32_t pixelArraySize[2] = {
        camerahal->sensorW, camerahal->sensorH
    };
    ADD_OR_SIZE(ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE, pixelArraySize, 2);
    ADD_OR_SIZE(ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE, pixelArraySize,2);
    static const uint32_t kMaxRawValue = 4000;
    static const uint32_t kBlackLevel  = 1000;
    ADD_OR_SIZE(ANDROID_SENSOR_INFO_WHITE_LEVEL,
            &kMaxRawValue, 1);

    static const int32_t blackLevelPattern[4] = {
            kBlackLevel, kBlackLevel,
            kBlackLevel, kBlackLevel
    };
    ADD_OR_SIZE(ANDROID_SENSOR_BLACK_LEVEL_PATTERN,
            blackLevelPattern, sizeof(blackLevelPattern)/sizeof(int32_t));

    //TODO: sensor color calibration fields

    // android.flash
    uint8_t flashAvailable;
    if (cameraId == 0)
        flashAvailable = 1;
    else
        flashAvailable = 0;
    ADD_OR_SIZE(ANDROID_FLASH_INFO_AVAILABLE, &flashAvailable, 1);

    static const int64_t flashChargeDuration = 0;
    ADD_OR_SIZE(ANDROID_FLASH_INFO_CHARGE_DURATION, &flashChargeDuration, 1);

    // android.tonemap

    static const int32_t tonemapCurvePoints = 128;
    ADD_OR_SIZE(ANDROID_TONEMAP_MAX_CURVE_POINTS, &tonemapCurvePoints, 1);

    // android.scaler

    ADD_OR_SIZE(ANDROID_SCALER_AVAILABLE_FORMATS,
            kAvailableFormats,
            sizeof(kAvailableFormats)/sizeof(uint32_t));

    int32_t availableRawSizes[2] = {
        camerahal->sensorRawW, camerahal->sensorRawH
    };
    ADD_OR_SIZE(ANDROID_SCALER_AVAILABLE_RAW_SIZES,
            availableRawSizes, 2);

    ADD_OR_SIZE(ANDROID_SCALER_AVAILABLE_RAW_MIN_DURATIONS,
            kAvailableRawMinDurations,
            sizeof(kAvailableRawMinDurations)/sizeof(uint64_t));

    ADD_OR_SIZE(ANDROID_SCALER_AVAILABLE_PROCESSED_SIZES,
        camerahal->PreviewResolutions,
        (camerahal->numPreviewResolution)*2);

    ADD_OR_SIZE(ANDROID_SCALER_AVAILABLE_JPEG_SIZES,
        camerahal->jpegResolutions,
        (camerahal->numJpegResolution)*2);

    ADD_OR_SIZE(ANDROID_SCALER_AVAILABLE_PROCESSED_MIN_DURATIONS,
            kAvailableProcessedMinDurations,
            sizeof(kAvailableProcessedMinDurations)/sizeof(uint64_t));

    ADD_OR_SIZE(ANDROID_SCALER_AVAILABLE_JPEG_MIN_DURATIONS,
            kAvailableJpegMinDurations,
            sizeof(kAvailableJpegMinDurations)/sizeof(uint64_t));

    static const float maxZoom = CAMERA_DIGITAL_ZOOM_MAX;
    ADD_OR_SIZE(ANDROID_SCALER_AVAILABLE_MAX_DIGITAL_ZOOM, &maxZoom, 1);

    // android.jpeg
    static const int32_t jpegThumbnailSizes[] = {
            JPEG_THUMBNAIL_WIDTH, JPEG_THUMBNAIL_HEIGHT,
              0, 0
    };

    ADD_OR_SIZE(ANDROID_JPEG_AVAILABLE_THUMBNAIL_SIZES,
            jpegThumbnailSizes, sizeof(jpegThumbnailSizes)/sizeof(int32_t));

    static const int32_t jpegMaxSize = JPEG_MAX_SIZE;
    ADD_OR_SIZE(ANDROID_JPEG_MAX_SIZE, &jpegMaxSize, 1);

    // android.stats

    static const uint8_t availableFaceDetectModes[] = {
            ANDROID_STATISTICS_FACE_DETECT_MODE_OFF,
            ANDROID_STATISTICS_FACE_DETECT_MODE_FULL
    };
    ADD_OR_SIZE(ANDROID_STATISTICS_INFO_AVAILABLE_FACE_DETECT_MODES,
            availableFaceDetectModes,
            sizeof(availableFaceDetectModes));

    camerahal->maxFaceCount = CAMERA2_MAX_FACES;
    ADD_OR_SIZE(ANDROID_STATISTICS_INFO_MAX_FACE_COUNT,
            &(camerahal->maxFaceCount), 1);

    static const int32_t histogramSize = 64;
    ADD_OR_SIZE(ANDROID_STATISTICS_INFO_HISTOGRAM_BUCKET_COUNT,
            &histogramSize, 1);

    static const int32_t maxHistogramCount = 1000;
    ADD_OR_SIZE(ANDROID_STATISTICS_INFO_MAX_HISTOGRAM_COUNT,
            &maxHistogramCount, 1);

    static const int32_t sharpnessMapSize[2] = {64, 64};
    ADD_OR_SIZE(ANDROID_STATISTICS_INFO_SHARPNESS_MAP_SIZE,
            sharpnessMapSize, sizeof(sharpnessMapSize)/sizeof(int32_t));

    static const int32_t maxSharpnessMapValue = 1000;
    ADD_OR_SIZE(ANDROID_STATISTICS_INFO_MAX_SHARPNESS_MAP_VALUE,
            &maxSharpnessMapValue, 1);

    // android.control
    ADD_OR_SIZE(ANDROID_CONTROL_AVAILABLE_SCENE_MODES,
            availableSceneModes, sizeof(availableSceneModes));

    static const uint8_t availableEffects[] = {
            ANDROID_CONTROL_EFFECT_MODE_OFF
    };
    ADD_OR_SIZE(ANDROID_CONTROL_AVAILABLE_EFFECTS,
            availableEffects, sizeof(availableEffects));

    int32_t max3aRegions = 1;
    ADD_OR_SIZE(ANDROID_CONTROL_MAX_REGIONS,
            &max3aRegions, 1);

    ADD_OR_SIZE(ANDROID_CONTROL_AE_AVAILABLE_MODES,
            camerahal->availableAeModes, camerahal->numAvailableAeModes);

    static const camera_metadata_rational exposureCompensationStep = {
            1, 1
    };
    ADD_OR_SIZE(ANDROID_CONTROL_AE_COMPENSATION_STEP,
            &exposureCompensationStep, 1);

    int32_t exposureCompensationRange[] = {-3, 3};
    ADD_OR_SIZE(ANDROID_CONTROL_AE_COMPENSATION_RANGE,
            exposureCompensationRange,
            sizeof(exposureCompensationRange)/sizeof(int32_t));

    static const int32_t availableTargetFpsRanges[] = {
            4, 15, 4, 30, 5, 30
    };
    ADD_OR_SIZE(ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES,
            availableTargetFpsRanges,
            sizeof(availableTargetFpsRanges)/sizeof(int32_t));

    static const uint8_t availableAntibandingModes[] = {
            ANDROID_CONTROL_AE_ANTIBANDING_MODE_OFF,
            ANDROID_CONTROL_AE_ANTIBANDING_MODE_AUTO
    };
    ADD_OR_SIZE(ANDROID_CONTROL_AE_AVAILABLE_ANTIBANDING_MODES,
            availableAntibandingModes, sizeof(availableAntibandingModes));

    static const uint8_t availableAwbModes[] = {
            ANDROID_CONTROL_AWB_MODE_OFF,
            ANDROID_CONTROL_AWB_MODE_AUTO,
            ANDROID_CONTROL_AWB_MODE_INCANDESCENT,
            ANDROID_CONTROL_AWB_MODE_FLUORESCENT,
            ANDROID_CONTROL_AWB_MODE_DAYLIGHT,
            ANDROID_CONTROL_AWB_MODE_CLOUDY_DAYLIGHT
    };
    ADD_OR_SIZE(ANDROID_CONTROL_AWB_AVAILABLE_MODES,
            availableAwbModes, sizeof(availableAwbModes));

    ADD_OR_SIZE(ANDROID_CONTROL_AF_AVAILABLE_MODES,
                camerahal->availableAfModes, camerahal->numAvailableAfModes);

    static const uint8_t availableVstabModes[] = {
            ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_OFF,
            ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_ON
    };
    ADD_OR_SIZE(ANDROID_CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES,
            availableVstabModes, sizeof(availableVstabModes));

//    ADD_OR_SIZE(ANDROID_CONTROL_SCENE_MODE_OVERRIDES,
 //           camerahal->sceneModeOverrides, camerahal->numSceneModeOverrides);

    static const uint8_t quirkTriggerAuto = 1;
    ADD_OR_SIZE(ANDROID_QUIRKS_TRIGGER_AF_WITH_AUTO,
            &quirkTriggerAuto, 1);

    static const uint8_t quirkUseZslFormat = 1;//1zsl without realization
    ADD_OR_SIZE(ANDROID_QUIRKS_USE_ZSL_FORMAT,
            &quirkUseZslFormat, 1);

    static const uint8_t quirkMeteringCropRegion = 0;
    ADD_OR_SIZE(ANDROID_QUIRKS_METERING_CROP_REGION,
            &quirkMeteringCropRegion, 1);

#undef ADD_OR_SIZE
    /** Allocate metadata if sizing */
    if (sizeRequest) {
        HAL_LOGD("Allocating %d entries, %d extra bytes for "
                "static camera info",
                entryCount, dataCount);
        *info = allocate_camera_metadata(entryCount, dataCount);
        if (*info == NULL) {
            HAL_LOGE("Unable to allocate camera static info"
                    "(%d entries, %d bytes extra data)",
                    entryCount, dataCount);
            return NO_MEMORY;
        }
    }
    return OK;
}

static int HAL2_getCameraInfo(int cameraId, struct camera_info *info)//malloc camera_meta for load default paramet
{
    HAL_LOGD("cameraID: %d", cameraId);
    static camera_metadata_t * mCameraInfo[2] = {NULL, NULL};
    status_t res;

    if (cameraId == 0) {
        info->facing = CAMERA_FACING_BACK;//facing means back cam or front cam
        info->orientation = 90;
        if (!g_Camera2[0]) {
            g_Camera2[0] = (SprdCamera2Info *)malloc(sizeof(SprdCamera2Info));
			if(!g_Camera2[0])
			   HAL_LOGE("no mem! backcam");
            initCamera2Info(cameraId);
        }
    } else if (cameraId == 1) {
        info->facing = CAMERA_FACING_FRONT;
		info->orientation = 270;
        if (!g_Camera2[1]) {
            g_Camera2[1] = (SprdCamera2Info *)malloc(sizeof(SprdCamera2Info));
			if (!g_Camera2[1])
			   HAL_LOGE("no mem! frontcam");

			initCamera2Info(cameraId);
        }
    } else {
        return BAD_VALUE;
    }

    info->device_version = HARDWARE_DEVICE_API_VERSION(2, 0);
    if (mCameraInfo[cameraId] == NULL) {
        res = ConstructStaticInfo(g_Camera2[cameraId],&(mCameraInfo[cameraId]), cameraId, true);
        if (res != OK) {
            HAL_LOGE("Unable to allocate static info: %s (%d)",
                    strerror(-res), res);
            return res;
        }
        res = ConstructStaticInfo(g_Camera2[cameraId], &(mCameraInfo[cameraId]), cameraId, false);
        if (res != OK) {
            HAL_LOGE("Unable to fill in static info: %s (%d)",
                    strerror(-res), res);
            return res;
        }
    }
    info->static_camera_characteristics = mCameraInfo[cameraId];
    return NO_ERROR;
}

#define SET_METHOD(m) m : HAL2_device_##m

static camera2_device_ops_t camera2_device_ops = {
        SET_METHOD(set_request_queue_src_ops),
        SET_METHOD(notify_request_queue_not_empty),
        SET_METHOD(set_frame_queue_dst_ops),
        SET_METHOD(get_in_progress_count),
        SET_METHOD(flush_captures_in_progress),
        SET_METHOD(construct_default_request),
        SET_METHOD(allocate_stream),
        SET_METHOD(register_stream_buffers),
        SET_METHOD(release_stream),
        SET_METHOD(allocate_reprocess_stream),
        SET_METHOD(allocate_reprocess_stream_from_stream),
        SET_METHOD(release_reprocess_stream),
        SET_METHOD(trigger_action),
        SET_METHOD(set_notify_callback),
        SET_METHOD(get_metadata_vendor_tag_ops),
        SET_METHOD(dump),
        NULL
};

#undef SET_METHOD


static int HAL2_camera_device_open(const struct hw_module_t* module,
                                  const char *id,
                                  struct hw_device_t** device)
{
    int cameraId = atoi(id);
    int openInvalid = 0;

    Mutex::Autolock lock(g_camera_mutex);
    if (g_camera_vaild) {
        HAL_LOGE("ERR Can't open, other camera is in use");
        return -EBUSY;
    }
    g_camera_vaild = false;
    HAL_LOGD("\n\n>>> SPRD CameraHAL_2(ID:%d) <<<\n\n", cameraId);
    if (cameraId < 0 || cameraId >= HAL2_getNumberOfCameras()) {
        HAL_LOGE("ERR Invalid camera ID %s", id);
        return -EINVAL;
    }

    HAL_LOGD("g_cam2_device : 0x%08x", (unsigned int)g_cam2_device);
    if (g_cam2_device) {
        if (obj(g_cam2_device)->getCameraId() == cameraId) {
            HAL_LOGD("return existing camera ID %s", id);
            goto done;
        } else {
            HAL_LOGD("START waiting for cam device free");
            while (g_cam2_device)
                usleep(SIG_WAITING_TICK);
            HAL_LOGD("END   waiting for cam device free");
        }
    }

    g_cam2_device = (camera2_device_t *)malloc(sizeof(camera2_device_t));
    HAL_LOGV("g_cam2_device : 0x%08x", (unsigned int)g_cam2_device);

    if (!g_cam2_device)
        return -ENOMEM;

    g_cam2_device->common.tag     = HARDWARE_DEVICE_TAG;
    g_cam2_device->common.version = CAMERA_DEVICE_API_VERSION_2_0;
    g_cam2_device->common.module  = const_cast<hw_module_t *>(module);
    g_cam2_device->common.close   = HAL2_camera_device_close;

    g_cam2_device->ops = &camera2_device_ops;

    HAL_LOGD("open camera2 %s", id);

    g_cam2_device->priv = new SprdCameraHWInterface2(cameraId, g_cam2_device, g_Camera2[cameraId], &openInvalid);
    if (openInvalid) {
        HAL_LOGE("SprdCameraHWInterface2 creation failed");
        return -ENODEV;
    }
done:
    *device = (hw_device_t *)g_cam2_device;
    HAL_LOGV("opened camera2 %s (%p)", id, *device);
    g_camera_vaild = true;

    return 0;
}


static hw_module_methods_t camera_module_methods = {
            open : HAL2_camera_device_open
};

extern "C" {
    struct camera_module HAL_MODULE_INFO_SYM = {
      common : {
          tag                : HARDWARE_MODULE_TAG,
          module_api_version : CAMERA_MODULE_API_VERSION_2_0,
          hal_api_version    : HARDWARE_HAL_API_VERSION,
          id                 : CAMERA_HARDWARE_MODULE_ID,
          name               : "Sprd Camera HAL2",
          author             : "Spreadtrum Corporation",
          methods            : &camera_module_methods,
          dso:                NULL,
          reserved:           {0},
      },
      get_number_of_cameras : HAL2_getNumberOfCameras,
      get_camera_info       : HAL2_getCameraInfo,
      set_callbacks         : NULL,
      get_vendor_tag_ops    : NULL,
      reserved              :{NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL}
    };
}

} // namespace android

