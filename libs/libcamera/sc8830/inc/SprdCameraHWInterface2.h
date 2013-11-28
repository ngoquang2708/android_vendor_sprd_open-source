/*
**
** Copyright 2008, The Android Open Source Project
** Copyright 2012, Samsung Electronics Co. LTD
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#ifndef SPRD_CAMERA_HW_INTERFACE_2_H
#define SPRD_CAMERA_HW_INTERFACE_2_H

#include <hardware/camera2.h>
#include <camera/Camera.h>
#include <camera/CameraParameters.h>
#include <utils/List.h>
#include "SprdBaseThread.h"
#include "gralloc_priv.h"
#include <fcntl.h>

#include "SprdCamera2.h"

#include "cutils/properties.h"
#include "SprdOEMCamera.h"

namespace android {


#define NUM_MAX_STREAM_THREAD       (5)
#define NUM_MAX_CAMERA_BUFFERS      (16)

#define NUM_MAX_SUBSTREAM           (4)

#define STREAM_ID_PREVIEW           (1)
#define STREAM_MASK_PREVIEW         (1<<STREAM_ID_PREVIEW)
#define STREAM_ID_RECORD            (2)
#define STREAM_MASK_RECORD          (1<<STREAM_ID_RECORD)
#define STREAM_ID_PRVCB             (3)
#define STREAM_MASK_PRVCB           (1<<STREAM_ID_PRVCB)
#define STREAM_ID_JPEG              (4)
#define STREAM_MASK_JPEG            (1<<STREAM_ID_JPEG)
#define STREAM_ID_JPEG_REPROCESS    (8)
#define STREAM_ID_LAST              STREAM_ID_JPEG_REPROCESS

#define SIGNAL_REQ_THREAD_REQ_Q_NOT_EMPTY             (SIGNAL_THREAD_COMMON_LAST<<1)

#define SIGNAL_REQ_THREAD_REQ_DONE          (SIGNAL_THREAD_COMMON_LAST<<3)

#define SIGNAL_THREAD_RELEASE                   (SIGNAL_THREAD_COMMON_LAST<<8)

#define SIGNAL_STREAM_REPROCESSING_START        (SIGNAL_THREAD_COMMON_LAST<<14)


#define SUBSTREAM_TYPE_NONE         (0)
#define SUBSTREAM_TYPE_JPEG         (1)
#define SUBSTREAM_TYPE_RECORD       (2)
#define SUBSTREAM_TYPE_PRVCB        (3)
#define FLASH_STABLE_WAIT_TIMEOUT        (10)

#define SIG_WAITING_TICK            (5000)

#define MAX_REQUEST_NUM              (8)


#define ON_HAL_BUFQ         (1)
#define ON_HAL_BUFERR       (2)
#define ON_HAL_DRIVER       (3)
#define ON_SERVICE          (4)
#define ON_HAL_INIT         (5)//service deq 6 bufs firstly


typedef struct stream_parameters {
            uint32_t                width;
            uint32_t                height;
            int                     format;
    const   camera2_stream_ops_t*   streamOps;
            uint32_t                usage;
            int                     numSvcBuffers;
            int                     numSvcBufsInHal;
            buffer_handle_t         svcBufHandle[NUM_MAX_CAMERA_BUFFERS];
            int                     svcBufStatus[NUM_MAX_CAMERA_BUFFERS];
            int                     bufIndex;
            int                     minUndequedBuffer;
			int                     cancelBufNum;
			int64_t                 m_timestamp;
} stream_parameters_t;

typedef struct substream_parameters {
            int                     type;
            uint32_t                width;
            uint32_t                height;
            int                     format;
    const   camera2_stream_ops_t*   streamOps;
            uint32_t                usage;
            int                     numSvcBuffers;
            int                     numOwnSvcBuffers;
            buffer_handle_t         svcBufHandle[NUM_MAX_CAMERA_BUFFERS];
            uint32_t                subStreamAddVirt[NUM_MAX_CAMERA_BUFFERS];
			int                     subStreamGraphicFd[NUM_MAX_CAMERA_BUFFERS];//previewCbAddPhy
			int                     svcBufStatus[NUM_MAX_CAMERA_BUFFERS];
            int                     svcBufIndex;
            int                     numSvcBufsInHal;
            int                     minUndequedBuffer;
} substream_parameters_t;

typedef struct substream_entry {
    int                     priority;
    int                     streamId;
} substream_entry_t;

typedef struct _SprdCamera2Info
{
    int32_t     sensorW;
    int32_t     sensorH;
    int32_t     sensorRawW;
    int32_t     sensorRawH;
    int         numPreviewResolution;
    const int32_t     *PreviewResolutions;
    int         numJpegResolution;
    const int32_t     *jpegResolutions;
    float       minFocusDistance;
    float       focalLength;
    float       aperture;
    float       fnumber;
    const uint8_t       *availableAfModes;
    const uint8_t       *sceneModeOverrides;
    const uint8_t       *availableAeModes;
    int         numAvailableAfModes;
    int         numSceneModeOverrides;
    int         numAvailableAeModes;

    /* FD information */
    int32_t    maxFaceCount;
}SprdCamera2Info;



