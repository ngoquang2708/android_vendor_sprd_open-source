#ifndef __ENG_PCCLIENT_H__
#define __ENG_PCCLIENT_H__

#define ENG_CALISTR				"calibration"
#define ENG_TESTMODE			"engtestmode"
#define ENG_STREND				"\r\n"
#define ENG_USBIN				"/sys/class/android_usb/android0/state"
#define ENG_USBCONNECTD			"CONFIGURED"
#define ENG_FACOTRYMODE_FILE	"/productinfo/factorymode.file"
#define ENG_FACOTRYSYNC_FILE	"/factorysync.file"
#define ENG_MODEMRESET_PROPERTY	"persist.sys.sprd.modemreset"
#define ENG_USB_PROPERTY	    "persist.sys.sprd.usbfactorymode"
#define RAWDATA_PROPERTY		"sys.rawdata.ready"
#define ENG_ATDIAG_AT			"AT+SPBTWIFICALI="
#define ENG_BUFFER_SIZE			2048
#define ENG_CMDLINE_LEN			1024
#define ENG_DEV_PATH_LEN        80

enum {
    ENG_CMDERROR = -1,
    ENG_CMD4LINUX = 0,
    ENG_CMD4MODEM
};

enum {
    ENG_RUN_TYPE_TD,
    ENG_RUN_TYPE_WCDMA,
    ENG_RUN_TYPE_BTWIFI
};

extern char* s_connect_ser_path[];
extern char* s_cp_pipe[];
extern char* s_at_ser_path;

#endif
