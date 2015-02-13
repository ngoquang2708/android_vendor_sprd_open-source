#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>
#include <private/android_filesystem_config.h>
#include <android/log.h>
#include <hardware/hardware.h>
#include <hardware/bluetooth.h>
#include <hardware/bt_fm.h>
#include <hardware/bt_sock.h>
#include "btif_util.h"
#include <hardware/fm.h>
#include <cutils/properties.h>
#include "eng_diag.h"
#include "audioserver/audio_server.h"

#define FM_CMD_STATE 0x00  //open/close fm
#define FM_CMD_VOLUME 0x01  //set FM volume
#define FM_CMD_MUTE   0x02   // set FM mute
#define FM_CMD_TUNE  0x03   //get a single frequency information
#define FM_CMD_SEEK   0x04  //
#define FM_CMD_READ_REG  0x05
#define FM_CMD_WRITE_REG  0x06
#define FM_SUCCESS 0
#define FM_FAILURE  1
#define FM_OPEN 1
#define FM_CLOSE 0

enum fmStatus {
FM_STATE_DISABLED,
FM_STATE_ENABLED,
FM_STATE_PLAYING,
FM_STATE_STOPED,
FM_STATE_PANIC,
FM_STATE_ERR,
};
typedef struct _FM_SIGNAL_PARAM_T
{
    unsigned char  nOperInd; 	// 	Tune: 	0: tune successful;
  			     		//			1: tune failure
						//	Seek: 	0: seek out valid channel successful
	                    //			1: seek out valid channel failure	
    unsigned short  nStereoInd;			// 	0: Stereo; Other: Mono;		
    unsigned int 	nRssi;  			// 	RSSI Value
    unsigned int	nFreqValue; 		// 	Frequency, Unit:KHz
    unsigned int	nPwrIndicator; 	    // 	Power indicator
    unsigned int	nFreqOffset; 		// 	Frequency offset 
    unsigned int	nPilotDet; 			// 	pilot_det
    unsigned int	nNoDacLpf; 			// 	no_dac_lpf
			
} FM_SIGNAL_PARAM_T;

typedef enum {
	   AUDIO_POLICY_DEVICE_STATE_UNAVAILABLE,
	   AUDIO_POLICY_DEVICE_STATE_AVAILABLE, 																												 
 
	   AUDIO_POLICY_DEVICE_STATE_CNT,
	   AUDIO_POLICY_DEVICE_STATE_MAX = AUDIO_POLICY_DEVICE_STATE_CNT - 1,
 }audio_policy_dev_state_t;

 /* usages used for audio_policy->set_force_use() */
typedef enum {
	   AUDIO_POLICY_FORCE_FOR_COMMUNICATION,
	   AUDIO_POLICY_FORCE_FOR_MEDIA,
	   AUDIO_POLICY_FORCE_FOR_RECORD,
	   AUDIO_POLICY_FORCE_FOR_DOCK,
	   AUDIO_POLICY_FORCE_FOR_SYSTEM,

	   AUDIO_POLICY_FORCE_USE_CNT,
	   AUDIO_POLICY_FORCE_USE_MAX = AUDIO_POLICY_FORCE_USE_CNT - 1,
 } audio_policy_force_use_t;

 /* device categories used for audio_policy->set_force_use() */
 typedef enum {
	   AUDIO_POLICY_FORCE_NONE,
	   AUDIO_POLICY_FORCE_SPEAKER,
	   AUDIO_POLICY_FORCE_HEADPHONES,
	   AUDIO_POLICY_FORCE_BT_SCO,
	   AUDIO_POLICY_FORCE_BT_A2DP,
       AUDIO_POLICY_FORCE_WIRED_ACCESSORY,
	   AUDIO_POLICY_FORCE_BT_CAR_DOCK,
	   AUDIO_POLICY_FORCE_BT_DESK_DOCK,
	   AUDIO_POLICY_FORCE_ANALOG_DOCK,
	   AUDIO_POLICY_FORCE_DIGITAL_DOCK,
	   AUDIO_POLICY_FORCE_NO_BT_A2DP, /* A2DP sink is not preferred to speaker or wired HS */
	   AUDIO_POLICY_FORCE_SYSTEM_ENFORCED,
 
	   AUDIO_POLICY_FORCE_CFG_CNT,
	   AUDIO_POLICY_FORCE_CFG_MAX = AUDIO_POLICY_FORCE_CFG_CNT - 1,
 
	   AUDIO_POLICY_FORCE_DEFAULT = AUDIO_POLICY_FORCE_NONE,
 } audio_policy_forced_cfg_t;