class SprdCameraHWInterface2 : public virtual RefBase {
public:
    SprdCameraHWInterface2(int cameraId, camera2_device_t *dev, SprdCamera2Info * camera, int *openInvalid);
    virtual             ~SprdCameraHWInterface2();

    virtual void        release();

    inline  int         getCameraId() const
	{
        return m_CameraId;
	}

    virtual int         setRequestQueueSrcOps(const camera2_request_queue_src_ops_t *request_src_ops);
    virtual int         notifyRequestQueueNotEmpty();
    virtual int         setFrameQueueDstOps(const camera2_frame_queue_dst_ops_t *frame_dst_ops);
    virtual int         getInProgressCount();
    virtual int         flushCapturesInProgress();
    virtual int         ConstructDefaultRequest(int request_template, camera_metadata_t **request);
    virtual int         allocateStream(uint32_t width, uint32_t height,
                                    int format, const camera2_stream_ops_t *stream_ops,
                                    uint32_t *stream_id, uint32_t *format_actual, uint32_t *usage, uint32_t *max_buffers);
    virtual int         registerStreamBuffers(uint32_t stream_id, int num_buffers, buffer_handle_t *buffers);
    virtual int         releaseStream(uint32_t stream_id);
    virtual int         allocateReprocessStream(uint32_t width, uint32_t height,
                                    uint32_t format, const camera2_stream_in_ops_t *reprocess_stream_ops,
                                    uint32_t *stream_id, uint32_t *consumer_usage, uint32_t *max_buffers);
    virtual int         allocateReprocessStreamFromStream(uint32_t output_stream_id,
                                const camera2_stream_in_ops_t *reprocess_stream_ops, uint32_t *stream_id);
    virtual int         releaseReprocessStream(uint32_t stream_id);
    virtual int         triggerAction(uint32_t trigger_id, int ext1, int ext2);
    virtual int         setNotifyCallback(camera2_notify_callback notify_cb, void *user);
    virtual int         getMetadataVendorTagOps(vendor_tag_query_ops_t **ops);
    virtual int         dump(int fd);

private:

    typedef enum _metadata_mode {
		METADATA_MODE_NONE,
		METADATA_MODE_FULL
	}metadata_mode;

	typedef enum _capture_intent {
		CAPTURE_INTENT_CUSTOM = 0,
		CAPTURE_INTENT_PREVIEW,
		CAPTURE_INTENT_STILL_CAPTURE,
		CAPTURE_INTENT_VIDEO_RECORD,
		CAPTURE_INTENT_VIDEO_SNAPSHOT,
		CAPTURE_INTENT_ZERO_SHUTTER_LAG
	}capture_intent;

