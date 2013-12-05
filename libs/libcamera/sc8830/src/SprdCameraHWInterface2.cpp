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

namespace android {

#define SET_PARM(x,y) 	do { \
							ALOGD("%s: set camera param: %s, %d", __func__, #x, y); \
							camera_set_parm (x, y, NULL, NULL); \
						} while(0)

#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))

gralloc_module_t const* SprdCameraHWInterface2::m_grallocHal;
SprdCamera2Info * g_Camera2[2] = { NULL, NULL };

SprdCameraHWInterface2::SprdCameraHWInterface2(int cameraId, camera2_device_t *dev, SprdCamera2Info * camera, int *openInvalid):
            m_previewStream(NULL),
            m_requestQueueOps(NULL),
            m_frameQueueOps(NULL),
            m_callbackClient(NULL),
            m_halDevice(dev),
            m_reqIsProcess(false),
            m_CameraId(cameraId)
{
    ALOGD("(%s): reqindex=%d ENTER", __FUNCTION__, m_reqIsProcess);
    int ret = 0;
    int res = 0;

    memset(mPreviewHeapArray_phy, 0, sizeof(mPreviewHeapArray_phy));
	memset(mPreviewHeapArray_vir, 0, sizeof(mPreviewHeapArray_vir));
	memset(&m_staticReqInfo,0,sizeof(camera_req_info));

    if (!m_grallocHal) {
        ret = hw_get_module(GRALLOC_HARDWARE_MODULE_ID, (const hw_module_t **)&m_grallocHal);
        if (ret)
        {
            ALOGE("ERR(%s):Fail on loading gralloc HAL", __FUNCTION__);
			*openInvalid = 1;
			return;
        }
    }

    m_Camera2 = camera;
	ALOGD("(%s): camera init", __FUNCTION__);
	if (CAMERA_SUCCESS != camera_init(cameraId)) {
        ALOGE("(%s): camera init failed. exiting", __FUNCTION__);
        return;
	}
    m_RequestQueueThread = new RequestQueueThread(this);

	if(m_RequestQueueThread == NULL) {
        ALOGE("ERR(%s):create req thread No mem", __FUNCTION__);
		*openInvalid = 1;
		return;
	}
	m_halRefreshReq = allocate_camera_metadata(30,300);
	if(!m_halRefreshReq) {
        ALOGE("ERR(%s):alloc halreq No mem", __FUNCTION__);
		*openInvalid = 1;
		return;
	}
    for (int i = 0 ; i < STREAM_ID_LAST+1 ; i++)
         m_subStreams[i].type =  SUBSTREAM_TYPE_NONE;

	mCameraState.camera_state = SPRD_IDLE;
	mCameraState.preview_state = SPRD_INIT;

    m_RequestQueueThread->Start("RequestThread", PRIORITY_DEFAULT, 0);
    ALOGV("DEBUG(%s): created requestthread ", __FUNCTION__);

    ALOGD("(%s): EXIT", __FUNCTION__);
}

SprdCameraHWInterface2::~SprdCameraHWInterface2()
{
    ALOGD("(%s): ENTER", __FUNCTION__);
    this->release();
    ALOGD("(%s): EXIT", __FUNCTION__);
}

SprdCameraHWInterface2::PreviewStream::~PreviewStream()
{
  ALOGD("(%s): ENTER", __FUNCTION__);
}

void SprdCameraHWInterface2::RequestQueueThread::release()
{
    SetSignal(SIGNAL_THREAD_RELEASE);
}

bool SprdCameraHWInterface2::WaitForCameraStop()
{
	Mutex::Autolock stateLock(&mStateLock);

	if (SPRD_INTERNAL_STOPPING == mCameraState.camera_state)
	{
	    while(SPRD_INIT != mCameraState.camera_state
				&& SPRD_ERROR != mCameraState.camera_state) {
	       ALOGD("WaitForCameraStop: waiting for SPRD_IDLE");
	        mStateWait.wait(mStateLock);
	        ALOGD("WaitForCameraStop: woke up");
	    }
	}

	return SPRD_INIT == mCameraState.camera_state;
}

void SprdCameraHWInterface2::release()
{
    int i, res;
	Sprd_camera_state       camStatus = (Sprd_camera_state)0;
    ALOGD("(HAL2::release): ENTER");

	if (m_RequestQueueThread != NULL) {
        m_RequestQueueThread->release();
    }
	if (m_RequestQueueThread != NULL) {
        ALOGD("(HAL2::release): START Waiting for m_RequestQueueThread termination");
        while (!m_RequestQueueThread->IsTerminated())
            usleep(SIG_WAITING_TICK);
        ALOGD("(HAL2::release): END   Waiting for m_RequestQueueThread termination");
        m_RequestQueueThread = NULL;//sp without refence, free auto
    }

	camStatus = getPreviewState();

	if(camStatus != SPRD_IDLE) {
        ALOGE("(HAL2::release) preview sta=%d",camStatus);
		if(camStatus == SPRD_PREVIEW_IN_PROGRESS)
		{
	        res = releaseStream(STREAM_ID_PREVIEW);
			if(res != CAMERA_SUCCESS) {
				ALOGE("releaseStream error.");
			}
		}
	}

	camStatus = getCameraState();
	if(camStatus != SPRD_IDLE) {
        ALOGE("ERR stopping camera sta=%d",camStatus);
		return;
	}
    setCameraState(SPRD_INTERNAL_STOPPING, STATE_CAMERA);

	ALOGD("stopping camera.");
	if(CAMERA_SUCCESS != camera_stop(camera_cb, this)){
		setCameraState(SPRD_ERROR, STATE_CAMERA);
		ALOGE("release: fail to camera_stop().");
		return;
	}

	WaitForCameraStop();
	if(m_halRefreshReq)
	{
        free(m_halRefreshReq);
		m_halRefreshReq = NULL;
	}
	if(g_Camera2[m_CameraId])
	{
		free(g_Camera2[m_CameraId]);
		g_Camera2[m_CameraId] = NULL;
	}
    ALOGD("(HAL2::release): EXIT");
}
int SprdCameraHWInterface2::setRequestQueueSrcOps(const camera2_request_queue_src_ops_t *request_src_ops)
{
    ALOGV("DEBUG(%s):", __FUNCTION__);
    if ((NULL != request_src_ops) && (NULL != request_src_ops->dequeue_request)
            && (NULL != request_src_ops->free_request) && (NULL != request_src_ops->request_count)) {
        m_requestQueueOps = (camera2_request_queue_src_ops_t*)request_src_ops;
        return 0;
    }
    else {
        ALOGE("DEBUG(%s):setRequestQueueSrcOps : NULL arguments", __FUNCTION__);
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
        ALOGE("DEBUG(%s):queue ops NULL. ignoring request", __FUNCTION__);
        return 0;
    }
    reqNum = m_ReqQueue.size();

	if(m_reqIsProcess == true)
		reqNum++;
    if(reqNum >= MAX_REQUEST_NUM)
	{
		ALOGD("DEBUG(%s):queue is full(%d). ignoring request", __FUNCTION__, reqNum);
		return 0;// 1
	}
	else
	{
		m_requestQueueOps->dequeue_request(m_requestQueueOps, &curReq);//MetadataQueue construct, get preview metadata req
		if(!curReq)
	    {
            ALOGD("%s req is NULL",__FUNCTION__);
			return 0;
	    }
	    reqNum++;
	    ALOGD("notifyRequestQueueNotEmpty currep=0x%x reqnum=%d",(uint32_t)curReq,(uint32_t)reqNum);

        m_ReqQueue.push_back(curReq);
		while(curReq)
		{
		   m_requestQueueOps->dequeue_request(m_requestQueueOps, &curReq);
		   ALOGD("%s Srv req 0x%x",__FUNCTION__,(uint32_t)curReq);
		}
		ALOGD("notifyRequestQueueNotEmpty srv clear flag processing=%d",m_reqIsProcess);
		if(!m_reqIsProcess)//hal idle
		{
            m_RequestQueueThread->SetSignal(SIGNAL_REQ_THREAD_REQ_Q_NOT_EMPTY);
		}
		return 0;
	}
}

int SprdCameraHWInterface2::setFrameQueueDstOps(const camera2_frame_queue_dst_ops_t *frame_dst_ops)
{
    ALOGV("DEBUG(%s):", __FUNCTION__);
    if ((NULL != frame_dst_ops) && (NULL != frame_dst_ops->dequeue_frame)
            && (NULL != frame_dst_ops->cancel_frame) && (NULL !=frame_dst_ops->enqueue_frame)) {
        m_frameQueueOps = (camera2_frame_queue_dst_ops_t *)frame_dst_ops;
        return 0;
    }
    else {
        ALOGE("DEBUG(%s):setFrameQueueDstOps : NULL arguments", __FUNCTION__);
        return 1;
    }
}

int SprdCameraHWInterface2::getInProgressCount()
{
    return 0;
}

int SprdCameraHWInterface2::flushCapturesInProgress()
{
    return 0;
}

static int initCamera2Info(int cameraId)
{
    if(cameraId == 0) {
	    if (!g_Camera2[0]) {
            ALOGE("%s back not alloc!",__FUNCTION__);
			return 1;
		}

		g_Camera2[0]->sensorW             = 1632;
	    g_Camera2[0]->sensorH             = 1224;
	    g_Camera2[0]->sensorRawW          = 1632;
	    g_Camera2[0]->sensorRawH          = 1224;
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
	} else if(cameraId == 1) {
	    if (!g_Camera2[1]) {
            ALOGE("%s back not alloc!",__FUNCTION__);
			return 1;
		}

		g_Camera2[1]->sensorW             = 800;
	    g_Camera2[1]->sensorH             = 600;
	    g_Camera2[1]->sensorRawW          = 800;
	    g_Camera2[1]->sensorRawH          = 600;
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
        int type = get_camera_metadata_tag_type(tag);//data type occupy byte size
        if (type < 0 ) return BAD_VALUE;
        (*entryCount)++;
        (*dataCount) += calculate_camera_metadata_entry_data_size(type,
                entryDataCount);
		ALOGD("addorsize datasize=%d",*dataCount);
        return OK;
    }
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
        160, 120
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

    static const uint8_t sceneMode = ANDROID_CONTROL_SCENE_MODE_UNSUPPORTED;
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
        ALOGV("Allocating %d entries, %d extra bytes for "
                "request template type %d",
                entryCount, dataCount, request_template);
        *request = allocate_camera_metadata(entryCount, dataCount);
        if (*request == NULL) {
            ALOGE("Unable to allocate new request template type %d "
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
    ALOGD("DEBUG(%s): making template (%d) ", __FUNCTION__, request_template);

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
        ALOGE("Unable to populate new request for template %d",
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

status_t SprdCameraHWInterface2::PreviewStream::attachSubStream(int stream_id, int priority)
{
    ALOGV("(%s): substream_id(%d)", __FUNCTION__, stream_id);
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
        ALOGD("(%s): No idle index (%d)", __FUNCTION__, index);
        return NO_MEMORY;
    }
    m_attachedSubStreams[index].streamId = stream_id;
    m_attachedSubStreams[index].priority = priority;
    m_numRegisteredStream++;
    return NO_ERROR;
}

status_t SprdCameraHWInterface2::PreviewStream::detachSubStream(int stream_id)
{
    ALOGV("(%s): substream_id(%d)", __FUNCTION__, stream_id);
    int index;
    bool found = false;

    for (index = 0 ; index < NUM_MAX_SUBSTREAM ; index++) {
        if (m_attachedSubStreams[index].streamId == stream_id) {
            found = true;
            break;
        }
    }
    if (!found)
        return BAD_VALUE;
    m_attachedSubStreams[index].streamId = -1;
    m_attachedSubStreams[index].priority = 0;
    m_numRegisteredStream--;
    return NO_ERROR;
}

int SprdCameraHWInterface2::allocateStream(uint32_t width, uint32_t height, int format, const camera2_stream_ops_t *stream_ops,
                                    uint32_t *stream_id, uint32_t *format_actual, uint32_t *usage, uint32_t *max_buffers)
{
    ALOGD("(%s): stream width(%d) height(%d) format(%x)", __FUNCTION__,  width, height, format);

    stream_parameters_t newParameters;
    substream_parameters_t *subParameters;
    status_t res;

    if ((format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED || format == CAMERA2_HAL_PIXEL_FORMAT_OPAQUE
		|| format == HAL_PIXEL_FORMAT_YCrCb_420_SP)  &&
            isSupportedResolution(m_Camera2, width, height)) {
		if (NULL == m_previewStream) {
            *stream_id = STREAM_ID_PREVIEW;//temporate preview thread created on oem, so new streamthread object,but not run.

            m_previewStream  = new PreviewStream(this, *stream_id);
			if(m_previewStream == NULL)
			{
                ALOGD("(%s): new preview stream fail due to no mem", __FUNCTION__);
				return 1;
			}

            *format_actual                      = HAL_PIXEL_FORMAT_YCrCb_420_SP;
            *usage                              = GRALLOC_USAGE_SW_WRITE_OFTEN | GRALLOC_USAGE_CAMERA_BUFFER;
            *max_buffers                        = 6;

            m_previewStream->m_parameters.width                 = width;
            m_previewStream->m_parameters.height                = height;
            m_previewStream->m_parameters.format                = *format_actual;
            m_previewStream->m_parameters.streamOps             = stream_ops;
            m_previewStream->m_parameters.usage                 = *usage;
            m_previewStream->m_parameters.numSvcBufsInHal       = 0;
            m_previewStream->m_parameters.minUndequedBuffer     = 2;//v4l2 around buffers num
            m_previewStream->m_numRegisteredStream = 1;//parent stream total the num of stream
            ALOGD("(%s): m_numRegisteredStream preview format=0x%x", __FUNCTION__, *format_actual);

            return 0;
		} else {
			*stream_id = STREAM_ID_RECORD;
			ALOGD("DEBUG(%s): wjp stream_id (%d) stream_ops(0x%x)", __FUNCTION__, *stream_id,(uint32_t)stream_ops);
            subParameters = &m_subStreams[STREAM_ID_RECORD];
            memset(subParameters, 0, sizeof(substream_parameters_t));

            *format_actual = HAL_PIXEL_FORMAT_YCrCb_420_SP;
            *usage = GRALLOC_USAGE_SW_WRITE_OFTEN | GRALLOC_USAGE_CAMERA_BUFFER;
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
            ALOGD("wjp subParameters->streamOps 0x%x",(uint32_t)subParameters->streamOps);
            res = m_previewStream->attachSubStream(STREAM_ID_RECORD, 20);
            if (res != NO_ERROR) {
                ALOGE("(%s): substream attach failed. res(%d)", __FUNCTION__, res);
                return 1;
            }
            ALOGD("(%s): Enabling Record", __FUNCTION__);
            return 0;
		}
    }
	else if (format == HAL_PIXEL_FORMAT_YCrCb_420_SP || format == HAL_PIXEL_FORMAT_YV12) {
        *stream_id = STREAM_ID_PRVCB;
        subParameters = &m_subStreams[STREAM_ID_PRVCB];
        memset(subParameters, 0, sizeof(substream_parameters_t));

        if (!m_previewStream) {
            return 1;
        }

        *format_actual = format;
        *usage = GRALLOC_USAGE_SW_WRITE_OFTEN;
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

        res = m_previewStream->attachSubStream(STREAM_ID_PRVCB, 20);
        if (res != NO_ERROR) {
            ALOGE("(%s): substream attach failed. res(%d)", __FUNCTION__, res);
            return 1;
        }
        ALOGD("(%s): Enabling previewcb m_numRegisteredStream = %d", __FUNCTION__, m_previewStream->m_numRegisteredStream);

        return 0;
    }
    else if (format == HAL_PIXEL_FORMAT_BLOB //first create jpeg substream
            && isSupportedJpegResolution(m_Camera2, width, height)) {
        *stream_id = STREAM_ID_JPEG;

        subParameters = &m_subStreams[*stream_id];
        memset(subParameters, 0, sizeof(substream_parameters_t));
        *format_actual = HAL_PIXEL_FORMAT_RGBA_8888;//HAL_PIXEL_FORMAT_BLOB
        *usage = GRALLOC_USAGE_SW_WRITE_OFTEN;
        *max_buffers = 4;
        subParameters->type          = SUBSTREAM_TYPE_JPEG;
        subParameters->width         = width;
        subParameters->height        = height;
        subParameters->format        = *format_actual;
        subParameters->streamOps     = stream_ops;
        subParameters->usage         = *usage;
        subParameters->numOwnSvcBuffers = *max_buffers;
        subParameters->numSvcBufsInHal  = 0;
        subParameters->minUndequedBuffer = 2;
        ALOGD("(%s): substream jpeg", __FUNCTION__);
        return 0;
    }
    ALOGE("(%s): Unsupported Pixel Format", __FUNCTION__);
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
		ALOGV("WaitForPreviewStart: waiting for SPRD_PREVIEW_IN_PROGRESS");
		mStateWait.wait(mStateLock);
		ALOGV("WaitForPreviewStart: woke up");
	}

	return SPRD_PREVIEW_IN_PROGRESS == mCameraState.preview_state;
}

bool SprdCameraHWInterface2::WaitForPreviewStop()
{
	Mutex::Autolock statelock(&mStateLock);
    while (SPRD_IDLE != mCameraState.preview_state
			&& SPRD_ERROR != mCameraState.preview_state) {
		ALOGD("WaitForPreviewStop: waiting for SPRD_IDLE");
		mStateWait.wait(mStateLock);
		ALOGD("WaitForPreviewStop: woke up");
    }

	return SPRD_IDLE == mCameraState.preview_state;
}

void SprdCameraHWInterface2::setCameraState(Sprd_camera_state state, state_owner owner)
{

	ALOGV("setCameraState:state: %s, owner: %d", getCameraStateStr(state), owner);
	Mutex::Autolock stateLock(&mStateLock);

	switch (state) {
	//camera state
	case SPRD_INIT:
		mCameraState.camera_state = SPRD_INIT;
		mCameraState.preview_state = SPRD_IDLE;
		mCameraState.capture_state = SPRD_IDLE;
		mCameraState.focus_state = SPRD_IDLE;
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
		ALOGV("setCameraState: error");
		break;
	}

	ALOGV("setCameraState:camera state = %s, preview state = %s, capture state = %s focus state = %s",
				getCameraStateStr(mCameraState.camera_state),
				getCameraStateStr(mCameraState.preview_state),
				getCameraStateStr(mCameraState.capture_state),
				getCameraStateStr(mCameraState.focus_state));
}

void SprdCameraHWInterface2::HandleStopPreview(camera_cb_type cb,
										  int32_t parm4)
{
	ALOGD("HandleStopPreview in: cb = %d, parm4 = 0x%x, state = %s",
				cb, parm4, getCameraStateStr(getPreviewState()));

	transitionState(SPRD_INTERNAL_PREVIEW_STOPPING,
				SPRD_IDLE,
				STATE_PREVIEW);
	/*freePreviewMem();*/

	ALOGD("HandleStopPreview out, state = %s", getCameraStateStr(getPreviewState()));
}

void SprdCameraHWInterface2::HandleStopCamera(camera_cb_type cb, int32_t parm4)
{
	ALOGD("HandleStopCamera in: cb = %d, parm4 = 0x%x, state = %s",
				cb, parm4, getCameraStateStr(getCameraState()));

	transitionState(SPRD_INTERNAL_STOPPING, SPRD_INIT, STATE_CAMERA);

	ALOGD("HandleStopCamera out, state = %s", getCameraStateStr(getCameraState()));
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
#if 0
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
#endif
	case CAMERA_FUNC_START:
		obj->HandleStartCamera(cb, parm4);
	    break;

	case CAMERA_FUNC_STOP:
		obj->HandleStopCamera(cb, parm4);
		break;

    default:
        // transition to SPRD_ERROR ?
        ALOGE("Unknown camera-callback status %d", cb);
		break;
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

    ALOGD("(%s): stream_id(%d), num_buff(%d), handle(%x) ", __FUNCTION__,
        stream_id, num_buffers, (uint32_t)registeringBuffers);

    if (stream_id == STREAM_ID_PREVIEW && m_previewStream != NULL) {
        targetStreamParms = &(m_previewStream->m_parameters);
		camStatus = getCameraState();
	    if(camStatus != SPRD_IDLE)//not init
		{
	        ALOGD("ERR registerStreamBuffers Sta=%d",camStatus);
			return UNKNOWN_ERROR;
		}

		ALOGD("DEBUG(%s): format(%x) width(%d), height(%d)",
				__FUNCTION__, targetStreamParms->format, targetStreamParms->width,
				targetStreamParms->height);
	    targetStreamParms->numSvcBuffers = num_buffers;

	    //use graphic buffers, firstly cancel buffers dequeued on framework
	    for(i = 0; i < targetStreamParms->numSvcBuffers; i++)
		{
	         const private_handle_t *priv_handle = reinterpret_cast<const private_handle_t *>(registeringBuffers[i]);
	         mPreviewHeapArray_vir[i] = priv_handle->base;

			 MemoryHeapIon::Get_phy_addr_from_ion(priv_handle->share_fd,&phyaddr,&size);
			 ALOGD("meinv_wjp phyaddr is 0x%x",phyaddr);
			 mPreviewHeapArray_phy[i] = phyaddr;
			 ALOGD("DEBUG(%s): preview addr 0x%x",__FUNCTION__, priv_handle->base);

			 targetStreamParms->svcBufHandle[i] = registeringBuffers[i];//important
			 targetStreamParms->svcBufStatus[i] = ON_HAL_INIT;
			 ALOGD("registerStreamBuffers index=%d Preview phyadd=0x%x,srv_add=0x%x",
					i, (uint32_t)phyaddr,(uint32_t)targetStreamParms->svcBufHandle[i]);
		}

	    targetStreamParms->cancelBufNum = targetStreamParms->minUndequedBuffer;
	    ALOGD("DEBUG(%s): END registerStreamBuffers", __FUNCTION__);

	    return NO_ERROR;

    }
	else if (stream_id == STREAM_ID_PRVCB || stream_id == STREAM_ID_JPEG || stream_id == STREAM_ID_RECORD) {
        substream_parameters_t  *targetParms;
        targetParms = &m_subStreams[stream_id];
        targetParms->numSvcBuffers = num_buffers;
        for (uint32_t i = 0 ; i < targetParms->numSvcBuffers ; i++) {
            if (m_grallocHal) {
                const private_handle_t *priv_handle = reinterpret_cast<const private_handle_t *>(registeringBuffers[i]);
                ALOGD("(%s): registering substream(%d) Buffers[%d] (%x) ", __FUNCTION__,
                         i, stream_id, (uint32_t)(priv_handle->base));
                targetParms->subStreamGraphicFd[i] = priv_handle->fd;
				targetParms->subStreamAddVirt[i]   = (uint32_t)priv_handle->base;
                targetParms->svcBufHandle[i]       = registeringBuffers[i];
				targetParms->svcBufStatus[i] = ON_HAL_INIT;
				targetParms->numSvcBufsInHal++;
            }
        }
        return 0;
    }
    else {
        ALOGE("(%s): unregistered stream id (%d)", __FUNCTION__, stream_id);
        return 1;
    }

}

int SprdCameraHWInterface2::releaseStream(uint32_t stream_id)
{
    status_t res = NO_ERROR;
	int i = 0;
    stream_parameters_t     *targetStreamParms;
	Sprd_camera_state       camStatus = (Sprd_camera_state)0;

	ALOGD("(%s): stream_id(%d)", __FUNCTION__, stream_id);

    if (stream_id == STREAM_ID_PREVIEW) {
		camStatus = getPreviewState();

		if(camStatus != SPRD_PREVIEW_IN_PROGRESS)
		{
		   ALOGE("ERR releaseStream Preview status=%d",camStatus);
		   return UNKNOWN_ERROR;
		}
		m_previewStream->setRecevStopMsg(true);//sync receive preview frame
        setCameraState(SPRD_INTERNAL_PREVIEW_STOPPING, STATE_PREVIEW);
        if(CAMERA_SUCCESS != camera_stop_preview()) {
			setCameraState(SPRD_ERROR, STATE_PREVIEW);
			ALOGE("stopPreviewInternal X: fail to camera_stop_preview().");
			return UNKNOWN_ERROR;
	    }
	    WaitForPreviewStop();

		targetStreamParms = &(m_previewStream->m_parameters);
		for(;i < targetStreamParms->numSvcBuffers;i++)
		{
           res = targetStreamParms->streamOps->cancel_buffer(targetStreamParms->streamOps, &(targetStreamParms->svcBufHandle[i]));
		   if(res)
		   {
              ALOGE("releaseStream cancelbuf res=%d",res);
		   }
		}
		camera_set_preview_mem(0, 0, 0, 0);
        if(m_previewStream != NULL)
        {
			delete m_previewStream;
			m_previewStream = NULL;
        }
    }
	else if (stream_id == STREAM_ID_JPEG) {
        memset(&m_subStreams[stream_id], 0, sizeof(substream_parameters_t));
        ALOGD("(%s): jpg stream release!", __FUNCTION__);
        return 0;
    }
	else {
        ALOGE("ERR:(%s): wrong stream id (%d)", __FUNCTION__, stream_id);
        return 1;
    }

    ALOGD("(%s): END", __FUNCTION__);
    return CAMERA_SUCCESS;
}

int SprdCameraHWInterface2::allocateReprocessStream(
    uint32_t width, uint32_t height, uint32_t format,
    const camera2_stream_in_ops_t *reprocess_stream_ops,
    uint32_t *stream_id, uint32_t *consumer_usage, uint32_t *max_buffers)
{
    ALOGV("DEBUG(%s):", __FUNCTION__);
    return 0;
}

int SprdCameraHWInterface2::allocateReprocessStreamFromStream(
            uint32_t output_stream_id,
            const camera2_stream_in_ops_t *reprocess_stream_ops,
            // outputs
            uint32_t *stream_id)
{
    ALOGD("(%s): output_stream_id(%d)", __FUNCTION__, output_stream_id);
    return 0;
}

int SprdCameraHWInterface2::releaseReprocessStream(uint32_t stream_id)
{
    ALOGD("(%s): stream_id(%d)", __FUNCTION__, stream_id);
	return 0;
}

int SprdCameraHWInterface2::triggerAction(uint32_t trigger_id, int ext1, int ext2)
{
    ALOGD("DEBUG(%s): id(%x), %d, %d", __FUNCTION__, trigger_id, ext1, ext2);
    #if 0
    Mutex::Autolock lock(m_afModeTriggerLock);
    ALOGV("DEBUG(%s): id(%x), %d, %d", __FUNCTION__, trigger_id, ext1, ext2);

    switch (trigger_id) {
    case CAMERA2_TRIGGER_AUTOFOCUS:
        ALOGV("DEBUG(%s):TRIGGER_AUTOFOCUS id(%d)", __FUNCTION__, ext1);
        OnAfTrigger(ext1);
        break;

    case CAMERA2_TRIGGER_CANCEL_AUTOFOCUS:
        ALOGV("DEBUG(%s):CANCEL_AUTOFOCUS id(%d)", __FUNCTION__, ext1);
        OnAfCancel(ext1);
        break;
    case CAMERA2_TRIGGER_PRECAPTURE_METERING:
        ALOGV("DEBUG(%s):CAMERA2_TRIGGER_PRECAPTURE_METERING id(%d)", __FUNCTION__, ext1);
        OnPrecaptureMeteringTriggerStart(ext1);
        break;
    default:
        break;
    }
	#endif
    return 0;
}

int SprdCameraHWInterface2::setNotifyCallback(camera2_notify_callback notify_cb, void *user)
{
    ALOGV("DEBUG(%s): cb_addr(%x)", __FUNCTION__, (unsigned int)notify_cb);
    m_notifyCb = notify_cb;
    m_callbackClient = user;
    return 0;
}

int SprdCameraHWInterface2::getMetadataVendorTagOps(vendor_tag_query_ops_t **ops)
{
    ALOGV("DEBUG(%s):", __FUNCTION__);
    return 0;
}

int SprdCameraHWInterface2::dump(int fd)
{
    ALOGV("DEBUG(%s):", __FUNCTION__);
    return 0;
}

void  SprdCameraHWInterface2::PreviewStream::pushBufQ(int index)
{
    Mutex::Autolock lock(m_BufQLock);
    m_bufQueue.push_back(index);
}

void  SprdCameraHWInterface2::PreviewStream::setRecevStopMsg(bool IsRecevMsg)
{
    Mutex::Autolock lock(m_BufQLock);
	m_IsRecevStopMsg = IsRecevMsg;
}

bool  SprdCameraHWInterface2::PreviewStream::getRecevStopMsg(void)
{
    Mutex::Autolock lock(m_BufQLock);
	return m_IsRecevStopMsg;
}

int SprdCameraHWInterface2::PreviewStream::popBufQ()
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

void SprdCameraHWInterface2::PreviewStream::releaseBufQ()
{
    List<int>::iterator round;

    Mutex::Autolock lock(m_BufQLock);
    ALOGD("(%s)bufqueue.size : %d", __FUNCTION__, m_bufQueue.size());

    while(m_bufQueue.size() > 0){
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
	return m_reqIsProcess;//isprocessing
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
    ALOGD("(%s)m_ReqQueue.size : %d", __FUNCTION__, m_ReqQueue.size());

    while(m_ReqQueue.size() > 0){
        round  = m_ReqQueue.begin()++;
        m_ReqQueue.erase(round);
    }
    return;
}

status_t SprdCameraHWInterface2::Camera2RefreshSrvReq(camera_req_info *srcreq, camera_metadata_t *dstreq)
{
    status_t res = 0;
	Mutex::Autolock lock(m_requestMutex);
	//uint8_t  data = 0;
	if(!srcreq || !dstreq)
	{
        ALOGE("%s para is null",__FUNCTION__);
		return BAD_VALUE;
	}
	if (0 != add_camera_metadata_entry(dstreq, ANDROID_REQUEST_ID,
                &srcreq->requestID, 1))
        return NO_MEMORY;

    if (0 != add_camera_metadata_entry(dstreq, ANDROID_REQUEST_METADATA_MODE,
                &srcreq->metadataMode, 1))
        return NO_MEMORY;

    if (0 != add_camera_metadata_entry(dstreq, ANDROID_REQUEST_FRAME_COUNT,
                &srcreq->frmCnt, 1))
        return NO_MEMORY;
#if 0
    if (0 != add_camera_metadata_entry(dstreq, ANDROID_SENSOR_TIMESTAMP,
                &srcreq->sensorTimeStamp, 1))
        return NO_MEMORY;
	#endif
	//data = srcreq->sceneMode - 1;
	ALOGD("%s scene=%d",__FUNCTION__,srcreq->sceneMode);
	if (0 != add_camera_metadata_entry(dstreq, ANDROID_CONTROL_SCENE_MODE,
				&srcreq->sceneMode, 1))
		return NO_MEMORY;

	return res;
}

void SprdCameraHWInterface2::CameraConvertCropRegion(float **outPut, float sensorWidth, float sensorHeight, cropZoom *cropRegion)
{
	float    minOutputWidth,minOutputHeight;
	float    OutputWidth,OutputHeight;
	float    zoomRatio;
	float    outputRatio;
	float    minOutputRatio;
	float    realSensorRatio;
	float    zoomWidth,zoomHeight,zoomLeft,zoomTop;
	uint32_t i = 0;

	ALOGD("%s: crop %d %d %d %d.", __FUNCTION__ ,
		  cropRegion->crop_x, cropRegion->crop_y, cropRegion->crop_w ,cropRegion->crop_h);

	zoomWidth = (float)cropRegion->crop_w;
	zoomHeight = (float)cropRegion->crop_h;

	minOutputRatio = outPut[0][0]/outPut[0][1];
	minOutputWidth = outPut[0][0];
	minOutputHeight = outPut[0][1];
	for (i = 1 ; i < sizeof(outPut) / sizeof(outPut[0]) ; ++i) {
		outputRatio = outPut[i][0]/outPut[i][1];
		OutputWidth = outPut[i][0];
		OutputHeight = outPut[i][1];

		if (minOutputRatio > outputRatio) {
			minOutputRatio = outputRatio;
			minOutputWidth = OutputWidth;
			minOutputHeight = OutputHeight;
		}
	}
	realSensorRatio = sensorWidth/sensorHeight;
	if (minOutputRatio > 1632/1224) {
		zoomRatio = 1632/zoomWidth;
		zoomWidth = sensorWidth/zoomRatio;
		zoomHeight = zoomWidth *minOutputHeight / minOutputWidth;
	} else {
		zoomRatio = 1224/zoomHeight;
		zoomHeight = sensorHeight / zoomRatio;
        zoomWidth = zoomHeight *
                minOutputWidth / minOutputHeight;
	}

	zoomLeft = (sensorWidth - zoomWidth) / 2;
    zoomTop  = (sensorHeight - zoomHeight) / 2;

	cropRegion->crop_x = (uint32_t)zoomLeft;
	cropRegion->crop_y = (uint32_t)zoomTop;
	cropRegion->crop_w = (uint32_t)zoomWidth;
	cropRegion->crop_h = (uint32_t)zoomHeight;

    ALOGD("%s:Crop region calculated (x=%d,y=%d,w=%d,h=%d) for zoom=%f",__FUNCTION__ ,
        cropRegion->crop_x, cropRegion->crop_y, cropRegion->crop_w, cropRegion->crop_h,zoomRatio);
}

void SprdCameraHWInterface2::Camera2GetSrvReqInfo( camera_req_info *srcreq, camera_metadata_t *orireq)
{
    status_t res = 0;
    camera_metadata_entry_t entry;
	//camera_srv_req  *tmpreq = NULL;
    uint32_t reqCount = 0;
	camera_parm_type drvTag = (camera_parm_type)0;
    uint32_t index = 0;
	int32_t i = 0;
	Mutex::Autolock lock(m_requestMutex);

	ALOGD("DEBUG(%s): ", __FUNCTION__);
    if(!orireq || !srcreq)
	{
	    ALOGD("DEBUG(%s): Err para is NULL!", __FUNCTION__);
        return;
	}
	#define ASIGNIFNOTEQUAL(x, y, flag) if((x) != (y))\
		                            {\
			                            (x) = (y);\
			                            if((void *)flag != NULL)\
			                            {\
                                           if(mCameraState.preview_state == SPRD_INIT)\
                                           {\
										   	  SET_PARM(flag,x);\
                                           }\
										   else\
										   {\
                                              m_reqIsProcess = true;\
                                              SET_PARM(flag,x);\
                                              m_RequestQueueThread->SetSignal(SIGNAL_REQ_THREAD_REQ_DONE);\
										   }\
			                            }\
		                            }
	srcreq->ori_req = orireq;
    reqCount = (uint32_t)get_camera_metadata_entry_count(srcreq->ori_req);
	//first get metadata struct
    for (; index < reqCount ; index++) {

        if (get_camera_metadata_entry(srcreq->ori_req, index, &entry)==0) {
            switch (entry.tag) {
#if 0
            case ANDROID_SENSOR_TIMESTAMP:
                srcreq->sensorTimeStamp = entry.data.i64[0];
                break;
#endif
			case ANDROID_CONTROL_AF_MODE:
				break;
            case ANDROID_REQUEST_TYPE:
				ASIGNIFNOTEQUAL(srcreq->isReprocessing, entry.data.u8[0], (camera_parm_type)NULL)
				ALOGD("DEBUG(%s): ANDROID_REQUEST_TYPE (%d)",  __FUNCTION__, entry.data.u8[0]);
				break;
			#if 0
			case ANDROID_REQUEST_INPUT_STREAMS:
				break;
            #endif
			case ANDROID_CONTROL_MODE:
				ASIGNIFNOTEQUAL(srcreq->ctlMode, (ctl_mode)(entry.data.u8[0] + 1),(camera_parm_type)NULL)
				ALOGD("DEBUG(%s): ANDROID_CONTROL_MODE (%d)",  __FUNCTION__, (ctl_mode)(entry.data.u8[0] + 1));
				break;

			case ANDROID_CONTROL_CAPTURE_INTENT:
				ASIGNIFNOTEQUAL(srcreq->captureIntent, (capture_intent)entry.data.u8[0],(camera_parm_type)NULL)
				ALOGD("DEBUG(%s): ANDROID_CONTROL_CAPTURE_INTENT (%d)",  __FUNCTION__, (capture_intent)entry.data.u8[0]);
				break;

			case ANDROID_SCALER_CROP_REGION:
				ALOGD("wjp:test zoom.");
				{
				cropZoom zoom = {0,0,0,0};
                cropZoom zoom1 = {0,0,0,0};
				zoom1.crop_x = entry.data.i32[0] & ~0x03;
				zoom1.crop_y = entry.data.i32[1] & ~0x03;
				zoom1.crop_w = entry.data.i32[2] & ~0x03;
				zoom1.crop_h = entry.data.i32[3] & ~0x03;
				if(zoom1.crop_x != srcreq->cropRegion0 || zoom1.crop_y != srcreq->cropRegion1\
					  || zoom1.crop_w != srcreq->cropRegion2 || zoom1.crop_h != srcreq->cropRegion3)
				{
				    zoom.crop_x = srcreq->cropRegion0 = zoom1.crop_x;
					zoom.crop_y = srcreq->cropRegion1 = zoom1.crop_y;
					zoom.crop_w = srcreq->cropRegion2 = zoom1.crop_w;
					zoom.crop_h = srcreq->cropRegion3 = zoom1.crop_h;
					res = androidParametTagToDrvParaTag(ANDROID_SCALER_CROP_REGION, &drvTag);
					ALOGD("wjp:drvTag=%d.",drvTag);
					if(res)
					{
						ALOGE("ERR(%s): drv not support zoom",  __FUNCTION__);
					}
					if(mCameraState.preview_state == SPRD_INIT)
                    {
				   	  SET_PARM(drvTag,(uint32_t)&zoom);
                    }
				    else
				    {
                      m_reqIsProcess = true;
                      SET_PARM(drvTag,(uint32_t)&zoom);
                      m_RequestQueueThread->SetSignal(SIGNAL_REQ_THREAD_REQ_DONE);
				    }
				}
				ALOGD("DEBUG(%s): ANDROID_SCALER_CROP_REGION (%d %d %d %d)",  __FUNCTION__,zoom1.crop_x,zoom1.crop_y,zoom1.crop_w,zoom1.crop_h);
				}
				break;

            case ANDROID_REQUEST_ID:
				ASIGNIFNOTEQUAL(srcreq->requestID, entry.data.i32[0],(camera_parm_type)NULL)
                ALOGD("DEBUG(%s): ANDROID_REQUEST_ID (%d)",  __FUNCTION__, entry.data.i32[0]);
                break;

            case ANDROID_REQUEST_METADATA_MODE:
				ASIGNIFNOTEQUAL(srcreq->metadataMode, (metadata_mode)entry.data.u8[0],(camera_parm_type)NULL)
                ALOGD("DEBUG(%s): ANDROID_REQUEST_METADATA_MODE (%d)",  __FUNCTION__, (metadata_mode)entry.data.u8[0]);
                break;

            case ANDROID_REQUEST_OUTPUT_STREAMS:
			{
				uint8_t tmpMask = 0;
				for(i = 0; i < entry.count; i++)
				{
					tmpMask |= 1 << entry.data.u8[i];
				}
				ASIGNIFNOTEQUAL(srcreq->outputStreamMask,tmpMask,(camera_parm_type)NULL)
				ALOGD("DEBUG(%s): ANDROID_REQUEST_OUTPUT_STREAMS (%d)",  __FUNCTION__, tmpMask);
            }
                break;

            case ANDROID_REQUEST_FRAME_COUNT:
				ASIGNIFNOTEQUAL(srcreq->frmCnt,entry.data.i32[0],(camera_parm_type)NULL)
                ALOGD("DEBUG(%s): ANDROID_REQUEST_FRAME_COUNT (%d)",  __FUNCTION__,entry.data.i32[0]);
                break;

            case ANDROID_CONTROL_SCENE_MODE:
			{
			{
				int8_t             sceneMode;
				res = androidSceneModeToDrvMode((camera_metadata_enum_android_control_scene_mode_t)entry.data.u8[0], &sceneMode);

				ALOGD("DEBUG(%s): drv mode (%d) ret=%d",  __FUNCTION__,sceneMode,res);
				if(res)
				{
					ALOGE("ERR(%s): Scene not support",  __FUNCTION__);
				}
				res = androidParametTagToDrvParaTag(ANDROID_CONTROL_SCENE_MODE, &drvTag);
				if(res)
				{
					ALOGE("ERR(%s): drv not support scene mode",  __FUNCTION__);
				}
				ASIGNIFNOTEQUAL(srcreq->sceneMode, sceneMode,drvTag)
			}
                break;
            }
            default:
                //ALOGD("DEBUG(%s):Bad Metadata tag (%d)",  __FUNCTION__, entry.tag);
                break;
            }
        }
    }
    //stream process later
    if((srcreq->outputStreamMask & STREAM_MASK_PREVIEW || srcreq->outputStreamMask & STREAM_ID_PRVCB) && \
		    (srcreq->captureIntent == CAPTURE_INTENT_VIDEO_RECORD || srcreq->captureIntent == CAPTURE_INTENT_PREVIEW))//diff req
	{
	    int ret = 0;
	    stream_parameters_t     *targetStreamParms = &(m_previewStream->m_parameters);

        if((mCameraState.preview_state == SPRD_INIT) || (SPRD_IDLE == mCameraState.preview_state))//start preview
        {
			m_staticReqInfo.outputStreamMask = srcreq->outputStreamMask;
            //hal parameters set
			SET_PARM(CAMERA_PARM_PREVIEW_MODE, CAMERA_PREVIEW_MODE_SNAPSHOT);
			m_reqIsProcess = true;
		    //if(camera_set_preview_dimensions(targetStreamParms->width,targetStreamParms->height,NULL,NULL) != 0)
			if(camera_set_dimensions(640,480,targetStreamParms->width,targetStreamParms->height,NULL,NULL,0) != 0) {
		       ALOGE("setCameraPreviewDimensions failed");
			   return ;
			}
		    camerea_set_preview_format(targetStreamParms->format);
			if (camera_set_preview_mem((uint32_t)mPreviewHeapArray_phy,
						(uint32_t)mPreviewHeapArray_vir,
						(targetStreamParms->width * targetStreamParms->height * 3)/2,
						(uint32_t)targetStreamParms->numSvcBuffers))

				return ;

		    setCameraState(SPRD_INTERNAL_PREVIEW_REQUESTED, STATE_PREVIEW); 
		    camera_ret_code_type qret = camera_start_preview(camera_cb, this,CAMERA_NORMAL_MODE);//mode
			ALOGV("camera_start_preview X");
			if (qret != CAMERA_SUCCESS) {
				ALOGE("startPreview failed: sensor error.");
				setCameraState(SPRD_ERROR, STATE_PREVIEW);
				return ;
			}

			ret = WaitForPreviewStart();
			ALOGD("camera_start_preview X ret=%d",ret);
		}
	}
}

int SprdCameraHWInterface2::recordingFrame(PreviewStream *stream, int32_t *srcBufVirt, int64_t frameTimeStamp)
{
	stream_parameters_t     *StreamParms = &(stream->m_parameters);
    substream_parameters_t  *subParms    = &m_subStreams[STREAM_ID_RECORD];
	void *VirtBuf = NULL;
    int  ret = 0;
    buffer_handle_t * buf = NULL;
	const private_handle_t *priv_handle = NULL;

	if ((NULL == subParms->streamOps) || (NULL == subParms->streamOps->dequeue_buffer)) {
		ALOGE("ERR(%s): haven't stream ops", __FUNCTION__);
		return 0;
	}

	ret = subParms->streamOps->dequeue_buffer(subParms->streamOps, &buf);
    if (ret != NO_ERROR || buf == NULL) {
        ALOGD("DEBUG(%s):  prvcb stream(%d) dequeue_buffer fail res(%d)",__FUNCTION__ , stream->m_index,  ret);
		return ret;
    }
	//lock
	if (m_grallocHal->lock(m_grallocHal, *buf, subParms->usage, 0, 0,
                   subParms->width, subParms->height, &VirtBuf) != 0) {
		ALOGE("ERR(%s): could not obtain gralloc buffer", __FUNCTION__);
	}
	priv_handle = reinterpret_cast<const private_handle_t *>(*buf);

    if (subParms->format == HAL_PIXEL_FORMAT_YCrCb_420_SP) {
            ALOGD("DEBUG(%s):preview_w = %d, preview_h = %d, cb_w = %d, cb_h = %d",
                     __FUNCTION__, StreamParms->width, StreamParms->height,
                     subParms->width, subParms->height);
			memcpy((char *)(priv_handle->base),
                    srcBufVirt, (subParms->width * subParms->height * 3)/2);
    }
    //unlock
    if (m_grallocHal) {
        m_grallocHal->unlock(m_grallocHal, *buf);
    } else {
        ALOGD("ERR previewCBFrame gralloc is NULL");
	}

    ret = subParms->streamOps->enqueue_buffer(subParms->streamOps,
                                               frameTimeStamp,
                                               buf);

    ALOGD("DEBUG(%s): return %d",__FUNCTION__, ret);

    return ret;
}

int SprdCameraHWInterface2::previewCBFrame(PreviewStream *stream, int32_t *srcBufVirt, int64_t frameTimeStamp)
{
    stream_parameters_t     *StreamParms = &(stream->m_parameters);
    substream_parameters_t  *subParms    = &m_subStreams[STREAM_ID_PRVCB];
	void *VirtBuf = NULL;
    int  ret = 0;
    buffer_handle_t * buf = NULL;
	const private_handle_t *priv_handle = NULL;

	if ((NULL == subParms->streamOps) || (NULL == subParms->streamOps->dequeue_buffer)) {
		ALOGE("ERR(%s): haven't stream ops", __FUNCTION__);
		return 0;
	}

	ret = subParms->streamOps->dequeue_buffer(subParms->streamOps, &buf);
    if (ret != NO_ERROR || buf == NULL) {
        ALOGD("DEBUG(%s):  prvcb stream(%d) dequeue_buffer fail res(%d)",__FUNCTION__ , stream->m_index,  ret);
		return ret;
    }
	//lock
	if (m_grallocHal->lock(m_grallocHal, *buf, subParms->usage, 0, 0,
                   subParms->width, subParms->height, &VirtBuf) != 0) {
		ALOGE("ERR(%s): could not obtain gralloc buffer", __FUNCTION__);
	}
	priv_handle = reinterpret_cast<const private_handle_t *>(*buf);

    if (subParms->format == HAL_PIXEL_FORMAT_YCrCb_420_SP) {
            ALOGD("DEBUG(%s):preview_w = %d, preview_h = %d, cb_w = %d, cb_h = %d",
                     __FUNCTION__, StreamParms->width, StreamParms->height,
                     subParms->width, subParms->height);
			memcpy((char *)(priv_handle->base),
                    srcBufVirt, (subParms->width * subParms->height * 3)/2);
    }
    //unlock
    if (m_grallocHal) {
        m_grallocHal->unlock(m_grallocHal, *buf);
    } else {
        ALOGD("ERR previewCBFrame gralloc is NULL");
	}

    ret = subParms->streamOps->enqueue_buffer(subParms->streamOps,
                                               frameTimeStamp,
                                               buf);

    ALOGD("DEBUG(%s): return %d",__FUNCTION__, ret);

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
	ALOGV("transitionState: owner = %d, lock = %d", owner, lock);

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
		ALOGV("changeState: error owner");
		break;
	}

	if (NULL != which_ptr) {
		if (from != *which_ptr) {
			to = SPRD_ERROR;
		}

		ALOGV("changeState: %s --> %s", getCameraStateStr(from),
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
	ALOGV("HandleCameraStart in: cb = %d, parm4 = 0x%x, state = %s",
				cb, parm4, getCameraStateStr(getCameraState()));

	transitionState(SPRD_INIT, SPRD_IDLE, STATE_CAMERA);

	ALOGV("HandleCameraStart out, state = %s", getCameraStateStr(getCameraState()));
}

void SprdCameraHWInterface2::HandleStartPreview(camera_cb_type cb,
											 int32_t parm4)
{

	ALOGV("HandleStartPreview in: cb = %d, parm4 = 0x%x, state = %s",
				cb, parm4, getCameraStateStr(getPreviewState()));

	switch(cb) {
	case CAMERA_RSP_CB_SUCCESS:
		transitionState(SPRD_INTERNAL_PREVIEW_REQUESTED,
					SPRD_PREVIEW_IN_PROGRESS,
					STATE_PREVIEW);
		break;

	case CAMERA_EVT_CB_FRAME:
		ALOGV("CAMERA_EVT_CB_FRAME");
		switch (getPreviewState()) {
		case SPRD_PREVIEW_IN_PROGRESS:
			receivePreviewFrame((camera_frame_type *)parm4);
			break;

		case SPRD_INTERNAL_PREVIEW_STOPPING:
			ALOGV("camera cb: discarding preview frame "
			"while stopping preview");
			break;

		default:
			ALOGV("HandleStartPreview: invalid state");
			break;
			}
		break;

	case CAMERA_EXIT_CB_FAILED:			//Execution failed or rejected
		ALOGE("SprdCameraHardware::camera_cb: @CAMERA_EXIT_CB_FAILURE(%d) in state %s.",
				parm4, getCameraStateStr(getPreviewState()));
		transitionState(getPreviewState(), SPRD_ERROR, STATE_PREVIEW);
		//receiveCameraExitError();
		break;

	default:
		transitionState(getPreviewState(), SPRD_ERROR, STATE_PREVIEW);
		ALOGE("unexpected cb %d for CAMERA_FUNC_START_PREVIEW.", cb);
		break;
	}

	ALOGV("HandleStartPreview out, state = %s", getCameraStateStr(getPreviewState()));
}

void SprdCameraHWInterface2::receivePreviewFrame(camera_frame_type *frame)
{
	if (NULL == frame) {
		ALOGE("receivePreviewFrame: invalid frame pointer");
		return;
	}

    status_t res = 0;
    stream_parameters_t     *targetStreamParms;
	buffer_handle_t * buf = NULL;
    const private_handle_t *priv_handle = NULL;
    bool found = false;
    int Index = 0;

    if(m_previewStream)
	{
	    targetStreamParms = &(m_previewStream->m_parameters);
		targetStreamParms->bufIndex = frame->buf_id;
		targetStreamParms->m_timestamp = frame->timestamp;

		ALOGD("receivePreviewFrame Index=%d status=%d Firstfrm=%d",targetStreamParms->bufIndex,targetStreamParms->svcBufStatus[targetStreamParms->bufIndex], m_previewStream->m_IsFirstFrm);
		//check if kick cancel key first
		if(m_previewStream->getRecevStopMsg())
		{
		    ALOGD("receivePreviewFrame recev stop msg!");
            if(!m_previewStream->m_IsFirstFrm)
            {
                m_previewStream->m_IsFirstFrm = true;
				m_RequestQueueThread->SetSignal(SIGNAL_REQ_THREAD_REQ_DONE);//important
            }
			return;
		}
		//check buf status
		if(targetStreamParms->svcBufStatus[targetStreamParms->bufIndex] != ON_HAL_DRIVER && targetStreamParms->svcBufStatus[targetStreamParms->bufIndex] != ON_HAL_INIT)
		{
            ALOGE("receivepreviewframe BufStatus ERR(%d)",targetStreamParms->svcBufStatus[targetStreamParms->bufIndex]);
			targetStreamParms->svcBufStatus[targetStreamParms->bufIndex] = ON_HAL_BUFERR;
			if(!m_previewStream->m_IsFirstFrm)
			{
			    m_previewStream->m_IsFirstFrm = true;
			    m_RequestQueueThread->SetSignal(SIGNAL_REQ_THREAD_REQ_DONE);//important
			}
			return;
		}
		//first deq 2 buf for graphic
		if(targetStreamParms->cancelBufNum > 0)
		{
		   targetStreamParms->cancelBufNum--;
		   ALOGD("receivepreviewframe cancelbuf index=%d",targetStreamParms->bufIndex);
		   targetStreamParms->svcBufStatus[targetStreamParms->bufIndex] = ON_HAL_BUFQ;
		   m_previewStream->pushBufQ(targetStreamParms->bufIndex);//targetStreamParms->bufIndex
		   return;
		}
		else
	   {
	    //graphic deq 6 buf
		if(!m_previewStream->m_IsFirstFrm)
		{
            for(int j=0;j < (targetStreamParms->numSvcBuffers - targetStreamParms->minUndequedBuffer);j++)
			{
				found = false;
                res = targetStreamParms->streamOps->dequeue_buffer(targetStreamParms->streamOps, &buf);//first deq 4 from 8 bufs
	            if (res != NO_ERROR || buf == NULL) {
	                ALOGD("DEBUG(%s): first dequeue_buffer fail",__FUNCTION__);
	                return;
	            }
				priv_handle = reinterpret_cast<const private_handle_t *>(*buf);
				int phyaddr =0;
				int size =0;
				MemoryHeapIon::Get_phy_addr_from_ion(priv_handle->share_fd,&phyaddr,&size);
				for (Index = 0; Index < targetStreamParms->numSvcBuffers ; Index++) {
	                if (phyaddr == mPreviewHeapArray_phy[Index] && (targetStreamParms->svcBufStatus[Index] == ON_HAL_INIT || targetStreamParms->svcBufStatus[Index] == ON_HAL_BUFQ)) {
	                    found = true;
						ALOGD("receivePreviewFrame,Index=%d",Index);
						targetStreamParms->svcBufStatus[Index] = ON_HAL_DRIVER;
	                    break;
	                }
	            }
	            if (!found)
	        	{
	                ALOGD("ERR receivepreviewframe cannot found buf=0x%x ",priv_handle->phyaddr);
					return;
	        	}
			}
		}

	    if(m_previewStream->m_index != STREAM_ID_PREVIEW)
		{
			m_previewStream->pushBufQ(targetStreamParms->bufIndex);
			targetStreamParms->svcBufStatus[targetStreamParms->bufIndex] = ON_HAL_BUFQ;
			Index = m_previewStream->popBufQ();
			ALOGD("receivePreviewFrame,popIndex=%d,previewid=%d",Index,m_previewStream->m_index);
			res = camera_release_frame(Index);
			if(res)
			{
				ALOGD("ERR receivepreviewframe1 release frame!");
				targetStreamParms->svcBufStatus[Index] = ON_HAL_BUFERR;
			}
			targetStreamParms->svcBufStatus[Index] = ON_HAL_DRIVER;
			if(!m_previewStream->m_IsFirstFrm)
			{
				m_previewStream->m_IsFirstFrm = true;
				m_RequestQueueThread->SetSignal(SIGNAL_REQ_THREAD_REQ_DONE);
			}
			 return;
		}
		ALOGD("m_staticReqInfo.outputStreamMask=0x%x.",m_staticReqInfo.outputStreamMask);
        if(m_staticReqInfo.outputStreamMask & STREAM_MASK_PRVCB) {
		    int i = 0;
            for (; i < NUM_MAX_SUBSTREAM ; i++) {
	            if (m_previewStream->m_attachedSubStreams[i].streamId == STREAM_ID_PRVCB) {
	        	    previewCBFrame(m_previewStream,
						(int32_t*)mPreviewHeapArray_vir[targetStreamParms->bufIndex],
					targetStreamParms->m_timestamp);
	                break;
				}
			}
			if(i == NUM_MAX_SUBSTREAM) {
				ALOGD("err m_Camera2DealwithSubstream substream not regist");
			}
		}
		if(m_staticReqInfo.outputStreamMask & STREAM_MASK_RECORD) {
		    int i = 0;
			substream_parameters_t *subParameters;
			subParameters = &m_subStreams[STREAM_ID_RECORD];
            for (; i < NUM_MAX_SUBSTREAM ; i++) {
	           // if (m_previewStream->m_attachedSubStreams[i].streamId == STREAM_ID_RECORD) {
				if (SUBSTREAM_TYPE_RECORD == subParameters->type) {
					recordingFrame(m_previewStream,
									(int32_t*)mPreviewHeapArray_vir[targetStreamParms->bufIndex],
									targetStreamParms->m_timestamp);
	                break;
				}
		}
			if(i == NUM_MAX_SUBSTREAM) {
			  ALOGD("err m_Camera2DealwithSubstream substream not regist");
			}
		}

        if(m_staticReqInfo.outputStreamMask & STREAM_MASK_PREVIEW) {
			ALOGD("receivePreviewFrame Display Preview,add=0x%x",
				 (uint32_t)targetStreamParms->svcBufHandle[targetStreamParms->bufIndex]);

			if(targetStreamParms->svcBufStatus[targetStreamParms->bufIndex] == ON_HAL_DRIVER)//can enq graphic buf
			{
			    ALOGD("wjp test enqueue data.");
                res = targetStreamParms->streamOps->enqueue_buffer(targetStreamParms->streamOps, targetStreamParms->m_timestamp,
                                 &(targetStreamParms->svcBufHandle[targetStreamParms->bufIndex]));
				if(res)
				{
				    targetStreamParms->svcBufStatus[targetStreamParms->bufIndex] = ON_HAL_BUFERR;
					ALOGD("Err receivePreviewFrame Display Preview(res=%d)",res);
					if(!m_previewStream->m_IsFirstFrm)
					{
					   m_previewStream->m_IsFirstFrm = true;
				       m_RequestQueueThread->SetSignal(SIGNAL_REQ_THREAD_REQ_DONE);//important
					}
					return;
				}
				else
		            targetStreamParms->svcBufStatus[targetStreamParms->bufIndex] = ON_SERVICE;
                if(!m_previewStream->m_IsFirstFrm)
                {
                    m_previewStream->m_IsFirstFrm = true;
				    m_RequestQueueThread->SetSignal(SIGNAL_REQ_THREAD_REQ_DONE);//important
                }
					//deq one
				found = false;
				res = targetStreamParms->streamOps->dequeue_buffer(targetStreamParms->streamOps, &buf);
                if (res != NO_ERROR || buf == NULL) {
	                ALOGD("DEBUG(%s): dequeue_buffer fail",__FUNCTION__);
	                return;
	            }
				priv_handle = reinterpret_cast<const private_handle_t *>(*buf);
				int phyaddr =0;
				int size =0;
				MemoryHeapIon::Get_phy_addr_from_ion(priv_handle->share_fd,&phyaddr,&size);
				for (Index = 0; Index < targetStreamParms->numSvcBuffers ; Index++) {
	                if (phyaddr == mPreviewHeapArray_phy[Index] && (targetStreamParms->svcBufStatus[Index] == ON_SERVICE || targetStreamParms->svcBufStatus[Index] == ON_HAL_INIT)) {
	                    found = true;
						ALOGD("receivePreviewFrame,Index=%d sta=%d",Index,targetStreamParms->svcBufStatus[Index]);
	                    break;
	                }
	            }
	            if (!found)
	        	{
	                ALOGE("ERR receivepreviewframe cannot found buf=0x%x ",priv_handle->phyaddr);
					return;
	        	}
				m_previewStream->pushBufQ(Index);
				targetStreamParms->svcBufStatus[Index] = ON_HAL_BUFQ;
				Index = m_previewStream->popBufQ();
				ALOGD("receivePreviewFrame newindex=%d",Index);
				res = camera_release_frame(Index);
				if(res)
				{
	               ALOGD("ERR receivepreviewframe release buf deq from graphic");
				   targetStreamParms->svcBufStatus[Index] = ON_HAL_BUFERR;
				}
				targetStreamParms->svcBufStatus[Index] = ON_HAL_DRIVER;
			}
			else
			{
			    m_previewStream->pushBufQ(targetStreamParms->bufIndex);
				targetStreamParms->svcBufStatus[targetStreamParms->bufIndex] = ON_HAL_BUFQ;
				Index = m_previewStream->popBufQ();
				ALOGD("receivepreviewframe not equal srv bufq, index=%d",Index);
                res = camera_release_frame(Index);
				if(res)
				{
	               ALOGD("ERR receivepreviewframe release buf deq from graphic"); 
				   targetStreamParms->svcBufStatus[Index] = ON_HAL_BUFERR;
				}
				targetStreamParms->svcBufStatus[Index] = ON_HAL_DRIVER;
				if(!m_previewStream->m_IsFirstFrm)
				{
				    m_previewStream->m_IsFirstFrm = true;
				    m_RequestQueueThread->SetSignal(SIGNAL_REQ_THREAD_REQ_DONE);//important
				}
			}
	    }
        else
		{
			m_previewStream->pushBufQ(targetStreamParms->bufIndex);
			targetStreamParms->svcBufStatus[targetStreamParms->bufIndex] = ON_HAL_BUFQ;
			Index = m_previewStream->popBufQ();
			ALOGD("receivePreviewFrame stream not output,popIndex=%d",Index);
			res = camera_release_frame(Index);
			if(res)
			{
                 ALOGD("ERR receivepreviewframe2 release frame!");
				 targetStreamParms->svcBufStatus[Index] = ON_HAL_BUFERR;
			}
			targetStreamParms->svcBufStatus[Index] = ON_HAL_DRIVER;
			if(!m_previewStream->m_IsFirstFrm)
			{
				m_previewStream->m_IsFirstFrm = true;
                m_RequestQueueThread->SetSignal(SIGNAL_REQ_THREAD_REQ_DONE);//important
            }
		}
	}
	}
}

void SprdCameraHWInterface2::RequestQueueThreadFunc(SprdBaseThread * self)
{
    camera_metadata_t *curReq = NULL;
    camera_metadata_t *newReq = NULL;
    size_t numEntries = 0;
    size_t frameSize = 0;
    uint32_t currentSignal = self->GetProcessingSignal();
    RequestQueueThread *  selfThread      = ((RequestQueueThread*)self);
    int ret;

    ALOGD("DEBUG(%s): RequestQueueThread (%x)", __FUNCTION__, currentSignal);

    if (currentSignal & SIGNAL_THREAD_RELEASE) {
        ClearReqQ();
        ALOGD("DEBUG(%s): processing SIGNAL_THREAD_RELEASE DONE", __FUNCTION__);
        selfThread->SetSignal(SIGNAL_THREAD_TERMINATE);
        return;
    }

    if (currentSignal & SIGNAL_REQ_THREAD_REQ_Q_NOT_EMPTY) {
        ALOGD("DEBUG(%s): RequestQueueThread processing SIGNAL_REQ_THREAD_REQ_Q_NOT_EMPTY", __FUNCTION__);
        curReq = PopReqQ();
        if (!curReq) {
            ALOGD("DEBUG(%s): No more service requests left in the queue!", __FUNCTION__);
        }
        else
		{
		    ALOGD("DEBUG(%s): bef curreq=0x%x", __FUNCTION__, (uint32_t)curReq);
		    Camera2GetSrvReqInfo(&m_staticReqInfo, curReq);
            ALOGD("DEBUG(%s): aft", __FUNCTION__);
        }
    }

    if (currentSignal & SIGNAL_REQ_THREAD_REQ_DONE) {
		bool IsProcessing = false;
        IsProcessing = GetReqProcessStatus();
		if(!IsProcessing)
		{
           ALOGE("ERR %s status should processing",__FUNCTION__);
		}
		ret = Camera2RefreshSrvReq(&m_staticReqInfo,m_halRefreshReq);
		if(ret)
		{
           ALOGE("ERR %s refresh req",__FUNCTION__);
		}
		ALOGD("DEBUG(%s):  processing SIGNAL_REQ_THREAD_REQ_DONE,free add=0x%x", __FUNCTION__, (uint32_t)m_staticReqInfo.ori_req);
        ret = m_requestQueueOps->free_request(m_requestQueueOps, m_staticReqInfo.ori_req);//orignal preview req metadata
        if (ret < 0)
            ALOGE("ERR(%s): free_request ret = %d", __FUNCTION__, ret);
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
            ALOGE("ERR(%s): dequeue_frame ret = %d", __FUNCTION__, ret);

        if (newReq==NULL) {
            ALOGV("DBG(%s): frame dequeue returned NULL",__FUNCTION__ );
        }
        else {
            ALOGV("DEBUG(%s): frame dequeue done. numEntries(%d) frameSize(%d)",__FUNCTION__ , numEntries, frameSize);
        }
        ret = append_camera_metadata(newReq, m_halRefreshReq);
        if (ret == 0) {
            ALOGD("DEBUG(%s): frame metadata append success add=0x%x",__FUNCTION__, (uint32_t)newReq);
            m_frameQueueOps->enqueue_frame(m_frameQueueOps, newReq);
        }
        else {
            ALOGE("ERR(%s): frame metadata append fail (%d)",__FUNCTION__, ret);
        }
		if (GetReqQueueSize() > 0)
	               selfThread->SetSignal(SIGNAL_REQ_THREAD_REQ_Q_NOT_EMPTY);
    }
    ALOGV("DEBUG(%s): RequestQueueThread Exit", __FUNCTION__);
    return;
}

SprdCameraHWInterface2::RequestQueueThread::~RequestQueueThread()
{
   ALOGV("DEBUG(%s):", __FUNCTION__);
}


static camera2_device_t *g_cam2_device = NULL;
static bool g_camera_vaild = false;
static Mutex g_camera_mutex;

static int HAL2_camera_device_close(struct hw_device_t* device)
{
    Mutex::Autolock lock(g_camera_mutex);
    ALOGD("(%s): ENTER", __FUNCTION__);
    if (device) {

        camera2_device_t *cam_device = (camera2_device_t *)device;
        ALOGV("cam_device(0x%08x):", (unsigned int)cam_device);
        ALOGV("g_cam2_device(0x%08x):", (unsigned int)g_cam2_device);
        delete static_cast<SprdCameraHWInterface2 *>(cam_device->priv);
        free(cam_device);
        g_camera_vaild = false;
        g_cam2_device = NULL;
    }

    ALOGD("(%s): EXIT", __FUNCTION__);
    return 0;
}

static inline SprdCameraHWInterface2 *obj(const struct camera2_device *dev)
{
    return reinterpret_cast<SprdCameraHWInterface2 *>(dev->priv);
}

static int HAL2_device_set_request_queue_src_ops(const struct camera2_device *dev,
            const camera2_request_queue_src_ops_t *request_src_ops)
{
    ALOGV("DEBUG(%s):", __FUNCTION__);
    return obj(dev)->setRequestQueueSrcOps(request_src_ops);
}

static int HAL2_device_notify_request_queue_not_empty(const struct camera2_device *dev)
{
    ALOGV("DEBUG(%s):", __FUNCTION__);
    return obj(dev)->notifyRequestQueueNotEmpty();
}

static int HAL2_device_set_frame_queue_dst_ops(const struct camera2_device *dev,
            const camera2_frame_queue_dst_ops_t *frame_dst_ops)
{
    ALOGV("DEBUG(%s):", __FUNCTION__);
    return obj(dev)->setFrameQueueDstOps(frame_dst_ops);
}

static int HAL2_device_get_in_progress_count(const struct camera2_device *dev)
{
    ALOGV("DEBUG(%s):", __FUNCTION__);
    return obj(dev)->getInProgressCount();
}

static int HAL2_device_flush_captures_in_progress(const struct camera2_device *dev)
{
    ALOGV("DEBUG(%s):", __FUNCTION__);
    return obj(dev)->flushCapturesInProgress();
}

static int HAL2_device_construct_default_request(const struct camera2_device *dev,
            int request_template, camera_metadata_t **request)
{
    ALOGV("DEBUG(%s):", __FUNCTION__);
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
    ALOGV("(%s): ", __FUNCTION__);
    return obj(dev)->allocateStream(width, height, format, stream_ops,
                                    stream_id, format_actual, usage, max_buffers);
}

static int HAL2_device_register_stream_buffers(const struct camera2_device *dev,
            uint32_t stream_id,
            int num_buffers,
            buffer_handle_t *buffers)
{
    ALOGV("DEBUG(%s):", __FUNCTION__);
    return obj(dev)->registerStreamBuffers(stream_id, num_buffers, buffers);
}

static int HAL2_device_release_stream(
        const struct camera2_device *dev,
            uint32_t stream_id)
{
    ALOGV("DEBUG(%s)(id: %d):", __FUNCTION__, stream_id);
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
    ALOGV("DEBUG(%s):", __FUNCTION__);
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
    ALOGV("DEBUG(%s):", __FUNCTION__);
    return obj(dev)->allocateReprocessStreamFromStream(output_stream_id,
                                    reprocess_stream_ops, stream_id);
}

static int HAL2_device_release_reprocess_stream(
        const struct camera2_device *dev,
            uint32_t stream_id)
{
    ALOGV("DEBUG(%s):", __FUNCTION__);
    return obj(dev)->releaseReprocessStream(stream_id);
}

static int HAL2_device_trigger_action(const struct camera2_device *dev,
           uint32_t trigger_id,
            int ext1,
            int ext2)
{
    ALOGV("DEBUG(%s):", __FUNCTION__);
    if (!g_camera_vaild)
        return 0;
    return obj(dev)->triggerAction(trigger_id, ext1, ext2);
}

static int HAL2_device_set_notify_callback(const struct camera2_device *dev,
            camera2_notify_callback notify_cb,
            void *user)
{
    ALOGV("DEBUG(%s):", __FUNCTION__);
    return obj(dev)->setNotifyCallback(notify_cb, user);
}

static int HAL2_device_get_metadata_vendor_tag_ops(const struct camera2_device*dev,
            vendor_tag_query_ops_t **ops)
{
    ALOGV("DEBUG(%s):", __FUNCTION__);
    return obj(dev)->getMetadataVendorTagOps(ops);
}

static int HAL2_device_dump(const struct camera2_device *dev, int fd)
{
    ALOGV("DEBUG(%s):", __FUNCTION__);
    return obj(dev)->dump(fd);
}





static int HAL2_getNumberOfCameras()
{
    ALOGV("(%s): returning 2", __FUNCTION__);
    return 2;
}

static status_t ConstructStaticInfo(SprdCamera2Info *camerahal, camera_metadata_t **info,
        int cameraId, bool sizeRequest) {

    size_t entryCount = 0;
    size_t dataCount = 0;
    status_t ret;

    ALOGD("ConstructStaticInfo info_add=0x%x,sizereq=%d",
                (int)*info, (int)sizeRequest);
#define ADD_OR_SIZE( tag, data, count ) \
    if ( ( ret = addOrSize(*info, sizeRequest, &entryCount, &dataCount, \
            tag, data, count) ) != OK ) return ret

    // android.info

    int32_t hardwareLevel = ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_LIMITED;//ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_FULL
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
    static const float sensorPhysicalSize[2] = {3.50f, 2.625f}; // mm
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

    static const float maxZoom = 4;
    ADD_OR_SIZE(ANDROID_SCALER_AVAILABLE_MAX_DIGITAL_ZOOM, &maxZoom, 1);

    // android.jpeg
    static const int32_t jpegThumbnailSizes[] = {
            160, 120,
            160, 160,
            160, 90,
            144, 96,
              0, 0
    };

    ADD_OR_SIZE(ANDROID_JPEG_AVAILABLE_THUMBNAIL_SIZES,
            jpegThumbnailSizes, sizeof(jpegThumbnailSizes)/sizeof(int32_t));
    
    static const int32_t jpegMaxSize = 10 * 1024 * 1024;
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
    #if 0
    static const uint8_t availableSceneModes[] = {
            ANDROID_CONTROL_SCENE_MODE_ACTION,
            ANDROID_CONTROL_SCENE_MODE_NIGHT,
            ANDROID_CONTROL_SCENE_MODE_SUNSET,
            ANDROID_CONTROL_SCENE_MODE_PARTY
    };
	#endif
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
            15, 15, 24, 24, 25, 25, 15, 30, 30, 30
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

    ADD_OR_SIZE(ANDROID_CONTROL_SCENE_MODE_OVERRIDES,
            camerahal->sceneModeOverrides, camerahal->numSceneModeOverrides);

    static const uint8_t quirkTriggerAuto = 1;
    ADD_OR_SIZE(ANDROID_QUIRKS_TRIGGER_AF_WITH_AUTO,
            &quirkTriggerAuto, 1);

    static const uint8_t quirkUseZslFormat = 1;//1zsl without realization
    ADD_OR_SIZE(ANDROID_QUIRKS_USE_ZSL_FORMAT,
            &quirkUseZslFormat, 1);

    static const uint8_t quirkMeteringCropRegion = 1;
    ADD_OR_SIZE(ANDROID_QUIRKS_METERING_CROP_REGION,
            &quirkMeteringCropRegion, 1);


#undef ADD_OR_SIZE
    /** Allocate metadata if sizing */
    if (sizeRequest) {
        ALOGD("Allocating %d entries, %d extra bytes for "
                "static camera info",
                entryCount, dataCount);
        *info = allocate_camera_metadata(entryCount, dataCount);
        if (*info == NULL) {
            ALOGE("Unable to allocate camera static info"
                    "(%d entries, %d bytes extra data)",
                    entryCount, dataCount);
            return NO_MEMORY;
        }
    }
    return OK;
}

static int HAL2_getCameraInfo(int cameraId, struct camera_info *info)//malloc camera_meta for load default paramet
{
    ALOGD("DEBUG(%s): cameraID: %d", __FUNCTION__, cameraId);
    static camera_metadata_t * mCameraInfo[2] = {NULL, NULL};
    status_t ret;
    property_set("camera.disable_zsl_mode","1");
    if (cameraId == 0) {
        info->facing = CAMERA_FACING_BACK;//facing means back cam or front cam
        info->orientation = 90;
        if (!g_Camera2[0])
        {
            g_Camera2[0] = (SprdCamera2Info *)malloc(sizeof(SprdCamera2Info));
			if(!g_Camera2[0])
			   ALOGE("%s no mem! backcam",__FUNCTION__);

            initCamera2Info(cameraId);
        }
    } else if (cameraId == 1) {
        info->facing = CAMERA_FACING_FRONT;
		info->orientation = 270;
        if (!g_Camera2[1]) {
            g_Camera2[1] = (SprdCamera2Info *)malloc(sizeof(SprdCamera2Info));
			if(!g_Camera2[1])
			   ALOGE("%s no mem! frontcam",__FUNCTION__);

			initCamera2Info(cameraId);
        }
    } else {
        return BAD_VALUE;
    }

    info->device_version = HARDWARE_DEVICE_API_VERSION(2, 0);
    if (mCameraInfo[cameraId] == NULL) {
        ret = ConstructStaticInfo(g_Camera2[cameraId],&(mCameraInfo[cameraId]), cameraId, true);
        if (ret != OK) {
            ALOGE("%s: Unable to allocate static info: %s (%d)",
                    __FUNCTION__, strerror(-ret), ret);
            return ret;
        }
        ret = ConstructStaticInfo(g_Camera2[cameraId], &(mCameraInfo[cameraId]), cameraId, false);
        if (ret != OK) {
            ALOGE("%s: Unable to fill in static info: %s (%d)",
                    __FUNCTION__, strerror(-ret), ret);
            return ret;
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
        ALOGE("ERR(%s): Can't open, other camera is in use", __FUNCTION__);
        return -EBUSY;
    }
    g_camera_vaild = false;
    ALOGD("\n\n>>> SPRD CameraHAL_2(ID:%d) <<<\n\n", cameraId);
    if (cameraId < 0 || cameraId >= HAL2_getNumberOfCameras()) {
        ALOGE("ERR(%s):Invalid camera ID %s", __FUNCTION__, id);
        return -EINVAL;
    }

    ALOGD("g_cam2_device : 0x%08x", (unsigned int)g_cam2_device);
    if (g_cam2_device) {
        if (obj(g_cam2_device)->getCameraId() == cameraId) {
            ALOGD("DEBUG(%s):returning existing camera ID %s", __FUNCTION__, id);
            goto done;
        } else {
            ALOGD("(%s): START waiting for cam device free", __FUNCTION__);
            while (g_cam2_device)
                usleep(SIG_WAITING_TICK);
            ALOGD("(%s): END   waiting for cam device free", __FUNCTION__);
        }
    }

    g_cam2_device = (camera2_device_t *)malloc(sizeof(camera2_device_t));
    ALOGV("g_cam2_device : 0x%08x", (unsigned int)g_cam2_device);

    if (!g_cam2_device)
        return -ENOMEM;

    g_cam2_device->common.tag     = HARDWARE_DEVICE_TAG;
    g_cam2_device->common.version = CAMERA_DEVICE_API_VERSION_2_0;
    g_cam2_device->common.module  = const_cast<hw_module_t *>(module);
    g_cam2_device->common.close   = HAL2_camera_device_close;

    g_cam2_device->ops = &camera2_device_ops;

    ALOGD("DEBUG(%s):open camera2 %s", __FUNCTION__, id);

    g_cam2_device->priv = new SprdCameraHWInterface2(cameraId, g_cam2_device, g_Camera2[cameraId], &openInvalid);
    if (openInvalid) {
        ALOGE("DEBUG(%s): SprdCameraHWInterface2 creation failed", __FUNCTION__);
        return -ENODEV;
    }
done:
    *device = (hw_device_t *)g_cam2_device;
    ALOGV("DEBUG(%s):opened camera2 %s (%p)", __FUNCTION__, id, *device);
    g_camera_vaild = true;

    return 0;
}


static hw_module_methods_t camera_module_methods = {
            open : HAL2_camera_device_open
};

extern "C" {
    struct camera_module HAL_MODULE_INFO_SYM = {
      common : {
#ifdef CONFIG_CAMERA_HAL_20
          tag                : HARDWARE_MODULE_TAG,
#else
          tag                : 1,
#endif
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
      set_callbacks         : NULL
    };
}

} // namespace android

