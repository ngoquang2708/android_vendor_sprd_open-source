LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
    modemd.c      \
    modemd_vlx.c  \
    modemd_ext.c  \
    modemd_sipc.c \
    nv_read.c

LOCAL_SHARED_LIBRARIES := \
    libcutils             \
    libhardware_legacy

ifeq ($(strip $(TARGET_USERIMAGES_USE_EXT4)),true)
LOCAL_CFLAGS := -DCONFIG_EMMC
endif

LOCAL_MODULE := modemd

LOCAL_MODULE_TAGS := optional

include $(BUILD_EXECUTABLE)
