LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := collect_apr
LOCAL_MODULE_TAGS := debug
LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_SRC_FILES := collect_apr
include $(BUILD_PREBUILT)

