LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
	wcnd.c \

LOCAL_SHARED_LIBRARIES := \
	libcutils

LOCAL_MODULE := wcnd

LOCAL_MODULE_TAGS := optional

include $(BUILD_EXECUTABLE)
