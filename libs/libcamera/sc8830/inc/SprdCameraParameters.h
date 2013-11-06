/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ANDROID_HARDWARE_SPRD_CAMERA_PARAMETERS_H
#define ANDROID_HARDWARE_SPRD_CAMERA_PARAMETERS_H

#include <camera/CameraParameters.h>

namespace android {

class SprdCameraParameters : public CameraParameters {
public:
	enum ConfigType {
		kFrontCameraConfig,
		kBackCameraConfig
	};

	typedef struct {
		int width;
		int height;
	} Size;

	typedef struct {
		int x;
		int y;
		int width;
		int height;
	}Rect;

public:
    SprdCameraParameters();
    SprdCameraParameters(const String8 &params);
    ~SprdCameraParameters();

	void setDefault(ConfigType config);

	void getFocusAreas(int *area, int *count);
	void getFocusAreas(int *area, int *count, Size *preview_size,
					 Rect *preview_rect, int orientation, bool mirror);
	int getFocusMode();
	int getWhiteBalance();
	int getCameraId();
	int getJpegQuality();
	int getJpegThumbnailQuality();
	int getEffect();
	int getSceneMode();
	int getZoom();
	int getBrightness();
	int getSharpness();
	int getContrast();
	int getSaturation();
	int getExposureCompensation();
	int getAntiBanding();
	int getIso();
	int getRecordingHint();
	int getFlashMode();
	int getSlowmotion();
	int getPreviewEnv();
	int getPreviewFameRate();
	int getAutoExposureMode();
	void getMeteringAreas(int *area, int *count, Size *preview_size,
					 Rect *preview_rect, int orientation, bool mirror);
	void setZSLSupport(const char* value);
	void updateSupportedPreviewSizes(int width, int height);


	// These sizes have to be a multiple of 16 in each dimension
	static const Size kPreviewSizes[];
	static const int kPreviewSettingCount;
	static const int kDefaultPreviewSize;

	static const unsigned int kFocusZoneMax = 5;
	static const unsigned int kMeteringAreasMax = 5;
	static const int kInvalidValue = 0xffffffff;
	static const int kFrontCameraConfigCount;
	static const int kBackCameraConfigCount;

	static const char KEY_FOCUS_AREAS[];
	static const char KEY_FOCUS_MODE[];
	static const char KEY_WHITE_BALANCE[];
	static const char KEY_CAMERA_ID[];
	static const char KEY_JPEG_QUALITY[];
	static const char KEY_JPEG_THUMBNAIL_QUALITY[];
	static const char KEY_EFFECT[];
	static const char KEY_SCENE_MODE[];
	static const char KEY_ZOOM[];
	static const char KEY_BRIGHTNESS[];
	static const char KEY_CONTRAST[];
	static const char KEY_EXPOSURE_COMPENSATION[];
	static const char KEY_ANTI_BINDING[];
	static const char KEY_ISO[];
	static const char KEY_RECORDING_HINT[];
	static const char KEY_FLASH_MODE[];
	static const char KEY_SLOWMOTION[];
	static const char KEY_SATURATION[];
	static const char KEY_SHARPNESS[];
	static const char KEY_PREVIEWFRAMERATE[];
	static const char KEY_AUTO_EXPOSURE[];
	static const char KEY_METERING_AREAS[];
	static const char KEY_PREVIEW_ENV[];

private:

};

}//namespace android

#endif
