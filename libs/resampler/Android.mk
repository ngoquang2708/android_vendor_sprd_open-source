LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

$(call add-prebuilt-files, STATIC_LIBRARIES, libaudioresampler.a)
