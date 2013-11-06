ifneq ($(TARGET_SIMULATOR),true)

LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES := $(call all-subdir-java-files) \
                        src/com/spreadtrum/android/eng/ISlogService.aidl \

LOCAL_PACKAGE_NAME := engineeringmodel

LOCAL_PROGUARD_ENABLED := full

LOCAL_CERTIFICATE := platform

include $(BUILD_PACKAGE)

include $(call all-makefiles-under,$(LOCAL_PATH))

endif
