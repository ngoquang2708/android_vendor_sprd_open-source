LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
	nandflash_rwtest.c

LOCAL_SHARED_LIBRARIES := \
    libcutils

LOCAL_MODULE := nandflash_rwtest
LOCAL_MODULE_TAGS := debug

include $(BUILD_EXECUTABLE)
