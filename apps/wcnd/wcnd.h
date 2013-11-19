#ifndef __WCND_H__
#define __WCND_H__

#define WCND_DEBUG
#define WCND_CONFIG_FILE_PATH "/data/misc/wifi/wifimac.txt"
#define WCND_FACTORY_CONFIG_FILE_PATH "/productinfo/wifimac.txt"
#define MAC_LEN 6

#ifdef WCND_DEBUG
#define WCND_LOGD(x...) ALOGD( x )
#define WCND_LOGE(x...) ALOGE( x )
#else
#define WCND_LOGD(x...) do {} while(0)
#define WCND_LOGE(x...) do {} while(0)
#endif

#endif
