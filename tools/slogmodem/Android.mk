LOCAL_PATH:= $(call my-dir)


#slogmodem
include $(CLEAR_VARS)
LOCAL_SRC_FILES := modem.c \
		modem_common.c

ifeq ($(strip $(SPRD_EXTERNAL_WCN)),true)
LOCAL_CFLAGS += -DEXTERNAL_WCN
endif

LOCAL_MODULE := slogmodem
LOCAL_STATIC_LIBRARIES := libcutils libc
LOCAL_MODULE_TAGS := optional
LOCAL_LDLIBS += -lpthread
LOCAL_C_INCLUDES += external/jpeg external/zlib
LOCAL_SHARED_LIBRARIES := liblog libz libjpeg
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := slog_modem.conf
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_PATH := $(TARGET_OUT_ETC)
LOCAL_SRC_FILES := $(LOCAL_MODULE)
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE := slog_modem.conf.user
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_PATH := $(TARGET_OUT_ETC)
LOCAL_SRC_FILES := $(LOCAL_MODULE)
include $(BUILD_PREBUILT)




CUSTOM_MODULES += slogmodem
CUSTOM_MODULES += slog_modem.conf
CUSTOM_MODULES += slog_modem.conf.user


