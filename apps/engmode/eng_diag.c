#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "engopt.h"
#include "eng_attok.h"
#include "eng_pcclient.h"
#include "eng_diag.h"
#include "eng_sqlite.h"
#include "vlog.h"
#include "crc16.h"
#include "string.h"
#include "eng_audio.h"
#include "eut_opt.h"
#include <ctype.h>
#include "cutils/properties.h"
#include <sys/reboot.h>
#include "eng_btwifiaddr.h"
#include "vlog.h"
#include "eng_productdata.h"
#include "eng_sqlite.h"
#ifdef ENG_AT_CHANNEL
#include "AtChannel.h"
#endif
#include "string_exchange_bin.h"

#define NUM_ELEMS(x) (sizeof(x)/sizeof(x[0]))
#define NVITEM_ERROR_E  int
#define NVERR_NONE 0



// SIPC interfaces in AP linux for AT CMD
char *at_sipc_devname[] = {
    "/dev/stty_td30", // AT channel in TD mode
    "/dev/stty_w30" // AT channel in W mode
};

int g_reset = 0;
extern int g_run_mode;
extern AUDIO_TOTAL_T *audio_total;
extern void eng_check_factorymode(void);
extern int parse_vb_effect_params(void *audio_params_ptr, unsigned int params_size);
extern int SetAudio_pga_parameter_eng(AUDIO_TOTAL_T *aud_params_ptr, unsigned int params_size, uint32_t vol_level);
extern int eng_battery_calibration(char *data,unsigned int count,char *out_msg,int out_len);
extern void adc_get_result(char* chan);
extern void disable_calibration(void);
extern void enable_calibration(void);
extern void stringfile2nvstruct(char *filename, void *para_ptr, int lenbytes);
extern void  nvstruct2stringfile(char* filename,void *para_ptr, int lenbytes);

extern  struct eng_bt_eutops bt_eutops;
extern  struct eng_wifi_eutops wifi_eutops;
extern	struct eng_gps_eutops gps_eutops;

static unsigned char tun_data[376];
static int diag_pipe_fd = 0;
static char eng_atdiag_buf[2*ENG_DIAG_SIZE];
static char eng_diag_buf[2*ENG_DIAG_SIZE];
static char eng_audio_diag_buf[2*ENG_DIAG_SIZE];
static int eng_diag_len = 0;
static int g_is_data = 0;
static int g_indicator = 0;
static int g_index = 0;
static int cmd_type;
static int eq_or_tun_type,eq_mode_sel_type;
static int s_cmd_index = -1;
static int s_cp_ap_proc = 0;

static int write_productnvdata(char* buffer , int size);
static int read_productnvdata(char* buffer , int size);
static int eng_diag_getver(unsigned char *buf,int len, char *rsp);
static int eng_diag_bootreset(unsigned char *buf,int len, char *rsp);
static int eng_diag_getband(char *buf,int len, char *rsp);
static int eng_diag_btwifi(char *buf,int len, char *rsp, int *extra_len);
static int eng_diag_audio(char *buf,int len, char *rsp);
static int eng_diag_product_ctrl(char *buf,int len, char *rsp, int rsplen);
static int eng_diag_direct_phschk(char *buf,int len, char *rsp, int rsplen);
static void eng_diag_reboot(int reset);
static int eng_diag_deep_sleep(char *buf,int len, char *rsp);
int is_audio_at_cmd_need_to_handle(char *buf,int len);
int is_btwifi_addr_need_to_handle(char *buf,int len);
int eng_diag_factorymode(char *buf,int len, char *rsp);
int eng_diag_mmicit_read(char *buf,int len, char *rsp, int rsplen);
int get_sub_str(char *buf,char **revdata, char a, char b);
int get_cmd_index(char *buf);
int eng_diag_decode7d7e(char *buf,int len);
int eng_diag_adc(char *buf, int * Irsp); //add by kenyliu on 2013 07 12 for get ADCV  bug 188809
void At_cmd_back_sig(void);//add by kenyliu on 2013 07 15 for set calibration enable or disable  bug 189696

static const char *at_sadm="AT+SADM4AP";
static const char *at_spenha="AT+SPENHA";
static const char *at_calibr="AT+CALIBR";

//static int at_sadm_cmd_to_handle[] = {7,8,9,10,11,12,-1};
static int at_sadm_cmd_to_handle[] = {7,8,9,10,11,12,-1};
//static int at_spenha_cmd_to_handle[] = {0,1,2,3,4,-1};
static int at_spenha_cmd_to_handle[] = {0,1,2,3,4,-1};

#define AUDFIFO "/data/local/media/audiopara_tuning"

struct eut_cmd eut_cmds[]={
    {EUT_REQ_INDEX,ENG_EUT_REQ},
    {EUT_INDEX,ENG_EUT},
    {GPSSEARCH_REQ_INDEX,ENG_GPSSEARCH_REQ},
    {GPSSEARCH_INDEX,ENG_GPSSEARCH},
    {WIFICH_REQ_INDEX,ENG_WIFICH_REQ},
    {WIFICH_INDEX,ENG_WIFICH},
    {WIFIMODE_INDEX,ENG_WIFIMODE},
    {WIFIRATIO_REQ_INDEX,ENG_WIFIRATIO_REQ},
    {WIFIRATIO_INDEX,ENG_WIFIRATIO},
    {WIFITX_FACTOR_REQ_INDEX,ENG_WIFITX_FACTOR_REQ},
    {WIFITX_FACTOR_INDEX,ENG_WIFITX_FACTOR},
	{ENG_WIFITXGAININDEX_REQ_INDEX, ENG_WIFITXGAININDEX_REQ},
	{ENG_WIFITXGAININDEX_INDEX, ENG_WIFITXGAININDEX},
    {WIFITX_REQ_INDEX,ENG_WIFITX_REQ},
    {WIFITX_INDEX,ENG_WIFITX},
    {WIFIRX_PACKCOUNT_INDEX,ENG_WIFIRX_PACKCOUNT},
    {WIFICLRRXPACKCOUNT_INDEX,ENG_WIFI_CLRRXPACKCOUNT},
    {WIFIRX_REQ_INDEX,ENG_WIFIRX_REQ},
    {WIFIRX_INDEX,ENG_WIFIRX},
    {GPSPRNSTATE_REQ_INDEX,ENG_GPSPRNSTATE_REQ},
    {GPSSNR_REQ_INDEX,ENG_GPSSNR_REQ},
    {GPSPRN_INDEX,ENG_GPSPRN},
	{ENG_WIFIRATE_REQ_INDEX, ENG_WIFIRATE_REQ},
    {ENG_WIFIRATE_INDEX,ENG_WIFIRATE},
    {ENG_WIFIRSSI_REQ_INDEX, ENG_WIFIRSSI_REQ},  
};

static int eng_diag_write2pc(void)
{
    int ret;
    int i=0;
    int fd=0;
    int error = 0;

    do{
           fd = get_ser_fd();
           error = 0;
           ret = write(fd,eng_diag_buf,eng_diag_len);
           if(ret < 0){
               error = errno;
               ENG_LOG("%s error: %s \n",__func__,strerror(errno));
               if(error == EBUSY)
               usleep(200000);
           }
           i++;
    }while(( error == EBUSY)&&(i<25));
    return ret;
}

static void print_log(char* log_data,int cnt)
{
    int i;

    if (cnt > ENG_DIAG_SIZE)
        cnt = ENG_DIAG_SIZE;

    ENG_LOG("vser receive:\n");
    for(i = 0; i < cnt; i++) {
        if (isalnum(log_data[i])){
            ENG_LOG("%c ", log_data[i]);
        }else{
            ENG_LOG("%2x ", log_data[i]);
        }
    }
    ENG_LOG("\n");
}

char* eng_diag_at_channel()
{
    return at_sipc_devname[g_run_mode];
}

