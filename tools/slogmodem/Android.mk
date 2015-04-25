# CP log
LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := slogmodem
LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES := client_hdl.cpp \
		client_mgr.cpp \
		client_req.cpp \
		cp_log_cmn.cpp \
		cp_log.cpp \
		cp_stat_hdl.cpp \
		data_proc_hdl.cpp \
		fd_hdl.cpp \
		file_mgr.cpp \
		log_config.cpp \
		log_ctrl.cpp \
		log_dir.cpp \
		log_file_mgr.cpp \
		log_pipe_dev.cpp \
		log_pipe_hdl.cpp \
		log_stat.cpp \
		modem_stat_hdl.cpp \
		multiplexer.cpp \
		parse_utils.cpp \
		slog_config.cpp \
		total_dir_stat.cpp

LOCAL_C_INCLUDES += external/zlib

ifeq ($(strip $(SPRD_EXTERNAL_WCN)), true)
	LOCAL_CFLAGS += -DEXTERNAL_WCN
	LOCAL_SRC_FILES += ext_wcn_log_hdl.cpp \
			   ext_wcn_stat_hdl.cpp
else
	LOCAL_SRC_FILES += int_wcn_log_hdl.cpp \
			   int_wcn_stat_hdl.cpp
endif

LOCAL_SHARED_LIBRARIES := libc \
			libcutils \
			liblog \
			libutils \
			libz
LOCAL_CPPFLAGS += -std=c++11
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := slog_modem.conf
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_PATH := $(TARGET_OUT_ETC)
LOCAL_SRC_FILES := $(LOCAL_MODULE)
include $(BUILD_PREBUILT)

CUSTOM_MODULES += slogmodem slog_modem.conf
