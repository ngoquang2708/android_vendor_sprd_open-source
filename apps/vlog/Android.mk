LOCAL_PATH:= $(call my-dir)
#vlog server, use socket to put modem lot to PC
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
	vlog-sv.c

LOCAL_SHARED_LIBRARIES := \
    libutils \
    libcutils

LOCAL_MODULE := vlog-sv
LOCAL_MODULE_TAGS := debug

include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
	vlog-iq.c

LOCAL_SHARED_LIBRARIES := \
    libutils

LOCAL_MODULE := vlog-iq
LOCAL_MODULE_TAGS := debug

include $(BUILD_EXECUTABLE)