int eng_diag_parse(char *buf,int len)
{
    int i;
    int ret = CMD_COMMON;
    MSG_HEAD_T *head_ptr=NULL;
    eng_diag_decode7d7e((char *)(buf + 1), (len - 1));
    head_ptr =(MSG_HEAD_T *)(buf+1);

    ENG_LOG("%s: cmd=0x%x; subcmd=0x%x\n",__FUNCTION__, head_ptr->type, head_ptr->subtype);
    //ENG_LOG("%s: cmd is:%s \n", __FUNCTION__, (buf + DIAG_HEADER_LENGTH + 1));

    switch(head_ptr->type) {
        case DIAG_CMD_CHANGEMODE:
            ENG_LOG("%s: Handle DIAG_CMD_CHANGEMODE\n",__FUNCTION__);
            if(head_ptr->subtype==RESET_MODE) {
                ret = CMD_USER_RESET;
            } else {
                ret=CMD_COMMON;
            }
            break;
        case DIAG_CMD_FACTORYMODE:
            ENG_LOG("%s: Handle DIAG_CMD_FACTORYMODE\n",__FUNCTION__);
            if(head_ptr->subtype==0x07) {
                ret = CMD_USER_FACTORYMODE;
            } else if(head_ptr->subtype >= 0x2 && head_ptr->subtype <= 0x4) {
                // 2: NVITEM_PRODUCT_CTRL_READ
                // 3: NVITEM_PRODUCT_CTRL_WRITE
                // 4: NVITEM_PRODUCT_CTRL_ERASE
                ret=CMD_USER_PRODUCT_CTRL;
            } else if(head_ptr->subtype==DIAG_SUB_MMICIT_READ){
                ret=CMD_USER_MMICIT_READ;
            } else {
                ret=CMD_COMMON;
            }
            break;
        case DIAG_CMD_DIRECT_PHSCHK:
            ENG_LOG("%s: Handle DIAG_CMD_DIRECT_PHSCHK\n",__FUNCTION__);
            ret =CMD_USER_DIRECT_PHSCHK;
            break;
        case DIAG_CMD_ADC_F:
            ret =CMD_USER_ADC;
            break;
        case DIAG_CMD_AT:
            if(is_audio_at_cmd_need_to_handle(buf,len)){
                ENG_LOG("%s: Handle DIAG_CMD_AUDIO\n",__FUNCTION__);
                ret = CMD_USER_AUDIO;
            } else if(is_ap_at_cmd_need_to_handle(buf,len)){
                ret = CMD_USER_APCMD;
            } else{
                ret = CMD_COMMON;
            }
            break;
        case DIAG_CMD_GETVOLTAGE:
            ret = CMD_USER_GETVOLTAGE;
            break;
        case DIAG_CMD_APCALI:
            ret = CMD_USER_APCALI;
            break;
        case DIAG_CMD_VER:
            ENG_LOG("%s: Handle DIAG_CMD_VER",__FUNCTION__);
            if(head_ptr->subtype==0x2) {
                ret = CMD_USER_VER;
            }
            break;
        case DIAG_CMD_IMEIBTWIFI:
            ret = is_btwifi_addr_need_to_handle(buf,len);
            if(ret){
                if(2 == ret){
                    s_cp_ap_proc = 1; // This command should send to AP and CP.
                }
                ret = CMD_USER_BTWIFI;
            }else{
                ret = CMD_COMMON;
            }
            break;
        case DIAG_CMD_CURRENT_TEST:
            ENG_LOG("%s: Handle DIAG_CMD_CURRENT_TEST", __FUNCTION__);
            if(head_ptr->subtype==0x2) {
                ret = CMD_USER_DEEP_SLEEP;
            }
            break;
        default:
            ENG_LOG("%s: Default\n",__FUNCTION__);
            ret = CMD_COMMON;
            break;
    }
    return ret;
}

int eng_diag_user_handle(int type, char *buf,int len)
{
    int rlen = 0,i;
    int extra_len=0;
    int ret;
    MSG_HEAD_T head,*head_ptr=NULL;
    char rsp[512];
    int adc_rsp[8];

    memset(rsp, 0, sizeof(rsp));
    memset(adc_rsp, 0, sizeof(adc_rsp));

    ENG_LOG("%s: type=%d\n",__FUNCTION__, type);

    switch(type){
        case CMD_USER_VER:
            rlen=eng_diag_getver((unsigned char*)buf,len, rsp);
            break;
        case CMD_USER_RESET:
            rlen=eng_diag_bootreset((unsigned char*)buf,len, rsp);
            g_reset = 1;
            break;
        case CMD_USER_BTWIFI:
            rlen=eng_diag_btwifi(buf, len, rsp, &extra_len);
            if(!rlen)
                return 0;
            break;
        case CMD_USER_FACTORYMODE:
            rlen=eng_diag_factorymode(buf, len, rsp);
            break;
        case CMD_USER_ADC:
            rlen=eng_diag_adc(buf, adc_rsp);
            break;
        case CMD_USER_AUDIO:
            memset(eng_audio_diag_buf,0,sizeof(eng_audio_diag_buf));
            rlen=eng_diag_audio(buf, len, eng_audio_diag_buf);
            break;
        case CMD_USER_GETVOLTAGE:
        case CMD_USER_APCALI:
            rlen = eng_battery_calibration(buf,len,eng_diag_buf,sizeof(eng_diag_buf));
            eng_diag_len = rlen;
            eng_diag_write2pc();
            return 0;
            break;
        case CMD_USER_APCMD:
            // For compatible with pc tool: MOBILETEST,
            // send a empty diag framer first.
            {
                char emptyDiag[] = {0x7e,0x00,0x00,0x00,0x00,0x08,0x00,0xd5,0x00,0x7e};
                write(get_ser_fd(), emptyDiag, sizeof(emptyDiag));
            }
            rlen = eng_diag_apcmd_hdlr(buf, len, rsp);
            break;
        case CMD_USER_PRODUCT_CTRL:
            ENG_LOG("%s: CMD_USER_PRODUCT_CTRL\n",__FUNCTION__);
            memset(eng_diag_buf, 0, sizeof(eng_diag_buf));
            rlen = eng_diag_product_ctrl(buf,len,eng_diag_buf,sizeof(eng_diag_buf));
            eng_diag_len = rlen;
            eng_diag_write2pc();
            return 0;
        case CMD_USER_DIRECT_PHSCHK:
            ENG_LOG("%s: CMD_USER_DIRECT_PHSCHK\n", __FUNCTION__);
            memset(eng_diag_buf, 0, sizeof(eng_diag_buf));
            rlen = eng_diag_direct_phschk(buf,len,eng_diag_buf,sizeof(eng_diag_buf));
            eng_diag_len = rlen;
            eng_diag_write2pc();
            return 0;
        case CMD_USER_MMICIT_READ:
            ENG_LOG("%s: CMD_USER_MMICIT_READ Req !\n", __FUNCTION__);
            rlen = eng_diag_mmicit_read(buf,len,eng_diag_buf,sizeof(eng_diag_buf));
            eng_diag_len = rlen;
            eng_diag_write2pc();
            return 0;
        case CMD_USER_DEEP_SLEEP:
            ENG_LOG("%s: CMD_USER_DEEP_SLEEP Req!\n", __FUNCTION__);
            rlen = eng_diag_deep_sleep(buf, len, rsp);
            s_cp_ap_proc = 1;// This cmd need cp proc.
            return 0;
        default:
            break;
    }

    memcpy((char*)&head,buf+1,sizeof(MSG_HEAD_T));
    head.len = sizeof(MSG_HEAD_T)+rlen-extra_len;
    ENG_LOG("%s: head.len=%d\n",__FUNCTION__, head.len);
    eng_diag_buf[0] =0x7e;
    if ( (type == CMD_USER_AUDIO) || (type == CMD_USER_APCMD) ) {
        head.seq_num = 0;
        head.type = 0x9c;
        head.subtype = 0x00;
    }

    if( type == CMD_USER_ADC )
    {
        head.subtype = 0x00;
    }

    memcpy(eng_diag_buf+1,&head,sizeof(MSG_HEAD_T));

    if ( type == CMD_USER_AUDIO ) {
        memcpy(eng_diag_buf+sizeof(MSG_HEAD_T)+1,eng_audio_diag_buf,strlen(eng_audio_diag_buf));
    }else if(type == CMD_USER_ADC ){
        memcpy(eng_diag_buf+sizeof(MSG_HEAD_T)+1,adc_rsp,sizeof(adc_rsp));
    }else{
        memcpy(eng_diag_buf+sizeof(MSG_HEAD_T)+1,rsp,sizeof(rsp));
    }
    eng_diag_buf[head.len+extra_len+1] = 0x7e;
    eng_diag_len = head.len+extra_len+2;
    ret = eng_diag_write2pc();
    if ( ret<=0 ){
        ENG_LOG("%s: write to pc failed \n", __FUNCTION__);
        restart_gser();
    }

    if (g_reset){
        eng_diag_reboot(g_reset);
    }

    if ( type == CMD_USER_AUDIO || (CMD_USER_APCMD == type)) {
        ENG_LOG("%s: this is audio type !\n", __FUNCTION__);
        return 1;
    }

    return 0;
}

static int eng_atdiag_parse(unsigned char *buf,int len)
{
    int i;
    int ret=CMD_COMMON;
    MSG_HEAD_T *head_ptr=NULL;
    head_ptr = (MSG_HEAD_T *)(buf+1);

    ENG_LOG("%s: cmd=0x%x; subcmd=0x%x",__FUNCTION__, head_ptr->type, head_ptr->subtype);

    switch(head_ptr->type) {
        case DIAG_CMD_VER:
            ENG_LOG("%s: Handle DIAG_CMD_VER",__FUNCTION__);
            if(head_ptr->subtype==0x00) {
                ret = CMD_USER_VER;
            }
            break;
        default:
            ENG_LOG("%s: Default",__FUNCTION__);
            ret = CMD_COMMON;
            break;
    }
    return ret;
}

int eng_hex2ascii(char *input, char *output, int length)
{
    int i;
    char tmp[3];
    for(i=0; i<length; i++) {
        memset(tmp, 0, sizeof(tmp));
        sprintf(tmp, "%02x",input[i]);
        strcat(output, tmp);
    }

    ENG_LOG("%s: %s",__FUNCTION__, output);
    return strlen(output);
}

