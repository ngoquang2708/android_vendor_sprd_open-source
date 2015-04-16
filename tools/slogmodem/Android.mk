LOCAL_PATH:= $(call my-dir)


#slogmodem
include $(CLEAR_VARS)
LOCAL_SRC_FILES := cp_config.c \
		   modem.c \
		   modem_common.c

ifeq ($(strip $(SPRD_EXTERNAL_WCN)),true)
LOCAL_CFLAGS += -DEXTERNAL_WCN
endif

LOCAL_MODULE := slogmodem
LOCAL_MODULE_TAGS := optional
LOCAL_C_INCLUDES += external/jpeg external/zlib
LOCAL_CFLAGS += -std=c99
LOCAL_SHARED_LIBRARIES := libc libcutils liblog libz libjpeg
include $(BUILD_EXECUTABLE)

CUSTOM_MODULES += slogmodem
