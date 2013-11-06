LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := utest_vsp_m4vh263enc
LOCAL_MODULE_TAGS := debug
LOCAL_CFLAGS := -fno-strict-aliasing -D_VSP_LINUX_ -D_VSP_
LOCAL_PRELINK_MODULE := false
LOCAL_ARM_MODE := arm

LOCAL_SRC_FILES := m4vh263enc.cpp \
		../../../util/util.c

LOCAL_C_INCLUDES := $(LOCAL_PATH) \
		$(LOCAL_PATH)/../../../util

LOCAL_SHARED_LIBRARIES := libutils libbinder libdl

LOCAL_MODULE_PATH := $(TARGET_OUT_OPTIONAL_EXECUTABLES)

include $(BUILD_EXECUTABLE)