#ifndef AUDIO_DEVICE_OUT_FM_HEADSET
#define AUDIO_DEVICE_OUT_FM_HEADSET 0x1000000
#endif
#ifndef AUDIO_DEVICE_OUT_FM_SPEAKER
#define AUDIO_DEVICE_OUT_FM_SPEAKER 0x2000000
#endif

#ifndef BTA_FM_OK
#define BTA_FM_OK 0
#endif
#ifndef BTA_FM_SCAN_ABORT
#define	BTA_FM_SCAN_ABORT 3
#endif
#define SEARCH_DIRECTION_UP  128

#define SEARCH_DIRECTION_DOWN 0

static hw_module_t * s_hwModule = NULL;
static hw_device_t * s_hwDev    = NULL;

static int mMinFreq = 70000;
static int mMaxFreq = 108000;

static bluetooth_device_t* sBtDevice = NULL;
static const bt_interface_t* sBtInterface = NULL;
static const btfm_interface_t* sFmInterface = NULL;
static int sFmStatus = FM_STATE_PANIC;
static int sFmSearchStatus = FM_STATE_PANIC;
static int sFmTuneStatus = FM_STATE_PANIC;
static int sFmMuteStatus = FM_STATE_PANIC;
static int sFmVolumeStatus = FM_STATE_PANIC;

static int mFmTune = 69500;
static FM_SIGNAL_PARAM_T mFmSignalParm;
int fmClose( void );
int fmOpen();
int fmPlay( uint freq );
int fmstop();
void btfmEnableCallback (int status)  
{  
    if (status == BT_STATUS_SUCCESS)
    {
		sFmStatus = FM_STATE_ENABLED;
    }

    ALOGI("Enable callback, status: %d", sFmStatus); 
}

void btfmDisableCallback (int status)
{ 
	if (status == BT_STATUS_SUCCESS) sFmStatus = FM_STATE_DISABLED;  
	ALOGI("Disable callback, status: %d", sFmStatus); 
}

void btfmtuneCallback (int status, int rssi, int snr, int freq) 
{ 
   if(status == BTA_FM_OK)
   {
   		mFmSignalParm.nOperInd = FM_SUCCESS;
		mFmSignalParm.nRssi = rssi;
		mFmSignalParm.nFreqValue = freq;
    	sFmTuneStatus = FM_STATE_ENABLED;
   }
   else 
   {
		mFmSignalParm.nOperInd = FM_FAILURE;
		sFmTuneStatus = FM_STATE_ERR;
   }
	ALOGI("Tune callback, status: %d, freq: %d rssi: %d", status, freq,rssi); 
}

void btfmMuteCallback (int status, BOOLEAN isMute)
{ 
	if(status == BTA_FM_OK)
	{

		 sFmMuteStatus = FM_STATE_ENABLED;
	}
	else 
	{
		 sFmTuneStatus = FM_STATE_ERR;
	}

	ALOGI("Mute callback, status: %d, isMute: %d", status, isMute); 
}

void btfmSearchCallback (int status, int rssi, int snr, int freq)
{ 
	ALOGI("Search callback, status: %d", status); 
}

void btfmSearchCompleteCallback(int status, int rssi, int snr, int freq)
{ 
   if(status == BTA_FM_OK)
   {
   		mFmSignalParm.nOperInd = FM_SUCCESS;
		mFmSignalParm.nRssi = rssi;
		mFmSignalParm.nFreqValue = freq;
    	sFmSearchStatus = FM_STATE_ENABLED;
   }
   else if(status == BTA_FM_SCAN_ABORT)
   {
        mFmSignalParm.nFreqValue = freq;
   		sFmSearchStatus = FM_STATE_STOPED;
   }
   else 
   {
		mFmSignalParm.nOperInd = FM_FAILURE;
		sFmSearchStatus = FM_STATE_ERR;
   }
	ALOGI("Search complete callback"); 
}

