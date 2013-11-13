ifneq ($(TARGET_SIMULATOR),true)

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_PRELINK_MODULE    := false
LOCAL_SHARED_LIBRARIES  := libcutils libsqlite libhardware libhardware_legacy libvbeffect libvbpga libnvexchange libatchannel
LOCAL_STATIC_LIBRARIES  :=
LOCAL_LDLIBS        += -Idl
ifeq ($(strip $(BOARD_USE_EMMC)),true)
LOCAL_CFLAGS += -DCONFIG_EMMC
endif

ifeq ($(USE_BOOT_AT_DIAG),true)
LOCAL_CFLAGS += -DUSE_BOOT_AT_DIAG
endif

LOCAL_C_INCLUDES    +=  external/sqlite/dist/
LOCAL_C_INCLUDES    +=  vendor/sprd/open-source/libs/libatchannel/
LOCAL_C_INCLUDES    +=  vendor/sprd/open-source/libs/audio/nv_exchange/
LOCAL_C_INCLUDES    +=  vendor/sprd/open-source/libs/audio/
LOCAL_SRC_FILES     := eng_pcclient.c  \
		       eng_diag.c \
		       vlog.c \
		       vdiag.c \
		       bt_eut.c \
		       wifi_eut_shark.c \
		       eng_productdata.c \
		       gps_eut.c \
		       adc_calibration.c\
		       crc16.c \
		       eng_attok.c \
		       engopt.c \
		       eng_at.c \
               eng_sqlite.c \
               eng_btwifiaddr.c \
               eng_cmd4linuxhdlr.c \
               eng_testhardware.c \
               power.c \
               backlight.c \
               eng_util.c

LOCAL_MODULE := engpc
LOCAL_MODULE_TAGS := optional
include $(BUILD_EXECUTABLE)

endif
