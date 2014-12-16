LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

#ifeq ($(BOARD_WLAN_DEVICE), sprdwl)
LOCAL_CFLAGS += -DUSE_MARLIN
#endif

LOCAL_SRC_FILES:= \
	wcnd.c \
	wcnd_cmd.c \
	wcnd_worker.c \
	wcnd_sm.c \
	wcnd_eng_cmd_executer.c \
	wcnd_download.c

LOCAL_SHARED_LIBRARIES := \
	libcutils \
	libiwnpi \
	libengbt


#ifeq ($(BOARD_WLAN_DEVICE), bcmdhd)
LOCAL_CFLAGS += -DHAVE_SLEEPMODE_CONFIG
#endif

LOCAL_SRC_FILES += wcnd_eng_wifi_priv.c


LOCAL_MODULE := wcnd

LOCAL_MODULE_TAGS := optional

LOCAL_REQUIRED_MODULES := wcnd_cli

include $(BUILD_EXECUTABLE)


#build wcnd_cli
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
	wcnd_cli.c


LOCAL_SHARED_LIBRARIES := \
	libcutils

LOCAL_MODULE := wcnd_cli

LOCAL_MODULE_TAGS := optional

include $(BUILD_EXECUTABLE)
