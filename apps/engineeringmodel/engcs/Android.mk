ifneq ($(TARGET_SIMULATOR),true)
#ENGSERVER
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_PRELINK_MODULE    := false
LOCAL_SHARED_LIBRARIES  := libcutils
LOCAL_STATIC_LIBRARIES  := 
LOCAL_LDLIBS        += -Idl
LOCAL_CFLAGS        += -static

LOCAL_C_INCLUDES    += engservice.h \
		                        engopt.h
		                        
LOCAL_SRC_FILES     := engservice.c \
		                       engopt.c \
		                       fdevent.c

LOCAL_MODULE := engservice
LOCAL_MODULE_TAGS := optional
include $(BUILD_EXECUTABLE)

#LIBENGCLIENT
CAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_PRELINK_MODULE := false

LOCAL_LDLIBS        += -Idl

#LOCAL_CFLAGS += -DENG_API_LOG

LOCAL_SHARED_LIBRARIES := libcutils

LOCAL_C_INCLUDES  += engopt.h \
					engapi.h

LOCAL_SRC_FILES     :=   engopt.c \
		       	          engapi.c \
       				   eng_attok.c \
	      				   engparcel.c \
	      				   engclient.c \
				  eng_common.c \
		       	          eng_modemclient.c \
                          engphasecheck.c \
		       	          eng_appclient.c

LOCAL_MODULE:= libengclient
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)


#LIBENG_WIFI_PTEST
CAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_PRELINK_MODULE := false

LOCAL_LDLIBS        += -Idl

#LOCAL_CFLAGS += -DENG_API_LOG

LOCAL_SHARED_LIBRARIES := libcutils

LOCAL_C_INCLUDES  += eng_wifi_ptest.h

LOCAL_SRC_FILES     :=   eng_wifi_ptest.c

LOCAL_MODULE:= libeng_wifi_ptest
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)


#ENGMODEMCLIENT
CAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_PRELINK_MODULE    := false
LOCAL_SHARED_LIBRARIES  := libcutils
LOCAL_STATIC_LIBRARIES  :=
LOCAL_LDLIBS        += -Idl
LOCAL_CFLAGS        += -static 

LOCAL_C_INCLUDES    += engclient.h \
             			          engopt.h
             			          
LOCAL_SRC_FILES     := engclient.c \
       	       	          engopt.c \
      	       		   eng_attok.c \
			eng_common.c \
   			   engparcel.c \
	       	          eng_modemclient.c 

LOCAL_MODULE := engmodemclient
LOCAL_MODULE_TAGS := optional
include $(BUILD_EXECUTABLE)


#ENGAPPCLIENT
CAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_PRELINK_MODULE    := false
LOCAL_SHARED_LIBRARIES  := libcutils
LOCAL_STATIC_LIBRARIES  :=
LOCAL_LDLIBS        += -Idl
LOCAL_CFLAGS        += -static

LOCAL_C_INCLUDES    += engclient.h \
             			          engopt.h
             			          
LOCAL_SRC_FILES     := eng_appclient.c \
		       	          engopt.c \
			                 eng_attok.c \
			                 engparcel.c \
		       	          engclient.c \
		       	          eng_appclienttest.c

LOCAL_MODULE := engappclient
LOCAL_MODULE_TAGS := optional
include $(BUILD_EXECUTABLE)


#ENGPCCLIENT
CAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_PRELINK_MODULE    := false
LOCAL_SHARED_LIBRARIES  := libcutils libsqlite libhardware libhardware_legacy libvbeffect	libvbpga libnvexchange
LOCAL_STATIC_LIBRARIES  :=
LOCAL_LDLIBS        += -Idl
ifeq ($(strip $(BOARD_USE_EMMC)),true)
LOCAL_CFLAGS += -DCONFIG_EMMC
endif

ifeq ($(USE_BOOT_AT_DIAG),true)
LOCAL_CFLAGS += -DUSE_BOOT_AT_DIAG
endif

LOCAL_C_INCLUDES    +=  external/sqlite/dist/

LOCAL_C_INCLUDES    +=  engphasecheck.h

LOCAL_SRC_FILES     := eng_pcclient.c  \
           eng_testhardware.c \
			eng_common.c \
			engapi.c \
			eng_cmd4linuxhdlr.c \
            backlight.c \
			eng_appclient.c  \
			engclient.c \
			engopt.c \
			eng_sqlite.c \
			crc16.c \
			power.c \
			eng_attok.c \
			eng_diag.c \
			vlog.c \
			vdiag.c \
			engphasecheck.c\
			eng_sd_log.c \
			bt_eut.c \
			wifi_eut.c \
			gps_eut.c \
			adc_calibration.c\
			eng_at_trans.c \

LOCAL_MODULE := engpcclient
LOCAL_MODULE_TAGS := optional
include $(BUILD_EXECUTABLE)

#ENG SETBTWIFI ADDR
CAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_PRELINK_MODULE    := false
LOCAL_SHARED_LIBRARIES  := libcutils libsqlite libengclient
LOCAL_STATIC_LIBRARIES  :=
LOCAL_LDLIBS        += -Idl

LOCAL_C_INCLUDES    +=  external/sqlite/dist/

LOCAL_SRC_FILES     := eng_setbtwifiaddr.c   \
		       eng_sqlite.c	

LOCAL_MODULE := engsetmacaddr

LOCAL_MODULE_TAGS := optional
include $(BUILD_EXECUTABLE)


#ENG TEST WIFI AND GPS
#CAL_PATH := $(call my-dir)

#include $(CLEAR_VARS)
#LOCAL_PRELINK_MODULE    := false
#LOCAL_SHARED_LIBRARIES  := libcutils libhardware_legacy libutils
#LOCAL_STATIC_LIBRARIES  :=
#LOCAL_LDLIBS        += -Idl
#LOCAL_CFLAGS        += -D$(BOARD_PRODUCT_NAME)


#LOCAL_SRC_FILES     := testhardware.c

#LOCAL_MODULE := testhardware
#LOCAL_MODULE_TAGS := optional
#include $(BUILD_EXECUTABLE)


#ENGHARDWARETEST
CAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)
LOCAL_PRELINK_MODULE := false
LOCAL_LDLIBS        += -Idl
#LOCAL_CFLAGS += -DENG_API_LOG

LOCAL_SHARED_LIBRARIES := libcutils

LOCAL_C_INCLUDES  += eng_hardware_test.h \
		                        engopt.h

LOCAL_SRC_FILES     :=   eng_hardware_test.c \
			wifi_eut.c \
			bt_eut.c \
		    engopt.c

LOCAL_MODULE:= enghardwaretest
LOCAL_MODULE_TAGS := optional

include $(BUILD_EXECUTABLE)
include $(call all-makefiles-under,$(LOCAL_PATH))
endif