int eng_atdiag_euthdlr(char * buf, int len, char * rsp,int module_index)
{
    char args0[15] = {0};
    char args1[15] = {0};
    char *data[2] = {args0,args1};
    int cmd_index = -1;
    get_sub_str(buf,data ,'=' ,',');
    cmd_index = get_cmd_index(buf);
	ENG_LOG("\r\n");
    ENG_LOG("eng_atdiag_euthdlr(), args0 =%s, args1=%s, cmd_index=%d\n",args0,args1,cmd_index);
    switch(cmd_index){
        case EUT_REQ_INDEX:
            if(module_index == BT_MODULE_INDEX){
                ALOGD("case BT_EUT_REQ_INDEX");
                bt_eutops.bteut_req(rsp);
            }
            else if(module_index == WIFI_MODULE_INDEX){
                ENG_LOG("case WIFIEUT_INDEX");
                wifi_eut_get(rsp);
            }
            else {
                ALOGD("case GPS_INDEX");
                gps_eutops.gpseut_req(rsp);
            }
            break;
        case EUT_INDEX:
            if(module_index == BT_MODULE_INDEX){
                ALOGD("case BTEUT_INDEX");
                bt_eutops.bteut(atoi(data[1]),rsp);
            }
            else if(module_index == WIFI_MODULE_INDEX){
                ENG_LOG("case WIFIEUT_INDEX");
                wifi_eut_set(atoi(data[1]), rsp);
            }
            else {
                ALOGD("case GPS_INDEX");
                gps_eutops.gpseut(atoi(data[1]),rsp);
            }
            break;
        case WIFICH_REQ_INDEX:
            wifi_channel_get(rsp);
            break;
        case WIFICH_INDEX:
            ENG_LOG("case WIFICH_INDEX   %d",WIFICH_INDEX);
            wifi_channel_set(atoi(data[1]),rsp);
            break;
        case WIFIMODE_INDEX:
            //wifi_eutops.set_wifi_mode(data[1],rsp);
            break;
        case WIFIRATIO_INDEX:
            ALOGD("case WIFIRATIO_INDEX   %d",WIFIRATIO_INDEX);
            //wifi_eutops.set_wifi_ratio(atof(data[1]),rsp);
            break;
        case WIFITX_FACTOR_INDEX:
            //wifi_eutops.set_wifi_tx_factor(atol(data[1]),rsp);
            break;
        case WIFITX_INDEX:
			ENG_LOG("case WIFITX_INDEX   %d",WIFITX_INDEX);
            wifi_tx_set(atoi(data[1]),rsp);
            break;
        case WIFIRX_INDEX:
            wifi_rx_set(atoi(data[1]),rsp);
            break;
        case WIFITX_REQ_INDEX:
            wifi_tx_get(rsp);
            break;
        case WIFIRX_REQ_INDEX:
            wifi_rx_get(rsp);
            break;
        case WIFITX_FACTOR_REQ_INDEX:
            //wifi_eutops.wifi_tx_factor_req(rsp);
            break;
        case WIFIRATIO_REQ_INDEX:
            //wifi_eutops.wifi_ratio_req(rsp);
            break;
        case WIFIRX_PACKCOUNT_INDEX:
            wifi_rxpktcnt_get(rsp);
            break;
        case WIFICLRRXPACKCOUNT_INDEX:
            //wifi_eutops.wifi_clr_rxpackcount(rsp);
            break;
        case GPSSEARCH_REQ_INDEX:
            gps_eutops.gps_search_req(rsp);
            break;
        case GPSSEARCH_INDEX:
            gps_eutops.gps_search(atoi(data[1]),rsp);
            break;
        case GPSPRNSTATE_REQ_INDEX:
            gps_eutops.gps_prnstate_req(rsp);
            break;
        case GPSSNR_REQ_INDEX:
            gps_eutops.gps_snr_req(rsp);
            break;
        case GPSPRN_INDEX:
            gps_eutops.gps_setprn(atoi(data[1]),rsp);
            break;
//-----------------------------------------------------
		case ENG_WIFIRATE_INDEX:
			ENG_LOG("%s(), case:ENG_WIFIRATE_INDEX\n", __FUNCTION__);
			wifi_rate_set(data[1], rsp);
			break;
		case ENG_WIFIRATE_REQ_INDEX:
			ENG_LOG("%s(), case:ENG_WIFIRATE_REQ_INDEX\n", __FUNCTION__);
			wifi_rate_get(rsp);
			break;
		case ENG_WIFITXGAININDEX_INDEX:
			ENG_LOG("%s(), case:ENG_WIFITXGAININDEX_INDEX\n", __FUNCTION__);
			wifi_txgainindex_set(atoi(data[1]),rsp);
			break;
		case ENG_WIFITXGAININDEX_REQ_INDEX:
			ENG_LOG("%s(), case:ENG_WIFITXGAININDEX_REQ_INDEX\n", __FUNCTION__);
			wifi_txgainindex_get(rsp);
			break;
		case ENG_WIFIRSSI_REQ_INDEX:
			ENG_LOG("%s(), case:ENG_WIFIRSSI_REQ_INDEX\n", __FUNCTION__);
			wifi_rssi_get(rsp);
			break;
//-----------------------------------------------------
        default:
            strcpy(rsp,"can not match the at command");
            return 0;
    }

    // @alvin:
    // Here: I think it will response to pc directly outside
    // this function and should not send to modem again.
    ALOGD(" eng_atdiag_rsp   %s",rsp);

    return 0;
}
int get_sub_str(char *buf,char **revdata, char a, char b)
{
    int len,len1;
    char *start;
    char *current;
    char *end = buf;
    start = strchr(buf,a);
    current = strchr(buf,b);
    ALOGD("get_sub_str ----->>  %d",(int)current);
    if(!current){
        return 0;
    }
    while (end && *end != '\0')
        end++;
    if((start != NULL) & (end !=NULL)){
        start++;
        current++;
        len = current-start-1;
        len1 = end-current;
        ALOGD("get_sub_str  len1= %d",len1);
        memcpy(revdata[0],start,len);
        memcpy(revdata[1],current,len1);
    }
    return 0;
}
int get_cmd_index(char *buf)
{
    int index = -1;
    int i;
    for(i=0;i<(int)NUM_ELEMS(eut_cmds);i++){
       if(strstr(buf,eut_cmds[i].name) != NULL)
        {
            index = eut_cmds[i].index;
            break;
        }
    }
    return index;
}

int eng_atdiag_hdlr(unsigned char *buf,int len, char* rsp)
{
    int i,rlen=0;
    int type=CMD_COMMON;
    MSG_HEAD_T head,*head_ptr=NULL;

    type = eng_atdiag_parse(buf,len);

    ENG_LOG("%s: type=%d",__FUNCTION__, type);
    switch(type) {
        case CMD_USER_VER:
            rlen=eng_diag_getver(buf,len, rsp);
            break;
    }

#if 0 // @alvin: FIX ME
    if(type != CMD_COMMON) {
        memset(eng_diag_buf, 0, sizeof(eng_diag_buf));
        memset(eng_atdiag_buf, 0, sizeof(eng_atdiag_buf));
        memcpy((char*)&head,buf,sizeof(MSG_HEAD_T));
        head.len = sizeof(MSG_HEAD_T)+rlen;
        ENG_LOG("%s: head.len=%d\n",__FUNCTION__, head.len);
        memcpy(eng_diag_buf,&head,sizeof(MSG_HEAD_T));
        memcpy(eng_diag_buf+sizeof(MSG_HEAD_T),rsp,rlen);
        rlen = eng_hex2ascii(eng_diag_buf, eng_atdiag_buf, head.len);
        rlen = eng_atdiag_rsp(eng_get_csclient_fd(), eng_atdiag_buf, rlen);
    }
#endif
    return rlen;
}

int eng_diag(char *buf,int len)
{
    int ret = 0;
    int type;
    int retry_time = 0;
    int ret_val = 0;
    char rsp[512];
    MSG_HEAD_T head,*head_ptr=NULL;

    memset(rsp, 0, sizeof(rsp));

    type = eng_diag_parse(buf,len);

    ENG_LOG("%s:write type=%d\n",__FUNCTION__, type);

    if (type != CMD_COMMON){
        ret_val = eng_diag_user_handle(type, buf, len);
        ENG_LOG("%s:user handle\n",__FUNCTION__);

        if (ret_val) {
            eng_diag_buf[0] =0x7e;

            sprintf(rsp,"%s","\r\nOK\r\n");
            head.len = sizeof(MSG_HEAD_T)+strlen("\r\nOK\r\n");
            ENG_LOG("%s: head.len=%d\n",__FUNCTION__, head.len);
            head.seq_num = 0;
            head.type = 0x9c;
            head.subtype = 0x00;
            memcpy(eng_diag_buf+1,&head,sizeof(MSG_HEAD_T));
            memcpy(eng_diag_buf+sizeof(MSG_HEAD_T)+1,rsp,strlen(rsp));
            eng_diag_buf[head.len+1] = 0x7e;
            eng_diag_len = head.len+2;

            retry_time = 0; //reset retry time counter
write_again:
            ret = eng_diag_write2pc();
            if (ret <= 0) {
                restart_gser();
                if((++retry_time) <= 10){
                    goto write_again; // try 10 times.
                }
            }
        }

        if(s_cp_ap_proc){
            ENG_LOG("%s: This command need to send to CP\n",__FUNCTION__);
            s_cp_ap_proc = 0;
            ret = 0;
        }else{
            ret = 1;
        }
    }

    ENG_LOG("%s: ret=%d\n",__FUNCTION__, ret);

    return ret;
}