	typedef enum _ctl_mode {
		CONTROL_NONE = 0,
		CONTROL_OFF,
		CONTROL_AUTO,
		CONTROL_USE_SCENE_MODE
	}ctl_mode;

	enum Sprd_camera_state {
		SPRD_INIT,
		SPRD_IDLE,
		SPRD_ERROR,
		SPRD_PREVIEW_IN_PROGRESS,
		SPRD_FOCUS_IN_PROGRESS,
		SPRD_WAITING_RAW,
		SPRD_WAITING_JPEG,

		// internal states
		SPRD_INTERNAL_PREVIEW_STOPPING,
		SPRD_INTERNAL_CAPTURE_STOPPING,
		SPRD_INTERNAL_PREVIEW_REQUESTED,
		SPRD_INTERNAL_RAW_REQUESTED,
		SPRD_INTERNAL_STOPPING
	};

	enum state_owner {
		STATE_CAMERA,
		STATE_PREVIEW,
		STATE_CAPTURE,
		STATE_FOCUS,
	};
    typedef struct _cropZoom{
        uint32_t crop_x;
		uint32_t crop_y;
		uint32_t crop_w;
		uint32_t crop_h;
	}cropZoom;
	typedef struct _camera_state	{
		Sprd_camera_state 	camera_state;
		Sprd_camera_state 	preview_state;
		Sprd_camera_state 	capture_state;
		Sprd_camera_state 	focus_state;
	} camera_state;

	typedef struct _camera_req_info	{
		camera_metadata_t * ori_req;
		int32_t            requestID;//the flag of using at the same time
		uint32_t           frmCnt;
		uint32_t	       cropRegion0;//for preview
		uint32_t	       cropRegion1;
		uint32_t	       cropRegion2;
		uint32_t	       cropRegion3;
		uint8_t 	     outputStreamMask;
		uint8_t            isReprocessing;
		int8_t             sceneMode;
		capture_intent     captureIntent;//android action
		ctl_mode           ctlMode;
		metadata_mode    metadataMode;
	} camera_req_info;

    static const int kPreviewBufferCount = 8;
	static const int kPreviewRotBufferCount = 4;

class RequestQueueThread : public SprdBaseThread{
        SprdCameraHWInterface2 *mHardware;
    public:
        RequestQueueThread(SprdCameraHWInterface2 *hw):
            SprdBaseThread(),
            mHardware(hw) { }
        ~RequestQueueThread();
        void threadDealWithSiganl()
	    {
            mHardware->RequestQueueThreadFunc(this);
            return;
        }
        void        release(void);
        bool        m_releasing;
    };

    class PreviewStream{
        SprdCameraHWInterface2 *mHardware;
    public:
        PreviewStream(SprdCameraHWInterface2 *hw, uint8_t new_index):
            mHardware(hw),
			m_IsRecevStopMsg(false),
			m_IsFirstFrm(false),
			m_numRegisteredStream(1),
            m_index(new_index) {
            memset(&m_parameters, 0, sizeof(stream_parameters_t));
			for (int i = 0 ; i < NUM_MAX_SUBSTREAM ; i++) {
		        m_attachedSubStreams[i].streamId    = -1;
		        m_attachedSubStreams[i].priority    = 0;
		    }
			}
        ~PreviewStream();
        status_t    attachSubStream(int stream_id, int priority);
        status_t    detachSubStream(int stream_id);
        void        pushBufQ(int index);
        int         popBufQ();
		void        releaseBufQ();
		void        setRecevStopMsg(bool IsRecevMsg);
		bool        getRecevStopMsg();
        uint8_t                         m_index;
    //private:
        stream_parameters_t             m_parameters;
        substream_entry_t               m_attachedSubStreams[NUM_MAX_SUBSTREAM];
		int                             m_numRegisteredStream;
		List<int>                       m_bufQueue;
		Mutex                           m_BufQLock;
		bool                            m_IsRecevStopMsg;
		bool                            m_IsFirstFrm;
     };

