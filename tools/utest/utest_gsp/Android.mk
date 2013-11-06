ifneq ($(shell ls -d vendor/sprd/proprietories-source 2>/dev/null),)



LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

ifeq ($(strip $(TARGET_BOARD_PLATFORM)),sc8830xxx)



LOCAL_MODULE := utest_gsp
LOCAL_MODULE_TAGS := debug
LOCAL_CFLAGS := -fno-strict-aliasing -DCHIP_ENDIAN_LITTLE
LOCAL_PRELINK_MODULE := false
LOCAL_ARM_MODE := arm



LOCAL_SRC_FILES := utest_gsp.cpp 
LOCAL_SRC_FILES += gsp_hal.c

	

LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL/usr/include/video

LOCAL_SHARED_LIBRARIES := libutils libbinder
LOCAL_STATIC_LIBRARIES := libsprdm4vencoder

LOCAL_MODULE_PATH := $(TARGET_OUT_OPTIONAL_EXECUTABLES)


include $(BUILD_EXECUTABLE)



endif

endif