//send at should from ril interface @allen
//enable this function temporarily @alvin
int eng_diag_getver(unsigned char *buf,int len, char *rsp)
{
    int wlen,fd;
    int maxlen=248;
    int rlen = 0;
    int cmdlen;
    int sipc_fd;
    MSG_HEAD_T head,*head_ptr=NULL;
    char androidver[256];
    char sprdver[256];
    char modemver[512];
    char *ptr, *atrsp;

    //get android version
    memset(androidver, 0, sizeof(androidver));
    property_get(ENG_ANDROID_VER, androidver, "");
    ENG_LOG("%s: Android %s",__FUNCTION__, androidver);

    //get sprd version
    memset(sprdver, 0, sizeof(sprdver));
    property_get(ENG_SPRD_VERS, sprdver, "");
    ENG_LOG("%s: %s",__FUNCTION__, sprdver);

#if 0 // For getting AP version
#ifndef ENG_AT_CHANNEL
    // open at sipc channel
    sipc_fd = eng_open_dev(at_sipc_devname[g_run_mode], O_WRONLY);
    if(sipc_fd < 0) {
        ENG_LOG("%s: can't open sipc: %s\n", __FUNCTION__, at_sipc_devname[g_run_mode]);
        return 0;
    }

    //get modem version
    do {
        memset(modemver, 0, sizeof(modemver));
        strcpy(modemver, "at+cgmr\r");
        cmdlen=strlen(modemver);
        wlen = write(sipc_fd, modemver, cmdlen);
        memset(modemver, 0, sizeof(modemver));
        rlen = read(sipc_fd, modemver, sizeof(modemver));
        ENG_LOG("%s: %s",__FUNCTION__, modemver);
    }while(strstr(modemver, "desterr")!=NULL);

    close(sipc_fd);// close the sipc channel
#else
    // User at channel, modemId = 0 & simId = 0 is temporary.
    atrsp = sendAt(0, 0, "at+cgmr\r");
    memset(modemver, 0, sizeof(modemver));
    strcpy(modemver, atrsp);
#endif

    ptr = strstr(modemver, "HW");
    *ptr = 0;
#endif
    //ok
    sprintf(rsp, "%s",sprdver);
    rlen = strlen(rsp);

    if(rlen > maxlen) {
        rlen=maxlen;
        rsp[rlen]=0;
    }
    ENG_LOG("%s:rlen=%d; %s", __FUNCTION__,rlen, rsp);
    return rlen;
}

int eng_diag_bootreset(unsigned char *buf,int len, char *rsp)
{
    int rlen = 0;

    sprintf(rsp, "%s","OK");
    rlen = strlen(rsp);

    ENG_LOG("%s:rlen=%d; %s", __FUNCTION__,rlen, rsp);
    return rlen;
}

int eng_diag_apcmd_hdlr(unsigned char *buf, int len, char *rsp)
{
    int rlen = 0, i = 0;
    char *ptr = NULL;

    ptr = buf + 1 + sizeof(MSG_HEAD_T);

    while(*(ptr + i) != 0x7e) {
        i ++;
    }

    *(ptr + i - 1) = '\0';

    ENG_LOG("%s: s_cmd_index: %d", __FUNCTION__, s_cmd_index);
    eng_linuxcmd_hdlr(s_cmd_index, ptr, rsp);

    rlen = strlen(rsp);

    ENG_LOG("%s:rlen:%d; %s", __FUNCTION__, rlen, rsp);

    return rlen;
}

#if 0
int eng_diag_getband(char *buf,int len, char *rsp)
{
    char cmdbuf[64];
    int wlen, rlen, cmdlen;

    memset(cmdbuf, 0, sizeof(cmdbuf));
    sprintf(cmdbuf, "%d,%d",ENG_AT_CURRENT_BAND,0);
    cmdlen=strlen(cmdbuf);
    wlen = eng_at_write(eng_get_csclient_fd(), cmdbuf, cmdlen);

    memset(cmdbuf, 0, sizeof(cmdbuf));
    rlen = eng_at_read(eng_get_csclient_fd(),cmdbuf,sizeof(cmdbuf));
    ENG_LOG("%s: rsp=%s\n",__FUNCTION__, cmdbuf);
    sprintf(rsp, "%s", cmdbuf);

    return rlen;
}
#endif

static void eng_diag_char2hex(unsigned char *hexdata, char *chardata)
{
    int i, index=0;
    char *ptr;
    char tmp[4];

    while((ptr=strchr(chardata, ':'))!=NULL) {
        snprintf(tmp,3, "%s", chardata);
        hexdata[index++]=strtol(tmp, NULL, 16);
        chardata = ptr+1;
    }

    hexdata[index++]=strtol(chardata, NULL, 16);

    for(i=0; i<index; i++){
        ENG_LOG("%s: [%d]=0x%x\n",__FUNCTION__,i,hexdata[i]);
    }
}

int eng_diag_decode7d7e(char *buf,int len)
{
    int i,j;
    char tmp;
    ENG_LOG("%s: len=%d",__FUNCTION__, len);
    for(i=0; i<len; i++) {
        if((buf[i]==0x7d)||(buf[i]==0x7e)){
            tmp = buf[i+1]^0x20;
            ENG_LOG("%s: tmp=%x, buf[%d]=%x",__FUNCTION__, tmp, i+1, buf[i+1]);
            buf[i] = tmp;
            j = i+1;
            memcpy(&buf[j], &buf[j+1],len-j);
            len--;
            ENG_LOG("%s AFTER:",__FUNCTION__);
            /*
            for(j=0; j<len; j++) {
                ENG_LOG("%x,",buf[j]);
            }*/
        }
    }

    return 0;
}

int eng_diag_encode7d7e(char *buf, int len,int *extra_len)
{
    int i,j;
    char tmp;

    ENG_LOG("%s: len=%d",__FUNCTION__, len);

    for(i=0; i<len; i++) {
        if((buf[i]==0x7d)||(buf[i]==0x7e)){
            tmp=buf[i]^0x20;
            ENG_LOG("%s: tmp=%x, buf[%d]=%x",__FUNCTION__, tmp, i, buf[i]);
            buf[i]=0x7d;
            for(j=len; j>i+1; j--) {
                buf[j] = buf[j-1];
            }
            buf[i+1]=tmp;
            len++;
            (*extra_len)++;

            ENG_LOG("%s: AFTER:[%d]",__FUNCTION__, len);
            for(j=0; j<len; j++) {
                ENG_LOG("%x,",buf[j]);
            }
        }
    }

    return len;

}

int eng_diag_btwifi(char *buf,int len, char *rsp, int *extra_len)
{
    int rlen = 0,i;
    int ret=-1;
    unsigned short crc=0;
    unsigned char crc1, crc2, crc3, crc4;
    char tmp;
    char btaddr[32]={0};
    char wifiaddr[32]={0};
    char *pBtAddr = NULL, *pWifiAddr = NULL;
    REF_NVWriteDirect_T *direct;
    MSG_HEAD_T *head_ptr=NULL;
    head_ptr = (MSG_HEAD_T *)(buf+1);
    direct = (REF_NVWriteDirect_T *)(buf + DIAG_HEADER_LENGTH + 1);

    ENG_LOG("Call %s, subtype=%x\n",__FUNCTION__, head_ptr->subtype);
    eng_diag_decode7d7e((char *)direct, len-DIAG_HEADER_LENGTH-1);

    if((head_ptr->subtype&DIAG_CMD_READ)==0){ 	//write command
        crc1 = *(buf + DIAG_HEADER_LENGTH + sizeof(REF_NVWriteDirect_T) + 1);
        crc2 = *(buf + DIAG_HEADER_LENGTH + sizeof(REF_NVWriteDirect_T) + 2);
        crc = crc16(crc,(const unsigned char*)direct,sizeof(REF_NVWriteDirect_T));
        crc3 = crc&0xff;
        crc4 = (crc>>8)&0xff;
        ENG_LOG("%s: crc [%x,%x], [%x,%x]\n",__func__,crc3,crc4,crc1,crc2);

        if((crc1==crc3)&&(crc2==crc4)){
            //write bt address
            if((head_ptr->subtype&DIAG_CMD_BTBIT)>0) {
                sprintf(btaddr, "%02x:%02x:%02x:%02x:%02x:%02x",\
                        direct->btaddr[5],direct->btaddr[4],direct->btaddr[3], \
                        direct->btaddr[2],direct->btaddr[1],direct->btaddr[0]);
                pBtAddr = btaddr;
                ENG_LOG("%s: BTADDR:%s\n",__func__, btaddr);
            }

            //write wifi address
            if((head_ptr->subtype&DIAG_CMD_WIFIBIT)>0) {
                sprintf(wifiaddr, "%02x:%02x:%02x:%02x:%02x:%02x",\
                        direct->wifiaddr[0],direct->wifiaddr[1],direct->wifiaddr[2], \
                        direct->wifiaddr[3],direct->wifiaddr[4],direct->wifiaddr[5]);
                pWifiAddr = wifiaddr;
                ENG_LOG("%s: WIFIADDR:%s\n",__func__,wifiaddr);
            }

            eng_btwifimac_write(pBtAddr, pWifiAddr);
        }

        if(!s_cp_ap_proc){
            //alreays write successfully
            head_ptr->subtype = 0x01;
            rsp[0]=0x00; rsp[1]=0x00;
            rlen=2;
        }
    } else {//read command
        direct = (REF_NVWriteDirect_T *)rsp;

        //read btaddr
        if((head_ptr->subtype&DIAG_CMD_BTBIT)>0) {
            ret = eng_btwifimac_read(btaddr, ENG_BT_MAC);
            ENG_LOG("%s: after BTADDR:%s\n",__func__, btaddr);
            pBtAddr = (char *)(direct->btaddr);
            if(!ret) {
                eng_diag_char2hex((unsigned char *)pBtAddr, btaddr);
                tmp=pBtAddr[0]; pBtAddr[0]=pBtAddr[5];pBtAddr[5]=tmp;	//converge BT address
                tmp=pBtAddr[1]; pBtAddr[1]=pBtAddr[4];pBtAddr[4]=tmp;
                tmp=pBtAddr[2]; pBtAddr[2]=pBtAddr[3];pBtAddr[3]=tmp;
            }
        }

        //read wifiaddr
        if((head_ptr->subtype&DIAG_CMD_WIFIBIT)>0) {
            ret = eng_btwifimac_read(wifiaddr, ENG_WIFI_MAC);
            ENG_LOG("%s: after WIFIADDR:%s\n",__func__, wifiaddr);
            pWifiAddr = (char *)(direct->wifiaddr);
            if(!ret)
                eng_diag_char2hex((unsigned char *)pWifiAddr, wifiaddr);
        }

        //response
        head_ptr->subtype = 0x01;
        rlen = sizeof(REF_NVWriteDirect_T);
        crc = crc16(crc,(const unsigned char*)direct,sizeof(REF_NVWriteDirect_T));

        rlen = eng_diag_encode7d7e((char *)direct, rlen, extra_len);
        memcpy(rsp, direct, rlen);
        *(rsp+rlen) = crc&0xff;
        *(rsp+rlen+1) = (crc>>8)&0xff;
        ENG_LOG("%s: read crc = %d, [%x,%x],extra_len=%d\n",__func__, \
                crc, *(rsp+rlen), *(rsp+rlen+1),*extra_len);
        rlen += 2;
    }

    // clear BT/WIFI bit in this diag framer
    if(s_cp_ap_proc){
        head_ptr->subtype &= ~(DIAG_CMD_BTBIT|DIAG_CMD_WIFIBIT);
    }

    ENG_LOG("%s: rlen=%d\n",__func__, rlen);
    return rlen;
}

