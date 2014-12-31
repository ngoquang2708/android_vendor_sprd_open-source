#ifndef _FACTORY_H
#define _FACTORY_H
/**************Backlight************/
#define LCD_BACKLIGHT_DEV			"/sys/class/backlight/sprd_backlight/brightness"
#define KEY_BACKLIGHT_DEV 			"/sys/class/leds/keyboard-backlight/brightness"
#define LCD_BACKLIGHT_MAX_DEV		"/sys/class/backlight/sprd_backlight/max_brightness"
#define KEY_BACKLIGHT_MAX_DEV 		"/sys/class/leds/keyboard-backlight/max_brightness"
/*************end Backlight*********/

/************Bcamera************/
#define SPRD_DBG	LOGD
#define LIBRARY_PATH "/system/lib/hw/"


#define FLASH_SUPPORT "/sys/class/flash_test/flash_test/flash_value"
#define FLASH_YES_NO "flash_mode is :0x0"

/*********end Bcamera***********/


/************BT*************/
#define SPRD_DBG LOGD
/************************************************************************************
 **  Constants & Macros
 ************************************************************************************/
#define SPRD_BT_TITLE_Y     40
#define SPRD_BT_TITLE       "SpreadTrum BT Test"

/* Common Bluetooth field definitions */
#define BT_MAC_FILE_PATH  "/data/misc/bluedroid/btmac.txt"
#define BD_ADDR_LEN     6                         /* Device address length */
#define BD_NAME_LEN     248                       /* Device name length */
typedef unsigned char BD_ADDR[BD_ADDR_LEN];       /* Device address */
typedef unsigned char*BD_ADDR_PTR;                /* Pointer to Device Address */
typedef unsigned char BD_NAME[BD_NAME_LEN + 1];   /* Device name */
typedef unsigned char*BD_NAME_PTR;                /* Pointer to Device name */
#define BT_MAC_STR_LEN     (17)
#define MAX_REMOTE_DEVICES (10)
#define CASE_RETURN_STR(const) case const: return #const;

//The time of BT Discovery
#define BT_DISCOVERY_TIME    2
#define BT_ENABLE_EVNET             1
#define BT_DISABLE_EVNET            2
#define BT_DISCOVERY_START_EVNET    3
#define BT_DISCOVERY_STOP_EVNET     4

/******************BT end***************/

/**************charge******************/
#define SPRD_CALI_MASK			0x00000200

#define ENG_BATVOL		"/sys/class/power_supply/battery/real_time_voltage"
#define ENG_CHRVOL		"/sys/class/power_supply/battery/charger_voltage"
#define ENG_CURRENT		"/sys/class/power_supply/battery/real_time_current"
#define ENG_USBONLINE	"/sys/class/power_supply/usb/online"
#define ENG_ACONLINE	"/sys/class/power_supply/ac/online"
/****************end charge*************/


/***************factorytest.c************/
#define PCBATXTPATH "/productinfo/PCBAtest.txt"
#define PHONETXTPATH   "/productinfo/wholephonetest.txt"
/****************end*****************/


/**************FM******************/

#define FM_INSMOD_COMMEND    "insmod /system/lib/modules/trout_fm.ko"

#define FM_IOCTL_BASE     'R'
#define FM_IOCTL_ENABLE      _IOW(FM_IOCTL_BASE, 0, int)
#define FM_IOCTL_GET_ENABLE  _IOW(FM_IOCTL_BASE, 1, int)
#define FM_IOCTL_SET_TUNE    _IOW(FM_IOCTL_BASE, 2, int)
#define FM_IOCTL_GET_FREQ    _IOW(FM_IOCTL_BASE, 3, int)
#define FM_IOCTL_SEARCH      _IOW(FM_IOCTL_BASE, 4, int[4])
#define FM_IOCTL_STOP_SEARCH _IOW(FM_IOCTL_BASE, 5, int)
#define FM_IOCTL_SET_VOLUME  _IOW(FM_IOCTL_BASE, 7, int)
#define FM_IOCTL_GET_VOLUME  _IOW(FM_IOCTL_BASE, 8, int)
#define Trout_FM_IOCTL_CONFIG   _IOW(FM_IOCTL_BASE, 9, int)
#define Trout_FM_IOCTL_GET_RSSI _IOW(FM_IOCTL_BASE, 10, int)


