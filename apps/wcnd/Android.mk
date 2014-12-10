LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

#ifeq ($(BOARD_WLAN_DEVICE), sc2331)
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

LOCAL_MODULE := wcnd

LOCAL_MODULE_TAGS := optional

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
