LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

ifeq ($(strip $(TARGET_BOARD_PLATFORM)),sc8830)
LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/vsp/sc8830/inc	\
	$(LOCAL_PATH)/vsp/sc8830/src \
	$(LOCAL_PATH)/jpeg/jpeg_fw_8830/inc \
	$(LOCAL_PATH)/jpeg/jpeg_fw_8830/src \
	$(LOCAL_PATH)/sc8830/inc \
	$(LOCAL_PATH)/sensor_drv_u/inc \
	$(LOCAL_PATH)/isp/inc \
	$(LOCAL_PATH)/sc8830/isp_calibration/inc \
	external/skia/include/images \
	external/skia/include/core\
        external/jhead \
        external/sqlite/dist \
	$(TARGET_OUT_INTERMEDIATES)/KERNEL/source/include/video \
	$(TOP)/device/sprd/common/libs/gralloc \
	$(TOP)/device/sprd/common/libs/mali/src/ump/include

LOCAL_SRC_FILES:= \
	sc8830/src/SprdOEMCamera.c \
        sc8830/src/SprdCameraHardwareInterface.cpp \
	sc8830/src/SprdCameraParameters.cpp \
	sc8830/src/cmr_oem.c \
	sc8830/src/cmr_set.c \
	sc8830/src/cmr_mem.c \
	sc8830/src/cmr_msg.c \
	sc8830/src/cmr_scale.c \
	sc8830/src/cmr_rotate.c \
	sc8830/src/cmr_copy.c \
	sc8830/src/cmr_v4l2.c \
	sc8830/src/jpeg_codec.c \
	sc8830/src/dc_cfg.c \
	sc8830/src/dc_product_cfg.c \
	sc8830/src/sensor_cfg.c \
	sc8830/src/cmr_arith.c \
	sensor_drv_u/src/sensor_drv_u.c \
	sensor/sensor_ov8825_mipi_raw.c \
	sensor/sensor_ov13850_mipi_raw.c \
	sensor/sensor_ov5648_mipi_raw.c \
	sensor/sensor_imx179_mipi_raw.c \
	sensor/sensor_ov5640_mipi.c \
	sensor/sensor_gc2035.c \
	sensor/sensor_ov8865_mipi_raw.c \
	sensor/sensor_gt2005.c \
	sensor/sensor_gc0308.c \
	vsp/sc8830/src/jpg_drv_sc8830.c \
	jpeg/jpeg_fw_8830/src/jpegcodec_bufmgr.c \
	jpeg/jpeg_fw_8830/src/jpegcodec_global.c \
	jpeg/jpeg_fw_8830/src/jpegcodec_table.c \
	jpeg/jpeg_fw_8830/src/jpegenc_bitstream.c \
	jpeg/jpeg_fw_8830/src/jpegenc_frame.c \
	jpeg/jpeg_fw_8830/src/jpegenc_header.c \
	jpeg/jpeg_fw_8830/src/jpegenc_init.c \
	jpeg/jpeg_fw_8830/src/jpegenc_interface.c \
	jpeg/jpeg_fw_8830/src/jpegenc_malloc.c \
	jpeg/jpeg_fw_8830/src/jpegenc_api.c \
	jpeg/jpeg_fw_8830/src/jpegdec_bitstream.c \
	jpeg/jpeg_fw_8830/src/jpegdec_frame.c \
	jpeg/jpeg_fw_8830/src/jpegdec_init.c \
	jpeg/jpeg_fw_8830/src/jpegdec_interface.c \
	jpeg/jpeg_fw_8830/src/jpegdec_malloc.c \
	jpeg/jpeg_fw_8830/src/jpegdec_dequant.c	\
	jpeg/jpeg_fw_8830/src/jpegdec_out.c \
	jpeg/jpeg_fw_8830/src/jpegdec_parse.c \
	jpeg/jpeg_fw_8830/src/jpegdec_pvld.c \
	jpeg/jpeg_fw_8830/src/jpegdec_vld.c \
	jpeg/jpeg_fw_8830/src/jpegdec_api.c  \
	jpeg/jpeg_fw_8830/src/exif_writer.c  \
	jpeg/jpeg_fw_8830/src/jpeg_stream.c \
	isp/isp_app.c \
	isp/isp_app_msg.c \
	isp/isp_video.c \
	isp/isp_param_tune_com.c \
	isp/isp_param_tune_v0000.c \
	isp/isp_param_tune_v0001.c \
	isp/isp_param_size.c \
	sc8830/isp_calibration/src/utest_camera.cpp \
	sc8830/isp_calibration/src/isp_calibration.c \
	sc8830/isp_calibration/src/isp_cali_interface.c