    void                RequestQueueThreadFunc(SprdBaseThread * self);
	void                SetReqProcessing(bool IsProc);
	bool                GetReqProcessStatus();
	void                Camera2GetSrvReqInfo( camera_req_info *srcreq, camera_metadata_t *orireq);
	void                CameraConvertCropRegion(float **outPut, float sensorWidth, float sensorHeight, cropZoom *cropRegion);
	status_t            Camera2RefreshSrvReq(camera_req_info *srcreq, camera_metadata_t *dstreq);
	status_t            CamconstructDefaultRequest(SprdCamera2Info *camHal, int request_template,camera_metadata_t **request, bool sizeRequest);
    bool                isSupportedJpegResolution(SprdCamera2Info *camHal, int width, int height);
	bool                isSupportedResolution(SprdCamera2Info *camHal, int width, int height);
	int                 previewCBFrame(PreviewStream *stream, int32_t *srcBufVirt, int64_t frameTimeStamp);
	int                 recordingFrame(PreviewStream *stream, int32_t *srcBufVirt, int64_t frameTimeStamp);
	void                receivePreviewFrame(camera_frame_type *frame);
	void                HandleStartPreview(camera_cb_type cb, int32_t parm4);
    void                HandleStartCamera(camera_cb_type cb,
								  		int32_t parm4);
	void                PushReqQ(camera_metadata_t *reqInfo);
	int              GetReqQueueSize();
	camera_metadata_t *PopReqQ();
	void                ClearReqQ();
	const char* getCameraStateStr(Sprd_camera_state s);
	Sprd_camera_state transitionState(Sprd_camera_state from,
						Sprd_camera_state to,
						state_owner owner,
						bool lock = true);
	void                            setCameraState(Sprd_camera_state state,
								state_owner owner = STATE_CAMERA);
	inline Sprd_camera_state        getCameraState()
	{
       return mCameraState.camera_state;
	}
	inline Sprd_camera_state        getPreviewState()
	{
       return mCameraState.preview_state;
	}

	static void camera_cb(camera_cb_type cb,
            const void *client_data,
            camera_func_type func,
            int32_t parm4);
	bool                            WaitForPreviewStart();
	bool                            WaitForCameraStop();
	bool                            WaitForPreviewStop();
	void                  HandleStopCamera(camera_cb_type cb, int32_t parm4);
	void                  HandleStopPreview(camera_cb_type cb, int32_t parm4);
	sp<RequestQueueThread>      m_RequestQueueThread;
    substream_parameters_t  m_subStreams[STREAM_ID_LAST+1];
    PreviewStream *m_previewStream;
    SprdCamera2Info      *m_Camera2;
    camera2_request_queue_src_ops_t     *m_requestQueueOps;
    camera2_frame_queue_dst_ops_t       *m_frameQueueOps;
    camera2_notify_callback             m_notifyCb;
    void                                *m_callbackClient;
    camera2_device_t                    *m_halDevice;
	camera_req_info                      m_staticReqInfo;
	bool                               m_reqIsProcess;
	camera_metadata_t                   *m_halRefreshReq;
    static gralloc_module_t const*      m_grallocHal;
	int32_t						   mPreviewHeapArray_phy[kPreviewBufferCount+kPreviewRotBufferCount+1];
	int32_t						   mPreviewHeapArray_vir[kPreviewBufferCount+kPreviewRotBufferCount+1];
    int             				    m_CameraId;
    List<camera_metadata_t *>           m_ReqQueue;
	Mutex                               m_requestMutex;
	Mutex                           mStateLock;
	Mutex                           mAFfModeTriggerLock;
	Condition                       mStateWait;
    volatile camera_state           mCameraState;
};

}; // namespace android

#endif
