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
};

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

static hw_module_t * s_hwModule = NULL;
static hw_device_t * s_hwDev    = NULL;

static bluetooth_device_t* sBtDevice = NULL;
static const bt_interface_t* sBtInterface = NULL;
static const btfm_interface_t* sFmInterface = NULL;
static int sFmStatus = FM_STATE_PANIC;

void btfmEnableCallback (int status)  {  if (status == BT_STATUS_SUCCESS) sFmStatus = FM_STATE_ENABLED; ALOGI("Enable callback, status: %d", sFmStatus); }
void btfmDisableCallback (int status) { if (status == BT_STATUS_SUCCESS) sFmStatus = FM_STATE_DISABLED;   ALOGI("Disable callback, status: %d", sFmStatus); }
void btfmtuneCallback (int status, int rssi, int snr, int freq) { ALOGI("Tune callback, status: %d, freq: %d", status, freq); }
void btfmMuteCallback (int status, BOOLEAN isMute){ ALOGI("Mute callback, status: %d, isMute: %d", status, isMute); }
void btfmSearchCallback (int status, int rssi, int snr, int freq){ ALOGI("Search callback, status: %d", status); }
void btfmSearchCompleteCallback(int status, int rssi, int snr, int freq){ ALOGI("Search complete callback"); }
void btfmAudioModeCallback(int status, int audioMode){ ALOGI("Audio mode change callback, status: %d, audioMode: %d", status, audioMode); }
void btfmAudioPathCallback(int status, int audioPath){ ALOGI("Audio path change callback, status: %d, audioPath: %d", status, audioPath); }
void btfmVolumeCallback(int status, int volume){ ALOGI("Volume change callback, status: %d, volume: %d", status, volume); }
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
			fmPlay(1077);
			*pdata=0;
   	    }
   	else if(*pdata==0)
   		{
   			ALOGE("eut: close fm");
			fmStopEx();
			fmCloseEx();
			
   		}
	return 0;
}

//for Pandora tool  ( Mobiletest)
int start_fm_test(unsigned char * buf,int len)
{
    int fd;
    int i;
    char *pdata=NULL;
    MSG_HEAD_T *head_ptr=NULL;
    unsigned char temp[11];
    head_ptr = (MSG_HEAD_T *)(buf+1); //Diag_head
    pdata = buf + DIAG_HEADER_LENGTH + 1; //data  
    ALOGE("start FM test Subtype=%d",head_ptr->subtype);
	switch(head_ptr->subtype)
		 {
		 case FM_CMD_STATE: 
			 fm_set_status(pdata); 
			 break;
		 case FM_CMD_VOLUME:
			 //fm_set_volum(pdata);
			 break;
		 case FM_CMD_MUTE:
			 //fm_set_mute(pdata);
			 break;
		 case FM_CMD_TUNE:
			 //fm_get_tune(pdata);
			 break;
		 case FM_CMD_SEEK:
			 //fm_set_seek(pdata);
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
   return 0;
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
        return -1;
    }

    int  status;

    int counter = 0;
    while (sFmStatus != FM_STATE_ENABLED && counter++ < 3) sleep(1);

    if (sFmStatus != FM_STATE_ENABLED) {
         ALOGI("fm service has not enabled, status: %d", sFmStatus);
         return -1;
    }
    sFmInterface->tune(freq * 10);

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
int fmStopEx( void )
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

int fmCloseEx( void )
{
    int counter = 0;

    if (sFmInterface)
        sFmInterface->disable();
    else
        return -1;

    while (counter++ < 3 && FM_STATE_DISABLED != sFmStatus) sleep(1);
    if (FM_STATE_DISABLED != sFmStatus) return -1;

    if (sFmInterface) sFmInterface->cleanup();
    if (sBtInterface) {
         sBtInterface->disableRadio();
		 sleep(2);
		 sBtInterface->cleanup();
    }

    sFmStatus = FM_STATE_DISABLED;

    ALOGI("Close successful.");

    return 0;
}


