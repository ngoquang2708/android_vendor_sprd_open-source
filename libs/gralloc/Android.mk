# 
# Copyright (C) 2010 ARM Limited. All rights reserved.
# 
# Copyright (C) 2008 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

ifneq ($(TARGET_SIMULATOR),true)
LOCAL_PATH := $(call my-dir)

# HAL module implemenation, not prelinked and stored in
# hw/<OVERLAY_HARDWARE_MODULE_ID>.<ro.product.board>.so
include $(CLEAR_VARS)
LOCAL_PRELINK_MODULE := false
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw

MALI_DDK_TEST_PATH := hardware/arm/

ifeq ($(strip $(TARGET_BOARD_PLATFORM)),sc8830)
	DEVICE_USE_3_FRAMEBUFFER := true
else
	DEVICE_USE_3_FRAMEBUFFER := false
endif

ifeq ($(strip $(DEVICE_USE_3_FRAMEBUFFER)), true)
	LOCAL_CFLAGS += -DUSE_3_FRAMEBUFFER
endif

LOCAL_MODULE := gralloc.$(TARGET_BOARD_PLATFORM)
LOCAL_MODULE_TAGS := optional

# Which DDK are we building for?
ifneq (,$(wildcard $(MALI_DDK_TEST_PATH)))
# Mali-T6xx DDK
MALI_DDK_PATH := vendor/arm/mali6xx
LOCAL_SHARED_LIBRARIES := liblog libcutils libGLESv1_CM libion

# All include files are accessed from the DDK root
DDK_PATH := $(LOCAL_PATH)/../../..
UMP_HEADERS_PATH := $(DDK_PATH)/kernel/include
LOCAL_C_INCLUDES := $(DDK_PATH) $(UMP_HEADERS_PATH)

LOCAL_CFLAGS := -DLOG_TAG=\"gralloc.$(TARGET_BOARD_PLATFORM)\" -DMALI_600
# -DSTANDARD_LINUX_SCREEN
else
# Mali-200/300/400MP DDK
MALI_DDK_PATH := vendor/sprd/open-source/libs
#SHARED_MEM_LIBS := libUMP
SHARED_MEM_LIBS := libion libhardware
LOCAL_SHARED_LIBRARIES := liblog libcutils libGLESv1_CM $(SHARED_MEM_LIBS)

# Include the UMP header files
LOCAL_C_INCLUDES := $(MALI_DDK_PATH)/mali/src/ump/include
LOCAL_C_INCLUDES += \
    $(TARGET_OUT_INTERMEDIATES)/KERNEL/usr/include/video/ \
    $(TARGET_OUT_INTERMEDIATES)/KERNEL/
LOCAL_CFLAGS:= -DLOG_TAG=\"gralloc.$(TARGET_BOARD_PLATFORM)\"
# -DGRALLOC_32_BITS -DSTANDARD_LINUX_SCREEN -DPLATFORM_SDK_VERSION=$(PLATFORM_SDK_VERSION)

endif

ifeq ($(strip $(USE_RGB_VIDEO_LAYER)) , true)
ifeq ($(strip $(TARGET_BOARD_PLATFORM)),sc8810)
        LOCAL_CFLAGS += -DDRM_SPECIAL_PROCESS
endif
endif

ifeq ($(strip $(USE_UI_OVERLAY)),true)
        LOCAL_CFLAGS += -DUSE_UI_OVERLAY
endif

ifneq ($(strip $(TARGET_BUILD_VARIANT)), user)
        LOCAL_CFLAGS += -DDUMP_FB
endif

ifeq ($(strip $(USE_SPRD_DITHER)) , true)
LOCAL_SHARED_LIBRARIES += libdither
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../dither/
LOCAL_CFLAGS += -DSPRD_DITHER_ENABLE
endif

LOCAL_SRC_FILES := \
	gralloc_module.cpp \
	alloc_device.cpp \
	framebuffer_device.cpp \
	dump_bmp.cpp

#LOCAL_CFLAGS+= -DMALI_VSYNC_EVENT_REPORT_ENABLE
include $(BUILD_SHARED_LIBRARY)
endif
