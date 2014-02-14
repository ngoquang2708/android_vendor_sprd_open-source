#include "AtChannel.h"

#define VOICECALL_VOLUME_MAX_UI	6

static pthread_mutex_t  ATlock = PTHREAD_MUTEX_INITIALIZER;         //eng cannot handle many at commands once

int do_cmd_dual(int modemId, int simId, const  char* at_cmd)
{
    const char *err_str = NULL;

    pthread_mutex_lock(&ATlock);
    if (at_cmd) {
        err_str = sendAt(modemId, simId, at_cmd);
        ALOGD("do_cmd_dual Switch incall AT command [%s][%s] ", at_cmd, err_str);
    }
    pthread_mutex_unlock(&ATlock);

    return 0;
}

// 0x80 stands for 8KHz(NB) sampling rate BT Headset.
// 0x40 stands for 16KHz(WB) sampling rate BT Headset.
static int config_bt_dev_type(int bt_headset_type, cp_type_t cp_type, int cp_sim_id)
{
    const char *at_cmd = NULL;

    if (bt_headset_type == VX_NB_SAMPLING_RATE) {
        at_cmd = "AT+SSAM=128";
    } else if (bt_headset_type == VX_WB_SAMPLING_RATE) {
        at_cmd = "AT+SSAM=64";
    }
    do_cmd_dual(cp_type, cp_sim_id, at_cmd);
    usleep(10000);

    return 0;
}

static int at_cmd_route(struct tiny_audio_device *adev)
{
    const char *at_cmd = NULL;
    if ((adev->mode != AUDIO_MODE_IN_CALL) && (!adev->voip_start)) {
        ALOGE("Error: NOT mode_in_call, current mode(%d)", adev->mode);
        return -1;
    }

    if (adev->out_devices & (AUDIO_DEVICE_OUT_WIRED_HEADSET | AUDIO_DEVICE_OUT_WIRED_HEADPHONE)) {
        at_cmd = "AT+SSAM=2";
    } else if (adev->out_devices & (AUDIO_DEVICE_OUT_BLUETOOTH_SCO
                                | AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET
                                | AUDIO_DEVICE_OUT_BLUETOOTH_SCO_CARKIT)) {
        if (adev->bluetooth_type)
            config_bt_dev_type(adev->bluetooth_type,
                    st_vbc_ctrl_thread_para->adev->cp_type, android_sim_num);

        if (adev->bluetooth_nrec) {
            at_cmd = "AT+SSAM=6";
        } else {
            at_cmd = "AT+SSAM=5";
        }
    } else if (adev->out_devices & AUDIO_DEVICE_OUT_SPEAKER) {
        at_cmd = "AT+SSAM=1";
    } else {
        at_cmd = "AT+SSAM=0";
    }

    do_cmd_dual(st_vbc_ctrl_thread_para->adev->cp_type, android_sim_num, at_cmd);

    return 0;
}

int at_cmd_volume(float vol, int mode)
{
    char buf[16];
    char *at_cmd = buf;
    int volume = vol * VOICECALL_VOLUME_MAX_UI + 1;

    if (volume >= VOICECALL_VOLUME_MAX_UI) volume = VOICECALL_VOLUME_MAX_UI;
    ALOGI("%s mode=%d ,volume=%d, android vol:%f ", __func__,mode,volume,vol);
    snprintf(at_cmd, sizeof buf, "AT+VGR=%d", volume);

    do_cmd_dual(st_vbc_ctrl_thread_para->adev->cp_type, android_sim_num, at_cmd);
    return 0;
}

int at_cmd_mic_mute(bool mute)
{
    const char *at_cmd;
    ALOGW("audio at_cmd_mic_mute %d", mute);
    if (mute) at_cmd = "AT+CMUT=1";
    else at_cmd = "AT+CMUT=0";

    do_cmd_dual(st_vbc_ctrl_thread_para->adev->cp_type, android_sim_num, at_cmd);
    return 0;
}

int at_cmd_downlink_mute(bool mute)
{
    char r_buf[32];
    const char *at_cmd;
    ALOGW("audio at_cmd_downlink_mute set %d", mute);
    if (mute){
        at_cmd = "AT+SDMUT=1";
    }
    else{
        at_cmd = "AT+SDMUT=0";
    }
    do_cmd_dual(st_vbc_ctrl_thread_para->adev->cp_type, android_sim_num, at_cmd);
    return 0;
}

int at_cmd_audio_loop(int enable, int mode, int volume,int loopbacktype,int voiceformat,int delaytime)
{
    char buf[89];
    char *at_cmd = buf;
    if(volume >9) {
        volume = 9;
    }
    ALOGW("audio at_cmd_audio_loop enable:%d,mode:%d,voluem:%d,loopbacktype:%d,voiceformat:%d,delaytime:%d",
            enable,mode,volume,loopbacktype,voiceformat,delaytime);

    snprintf(at_cmd, sizeof buf, "AT+SPVLOOP=%d,%d,%d,%d,%d,%d", enable,mode,volume,loopbacktype,voiceformat,delaytime);

    do_cmd_dual(st_vbc_ctrl_thread_para->adev->cp_type, android_sim_num, at_cmd);
    return 0;
}

int at_cmd_cp_usecase_type(audio_cp_usecase_t type)
{
    char buf[89];
    char *at_cmd = buf;
    if(type > AUDIO_CP_USECASE_MAX) {
        type = AUDIO_CP_USECASE_VOIP_1;
    }
    ALOGW("%s, type:%d ",__func__,type);

    snprintf(at_cmd, sizeof buf, "AT+SPAPAUDMODE=%d", type);

    do_cmd_dual(st_vbc_ctrl_thread_para->adev->cp_type, android_sim_num, at_cmd);
    return 0;
}
