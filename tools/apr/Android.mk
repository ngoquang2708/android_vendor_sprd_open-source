LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := collect_apr
LOCAL_MODULE_TAGS := debug

LOCAL_STATIC_LIBRARIES := libxml2
LOCAL_SHARED_LIBRARIES := libcutils libhardware
LOCAL_SHARED_LIBRARIES += libicuuc
LOCAL_SHARED_LIBRARIES += libc

LOCAL_SRC_FILES := main.cpp
LOCAL_SRC_FILES += Observable.cpp
LOCAL_SRC_FILES += Observer.cpp
LOCAL_SRC_FILES += AprData.cpp
LOCAL_SRC_FILES += XmlStorage.cpp
LOCAL_SRC_FILES += Thread.cpp
LOCAL_SRC_FILES += AnrThread.cpp
LOCAL_SRC_FILES += ModemThread.cpp
LOCAL_SRC_FILES += common.c

LOCAL_LDLIBS := -L$(SYSROOT)/usr/lib -llog -lpthread -lxml
LOCAL_LDLIBS += prebuilts/ndk/current/sources/cxx-stl/stlport/libs/armeabi/libstlport_static.a
#LOCAL_LDLIBS += static

LOCAL_CFLAGS := -D_STLP_USE_NO_IOSTREAMS
LOCAL_CFLAGS += -D_STLP_USE_MALLOC

LOCAL_C_INCLUDES := $(LOCAL_PATH)/inc
LOCAL_C_INCLUDES += prebuilts/ndk/current/sources/cxx-stl/stlport/stlport
LOCAL_C_INCLUDES += external/libxml2/include
LOCAL_C_INCLUDES += external/icu4c/common

include $(BUILD_EXECUTABLE)

