LOCAL_PATH:= $(call my-dir)

#slog
include $(CLEAR_VARS)
LOCAL_SRC_FILES := slog.c \
				common.c \
				parse_conf.c \
				screenshot.c \
				android.c \
				modem.c \
				snap.c \
				tcp.c \
				bt.c
LOCAL_MODULE := slog
LOCAL_STATIC_LIBRARIES := libcutils libc
LOCAL_MODULE_TAGS := optional
LOCAL_LDLIBS += -lpthread
LOCAL_C_INCLUDES += external/jpeg external/zlib
LOCAL_SHARED_LIBRARIES := liblog libz libjpeg
include $(BUILD_EXECUTABLE)

#slogctl
include $(CLEAR_VARS)
LOCAL_SRC_FILES := slogctl.c \
			common.c
LOCAL_MODULE := slogctl
LOCAL_STATIC_LIBRARIES := libcutils libc
LOCAL_MODULE_TAGS := optional
LOCAL_LDLIBS += -lpthread
LOCAL_C_INCLUDES += external/jpeg external/zlib
LOCAL_SHARED_LIBRARIES := liblog libz libjpeg
include $(BUILD_EXECUTABLE)

#tar
include $(CLEAR_VARS)
LOCAL_MODULE := tar
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_OUT_EXECUTABLES)
LOCAL_SRC_FILES := $(LOCAL_MODULE)
include $(BUILD_PREBUILT)

#tcp
include $(CLEAR_VARS)
LOCAL_MODULE := tcp
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_OUT_EXECUTABLES)
LOCAL_SRC_FILES := $(LOCAL_MODULE)
include $(BUILD_PREBUILT)


include $(CLEAR_VARS)
LOCAL_MODULE := slog.conf
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_PATH := $(TARGET_OUT_ETC)
LOCAL_SRC_FILES := $(LOCAL_MODULE)
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE := slog.conf.user
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_PATH := $(TARGET_OUT_ETC)
LOCAL_SRC_FILES := $(LOCAL_MODULE)
include $(BUILD_PREBUILT)

CUSTOM_MODULES += slog
CUSTOM_MODULES += slogctl
CUSTOM_MODULES += tar
CUSTOM_MODULES += tcp
CUSTOM_MODULES += slog.conf
CUSTOM_MODULES += slog.conf.user