int eng_diag_factorymode(char *buf,int len, char *rsp)
{
    char *pdata=NULL;
    MSG_HEAD_T *head_ptr=NULL;
    char value[PROPERTY_VALUE_MAX];

    head_ptr = (MSG_HEAD_T *)(buf+1);
    pdata = buf + DIAG_HEADER_LENGTH + 1; //data content;

    ENG_LOG("%s: operation=%x; end=%x\n",__func__, *pdata, *(pdata+1));

    switch(*pdata) {
        case 0x00:
            ENG_LOG("%s: should close the vser,gser when next reboot\n",__FUNCTION__);
        case 0x01:
            eng_sql_string2int_set(ENG_TESTMODE, *pdata);
            eng_check_factorymode(1);
            head_ptr->subtype = 0x00;
            break;
        default:
            head_ptr->subtype = 0x01;
            break;
    }

    return 0;
}

int ascii2bin(unsigned char *dst,unsigned char *src,unsigned long size){
    unsigned char h,l;
    unsigned long count = 0;

    if((NULL==dst)||(NULL==src))
        return -1;

    while(count<size){
        if((*src>='0')&&(*src<='9')){
            h = *src - '0';
        }else{
            h = *src - 'A' + 10;
        }

        src++;

        if((*src>='0')&&(*src<='9')){
            l = *src - '0';
        }else{
            l = *src - 'A' + 10;
        }

        src++;
        count +=2;

        *dst = (unsigned char)(h<<4|l);
        dst++;
    }

    return 0;
}

int bin2ascii(unsigned char *dst,unsigned char *src,unsigned long size){
    unsigned char semi_octet;
    unsigned long count = 0;

    if((NULL==dst)||(NULL==src))
        return -1;

    while(count<size){
        semi_octet = ((*src)&0xf0)>>4;
        if(semi_octet<=9){
            *dst = semi_octet + '0';
        }else{
            *dst = semi_octet + 'A' - 10;
        }

        dst++;

        semi_octet = ((*src)&0x0f);
        if(semi_octet<=9){
            *dst = semi_octet + '0';
        }else{
            *dst = semi_octet + 'A' - 10;
        }

        dst++;

        src++;
        count ++;
    }

    return 0;
}

int at_tok_equel_start(char **p_cur)
{
    if (*p_cur == NULL) {
        return -1;
    }

    // skip prefix
    // consume "^[^:]:"

    *p_cur = strchr(*p_cur, '=');

    if (*p_cur == NULL) {
        return -1;
    }

    (*p_cur)++;

    return 0;
}

int is_ap_at_cmd_need_to_handle(char *buf, int len)
{
    unsigned int i,ret = 0;
    MSG_HEAD_T *head_ptr=NULL;
    char *ptr = NULL;

    if(NULL == buf){
        ENG_LOG("%s,null pointer",__FUNCTION__);
        return 0;
    }

    head_ptr = (MSG_HEAD_T *)(buf+1);
    ptr = buf + 1 + sizeof(MSG_HEAD_T);

    if(-1 != (s_cmd_index = eng_at2linux(ptr))) {
        return 1;
    } else {
        return 0;
    }
}

/**
 * @brief judge if the AT cmd need to handle
 *
 * @param buf diag buf
 * @param len diag len
 *
 * @return 1 if the cmd need to handle,otherwise 0
 */
int is_audio_at_cmd_need_to_handle(char *buf,int len){
    unsigned int i,ret = 0;
    MSG_HEAD_T *head_ptr=NULL;
    char *ptr = NULL;

    if(NULL == buf){
        ENG_LOG("%s,null pointer",__FUNCTION__);
        return 0;
    }

    head_ptr = (MSG_HEAD_T *)(buf+1);
    ptr = buf + 1 + sizeof(MSG_HEAD_T);

    if(g_is_data)
    {
        //if not AT cmd
        ret = strncmp(ptr,"AT",strlen("AT"));
        if((ret!=0&&isdigit(*ptr))||(ret!=0&&isupper(*ptr))){
            return 1;
        }
    }

    //AT+SADM4AP
    ret = strncmp(ptr,at_sadm,strlen(at_sadm));
    if ( 0==ret ) {
        at_tok_equel_start(&ptr);
        at_tok_nextint(&ptr,&cmd_type);
        ENG_LOG("%s,SADM4AP :value = 0x%02x",__FUNCTION__,cmd_type);
        for ( i = 0; i < sizeof(at_sadm_cmd_to_handle)/sizeof(int); i += 1 ) {
            if(-1==at_sadm_cmd_to_handle[i]){
                ENG_LOG("end of at_sadm_cmd_to_handle");
                return 0;
            }
            if ( at_sadm_cmd_to_handle[i]==cmd_type ) {
                ENG_LOG("at_sadm_cmd_to_handle=%d",at_sadm_cmd_to_handle[i]);
                if ( GET_ARM_VOLUME_MODE_COUNT!=cmd_type ) {
                    ENG_LOG("NOT CMD TO GET COUNT");
                    g_index = atoi(ptr);
                    //g_index -= '0';
                    ENG_LOG("index = %d",g_index);
                    if ( g_index>=adev_get_audiomodenum4eng()) {
                        return 0;
                    }
                }
                return 1;
            }
        }
    }

    //AT+SPENHA
    ret = strncmp(ptr,at_spenha,strlen(at_spenha));
    if ( 0==ret ) {
        at_tok_equel_start(&ptr);
        at_tok_nextint(&ptr,&eq_or_tun_type);
        at_tok_nextint(&ptr,&cmd_type);
        ENG_LOG("%s,SPENHA :value = 0x%02x",__FUNCTION__,cmd_type);
        for ( i = 0; i < sizeof(at_spenha_cmd_to_handle)/sizeof(int); i += 1 ) {
            if(-1==at_spenha_cmd_to_handle[i]){
                ENG_LOG("end of at_spenha_cmd_to_handle");
                return 0;
            }

            if ( at_spenha_cmd_to_handle[i]==cmd_type ) {
                ENG_LOG("at_spenha_cmd_to_handle=%d",at_spenha_cmd_to_handle[i]);
                if ( GET_AUDIO_ENHA_MODE_COUNT!=cmd_type ) {
                    at_tok_nextint(&ptr,&g_index);
                    if (( g_index>adev_get_audiomodenum4eng())||( g_index<=0)) {
                        return 0;
                    }
                    g_index--;
                    ENG_LOG("BINARY index = %x",g_index);
                    if ( SET_AUDIO_ENHA_DATA_TO_MEMORY==cmd_type ) {
                        eq_mode_sel_type = *ptr;
                        eq_mode_sel_type -= '0';
                        ENG_LOG("BINARY eq_mode_sel_type = %x",eq_mode_sel_type);
                    }
                }


                return 1;
            }
        }
    }
    //AT+CALIBR
    ret = strncmp(ptr,at_calibr,strlen(at_calibr));
    if ( 0==ret ) {
        at_tok_equel_start(&ptr);
        at_tok_nextint(&ptr,&cmd_type);
        ENG_LOG("%s,CALIBR :value = 0x%02x",__FUNCTION__,cmd_type);
        return 1;
    }

    ENG_LOG("%s,cmd don't need to handle",__FUNCTION__);

    return 0;
}

int eng_diag_adc(char *buf, int *Irsp)
{
    MSG_HEAD_T *head_ptr=NULL;
    unsigned char buf1[8];
    int result;
    if(Irsp != NULL){
        sprintf(Irsp,"\r\nERROR\r\n");
    }
    else
    {
        ENG_LOG("%s,in eng_diag_adc,Irsp is null",__FUNCTION__);
        return 0;
    }

    head_ptr = (MSG_HEAD_T *)(buf+1);
    memset(buf1, 0, 8);
    sprintf(buf1,"%d", head_ptr->subtype);
    adc_get_result(buf1);
    result = atoi(buf1);
    Irsp[0] = result;
    return strlen(Irsp);
}

