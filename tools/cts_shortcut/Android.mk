LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := cts_shortcut.c
LOCAL_MODULE := cts_shortcut
LOCAL_STATIC_LIBRARIES := libcutils
LOCAL_MODULE_TAGS := optional

include $(BUILD_EXECUTABLE)
