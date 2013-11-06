/*
* hardware/sprd/hsdroid/libcamera/sprdcamerahardwareinterface.cpp
 * Dcam HAL based on sc8800g2
 *
 * Copyright (C) 2011 Spreadtrum
 *
 * Author: Xiaozhe wang <xiaozhe.wang@spreadtrum.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
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
#include "SprdCameraHardwareConfig.h"
#include "SprdCameraHardwareInterface.h"
//#include "SprdCameraHardwareStub.h"

#ifdef CONFIG_CAMERA_ISP
extern "C" {
#include "isp_video.h"
}
#endif

//#define USE_ION_MEM		1
#define CMCC_POWER_OPT          1

#define LOGV       ALOGV
#define LOGE       ALOGE
#define LOGI       ALOGI
#define LOGW       ALOGW
#define LOGD       ALOGD



#define ARRAY_SIZE(x) (sizeof(x) / sizeof(*x))
#define PRINT_TIME 0
#define METADATA_SIZE 12 // (4 * 3)

#define SET_PARM(x,y) do {                                             \
        ALOGV("initCameraParameters: set parm: %s, %d", #x, y);         \
        camera_set_parm (x, y, NULL, NULL);                       \
    } while(0)

extern "C" {

int32_t g_camera_id = -1;
uint32_t g_sprd_zoom_levle = 0;

static inline void print_time()
{
#if PRINT_TIME
    struct timeval time;
    gettimeofday(&time, NULL);
    LOGV("time: %lld us.", time.tv_sec * 1000000LL + time.tv_usec);
#endif
}

typedef struct {
    int width;
    int height;
} preview_size_type;

// These sizes have to be a multiple of 16 in each dimension
static preview_size_type preview_sizes[] = {
    { 640, 480 },
    { 480, 320 }, // HVGA
    { 432, 320 }, // 1.35-to-1, for photos. (Rounded up from 1.3333 to 1)
    { 352, 288 }, // CIF
    { 320, 240 }, // QVGA
    { 240, 160 }, // SQVGA
    { 176, 144 }, // QCIF
};
#define PREVIEW_SIZE_COUNT (sizeof(preview_sizes)/sizeof(preview_size_type))

// default preview size is QVGA
#define DEFAULT_PREVIEW_SETTING 0

#define LIKELY( exp )       (__builtin_expect( (exp) != 0, true  ))
#define UNLIKELY( exp )     (__builtin_expect( (exp) != 0, false ))
}


struct buf_addrs {
    uint32_t type;  // make sure that this is 4 byte.
    unsigned int addr_y;
    unsigned int addr_cbcr;
    unsigned int buf_index;
    unsigned int reserved;
};

namespace android {
    static Mutex singleton_lock;
   // static Mutex rex_init_lock;
    //static Condition rex_init_wait;

    static uint8_t* malloc_preview(uint32_t, uint32_t *, uint32_t);
    static uint8_t* malloc_raw(uint32_t, uint32_t *, uint32_t);
    static int free_preview(uint32_t *, uint32_t , uint32_t);
    static int free_raw(uint32_t *, uint32_t , uint32_t);
    static int reassoc(qdsp_module_type module);
    //static void cb_rex_signal_ready(void);


gralloc_module_t const* SprdCameraHardware::mGrallocHal;


#ifdef CMCC_POWER_OPT
#include<fcntl.h>
static int flag_lower_emc_freq = 0;
static void lower_emc_freq(char flag)
{
	int fp_emc_freq = -1;
	fp_emc_freq = open("/sys/emc/emc_freq", O_WRONLY);
	if (fp_emc_freq > 0)
	{
		write(fp_emc_freq, &flag, sizeof(flag));
		close(fp_emc_freq);
		LOGV("lower_emc_freq: %c \n", flag);
	} else if (-1 != fp_emc_freq) {
		close(fp_emc_freq);
		LOGV("lower_emc_freq: direct close!");
	} else {
		LOGV("lower_emc_freq: fp_emc_freq open failed \n");
	}
}
#endif
    SprdCameraHardware::SprdCameraHardware(int cameraId)
        : mParameters(),
        mPreviewHeight(-1),
        mPreviewWidth(-1),
        mRawHeight(-1),
        mRawWidth(-1),
        mCameraState(QCS_INIT),
        mPreviewFrameSize(0),
        mRawSize(0),
        mPreviewCount(0),
        mNotify_cb(0),
        mData_cb(0),
        mData_cb_timestamp(0),
        mUser(0),
        mMsgEnabled(0),
        mPreviewHeap(NULL),
        mRawHeap(NULL),
        mMiscHeap(NULL),
        mFDHeap(NULL),
        mFDAddr(0),
        mPreviewStartFlag(0),
        mMetadataHeap(NULL),
        mIsStoreMetaData(false),
        mPreviewWindow(NULL),
        mSettingPreviewWindowState(PREVIEW_WINDOW_SET_IDLE)
    {
        LOGV("openCameraHardware: call createInstance. cameraId: %d.", cameraId);

        if (!mGrallocHal) {
                int ret = hw_get_module(GRALLOC_HARDWARE_MODULE_ID, (const hw_module_t **)&mGrallocHal);
                if (ret)
                        LOGE("ERR(%s):Fail on loading gralloc HAL", __func__);
        }

    	char propBuf_atv[PROPERTY_VALUE_MAX];
		property_get("sys.camera.atv", propBuf_atv, "0");
        LOGV("openCameraHardware: call createInstance. cameraId: %d.", cameraId);
		if(0 == strcmp(propBuf_atv, "1")){
			g_camera_id = 5; //for ATV
		}
		else{
			g_camera_id = cameraId;
		}

        if(2 == g_camera_id){
                //SprdCameraHardwareStub::createInstance();

        }
        else{
                //SprdCameraHardware::createInstance();
                //createInstance();
                initDefaultParameters();
        }
    }

    status_t SprdCameraHardware::initDefaultParameters()
    {
        LOGV("initDefaultParameters E");
        CameraParameters p;

        preview_size_type* ps = &preview_sizes[DEFAULT_PREVIEW_SETTING];
        p.setPreviewSize(ps->width, ps->height);
        p.setPreviewFrameRate(15);
        p.setPreviewFormat("yuv420sp");
        p.setPictureFormat("jpeg");
        p.set("jpeg-quality", "100"); // maximum quality
        p.set("jpeg-thumbnail-width", "320");
        p.set("jpeg-thumbnail-height", "240");
        p.set("jpeg-thumbnail-quality", "80");
        p.set("focus-mode", "auto");

        if(1 == g_camera_id) //for the front camera
        {
                uint32_t count, i;
                count = ARRAY_SIZE(sprd_front_camera_hardware_config);
                LOGV("front camera parameters: set count : %d", count);
                for(i = 0; i < count; i++)
                {
                        LOGV("front camera parameters: set %d: key: %s, value: %s.", i, sprd_front_camera_hardware_config[i].key, sprd_front_camera_hardware_config[i].value);
                        p.set(sprd_front_camera_hardware_config[i].key, sprd_front_camera_hardware_config[i].value);
                }
        }
        else //for the back camera
        {
                uint32_t count, i;
                count = ARRAY_SIZE(sprd_back_camera_hardware_config);
                LOGV("back camera parameters: set count : %d", count);
                for(i = 0; i < count; i++)
                {
                        LOGV("back camera parameters: set %d: key: %s, value: %s.", i, sprd_back_camera_hardware_config[i].key, sprd_back_camera_hardware_config[i].value);
                        p.set(sprd_back_camera_hardware_config[i].key, sprd_back_camera_hardware_config[i].value);
                }
        }
        if (setParameters(p) != NO_ERROR) {
            LOGE("Failed to set default parameters?!");
	     return UNKNOWN_ERROR;
        }
        LOGV("initDefaultParameters X.");
        return NO_ERROR;
    }

#define ROUND_TO_PAGE(x)  (((x)+0xfff)&~0xfff)

    // Called with mStateLock held!
bool SprdCameraHardware::startCameraIfNecessary()
{
        if (mCameraState == QCS_INIT) {
                LOGV("waiting for camera_init to initialize.startCameraIfNecessary");
                if(CAMERA_SUCCESS != camera_init(g_camera_id)){
                        mCameraState = QCS_INIT;
                        LOGE("CameraIfNecessary: fail to camera_init().");
                        return false;
                }

                //LOGV("starting REX emulation");
                LOGV("waiting for camera_start.g_camera_id: %d.", g_camera_id);
                // NOTE: camera_start() takes (height, width), not (width, height).
                if(CAMERA_SUCCESS != camera_start(camera_cb, this, mPreviewHeight, mPreviewWidth)){
                        mCameraState = QCS_ERROR;
                        LOGE("CameraIfNecessary: fail to camera_start().");
                        return false;
                }
                LOGV("OK to camera_start.");
                while(mCameraState != QCS_IDLE && mCameraState != QCS_ERROR) {
                        LOGV("init camera: waiting for QCS_IDLE");
                        mStateWait.wait(mStateLock);
                        LOGV("init camera: woke up");
                }
                LOGV("init camera: initializing parameters");
        }
        else LOGV("camera hardware has been started already");
        return true;
}

void SprdCameraHardware::enableMsgType(int32_t msgType)
{
        LOGV("mLock:enableMsgType S .\n");
        Mutex::Autolock lock(mLock);
        mMsgEnabled |= msgType;
        if (CAMERA_MSG_VIDEO_FRAME == msgType) {
                mRecordingRunState = RECORDING_STARTING;  //add for start record lock issue
        }
        LOGV("mLock:enableMsgType E .\n");
}

void SprdCameraHardware::disableMsgType(int32_t msgType)
{
        LOGV("'mLock:disableMsgType S.\n");
  //      Mutex::Autolock lock(mLock);
        mMsgEnabled &= ~msgType;
        LOGV("'mLock:disableMsgType E.\n");
}

bool SprdCameraHardware::msgTypeEnabled(int32_t msgType)
{
        LOGV("mLock:msgTypeEnabled S.\n");
        Mutex::Autolock lock(mLock);
        LOGV("mLock:msgTypeEnabled E.\n");
        return (mMsgEnabled & msgType);
}

status_t SprdCameraHardware::dump(int fd) const
{
        const size_t SIZE = 256;
        char buffer[SIZE];
        String8 result;
        const Vector<String16> args;

        // Dump internal primitives.
        snprintf(buffer, 255, "SprdCameraHardware::dump: state (%d)\n", mCameraState);
        result.append(buffer);
        snprintf(buffer, 255, "preview width(%d) x height (%d)\n", mPreviewWidth, mPreviewHeight);
        result.append(buffer);
        snprintf(buffer, 255, "raw width(%d) x height (%d)\n", mRawWidth, mRawHeight);
        result.append(buffer);
        snprintf(buffer, 255, "preview frame size(%d), raw size (%d), jpeg size (%d) and jpeg max size (%d)\n", mPreviewFrameSize, mRawSize, mJpegSize, mJpegMaxSize);
        result.append(buffer);
        write(fd, result.string(), result.size());
        mParameters.dump(fd, args);
        return NO_ERROR;
    }

sprd_camera_memory_t* SprdCameraHardware::GetPmem(const char *device_name, int buf_size, int num_bufs)
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
                if(memory->camera_memory->release){
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
        } else{
                LOGV("FreePmem: NULL");
        }
}

void SprdCameraHardware::FreeFdmem(void)
{
        if (mFDAddr) {
            camera_set_fd_mem(0,0,0);
            free((void*)mFDAddr);
            mFDAddr = 0;
        }
}
bool SprdCameraHardware::initPreview()
{
        uint32_t page_size, buffer_size;
        uint32_t preview_buff_cnt = kPreviewBufferCount;

        if(true != startCameraIfNecessary())
                return false;

        // Tell libqcamera what the preview and raw dimensions are.  We
        // call this method even if the preview dimensions have not changed,
        // because the picture ones may have.
        // NOTE: if this errors out, mCameraState != QCS_IDLE, which will be
        //       checked by the caller of this method.
        setCameraDimensions();
        LOGV("initPreview: preview size=%dx%d", mPreviewWidth, mPreviewHeight);

	camerea_set_preview_format(mPreviewFormat);
        mPreviewFrameSize = mPreviewWidth * mPreviewHeight * 3 / 2;
        buffer_size = camera_get_size_align_page(mPreviewFrameSize);

        if(camera_get_rot_set())
        {
                /* allocate 1 more buffer for rotation */
                preview_buff_cnt += kPreviewRotBufferCount;
                LOGV("initPreview: rotation, increase buffer: %d \n", preview_buff_cnt);
        }
        mPreviewHeap = GetPmem("/dev/pmem_adsp", buffer_size, preview_buff_cnt);
        if(NULL == mPreviewHeap) {
                mNotify_cb(CAMERA_MSG_ERROR, NO_MEMORY, 0, mUser);
                LOGE("initPreview: CAMERA_MSG_NO_MEMORY.");
                return false;
        }

        if(NULL == mPreviewHeap->handle){
                LOGE("Fail to GetPmem mPreviewHeap. buffer_size: 0x%x.", buffer_size);
                return false;
        }
        if(mPreviewHeap->phys_addr & 0xFF){
                LOGE("error: the mPreviewHeap is not 256 bytes aligned.");
                return false;
        }

	if (camera_set_preview_mem((uint32_t)mPreviewHeap->phys_addr,
				(uint32_t)mPreviewHeap->data,
				(uint32_t)mPreviewHeap->phys_size))
		return false;

        return true;
}

void SprdCameraHardware::deinitPreview()
{
        FreeFdmem();
        FreePmem(mPreviewHeap);
        mPreviewHeap = NULL;
        FreePmem(mFDHeap);
        mFDHeap = NULL;
}

    // Called with mStateLock held!
