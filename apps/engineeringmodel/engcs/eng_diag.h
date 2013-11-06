
#ifndef __ENG_DIAG_H__

#define __ENG_DIAG_H__

/*got it from tool*/
#define DIAG_CHANGE_MODE_F  12
typedef enum
{
	NORMAL_MODE                         = 0,
	LAYER1_TEST_MODE                    = 1,
	ASSERT_BACK_MODE                    = 2,
	CALIBRATION_MODE                    = 3,
	DSP_CODE_DOWNLOAD_BACK              = 4,
	DSP_CODE_DOWNLOAD_BACK_CALIBRATION  = 5,
	BOOT_RESET_MODE                     = 6,
	PRODUCTION_MODE                     = 7,
	RESET_MODE                          = 8,
	CALIBRATION_POST_MODE               = 9,
	PIN_TEST_MODE                       = 10,
	IQC_TEST_MODE                       = 11,
	WATCHDOG_RESET_MODE                 = 12,

	CALIBRATION_NV_ACCESS_MODE          = 13,
	CALIBRATION_POST_NO_LCM_MODE        = 14,

	TD_CALIBRATION_POST_MODE            = 15,
	TD_CALIBRATION_MODE                 = 16,
	TD_CALIBRATION_POST_NO_LCM_MODE     = 17,

	MODE_MAX_TYPE,

	MODE_MAX_MASK                       = 0x7F

}MCU_MODE_E;

#define MAX_IMEI_LENGTH		8
#define MAX_BTADDR_LENGTH	6
#define MAX_WIFIADDR_LENGTH	6
#define GPS_NVINFO_LENGTH	44
#define DIAG_HEADER_LENGTH	8

#define DIAG_CMD_VER		0x00
#define DIAG_CMD_IMEIBTWIFI	0x5E
#define DIAG_CMD_READ		0x80
#define DIAG_CMD_GETVOLTAGE	0x1E
#define DIAG_CMD_APCALI		0x62
#define DIAG_CMD_FACTORYMODE	0x0D
#define DIAG_CMD_ADC_F		0x0F  //add by kenyliu on 2013 07 12 for get ADCV  bug 188809
#define DIAG_CMD_AUDIO		0x68
#define DIAG_CMD_CHANGEMODE     DIAG_CHANGE_MODE_F



#define DIAG_CMD_IMEIBIT		0x01
#define DIAG_CMD_BTBIT			0x04
#define DIAG_CMD_WIFIBIT		0x40

#define AUDIO_NV_ARM_INDI_FLAG          0x02
#define AUDIO_ENHA_EQ_INDI_FLAG         0x04
#define AUDIO_DATA_READY_INDI_FLAG      (AUDIO_NV_ARM_INDI_FLAG|AUDIO_ENHA_EQ_INDI_FLAG)


#define ENG_TESTMODE			"engtestmode"
#define ENG_SPRD_VERS			"ro.build.description"

typedef enum
{
	CMD_COMMON=-1,
	CMD_USER_VER,
	CMD_USER_BTWIFI,
	CMD_USER_FACTORYMODE,
	CMD_USER_AUDIO,
	CMD_USER_RESET,
	CMD_USER_GETVOLTAGE,
	CMD_USER_APCALI,
	CMD_USER_ADC, //add by kenyliu on 2013 07 12 for get ADCV  bug 188809
	CMD_INVALID
}DIAG_CMD_TYPE;

#define ENG_DIAG_SIZE 4096
#define ENG_LINUX_VER	"/proc/version"
#define ENG_ANDROID_VER "ro.build.version.release"
#define ENG_AUDIO       "/sys/class/vbc_param_config/vbc_param_store"
#define ENG_FM_DEVSTAT	"/sys/class/fm_devstat_config/fm_devstat_store"



// This is the communication frame head
typedef struct msg_head_tag
{
	unsigned int  seq_num;      // Message sequence number, used for flow control
	unsigned short  len;          // The totoal size of the packet "sizeof(MSG_HEAD_T)
	                      // + packet size"
	unsigned char   type;         // Main command type
	unsigned char   subtype;      // Sub command type
}__attribute__((packed)) MSG_HEAD_T;

typedef struct {
	unsigned char byEngineSn[24];
	unsigned int  dwMapVersion;
	unsigned char byActivatecode[16];
}GPS_NV_INFO_T;

typedef struct {
	unsigned char imei1[MAX_IMEI_LENGTH];
	unsigned char imei2[MAX_IMEI_LENGTH];
	unsigned char btaddr[MAX_BTADDR_LENGTH];
	unsigned char gpsinfo[GPS_NVINFO_LENGTH];
	unsigned char wifiaddr[MAX_WIFIADDR_LENGTH];
	unsigned char reserved1[2];
	unsigned char imei3[MAX_IMEI_LENGTH];
	unsigned char imei4[MAX_IMEI_LENGTH];
	unsigned char reserved2[16];
}REF_NVWriteDirect_T;



int eng_diag(char *buf,int len);
int eng_diag_write2pc(int fd);
int eng_diag_writeimei(char *req, char *rsp);
void *eng_vlog_thread(void *x);
void * eng_getpara(void);
void *eng_vdiag_thread(void *x);
void * eng_sd_log(void * args);

#endif
