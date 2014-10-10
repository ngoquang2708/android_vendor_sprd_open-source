LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
	nandflash_bbctest.c

LOCAL_SHARED_LIBRARIES := \
    libcutils

LOCAL_MODULE := nandflash_bbctest
LOCAL_MODULE_TAGS := debug

include $(BUILD_EXECUTABLE)
