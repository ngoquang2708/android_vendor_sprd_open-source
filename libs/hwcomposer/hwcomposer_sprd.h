#ifndef _HWCOMPOSER_SPRD_H_
#define _HWCOMPOSER_SPRD_H_
#include <pthread.h>
#include <semaphore.h>
#include <binder/MemoryHeapIon.h>
#include "vsync.h"
#ifdef _HWCOMPOSER_USE_GSP
#include "sc8830/gsp_hal.h"
#endif

#ifdef OVERLAY_COMPOSER_GPU
#include "OverlayComposer/OverlayComposer.h"
#endif

struct sprd_img {
    uint32_t w;
    uint32_t h;
    uint32_t format;
    uint32_t y_addr;
    uint32_t u_addr;
    uint32_t v_addr;
};

struct hwc_context_t {
    hwc_composer_device_t device;

    hwc_procs_t *procs;
    /* our private state goes below here */
    int fb_layer_count;
    int fbfd;
    int fb_width;
    int fb_height;
    int pre_fb_layer_count;

#ifdef _HWCOMPOSER_USE_GSP
	gsp_device_t *gsp_dev;
#endif
    //the following are for osd overlay layer
    int osd_overlay_flag;
    int osd_overlay_phy_addr;
    int YUVLayerCount;
    bool RGBLayerFullScreen;
    //the following are for video overlay layer
    int video_overlay_flag;
    sp<MemoryHeapIon> ion_heap;
    int overlay_phy_addr;
    void *overlay_v_addr;
    uint32_t overlay_buf_size;
    int    overlay_index;
	
    sp<MemoryHeapIon> ion_heap2;
    int overlay_phy_addr2;
    void *overlay_v_addr2;
    uint32_t overlay_buf_size2;
    int    overlay_index2;
    int    osd_sync_display;

#ifdef SCAL_ROT_TMP_BUF
    sp<MemoryHeapIon> ion_heap_tmp;
    int overlay_phy_addr_tmp;
    void *overlay_v_addr_tmp;
    uint32_t overlay_buf_size_tmp;
#endif

    struct sprd_img src_img;
    struct sprd_rect src_rect;//scaling&rot input

    struct sprd_rect fb_rect;//fb position

#ifdef _PROC_OSD_WITH_THREAD
    pthread_t  osd_proc_thread;
    sem_t         cmd_sem;
    sem_t         done_sem;
    volatile void * osd_proc_cmd;
#endif
    sp<VSyncThread> mVSyncThread;
    void *fb_virt_addr;

#ifdef OVERLAY_COMPOSER_GPU
    overlayDevice_t OD;
#endif
};
#endif