#define TROUT_FM_DEV_NAME   		"/dev/Trout_FM" //
//#define SPRD_HEADSET_SWITCH_DEV     "/sys/class/switch/h2w/state"
#define SPRD_HEADSETOUT             0
#define SPRD_HEADSETIN              1
//#define FM_CLOSE     				0
//#define FM_PLAY     				1
//#define FM_PLAY_ERR     			2
#define HEADSET_CLOSE   			0
#define HEADSET_OPEN    			1
#define HEADSET_CHECK   			2
#define STATE_CLEAN     			0
#define STATE_DISPLAY   			1




#define V4L2_CID_PRIVATE_BASE           0x8000000
#define V4L2_CID_PRIVATE_TAVARUA_STATE  (V4L2_CID_PRIVATE_BASE + 4)

#define V4L2_CTRL_CLASS_USER            0x980000
#define V4L2_CID_BASE                   (V4L2_CTRL_CLASS_USER | 0x900)
#define V4L2_CID_AUDIO_VOLUME           (V4L2_CID_BASE + 5)
#define V4L2_CID_AUDIO_MUTE             (V4L2_CID_BASE + 9)


#define START_FRQ	10410
#define END_FRQ     8750
#define THRESH_HOLD 100
#define DIRECTION   128
#define SCANMODE    0
#define MUTI_CHANNEL false
#define CONTYPE     0
#define CONVALUE    0



/******************END*******************/


/****************GPS****************/
#define SPRD_GPS_TITLE_Y 	40
#define SPRD_GPS_TITLE 		"SpreadTrum GPS Test"

#define FUN_ENTER	LOGD("%s enter \n",__FUNCTION__);
#define FUN_EXIT	LOGD("%s exit \n",__FUNCTION__);
#define SPRD_DBG	LOGD
#define GPSNATIVETEST_WAKE_LOCK_NAME  "gps_native_test"
#define GPS_TEST_PASS  TEXT_TEST_PASS   //"PASS"
#define GPS_TEST_FAILED  TEXT_TEST_FAIL //"FAILED"
#define GPS_TESTING  TEXT_BT_SCANING    //"TESTING......"
#define GPS_TEST_TIME_OUT	     (30) // s120
/********************end gps*************/



/*****************Gsensor*************/
#define SPRD_GSENSOR_DEV					"/dev/lis3dh_acc"
#define	GSENSOR_IOCTL_BASE 77
#define GSENSOR_IOCTL_GET_XYZ           _IOW(GSENSOR_IOCTL_BASE, 22, int)
#define GSENSOR_IOCTL_SET_ENABLE      	_IOW(GSENSOR_IOCTL_BASE, 2, int)
#define LIS3DH_ACC_IOCTL_GET_CHIP_ID    _IOR(GSENSOR_IOCTL_BASE, 255, char[32])

#define SPRD_GSENSOR_OFFSET					60
#define SPRD_GSENSOR_1G						1024
#define GSENSOR_TIMEOUT                     20
/***************end gsensor************/


/****************Headerset***********/
#define SPRD_HEADSET_SWITCH_DEV 		"/sys/class/switch/h2w/state"
#define SPRD_HEASETKEY_DEV				"headset-keyboard"
#define SPRD_HEADSET_KEY	   			KEY_MEDIA
#define SPRD_HEADSET_KEYLONGPRESS		KEY_END
/**************end Headerset***********/



/***************key**************/
#define KEY_TIMEOUT 20
/***************end key**********/


/**************Lsensor**********/
#define SPRD_PLS_CTL				"/dev/ltr_558als"
#define SPRD_PLS_LIGHT_THRESHOLD	30
#define SPRD_PLS_INPUT_DEV			"alps_pxy"

