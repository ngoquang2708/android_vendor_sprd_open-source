LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
        SPRDAACDecoder.cpp

LOCAL_C_INCLUDES := \
          frameworks/av/media/libstagefright/include \
          frameworks/native/include/media/openmax    \
	  media/libstagefright/codecs/aacdec_sprd   \
	  frameworks/av/include/media/stagefright

LOCAL_CFLAGS := -DOSCL_EXPORT_REF= -DOSCL_IMPORT_REF= -D_AACARM_  -D_ARMNINEPLATFORM_  -DAAC_DEC_LITTLE_ENDIAN

LOCAL_LDFLAGS += -Wl,--no-warn-shared-textrel

LOCAL_SHARED_LIBRARIES := \
          libstagefright libstagefright_omx libstagefright_foundation libutils libui libbinder libdl libcutils liblog

LOCAL_LDFLAGS += $(TOP)/vendor/sprd/open-source/libs/omx_components/audio/aacdec_sprd/libaacdec_sprd.a

LOCAL_MODULE := libstagefright_sprd_aacdec
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)
