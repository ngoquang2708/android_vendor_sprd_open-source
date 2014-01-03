#ifndef __WCND_H__
#define __WCND_H__

#define WCND_DEBUG
#define WCND_WIFI_CONFIG_FILE_PATH "/data/misc/wifi/wifimac.txt"
#define WCND_WIFI_FACTORY_CONFIG_FILE_PATH "/productinfo/wifimac.txt"
#define WCND_BT_CONFIG_FILE_PATH "/data/misc/bluedroid/btmac.txt"
#define WCND_BT_FACTORY_CONFIG_FILE_PATH "/productinfo/btmac.txt"
#define MAC_LEN 6

#ifdef WCND_DEBUG
#define WCND_LOGD(x...) ALOGD( x )
#define WCND_LOGE(x...) ALOGE( x )
#else
#define WCND_LOGD(x...) do {} while(0)
#define WCND_LOGE(x...) do {} while(0)
#endif

#endif
