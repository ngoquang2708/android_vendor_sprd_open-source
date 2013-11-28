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

#define LOG_TAG "SprdCamera2"
#include <utils/Log.h>
#include "system/camera_metadata.h"
#include "SprdCameraHardwareConfig2.h"
#include "SprdOEMCamera.h"

#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))

int androidSceneModeToDrvMode(camera_metadata_enum_android_control_scene_mode_t androidScreneMode, int8_t *convertDrvMode)
{
   int ret = 0;

   switch(androidScreneMode)
   {
      case ANDROID_CONTROL_SCENE_MODE_UNSUPPORTED:
	  *convertDrvMode = CAMERA_SCENE_MODE_AUTO;
	  break;

	  case ANDROID_CONTROL_SCENE_MODE_ACTION:
      *convertDrvMode = CAMERA_SCENE_MODE_ACTION;
	  break;

	  case ANDROID_CONTROL_SCENE_MODE_NIGHT:
      *convertDrvMode = CAMERA_SCENE_MODE_NIGHT;
	  break;

	  case ANDROID_CONTROL_SCENE_MODE_PORTRAIT:
      *convertDrvMode = CAMERA_SCENE_MODE_PORTRAIT;
	  break;

      case ANDROID_CONTROL_SCENE_MODE_LANDSCAPE:
      *convertDrvMode = CAMERA_SCENE_MODE_LANDSCAPE;
	  break;

	  default:
	  *convertDrvMode = CAMERA_SCENE_MODE_AUTO;
   }
   return ret;
}


int androidParametTagToDrvParaTag(uint32_t androidParaTag, camera_parm_type *convertDrvTag)
{
   int ret = 0;

   switch(androidParaTag)
   {
      case ANDROID_CONTROL_SCENE_MODE:
	  *convertDrvTag = CAMERA_PARM_SCENE_MODE;
	  break;
	  #if 1
	  case ANDROID_SCALER_CROP_REGION:
      *convertDrvTag = CAMERA_PARM_ZOOM_RECT;
	  break;
	  #endif
	  default:
	  break;
   }
   return ret;
}