void btfmAudioModeCallback(int status, int audioMode)
{ 
	ALOGI("Audio mode change callback, status: %d, audioMode: %d", status, audioMode);
}

void btfmAudioPathCallback(int status, int audioPath)
{ 
	ALOGI("Audio path change callback, status: %d, audioPath: %d", status, audioPath); 
}

void btfmVolumeCallback(int status, int volume)
{ 
	if(status == BTA_FM_OK)
	{
	
		sFmVolumeStatus = FM_STATE_ENABLED;
	}
	else 
	{
		sFmVolumeStatus = FM_STATE_ERR;
	}

	ALOGI("Volume change callback, status: %d, volume: %d", status, volume);
}
void btAdapterStateChangedCallback(bt_state_t state);

static bt_callbacks_t btCallbacks = {
    sizeof(bt_callbacks_t),
    btAdapterStateChangedCallback, /*adapter_state_changed */
    NULL, /*adapter_properties_cb */
    NULL, /* remote_device_properties_cb */
    NULL, /* device_found_cb */
    NULL, /* discovery_state_changed_cb */
    NULL, /* pin_request_cb  */
    NULL, /* ssp_request_cb  */
    NULL, /*bond_state_changed_cb */
    NULL, /* acl_state_changed_cb */
    NULL, /* thread_evt_cb */
    NULL, /*dut_mode_recv_cb */
//    NULL, /*authorize_request_cb */
#if BLE_INCLUDED == TRUE
    NULL, /* le_test_mode_cb */
#else
    NULL
#endif
};

static btfm_callbacks_t fmCallback = {
    sizeof (btfm_callbacks_t),
    btfmEnableCallback,             // btfm_enable_callback
    btfmDisableCallback,            // btfm_disable_callback
    btfmtuneCallback,               // btfm_tune_callback
    btfmMuteCallback,               // btfm_mute_callback
    btfmSearchCallback,             // btfm_search_callback
    btfmSearchCompleteCallback,     // btfm_search_complete_callback
    NULL,                           // btfm_af_jump_callback
    btfmAudioModeCallback,          // btfm_audio_mode_callback
    btfmAudioPathCallback,          // btfm_audio_path_callback
    NULL,                           // btfm_audio_data_callback
    NULL,                           // btfm_rds_mode_callback
    NULL,                           // btfm_rds_type_callback
    NULL,                           // btfm_deemphasis_callback
    NULL,                           // btfm_scan_step_callback
    NULL,                           // btfm_region_callback
    NULL,                           // btfm_nfl_callback
    btfmVolumeCallback,             //btfm_volume_callback
    NULL,                           // btfm_rds_data_callback
    NULL,                           // btfm_rtp_data_callback
};

void btAdapterStateChangedCallback(bt_state_t state)
{
   if(state == BT_RADIO_OFF || state == BT_RADIO_ON) {
        ALOGI("FM Adapter State Changed: %d",state);
        if (state != BT_RADIO_ON) return;
            sFmInterface = (btfm_interface_t*)sBtInterface->get_fm_interface();
            int retVal = sFmInterface->init(&fmCallback);
            retVal = sFmInterface->enable(96);
    } else {
         ALOGI("err State Changed: %d",state);
    }
}


int fm_set_status(char * pdata)
{ 
   if (*pdata==1)
   	   {
   	   		ALOGE("eut: open fm");
			fmOpen();
			fmPlay(mFmTune);
			*pdata=0;
   	    }
   	else if(*pdata==0)
   		{
   			ALOGE("eut: close fm");
			fmStop();
			fmClose();
			
   		}
	return 0;
}