#define LTR_IOCTL_MAGIC         0x1C
#define LTR_IOCTL_GET_PFLAG	    _IOR(LTR_IOCTL_MAGIC, 1, int)
#define LTR_IOCTL_GET_LFLAG     _IOR(LTR_IOCTL_MAGIC, 2, int)
#define LTR_IOCTL_SET_PFLAG     _IOW(LTR_IOCTL_MAGIC, 3, int)
#define LTR_IOCTL_SET_LFLAG     _IOW(LTR_IOCTL_MAGIC, 4, int)
#define LTR_IOCTL_GET_DATA      _IOW(LTR_IOCTL_MAGIC, 5, unsigned char)
/**************end Lsensor********/


/************modem*************/
#if 0
#define PROP_MODEM_W_COUNT  "ro.modem.t.count"
static char* modem_port[] = {
	"/dev/stty_td1",
	"/dev/stty_td4"
};
#else
#define PROP_MODEM_W_COUNT  "ro.modem.tl.count"
static char* modem_port[] = {
	"/dev/stty_lte1",
	"/dev/stty_lte4"
};

#endif
#define MAX_MODEM_COUNT 	2

#define SCREEN_TEXT_LEN		40
#define TITLE_Y_POS 		30
/*************end modem************/


/***************sdcard*************/
#ifdef CONFIG_NAND
#define SPRD_SD_DEV			"/dev/block/mmcblk0p1"
#define SPRD_MOUNT_DEV		"mount -t vfat /dev/block/mmcblk0p1  /sdcard"
#else
#define SPRD_SD_DEV			"/dev/block/mmcblk1p1"
#define SPRD_MOUNT_DEV		"mount -t vfat /dev/block/mmcblk1p1  /sdcard"
#endif
#define SPRD_UNMOUNT_DEV	"unmount /sdcard"

#define SPRD_SDCARD_PATH	"/sdcard"
#define SPRD_SD_TESTFILE	"/sdcard/test.txt"
#define SPRD_SD_FMTESTFILE  "/sdcard/fmtest.txt"

#define RW_LEN	512
/******************end sdcard*************/


/*****************speaker***************/

#define CARD_SPRDPHONE "sprdphone"

/***************end spreaker**************/


/*************touch pannel***************/
#define SPRD_TS_INPUT_DEV			"focaltech_ts"

#define SPRD_TS_MODULE              "insmod /system/lib/modules/focaltech_ts.ko"

#define ABS_MT_POSITION_X			0x35	/* Center X ellipse position */
#define ABS_MT_POSITION_Y			0x36	/* Center Y ellipse position */
#define ABS_MT_TOUCH_MAJOR			0x30
#define SYN_REPORT					0
#define SYN_MT_REPORT				2
/****************end tp***************/

/**************UI*****************/
#define MAX_COLS 64
#define MAX_ROWS 32

#define CHAR_WIDTH 18
#define CHAR_HEIGHT 35

#define PASS_POSITION (8*CHAR_HEIGHT)
#define FAIL_POSITION (16*CHAR_WIDTH)

#define PROGRESSBAR_INDETERMINATE_STATES 6
#define PROGRESSBAR_INDETERMINATE_FPS 15
/**************end UI**************/


/****************version**********/
#define PROP_ANDROID_VER	"ro.build.version.release"
#define PROP_SPRD_VER		"ro.build.description"
#define PATH_LINUX_VER		"/proc/version"
/**************end version***********/


/***************vibrator***********/
#define VIBRATOR_ENABLE_DEV			"/sys/class/timed_output/vibrator/enable"
/*************end vibartor********/



/************wifi*****************/
#define SPRD_ERR        LOGE

#define SPRD_WIFI_TITLE 	"SpreadTrum WIFI Test...."
#define SPRD_WIFI_WAIT 		"Please Waiting...."
#define SPRD_WIFI_ERROR 	"WIFI Test Error!!"
#define SPRD_WIFI_TIMEOUT 	"Time Out, Rechoose wifi item!!"
#define SPRD_WIFI_PASSED 	"WIFI PASS..."


#define ENG_WIFI_ONCE
/*****************end wifi************/


/****************OTG*************/
#define OTG_FILE_PATH  "/sys/bus/platform/drivers/dwc_otg/is_support_otg"
#define OTG_TESTFILE_PATH "/system/bin/test"
/****************end OTG**********/


/**************telephnoy*********/
#define TEL_DEVICE_PATH "/dev/stty_lte31"
/**************end tel**********/

#endif
