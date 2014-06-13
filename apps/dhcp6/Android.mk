# Copyright 2006 The Android Open Source Project
LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)
LOCAL_SRC_FILES :=  dhcp6s.c common.c if.c ifaddrs.c config.c timer.c lease.c \
                    base64.c auth.c dhcp6_ctl.c cfparse.c cftoken.c
LOCAL_MODULE := dhcp6s
LOCAL_MODULE_TAGS := optional
LOCAL_SHARED_LIBRARIES := libc libcutils libnetutils
LOCAL_CFLAGS := -DMODULE \
                -DREDIRECT_SYSLOG_TO_ANDROID_LOGCAT \
                -DANDROID_CHANGES
include $(BUILD_EXECUTABLE)
