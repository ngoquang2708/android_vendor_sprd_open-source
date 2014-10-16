
BUILD_CAMERA_TEST:=1

ifeq ($(BUILD_CAMERA_TEST),1)

LOCAL_PATH:=$(call my-dir)
include $(CLEAR_VARS)

LOCAL_CFLAGS:=

LOCAL_SRC_FILES:= \
	at_camera.c

LOCAL_C_INCLUDES:=$(LOCAL_PATH)

LOCAL_SHARED_LIBRARIES:= \
	libcutils

LOCAL_SHARED_LIBRARIES += libdl

LOCAL_MODULE:=at_camera
LOCAL_MODULE_TAGS := optional
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_C_INCLUDES:=$(LOCAL_PATH)

endif

