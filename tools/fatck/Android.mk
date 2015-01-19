BUILD_FATCK := false

ifeq ($(BUILD_FATCK), true)

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := boot.c  fatck.c

LOCAL_C_INCLUDES := external/fsck_msdos/

LOCAL_CFLAGS := -O2 -g -W -Wall -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64

LOCAL_MODULE := fatck
LOCAL_MODULE_TAGS :=
LOCAL_SYSTEM_SHARED_LIBRARIES := libc

LOCAL_MODULE_TAGS := debug

include $(BUILD_EXECUTABLE)

endif