//for mobile test tool
void At_cmd_back_sig(void)
{
    eng_diag_buf[0] =0x7e;

    eng_diag_buf[1] =0x00;
    eng_diag_buf[2] =0x00;
    eng_diag_buf[3] =0x00;
    eng_diag_buf[4] =0x00;
    eng_diag_buf[5] =0x08;
    eng_diag_buf[6] =0x00;
    eng_diag_buf[7] =0xD5;
    eng_diag_buf[8] =0x00;

    eng_diag_buf[9] =0x7e;
    eng_diag_write2pc();
    memset(eng_diag_buf, 0, 10);
}

static AUDIO_TOTAL_T * eng_regetpara(void)
{
    int srcfd;
    char *filename = NULL;
    //eng_getparafromnvflash();
    ALOGW("wangzuo eng_regetpara 1");

    AUDIO_TOTAL_T * aud_params_ptr;
    int len = sizeof(AUDIO_TOTAL_T)*adev_get_audiomodenum4eng();

    aud_params_ptr = calloc(1, len);
    if (!aud_params_ptr)
        return 0;
    memset(aud_params_ptr, 0, len);
    srcfd = open((char *)(ENG_AUDIO_PARA_DEBUG), O_RDONLY);
    filename = (srcfd < 0 )? ( ENG_AUDIO_PARA):(ENG_AUDIO_PARA_DEBUG);
    if(srcfd >= 0)
    {
        close(srcfd);
    }


    ALOGW("eng_regetpara %s", filename);////done,into
    stringfile2nvstruct(filename,aud_params_ptr,len);

    return aud_params_ptr;
}

static void eng_setpara(AUDIO_TOTAL_T * ptr)
{//to do
    int len = sizeof(AUDIO_TOTAL_T)*adev_get_audiomodenum4eng();

    ALOGW("wangzuo eng_setpara 2");
    nvstruct2stringfile(ENG_AUDIO_PARA_DEBUG, ptr, len);
}
static void eng_notify_mediaserver_updatapara(int ram_ops,int index,AUDIO_TOTAL_T *aud_params_ptr)
{
    int result = 0;
    int fifo_id = -1;
    int ret;
    ALOGE("eng_notify_mediaserver_updatapara E,%d:%d!\n",ram_ops,index);
    fifo_id = open( AUDFIFO ,O_WRONLY|O_NONBLOCK);
    if(fifo_id != -1) {
        int buff = 1;
        ALOGE("eng_notify_mediaserver_updatapara notify buff!\n");
        result = write(fifo_id,&ram_ops,sizeof(int));
        if(ram_ops & ENG_RAM_OPS)
        {
            result = write(fifo_id,&index,sizeof(int));
            result = write(fifo_id,aud_params_ptr,sizeof(AUDIO_TOTAL_T));
            ALOGE("eng_notify_mediaserver_updatapara,index:%d,size:%d!\n",index,sizeof(AUDIO_TOTAL_T));
        }
        close(fifo_id);
    } else {
        ALOGE("%s open audio FIFO error %s,fifo_id:%d\n",__FUNCTION__,strerror(errno),fifo_id);
    }

    ALOGE("eng_notify_mediaserver_updatapara X,result:%d!\n",result);
    return result;
}

