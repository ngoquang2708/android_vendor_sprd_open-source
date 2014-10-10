LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES := ubiwltest.c

LOCAL_MODULE := ubiwltest

include $(BUILD_EXECUTABLE)