LOCAL_MODULE_PATH := $(TARGET_OUT_OPTIONAL_EXECUTABLES)
LOCAL_CFLAGS := -fno-strict-aliasing -D_VSP_ -DJPEG_ENC -D_VSP_LINUX_ -DCHIP_ENDIAN_LITTLE -DCONFIG_CAMERA_2M -DANDROID_4100

ifeq ($(strip $(TARGET_BOARD_PLATFORM)),sc8810)
LOCAL_CFLAGS += -DCONFIG_CAMERA_5M
endif

ifeq ($(strip $(TARGET_BOARD_CAMERA_FLASH_CTRL)),true)
LOCAL_CFLAGS += -DCONFIG_CAMERA_FLASH_CTRL
endif

ifeq ($(strip $(TARGET_BOARD_PLATFORM)),sc7710)
LOCAL_CFLAGS += -DCONFIG_CAMERA_5M
endif

ifeq ($(strip $(CAMERA_SUPPORT_SIZE)),13M)
LOCAL_CFLAGS += -DCONFIG_CAMERA_SUPPORT_13M
endif

ifeq ($(strip $(CAMERA_SUPPORT_SIZE)),8M)
LOCAL_CFLAGS += -DCONFIG_CAMERA_SUPPORT_8M
endif

ifeq ($(strip $(CAMERA_SUPPORT_SIZE)),5M)
LOCAL_CFLAGS += -DCONFIG_CAMERA_SUPPORT_5M
endif

ifeq ($(strip $(CAMERA_SUPPORT_SIZE)),3M)
LOCAL_CFLAGS += -DCONFIG_CAMERA_SUPPORT_3M
endif

ifeq ($(strip $(CAMERA_SUPPORT_SIZE)),2M)
LOCAL_CFLAGS += -DCONFIG_CAMERA_SUPPORT_2M
endif

ifeq ($(strip $(TARGET_BOARD_NO_FRONT_SENSOR)),true)
LOCAL_CFLAGS += -DCONFIG_DCAM_SENSOR_NO_FRONT_SUPPORT
endif

ifeq ($(strip $(TARGET_BOARD_Z788)),true)
LOCAL_CFLAGS += -DCONFIG_CAMERA_788
endif

ifeq ($(strip $(TARGET_BOARD_PLATFORM)),sc8810)
else
ifeq ($(strip $(TARGET_BOARD_PLATFORM)),sc7710)
else
LOCAL_CFLAGS += -DCONFIG_CAMERA_ISP
endif
endif

ifeq ($(strip $(TARGET_BOARD_CAMERA_SUPPORT_VIDEO_SIZE)),1080P)
LOCAL_CFLAGS += -DCONFIG_CAMERA_VIDEO_SIZE_SUPPORT_1080P
endif

ifeq ($(strip $(TARGET_BOARD_CAMERA_SUPPORT_VIDEO_SIZE)),720P)
LOCAL_CFLAGS += -DCONFIG_CAMERA_VIDEO_SIZE_SUPPORT_720P
endif

ifeq ($(strip $(TARGET_BOARD_CAMERA_SUPPORT_VIDEO_SIZE)),D1)
LOCAL_CFLAGS += -DCONFIG_CAMERA_VIDEO_SIZE_SUPPORT_D1
endif

ifeq ($(strip $(TARGET_BOARD_CAMERA_CAPTURE_MODE)),true)
LOCAL_CFLAGS += -DCONFIG_CAMERA_ZSL_CAPTURE
endif

ifeq ($(strip $(TARGET_BOARD_CAMERA_ROTATION_CAPTURE)),true)
LOCAL_CFLAGS += -DCONFIG_CAMERA_ROTATION_CAPTURE
endif

ifeq ($(strip $(TARGET_BOARD_FRONT_CAMERA_ROTATION)),true)
LOCAL_CFLAGS += -DCONFIG_FRONT_CAMERA_ROTATION
endif

ifeq ($(strip $(TARGET_BOARD_BACK_CAMERA_ROTATION)),true)
LOCAL_CFLAGS += -DCONFIG_BACK_CAMERA_ROTATION
endif

ifeq ($(strip $(TARGET_BOARD_CAMERA_ROTATION)),true)
LOCAL_CFLAGS += -DCONFIG_CAMERA_ROTATION
endif

ifeq ($(strip $(TARGET_BOARD_CAMERA_ANTI_SHAKE)),true)
LOCAL_CFLAGS += -DCONFIG_CAMERA_ANTI_SHAKE
endif

ifeq ($(strip $(TARGET_BOARD_CAMERA_DMA_COPY)),true)
LOCAL_CFLAGS += -DCONFIG_CAMERA_DMA_COPY
endif