int CallbackStatus(int status,char *p)
{
         if(status == FM_STATE_ENABLED)
        {
        	ALOGE("%s return FM_STATE_ENABLED",p);
        	return FM_SUCCESS;
        }
		else if(FM_STATE_ERR == status)
		{
			ALOGE("%s return FM_STATE_ERR",p);
			return FM_FAILURE;
		}
		else 
		{
		   ALOGE("%s timeout ....",p);
			return FM_FAILURE;
		}
		return FM_FAILURE;
}
int fm_set_volume(unsigned char pdata)
{
    int status;
	int counter = 0;
	ALOGE("sFmStatus, status: %d", sFmStatus);
 	if((NULL != sFmInterface) && ((sFmStatus == FM_STATE_ENABLED) ||(sFmStatus == FM_STATE_PLAYING)) )
 	{
 		sFmVolumeStatus = FM_STATE_PANIC;
 		if ((status = sFmInterface->set_volume((int)pdata)) != BT_STATUS_SUCCESS) {
            ALOGE("Failed FM setFMVolumeNative, status: %d", status);
			return FM_FAILURE;
        }
		while (counter++ < 3 && FM_STATE_ENABLED != sFmVolumeStatus && FM_STATE_ERR != sFmVolumeStatus) sleep(1);

		return CallbackStatus(sFmVolumeStatus,"sFmVolumeStatus");
    }
	else 
	{
		return FM_FAILURE;
    }
	return FM_FAILURE;
}
int fm_set_mute()
{
	 int status;
	 int counter = 0;
	 ALOGE("sFmStatus, status: %d", sFmStatus);
 	if((NULL != sFmInterface) && ((sFmStatus == FM_STATE_ENABLED) ||(sFmStatus == FM_STATE_PLAYING)) )
 	{

		sFmMuteStatus = FM_STATE_PANIC;
 		if ((status = sFmInterface->mute(1)) != BT_STATUS_SUCCESS) {
            ALOGE("Failed FM setFMmuteNative, status: %d", status);
			return FM_FAILURE;
        }
		while (counter++ < 3 && FM_STATE_ENABLED != sFmMuteStatus && FM_STATE_ERR != sFmMuteStatus) sleep(1);

		return CallbackStatus(sFmMuteStatus,"sFmMuteStatus");
    }
	else 
	{
		return FM_FAILURE;
    }
	return FM_FAILURE;
}

int fm_set_seek(unsigned int startFreq,char mode)
{
	 int status;
	 int counter = 0;
	 int diffFreq= 0;
	 int direction;
	 memset(&mFmSignalParm,0,sizeof(FM_SIGNAL_PARAM_T));
	 mFmSignalParm.nOperInd = FM_FAILURE;
	 ALOGE("sFmStatus, status: %d", sFmStatus);
 	if((NULL != sFmInterface) && ((sFmStatus == FM_STATE_ENABLED) ||(sFmStatus == FM_STATE_PLAYING)) )
 	{	
 	    if(mode == 0)
 	    {
 	    	diffFreq = 0 -10;
			direction = SEARCH_DIRECTION_UP;
 	    }
		else if(mode == 1)
		{
			diffFreq =  10;
			direction = SEARCH_DIRECTION_DOWN;
		}
 		sFmSearchStatus = FM_STATE_PANIC;
		if ((status = sFmInterface->combo_search(startFreq,startFreq+diffFreq,105,direction,0,0,0,0)) != BT_STATUS_SUCCESS) {
				ALOGE("Failed FM Tune, status: %d", status);
				return FM_FAILURE;
		}
 		while (counter++ < 10 && FM_STATE_ENABLED != sFmSearchStatus && FM_STATE_ERR != sFmSearchStatus) 
 		{
 			if(sFmSearchStatus == FM_STATE_STOPED)
 			{
 			   
 			   if ((status = sFmInterface->combo_search(mFmSignalParm.nFreqValue,mFmSignalParm.nFreqValue+diffFreq,105,direction,0,0,0,0)) != BT_STATUS_SUCCESS) {
					ALOGE("Failed FM Tune, status: %d", status);
					return FM_FAILURE;
		        }
 			}
			sleep(1);
 		}

		return CallbackStatus(sFmSearchStatus,"sFmSearchStatus");

    }
	else 
	{
		return FM_FAILURE;
    }
	return FM_FAILURE;

}