void * eng_getpara(void)
{
    int srcfd;
    char *filename = NULL;
    ALOGW("wangzuo eng_getpara 3");////done,into
    int audio_fd;
    static int read = 0;
    int len = sizeof(AUDIO_TOTAL_T)*adev_get_audiomodenum4eng();
    if(read)
    {
        ALOGW("eng_getpara read already.");////done,into
        return audio_total;
    }
    else
    {
        read =1;
    }
    memset(audio_total, 0, len);
    srcfd = open((char *)(ENG_AUDIO_PARA_DEBUG), O_RDONLY);
    filename = (srcfd < 0 )? ( ENG_AUDIO_PARA):(ENG_AUDIO_PARA_DEBUG);
    if(srcfd >= 0)
    {
        close(srcfd);
    }
    ALOGW("wangzuo eng_getpara %s", filename);////done,into
    stringfile2nvstruct(filename, audio_total, len); //get data from audio_hw.txt.
    return  audio_total;
}
int eng_diag_audio(char *buf,int len, char *rsp)
{
    int fd;
    int wlen,rlen,ret = 0;
    MSG_HEAD_T *head_ptr=NULL;
    char *ptr = NULL;
    AUDIO_TOTAL_T *audio_ptr;
    int audio_fd = -1;
    int audiomode_count =0;
    int ram_ofs = 0;
    if(rsp != NULL){
        sprintf(rsp,"\r\nERROR\r\n");
    }

    if (( NULL == buf )||( NULL == rsp)) {
        goto out;
    }

    head_ptr = (MSG_HEAD_T *)(buf+1);
    ENG_LOG("Call %s, subtype=%x\n",__FUNCTION__, head_ptr->subtype);
    ptr = buf + 1 + sizeof(MSG_HEAD_T);

    //AT+CALIBR
    ret = strncmp(ptr,at_calibr,strlen(at_calibr));
    if ( 0==ret ) {
        if(SET_CALIBRATION_ENABLE == cmd_type){
            enable_calibration();
            sprintf(rsp,"\r\nenable_calibration   \r\n");
        }else if (SET_CALIBRATION_DISABLE == cmd_type){
            disable_calibration();
            sprintf(rsp,"\r\ndisable_calibration   \r\n");
        }
        At_cmd_back_sig();
        //return rsp != NULL ? strlen(rsp):0;
        return strlen(rsp);
    }

    //audio_fd = open(ENG_AUDIO_PARA_DEBUG,O_RDWR);
    ENG_LOG("Call %s, ptr=%s\n",__FUNCTION__, ptr);

    if(g_is_data){
        ENG_LOG("HEY,DATA HAS COME!!!!");
        g_is_data = g_is_data;
        wlen = head_ptr->len - sizeof(MSG_HEAD_T) - 1;
        ENG_LOG("NOTICE:length is %x,%x,%x,%x",wlen,sizeof(AUDIO_TOTAL_T),sizeof(AUDIO_NV_ARM_MODE_INFO_T),sizeof(AUDIO_ENHA_EQ_STRUCT_T));

        audio_ptr = (AUDIO_TOTAL_T *)eng_regetpara();//audio_ptr = (AUDIO_TOTAL_T *)mmap(0,4*sizeof(AUDIO_TOTAL_T),PROT_READ|PROT_WRITE,MAP_SHARED,audio_fd,0);
        if ((AUDIO_TOTAL_T *)(-1) == audio_ptr || (AUDIO_TOTAL_T *)(0) == audio_ptr ) {
            ALOGE("mmap failed %s",strerror(errno));
            goto out;
        }


        if ( g_is_data&AUDIO_NV_ARM_DATA_MEMORY){
            ram_ofs |=ENG_RAM_OPS;
            g_is_data &= (~AUDIO_NV_ARM_DATA_MEMORY);
            g_indicator |= AUDIO_NV_ARM_INDI_FLAG;
            ascii2bin((unsigned char *)(&audio_total[g_index].audio_nv_arm_mode_info.tAudioNvArmModeStruct),(unsigned char *)ptr,wlen);
        }
        if ( g_is_data&AUDIO_NV_ARM_DATA_FLASH){
            ram_ofs |=ENG_FLASH_OPS;
            g_is_data &= (~AUDIO_NV_ARM_DATA_FLASH);
            g_indicator |= AUDIO_NV_ARM_INDI_FLAG;
            ascii2bin((unsigned char *)(&audio_total[g_index].audio_nv_arm_mode_info.tAudioNvArmModeStruct),(unsigned char *)ptr,wlen);
            audio_ptr[g_index].audio_nv_arm_mode_info.tAudioNvArmModeStruct=audio_total[g_index].audio_nv_arm_mode_info.tAudioNvArmModeStruct;
        }
        if ( g_is_data&AUDIO_ENHA_DATA_MEMORY){
            ram_ofs |=ENG_RAM_OPS;
            g_is_data &= (~AUDIO_ENHA_DATA_MEMORY);
            g_indicator |= AUDIO_ENHA_EQ_INDI_FLAG;
            ascii2bin((unsigned char *)(&audio_total[g_index].audio_enha_eq),(unsigned char *)ptr,wlen);
        }
        if ( g_is_data&AUDIO_ENHA_DATA_FLASH){
            ram_ofs |=ENG_FLASH_OPS;
            g_is_data &= (~AUDIO_ENHA_DATA_FLASH);
            g_indicator |= AUDIO_ENHA_EQ_INDI_FLAG;
            ascii2bin((unsigned char *)(&audio_total[g_index].audio_enha_eq),(unsigned char *)ptr,wlen);
            audio_ptr[g_index].audio_enha_eq=audio_total[g_index].audio_enha_eq;
        }
        if ( g_is_data&AUDIO_ENHA_TUN_DATA_MEMORY){
            ram_ofs |=ENG_RAM_OPS;
            g_is_data &= (~AUDIO_ENHA_TUN_DATA_MEMORY);
            ascii2bin((unsigned char *)tun_data,(unsigned char *)ptr,wlen);
        }

        ENG_LOG("g_indicator = 0x%x,%x\n",g_indicator,AUDIO_DATA_READY_INDI_FLAG);

        if ( audio_ptr ) {
            if(ram_ofs & ENG_FLASH_OPS)
            {
                eng_setpara(audio_ptr);
            }

            if(g_indicator)
            {
                ram_ofs |= ENG_PGA_OPS;
            }
            eng_notify_mediaserver_updatapara(ram_ofs,g_index,&audio_total[g_index]);
            free(audio_ptr);
        }

        if ( g_indicator ) {
            ENG_LOG("data is ready!g_indicator = 0x%x,g_index:%d\n",g_indicator,g_index);
            g_indicator = 0;
            parse_vb_effect_params((void *)audio_total, adev_get_audiomodenum4eng()*sizeof(AUDIO_TOTAL_T));
        }

        sprintf(rsp,"\r\nOK\r\n");
        goto out;
    }

    //if ptr points to "AT+SADM4AP"
    ret = strncmp(ptr,at_sadm,strlen(at_sadm));
    if ( 0==ret ) {
        switch ( cmd_type) {
            case GET_ARM_VOLUME_MODE_COUNT:
                ENG_LOG("%s,GET MODE COUNT:%d\n",__FUNCTION__,adev_get_audiomodenum4eng());
                sprintf(rsp,"+SADM4AP: %d",adev_get_audiomodenum4eng());
                ENG_LOG("%s,GET MODE COUNT:%s\n",__FUNCTION__,rsp);
                break;
            case GET_ARM_VOLUME_MODE_NAME:
                ENG_LOG("ARM VOLUME NAME is %s",audio_total[g_index].audio_nv_arm_mode_info.ucModeName);
                sprintf(rsp,"+SADM4AP: %d,\"%s\"",g_index,audio_total[g_index].audio_nv_arm_mode_info.ucModeName);
                ENG_LOG("%s,GET_ARM_VOLUME_MODE_NAME:%d ---- >%s \n",__FUNCTION__,g_index,rsp);
                break;
            case SET_ARM_VOLUME_DATA_TO_RAM:
                ENG_LOG("%s,set arm nv mode data to memory\n",__FUNCTION__);
                g_is_data |= AUDIO_NV_ARM_DATA_MEMORY;
                sprintf(rsp,"\r\n> ");
                break;
            case SET_ARM_VOLUME_DATA_TO_FLASH:
                ENG_LOG("%s,set arm nv mode data to flash\n",__FUNCTION__);
                g_is_data |= AUDIO_NV_ARM_DATA_FLASH;
                sprintf(rsp,"\r\n> ");
                break;
            case GET_ARM_VOLUME_DATA_FROM_FLASH:
                audio_ptr = (AUDIO_TOTAL_T *)eng_regetpara();//(AUDIO_TOTAL_T *)mmap(0,4*sizeof(AUDIO_TOTAL_T),PROT_READ|PROT_WRITE,MAP_SHARED,audio_fd,0);
                if (((AUDIO_TOTAL_T *)( -1 )!= audio_ptr) && ((AUDIO_TOTAL_T *)( 0 )!= audio_ptr) ) {
                    //audio_total[g_index].audio_nv_arm_mode_info.tAudioNvArmModeStruct=audio_ptr[g_index].audio_nv_arm_mode_info.tAudioNvArmModeStruct;
                    //munmap((void *)audio_ptr,4*sizeof(AUDIO_TOTAL_T));
                    audio_total[g_index].audio_nv_arm_mode_info.tAudioNvArmModeStruct=audio_ptr[g_index].audio_nv_arm_mode_info.tAudioNvArmModeStruct;
                    free(audio_ptr);
                }
                //there is no break in this case,'cause it will share the code with the following case
            case GET_ARM_VOLUME_DATA_FROM_RAM:
                ENG_LOG("%s,get arm volume data,audio_total:0x%0x,--->%d \n",__FUNCTION__,audio_total,g_index);
                sprintf(rsp,"+SADM4AP: 0,\"%s\",",audio_total[g_index].audio_nv_arm_mode_info.ucModeName);
                bin2ascii((unsigned char *)(rsp+strlen(rsp)),(unsigned char *)(&audio_total[g_index].audio_nv_arm_mode_info.tAudioNvArmModeStruct),sizeof(AUDIO_NV_ARM_MODE_STRUCT_T));
                break;

            default:
                sprintf(rsp,"\r\nERROR\r\n");
                break;
        }				/* -----  end switch  ----- */

    }

    //AT+SPENHA
    ret = strncmp(ptr,at_spenha,strlen(at_spenha));
    if ( 0==ret ) {
        ENG_LOG("receive AT+SPENHA cmd\n");
        if ( 1 == eq_or_tun_type ) {
            switch ( cmd_type ) {
                case GET_AUDIO_ENHA_MODE_COUNT:
                    sprintf(rsp,"+SPENHA: 1");
                    break;
                case SET_AUDIO_ENHA_DATA_TO_MEMORY:
                case SET_AUDIO_ENHA_DATA_TO_FLASH:
                    ENG_LOG("%s,set enha tun data to flash\n",__FUNCTION__);
                    g_is_data |= AUDIO_ENHA_TUN_DATA_MEMORY;
                    sprintf(rsp,"\r\n> ");
                    break;
                case GET_AUDIO_ENHA_DATA_FROM_FLASH:
                case GET_AUDIO_ENHA_DATA_FROM_MEMORY:
                    ENG_LOG("%s,get audio enha data\n",__FUNCTION__);
                    sprintf(rsp,"+SPENHA: %d,",g_index);
                    bin2ascii((unsigned char *)(rsp+strlen(rsp)),(unsigned char *)tun_data,sizeof(tun_data));
                    break;
                default:
                    break;
            }
        }else{

            switch ( cmd_type ) {
                case GET_AUDIO_ENHA_MODE_COUNT:
                    sprintf(rsp,"+SPENHA: %d",adev_get_audiomodenum4eng());
                    break;

                case SET_AUDIO_ENHA_DATA_TO_MEMORY:
                    ENG_LOG("%s,set enha data to memory\n",__FUNCTION__);
                    g_is_data |=  AUDIO_ENHA_DATA_MEMORY;
                    sprintf(rsp,"\r\n> ");
                    break;
                case SET_AUDIO_ENHA_DATA_TO_FLASH:
                    ENG_LOG("%s,set enha data to flash\n",__FUNCTION__);
                    g_is_data |= AUDIO_ENHA_DATA_FLASH;
                    sprintf(rsp,"\r\n> ");
                    break;
                case GET_AUDIO_ENHA_DATA_FROM_FLASH:
                    audio_ptr = (AUDIO_TOTAL_T *)eng_regetpara();// (AUDIO_TOTAL_T *)mmap(0,4*sizeof(AUDIO_TOTAL_T),PROT_READ|PROT_WRITE,MAP_SHARED,audio_fd,0);
                    if ( NULL != audio_ptr ) {
                        audio_total[g_index].audio_enha_eq=audio_ptr[g_index].audio_enha_eq;
                        free(audio_ptr);//munmap((void *)audio_ptr,4*sizeof(AUDIO_TOTAL_T));
                    }
                    //there is no break in this case,'cause it will share the code with the following case
                case GET_AUDIO_ENHA_DATA_FROM_MEMORY:
                    ENG_LOG("%s,get audio enha data\n",__FUNCTION__);
                    sprintf(rsp,"+SPENHA: %d,",g_index);
                    bin2ascii((unsigned char *)(rsp+strlen(rsp)),(unsigned char *)(&audio_total[g_index].audio_enha_eq),sizeof(AUDIO_ENHA_EQ_STRUCT_T));
                    break;
                default:
                    break;
            }				/* -----  end switch  ----- */
        }
    }

out:
    //if (audio_fd >=0)
    //close(audio_fd);

    return rsp != NULL ? strlen(rsp):0;
}

static int eng_diag_product_ctrl(char *buf, int len, char *rsp, int rsplen)
{
    int offset   = 0;
    int data_len = 0;
    int head_len = 0;
    int rsp_len  = 0;
    unsigned char* nvdata = NULL;
    NVITEM_ERROR_E nverr = NVERR_NONE;
    MSG_HEAD_T *msg_head = (MSG_HEAD_T*)(buf + 1);

    head_len = sizeof(MSG_HEAD_T) + 2*sizeof(unsigned short);
    offset   = *(unsigned short*)((char*)msg_head + sizeof(MSG_HEAD_T));
    data_len = *(unsigned short*)((char*)msg_head + sizeof(MSG_HEAD_T) + sizeof(unsigned short));

    ENG_LOG("%s: offset: %d, data_len: %d\n", __FUNCTION__, offset, data_len);

    if(rsplen < (head_len + data_len + 2)) {// 2:0x7e
        ENG_LOG("%s: Rsp buffer is not enough, need buf: %d\n", __FUNCTION__, head_len + data_len);
        return 0;
    }

    // 2: NVITEM_PRODUCT_CTRL_READ
    // 3: NVITEM_PRODUCT_CTRL_WRITE
    ENG_LOG("%s: msg_head->subtype: %d\n", __FUNCTION__, msg_head->subtype);
    switch(msg_head->subtype) {
        case 2:
            {
                nvdata = (unsigned char*)malloc(data_len + head_len);
                memcpy(nvdata, msg_head, head_len);


                nverr = eng_read_productnvdata(nvdata + head_len, data_len);
                if(NVERR_NONE != nverr) {
                    ENG_LOG("%s: Read ERROR: %d\n", __FUNCTION__,nverr);
                    data_len = 0;
                }

                ((MSG_HEAD_T*)nvdata)->subtype = nverr;
                ((MSG_HEAD_T*)nvdata)->len = head_len + data_len;

                rsp_len = translate_packet(rsp, nvdata, head_len + data_len);

                free(nvdata);
            }
            break;
        case 3:
            {
                nvdata = (unsigned char*)malloc(rsplen);


                nverr = eng_read_productnvdata(nvdata , data_len);
                if(NVERR_NONE != nverr ) {
                    ENG_LOG("%s: Read before writing ERROR: %d\n", __FUNCTION__,nverr);
                }else {
                    memcpy(nvdata + offset, (char*)msg_head + head_len, data_len);
                    nverr = eng_write_productnvdata(nvdata, data_len);
                    if(NVERR_NONE != nverr) {
                        ENG_LOG("%s:Write ERROR: %d\n", __FUNCTION__,nverr);
                    }
                }

                free(nvdata);

                msg_head->subtype = nverr;
                msg_head->len = sizeof(MSG_HEAD_T);

                rsp_len = translate_packet(rsp, (unsigned char*)msg_head, sizeof(MSG_HEAD_T));
            }
            break;
        default:
            ENG_LOG("%s: ERROR Oper: %d !\n",__FUNCTION__, msg_head->subtype);
            return 0;
    }

    ENG_LOG("%s: rsp_len : %d\n", __FUNCTION__, rsp_len);

    return rsp_len;
}

