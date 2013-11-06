#ifndef _HWCOMPOSER_ANDROID_H_
#define _HWCOMPOSER_ANDROID_H_
#include <hardware/hwcomposer.h>
#include "vsync.h"
struct hwc_context_t {
    hwc_composer_device_t device;
    /* our private state goes below here */
    hwc_procs_t *procs;
    sp<VSyncThread> mVSyncThread;
};
#endif