int fm_get_tune(int frq)
{
	int status;
	int counter = 0;
	memset(&mFmSignalParm,0,sizeof(FM_SIGNAL_PARAM_T));
	mFmSignalParm.nOperInd = FM_FAILURE;
	if((NULL != sFmInterface) && ((sFmStatus == FM_STATE_ENABLED) ||(sFmStatus == FM_STATE_PLAYING)) )
 	{	
 		sFmTuneStatus = FM_STATE_PANIC;
		if ((status = sFmInterface->tune(frq)) != BT_STATUS_SUCCESS) {
				ALOGE("Failed FM Tune, status: %d", status);
				return FM_FAILURE;
			}
 		while (counter++ < 10 && FM_STATE_ENABLED != sFmTuneStatus && FM_STATE_ERR != sFmTuneStatus) sleep(1);

		return CallbackStatus(sFmTuneStatus,"sFmTuneStatus");

    }
	else 
	{
		return FM_FAILURE;
    }
	return FM_FAILURE;
}

unsigned int fm_read_reg(unsigned int addr)
{
	 int status;
	 int counter = 0;
	 ALOGE("sFmStatus, status: %d", sFmStatus);
 	if((NULL != sFmInterface) && ((sFmStatus == FM_STATE_ENABLED) ||(sFmStatus == FM_STATE_PLAYING)) )
 	{

 		if ((status = sFmInterface->read_reg(addr)) != BT_STATUS_SUCCESS) {
            ALOGE("Failed FM fm_read_reg, status: %d", status);
			return FM_FAILURE;
        }
         //needd return in callback.

		 
		return FM_SUCCESS;
    }
	else 
	{
		return FM_FAILURE;
    }

}
int fm_write_reg(unsigned int addr,int value)
{
	 int status;
	 int counter = 0;
	 ALOGE("sFmStatus, status: %d", sFmStatus);
 	if((NULL != sFmInterface) && ((sFmStatus == FM_STATE_ENABLED) ||(sFmStatus == FM_STATE_PLAYING)) )
 	{

 		if ((status = sFmInterface->write_reg(addr,value)) != BT_STATUS_SUCCESS) {
            ALOGE("Failed FM fm_write_reg, status: %d", status);
			return FM_FAILURE;
        }
         //needd return in callback.

		 
		return FM_SUCCESS;
    }
	else 
	{
		return FM_FAILURE;
    }
	return FM_FAILURE;

}

//for Pandora tool  ( Mobiletest)
int start_fm_test(unsigned char * buf,int len)
{
    int fd;
    int i;
	int ret = 0;
    char *pdata=NULL;
    MSG_HEAD_T *head_ptr=NULL;
    unsigned char temp[11];
    head_ptr = (MSG_HEAD_T *)(buf+1); //Diag_head
    pdata = buf + DIAG_HEADER_LENGTH + 1; //data  
    ALOGE("start FM test Subtype=%d",head_ptr->subtype);
	switch(head_ptr->subtype)
		 {
		 case FM_CMD_STATE: 
			 ret =fm_set_status(pdata); 
			 break;
		 case FM_CMD_VOLUME:
			 ret = fm_set_volume(pdata);
			 break;
		 case FM_CMD_MUTE:
			 ret = fm_set_mute();
			 break;
		 case FM_CMD_TUNE:
			 ret = fm_get_tune(93000);
			 break;
		 case FM_CMD_SEEK:
			 ret = fm_set_seek(93000,0);
			 break;
		 case FM_CMD_READ_REG:
			 //fm_read_reg(pdata);
			 break;
		 case FM_CMD_WRITE_REG:
			 //fm_write_reg(pdata);
			 break;
		 default:
			 break;
		 }
   return ret;
}


