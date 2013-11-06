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


#define LOG_TAG "SprdCameraParameters"
#include <utils/Log.h>

#include <string.h>
#include <stdlib.h>
#include "SprdCameraParameters.h"
#include "SprdCameraHardwareConfig.h"

namespace android {

//#define LOG_TAG	   "SprdCameraParameters"

#define LOGV       ALOGD
#define LOGE       ALOGE
#define LOGI       ALOGI
#define LOGW       ALOGW
#define LOGD       ALOGD

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

////////////////////////////////////////////////////////////////////////////////////
static int lookup(const struct str_map *const arr, const char *name, int def);
// Parse rectangle from string like "(100,100,200,200, weight)" .
// if exclude_weight is true: the weight is not write to rect array
static int parse_rect(int *rect, int *count, const char *str, bool exclude_weight);
static void coordinate_struct_convert(int *rect_arr,int arr_size);
static int coordinate_convert(int *rect_arr,int arr_size,int angle,int is_mirror, SprdCameraParameters::Size *preview_size,
							  SprdCameraParameters::Rect *preview_rect);
////////////////////////////////////////////////////////////////////////////////////

const int SprdCameraParameters::kDefaultPreviewSize = 0;
const SprdCameraParameters::Size SprdCameraParameters::kPreviewSizes[] = {
		{ 640, 480 },
		{ 480, 320 }, // HVGA
		{ 432, 320 }, // 1.35-to-1, for photos. (Rounded up from 1.3333 to 1)
		{ 352, 288 }, // CIF
		{ 320, 240 }, // QVGA
		{ 240, 160 }, // SQVGA
		{ 176, 144 }, // QCIF
	};

const int SprdCameraParameters::kPreviewSettingCount = sizeof(kPreviewSizes)/sizeof(Size);

const int SprdCameraParameters::kFrontCameraConfigCount = ARRAY_SIZE(sprd_front_camera_hardware_config);
const int SprdCameraParameters::kBackCameraConfigCount = ARRAY_SIZE(sprd_back_camera_hardware_config);

// Parameter keys to communicate between camera application and driver.
const char SprdCameraParameters::KEY_FOCUS_AREAS[] = "focus-areas";
const char SprdCameraParameters::KEY_FOCUS_MODE[] = "focus-mode";
const char SprdCameraParameters::KEY_WHITE_BALANCE[] = "whitebalance";
const char SprdCameraParameters::KEY_CAMERA_ID[] = "cameraid";
const char SprdCameraParameters::KEY_JPEG_QUALITY[] = "jpeg-quality";
const char SprdCameraParameters::KEY_JPEG_THUMBNAIL_QUALITY[] = "jpeg-thumbnail-quality";
const char SprdCameraParameters::KEY_EFFECT[] = "effect";
const char SprdCameraParameters::KEY_SCENE_MODE[] = "scene-mode";
const char SprdCameraParameters::KEY_ZOOM[] = "zoom";
const char SprdCameraParameters::KEY_BRIGHTNESS[] = "brightness";
const char SprdCameraParameters::KEY_CONTRAST[] = "contrast";
const char SprdCameraParameters::KEY_EXPOSURE_COMPENSATION[] = "exposure-compensation";
const char SprdCameraParameters::KEY_ANTI_BINDING[] = "antibanding";
const char SprdCameraParameters::KEY_ISO[] = "iso";
const char SprdCameraParameters::KEY_RECORDING_HINT[] = "recording-hint";
const char SprdCameraParameters::KEY_FLASH_MODE[] = "flash-mode";
const char SprdCameraParameters::KEY_SLOWMOTION[] = "slow-motion";
const char SprdCameraParameters::KEY_SATURATION[] = "saturation";
const char SprdCameraParameters::KEY_SHARPNESS[] = "sharpness";
const char SprdCameraParameters::KEY_PREVIEWFRAMERATE[] = "preview-frame-rate";
const char SprdCameraParameters::KEY_AUTO_EXPOSURE[] = "auto-exposure";
const char SprdCameraParameters::KEY_METERING_AREAS[] = "metering-areas";
const char SprdCameraParameters::KEY_PREVIEW_ENV[] = "preview-env";


////////////////////////////////////////////////////////////////////////////////////
SprdCameraParameters::SprdCameraParameters():CameraParameters()
{

}

SprdCameraParameters::SprdCameraParameters(const String8 &params):CameraParameters(params)
{

}

SprdCameraParameters::~SprdCameraParameters()
{

}

void SprdCameraParameters::setDefault(ConfigType config)
{
	struct config_element *element = NULL;
	int count = 0;

	setPreviewSize(kPreviewSizes[kDefaultPreviewSize].width,
				kPreviewSizes[kDefaultPreviewSize].height);

	setPreviewFrameRate(15);
	setPreviewFormat("yuv420sp");
	setPictureFormat("jpeg");

	set("jpeg-quality", "100"); // maximum quality
	set("jpeg-thumbnail-width", "320");
	set("jpeg-thumbnail-height", "240");
	set("jpeg-thumbnail-quality", "80");
	set("focus-mode", "auto");

	switch (config) {
	case kFrontCameraConfig:
		element = sprd_front_camera_hardware_config;
		count = kFrontCameraConfigCount;
		break;

	case kBackCameraConfig:
		element = sprd_back_camera_hardware_config;
		count = kBackCameraConfigCount;
		break;
	}

	LOGV("setDefault: config = %d, count = %d", config, count);

	for (int i=0; i<count; i++) {
		set(element[i].key, element[i].value);
		LOGV("SetDefault: key = %s, value = %s", element[i].key, element[i].value);
	}
}

//return rectangle: (x1, y1, x2, y2, weight), the values are on the screen's coordinate
void SprdCameraParameters::getFocusAreas(int *area, int *count)
{
	const char *p = get(KEY_FOCUS_AREAS);

	parse_rect(area, count, p, false);
}

//return rectangle: (x1, y1, x2, y2), the values are on the sensor's coordinate
void SprdCameraParameters::getFocusAreas(int *area, int *count, Size *preview_size,
										 Rect *preview_rect,
											int orientation, bool mirror)
{
	const char *p = get(KEY_FOCUS_AREAS);
	int focus_area[4 * kFocusZoneMax] = {0};
	int area_count = 0;

	parse_rect(&focus_area[0], &area_count, p, true);

	if(area_count > 0) {
		int ret = coordinate_convert(&focus_area[0], area_count, orientation, mirror,
								preview_size, preview_rect);

		if(ret) {
			area_count = 0;
			LOGV("error: coordinate_convert error, ignore focus \n");
		} else {
			coordinate_struct_convert(&focus_area[0], area_count * 4);

			for (int i=0; i<area_count * 4; i++) {
				area[i] = focus_area[i];
				if(focus_area[i+1] < 0) {
					area_count = 0;
					LOGV("error: focus area %d < 0, ignore focus \n", focus_area[i+1]);
				}
			}
		}
	}

	*count = area_count;
}

//return rectangle: (x1, y1, x2, y2), the values are on the sensor's coordinate
void SprdCameraParameters::getMeteringAreas(int *area, int *count, Size *preview_size,
										 Rect *preview_rect,
											int orientation, bool mirror)
{
	const char *p = get(KEY_METERING_AREAS);
	int metering_area[4 * kMeteringAreasMax] = {0};
	int area_count = 0;

	LOGV("getMeteringAreas: %s", p);

	parse_rect(&metering_area[0], &area_count, p, true);

	if(area_count > 0) {
		int ret = coordinate_convert(&metering_area[0], area_count, orientation, mirror,
								preview_size, preview_rect);

		if(ret) {
			area_count = 0;
			LOGV("error: coordinate_convert error, ignore focus \n");
		} else {
			coordinate_struct_convert(&metering_area[0], area_count * 4);

			for (int i=0; i<area_count * 4; i++) {
				area[i] = metering_area[i];
				if(metering_area[i+1] < 0) {
					area_count = 0;
					LOGV("error: focus area %d < 0, ignore focus \n", metering_area[i+1]);
				}
			}
		}
	}

	*count = area_count;
}

int SprdCameraParameters::getFocusMode()
{
	const char *p = get(KEY_FOCUS_MODE);

	return lookup(focus_mode_map, p, CAMERA_FOCUS_MODE_AUTO);
}

int SprdCameraParameters::getWhiteBalance()
{
	const char *p = get(KEY_WHITE_BALANCE);

	return lookup(wb_map, p, CAMERA_WB_AUTO);
}

int SprdCameraParameters::getCameraId()
{
	const char *p = get(KEY_CAMERA_ID);

	return lookup(camera_id_map, p, CAMERA_CAMERA_ID_BACK);
}

int SprdCameraParameters::getJpegQuality()
{
	return getInt(KEY_JPEG_QUALITY);
}

int SprdCameraParameters::getJpegThumbnailQuality()
{
	return getInt(KEY_JPEG_THUMBNAIL_QUALITY);
}

int SprdCameraParameters::getEffect()
{
	const char *p = get(KEY_EFFECT);

	return lookup(effect_map, p, CAMERA_EFFECT_NONE);
}

int SprdCameraParameters::getSceneMode()
{
	const char *p = get(KEY_SCENE_MODE);

	return lookup(scene_mode_map, p, CAMERA_SCENE_MODE_AUTO);
}

int SprdCameraParameters::getZoom()
{
	const char *p = get(KEY_ZOOM);

	return lookup(zoom_map, p, CAMERA_ZOOM_1X);
}

int SprdCameraParameters::getBrightness()
{
	const char *p = get(KEY_BRIGHTNESS);

	return lookup(brightness_map, p, CAMERA_BRIGHTNESS_DEFAULT);
}

int SprdCameraParameters::getSharpness()
{
	const char *p = get(KEY_SHARPNESS);

	return lookup(sharpness_map, p, CAMERA_SHARPNESS_DEFAULT);
}

int SprdCameraParameters::getPreviewFameRate()
{
	const char *p = get(KEY_PREVIEWFRAMERATE);

	return lookup(previewframerate_map, p, CAMERA_PREVIEWFRAMERATE_DEFAULT);
}

int SprdCameraParameters::getContrast()
{
	const char *p = get(KEY_CONTRAST);

	return lookup(contrast_map, p, CAMERA_CONTRAST_DEFAULT);
}

int SprdCameraParameters::getSaturation()
{
	const char *p = get(KEY_SATURATION);

	return lookup(saturation_map, p, CAMERA_SATURATION_DEFAULT);
}

int SprdCameraParameters::getExposureCompensation()
{
	const char *p = get(KEY_EXPOSURE_COMPENSATION);

	return lookup(exposure_compensation_map, p, CAMERA_EXPOSURW_COMPENSATION_DEFAULT);
}

int SprdCameraParameters::getAntiBanding()
{
	const char *p = get(KEY_ANTI_BINDING);

	return lookup(antibanding_map, p, CAMERA_ANTIBANDING_50HZ);
}

int SprdCameraParameters::getIso()
{
	const char *p = get(KEY_ISO);

	return lookup(iso_map, p, CAMERA_ISO_AUTO);
}

int SprdCameraParameters::getRecordingHint()
{
	const char *p = get(KEY_RECORDING_HINT);

	return lookup(camera_dcdv_mode, p, CAMERA_DC_MODE);
}

int SprdCameraParameters::getFlashMode()
{
	const char *p = get(KEY_FLASH_MODE);

	return lookup(flash_mode_map, p, CAMERA_FLASH_MODE_OFF);
}

int SprdCameraParameters::getSlowmotion()
{
	const char *p = get(KEY_SLOWMOTION);

	return lookup(slowmotion_map, p, CAMERA_SLOWMOTION_0);
}

int SprdCameraParameters::getPreviewEnv()
{
	const char *p = get(KEY_PREVIEW_ENV);

	return lookup(previewenv_map, p, CAMERA_DC_PREVIEW);
}

int SprdCameraParameters::getAutoExposureMode()
{
	const char *p = get(KEY_AUTO_EXPOSURE);

	return lookup(auto_exposure_mode_map, p, CAMERA_AE_FRAME_AVG);
}

void SprdCameraParameters::setZSLSupport(const char* value)
{
	set("zsl-supported",value);
}

void SprdCameraParameters::updateSupportedPreviewSizes(int width, int height)
{
	char size_new[32] = {0};
	char vals_new[256] = {0};
	const char *p = get(KEY_PREVIEW_SIZE);
	const char *vals_p = get(KEY_SUPPORTED_PREVIEW_SIZES);
	const char *pos_1 = vals_p, *pos_2 = vals_p;
	unsigned int p_len = strlen(p);
	unsigned int cnt = 0;
	unsigned int i = 0;

	height = width*3/4;
	sprintf(size_new, "%dx%d", width, height);
	LOGV("updateSupportedPreviewSizes preview-size %s", size_new);

	pos_1 = strstr(vals_p, p);
	if (!pos_1) return;

	pos_2 = pos_1 + p_len;
	strncpy(vals_new, vals_p, pos_1-vals_p);
	strcat(vals_new, size_new);
	strcat(vals_new, pos_2);
	LOGV("updateSupportedPreviewSizes preview-size-values %s", vals_new);

	set(KEY_SUPPORTED_PREVIEW_SIZES, vals_new);
	set(KEY_PREVIEW_SIZE, size_new);
}


///////////////////////////////////////////////////////////////////////////////
//
///////////////////////////////////////////////////////////////////////////////
// Parse rectangle from string like "(100,100,200,200, weight)" .
// if exclude_weight is true: the weight is not write to rect array
static int parse_rect(int *rect, int *count, const char *str, bool exclude_weight)
{
	char *a = (char *)str;
	char *b = a, *c = a;
	char k[40] = {0};
	char m[40]={0};
	int *rect_arr = rect;
	unsigned int cnt = 0;
	unsigned int i=0;

	if(!a) 	return 0;

	do {
		b = strchr(a,'(');
		if (b == 0)
			goto lookuprect_done;

		a = b + 1;
		b = strchr(a,')');
		if(b == 0)
			goto lookuprect_done;

		strncpy(k, a, (b-a));
		a = b + 1;

		c = strchr(k,',');
		strncpy(m,k,(c-k));
		*rect_arr++ = strtol(m, 0, 0);//left
		memset(m,0,20);

		b = c + 1;
		c = strchr(b, ',');
		strncpy(m, b, (c-b));
		*rect_arr++ = strtol(m, 0, 0);//top
		memset(m, 0, 20);

		b = c + 1;
		c = strchr(b, ',');
		strncpy(m, b, (c-b));
		*rect_arr++ = strtol(m, 0, 0);//right
		memset(m, 0, 20);

		b = c + 1;
		c = strchr(b, ',');
		strncpy(m, b, (c-b));
		*rect_arr++ = strtol(m, 0, 0);//bottom
		memset(m, 0, 20);

		b = c + 1;
		if (!exclude_weight)
			*rect_arr++ =strtol(b, 0, 0);//weight
		memset(m, 0, 20);
		memset(k, 0, 10);

		cnt++;

		if(cnt == SprdCameraParameters::kFocusZoneMax)
			break;

	}while(a);

lookuprect_done:
    *count = cnt;

	return cnt;
}

static int lookupvalue(const struct str_map *const arr, const char *name)
{
	//LOGV("lookup: name :%s .",name);
    if (name) {
        const struct str_map * trav = arr;
        while (trav->desc) {
            if (!strcmp(trav->desc, name))
                return trav->val;
            trav++;
        }
    }

	return SprdCameraParameters::kInvalidValue;
}

static int lookup(const struct str_map *const arr, const char *name, int def)
{
	int ret = lookupvalue(arr, name);

	return SprdCameraParameters::kInvalidValue == ret ? def : ret;
}

static void discard_zone_weight(int *arr, uint32_t size)
{
    uint32_t i = 0;
    int *dst_arr = &arr[4];
    int *src_arr = &arr[5];

    for(i=0;i<(size-1);i++)
    {
        *dst_arr++ = *src_arr++;
        *dst_arr++ = *src_arr++;
        *dst_arr++ = *src_arr++;
        *dst_arr++ = *src_arr++;
        src_arr++;
    }
    for(i=0;i<size;i++)
    {
        LOGV("discard_zone_weight: %d:%d,%d,%d,%d.\n",i,arr[i*4],arr[i*4+1],arr[i*4+2],arr[i*4+3]);
     }
}

static void coordinate_struct_convert(int *rect_arr,int arr_size)
{
    int i =0;
    int left = 0,top=0,right=0,bottom=0;
	int width = 0, height = 0;
    int *rect_arr_copy = rect_arr;

    for(i=0;i<arr_size/4;i++)
    {
        left   = rect_arr[i*4];
        top    = rect_arr[i*4+1];
        right  = rect_arr[i*4+2];
		bottom = rect_arr[i*4+3];
        width = (((right-left+3) >> 2)<<2);
        height =(((bottom-top+3) >> 2)<<2);
		rect_arr[i*4+2] = width;
		rect_arr[i*4+3] = height;
		LOGV("test:zone: left=%d,top=%d,right=%d,bottom=%d, w=%d, h=%d \n", left, top, right, bottom, width, height);
    }
    for(i=0;i<arr_size/4;i++)
    {
        LOGV("test:zone:%d,%d,%d,%d.\n",rect_arr_copy[i*4],rect_arr_copy[i*4+1],rect_arr_copy[i*4+2],rect_arr_copy[i*4+3]);
    }
}

static int coordinate_convert(int *rect_arr,int arr_size,int angle,int is_mirror, SprdCameraParameters::Size *preview_size,
							  SprdCameraParameters::Rect *preview_rect)
{
	int i;
	int x1,x2,y1,y2;
	int temp;
	int recHalfWidth;
	int recHalfHeight;
	int centre_x;
	int centre_y;
	int ret = 0;
	int width = preview_size->width;
	int height = preview_size->height;

	LOGV("coordinate_convert: mPreviewWidth=%d, mPreviewHeight=%d, arr_size=%d, angle=%d, is_mirror=%d \n",
	width, height, arr_size, angle, is_mirror);

	for(i=0;i<arr_size*2;i++) {
		x1 = rect_arr[i*2];
		y1 = rect_arr[i*2+1];

		if(is_mirror)
			x1 = -x1;

		switch(angle) {
		case 0:
			rect_arr[i*2]         = (1000 + x1) * height / 2000; // 480
			rect_arr[i*2 + 1]   = (1000 + y1) * width / 2000;	// 640
			break;

		case 90:
			rect_arr[i*2]         = (1000 - y1) * height / 2000;
			rect_arr[i*2 + 1]   = (1000 + x1) * width / 2000;
			break;

		case 180:
			rect_arr[i*2]         = (1000 - x1) * height / 2000;
			rect_arr[i*2 + 1]   = (1000 - y1) * width / 2000;
			break;

		case 270:
			rect_arr[i*2]         = (1000 + y1) * height / 2000;
			rect_arr[i*2 + 1]   = (1000 - y1) * width / 2000;
			break;
		}
	}

	for(i=0;i<arr_size;i++)
	{
		// (x1, y1, x2, y2)
		// if x1 > x2, (x2, y1, x1, y2)
		if(rect_arr[i*4] > rect_arr[i*4+2])
		{
			temp                    = rect_arr[i*4];
			rect_arr[i*4]       = rect_arr[i*4+2];
			rect_arr[i*4+2]     = temp;
		}

		if(rect_arr[i*4+1] > rect_arr[i*4+3])
		{
			temp                    = rect_arr[i*4+1];
			rect_arr[i*4+1]       = rect_arr[i*4+3];
			rect_arr[i*4+3]     = temp;
		}

		LOGV("coordinate_convert: %d: left=%d, top=%d, right=%d, bottom=%d.\n",i,rect_arr[i*4],rect_arr[i*4+1],rect_arr[i*4+2],rect_arr[i*4+3]);
	}

	// make sure the coordinate within [width, height]
	// for 90 degree only
	for(i=0; i<arr_size*4; i+=2) {
		if(rect_arr[i] < 0) {
			rect_arr[i]= 0;
		}
		if(rect_arr[i] > height) {
			rect_arr[i] = height;
		}

		if(rect_arr[i+1] < 0) {
			rect_arr[i+1]= 0;
		}

		if(rect_arr[i+1] > width) {
			rect_arr[i+1] = width;
		}
	}

	int preview_x = preview_rect->x;
	int preview_y = preview_rect->y;
	int preview_w = preview_rect->width;
	int preview_h = preview_rect->height;

	LOGV("coordinate_convert %d: preview rect: x=%d, y=%d, preview_w=%d, preview_h=%d.\n",
			i, preview_x, preview_y, preview_w, preview_h);

	for(i=0;i<arr_size;i++)
	{
		int point_x, point_y;

		LOGV("coordinate_convert %d: org: %d, %d, %d, %d.\n",i,rect_arr[i*4],rect_arr[i*4+1],rect_arr[i*4+2],rect_arr[i*4+3]);

		// only for angle 90/270
		// calculate the centre point
		recHalfHeight  	= (rect_arr[i*4+2] - rect_arr[i*4])/2;
		recHalfWidth   	= (rect_arr[i*4+3] - rect_arr[i*4+1])/2;
		centre_y  		= rect_arr[i*4+2] - recHalfHeight;
		centre_x 		= rect_arr[i*4+3] - recHalfWidth;
		LOGV("CAMERA HAL:coordinate_convert %d: center point: x=%d, y=%d\n", i, centre_x, centre_y);

		// map to sensor coordinate
		centre_y		= height - centre_y;
		LOGV("coordinate_convert %d: sensor centre pointer: x=%d, y=%d, half_w=%d, half_h=%d.\n",
				i, centre_x, centre_y, recHalfWidth, recHalfHeight);

		point_x = preview_x + centre_x*preview_w/width;
		point_y = preview_y + centre_y*preview_h/height;
		LOGV("coordinate_convert %d: out point: x=%d, y=%d\n", i, point_x, point_y);

		rect_arr[i*4]       = point_x - recHalfWidth;
		rect_arr[i*4+1]     = point_y - recHalfHeight;
		rect_arr[i*4+2]     = point_x + recHalfWidth;
		rect_arr[i*4+3]     = point_y + recHalfHeight;

		LOGV("coordinate_convert %d: final: %d, %d, %d, %d.\n",i,rect_arr[i*4],rect_arr[i*4+1],rect_arr[i*4+2],rect_arr[i*4+3]);
	}

	return ret;
}


}//namespace android
