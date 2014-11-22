LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
	download.c packet.c crc16.c connectivity_rf_parameters.c

LOCAL_SHARED_LIBRARIES := \
	libcutils

LOCAL_MODULE := download

LOCAL_MODULE_TAGS := optional

include $(BUILD_EXECUTABLE)
