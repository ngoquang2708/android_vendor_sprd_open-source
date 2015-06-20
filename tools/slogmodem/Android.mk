# CP log
LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := slogmodem
LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES := client_hdl.cpp \
		   client_mgr.cpp \
		   client_req.cpp \
		   cp_dir.cpp \
		   cp_dump.cpp \
		   cp_log_cmn.cpp \
		   cp_log.cpp \
		   cp_ringbuf.cpp \
		   cp_set_dir.cpp \
		   cp_sleep_log.cpp \
		   cp_stat_hdl.cpp \
		   cp_stor.cpp \
		   data_consumer.cpp \
		   data_proc_hdl.cpp \
		   dev_file_hdl.cpp \
		   dev_file_open.cpp \
		   diag_dev_hdl.cpp \
		   diag_stream_parser.cpp \
		   ext_wcn_dump.cpp \
		   fd_hdl.cpp \
		   file_watcher.cpp \
		   log_config.cpp \
		   log_ctrl.cpp \
		   log_file.cpp \
		   log_pipe_dev.cpp \
		   log_pipe_hdl.cpp \
		   media_stor.cpp \
		   modem_dump.cpp \
		   modem_stat_hdl.cpp \
		   multiplexer.cpp \
		   parse_utils.cpp \
		   slog_config.cpp \
		   stor_mgr.cpp \
		   timer_mgr.cpp

ifeq ($(strip $(SPRD_EXTERNAL_WCN)), true)
	LOCAL_CFLAGS += -DEXTERNAL_WCN
	LOCAL_SRC_FILES += ext_wcn_stat_hdl.cpp
else
	LOCAL_SRC_FILES += int_wcn_stat_hdl.cpp
endif

LOCAL_SHARED_LIBRARIES := libc \
			  libcutils \
			  liblog \
			  libutils
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
