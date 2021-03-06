LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := mali.ko
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/modules
LOCAL_SRC_FILES := mali/$(LOCAL_MODULE)
include $(BUILD_PREBUILT)

ifeq ($(TARGET_BUILD_VARIANT),user)
  DEBUGMODE := BUILD=no
else
  DEBUGMODE := $(DEBUGMODE)
endif

$(LOCAL_PATH)/mali/mali.ko: bootimage
	$(MAKE) -C $(shell dirname $@) MALI_PLATFORM=$(TARGET_BOARD_PLATFORM) USING_PP_CORE=$(TARGET_GPU_PP_CORE) MALI_GPU_BASE_FREQ=$(TARGET_GPU_BASE_FREQ) $(DEBUGMODE) KDIR=$(ANDROID_PRODUCT_OUT)/obj/KERNEL clean
	$(MAKE) -C $(shell dirname $@) MALI_PLATFORM=$(TARGET_BOARD_PLATFORM) USING_PP_CORE=$(TARGET_GPU_PP_CORE) MALI_GPU_BASE_FREQ=$(TARGET_GPU_BASE_FREQ) $(DEBUGMODE) KDIR=$(ANDROID_PRODUCT_OUT)/obj/KERNEL