ifeq ($(strip $(TARGET_BOARD_SP7710_CAMERA)),true)
LOCAL_CFLAGS += -DCONFIG_CAMERA_SP7710_FEATURE
endif

ifeq ($(strip $(TARGET_BOARD_BACK_CAMERA_INTERFACE)),mipi)
LOCAL_CFLAGS += -DCONFIG_BACK_CAMERA_MIPI
endif
ifeq ($(strip $(TARGET_BOARD_BACK_CAMERA_INTERFACE)),ccir)
LOCAL_CFLAGS += -DCONFIG_BACK_CAMERA_CCIR
endif

ifeq ($(strip $(TARGET_BOARD_FRONT_CAMERA_INTERFACE)),mipi)
LOCAL_CFLAGS += -DCONFIG_FRONT_CAMERA_MIPI
endif
ifeq ($(strip $(TARGET_BOARD_FRONT_CAMERA_INTERFACE)),ccir)
LOCAL_CFLAGS += -DCONFIG_FRONT_CAMERA_CCIR
endif

ifeq ($(strip $(TARGET_BOARD_CAMERA_SUPPORT_720P)),true)
LOCAL_CFLAGS += -DCONFIG_CAMERA_SUPPORT_720P
endif

ifeq ($(strip $(TARGET_BOARD_CAMERA_SUPPORT_CIF)),true)
LOCAL_CFLAGS += -DCONFIG_CAMERA_SUPPORT_CIF
endif

ifeq ($(strip $(CAMERA_DISP_ION)),true)
LOCAL_CFLAGS += -DUSE_ION_MEM
endif

ifeq ($(strip $(CAMERA_SENSOR_OUTPUT_ONLY)),true)
LOCAL_CFLAGS += -DCONFIG_SENSOR_OUTPUT_ONLY
endif
        
LOCAL_MODULE := utest_camera_$(TARGET_BOARD_PLATFORM)
LOCAL_MODULE_TAGS := optional

ifeq ($(strip $(TARGET_BOARD_PLATFORM)),sc8825)
LOCAL_SHARED_LIBRARIES := libexif libutils libbinder libcamera_client libskia libcutils libsqlite libhardware libisp libmorpho_facesolid libmorpho_easy_hdr
endif

ifeq ($(strip $(TARGET_BOARD_PLATFORM)),sc8810)
LOCAL_SHARED_LIBRARIES := libexif libutils libbinder libcamera_client libskia libcutils libsqlite libhardware libmorpho_facesolid
endif

ifeq ($(strip $(TARGET_BOARD_PLATFORM)),sc7710)
LOCAL_SHARED_LIBRARIES := libexif libutils libbinder libcamera_client libskia libcutils libsqlite libhardware libmorpho_facesolid
endif

ifeq ($(strip $(TARGET_BOARD_PLATFORM)),sc8830)
LOCAL_SHARED_LIBRARIES := libexif libutils libbinder libcamera_client libskia libcutils libsqlite libhardware libisp libmorpho_facesolid libmorpho_easy_hdr
endif

include $(BUILD_EXECUTABLE)

ifeq ($(strip $(TARGET_BOARD_PLATFORM)),sc8825)

include $(CLEAR_VARS)
LOCAL_PREBUILT_LIBS := sc8825/isp/libisp.so
LOCAL_MODULE_TAGS := optional
include $(BUILD_MULTI_PREBUILT)

include $(CLEAR_VARS)
LOCAL_PREBUILT_LIBS := arithmetic/sc8825/libmorpho_facesolid.so
LOCAL_MODULE_TAGS := optional
include $(BUILD_MULTI_PREBUILT)

include $(CLEAR_VARS)
LOCAL_PREBUILT_LIBS := arithmetic/sc8825/libmorpho_easy_hdr.so
LOCAL_MODULE_TAGS := optional
include $(BUILD_MULTI_PREBUILT)

endif


ifeq ($(strip $(TARGET_BOARD_PLATFORM)),sc8810)

include $(CLEAR_VARS)
LOCAL_PREBUILT_LIBS := arithmetic/sc8810/libmorpho_facesolid.so
LOCAL_MODULE_TAGS := optional
include $(BUILD_MULTI_PREBUILT)

endif

ifeq ($(strip $(TARGET_BOARD_PLATFORM)),sc7710)

include $(CLEAR_VARS)
LOCAL_PREBUILT_LIBS := arithmetic/sc8810/libmorpho_facesolid.so
LOCAL_MODULE_TAGS := optional
include $(BUILD_MULTI_PREBUILT)

endif

endif