static  int btHalLoad(void)
{
    int err = 0;

    hw_module_t* module;
    hw_device_t* device;

    ALOGI("Loading HAL lib + extensions");

    err = hw_get_module(BT_HARDWARE_MODULE_ID, (hw_module_t const**)&s_hwModule);
    if (err == 0)
    {
        err = s_hwModule->methods->open(module, BT_HARDWARE_MODULE_ID, (hw_device_t**)&s_hwDev);
        if (err == 0) {
            sBtDevice = (bluetooth_device_t *)s_hwDev;
            sBtInterface = sBtDevice->get_bluetooth_interface();
        }
    }

    ALOGI("HAL library loaded (%s)", strerror(err));

    return err;
}

static int btInit(void)
{
    ALOGI("INIT BT ");
    int retVal = (bt_status_t)sBtInterface->init(&btCallbacks);
    ALOGI("BT init: %d", retVal);
    if((BT_STATUS_SUCCESS == retVal)||(BT_STATUS_DONE == retVal)){
        return (0);
    }else{
        return (-1);
    }
}


int fmOpen( void )
{

    ALOGI("Try to open fm \n");
    if (FM_STATE_ENABLED == sFmStatus || FM_STATE_PLAYING == sFmStatus) return 0; // fm has been opened
    if ( btHalLoad() < 0 ) {
        return -1;
    } else {
        if (NULL == sBtInterface || NULL == sBtDevice) {
            return -1;
        }
    }

    if ( btInit() < 0 ) {
        return -1;
    }
    sBtInterface->enableRadio();
    ALOGI("Enable radio okay, try to get fm interface \n");

    setDeviceConnectionState(AUDIO_DEVICE_OUT_FM_SPEAKER,
            AUDIO_POLICY_DEVICE_STATE_UNAVAILABLE);
    setDeviceConnectionState(AUDIO_DEVICE_OUT_FM_HEADSET,
            AUDIO_POLICY_DEVICE_STATE_UNAVAILABLE);

    ALOGI("Fm open okay \n");
    return 0;
}

int fmPlay( uint freq )
{
    if( NULL == s_hwDev ) {
        ALOGI("not opened!\n");
        return 1;
    }

    int  status;

    int counter = 0;
    while (sFmStatus != FM_STATE_ENABLED && counter++ < 3) sleep(1);

    if (sFmStatus != FM_STATE_ENABLED) {
         ALOGI("fm service has not enabled, status: %d", sFmStatus);
         return 1;
    }
    sFmInterface->tune(freq);
	sleep(1);
    sFmInterface->set_audio_path(0x02);
    sleep(1);
    sFmInterface->set_volume(32);



    status = setDeviceConnectionState(AUDIO_DEVICE_OUT_FM_HEADSET,
             AUDIO_POLICY_DEVICE_STATE_AVAILABLE);
    setForceUse(AUDIO_POLICY_FORCE_FOR_MEDIA, AUDIO_POLICY_FORCE_NONE);

    sFmStatus = FM_STATE_PLAYING;


    ALOGI("Fm play okay \n");
    return 0;
}

//------------------------------------------------------------------------------
int fmStop( void )
{
    if( NULL != s_hwDev ) {
        setDeviceConnectionState(AUDIO_DEVICE_OUT_FM_HEADSET,
                AUDIO_POLICY_DEVICE_STATE_UNAVAILABLE);
        setForceUse(AUDIO_POLICY_FORCE_FOR_MEDIA, AUDIO_POLICY_FORCE_NONE);
        sFmStatus = FM_STATE_STOPED;
    }

    ALOGI("Stop okay \n");
    return 0;
}

int fmClose( void )
{
    int counter = 0;

    if (sFmInterface)
        sFmInterface->disable();
    else
        return 1;

    while (counter++ < 3 && FM_STATE_DISABLED != sFmStatus) sleep(1);
    if (FM_STATE_DISABLED != sFmStatus) return 1;

    if (sFmInterface) sFmInterface->cleanup();
    if (sBtInterface) {
         sBtInterface->disableRadio();
    }

    sFmStatus = FM_STATE_DISABLED;

    ALOGI("Close successful.");

    return 0;
}


