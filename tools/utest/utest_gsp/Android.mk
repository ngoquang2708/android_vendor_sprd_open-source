ifneq ($(shell ls -d vendor/sprd/proprietories-source 2>/dev/null),)



LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

ifeq ($(strip $(TARGET_BOARD_PLATFORM)),scx30gxxx)



LOCAL_MODULE := utest_gsp
LOCAL_MODULE_TAGS := debug
LOCAL_CFLAGS := -fno-strict-aliasing -DCHIP_ENDIAN_LITTLE
LOCAL_PRELINK_MODULE := false
LOCAL_ARM_MODE := arm

LOAD_GSP_HAL_SO := true #so switch

LOCAL_SRC_FILES := utest_gsp.cpp MemoryHeapIon.cpp

ifeq ($(strip $(LOAD_GSP_HAL_SO)),true)
LOCAL_CFLAGS += -DGSP_INTF_SO
else
LOCAL_SRC_FILES += gsp_hal.cpp
endif


LOCAL_C_INCLUDES := \
$(TOP)/vendor/sprd/open-source/libs/hwcomposer \
$(TOP)/vendor/sprd/open-source/libs/hwcomposer/sc8830 \
$(TARGET_OUT_INTERMEDIATES)/KERNEL/usr/include/video \
$(TOP)/vendor/sprd/open-source/libs/gralloc \

LOCAL_SHARED_LIBRARIES := liblog libEGL libGLESv1_CM  libui

LOCAL_SHARED_LIBRARIES += \
	libcutils \
	libhardware \
	libutils \
	libbinder
# LOCAL_STATIC_LIBRARIES := libsprdm4vencoder

LOCAL_MODULE_PATH := $(TARGET_OUT_OPTIONAL_EXECUTABLES)


include $(BUILD_EXECUTABLE)

include $(call all-makefiles-under,$(LOCAL_PATH))

endif

endif
