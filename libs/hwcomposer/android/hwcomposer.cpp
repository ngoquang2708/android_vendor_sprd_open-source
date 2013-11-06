/*
 * Copyright (C) 2010 The Android Open Source Project
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
#include <hardware/hardware.h>

#include <fcntl.h>
#include <errno.h>

#include <cutils/log.h>
#include <cutils/atomic.h>

#include <hardware/hwcomposer.h>
#include <EGL/egl.h>

#include "hwcomposer_android.h"
#include "dump_bmp.h"

/*****************************************************************************/
extern int dump_layer(const char* path ,const char* pSrc , const char* ptype ,  int width , int height , int format ,int64_t randNum ,  int index , int LayerIndex = 0);
static int64_t g_GeometoryChanged_Num = 0;
static bool g_GeometryChanged = false;
static bool bFirstGeometroyChanged = false;
static int getDumpPath(char *pPath)
{
	char value[PROPERTY_VALUE_MAX];
	if(0 == property_get("dump.hwcomposer.path" , value , "0"))
	{
		return -1;
	}
	if(strchr(value , '/') != NULL)
	{
		sprintf(pPath , "%s" , value);
		return 0;
	}
	else
		pPath[0] = 0;
	return -2;
}

static int hwc_device_open(const struct hw_module_t* module, const char* name,
        struct hw_device_t** device);

static struct hw_module_methods_t hwc_module_methods = {
    open: hwc_device_open
};

hwc_module_t HAL_MODULE_INFO_SYM = {
    common: {
        tag: HARDWARE_MODULE_TAG,
        version_major: 1,
        version_minor: 0,
        id: HWC_HARDWARE_MODULE_ID,
        name: "Sample hwcomposer module",
        author: "The Android Open Source Project",
        methods: &hwc_module_methods,
    }
};

/*****************************************************************************/

static void dump_layer(hwc_layer_t const* l) {
    ALOGD("\ttype=%d, flags=%08x, handle=%p, tr=%02x, blend=%04x, {%d,%d,%d,%d}, {%d,%d,%d,%d}",
            l->compositionType, l->flags, l->handle, l->transform, l->blending,
            l->sourceCrop.left,
            l->sourceCrop.top,
            l->sourceCrop.right,
            l->sourceCrop.bottom,
            l->displayFrame.left,
            l->displayFrame.top,
            l->displayFrame.right,
            l->displayFrame.bottom);
}

static int hwc_prepare(hwc_composer_device_t *dev, hwc_layer_list_t* list) {
	char value[PROPERTY_VALUE_MAX];
	if (list && (list->flags & HWC_GEOMETRY_CHANGED)) {
		for (size_t i=0 ; i<list->numHwLayers ; i++) {
			//dump_layer(&list->hwLayers[i]);
			list->hwLayers[i].compositionType = HWC_FRAMEBUFFER;
	}

	if(bFirstGeometroyChanged == false)
	{
		bFirstGeometroyChanged =  true;
		g_GeometoryChanged_Num = 0;
	}
	else
	{
		g_GeometoryChanged_Num++;
	}
	g_GeometryChanged = true;
    }
    else if(list)
    {
        g_GeometryChanged = false;
    }
	/*****************check dump flag and dump layers if true*************************/
	if(0 != property_get("dump.hwcomposer.flag" , value , "0"))
	{
		int flag = atoi(value);
		static int index = 0;
		char dumpPath[MAX_DUMP_PATH_LENGTH];
		if(list == NULL)
			return 0;
		if(HWCOMPOSER_DUMP_ORIGINAL_LAYERS & flag)
		{
			getDumpPath(dumpPath);
			if(g_GeometryChanged)
			{
				index = 0;
			}
			for (size_t i=0 ; i<list->numHwLayers ; i++) {
				hwc_layer_t * layer_t = &(list->hwLayers[i]);
				struct private_handle_t *private_h = (struct private_handle_t *)layer_t->handle;
				if(private_h == NULL)
				{
					continue;
				}
				dump_layer(dumpPath , (char*)private_h->base , "Layer" , private_h->width , private_h->height , private_h->format , g_GeometoryChanged_Num , index , i);
			}
			index++;
		}

	}
	/***************************************dump end****************************/
    return 0;
}

static int hwc_set(hwc_composer_device_t *dev,
        hwc_display_t dpy,
        hwc_surface_t sur,
        hwc_layer_list_t* list)
{
    //for (size_t i=0 ; i<list->numHwLayers ; i++) {
    //    dump_layer(&list->hwLayers[i]);
    //}
    //add for dump layer to file, need set property dump.hwcomposer.path & dump.hwcomposer.flag
    EGLBoolean sucess = eglSwapBuffers((EGLDisplay)dpy, (EGLSurface)sur);
    if (!sucess) {
        return HWC_EGL_ERROR;
    }
    return 0;
}

static int hwc_eventControl(struct hwc_composer_device* dev, int event, int enabled)
{
    struct hwc_context_t *hwc_dev = (struct hwc_context_t *) dev;

    switch(event)
    {
    case HWC_EVENT_VSYNC:
        if (hwc_dev->mVSyncThread != 0)
            hwc_dev->mVSyncThread->setEnabled(enabled);
        break;
    default:
        break;
    }
    return 0;
}
static hwc_methods_t hwc_device_methods = {
    eventControl: hwc_eventControl
};

static int hwc_device_close(struct hw_device_t *dev)
{
    struct hwc_context_t* ctx = (struct hwc_context_t*)dev;
    if (ctx) {
        hwc_eventControl(&ctx->device, HWC_EVENT_VSYNC, 0);
        if (ctx->mVSyncThread != NULL) {
            ctx->mVSyncThread->requestExitAndWait();
        }
        free(ctx);
    }
    return 0;
}
static void hwc_registerProcs(struct hwc_composer_device* dev,
                                    hwc_procs_t const* procs)
{
    struct hwc_context_t *hwc_dev = (struct hwc_context_t *) dev;

    hwc_dev->procs = (typeof(hwc_dev->procs)) procs;
}

/*****************************************************************************/

static int hwc_device_open(const struct hw_module_t* module, const char* name,
        struct hw_device_t** device)
{
    int status = -EINVAL;
    if (!strcmp(name, HWC_HARDWARE_COMPOSER)) {
        struct hwc_context_t *dev;
        dev = (hwc_context_t*)malloc(sizeof(*dev));

        /* initialize our state here */
        memset(dev, 0, sizeof(*dev));

        /* initialize the procs */
        dev->device.common.tag = HARDWARE_DEVICE_TAG;
        dev->device.common.version = HWC_DEVICE_API_VERSION_0_3;
        dev->device.common.module = const_cast<hw_module_t*>(module);
        dev->device.common.close = hwc_device_close;

        dev->device.prepare = hwc_prepare;
        dev->device.set = hwc_set;
        dev->device.registerProcs = hwc_registerProcs;
        dev->device.methods = &hwc_device_methods;
        *device = &dev->device.common;
        dev->mVSyncThread = new VSyncThread(dev);
        status = 0;
    }
    return status;
}

