LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := libuvdenoise
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_PATH := $(TOP)/vendor/sprd/open-source/libs/libcamera/sc8830/isp
LOCAL_SHARED_LIBRARIES := liblog libutils libcutils


#LOCAL_CFLAGS = -g -mfpu=neon -O3 -mcpu=cortex-a7
LOCAL_ARM_MODE := arm

LOCAL_SRC_FILES := \
		denoise_alg0.c

		
LOCAL_C_INCLUDES := $(LOCAL_PATH)/../inc \
			 $(LOCAL_PATH)/../../inc

include $(BUILD_SHARED_LIBRARY)