static int eng_diag_direct_phschk(char *buf,int len, char *rsp, int rsplen)
{
    int crc      = 0;
    int data_len = 0;
    int recv_crc = 0;
    int rsp_len  = 0;
    unsigned char result;
    unsigned char* nvdata;
    ERR_IMEI_E error;

    NVITEM_ERROR_E nverr = NVERR_NONE;
    MSG_HEAD_T *msg_head = (MSG_HEAD_T*)(buf + 1);

    do {
        recv_crc = *(unsigned short*)&(buf[msg_head->len - sizeof(unsigned short) + 1]);
        crc = crc16(0,(unsigned char*)(msg_head + 1), msg_head->len - sizeof(MSG_HEAD_T) - sizeof(unsigned short));

        if(recv_crc != crc) {
            ENG_LOG("%s: CRC Error! recv_crc: %d, crc16: %d\n", __FUNCTION__, recv_crc, crc);
            msg_head->len = sizeof(MSG_HEAD_T) + 2*sizeof(unsigned short);
            *(unsigned short*)(msg_head + 1) = IMEI_CRC_ERR;
            break;
        }

        ENG_LOG("%s: Current oper: %d\n", __FUNCTION__, (msg_head->subtype & RW_MASK));

        if((msg_head->subtype & RW_MASK) == WRITE_MODE){
            if(0 != (msg_head->subtype & RM_VALID_CMD_MASK)){
                nvdata = (unsigned char*)(msg_head + 1);
                data_len = msg_head->len - sizeof(MSG_HEAD_T) - sizeof(unsigned short);

                ENG_LOG("%s: data_len: %d\n", __FUNCTION__, data_len);


                nverr = eng_write_productnvdata(nvdata, data_len);
                if(NVERR_NONE != nverr) {
                    ENG_LOG("%s:Write ERROR: %d\n", __FUNCTION__,nverr);
                    error  = IMEI_SAVE_ERR;
                    result = 0;
                }else {
                    error  = IMEI_ERR_NONE;
                    result = 1;
                }
            }else{
                ENG_LOG("%s: Write error, subtype : %d\n", __FUNCTION__, msg_head->subtype);
                error  = IMEI_CMD_ERR;
                result = 0;
            }

            if(result) {
                msg_head->len = sizeof(MSG_HEAD_T) + sizeof(unsigned short);
                msg_head->subtype = MSG_ACK;
                *((unsigned short*)((unsigned char*)(msg_head + 1))) = 0;
            }else {
                msg_head->subtype = MSG_NACK;
                *(unsigned short*)(msg_head + 1) = error;
                *((unsigned short*)((unsigned char*)(msg_head + 1) + sizeof(unsigned short))) = 0;
                msg_head->len = sizeof(MSG_HEAD_T) + 2*sizeof(unsigned short);
            }

            rsp_len = translate_packet(rsp, (unsigned char*)msg_head, msg_head->len);
        }else { // Read Mode
            ENG_LOG("%s: Read Mode ! \n", __FUNCTION__);
            nvdata = (unsigned char*)malloc(rsplen + sizeof(MSG_HEAD_T) + sizeof(unsigned short));
            memcpy(nvdata, msg_head, sizeof(MSG_HEAD_T));

            nverr = eng_read_productnvdata(nvdata + sizeof(MSG_HEAD_T), rsplen);
            if(NVERR_NONE != nverr) {
                ENG_LOG("%s:Read ERROR: %d\n", __FUNCTION__,nverr);
                msg_head->len = sizeof(MSG_HEAD_T) + sizeof(unsigned short);
                *((unsigned short*)((unsigned char*)(msg_head + 1))) = 0;
                msg_head->subtype = MSG_NACK;
            }else {
                msg_head = (MSG_HEAD_T*)nvdata;
                msg_head->len = sizeof(MSG_HEAD_T) + sizeof(TEST_DATA_INFO_T) + sizeof(unsigned short);
                *((unsigned short*)((unsigned char*)(msg_head + 1) + sizeof(TEST_DATA_INFO_T))) =
                    crc16(0, ((unsigned char*)(msg_head + 1)), sizeof(TEST_DATA_INFO_T));
                msg_head->subtype = MSG_ACK;
            }

            rsp_len = translate_packet(rsp, (unsigned char*)msg_head, msg_head->len);

            free(nvdata);
        }
    }while(0);

    ENG_LOG("%s: rsp_len : %d\n", __FUNCTION__, rsp_len);

    return rsp_len;
}

int eng_diag_mmicit_read(char *buf,int len, char *rsp, int rsplen)
{
    int ret = 0, count;
    int datalen, namelen;
    char* rspdata = NULL;
    char* pCur    = NULL;
    eng_str2int_table_sqlresult result;
    MSG_HEAD_T *pHead = (MSG_HEAD_T*)(buf + 1);

    ret = eng_sql_string2int_table_get(&result);
    if(ret) {
        ENG_LOG("%s: ENG read sqlite table failed\n", __FUNCTION__);
        memcpy(rsp, buf, len);// return a empty diag framer
        return len;
    }

    rspdata = malloc(rsplen - 2);
    if(NULL == rspdata){
        ENG_LOG("%s: Buffer malloc failed\n", __FUNCTION__);
        return 0;
    }

    memcpy(rspdata, (char*)pHead, sizeof(MSG_HEAD_T));
    pCur = rspdata + sizeof(MSG_HEAD_T);
    datalen = sizeof(MSG_HEAD_T);

    for(count = 0; count < result.count; count ++) {
        namelen   = strlen(result.table[count].name);
        ENG_LOG("%s: namelen: %d, name: %s\n", __FUNCTION__, namelen, result.table[count].name);
        *(pCur++) = namelen + 2;
        *(pCur++) = result.table[count].groupid;
        *(pCur++) = result.table[count].value;
        sprintf(pCur, "%s", result.table[count].name);
        pCur     += namelen;
        datalen  += (namelen + 3);
    }

    ENG_LOG("%s: datalen: %d\n", __FUNCTION__, datalen);

    ((MSG_HEAD_T*)rspdata)->len = datalen;
    rsplen = translate_packet(rsp, rspdata, datalen);

    free(rspdata);

    return rsplen;
}

int is_btwifi_addr_need_to_handle(char *buf,int len)
{
    int crc = 0;
    int recv_crc = 0;
    int cmd_mask;
    MSG_HEAD_T* msg_head = (MSG_HEAD_T*)(buf + 1);

    // Check CRC
    recv_crc = *(unsigned short*)&(buf[msg_head->len - sizeof(unsigned short) + 1]);
    crc = crc16(0,(unsigned char*)(msg_head + 1), msg_head->len - sizeof(MSG_HEAD_T) - sizeof(unsigned short));

    if(recv_crc != crc) {
        ENG_LOG("%s: CRC Error! recv_crc: %d, crc16: %d\n", __FUNCTION__, recv_crc, crc);
        return 0;// send to CP
    }

    if((msg_head->subtype & RW_MASK) == WRITE_MODE){
        ENG_LOG("%s: Write mode !\n", __FUNCTION__);
        if((msg_head->subtype & RM_VALID_CMD_MASK) == 0){
            ENG_LOG("%s: not valid cmd\n", __FUNCTION__);
            return 0;
        }
    }else{
        ENG_LOG("%s: Read mode !\n", __FUNCTION__);
    }

    if(0 != (cmd_mask = (msg_head->subtype & 0x7f))){
        ENG_LOG("%s: cmd_mask: %d, subtype: %d\n", __FUNCTION__, cmd_mask, msg_head->subtype);

        if((cmd_mask & DIAG_CMD_BTBIT) || (cmd_mask & DIAG_CMD_WIFIBIT)){
            ENG_LOG("%s: Get BT/WIFI Mac addr req !\n", __FUNCTION__);
            if((cmd_mask & (~(DIAG_CMD_BTBIT|DIAG_CMD_WIFIBIT)))){
                ENG_LOG("%s: Have other commands !\n", __FUNCTION__);
                return 2;
            }else{
                ENG_LOG("%s: No other commands !\n", __FUNCTION__);
                return 1;
            }
        }
    }

    return 0;
}

static void eng_diag_reboot(int reset)
{
    char name[64] = {0};

    switch(reset){
        case 1:
            strcpy(name, "cftreboot");
            break;
        case 2:
            strcpy(name, "recovery");
            break;
        default:
            return;
    }

    sync();
    __reboot(LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
            LINUX_REBOOT_CMD_RESTART2, name);

    return;
}

static int eng_diag_deep_sleep(char *buf,int len, char *rsp)
{
    char cmd[] = {"echo mem > /sys/power/state"};
    system(cmd);
    return 0;
}
