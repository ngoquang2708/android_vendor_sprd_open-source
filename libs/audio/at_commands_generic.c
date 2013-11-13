#include "AtChannel.h"

#define VOICECALL_VOLUME_MAX_UI	6

static pthread_mutex_t  ATlock = PTHREAD_MUTEX_INITIALIZER;         //eng cannot handle many at commands once
static unsigned int cur_call_sim = 0;
static unsigned int cur_cp_type = 0;

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


int at_cmd_init(void)
{
    if ( cur_call_sim != android_sim_num
        ||cur_cp_type!= st_vbc_ctrl_thread_para->adev->cp_type) {
        cur_call_sim = android_sim_num;
	 cur_cp_type =  st_vbc_ctrl_thread_para->adev->cp_type;
    }

    return 0;
}

static int at_cmd_route(struct tiny_audio_device *adev)
{
    const char *at_cmd = NULL;
    if (adev->mode != AUDIO_MODE_IN_CALL) {
        ALOGE("Error: NOT mode_in_call, current mode(%d)", adev->mode);
        return -1;
    }

    if (adev->out_devices & (AUDIO_DEVICE_OUT_WIRED_HEADSET | AUDIO_DEVICE_OUT_WIRED_HEADPHONE)) {
        at_cmd = "AT+SSAM=2";
    } else if (adev->out_devices & (AUDIO_DEVICE_OUT_BLUETOOTH_SCO
                                | AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET
                                | AUDIO_DEVICE_OUT_BLUETOOTH_SCO_CARKIT)) {
        at_cmd = "AT+SSAM=5";
    } else if (adev->out_devices & AUDIO_DEVICE_OUT_SPEAKER) {
        at_cmd = "AT+SSAM=1";
    } else {
        at_cmd = "AT+SSAM=0";
    }
    do_cmd_dual(adev->cp_type, cur_call_sim, at_cmd);

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
    do_cmd_dual(cur_cp_type, cur_call_sim, at_cmd);
    return 0;
}

int at_cmd_mic_mute(bool mute)
{
    const char *at_cmd;
    ALOGW("audio at_cmd_mic_mute %d", mute);
    if (mute) at_cmd = "AT+CMUT=1";
    else at_cmd = "AT+CMUT=0";
    do_cmd_dual(cur_cp_type, cur_call_sim, at_cmd);
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
    do_cmd_dual(cur_cp_type, cur_call_sim, at_cmd);
    return 0;
}
