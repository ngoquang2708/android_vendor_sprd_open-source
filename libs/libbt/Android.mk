LOCAL_PATH := $(call my-dir)

ifeq ($(BOARD_HAVE_BLUETOOTH_SPRD),true)

include $(CLEAR_VARS)

BDROID_DIR := $(TOP_DIR)external/bluetooth/bluedroid

LOCAL_SRC_FILES := \
        src/bt_vendor_sprd.c \
        src/hardware.c \
        src/userial_vendor.c \
	src/pskey_get.c

LOCAL_C_INCLUDES += \
        $(LOCAL_PATH)/include \
        $(BDROID_DIR)/hci/include

LOCAL_SHARED_LIBRARIES := \
        libcutils \
        liblog

ifeq ($(findstring 8830,$(TARGET_BOOTLOADER_BOARD_NAME)),8830)
      LOCAL_CFLAGS += -DHW_ADC_ADAPT_SUPPORT
endif

ifeq ($(findstring 7715,$(TARGET_BOOTLOADER_BOARD_NAME)),7715)
      LOCAL_CFLAGS += -DHW_ADC_ADAPT_SUPPORT
endif

ifeq ($(findstring 8815,$(TARGET_BOOTLOADER_BOARD_NAME)),8815)
      LOCAL_CFLAGS += -DHW_ADC_ADAPT_SUPPORT
endif

ifeq ($(findstring 5735,$(TARGET_BOOTLOADER_BOARD_NAME)),5735)
      LOCAL_CFLAGS += -DHW_ADC_ADAPT_SUPPORT
endif

LOCAL_MODULE := libbt-vendor
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_MODULE_OWNER := sprd
LOCAL_MODULE_PATH := $(TARGET_OUT_VENDOR_SHARED_LIBRARIES)

include $(LOCAL_PATH)/vnd_buildcfg.mk

include $(BUILD_SHARED_LIBRARY)

endif