bool SprdCameraHardware::initRaw(bool initJpegHeap)
{
        uint32_t page_size, buffer_size;
        uint32_t sensor_max_width,sensor_max_height;
	uint32_t local_width, local_height;
	uint32_t mem_sie0, mem_size1;

        LOGV("initRaw E");
        if(true != startCameraIfNecessary())
                return false;

	if (camera_capture_max_img_size(&local_width, &local_height))
		return false;

	if (camera_capture_get_buffer_size(g_camera_id,local_width, local_height, &mem_sie0, &mem_size1))
		return false;

        mRawSize = mem_sie0;
        mJpegMaxSize = mRawSize;
        buffer_size = mJpegMaxSize;
        mJpegHeap = NULL;

        LOGV("initRaw: initializing mRawHeap.");

        buffer_size = camera_get_size_align_page(buffer_size);
        LOGV("CAMERA HARD:initRaw:mRawHeap align size = %d . count %d ",buffer_size, kRawBufferCount);

        mRawHeap = GetPmem("/dev/pmem_adsp", buffer_size, kRawBufferCount);
        if(NULL == mRawHeap)
            return false;
        if(NULL == mRawHeap->handle){
                LOGE("Fail to GetPmem mRawHeap. buffer_size: 0x%x.", buffer_size);
                return false;
        }
        if(0 != mem_size1) {
            buffer_size = camera_get_size_align_page(mem_size1);
            mMiscHeap = GetPmem("/dev/pmem_adsp", buffer_size, kRawBufferCount);
            if(NULL == mMiscHeap) {
                FreePmem(mRawHeap);
                mRawHeap = NULL;
                return false;
            }
            if(NULL == mMiscHeap->handle){
                    LOGE("Fail to GetPmem mRawHeap. buffer_size: 0x%x.", buffer_size);
                    FreePmem(mRawHeap);
                    mRawHeap = NULL;
                    return false;
            }
       }
        if(0 != mem_size1) {
            if (camera_set_capture_mem(0,
            (uint32_t)mRawHeap->phys_addr,
            (uint32_t)mRawHeap->data,
            (uint32_t)mRawHeap->phys_size,
            (uint32_t)mMiscHeap->phys_addr,
            (uint32_t)mMiscHeap->data,
            (uint32_t)mMiscHeap->phys_size))
                return false;
        }else {
            if (camera_set_capture_mem(0,
            (uint32_t)mRawHeap->phys_addr,
            (uint32_t)mRawHeap->data,
            (uint32_t)mRawHeap->phys_size,
            0,
            0,
            0))
                return false;
        }

        if (initJpegHeap) {
                LOGV("initRaw: initializing mJpegHeap.");
                buffer_size = mJpegMaxSize;
                buffer_size = camera_get_size_align_page(buffer_size);
                mJpegHeap =   new AshmemPool(buffer_size,
                	                               kJpegBufferCount,
                	                               0,
                	                               0,
                	                               "jpeg");
                if (!mJpegHeap->initialized()) {
                        LOGE("initRaw X failed: error initializing mJpegHeap.");
                        mJpegHeap = NULL;
                        FreePmem(mRawHeap);
                        mRawHeap = NULL;
                        FreePmem(mMiscHeap);
                        mMiscHeap = NULL;
                        return false;
                }
                LOGV("initJpeg success");
        }
        LOGV("initRaw X success");
        return true;
}

void SprdCameraHardware::release()
{
        LOGV("release E");
        LOGV("mLock:release S .\n");
        Mutex::Autolock l(&mLock);

        // Either preview was ongoing, or we are in the middle or taking a
        // picture.  It's the caller's responsibility to make sure the camera
        // is in the idle or init state before destroying this object.

        if (mCameraState != QCS_IDLE && mCameraState != QCS_INIT) {
                LOGE("Serious error: the camera state is %s, "
                        "not QCS_IDLE or QCS_INIT!",
                        getCameraStateStr(mCameraState));
        }
        mStateLock.lock();
        if (mCameraState != QCS_INIT) {
            // When libqcamera detects an error, it calls camera_cb from the
            // call to camera_stop, which would cause a deadlock if we
            // held the mStateLock.  For this reason, we have an intermediate
            // state QCS_INTERNAL_STOPPING, which we use to check to see if the
            // camera_cb was called inline.
                mCameraState = QCS_INTERNAL_STOPPING;
                mStateLock.unlock();

                LOGV("stopping camera.");
                if(CAMERA_SUCCESS != camera_stop(stop_camera_cb, this)){
                        mCameraState = QCS_ERROR;
                        mStateLock.unlock();
			if(mIsStoreMetaData) {
				mMetadataHeap = NULL;
			}
                        LOGE("release: fail to camera_stop().");
                        LOGV("mLock:release E.\n");
                        return;
                }

                mStateLock.lock();
                if (mCameraState == QCS_INTERNAL_STOPPING) {
                        while (mCameraState != QCS_INIT &&
                                mCameraState != QCS_ERROR) {
                                LOGV("stopping camera: waiting for QCS_INIT");
                                mStateWait.wait(mStateLock);
                        }
                }

                if(QCS_ERROR != mCameraState)
                        mCameraState = QCS_INIT;
        }
	if(mIsStoreMetaData) {
		mMetadataHeap = NULL;
	}
        mStateLock.unlock();
        LOGV("release X");
        LOGV("mLock:release E.\n");
    }

