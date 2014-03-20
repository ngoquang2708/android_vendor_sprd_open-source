LOCAL_PATH:= $(call my-dir)

################################################################################

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
        SPRDAPEDecoder.cpp

LOCAL_C_INCLUDES := \
        frameworks/av/media/libstagefright/include \
        frameworks/native/include/media/openmax \
        frameworks/av/include/media/stagefright \
        $(TOP)/vendor/sprd/open-source/libs/libstagefrighthw/include \
        $(TOP)/vendor/sprd/open-source/libs/omx_components/audio/apedec_sprd/decode_inc

LOCAL_CFLAGS := -DOSCL_EXPORT_REF= -DOSCL_IMPORT_REF= -D_AACARM_  -D_ARMNINEPLATFORM_  -DAAC_DEC_LITTLE_ENDIAN

LOCAL_SHARED_LIBRARIES := \
          libstagefright libstagefright_omx libstagefright_foundation libstagefrighthw libutils libui libbinder libdl libcutils

LOCAL_MODULE := libstagefright_sprd_apedec
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

################################################################################
