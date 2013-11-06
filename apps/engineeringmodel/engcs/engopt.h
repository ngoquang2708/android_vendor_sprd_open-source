#ifndef _ENG_OPT_H
#define _ENG_OPT_H

#include <stdio.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif
#ifdef LOG_TAG
#undef LOG_TAG
#endif

#define LOG_TAG 	"SPRDENG"
#include <utils/Log.h>
#define ENG_AT_LOG  ALOGD

#define ENG_TRACING

#ifdef ENG_TRACING
#define ENG_LOG  ALOGD
#else
//#define ENG_LOG  ALOGD
#define  ENG_LOG(format, ...)
#endif

#define ENG_APPCLIENT     "appclient"
#define ENG_PCCLIENT       "pcclient"
#define ENG_MODEM           "modem"
#define ENG_ADRV              "adrvclient"
#define ENG_LOGECLIENT   "logclient"
#define ENG_MONITOR       "engmonitor"
#define ENG_WELCOME	  "welcome"
#define ENG_ERROR	  	  "error"
#define ENG_DESTERROR	  "desterr"
#define ENG_EXIT               "engexit"

#define ENG_EUT			"EUT"
#define ENG_EUT_REQ		"EUT?"
#define ENG_WIFICH_REQ		"CH?"
#define ENG_WIFICH		"CH"
#define ENG_WIFIMODE    "MODE"
#define ENG_WIFIRATIO_REQ   "RATIO?"
#define ENG_WIFIRATIO   "RATIO"
#define ENG_WIFITX_FACTOR_REQ "TXFAC?"
#define ENG_WIFITX_FACTOR "TXFAC"
#define ENG_WIFITX_REQ      "TX?"
#define ENG_WIFITX      "TX"
#define ENG_WIFIRX_REQ      "RX?"
#define ENG_WIFIRX      "RX"
#define ENG_WIFIRX_PACKCOUNT	"RXPACKCOUNT?"
#define ENG_WIFI_CLRRXPACKCOUNT	"CLRRXPACKCOUNT"
#define ENG_GPSSEARCH_REQ	"SEARCH?"
#define ENG_GPSSEARCH	"SEARCH"
#define ENG_GPSPRNSTATE_REQ	"PRNSTATE?"
#define ENG_GPSSNR_REQ	"SNR?"
#define ENG_GPSPRN	"PRN"

typedef enum{
	EUT_REQ_INDEX = 0,
	EUT_INDEX,
	WIFICH_REQ_INDEX,
	WIFICH_INDEX,
	WIFIMODE_INDEX,
	WIFIRATIO_REQ_INDEX,
	WIFIRATIO_INDEX,
	WIFITX_FACTOR_REQ_INDEX,
	WIFITX_FACTOR_INDEX,
	WIFITX_REQ_INDEX,
	WIFITX_INDEX,
	WIFIRX_REQ_INDEX,
	WIFIRX_INDEX,
	WIFIRX_PACKCOUNT_INDEX,
	WIFICLRRXPACKCOUNT_INDEX,
	GPSSEARCH_REQ_INDEX,
	GPSSEARCH_INDEX,
	GPSPRNSTATE_REQ_INDEX,
	GPSSNR_REQ_INDEX,
	GPSPRN_INDEX,
}eut_cmd_enum;

typedef enum{
	BT_MODULE_INDEX=0,
	WIFI_MODULE_INDEX,
	GPS_MODULE_INDEX,
}eut_modules;

struct eut_cmd{
	int index;
	char *name;
};

typedef enum{
	CP_TD = 0,
	CP_WCDMA ,
}eng_cp_type;

typedef enum{
	CONNECT_UART  = 0,
	CONNECT_USB,
	CONNECT_PIPE,
}eng_connect_type;

struct eng_param{
	int califlag;
	int engtest;
	int cp_type;                /*td: CP_TD; wcdma:CP_WCDMA*/
	int connect_type;     /*usb:CONNECT_USB ; uart:CONNECT_UART*/
	int nativeflag;         /*0: vlx, CP directly communicates with PC tool
                                *1: native, AP directly communicates with PC tool  */
};

typedef  pthread_t                 eng_thread_t;

typedef void*  (*eng_thread_func_t)( void*  arg );

int  eng_thread_create( eng_thread_t  *pthread, eng_thread_func_t  start, void*  arg );

int  eng_open( const char*  pathname, int  options );
int  eng_read(int  fd, void*  buf, size_t  len);
int  eng_write(int  fd, const void*  buf, size_t  len);
int  eng_shutdown(int fd);
int  eng_close(int fd);

typedef  pthread_mutex_t          eng_mutex_t;
#define  ENG_MUTEX_INITIALIZER    PTHREAD_MUTEX_INITIALIZER
#define  eng_mutex_init           pthread_mutex_init
#define  eng_mutex_lock           pthread_mutex_lock
#define  eng_mutex_unlock         pthread_mutex_unlock
#define  eng_mutex_destroy        pthread_mutex_destroy

#define  ENG_MUTEX_DEFINE(m)      static eng_mutex_t   m = PTHREAD_MUTEX_INITIALIZER

#ifdef __cplusplus
}
#endif

#endif
