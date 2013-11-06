#include "vsync.h"
#include <hardware/hwcomposer.h>
#include <cutils/log.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>

#ifdef USE_SPRD_HWCOMPOSER
#include "hwcomposer_sprd.h"
#else
#include "hwcomposer_android.h"
#endif
using namespace android;
extern "C" int clock_nanosleep(clockid_t clock_id, int flags,
                           const struct timespec *request,
                           struct timespec *remain);


VSyncThread::VSyncThread(struct hwc_context_t* dev)
    : mDev(dev), mEnabled(false)
{
    char const * const device_template[] =
    {
        "/dev/graphics/fb%u",
        "/dev/fb%u",
        NULL
    };
    int fd = -1;
    int i = 0;
    char name[64];
    while ((fd == -1) && device_template[i])
    {
        snprintf(name, 64, device_template[i], 0);
        fd = open(name, O_RDWR, 0);
        i++;
    }
    if (fd < 0)
    {
        ALOGE("fail to open fb");
    }
    mFbFd = fd;
    getVSyncPeriod();
}
VSyncThread::~VSyncThread()
{
	if(mFbFd >= 0)
		close(mFbFd);
}

void VSyncThread::onFirstRef() {
    run("VSyncThread", PRIORITY_URGENT_DISPLAY + PRIORITY_MORE_FAVORABLE);
}
void VSyncThread::setEnabled(bool enabled) {
    Mutex::Autolock _l(mLock);
    mEnabled = enabled;
    mCondition.signal();
}

bool VSyncThread::threadLoop() {
    { // scope for lock
        Mutex::Autolock _l(mLock);
        while (!mEnabled) {
            mCondition.wait(mLock);
        }
    }
	  //8810 use sleep mode
#ifdef _VSYNC_USE_SOFT_TIMER
    static nsecs_t netxfakevsync = 0;
    const nsecs_t period = mVSyncPeriod;;
    const nsecs_t now = systemTime(CLOCK_MONOTONIC);
    nsecs_t next_vsync = netxfakevsync;
    nsecs_t sleep = next_vsync - now;
    if (sleep < 0) {
        // we missed, find where the next vsync should be
        sleep = (period - ((now - next_vsync) % period));
        next_vsync = now + sleep;
    }
    netxfakevsync = next_vsync + period;

    struct timespec spec;
    spec.tv_sec  = next_vsync / 1000000000;
    spec.tv_nsec = next_vsync % 1000000000;

    int err;
    do {
        err = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &spec, NULL);
    } while (err<0 && errno == EINTR);

    if (err == 0) {
	  if(!mDev->procs || !mDev->procs->vsync)
	  	return true;
        mDev->procs->vsync(mDev->procs, 0, next_vsync);
    }
#else //8825 use driver vsync mode now use sleep for temporaryly
#if 1
    static nsecs_t netxfakevsync = 0;
    const nsecs_t period = mVSyncPeriod;
    const nsecs_t now = systemTime(CLOCK_MONOTONIC);
    nsecs_t next_vsync = netxfakevsync;
    nsecs_t sleep = next_vsync - now;
    if (sleep < 0) {
        // we missed, find where the next vsync should be
        sleep = (period - ((now - next_vsync) % period));
        next_vsync = now + sleep;
    }
    netxfakevsync = next_vsync + period;

    struct timespec spec;
    spec.tv_sec  = next_vsync / 1000000000;
    spec.tv_nsec = next_vsync % 1000000000;

    int err;
    do {
        err = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &spec, NULL);
    } while (err<0 && errno == EINTR);

    if (err == 0) {
	  if(!mDev->procs || !mDev->procs->vsync)
	  	return true;
        mDev->procs->vsync(mDev->procs, 0, next_vsync);
    }
	//may open when driver ready
#else
    if (ioctl(mFbFd, FBIO_WAITFORVSYNC, NULL) == -1)  
    {  
        ALOGE("fail to wait vsync , mFbFd:%d" , mFbFd);  
    }  
    else  
    {  
	  if(!mDev->procs || !mDev->procs->vsync)
	  {
	  	ALOGW("device procs or vsync is null procs:%x , vsync:%x", mDev->procs , mDev->procs->vsync);
	  	return true;
	  }
         mDev->procs->vsync(mDev->procs, 0, systemTime(CLOCK_MONOTONIC));  
    }  
#endif
#endif
    return true;
}
int VSyncThread::getVSyncPeriod()
{
	struct fb_var_screeninfo info;
	if (ioctl(mFbFd, FBIOGET_VSCREENINFO, &info) == -1)
	{
		return -errno;
	}

	int refreshRate = 0;
	if ( info.pixclock > 0 )
	{
		refreshRate = 1000000000000000LLU /
		(
			uint64_t( info.upper_margin + info.lower_margin + info.yres )
			* ( info.left_margin  + info.right_margin + info.xres )
			* info.pixclock
		);
	}
	else
	{
		ALOGW( "fbdev pixclock is zero for fd: %d", mFbFd );
	}

	if (refreshRate == 0)
	{
		ALOGW("getVsyncPeriod refreshRate use fake rate, 60HZ");
		refreshRate = 60*1000;  // 60 Hz
	}
	float fps  = refreshRate / 1000.0f;
	mVSyncPeriod = nsecs_t(1e9 / fps);
	return 0;
}