SprdCameraHardware::~SprdCameraHardware()
{
        LOGV("~SprdCameraHardware E");
        Mutex::Autolock singletonLock(&singleton_lock);
        //singleton.clear();
        LOGV("~SprdCameraHardware X");
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

status_t SprdCameraHardware::startPreviewInternal()
{
        LOGV("startPreview E");

        if (mMsgEnabled & CAMERA_MSG_VIDEO_FRAME) {
#ifdef CMCC_POWER_OPT
			flag_lower_emc_freq = 1;
			lower_emc_freq('2');
#endif
		SET_PARM(CAMERA_PARM_PREVIEW_MODE, CAMERA_PREVIEW_MODE_MOVIE);
        } else {
#ifdef CMCC_POWER_OPT
		flag_lower_emc_freq = 1;
		lower_emc_freq('1');
#endif
		SET_PARM(CAMERA_PARM_PREVIEW_MODE, CAMERA_PREVIEW_MODE_SNAPSHOT);
        }

        if (mCameraState == QCS_PREVIEW_IN_PROGRESS) {
                LOGE("startPreview is already in progress, doing nothing.");
                // We might want to change the callback functions while preview is
                // streaming, for example to enable or disable recording.
                //setCallbackFuns(pcb, puser, rcb, ruser);
                LOGV("mLock:startPreview E.\n");
                return NO_ERROR;
        }

        // We check for these two states explicitly because it is possible
        // for startPreview() to be called in response to a raw or JPEG
        // callback, but before we've updated the state from QCS_WAITING_RAW
        // or QCS_WAITING_JPEG to QCS_IDLE.  This is because in camera_cb(),
        // we update the state *after* we've made the callback.  See that
        // function for an explanation.

        if (mCameraState == QCS_WAITING_RAW ||
                mCameraState == QCS_WAITING_JPEG) {
                while (mCameraState != QCS_IDLE &&
                        mCameraState != QCS_ERROR) {
                        LOGV("waiting for QCS_IDLE");
                        mStateWait.wait(mStateLock);
                }
        }

        if (mCameraState != QCS_IDLE) {
                LOGE("startPreview X Camera state is %s, expecting QCS_IDLE!",
                getCameraStateStr(mCameraState));
                LOGV("mLock:startPreview E.\n");
                return INVALID_OPERATION;
        }

        if (!initPreview()) {
                LOGE("startPreview X initPreview failed.  Not starting preview.");
                LOGV("mLock:startPreview E.\n");
                return UNKNOWN_ERROR;
        }

        // setCallbackFuns(pcb, puser, rcb, ruser);
        // hack to prevent first preview frame from being black
        mPreviewCount = 0;
        mCameraState = QCS_INTERNAL_PREVIEW_REQUESTED;
        camera_ret_code_type qret = camera_start_preview(camera_cb, this);
        LOGV("camera_start_preview X");
        if (qret == CAMERA_SUCCESS) {
                while(mCameraState != QCS_PREVIEW_IN_PROGRESS &&
                        mCameraState != QCS_ERROR) {
                        LOGV("waiting for QCS_PREVIEW_IN_PROGRESS");
                        mStateWait.wait(mStateLock);
                }
        }
        else {
                LOGE("startPreview failed: sensor error.");
                mCameraState = QCS_ERROR;
		FreePmem(mPreviewHeap);
		mPreviewHeap = NULL;
        }
        LOGV("startPreview X,mRecordingMode=%d.",mRecordingMode);
        LOGV("mLock:startPreview E.\n");
        return mCameraState == QCS_PREVIEW_IN_PROGRESS ? NO_ERROR : UNKNOWN_ERROR;
}

void SprdCameraHardware::stopPreviewInternal()
{
       LOGV("stopPreviewInternal Enter");

#ifdef CMCC_POWER_OPT
	if (flag_lower_emc_freq) {
		lower_emc_freq('0');
		flag_lower_emc_freq = 0;
	}
#endif

        if (mCameraState != QCS_PREVIEW_IN_PROGRESS) {
                LOGE("Preview not in progress!");
                FreeFdmem();
                FreePmem(mPreviewHeap);
                mPreviewHeap = NULL;
                FreePmem(mFDHeap);
                mFDHeap = NULL;
                return;
        }

        /*if (mAutoFocusCallback != NULL) {
            // WARNING: clear mAutoFocusCallback BEFORE you call
            // camera_stop_focus.  The CAMERA_EXIT_CB_ABORT is (erroneously)
            // delivered inline camera_stop_focus(), and we cannot acquire
            // mStateLock, because that would cause a deadlock.  In any case,
            // CAMERA_EXIT_CB_ABORT is delivered only when we call
            // camera_stop_focus.
            mAutoFocusCallback = NULL;
            camera_stop_focus();
        }*/
        //camera_stop_focus();
        mCameraState = QCS_INTERNAL_PREVIEW_STOPPING;

        if(CAMERA_SUCCESS != camera_stop_preview()){
                mCameraState = QCS_ERROR;
                FreeFdmem();
                FreePmem(mPreviewHeap);
                mPreviewHeap = NULL;
                FreePmem(mFDHeap);
                mFDHeap = NULL;
                LOGE("stopPreviewInternal: fail to camera_stop_preview().");
                return;
        }
        while (mCameraState != QCS_IDLE &&
        mCameraState != QCS_ERROR)  {
                LOGV("waiting for QCS_IDLE");
                mStateWait.wait(mStateLock);
        }

        LOGV("stopPreviewInternal: Freeing preview heap.");
        FreeFdmem();
        FreePmem(mPreviewHeap);
        mPreviewHeap = NULL;
        FreePmem(mFDHeap);
        mFDHeap = NULL;
        LOGV("stopPreviewInternal: X Preview has stopped.");
}

status_t SprdCameraHardware::startPreview()
{
        LOGV("mLock:startPreview S.\n");
        Mutex::Autolock l(&mLock);
        Mutex::Autolock stateLock(&mStateLock);
       if(mPreviewStartFlag == 0) {
            LOGV("don't need to start preview.");
            return NO_ERROR;
        }
        if(mMsgEnabled & CAMERA_MSG_VIDEO_FRAME)
                mRecordingMode = 1;
        else
                mRecordingMode = 0;
        return startPreviewInternal();
}

void SprdCameraHardware::stopPreview()
{
        LOGV("stopPreview: E");
        LOGV("mLock:stopPreview S.\n");
        Mutex::Autolock l(&mLock);
        Mutex::Autolock statelock(&mStateLock);
        mRecordingMode = 0;
        stopPreviewInternal();

        LOGV("stopPreview: X");
        LOGV("mLock:stopPreview E.\n");
}

bool SprdCameraHardware::previewEnabled()
{
        bool ret = 0;
        LOGV("mLock:previewEnabled S.\n");
        Mutex::Autolock l(&mLock);
        LOGV("mLock:previewEnabled E.\n");
        if(0 == mPreviewStartFlag) {
            return 1;
        }  else if (2 == mPreviewStartFlag) {
            return 0;
        } else {
            return mCameraState == QCS_PREVIEW_IN_PROGRESS;
        }
}

status_t SprdCameraHardware::startRecording()
{
        status_t ret = NO_ERROR;

        LOGV("mLock:startRecording S.\n");
        Mutex::Autolock l(&mLock);
        Mutex::Autolock stateLock(&mStateLock);
        if (mCameraState == QCS_PREVIEW_IN_PROGRESS) {
                LOGV("wxz call stopPreviewInternal in startRecording().");
                mCameraState = QCS_INTERNAL_PREVIEW_STOPPING;
                if(CAMERA_SUCCESS != camera_stop_preview()){
                        mCameraState = QCS_ERROR;
                        FreeFdmem();
			FreePmem(mPreviewHeap);
                        mPreviewHeap = NULL;
			FreePmem(mFDHeap);
                        mFDHeap = NULL;
                        LOGE("startRecording: fail to camera_stop_preview().");
                        return INVALID_OPERATION;
                }
                while (mCameraState != QCS_IDLE &&
                mCameraState != QCS_ERROR)  {
                        LOGV("waiting for QCS_IDLE");
                        mStateWait.wait(mStateLock);
                }

	        LOGV("startRecording: Freeing preview heap.");
            FreeFdmem();
		FreePmem(mPreviewHeap);
	        mPreviewHeap = NULL;
            FreePmem(mFDHeap);
            mFDHeap = NULL;
        }
        mRecordingMode = 1;
        LOGV("startRecording,mRecordingMode=%d.",mRecordingMode);
        ret = startPreviewInternal();
        mRecordingRunState = RECORDING_RUNING;
        return ret;
    }

void SprdCameraHardware::stopRecording()
{
        LOGV("stopRecording: E");
        LOGV("mLock:stopRecording S.\n");
        Mutex::Autolock l(&mLock);
        Mutex::Autolock statelock(&mStateLock);
        stopPreviewInternal();
        mRecordingRunState = RECORDING_STOP;
        LOGV("stopRecording: X");
        LOGV("mLock:stopRecording E.\n");
}

bool SprdCameraHardware::recordingEnabled()
{
        LOGV("mLock:recordingEnabled S.\n");
        Mutex::Autolock l(&mLock);
        Mutex::Autolock stateLock(&mStateLock);
        LOGV("mLock:recordingEnabled E.\n");
        return mCameraState == QCS_PREVIEW_IN_PROGRESS && (mMsgEnabled & CAMERA_MSG_VIDEO_FRAME);
}
void SprdCameraHardware::releaseRecordingFrame(const void *opaque)
{
        //LOGV("releaseRecordingFrame E. ");
        uint8_t *addr = (uint8_t *)opaque;
        uint32_t index;

        if (mCameraState != QCS_PREVIEW_IN_PROGRESS) {
                LOGE("releaseRecordingFrame: Preview not in progress!");
                return;
        }

	if(mIsStoreMetaData) {
		index = (addr - (uint8_t *)mMetadataHeap->data) / (METADATA_SIZE);
	}
	else {
        	index = (addr - (uint8_t *)mPreviewHeap->data) / (mPreviewWidth * mPreviewHeight * 3 / 2);
	}

	if(index > kPreviewBufferCount){
		LOGV("releaseRecordingFrame error: index: %d, addr: %x, data: %x, w=%d, h=%d \n",
			    index, (uint32_t)addr, (uint32_t)mPreviewHeap->data, mPreviewWidth, mPreviewHeight);
	}
        //LOGV("releaseRecordingFrame: index: %d, offset: %x, size: %x.", index,offset,size);
        camera_release_frame(index);
        LOGV("releaseRecordingFrame: index: %d", index);
        //LOGV("releaseRecordingFrame X. ");
}

status_t SprdCameraHardware::autoFocus()
{
        LOGV("Starting auto focus.");
        LOGV("mLock:autoFocus S.\n");
        Mutex::Autolock l(&mLock);
        Mutex::Autolock lock(&mStateLock);

        if (mCameraState != QCS_PREVIEW_IN_PROGRESS) {
                LOGE("Invalid camera state %s: expecting QCS_PREVIEW_IN_PROGRESS,"
                " cannot start autofocus!",
                getCameraStateStr(mCameraState));
                LOGV("mLock:autoFocus E.\n");
                return INVALID_OPERATION;
        }

        if(0 != camera_start_autofocus(CAMERA_AUTO_FOCUS, camera_cb, this))
        {
                LOGE("auto foucs fail.");
                //return INVALID_OPERATION;
        }
        LOGV("mLock:autoFocus E.\n");
        return NO_ERROR;
    }
status_t SprdCameraHardware::cancelAutoFocus()
{
        return camera_cancel_autofocus(this);
}
uint32_t get_physical_address(sprd_camera_memory_t *mMem,uint32_t *size)
{
        *size = mMem->phys_size;
        return mMem->phys_addr;
}

status_t SprdCameraHardware::setTakePictureSize(uint32_t width, uint32_t height)
{
	mRawWidth=width;
	mRawHeight=height;

	 return NO_ERROR;
}

status_t SprdCameraHardware::takePicture()
{
        takepicture_mode  mode = CAMERA_NORMAL_MODE;
        /*LOGV("takePicture: E raw_cb = %p, jpeg_cb = %p",
             raw_cb, jpeg_cb);*/
        print_time();

        Mutex::Autolock l(&mLock);
        Mutex::Autolock stateLock(&mStateLock);

        Sprd_camera_state last_state = mCameraState;
        if (mCameraState == QCS_PREVIEW_IN_PROGRESS) {
                LOGV("call stopPreviewInternal in takePicture().");
                stopPreviewInternal();
        }
        LOGV("ok to stopPreviewInternal in takePicture.");

        // We check for these two states explicitly because it is possible
        // for takePicture() to be called in response to a raw or JPEG
        // callback, but before we've updated the state from QCS_WAITING_RAW
        // or QCS_WAITING_JPEG to QCS_IDLE.  This is because in camera_cb(),
        // we update the state *after* we've made the callback.  See that
        // function for an explanation why.

        if (mCameraState == QCS_WAITING_RAW ||
            mCameraState == QCS_WAITING_JPEG) {
            while (mCameraState != QCS_IDLE &&
                   mCameraState != QCS_ERROR) {
                LOGV("waiting for QCS_IDLE");
                mStateWait.wait(mStateLock);
            }
        }

        if (mCameraState != QCS_IDLE) {
            LOGE("takePicture: %sunexpected state %d, expecting QCS_IDLE",
                 (last_state == QCS_PREVIEW_IN_PROGRESS ?
                  "(stop preview) " : ""),
                 mCameraState);
            // If we had to stop preview in order to take a picture, and
            // we failed to transition to a QCS_IDLE state, that's because
            // of an internal error.
                   LOGV("mLock:takePictureE.\n");
            return last_state == QCS_PREVIEW_IN_PROGRESS ?
                UNKNOWN_ERROR :
                INVALID_OPERATION;
        }
	LOGV("start to initRaw in takePicture.");

        camera_set_dimensions(mRawWidth,
                                   mRawHeight,
                                   mPreviewWidth,
                                   mPreviewHeight,
                                   NULL,
                                   NULL);

        interpoation_flag = 0;
        if (!initRaw(mData_cb != NULL)) {
                LOGE("initRaw failed.  Not taking picture.");
                LOGV("mLock:takePictureE.\n");
                return UNKNOWN_ERROR;
        }

        if (mCameraState != QCS_IDLE) {
                LOGE("takePicture: (init raw) "
                "unexpected state %d, expecting QCS_IDLE",
                mCameraState);
                LOGV("mLock:takePictureE.\n");
                // If we had to stop preview in order to take a picture, and
                // we failed to transition to a QCS_IDLE state, that's because
                // of an internal error.
                FreePmem(mRawHeap);
                mRawHeap = NULL;
                FreePmem(mMiscHeap);
                mMiscHeap = NULL;
                //           mJpegHeap = NULL;
                return last_state == QCS_PREVIEW_IN_PROGRESS ?
                UNKNOWN_ERROR :
                INVALID_OPERATION;
        }

        mCameraState = QCS_INTERNAL_RAW_REQUESTED;

        LOGV("INTERPOLATION::takePicture:mRawWidth=%d,g_sprd_zoom_levle=%d",mRawWidth,g_sprd_zoom_levle);

        if(1 == mParameters.getInt("hdr")) {
            mode = CAMERA_HDR_MODE;
        }

		if(1 != mParameters.getInt("capture-mode")) {
			mode = CAMERA_CONTINUE_SHOT_MODE;
		}
/*        mode = CAMERA_HDR_MODE;*/
        LOGV("cap mode %d.\n",mode);
        if(CAMERA_SUCCESS != camera_take_picture(camera_cb, this,mode))
        {
                mCameraState = QCS_ERROR;
                LOGE("takePicture: fail to camera_take_picture.");
                LOGV("mLock:takePictureE.\n");
                FreePmem(mRawHeap);
                mRawHeap = NULL;
                FreePmem(mMiscHeap);
                mMiscHeap = NULL;
                //          mJpegHeap = NULL;
                return UNKNOWN_ERROR;
        }

        // It's possible for the YUV callback as well as the JPEG callbacks
        // to be invoked before we even make it here, so we check for all
        // possible result states from takePicture.

        while (mCameraState != QCS_WAITING_RAW &&
               mCameraState != QCS_WAITING_JPEG &&
               mCameraState != QCS_IDLE &&
               mCameraState != QCS_ERROR)  {
            LOGV("takePicture: waiting for QCS_WAITING_RAW or QCS_WAITING_JPEG");
            mStateWait.wait(mStateLock);
            LOGV("takePicture: woke up, state is %s",
                 getCameraStateStr(mCameraState));
        }
        LOGV("takePicture: X");
        LOGV("mLock:takePictureE.\n");
        print_time();
        return mCameraState != QCS_ERROR ?
            NO_ERROR : UNKNOWN_ERROR;
    }

status_t SprdCameraHardware::cancelPicture()
{
        LOGV("mLock:cancelPicture S.\n");
        Mutex::Autolock l(&mLock);
        Mutex::Autolock stateLock(&mStateLock);

        switch (mCameraState) {
        case QCS_INTERNAL_RAW_REQUESTED:
        case QCS_WAITING_RAW:
        case QCS_WAITING_JPEG:
            LOGD("camera state is %s, stopping picture.",
                 getCameraStateStr(mCameraState));

            mCameraState = QCS_INTERNAL_CAPTURE_STOPPING;
            camera_stop_capture();
	    FreeCameraMem();
            while (mCameraState != QCS_IDLE &&
                   mCameraState != QCS_ERROR)  {
                LOGV("cancelPicture: waiting for QCS_IDLE");
                mStateWait.wait(mStateLock);
            }
            break;
        default:
            LOGV("not taking a picture (state %s)",
                 getCameraStateStr(mCameraState));
        }

        LOGV("cancelPicture: X");
		     LOGV("mLock:cancelPicture E.\n");
        return NO_ERROR;
    }

status_t SprdCameraHardware::setParameters(const CameraParameters& params)
{
        LOGV("setParameters: E params = %p", &params);
        LOGV("mLock:setParameters S.\n");
        Mutex::Autolock l(&mLock);
        Mutex::Autolock lock(&mStateLock);
        if(strcmp(params.get("null-window") , "1") == 0)
        {
                ALOGV("mPreviewLock::null window set preview window state");
                Mutex::Autolock previewLock(&mPreviewLock);
                mSettingPreviewWindowState = PREVIEW_WINDOW_SET_IDLE;
        }

        // FIXME: verify params
        // yuv422sp is here only for legacy reason. Unfortunately, we release
        // the code with yuv422sp as the default and enforced setting. The
        // correct setting is yuv420sp.
        if(strcmp(params.getPreviewFormat(), "yuv422sp")== 0)
        {
                mPreviewFormat = 0;
        }
        else if(strcmp(params.getPreviewFormat(), "yuv420sp") == 0)
        {
                mPreviewFormat = 1;
        }
        else if(strcmp(params.getPreviewFormat(), "rgb565") == 0)
        {
                mPreviewFormat = 2;
        }
		else if(strcmp(params.getPreviewFormat(), "yuv420p") == 0)
		{
			mPreviewFormat = 3;
		}
        else
        {
                LOGE("Onlyyuv422sp/yuv420sp/rgb565 preview is supported.\n");
                return INVALID_OPERATION;
        }
        mPreviewFormat = 1;
        if(strcmp(params.getPictureFormat(), "yuv422sp")== 0)
        {
                mPictureFormat = 0;
        }
        else if(strcmp(params.getPictureFormat(), "yuv420sp")== 0)
        {
                mPictureFormat = 1;
        }
        else if(strcmp(params.getPictureFormat(), "rgb565")== 0)
        {
                mPictureFormat = 2;
        }
        else if(strcmp(params.getPictureFormat(), "jpeg")== 0)
        {
                mPictureFormat = 3;
        }
        else
        {
                LOGE("Onlyyuv422sp/yuv420sp/rgb565/jpeg  picture format is supported.\n");
                return INVALID_OPERATION;
        }
        mPictureFormat = 1;
        LOGV("setParameters: mPreviewFormat=%d,mPictureFormat=%d.\n",mPreviewFormat,mPictureFormat);

        // FIXME: will this make a deep copy/do the right thing? String8 i
        // should handle it
        mParameters = params;
         //now we matain null-window state in hal. to ensure the correct state
        if(strcmp(mParameters.get("null-window") , "1") == 0)
        {
                mParameters.set("null-window" , "0");
        }

        // libqcamera only supports certain size/aspect ratios
        // find closest match that doesn't exceed app's request
        int width, height;
	int rawWidth, rawHeight;
        params.getPreviewSize(&width, &height);
        LOGV("requested size %d x %d", width, height);
	params.getPictureSize(&rawWidth, &rawHeight);
        ALOGV("requested size %d x %d", width, height);
        mPreviewWidth = (width + 1) & ~1;
        mPreviewHeight = (height + 1) & ~1;
        mRawHeight = (rawHeight + 1) & ~1;
        mRawWidth = (rawWidth + 1) & ~1;
        LOGV("requested picture size %d x %d", mRawWidth, mRawHeight);
        LOGV("requested preview size %d x %d", mPreviewWidth, mPreviewHeight);

        if(CAMERA_ZOOM_MAX <= atoi(mParameters.get("zoom")) ){
                mParameters.set("zoom", mParameters.get("max-zoom"));
                return BAD_VALUE;
        }
        if (camera_set_change_size(mRawWidth,mRawHeight,mPreviewWidth,mPreviewHeight)) {
            mPreviewStartFlag = 2;
        }
        if(NO_ERROR != initCameraParameters()){
                return UNKNOWN_ERROR;
        }
        LOGV("setParameters: X mCameraState=%d", mCameraState);
        LOGV("mLock:setParameters E.\n");
         return NO_ERROR;
    }

CameraParameters SprdCameraHardware::getParameters()// const
{
        LOGV("getParameters: EX");
        return mParameters;
}

static CameraInfo sCameraInfo[] = {
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

static CameraInfo sCameraInfo3[] = {
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

void* SprdCameraHardware::get_redisplay_mem(uint32_t size, uint32_t count, uint32_t *phy_addr)
{
        uint32_t i;
        uint32_t real_size = 0;
        int buffer_size = camera_get_size_align_page(size);
        LOGV("INTERPOLATION:temp mem size=%d,align size=%d .",size,buffer_size);
        mReDisplayHeap = GetPmem("/dev/pmem_adsp", buffer_size, 1);
        if(NULL == mReDisplayHeap)
            return NULL;
        if(NULL == mReDisplayHeap->handle){
                LOGE("Fail to GetPmem mTempHWHeap. buffer_size: 0x%x.", buffer_size);
                return NULL;
        }

        {
        *phy_addr = (uint32_t)get_physical_address(mReDisplayHeap,&real_size);
        uint32_t vaddr = (uint32_t)mReDisplayHeap->data;

        LOGV("get_temp_mem_by_HW: MALLOC size: %d, num: %d --> %p",
        buffer_size, count, (void *)vaddr);
        return (void *)vaddr;
        }

        LOGV("get_temp_mem_by_HW: X NULL");
        return NULL;
    }

void SprdCameraHardware::FreeCameraMem(void)
{
        Mutex::Autolock cbLock(&mCallbackLock);
        FreePmem(mRawHeap);
        mRawHeap = NULL;
        FreePmem(mMiscHeap);
        mMiscHeap = NULL;
        //     mJpegHeap = NULL;
}

void SprdCameraHardware::getPictureFormat(int * format)
{
        *format = mPictureFormat;
}

void SprdCameraHardware::HandleErrorState(void)
{
        Mutex::Autolock cbLock(&mCallbackLock);
        if(mMsgEnabled & CAMERA_MSG_ERROR)
        {
                if(mData_cb != NULL) {
                        LOGE("HandleErrorState");
			mNotify_cb(CAMERA_MSG_ERROR, 0,0,mUser);
                }
        }
        LOGE("HandleErrorState:don't enable error msg!");
}

void SprdCameraHardware::receivePreviewFDFrame(camera_frame_type *frame)
{
    Mutex::Autolock cbLock(&mCallbackLock);
    LOGE("get mCallbackLock");

    ssize_t offset = frame->buf_id;
    camera_frame_metadata_t metadata;
    camera_face_t face_info[FACE_DETECT_NUM];
    uint32_t k = 0;

    /* if(1 == mParameters.getInt("smile-snap-mode")){*/

    if (mCameraState != QCS_PREVIEW_IN_PROGRESS) {
        LOGV("fd:non preview.");
        return;
    }

    if (NULL == mFDAddr) {
         LOGV("already stop fd.");
         return;
    }

    LOGV("receive face_num %d.",frame->face_num);
    metadata.number_of_faces = frame->face_num;
    if((0 != frame->face_num)&&(frame->face_num<=FACE_DETECT_NUM)) {
       for(k=0 ; k<frame->face_num ; k++) {
            face_info[k].id = k;
#if 1
            face_info[k].rect[0] = (frame->face_ptr->sx*2000/mPreviewWidth)-1000;
            face_info[k].rect[1] = (frame->face_ptr->sy*2000/mPreviewHeight)-1000;
            face_info[k].rect[2] = (frame->face_ptr->ex*2000/mPreviewWidth)-1000;
            face_info[k].rect[3] = (frame->face_ptr->ey*2000/mPreviewHeight)-1000;
            LOGV("smile level %d.\n",frame->face_ptr->smile_level);
#else
            face_info[k].rect[0] = frame->face_ptr->sx;
            face_info[k].rect[1] = frame->face_ptr->sy;
            face_info[k].rect[2] = frame->face_ptr->ex;
            face_info[k].rect[3] = frame->face_ptr->ey;
            LOGV("rect:%d,%d,%d,%d.\n",
                face_info[k].rect[0],face_info[k].rect[1],face_info[k].rect[2],face_info[k].rect[3]);

            if(frame->face_ptr->smile_level > FACE_SMILE_LIMIT) {
                face_info[k].score   = 100;
            } else {
                face_info[k].score   = 0;
            }
#endif
            face_info[k].score = frame->face_ptr->smile_level;
            frame->face_ptr++;
       }
    }
    metadata.faces = &face_info[0];
    if(mMsgEnabled&CAMERA_MSG_PREVIEW_METADATA) {
        LOGV("smile capture msg is enabled.");
        if(camera_get_rot_set()) {
            mData_cb(CAMERA_MSG_PREVIEW_METADATA,mPreviewHeap->camera_memory,(kPreviewBufferCount+offset),&metadata,mUser);
        } else {
            mData_cb(CAMERA_MSG_PREVIEW_METADATA,mPreviewHeap->camera_memory,offset,&metadata,mUser);
        }
    } else {
        LOGV("smile capture msg is disabled.");
    }
/*    mParameters.set("smile-snap-mode",0);*/
    LOGE("done !");
}

void SprdCameraHardware::receivePreviewFrame(camera_frame_type *frame)
{
	Mutex::Autolock cbLock(&mCallbackLock);
	LOGE("get mCallbackLock");

        ssize_t offset = frame->buf_id;
        camera_frame_metadata_t metadata;
        camera_face_t face_info[FACE_DETECT_NUM];
        uint32_t k = 0;
	if ((mCameraState != QCS_PREVIEW_IN_PROGRESS)
		|| (mSettingPreviewWindowState == PREVIEW_WINDOW_SETTING)
		|| (mRecordingRunState == RECORDING_STARTING)) {
		LOGE("directly return! mCameraState is %d mSettingPreviewWindowState is %d mRecordingRunState is %d ",
			mCameraState, mSettingPreviewWindowState, mRecordingRunState);
		if(CAMERA_SUCCESS != camera_release_frame(frame->buf_id)){
                        LOGE("receivePreviewFrame: fail to camera_release_frame().offset: %d.", frame->buf_id);
              }
             return;
       }


        // Ignore the first frame--there is a bug in the VFE pipeline and that
        // frame may be bad.
        if (++mPreviewCount == 1) {
                if(CAMERA_SUCCESS != camera_release_frame(frame->buf_id)){
                        LOGE("receivePreviewFrame: fail to camera_release_frame().offset: %d.", frame->buf_id);
                }
                return;
        }
   //     LOGV("receivePreviewFrame\n");
#if 1
{
Mutex::Autolock previewLock(&mPreviewLock);
if(mSettingPreviewWindowState == PREVIEW_WINDOW_SET_OK)
{
        int width, height, frame_size, offset_size;

        // mParameters.getPreviewSize(&width, &height);
        width  = frame->dx;/*mPreviewWidth;*/
        height = frame->dy;/*mPreviewHeight;*/
	LOGV("receivePreviewFrame: width=%d, height=%d \n",width, height);
  /*  LOGV("mPreviewWindow=0x%x,mGrallocHal=0x%x.",mPreviewWindow,mGrallocHal);*/

        if (mPreviewWindow && mGrallocHal) {
                buffer_handle_t *buf_handle;
                int stride;
                if (0 != mPreviewWindow->dequeue_buffer(mPreviewWindow, &buf_handle, &stride)) {
                        LOGE("Could not dequeue gralloc buffer!\n");
                        goto callbacks;
                }
                //LOGI("stride: %d, width: %d, height: %d, frame->buf_id: %d.", stride, width, height, frame->buf_id);

                void *vaddr;
                if (!mGrallocHal->lock(mGrallocHal,
                                *buf_handle,
                                GRALLOC_USAGE_SW_WRITE_OFTEN,
                                0, 0, width, height, &vaddr)) {
                        char *frame_addr = (char *)frame->buf_Virt_Addr;

                        if((NULL == vaddr) || (NULL == frame_addr)){
                                LOGE("Fail to get gralloc buffer.");
                                goto callbacks;
                        } else{
			//LOGI("OK to get gralloc buffer. vaddr: 0x%x, frame_addr: 0x%x, frame->buf_Virt_Addr: 0x%x.", (uint32_t)vaddr, (uint32_t)frame_addr, (uint32_t)frame->buf_Virt_Addr);
		     }

					{
						native_handle_t *pNativeHandle = (native_handle_t *)*buf_handle;
						struct private_handle_t *private_h = (struct private_handle_t *)pNativeHandle;
						uint32_t phy_addr =  (uint32_t)(private_h->phyaddr);

						nsecs_t timestamp_old, timestamp_new;

						timestamp_old = systemTime();

#ifdef USE_ION_MEM
						if(0 != camera_copy_data(width, height, frame->buffer_phy_addr, phy_addr)){
							LOGE("fail to call camera_copy_data in receivePreviewFrame.");
							goto callbacks;
						}
#else
                        #if 0
						if(0 != camera_copy_data_virtual(width, height, frame->buffer_phy_addr, (uint32_t)vaddr)){
							LOGE("fail to camera_copy_data_virtual() in receivePreviewFrame.");
							goto callbacks;
						}
                        #else
                        memcpy((void*)vaddr, (void*)frame->buf_Virt_Addr,width*height*3/2);
                        #endif

						//memcpy(vaddr, frame_addr, width*height*3/2);
						//LOGV("receivePreviewFrame, copy to vaddr: src=%x, dst=%x, dstphy=%x \n", (uint32_t)frame_addr, (uint32_t)vaddr, phy_addr);
#endif
						timestamp_new = systemTime();
						LOGV("receivePreviewFrame %lld, %lld, time = %lld us \n",timestamp_old, timestamp_new, (timestamp_new-timestamp_old)/1000);

					}
                        mGrallocHal->unlock(mGrallocHal, *buf_handle);
                }
                else
                        LOGE("%s: could not obtain gralloc buffer", __func__);

                if (0 != mPreviewWindow->enqueue_buffer(mPreviewWindow, buf_handle)) {
                        LOGE("Could not enqueue gralloc buffer!\n");
                        goto callbacks;
                }
                else{
                //LOGI("OK to enqueue gralloc buffer!");
                }
        }
        else {
            LOGV("dont need copy to preview window!\n");
        }
}
}
#endif
callbacks:
#ifdef CONFIG_CAMERA_ISP
			send_img_data(2, mPreviewWidth, mPreviewHeight, (char *)frame->buf_Virt_Addr, frame->dx * frame->dy * 3 /2);
#endif
        if(mData_cb != NULL)
        {
                LOGV("receivePreviewFrame mMsgEnabled: 0x%x",mMsgEnabled);
                if (mMsgEnabled & CAMERA_MSG_PREVIEW_FRAME) {
#if 1
                        sprd_camera_memory_t *tempHeap = GetPmem("/dev/pmem_adsp", frame->dx * frame->dy * 3 /2, 1);
                        if(NULL == tempHeap)
                            return;
                        if(NULL == tempHeap->handle){
                                LOGE("Fail to GetPmem tempHeap.");
                                return;
                        }
                       /* if(0 != camera_convert_420_UV_VU(frame->buffer_phy_addr, tempHeap->phys_addr, frame->dx, frame->dy)){
                                LOGE("fail to camera_rotation_copy_data() in CAMERA_MSG_PREVIEW_FRAME.");
                                return;
                        }*/
                        memcpy(tempHeap->data,frame->buf_Virt_Addr,frame->dx * frame->dy * 3 /2);
                        mData_cb(CAMERA_MSG_PREVIEW_FRAME, tempHeap->camera_memory, 0, NULL, mUser);
                        FreePmem(tempHeap);
                        tempHeap = NULL;
#else
                        if(camera_get_rot_set()) {
                            mData_cb(CAMERA_MSG_PREVIEW_FRAME,mPreviewHeap->camera_memory,(kPreviewBufferCount+offset),NULL,mUser);
                        } else {
                            mData_cb(CAMERA_MSG_PREVIEW_FRAME,mPreviewHeap->camera_memory,offset,NULL,mUser);
                        }
#endif
                }

		if ((mMsgEnabled & CAMERA_MSG_VIDEO_FRAME) &&(mRecordingMode==1)) {
                        nsecs_t timestamp = systemTime();/*frame->timestamp;*/
                        LOGV("test timestamp = %lld, mIsStoreMetaData: %d.",timestamp, mIsStoreMetaData);

                        if ((mCameraState != QCS_PREVIEW_IN_PROGRESS)
					|| (mSettingPreviewWindowState == PREVIEW_WINDOW_SETTING)
					|| (mRecordingRunState == RECORDING_STARTING)) {

					LOGE("directly return! mCameraState is %d mSettingPreviewWindowState is %d mRecordingRunState is %d ",
						mCameraState, mSettingPreviewWindowState, mRecordingRunState);

					if(CAMERA_SUCCESS != camera_release_frame(frame->buf_id)){
					LOGE("receivePreviewFrame: fail to camera_release_frame().offset: %d.", frame->buf_id);
					}
					return;
			}

			if(mIsStoreMetaData) {
	                        uint32_t *data = (uint32_t *)mMetadataHeap->data + offset * METADATA_SIZE / 4;
				*data++ = kMetadataBufferTypeCameraSource;
				*data++ = frame->buffer_phy_addr;
				*data = (uint32_t)frame->buf_Virt_Addr;
	                        mData_cb_timestamp(timestamp, CAMERA_MSG_VIDEO_FRAME, mMetadataHeap,  offset, mUser);
			}
			else {
	                        //mData_cb_timestamp(timestamp, CAMERA_MSG_VIDEO_FRAME, mPreviewHeap->mBuffers[offset], mUser);
	                        if(camera_get_rot_set()) {
	                            mData_cb_timestamp(timestamp, CAMERA_MSG_VIDEO_FRAME, mPreviewHeap->camera_memory, (kPreviewBufferCount+offset), mUser);
	                        } else {
	                            mData_cb_timestamp(timestamp, CAMERA_MSG_VIDEO_FRAME, mPreviewHeap->camera_memory, offset, mUser);
	                        }
			}
                     //LOGV("receivePreviewFrame: record index: %d, offset: %x, size: %x, frame->buf_Virt_Addr: 0x%x.", offset, off, size, (uint32_t)frame->buf_Virt_Addr);
                }
		else{
                        if(CAMERA_SUCCESS != camera_release_frame(offset)){
                        LOGE("receivePreviewFrame: fail to camera_release_frame().offset: %d.", offset);
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
        LOGE("receivePreviewFrame:e.");

}

void SprdCameraHardware::notifyShutter(camera_frame_type *frame)
{
        LOGV("notifyShutter: E");
        print_time();
        //   Mutex::Autolock lock(&mStateLock);

        /*if (mShutterCallback)
        mShutterCallback(mPictureCallbackCookie);*/
        //if(mNotify_cb)
        LOGV("notifyShutter mMsgEnabled: 0x%x.", mMsgEnabled);
		if(CAMERA_CONTINUE_SHOT_MODE != frame->cap_mode){
			if (mMsgEnabled & CAMERA_MSG_SHUTTER)
        		mNotify_cb(CAMERA_MSG_SHUTTER, 0, 0, mUser);
		}
		else{
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
    void *vaddr;
    int width, height, frame_size, offset_size;
	nsecs_t timestamp_old, timestamp_new;

    LOGV("receiveRawPicture: E");
    print_time();

    Mutex::Autolock cbLock(&mCallbackLock);

    if(QCS_INTERNAL_CAPTURE_STOPPING == mCameraState){
		LOGV("receiveRawPicture: warning: mCameraState = QCS_INTERNAL_CAPTURE_STOPPING, return \n");
		return;
    }

    //width = 640;
    //height = 480;
    width  = mPreviewWidth;
    height = mPreviewHeight;

    if (mPreviewWindow && mGrallocHal) {
        buffer_handle_t *buf_handle;
        int stride;
        if (0 != mPreviewWindow->dequeue_buffer(mPreviewWindow, &buf_handle, &stride)) {
            LOGE("Could not dequeue gralloc buffer!\n");
            goto callbackraw;
        }

        if (!mGrallocHal->lock(mGrallocHal,
                               *buf_handle,
                               GRALLOC_USAGE_SW_WRITE_OFTEN,
                               0, 0, width, height, &vaddr))
        {
            native_handle_t *pNativeHandle = (native_handle_t *)*buf_handle;
            struct private_handle_t *private_h = (struct private_handle_t *)pNativeHandle;
            uint32_t phy_addr =  (uint32_t)(private_h->phyaddr);
			uint32_t tmp_phy_addr;

            if(NULL == vaddr){
                LOGE("Fail to get gralloc buffer.");
                goto enqueuehandle;
            }
            else{
            //LOGI("OK to get gralloc buffer. vaddr: 0x%x, frame_addr: 0x%x, frame->buf_Virt_Addr: 0x%x.", (uint32_t)vaddr, (uint32_t)frame_addr, (uint32_t)frame->buf_Virt_Addr);
            }

#ifdef USE_ION_MEM
            if( 0 != camera_get_data_redisplay(phy_addr, width, height, frame->buffer_phy_addr, frame->buffer_uv_phy_addr, frame->dx, frame->dy)){
                LOGE("Fail to camera_get_data_redisplay.");
                goto enqueuehandle;
            }
#else
			if(NULL == get_redisplay_mem(width*height*3/2, 1, &tmp_phy_addr))
				goto enqueuehandle;

            if( 0 != camera_get_data_redisplay(tmp_phy_addr, width, height, frame->buffer_phy_addr, frame->buffer_uv_phy_addr, frame->dx, frame->dy)){
                LOGE("Fail to camera_get_data_redisplay.");
		        FreePmem(mReDisplayHeap);
		        mReDisplayHeap = NULL;
                goto enqueuehandle;
            }
			timestamp_old = systemTime();
			if(0 != camera_copy_data_virtual(width, height, tmp_phy_addr, (uint32_t)vaddr)){
			        LOGE("fail to camera_copy_data_virtual() in receiveRawPicture.");
                      FreePmem(mReDisplayHeap);
		              mReDisplayHeap = NULL;
   		              goto enqueuehandle;
			}

			//memcpy(vaddr, mReDisplayHeap->data, width*height*3/2);
			timestamp_new = systemTime();
			LOGV("receiveRawPicture: %lld, %lld, time = %lld us \n",timestamp_old, timestamp_new, (timestamp_new-timestamp_old)/1000);
			FreePmem(mReDisplayHeap);
			mReDisplayHeap = NULL;
#endif
enqueuehandle:
            mGrallocHal->unlock(mGrallocHal, *buf_handle);
        }
        else
            LOGE("%s: could not obtain gralloc buffer", __func__);

        if (0 != mPreviewWindow->enqueue_buffer(mPreviewWindow, buf_handle)) {
            LOGE("Could not enqueue gralloc buffer!\n");
            goto callbackraw;
        }
	else{
		//LOGI("OK to enqueue gralloc buffer!");
	}
    }

callbackraw:
        //if (mRawPictureCallback != NULL) {
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
		        frame_size = frame->dx * frame->dy * 3 / 2;        //for YUV420
		else
			frame_size = frame->dx * frame->dy * 2;
	        if (offset + frame_size <= (ssize_t)mRawHeap->phys_size) {
	                offset /= frame_size;
			   LOGV("mMsgEnabled: 0x%x, offset: %d.",mMsgEnabled, (uint32_t)offset);
			   if (mMsgEnabled & CAMERA_MSG_RAW_IMAGE)
			   {
				mData_cb(CAMERA_MSG_RAW_IMAGE, mRawHeap->camera_memory, offset, NULL, mUser);
			    }
			if(mMsgEnabled & CAMERA_MSG_RAW_IMAGE_NOTIFY) {
				LOGV("mMsgEnabled & CAMERA_MSG_RAW_IMAGE_NOTIFY");
				mNotify_cb(CAMERA_MSG_RAW_IMAGE_NOTIFY, 0,0,mUser);
			}
	        }
	            else LOGE("receiveRawPicture: virtual address %p is out of range!",
	                      frame->buf_Virt_Addr);
        }
        else LOGV("Raw-picture callback was canceled--skipping.");
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
        Sprd_camera_state new_state = QCS_ERROR;
        Mutex::Autolock cbLock(&mCallbackLock);

        if (mData_cb!= NULL) {
            bool encode_location = true;

#define PARSE_LOCATION(what,type,fmt,desc) do {                                           \
                pt.what = 0;                                                              \
                const char *what##_str = mParameters.get("gps-"#what);                    \
                LOGV("receiveRawPicture: GPS PARM %s --> [%s]", "gps-"#what, what##_str); \
                if (what##_str) {                                                         \
                    type what = 0;                                                        \
                    if (sscanf(what##_str, fmt, &what) == 1)                              \
                        pt.what = what;                                                   \
                    else {                                                                \
                        LOGE("GPS " #what " %s could not"                                 \
                              " be parsed as a " #desc,                                   \
                              what##_str);                                                \
                        encode_location = false;                                          \
                    }                                                                     \
                }                                                                         \
                else {                                                                    \
                    LOGW("receiveRawPicture: GPS " #what " not specified: "               \
                          "defaulting to zero in EXIF header.");                          \
                    encode_location = false;                                              \
               }                                                                          \
            } while(0)

            PARSE_LOCATION(timestamp, long, "%ld", "long");
            if (!pt.timestamp) pt.timestamp = time(NULL);
            PARSE_LOCATION(altitude, double, "%lf", "double float");
            PARSE_LOCATION(latitude, double, "%lf", "double float");
            PARSE_LOCATION(longitude, double, "%lf", "double float");
  	    pt.process_method = mParameters.get("gps-processing-method");

#undef PARSE_LOCATION

            if (encode_location) {
                LOGV("receiveRawPicture: setting image location ALT %lf LAT %lf LON %lf",
                     pt.altitude, pt.latitude, pt.longitude);
                if (camera_set_position(&pt, NULL, NULL) != CAMERA_SUCCESS) {
                    LOGE("receiveRawPicture: camera_set_position: error");
                    // return;  // not a big deal
                }
            }
            else
		LOGV("receiveRawPicture: not setting image location");

            mJpegSize = 0;

            if(CAMERA_SUCCESS != camera_encode_picture(frame, &camera_handle, camera_cb, this)){
		mCameraState = QCS_ERROR;
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

        FreePmem(mRawHeap);
        mRawHeap = NULL;
        FreePmem(mMiscHeap);
        mMiscHeap = NULL;
        }
        print_time();
        LOGV("receivePostLpmRawPicture: X");
    }

    void   SprdCameraHardware::receiveJpegPictureFragment( JPEGENC_CBrtnType *encInfo)
    {
        camera_encode_mem_type *enc =  (camera_encode_mem_type *)encInfo->outPtr;

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
   /* temp_ptr = (uint8_t*)(base + mJpegSize);
    src_ptr = (uint8_t*)enc->buffer;
    for(i=0;i<size;i++) {
        *temp_ptr++ = *src_ptr++;
    }*/
    memcpy(base + mJpegSize, enc->buffer, size);
        mJpegSize += size;

	LOGV("receiveJpegPictureFragment X.");
    }


void   SprdCameraHardware::receiveJpegPosPicture(void)//(camera_frame_type *frame)
{
	LOGV("receiveJpegPosPicture: E");
	print_time();
	Sprd_camera_state new_state = QCS_ERROR;

	Mutex::Autolock cbLock(&mCallbackLock);
	LOGV("receiveJpegPosPicture mCallbackLock!");

        //if (mJpegPictureCallback != NULL) {
        if (mData_cb!= NULL) {

            bool encode_location = true;

#define PARSE_LOCATION(what,type,fmt,desc) do {                                           \
                pt.what = 0;                                                              \
                const char *what##_str = mParameters.get("gps-"#what);                    \
                LOGV("receiveRawPicture: GPS PARM %s --> [%s]", "gps-"#what, what##_str); \
                if (what##_str) {                                                         \
                    type what = 0;                                                        \
                    if (sscanf(what##_str, fmt, &what) == 1)                              \
                        pt.what = what;                                                   \
                    else {                                                                \
                        LOGE("GPS " #what " %s could not"                                 \
                              " be parsed as a " #desc,                                   \
                              what##_str);                                                \
                        encode_location = false;                                          \
                    }                                                                     \
                }                                                                         \
                else {                                                                    \
                    LOGW("receiveRawPicture: GPS " #what " not specified: "               \
                          "defaulting to zero in EXIF header.");                          \
                    encode_location = false;                                              \
               }                                                                          \
            } while(0)

            PARSE_LOCATION(timestamp, long, "%ld", "long");
            if (!pt.timestamp) pt.timestamp = time(NULL);
            PARSE_LOCATION(altitude, double, "%lf", "double float");
            PARSE_LOCATION(latitude, double, "%lf", "double float");
            PARSE_LOCATION(longitude, double, "%lf", "double float");

#undef PARSE_LOCATION

            if (encode_location) {
                LOGV("receiveJpegPosPicture: setting image location ALT %lf LAT %lf LON %lf",
                     pt.altitude, pt.latitude, pt.longitude);
                if (camera_set_position(&pt, NULL, NULL) != CAMERA_SUCCESS) {
                    LOGE("receiveRawPicture: camera_set_position: error");
                    // return;  // not a big deal
                }
            }
            else
		LOGV("receiveRawPicture: not setting image location");

            mJpegSize = 0;
        }
        else {
            LOGV("receiveJpegPosPicture JPEG callback was cancelled--not encoding image.");
            // We need to keep the raw heap around until the JPEG is fully
            // encoded, because the JPEG encode uses the raw image contained in
            // that heap.
        FreePmem(mRawHeap);
        mRawHeap = NULL;
        FreePmem(mMiscHeap);
        mMiscHeap = NULL;
        }
	print_time();
	LOGV("receiveJpegPosPicture: X");
	LOGV("receiveJpegPosPicture  free mCallbackLock!");
}

    // This method is called by a libqcamera thread, different from the one on
    // which startPreview() or takePicture() are called.

    void
    SprdCameraHardware::receiveJpegPicture(JPEGENC_CBrtnType *encInfo)
    {
        LOGV("receiveJpegPicture: E image (%d bytes out of %d)",
             mJpegSize, mJpegHeap->mBufferSize);
        print_time();
        Mutex::Autolock cbLock(&mCallbackLock);

        int index = 0;

        //if (mJpegPictureCallback) {
        if (mData_cb) {
	    LOGV("receiveJpegPicture: mData_cb.");
            // The reason we do not allocate into mJpegHeap->mBuffers[offset] is
            // that the JPEG image's size will probably change from one snapshot
            // to the next, so we cannot reuse the MemoryBase object.
            sp<MemoryBase> buffer = new
                MemoryBase(mJpegHeap->mHeap,
                           index * mJpegHeap->mBufferSize +
                           mJpegHeap->mFrameOffset,
                           mJpegSize);
            LOGV("receiveJpegPicture:  mMsgEnabled: 0x%x.", mMsgEnabled);
           // mJpegPictureCallback(buffer, mPictureCallbackCookie);
           if (CAMERA_CONTINUE_SHOT_MODE != encInfo->cap_mode) {
	           if (mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE){
				camera_memory_t *mem = mGetMemory_cb(-1, mJpegSize, 1, 0);
			       memcpy(mem->data, mJpegHeap->mHeap->base(), mJpegSize);
		           //mData_cb(CAMERA_MSG_COMPRESSED_IMAGE,buffer, mUser );
		           mData_cb(CAMERA_MSG_COMPRESSED_IMAGE,mem, 0, NULL, mUser );
	  		    mem->release(mem);
	           }
           } else {
           		camera_memory_t *mem = mGetMemory_cb(-1, mJpegSize, 1, 0);
			    memcpy(mem->data, mJpegHeap->mHeap->base(), mJpegSize);
		        //mData_cb(CAMERA_MSG_COMPRESSED_IMAGE,buffer, mUser );
		        mData_cb(CAMERA_MSG_COMPRESSED_IMAGE,mem, 0, NULL, mUser );
	  		    mem->release(mem);
           }
            buffer = NULL;
        }
        else LOGV("JPEG callback was cancelled--not delivering image.");

        // NOTE: the JPEG encoder uses the raw image contained in mRawHeap, so we need
        // to keep the heap around until the encoding is complete.
        LOGV("receiveJpegPicture: free the Raw and Jpeg mem. 0x%x, 0x%x", (unsigned int)mRawHeap, (unsigned int)mMiscHeap);
		//real add to do FreeCameraMem when shot_num == CONTINUE_NUM?
		if (encInfo->need_free) {
	        FreePmem(mRawHeap);
	        mRawHeap = NULL;
	        FreePmem(mMiscHeap);
	        mMiscHeap = NULL;
		}
        print_time();
        LOGV("receiveJpegPicture: X callback done.");
    }

    void
    SprdCameraHardware::receiveJpegPictureError(JPEGENC_CBrtnType *encInfo)
    {
        LOGV("receiveJpegPictureError.");
        print_time();
        Mutex::Autolock cbLock(&mCallbackLock);

        int index = 0;

        if (mData_cb) {
	        LOGV("receiveJpegPicture: mData_cb.");
           if (mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE){
	           mData_cb(CAMERA_MSG_COMPRESSED_IMAGE,NULL, 0, NULL, mUser );
           }
        } else LOGV("JPEG callback was cancelled--not delivering image.");
        // NOTE: the JPEG encoder uses the raw image contained in mRawHeap, so we need
        // to keep the heap around until the encoding is complete.
	if (encInfo->need_free) {
		FreePmem(mRawHeap);
		mRawHeap = NULL;
		FreePmem(mMiscHeap);
		mMiscHeap = NULL;
	}
        print_time();
        LOGV("receiveJpegPictureError: X callback done.");
    }

    static int lookupvalue(const struct str_map *const arr, const char *name)
    {
    	LOGV("lookup: name :%s .",name);
        if (name) {
            const struct str_map * trav = arr;
            while (trav->desc) {
                if (!strcmp(trav->desc, name))
                    return trav->val;
                trav++;
            }
        }
	return  0xFFFFFFFF;
    }
static int lookup(const struct str_map *const arr, const char *name, int def)
{
        int ret = lookupvalue(arr, name);

        return 0xFFFFFFFF == (unsigned int)ret ? def : ret;
}
static uint32_t lookupsizewh(uint32_t *arr, const char *size_str)
{
	uint32_t pos = 0;
	char *a=(char*)size_str;
	char *b;
	char k[10] = {0};

	uint32_t *size_arr = arr;
	uint32_t cnt = 0;
	uint32_t i=0;
	if(!a)
	{
		return 0;
	}

	b = strchr(a,'x');
	if(b==0)
		goto lookupsizewh_done;

	strncpy(k,a,(b-a));
	a = b+1;

	*size_arr++=strtol(k,0,0);
	*size_arr++=strtol(a,0,0);
	cnt++;
lookupsizewh_done:
	LOGV("lookupsizewh:cnt=%d.",cnt);
	LOGV("lookupsizewh:%d,%d.",arr[i],arr[i+1]);

	return cnt;

}

static uint32_t lookupsize(uint32_t *arr, const char *size_str)
{
	uint32_t pos = 0;
	char *a=(char*)size_str;
	char *b,*c;
	char k[10] = {0};
	char m[10]={0};
	uint32_t *size_arr = arr;
	uint32_t cnt = 0;
	uint32_t i=0;
	if(!a)
	{
		return 0;
	}
	do
	{
		b = strchr(a,'x');
		if(b==0)
			goto lookupsize_done;

		strncpy(k,a,(b-a));
		a = b+1;
		*size_arr++=strtol(k,0,0);

		b = strchr(a,',');
		if(b==0)
		{
			b = strchr(a,'\0');
		}
		memset(k,0,10);
	         strncpy(k,a,(b-a));
		a=b+1;
		*size_arr++=strtol(k,0,0);

		memset(k,0,10);
		cnt++;
	}while(a);
lookupsize_done:
	LOGV("lookupsize:cnt=%d.",cnt);
	for(i=0;i<2*cnt;i+=2)
	{
		LOGV("lookupsize:%d,%d.",arr[i],arr[i+1]);
	}
	return cnt;

}

#define CAMERA_HAL_FOCUS_ZONE_MAX   5
static int s_save_zone_info[5*CAMERA_HAL_FOCUS_ZONE_MAX+1];
static uint32_t lookuprect(int *arr, const char *rect_str)
{
	char *a=(char*)rect_str;
	char *b,*c;
	char k[40] = {0};
	char m[40]={0};
	int *rect_arr = arr+1;
	uint32_t cnt = 0;
	uint32_t i=0;
	if(!a)
	{
		return 0;
	}
	do
	{
		b = strchr(a,'(');
		if(b==0)
			goto lookuprect_done;
                  a = b+1;
                  b = strchr(a,')');
                  if(b==0)
			goto lookuprect_done;

		strncpy(k,a,(b-a));
		a = b+1;

                  c = strchr(k,',');
                  strncpy(m,k,(c-k));
		*rect_arr++=strtol(m,0,0);//left
		memset(m,0,20);

		b=c+1;
		c=strchr(b,',');
		strncpy(m,b,(c-b));
		*rect_arr++=strtol(m,0,0);//top
		memset(m,0,20);

		b=c+1;
		c=strchr(b,',');
		strncpy(m,b,(c-b));
		*rect_arr++=strtol(m,0,0);//right
		memset(m,0,20);

		b=c+1;
		c=strchr(b,',');
		strncpy(m,b,(c-b));
		*rect_arr++=strtol(m,0,0);//bottom
		memset(m,0,20);

	        b=c+1;
		*rect_arr++=strtol(b,0,0);//weight
		memset(m,0,20);
		memset(k,0,10);
		cnt++;
                  if(cnt==CAMERA_HAL_FOCUS_ZONE_MAX)
                    break;
	}while(a);
lookuprect_done:
	LOGV("lookuprect:cnt=%d.",cnt);
         arr[0] = cnt;
	for(i=1;i<5*cnt;i+=5)
	{
		LOGV("lookupsize:%d,%d,%d,%d,%d.\n",arr[i],arr[i+1],arr[i+2],arr[i+3],arr[i+4]);
	}
	return cnt;

}
static void discard_zone_weight(int *arr, uint32_t size)
{
    uint32_t i = 0;
    int *dst_arr = &arr[4];
    int *src_arr = &arr[5];

    for(i=0;i<(size-1);i++)
    {
        *dst_arr++ = *src_arr++;
        *dst_arr++ = *src_arr++;
        *dst_arr++ = *src_arr++;
        *dst_arr++ = *src_arr++;
        src_arr++;
    }
    for(i=0;i<size;i++)
    {
        LOGV("CAMERA HAL:discard_zone_weight: %d:%d,%d,%d,%d.\n",i,arr[i*4],arr[i*4+1],arr[i*4+2],arr[i*4+3]);
     }
}

static void coordinate_struct_convert(int *rect_arr,int arr_size)
{
    int i =0;
    int left = 0,top=0,right=0,bottom=0;
    int *rect_arr_copy = rect_arr;

    for(i=0;i<arr_size/4;i++)
    {
        left = *rect_arr++;
        top = *rect_arr++;
        right = *rect_arr;
        *rect_arr++ = (((right-left+3) >> 2)<<2);
        bottom = *rect_arr;
        *rect_arr++=(((bottom-top+3) >> 2)<<2);
    }
    for(i=0;i<arr_size/4;i++)
    {
        LOGV("CAMERA HAL test:zone:%d,%d,%d,%d.\n",rect_arr_copy[i*4],rect_arr_copy[i*4+1],rect_arr_copy[i*4+2],rect_arr_copy[i*4+3]);
    }
}
static int coordinate_convert(int *rect_arr,int arr_size,int angle,int is_mirror, int width, int height)
{
	int i;
	int x1,x2,y1,y2;
	int temp;
	int recHalfWidth;
	int recHalfHeight;
	int centre_x;
	int centre_y;
	int ret = 0;

	LOGV("coordinate_convert: mPreviewWidth=%d, mPreviewHeight=%d, arr_size=%d, angle=%d, is_mirror=%d \n", 
		width, height, arr_size, angle, is_mirror);

	for(i=0;i<arr_size*2;i++)
	{
            x1 = rect_arr[i*2];
            y1 = rect_arr[i*2+1];

            if(is_mirror)
                x1 = -x1;

            switch(angle)
            {
                case 0:
                    rect_arr[i*2]         = (1000 + x1) * height / 2000; // 480
                    rect_arr[i*2 + 1]   = (1000 + y1) * width / 2000;	// 640
                    break;

                case 90:
                    rect_arr[i*2]         = (1000 - y1) * height / 2000;
                    rect_arr[i*2 + 1]   = (1000 + x1) * width / 2000;
                    break;

                case 180:
                    rect_arr[i*2]         = (1000 - x1) * height / 2000;
                    rect_arr[i*2 + 1]   = (1000 - y1) * width / 2000;
                    break;

                case 270:
                    rect_arr[i*2]         = (1000 + y1) * height / 2000;
                    rect_arr[i*2 + 1]   = (1000 - y1) * width / 2000;
                    break;

            }
	}

    for(i=0;i<arr_size;i++)
    {
        // (x1, y1, x2, y2)
        // if x1 > x2, (x2, y1, x1, y2)
        if(rect_arr[i*4] > rect_arr[i*4+2])
        {
            temp                    = rect_arr[i*4];
            rect_arr[i*4]       = rect_arr[i*4+2];
            rect_arr[i*4+2]     = temp;

        }

        if(rect_arr[i*4+1] > rect_arr[i*4+3])
        {
            temp                    = rect_arr[i*4+1];
            rect_arr[i*4+1]       = rect_arr[i*4+3];
            rect_arr[i*4+3]     = temp;

        }
        LOGV("CAMERA HAL:coordinate_convert: %d: %d, %d, %d, %d.\n",i,rect_arr[i*4],rect_arr[i*4+1],rect_arr[i*4+2],rect_arr[i*4+3]);
    }

	// make sure the coordinate within [width, height]
	// for 90 degree only
	for(i=0; i<arr_size*4; i+=2){
		if(rect_arr[i] < 0){
			rect_arr[i]= 0;
		}
		if(rect_arr[i] > height){
			rect_arr[i] = height;
		}

		if(rect_arr[i+1] < 0){
			rect_arr[i+1]= 0;
		}
		if(rect_arr[i+1] > width){
			rect_arr[i+1] = width;
		}
	}

	for(i=0;i<arr_size;i++)
	{
		int preview_x, preview_y, preview_w, preview_h;
		int point_x, point_y;
		
		LOGV("CAMERA HAL:coordinate_convert %d: org: %d, %d, %d, %d.\n",i,rect_arr[i*4],rect_arr[i*4+1],rect_arr[i*4+2],rect_arr[i*4+3]);

		// only for angle 90/270
		// calculate the centre point
		recHalfHeight  	= (rect_arr[i*4+2] - rect_arr[i])/2;
		recHalfWidth   	= (rect_arr[i*4+3] - rect_arr[i+1])/2;
		centre_y  		= rect_arr[i*4+2] - recHalfHeight;
		centre_x 		= rect_arr[i*4+3] - recHalfWidth;
		LOGV("CAMERA HAL:coordinate_convert %d: center point: x=%d, y=%d\n", i, centre_x, centre_y);

		// map to sensor coordinate
		centre_y		= height - centre_y;
		LOGV("CAMERA HAL:coordinate_convert %d: sensor centre pointer: x=%d, y=%d, half_w=%d, half_h=%d.\n",
				i, centre_x, centre_y, recHalfWidth, recHalfHeight);
		
		ret = camera_get_preview_rect(&preview_x, &preview_y, &preview_w, &preview_h);
		if(ret){
			LOGV("coordinate_convert: camera_get_preview_rect failed, return \n");
			return -1;
		}
		LOGV("CAMERA HAL:coordinate_convert %d: preview rect: x=%d, y=%d, preview_w=%d, preview_h=%d.\n",
				i, preview_x, preview_y, preview_w, preview_h);
		
		point_x = preview_x + centre_x*preview_w/width;
		point_y = preview_y + centre_y*preview_h/height;
		LOGV("CAMERA HAL:coordinate_convert %d: out point: x=%d, y=%d\n", i, point_x, point_y);
		
		rect_arr[i] 	= point_x - recHalfWidth;
		rect_arr[i+1] 	= point_y - recHalfHeight;
		rect_arr[i+2] 	= point_x + recHalfWidth;
		rect_arr[i+3] 	= point_y + recHalfHeight;

		LOGV("CAMERA HAL:coordinate_convert %d: final: %d, %d, %d, %d.\n",i,rect_arr[i*4],rect_arr[i*4+1],rect_arr[i*4+2],rect_arr[i*4+3]);
	}

	return ret;
}

status_t strToFPS(const char *str, int &min, int &max) {
	  char *pDest;
	  char strTmp[100] = {0}; //CR 187754

	  if (str == NULL) return UNKNOWN_ERROR;

	  LOGV("preview-fps-range: %s", str);

	  pDest = strrchr(str,(int)',');
	   memcpy(strTmp, str, strlen(str) - strlen(pDest));
	    min = atoi(strTmp);
	    pDest = strrchr((const char *)str, (int)',');
        if (pDest == NULL) {
            LOGV("pDest is NULL.");
            return UNKNOWN_ERROR;
        }
	    pDest++;
	    strcpy(strTmp, pDest);
	    max = atoi(strTmp);

	   LOGV("preview-fps-range: min: %d, max: %d.", min, max);

	    return NO_ERROR;
  }

static uint32_t s_focus_zone[25];

    status_t SprdCameraHardware::initCameraParameters()
    {
        LOGV("initCameraParameters: E");

        // Because libqcamera is broken, for the camera_set_parm() calls
        // SprdCameraHardware camera_cb() is called synchronously,
        // so we cannot wait on a state change.  Also, we have to unlock
        // the mStateLock, because camera_cb() acquires it.

        if(true != startCameraIfNecessary())
            return UNKNOWN_ERROR;

	//wxz20120316: check the value of preview-fps-range. //CR168435
	{
		int min,max;
		if(NO_ERROR != strToFPS(mParameters.get("preview-fps-range"), min, max)){
			LOGE("Fail to strToFPS(). ");
			return UNKNOWN_ERROR;
		}
		if((min > max) || (min < 0) || (max < 0)){
			LOGE("Error to FPS range: mix: %d, max: %d.", min, max);
			return UNKNOWN_ERROR;
		}
	}
	//check preview size
	{
		int w,h;
		mParameters.getPreviewSize(&w, &h);
		if((w < 0) || (h < 0)){
			mParameters.setPreviewSize(640, 480);
			return UNKNOWN_ERROR;
		}
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
        if(1 == mParameters.getInt("sensororientation")){
        	SET_PARM(CAMERA_PARM_ORIENTATION, 1); //for portrait
        	mOrientation_parm = 1;
	}
	else{
        	SET_PARM(CAMERA_PARM_ORIENTATION, 0); //for landscape
        	mOrientation_parm = 0;
	}
        rotation = mParameters.getInt("sensorrotation");
        if (-1 == rotation)
            rotation = 0;
        SET_PARM(CAMERA_PARM_SENSOR_ROTATION, rotation);
		
        int shot_num = mParameters.getInt("capture-mode");
        SET_PARM(CAMERA_PARM_SHOT_NUM, shot_num);
#if 0//test code
	uint32_t size_cnt = 0;
         int is_mirror=0;
	char *src="(-800,-750,-650,0,1),(-500,-50,50,30,1)";

        LOGV("focus area: %s \n", mParameters.get("focus-areas"));

         size_cnt = lookuprect(&s_save_zone_info[0],src);
	if(size_cnt)
	{
            discard_zone_weight(&s_save_zone_info[1],size_cnt);
            is_mirror = (g_camera_id == 1) ? 1 : 0;
            coordinate_convert(&s_save_zone_info[1],size_cnt,sCameraInfo[g_camera_id].orientation,is_mirror, mPreviewWidth, mPreviewHeight);
            coordinate_struct_convert(&s_save_zone_info[1],size_cnt*4);
	}
#else
	uint32_t size_cnt = 0;
        int is_mirror=0;
		int convert_ret = 0;

        is_mirror = (g_camera_id == 1) ? 1 : 0;

        LOGV("focus area: %s, mirror=%d, orientation = %d \n", mParameters.get("focus-areas"), is_mirror, sCameraInfo[g_camera_id].orientation);

         size_cnt = lookuprect(&s_save_zone_info[0], mParameters.get("focus-areas"));
	if(size_cnt)
	{
            uint32_t i;
            discard_zone_weight(&s_save_zone_info[1], size_cnt);

            convert_ret = coordinate_convert(&s_save_zone_info[1],size_cnt,sCameraInfo[g_camera_id].orientation,is_mirror, mPreviewWidth, mPreviewHeight);
			if(convert_ret)
			{
                size_cnt = 0;
                LOGV("error: coordinate_convert error, ignore focus \n");
			}
			else
			{
	            coordinate_struct_convert(&s_save_zone_info[1],size_cnt*4);

	            for (i=0; i<size_cnt*4; i++)
	            {
	                if(s_save_zone_info[i+1] < 0)
	                {
	                    size_cnt = 0;
	                    LOGV("error: focus area %d < 0, ignore focus \n", s_save_zone_info[i+1]);
	                }
	            }
			}
	}
#endif
	s_focus_zone[0] = size_cnt;

	//SET_PARM(CAMERA_PARM_FOCUS_RECT,(int32_t)s_focus_zone);
	SET_PARM(CAMERA_PARM_FOCUS_RECT,(int32_t)s_save_zone_info);
if (g_camera_id == 0) {
	if(0xFFFFFFFF == (unsigned int)lookupvalue(focus_mode_map,
                        mParameters.get("focus-mode"))){
		mParameters.set("focus-mode", "auto");
		return UNKNOWN_ERROR;
	}
        SET_PARM(CAMERA_PARM_AF_MODE,
                 lookup(focus_mode_map,
                        mParameters.get("focus-mode"),
                        CAMERA_FOCUS_MODE_AUTO));
}
        SET_PARM(CAMERA_PARM_WB,
                 lookup(wb_map,
                        mParameters.get("whitebalance"),
                        CAMERA_WB_AUTO));
        SET_PARM(CAMERA_PARM_CAMERA_ID,
                 lookup(camera_id_map,
                        mParameters.get("cameraid"),
                        CAMERA_CAMERA_ID_BACK));
        SET_PARM(CAMERA_PARM_JPEGCOMP,  atoi(mParameters.get("jpeg-quality")));
        SET_PARM(CAMERA_PARM_THUMBCOMP, atoi(mParameters.get("jpeg-thumbnail-quality")));
        SET_PARM(CAMERA_PARM_EFFECT,
                 lookup(effect_map,
                        mParameters.get("effect"),
                        CAMERA_EFFECT_NONE));
        SET_PARM(CAMERA_PARM_SCENE_MODE,
                 lookup(scene_mode_map,
                        mParameters.get("scene-mode"),
                        CAMERA_SCENE_MODE_AUTO));
	g_sprd_zoom_levle = lookup(zoom_map,mParameters.get("zoom"), CAMERA_ZOOM_1X);
        SET_PARM(CAMERA_PARM_ZOOM, g_sprd_zoom_levle);

        SET_PARM(CAMERA_PARM_BRIGHTNESS,
                 lookup(brightness_map,
                        mParameters.get("brightness"),
                        CAMERA_BRIGHTNESS_DEFAULT));
        SET_PARM(CAMERA_PARM_CONTRAST,
                 lookup(contrast_map,
                        mParameters.get("contrast"),
                        CAMERA_CONTRAST_DEFAULT));

	SET_PARM(CAMERA_PARM_EXPOSURE_COMPENSATION,
		lookup(exposure_compensation_map,
			mParameters.get("exposure-compensation"),
			CAMERA_EXPOSURW_COMPENSATION_DEFAULT));

	SET_PARM(CAMERA_PARM_ANTIBANDING,
		lookup(antibanding_map,
			mParameters.get("antibanding"),
			CAMERA_ANTIBANDING_50HZ));

        SET_PARM(CAMERA_PARM_ISO,
                 lookup(iso_map,
                        mParameters.get("iso"),
                        CAMERA_ISO_AUTO));
       SET_PARM(CAMERA_PARM_DCDV_MODE,
                 lookup(camera_dcdv_mode,
                        mParameters.get("recording-hint"),
                        CAMERA_DC_MODE));
#if 0
	int recordin_hit = lookup(recording_hint_map, mParameters.get("recording-hint"), CAMERA_RECORDING_MAX);
	LOGV("recordin_hit = %d \n", recordin_hit);
	if(CAMERA_RECORDING_MAX != recordin_hit)
		SET_PARM(CAMERA_PARM_PREVIEW_MODE, recordin_hit);
#endif

#ifndef CONFIG_CAMERA_788
    if (g_camera_id == 0) {
	if(0xFFFFFFFF == (unsigned int)lookupvalue(flash_mode_map,
                        mParameters.get("flash-mode"))){
		mParameters.set("flash-mode", "off");
		return UNKNOWN_ERROR;
	}
        SET_PARM(CAMERA_PARM_FLASH,
                 lookup(flash_mode_map,
                        mParameters.get("flash-mode"),
                        CAMERA_FLASH_MODE_OFF));
    }
#endif

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
            LOGI("setting thumbnail dimensions to %dx%d, quality %d",
                 th_w, th_h, th_q);
            int ret = camera_set_thumbnail_properties(th_w, th_h, th_q);
            if (ret != CAMERA_SUCCESS) {
                LOGE("camera_set_thumbnail_properties returned %d", ret);
            }
        }

        // Set Default JPEG encoding--this does not cause a callback
        encode_properties.quality   = mParameters.getInt("jpeg-quality");
        if (encode_properties.quality < 0) {
            LOGW("JPEG-image quality is not specified "
                 "or is negative, defaulting to %d",
                 encode_properties.quality);
            encode_properties.quality = 100;
        }
        else LOGV("Setting JPEG-image quality to %d",
                  encode_properties.quality);
        encode_properties.format    = CAMERA_JPEG;
        encode_properties.file_size = 0x0;
        camera_set_encode_properties(&encode_properties);


        LOGV("initCameraParameters: X");
      return NO_ERROR;
    }

#undef SET_PARM

    // Called with mStateLock held!
    void SprdCameraHardware::setCameraDimensions()
    {
        if (mCameraState != QCS_IDLE) {
            LOGE("set camera dimensions: expecting state QCS_IDLE, not %s",
                 getCameraStateStr(mCameraState));
            return;
        }

        camera_set_dimensions(mRawWidth,
                                   mRawHeight,
                                   mPreviewWidth,
                                   mPreviewHeight,
                                   NULL,
                                   NULL);

    }

    SprdCameraHardware::Sprd_camera_state
     SprdCameraHardware::change_state(Sprd_camera_state new_state,
        bool lock)
    {
    	LOGV("start to change_state.");
        //if (lock) mStateLock.lock();
        LOGV("start to lock.");
        if (new_state != mCameraState) {
            // Due to the fact that we allow only one thread at a time to call
            // startPreview(), stopPreview(), or takePicture(), we know that
            // only one thread at a time may be blocked waiting for a state
            // transition on mStateWait.  That's why we signal(), not
            // broadcast().
LOGV("start to mCameraState.");
            LOGV("state transition %s --> %s",
                 getCameraStateStr(mCameraState),
                 getCameraStateStr(new_state));
LOGV("start to getCameraStateStr.");
            mCameraState = new_state;
            mStateWait.signal();
        }
		LOGV("start to signal.");
       // if (lock) mStateLock.unlock();
        LOGV("start to unlock.");
        return new_state;
    }

#define CAMERA_STATE(n) case n: if(n != CAMERA_FUNC_START_PREVIEW || cb != CAMERA_EVT_CB_FRAME) LOGV("STATE %s // STATUS %d", #n, cb);
#define TRANSITION(e,s) do { \
            obj->change_state(obj->mCameraState == e ? s : QCS_ERROR); \
        } while(0)
#define TRANSITION_LOCKED(e,s) do { \
            obj->change_state((obj->mCameraState == e ? s : QCS_ERROR), false); \
        } while(0)
#define TRANSITION_ALWAYS(s) obj->change_state(s)


    // This callback is called from the destructor.
    void SprdCameraHardware::stop_camera_cb(camera_cb_type cb,
                                                const void *client_data,
                                                camera_func_type func,
                                                int32_t parm4)
    {
        SprdCameraHardware *obj =
            (SprdCameraHardware *)client_data;
        switch(func) {
            CAMERA_STATE(CAMERA_FUNC_STOP)
                TRANSITION(QCS_INTERNAL_STOPPING, QCS_INIT);
            break;
        default:
            break;
        }
    }

    void SprdCameraHardware::camera_cb(camera_cb_type cb,
                                           const void *client_data,
                                           camera_func_type func,
                                           int32_t parm4)
    {
        SprdCameraHardware *obj =
            (SprdCameraHardware *)client_data;
#if 0
        // Promote the singleton to make sure that we do not get destroyed
        // while this callback is executing.
        if (UNLIKELY(getInstance() == NULL)) {
            LOGE("camera object has been destroyed--returning immediately");
            return;
        }
#endif
        if (cb == CAMERA_EXIT_CB_ABORT ||     // Function aborted
            cb == CAMERA_EXIT_CB_DSP_ABORT || // Abort due to DSP failure
            cb == CAMERA_EXIT_CB_ERROR ||     // Failed due to resource
            cb == CAMERA_EXIT_CB_FAILED)      // Execution failed or rejected
        {
            // Autofocus failures occur relatively often and are not fatal, so
            // we do not transition to QCS_ERROR for them.
            if (func != CAMERA_FUNC_START_FOCUS) {
                LOGE("SprdCameraHardware::camera_cb: @CAMERA_EXIT_CB_FAILURE(%d) in state %s.",
                     parm4,
                     obj->getCameraStateStr(obj->mCameraState));
	if((cb == CAMERA_EXIT_CB_FAILED)&&(func == CAMERA_FUNC_TAKE_PICTURE)){
		obj->FreeCameraMem();
	}else if((cb == CAMERA_EXIT_CB_FAILED)&&(func == CAMERA_FUNC_ENCODE_PICTURE)){
		obj->receiveJpegPictureError((JPEGENC_CBrtnType *)parm4);
	}

                TRANSITION_ALWAYS(QCS_ERROR);
                obj->HandleErrorState();
		return ;
            }
        }

        switch(func) {
            // This is the commonest case.
            CAMERA_STATE(CAMERA_FUNC_START_PREVIEW)
                switch(cb) {
                case CAMERA_RSP_CB_SUCCESS:
                    TRANSITION(QCS_INTERNAL_PREVIEW_REQUESTED,
                               QCS_PREVIEW_IN_PROGRESS);
                    break;
                case CAMERA_EVT_CB_FRAME:
                    switch (obj->mCameraState) {
                    case QCS_PREVIEW_IN_PROGRESS:
                        if (parm4)
                            obj->receivePreviewFrame((camera_frame_type *)parm4);
                        break;
                    case QCS_INTERNAL_PREVIEW_STOPPING:
                        LOGV("camera cb: discarding preview frame "
                             "while stopping preview");
                        break;
                    default:
                        // transition to QCS_ERROR
                        LOGE("camera cb: invalid state %s for preview!",
                             obj->getCameraStateStr(obj->mCameraState));
                             if(CAMERA_SUCCESS != camera_release_frame(((camera_frame_type *)parm4)->buf_id)){
                                 LOGE("camera cb: fail to camera_release_frame().offset: %d.", ((camera_frame_type *)parm4)->buf_id);
                             }
                        break;
                    }
                    break;
                case CAMERA_EVT_CB_FD:
                    if( QCS_PREVIEW_IN_PROGRESS == obj->mCameraState) {
                        if (parm4)
                            obj->receivePreviewFDFrame((camera_frame_type *)parm4);
                    }
                    break;
                default:
                    // transition to QCS_ERROR
                    LOGE("unexpected cb %d for CAMERA_FUNC_START_PREVIEW.",
                         cb);
                }
                break;
            CAMERA_STATE(CAMERA_FUNC_START)
                TRANSITION(QCS_INIT, QCS_IDLE);
		LOGV("OK to QCS_INIT, QCS_IDLE");
                break;
            CAMERA_STATE(CAMERA_FUNC_STOP_PREVIEW)
                TRANSITION(QCS_INTERNAL_PREVIEW_STOPPING,
                           QCS_IDLE);
                break;
            CAMERA_STATE(CAMERA_FUNC_RELEASE_PICTURE)
                TRANSITION(QCS_INTERNAL_CAPTURE_STOPPING,
                           QCS_IDLE);
                 obj->FreeCameraMem();
                break;
            CAMERA_STATE(CAMERA_FUNC_TAKE_PICTURE)
                if (cb == CAMERA_RSP_CB_SUCCESS) {
		//	obj->notifyShutter();
                    TRANSITION(QCS_INTERNAL_RAW_REQUESTED,
                               QCS_WAITING_RAW);
                }
                else if (cb == CAMERA_EVT_CB_SNAPSHOT_DONE) {
                    obj->notifyShutter((camera_frame_type *)parm4);
                    // Received pre-LPM raw picture. Notify callback now.
                    obj->receiveRawPicture((camera_frame_type *)parm4);
                }
                else if (cb == CAMERA_EXIT_CB_DONE)
		{
		     {
                  //      Mutex::Autolock lock(&obj->mStateLock);
                        TRANSITION_LOCKED(QCS_WAITING_RAW,
                                          //obj->mJpegPictureCallback != NULL ?
                                          obj->mData_cb != NULL ?
                                          QCS_WAITING_JPEG :
                                          QCS_IDLE);
                    }
                    // It's important that we call receiveRawPicture() before
                    // we transition the state because another thread may be
                    // waiting in cancelPicture(), and then delete this object.
                    // If the order were reversed, we might call
                    // receiveRawPicture on a dead object.
                    LOGV("Receiving post LPM raw picture.");
                    obj->receivePostLpmRawPicture((camera_frame_type *)parm4);
                } else {  // transition to QCS_ERROR
                    if (obj->mCameraState == QCS_ERROR) {
                        Mutex::Autolock l(&obj->mLock);
                        Mutex::Autolock stateLock(&obj->mStateLock);
                        LOGE("camera cb: invalid state %s for taking a picture!",
                             obj->getCameraStateStr(obj->mCameraState));
                             obj->FreeCameraMem();
                        if (obj->mMsgEnabled & CAMERA_MSG_RAW_IMAGE)
                       		 obj->mData_cb(CAMERA_MSG_RAW_IMAGE, NULL, 0 , NULL, obj->mUser);
                        if (obj->mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE)
                        	obj->mData_cb(CAMERA_MSG_COMPRESSED_IMAGE, NULL, 0, NULL, obj->mUser);
                        TRANSITION_ALWAYS(QCS_IDLE);
                    }
                }
                break;
            CAMERA_STATE(CAMERA_FUNC_ENCODE_PICTURE)
                switch (cb) {
                case CAMERA_RSP_CB_SUCCESS:
                    // We already transitioned the camera state to
                    // QCS_WAITING_JPEG when we called
                    // camera_encode_picture().
                    break;
                case CAMERA_EXIT_CB_BUFFER:
                    if (obj->mCameraState == QCS_WAITING_JPEG) {
                        obj->receiveJpegPictureFragment(
                            (JPEGENC_CBrtnType *)parm4);
                    }
                    else LOGE("camera cb: invalid state %s for receiving "
                              "JPEG fragment!",
                              obj->getCameraStateStr(obj->mCameraState));
                    break;
                case CAMERA_EXIT_CB_DONE:
                    if (obj->mCameraState == QCS_WAITING_JPEG) {
                        // Receive the last fragment of the image.
                        obj->receiveJpegPictureFragment(
                            (JPEGENC_CBrtnType *)parm4);

                        // The size of the complete JPEG image is in
                        // mJpegSize.
			LOGV("CAMERA_EXIT_CB_DONE MID.");
                        // It's important that we call receiveJpegPicture()
                        // before we transition the state because another
                        // thread may be waiting in cancelPicture(), and then
                        // delete this object.  If the order were reversed, we
                        // might call receiveRawPicture on a dead object.

                        obj->receiveJpegPicture((JPEGENC_CBrtnType *)parm4);

						if( ((JPEGENC_CBrtnType *)parm4)->need_free ) {
                        	TRANSITION(QCS_WAITING_JPEG, QCS_IDLE);
						} else {
							TRANSITION(QCS_WAITING_JPEG, QCS_INTERNAL_RAW_REQUESTED);
						}
                    }
                    // transition to QCS_ERROR
                    else LOGE("camera cb: invalid state %s for "
                              "receiving JPEG!",
                              obj->getCameraStateStr(obj->mCameraState));
                    break;
                case CAMERA_EXIT_CB_FAILED:
                    obj->receiveJpegPictureError((JPEGENC_CBrtnType *)parm4);
                    break;
	       case CAMERA_EVT_CB_SNAPSHOT_JPEG_DONE:
			 	TRANSITION_LOCKED(QCS_WAITING_RAW,
                                          //obj->mJpegPictureCallback != NULL ?
                                          obj->mData_cb != NULL ?
                                          QCS_WAITING_JPEG :
                                          QCS_IDLE);

				if (obj->mCameraState == QCS_WAITING_JPEG)
				{
					obj->receiveJpegPosPicture();
					obj->receiveJpegPictureFragment((JPEGENC_CBrtnType *)parm4);
					obj->receiveJpegPicture((JPEGENC_CBrtnType *)parm4);
		                           TRANSITION(QCS_WAITING_JPEG, QCS_IDLE);
				}
				else LOGE("camera cb: invalid state %s for "
		                              "receiving JPEG!",
		                              obj->getCameraStateStr(obj->mCameraState));
		break;
                default:
                    // transition to QCS_ERROR
                    LOGE("camera cb: unknown cb %d for JPEG!", cb);
                }
            break;
            CAMERA_STATE(CAMERA_FUNC_START_FOCUS) {
                // NO TRANSITION HERE.  We acquire mStateLock here because it is
                // possible for ::autoFocus to be called after the call to
                // mAutoFocusCallback() but before we set mAutoFocusCallback
                // to NULL.
                //if (obj->mAutoFocusCallback) {
                if (obj->mNotify_cb) {
                    switch (cb) {
                    case CAMERA_RSP_CB_SUCCESS:
                        LOGV("camera cb: autofocus has started.");
                        break;
                    case CAMERA_EXIT_CB_DONE: {
                        LOGV("camera cb: autofocus succeeded.");
                        LOGV("camera cb: autofocus mNotify_cb start.");
			//if(NULL != obj->mNotify_cb)
			if (obj->mMsgEnabled & CAMERA_MSG_FOCUS)
				obj->mNotify_cb(CAMERA_MSG_FOCUS, 1, 0, obj->mUser);
			else
				LOGE("camera cb: mNotify_cb is null.");
 		   LOGV("camera cb: autofocus mNotify_cb ok.");
                    }
                        break;
                    case CAMERA_EXIT_CB_ABORT:
                        LOGE("camera cb: autofocus aborted");
                        break;
                    case CAMERA_EXIT_CB_FAILED: {
                        LOGE("camera cb: autofocus failed");
                        Mutex::Autolock lock(&obj->mStateLock);
                        if (obj->mMsgEnabled & CAMERA_MSG_FOCUS)
                        	obj->mNotify_cb(CAMERA_MSG_FOCUS, 0, 0, obj->mUser);
                    }
                        break;
                    default:
                        LOGE("camera cb: unknown cb %d for "
                             "CAMERA_FUNC_START_FOCUS!", cb);
                    }
                }
            } break;
        default:
            // transition to QCS_ERROR
            LOGE("Unknown camera-callback status %d", cb);
        }
    }

#undef TRANSITION
#undef TRANSITION_LOCKED
#undef TRANSITION_ALWAYS
#undef CAMERA_STATE

    static unsigned clp2(unsigned x) {
        x = x - 1;
        x = x | (x >> 1);
        x = x | (x >> 2);
        x = x | (x >> 4);
        x = x | (x >> 8);
        x = x | (x >>16);
        return x + 1;
    }

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
/*wxzN
    SprdCameraHardware::PmemPool::PmemPool(const char *pmem_pool,
                                               int buffer_size, int num_buffers,
                                               int frame_size,
                                               int frame_offset,
                                               const char *name) :
        SprdCameraHardware::MemPool(buffer_size,
                                        num_buffers,
                                        frame_size,
                                        frame_offset,
                                        name)
    {
        LOGV("constructing MemPool %s backed by pmem pool %s: "
             "%d frames @ %d bytes, offset %d, buffer size %d",
             mName,
             pmem_pool, num_buffers, frame_size, frame_offset,
             buffer_size);

        // Make a new mmap'ed heap that can be shared across processes.

        mAlignedSize = clp2(buffer_size * num_buffers);

        sp<MemoryHeapBase> masterHeap =
            new MemoryHeapBase(pmem_pool, mAlignedSize,  MemoryHeapBase::NO_CACHING);
        sp<MemoryHeapPmem> pmemHeap = new MemoryHeapPmem(masterHeap, MemoryHeapBase::NO_CACHING);
        if (pmemHeap->getHeapID() >= 0) {
            pmemHeap->slap();
            masterHeap.clear();
            mHeap = pmemHeap;
            pmemHeap.clear();

            mFd = mHeap->getHeapID();
            if (::ioctl(mFd, PMEM_GET_SIZE, &mSize)) {
                LOGE("pmem pool %s ioctl(PMEM_GET_SIZE) error %s (%d)",
                     pmem_pool,
                     ::strerror(errno), errno);
                mHeap.clear();
                return;
            }

            LOGV("pmem pool %s ioctl(PMEM_GET_SIZE) is %ld",
                 pmem_pool,
                 mSize.len);

            completeInitialization();
        }
        else LOGE("pmem pool %s error: could not create master heap!",
                  pmem_pool);
    }

*/
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

const char* const SprdCameraHardware::getCameraStateStr(
        SprdCameraHardware::Sprd_camera_state s)
    {
        static const char* states[] = {
#define STATE_STR(x) #x
            STATE_STR(QCS_INIT),
            STATE_STR(QCS_IDLE),
            STATE_STR(QCS_ERROR),
            STATE_STR(QCS_PREVIEW_IN_PROGRESS),
            STATE_STR(QCS_WAITING_RAW),
            STATE_STR(QCS_WAITING_JPEG),
            STATE_STR(QCS_INTERNAL_PREVIEW_STOPPING),
            STATE_STR(QCS_INTERNAL_PREVIEW_REQUESTED),
            STATE_STR(QCS_INTERNAL_RAW_REQUESTED),
            STATE_STR(QCS_INTERNAL_STOPPING),
#undef STATE_STR
        };
        return states[s];
    }
status_t SprdCameraHardware::sendCommand(int32_t cmd, int32_t arg1, int32_t arg2)
{
    uint32_t buffer_size = mPreviewWidth * mPreviewHeight * 3 / 2;
    uint32_t addr = 0;
    LOGE("sendCommand: facedetect mem size 0x%x.",buffer_size);
	if(CAMERA_CMD_START_FACE_DETECTION == cmd){
        addr = (uint32_t)malloc(buffer_size);
        mFDAddr = addr;
        camera_set_fd_mem(0,addr,buffer_size);
        camera_set_start_facedetect(1);
	} else if(CAMERA_CMD_STOP_FACE_DETECTION == cmd) {
	    LOGE("sendCommand: not support the CAMERA_CMD_STOP_FACE_DETECTION.");
        camera_set_start_facedetect(0);
//        FreePmem(mFDHeap);
//        mFDHeap = NULL;
        FreeFdmem();
	}
	LOGE("sendCommand end.");
	return NO_ERROR;
}


////////////////////////================//////////////////////////////

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
		if(NULL == (mMetadataHeap = mGetMemory_cb(-1, METADATA_SIZE, kPreviewBufferCount, NULL))){
			LOGE("fail to alloc memory for the metadata for storeMetaDataInBuffers.");
			return INVALID_OPERATION;
		}
	}
    mIsStoreMetaData = true;

    return NO_ERROR;
}

status_t SprdCameraHardware::setPreviewWindow(preview_stream_ops *w)
{
    int min_bufs;
    Mutex::Autolock stateLock(&mStateLock);
    Mutex::Autolock previewLock(&mPreviewLock);
    mSettingPreviewWindowState = PREVIEW_WINDOW_SETTING;
    mPreviewWindow = w;
    LOGV("%s: mPreviewWindow %p", __func__, mPreviewWindow);

    if (!w) {
        LOGE("preview window is NULL!");
        mPreviewStartFlag = 0;
        mSettingPreviewWindowState = PREVIEW_WINDOW_SET_IDLE;
        return NO_ERROR;
    }


    mPreviewStartFlag = 1;
    //if (mPreviewRunning && !mPreviewStartDeferred) {
    if (mCameraState == QCS_PREVIEW_IN_PROGRESS){
        LOGI("stop preview (window change)");
        stopPreviewInternal();
    }

    if (w->get_min_undequeued_buffer_count(w, &min_bufs)) {
        LOGE("%s: could not retrieve min undequeued buffer count", __func__);
	  mSettingPreviewWindowState = PREVIEW_WINDOW_SET_FAILED;
        return INVALID_OPERATION;
    }

    if (min_bufs >= kPreviewBufferCount) {
        LOGE("%s: min undequeued buffer count %d is too high (expecting at most %d)", __func__,
             min_bufs, kPreviewBufferCount - 1);
    }

    LOGV("%s: setting buffer count to %d", __func__, kPreviewBufferCount);
    if (w->set_buffer_count(w, kPreviewBufferCount)) {
        LOGE("%s: could not set buffer count", __func__);
       mSettingPreviewWindowState = PREVIEW_WINDOW_SET_FAILED;
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

	usage = GRALLOC_USAGE_SW_WRITE_OFTEN;

    if (preview_width > 640) {
#ifdef USE_ION_MEM
		usage |= GRALLOC_USAGE_PRIVATE_0;
#endif
		if (w->set_usage(w, usage )) {
        	LOGE("%s: could not set usage on gralloc buffer", __func__);
             mSettingPreviewWindowState = PREVIEW_WINDOW_SET_FAILED;
        	return INVALID_OPERATION;
    	}
    } else {
#ifdef USE_ION_MEM
		usage |= GRALLOC_USAGE_PRIVATE_0;
#endif
		if (w->set_usage(w, usage )) {
        	LOGE("%s: could not set usage on gralloc buffer", __func__);
		mSettingPreviewWindowState = PREVIEW_WINDOW_SET_FAILED;
        	return INVALID_OPERATION;
    	}
    }

    if (w->set_buffers_geometry(w,
                                preview_width, preview_height,
                                hal_pixel_format)) {
        LOGE("%s: could not set buffers geometry to %s",
             __func__, str_preview_format);
	  mSettingPreviewWindowState = PREVIEW_WINDOW_SET_FAILED;
        return INVALID_OPERATION;
    }

   /* if (mPreviewRunning && mPreviewStartDeferred) {
        LOGV("start/resume preview");
        status_t ret = startPreviewInternal();
        if (ret == OK) {
            mPreviewStartDeferred = false;
            mPreviewCondition.signal();
        }
    }
    mPreviewLock.unlock();*/

   status_t ret = startPreviewInternal();
   if (ret != NO_ERROR) {
      mSettingPreviewWindowState = PREVIEW_WINDOW_SET_FAILED;
   	return INVALID_OPERATION;
   }
   mSettingPreviewWindowState = PREVIEW_WINDOW_SET_OK;
    return NO_ERROR;
}

int SprdCameraHardware::getCameraId() const
{
    return g_camera_id;
}


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
    CameraParameters p(str);
    return obj(dev)->setParameters(p);
}

/** Return the camera parameters. */
char *HAL_camera_device_get_parameters(struct camera_device *dev)
{
    LOGV("%s", __func__);
    String8 str;
    CameraParameters parms = obj(dev)->getParameters();
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
    int num=0;
    char propBuf_atv[PROPERTY_VALUE_MAX];

    LOGV("%s", __func__);

    property_get("sys.camera.atv", propBuf_atv, "0");
	LOGD("HAL_getNumberOfCameras: property_atv: %s.", propBuf_atv);
    if((0 == strcmp(propBuf_atv, "1"))){
		num = sizeof(sCameraInfo3) / sizeof(sCameraInfo3[0]);
    }
    else{
        num = sizeof(sCameraInfo) / sizeof(sCameraInfo[0]);
    }

    LOGV("HAL_getNumberOfCameras,%d.\n",num);
    return num;
}

static int HAL_getCameraInfo(int cameraId, struct camera_info *cameraInfo)
{
    char propBuf_atv[PROPERTY_VALUE_MAX];
    property_get("sys.camera.atv", propBuf_atv, "0");
    LOGD("HAL_getCameraInfo: property_atv: %s.", propBuf_atv);
    if((0 == strcmp(propBuf_atv, "1"))){
		memcpy(cameraInfo, &sCameraInfo3[cameraId], sizeof(CameraInfo));
    }
    else{
        memcpy(cameraInfo, &sCameraInfo[cameraId], sizeof(CameraInfo));
    }
    return 0;
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
      },
      get_number_of_cameras : HAL_getNumberOfCameras,
      get_camera_info       : HAL_getCameraInfo
    };
}

}

