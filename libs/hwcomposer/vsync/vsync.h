#ifndef _SPRD_HW_VSYNC_H_
#define _SPRD_HW_VSYNC_H_
#include <sys/types.h>

#include "Thread.h"
#include <hardware/hwcomposer.h>
using namespace android;

class VSyncThread: public Thread {
    struct hwc_context_t *mDev;
    mutable Mutex mLock;
    Condition mCondition;
    bool mEnabled;
    virtual void onFirstRef();
    virtual bool threadLoop();
    int getVSyncPeriod();
    int mFbFd;
    nsecs_t mVSyncPeriod;
public:
    VSyncThread(struct hwc_context_t *hwc);
    ~VSyncThread();
    void setEnabled(bool enabled);
};
#endif
