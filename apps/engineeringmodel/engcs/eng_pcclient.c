
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/resource.h>
#include "engopt.h"
#include "engat.h"
#include "engclient.h"
#include "eng_appclient.h"
#include "eng_pcclient.h"
#include "eng_sqlite.h"
#include "eng_diag.h"
#include "eng_cmd4linuxhdlr.h"
#include <termios.h>
#include <sys/ioctl.h>
#include "cutils/sockets.h"
#include "cutils/properties.h"
#include <private/android_filesystem_config.h>
#include "eng_at_trans.h"

//only support 2 sims now!!!!
#define MAX_CS_SIMS 2
#define ENG_RIL_SIM    "ril.sim1.absent"

#define NUM_ELEMS(x) (sizeof(x)/sizeof(x[0]))

static int pc_client_fd = -1;
static char eng_rspbuf[ENG_BUFFER_SIZE];
static char eng_atautobuf[ENG_BUFFER_SIZE];
static void *eng_atauto_thread(void *par);
static int cs_sim_fds[MAX_CS_SIMS];

static const char* TO_MULTI_SIM_CMDS[] = {
    "AT+CFUN=0",
    "AT+CFUN=1",
    "AT+CFUN=1，1",
    "AT+SFUN=2",
    "AT+SFUN=3",
    "AT+SFUN=4",
    "AT+SFUN=5"
};

int eng_get_simcount(void)
{
#if 0
    char simtype[8];
    int sim;

    memset(simtype, 0, sizeof(simtype));

    if ( 0 == property_get("persist.msms.phone_count", simtype, "1") )
    {
        ENG_LOG("%s: persist.msms.phone_count = %s", __FUNCTION__, simtype);
        sim = atoi(simtype);
    } else {
        sim =1;
    }

    return sim;
#endif
    return 1;
}

static struct eng_param cmdparam = {
    .califlag = 0,
    .engtest = 0,
    .cp_type = CP_TD,
    .connect_type = CONNECT_USB,
    .nativeflag = 0
};
extern void    disconnect_vbus_charger(void);
#if PC_DATA_FROM_AT_ROUTER
int create_pty(char *path)
{
    int fdm;
    char *slavename;
    char property[64] = {0};
    extern char *ptsname();
    fdm = open("/dev/ptmx", O_RDWR);
    grantpt(fdm);
    unlockpt(fdm);
    slavename = ptsname(fdm);
    unlink(path);
    ALOGD("[%d]%s --> %s\n", fdm, path, slavename);
    chmod(slavename, 0664);
    chown(slavename, AID_SYSTEM, AID_RADIO);
    sprintf(property, "%s  %s", slavename, path);

    /* set the property */
    property_set("sys.symlink.umts_router", property);
    property_set("ctl.stop", "smd_symlink");
    property_set("ctl.start", "smd_symlink");

    return fdm;
}
#endif

#ifdef CONFIG_ENG_UART_USB_AUTO
static int printk_fd = -1;

void eng_enable_kmsg(int enable)
{
    char log_level;

    printk_fd = open("/proc/sys/kernel/printk", O_RDWR);
    if (printk_fd == -1) {
        ALOGE("Open sysfs printk error. %s", strerror(errno));
        return;
    }

    log_level = (enable ? '8' : '0');

    write(printk_fd, &log_level, 1);

    close(printk_fd);
}


static int eng_pcclient_open_uart_device(void)
{
    struct termios termio;
    int uart_fd = -1;
    const char* uart_devname = "/dev/ttyS1";

    /* uart device */
    uart_fd = open(uart_devname, O_RDWR);
    if(uart_fd < 0){
        ALOGE("%s: open %s failed [%s]\n", __func__, uart_devname, strerror(errno));
        return -1;
    }

    /* set uart parameters: 115200n8n1 */
    tcgetattr(uart_fd, &termio);
    cfsetispeed(&termio, (speed_t)B115200);
    cfsetospeed(&termio, (speed_t)B115200);
    termio.c_cflag &= ~PARENB;
    termio.c_cflag &= ~CSIZE;
    termio.c_cflag |= CS8;
    termio.c_cflag &= ~CSTOPB;
    tcsetattr(uart_fd, TCSAFLUSH, &termio);

    dup2(uart_fd, 0);
    dup2(uart_fd, 1);
    dup2(uart_fd, 2);

    return uart_fd;
}


static int eng_pcclient_open_device()
{
    int usb_state_fd;
    int port_fd;

    int ret;
    char state[32] = {0};
    struct termios ser_settings;

    usb_state_fd = open(ENG_USBIN, O_RDONLY);
    if (usb_state_fd == -1) {
        ALOGE("open sysfs failed");
        exit(1);
    }
    memset(state, 0, sizeof(state));
    lseek(usb_state_fd, 0, SEEK_SET);

    ret = read(usb_state_fd, state, sizeof(state));
    if (ret > 0 && strncmp(state, ENG_USBCONNECTD, strlen(ENG_USBCONNECTD)) == 0) {
        ALOGD("USB connected, open gser device");
        port_fd = open(PC_GSER_DEV, O_RDWR);

        tcgetattr(port_fd, &ser_settings);
        cfmakeraw(&ser_settings);

        //tcsetattr(port_fd, TCSANOW, &ser_settings);
    }
    else{
        ALOGD("USB disconnected, open uart device");

        eng_enable_kmsg(0);

        port_fd = eng_pcclient_open_uart_device();
    }

    if(port_fd < 0){
        ENG_LOG("%s: open device fail [%s]\n",__FUNCTION__,  strerror(errno));
        return -1;
    }

    return port_fd;
}

#endif

static int eng_pcclient_init(char *name)
{
    int i;
    struct termios ser_settings;

#ifdef CONFIG_ENG_UART_USB_AUTO
    pc_client_fd = eng_pcclient_open_device();
#else

#if PC_DATA_FROM_AT_ROUTER
    pc_client_fd =  create_pty("/dev/umts_router");
#else
    pc_client_fd = open(PC_GSER_DEV, O_RDWR);
#endif
    if(pc_client_fd < 0){
        ENG_LOG("%s: open %s fail [%s]\n",__FUNCTION__, PC_GSER_DEV, strerror(errno));
        return -1;
    }

    tcgetattr(pc_client_fd, &ser_settings);
    cfmakeraw(&ser_settings);

    //tcsetattr(pc_client_fd, TCSANOW, &ser_settings);
#endif
    for ( i=0;i<eng_get_simcount();i++ ){
        cs_sim_fds[i] = -1;
        while((cs_sim_fds[i] = eng_at_open(name,i)) < 0){
            ENG_LOG(" %s: open server socket failed!, error[%d][%s]\n",\
                    __FUNCTION__, errno, strerror(errno));
            usleep(500*1000);
        }
    }
    return 0;
}

static int eng_atreq(int fd, char *buf, int length)
{
    int ret=0;
    int index=0;
    eng_cmd_type cmd_type=CMD_INVALID_TYPE;
    char cmd[ENG_BUFFER_SIZE];

    ENG_LOG("Call %s\n",__FUNCTION__);

    memset(cmd, 0, ENG_BUFFER_SIZE);

    if((index=eng_at2linux(buf)) < 0 || (cmd_type = eng_cmd_get_type(index)) == CMD_TO_APCP) {

        sprintf(cmd, "%d,%d,%s",ENG_AT_NOHANDLE_CMD, 1, buf);

        ENG_LOG("%s: cmd=%s\n",__FUNCTION__, cmd);
        ret = eng_at_write(fd, cmd, strlen(cmd));

        if(ret < 0) {
            ENG_LOG("%s: write cmd[%s] to server fail [%s]\n",__FUNCTION__, buf, strerror(errno));
            ret = -1;
        } else {
            ENG_LOG("%s: write cmd[%s] to server success\n",__FUNCTION__, buf);
        }

        ret = ENG_CMD4MODEM;

        if (cmd_type == CMD_TO_APCP) {
            ENG_LOG("%s: then the command %s will handled  at AP\n",__FUNCTION__, buf);
            memcpy(cmd, buf, length);
            memset(buf, 0, length);
            eng_linuxcmd_hdlr(index, cmd, buf);

            ret = ENG_CMD4LINUX;
        }

    } else {
        ENG_LOG("%s: Handle %s at Linux\n",__FUNCTION__, buf);
        memcpy(cmd, buf, length);
        memset(buf, 0, length);
        eng_linuxcmd_hdlr(index, cmd, buf);

        ret = ENG_CMD4LINUX;
    }

    return ret;
}

static int restart_gser(void)
{
    struct termios ser_settings;

    ENG_LOG("%s ERROR : %s\n", __FUNCTION__, strerror(errno));

    close(pc_client_fd);

    ENG_LOG("reopen serial\n");
    pc_client_fd = open(PC_GSER_DEV,O_RDWR);
    if(pc_client_fd < 0) {
        ENG_LOG("cannot open vendor serial\n");
        return -1;
    }

    tcgetattr(pc_client_fd, &ser_settings);
    cfmakeraw(&ser_settings);

    //tcsetattr(pc_client_fd, TCSANOW, &ser_settings);

    return 0;
}

static int eng_pc2clientbuf(char* databuf, char* readbuf,  int input_len, int* length_read_ptr)
{
    char * engbuf = readbuf;
    int i, length, ret = 0;
    int is_continue = 1;
    int buf_len = 0;
    int ret_val = 0;

    ENG_LOG("%s: Waitting cmd from PC, input_len %d \n", __func__,input_len);

    for(i=0; i < input_len; i++){
        if ( engbuf[i] == 0xd ){ //\r
            continue;
        }
        else if (engbuf[i]==0x1a){ // ^z
            databuf[buf_len]=engbuf[i];
            buf_len ++;
            break;
        }
        else if ( engbuf[i] == 0xa || buf_len >= ENG_BUFFER_SIZE){ //\n
            break;
        }
        else{
            databuf[buf_len]=engbuf[i];
            buf_len ++;
        }
    }

    if (i >= input_len) { //there isn't end character
        *length_read_ptr = input_len;
    }
    else {
        *length_read_ptr = i+1;
    }

    if (*length_read_ptr < 2)
    {
        ret =-1;
    }

#if 0
    for(i=0; i<buf_len; i++) {
        ENG_LOG("0x%x, ",databuf[i]);
    }
#endif

    return ret;
}

static int eng_modem2client(int fd, char * databuf, int length)
{
    int counter=0;

    ENG_LOG("%s: Waitting AT response from Server\n", __FUNCTION__);
    memset(databuf, 0, length);
    counter=eng_at_read(fd, databuf, length);
    ENG_LOG("%s[%d]:%s",__FUNCTION__, counter, databuf);
    return counter;
}

static int eng_modem2pc(int pc_client_fd, char *databuf, int length)
{
    write(pc_client_fd, databuf, length);
    return 0;
}

static int eng_linux2pc(int pc_client_fd, char *databuf)
{
    int len;

    ENG_LOG("%s: eng response = %s\n", __FUNCTION__, databuf);
    len = write(pc_client_fd, databuf, strlen(databuf));
    if (len <= 0) {
        restart_gser();
    }

    return 0;
}

#define VLOG_PRI  -20
static void set_vlog_priority(void)
{
    int inc = VLOG_PRI;
    int res = 0;

    errno = 0;
    res = nice(inc);
    if (res < 0){
        printf("cannot set vlog priority, res:%d ,%s\n", res,
                strerror(errno));
        return;
    }
    int pri = getpriority(PRIO_PROCESS, getpid());
    printf("now vlog priority is %d\n", pri);
    return;
}

static int eng_dispatch_simfd_counts(char* databuf)
{
    int count;
    unsigned int i;
    int is_multi=0;

    if ( 1 == eng_get_simcount())
    {
        count =1;
        return count;
    }

    for (i=0;i<NUM_ELEMS(TO_MULTI_SIM_CMDS);i++)
    {
        if (strcasestr(databuf,TO_MULTI_SIM_CMDS[i]) != NULL ){
            is_multi = 1;
            break;
        }
    }

    if (is_multi){
        count = MAX_CS_SIMS;
    } else {
        count = 1;
    }

    ENG_LOG("%s:count=%d",__func__,count);

    return count;
}

static int eng_cur_sim_fd(int index,int total)
{
    int fd;
    int n;
    char ril_sim[PROP_VALUE_MAX];

    if ( total>1 ){
        fd = cs_sim_fds[index];
    }
    else{
        memset(ril_sim, 0, sizeof(ril_sim));
        property_get(ENG_RIL_SIM, ril_sim, "");
        n = atoi(ril_sim);
        if ( 1==n ){
            fd = cs_sim_fds[1]; // send simcard 2
        } else {
            fd = cs_sim_fds[0];
        }
    }
    return fd;
}

static void eng_multicmds_modem2pc(int pc_client_fd,char*prev_buf,int plen,char *databuf, int dlen)
{
    //ENG_LOG("%s:prev=%s,databuf=%s",__func__,prev_buf,databuf);
    if (strcasestr(prev_buf,"OK") == NULL){
        write(pc_client_fd, prev_buf, plen);
    } else if (strcasestr(databuf,"OK") == NULL){
        eng_modem2pc(pc_client_fd, databuf, dlen);
    } else {
        eng_modem2pc(pc_client_fd, databuf, dlen);
    }
}

static void *eng_pcclient_hdlr(void *_param)
{
    char databuf[ENG_BUFFER_SIZE];
    char readbuf[ENG_BUFFER_SIZE];
    int resp_len;
    int length = 0;
    int length_read = 0;
    int offset_read  = 0;
    int fd, ret;
    int status;
    int i,total;
    char* prev_resp_buf=NULL;
    int prev_len = 0;

    ENG_LOG("%s: Run",__FUNCTION__);

    memset(databuf, 0, ENG_BUFFER_SIZE);

    for( ; ; ){

        ENG_LOG("%s: loop", __FUNCTION__);

        memset(readbuf, 0, ENG_BUFFER_SIZE);

        if (pc_client_fd<0)
        {
            restart_gser();
            continue;
        }

        length = read(pc_client_fd, readbuf, ENG_BUFFER_SIZE);
        if (length<=0)
        {
            restart_gser();
            continue;
        }

        ENG_LOG("%s ### data read length %d %s###", __FUNCTION__, length,readbuf);

        total = eng_dispatch_simfd_counts(readbuf);
        for ( i=0;i<total;i ++ )
        {
            offset_read  = 0;

            for(;(offset_read< length)&&(0 < length);)
            {
                length_read = 0;
                if(eng_pc2clientbuf(&databuf[offset_read], &readbuf[offset_read],
                            length - offset_read, &length_read) == -1) {
                    offset_read += length_read;
                    continue;
                }
                offset_read += length_read;

                ENG_LOG("%s ### data parse %d %d###", __FUNCTION__, offset_read,length_read);
                //write cmd from client to modem
                status = eng_atreq(eng_cur_sim_fd(i,total), databuf, ENG_BUFFER_SIZE);
                //write response from client to pc
                switch(status) {
                    case ENG_CMD4LINUX:
                        eng_linux2pc(pc_client_fd, databuf);
                        break;
                    case ENG_CMD4MODEM:
                        resp_len = eng_modem2client(eng_cur_sim_fd(i,total),databuf,ENG_BUFFER_SIZE);
                        if (total>1){
                            //ENG_LOG("total=%d,i=%d,prev=%s,databuf=%s",total,i,
                            //		prev_resp_buf,databuf);
                            if (i==0){
                                prev_len = resp_len;
                                prev_resp_buf = (char*)malloc(prev_len+1);
				if(NULL != prev_resp_buf){
                                    memset(prev_resp_buf,0,prev_len+1);
                                    memcpy(prev_resp_buf,databuf,prev_len);
				}
                            }
                            else{
                                if (prev_resp_buf){
                                    eng_multicmds_modem2pc(pc_client_fd,
                                            prev_resp_buf, prev_len, databuf, resp_len);
                                    free(prev_resp_buf);
                                    prev_resp_buf=NULL;
                                }
                            }
                        }else{
                            eng_modem2pc(pc_client_fd, databuf, resp_len);
                        }
                        break;
                }
                memset(databuf, 0, ENG_BUFFER_SIZE);
            }
        }
    }

    return NULL;
}

static int cs_sim1_fd;
int eng_get_csclient_fd(void)
{
    return cs_sim1_fd;
}

int eng_atcali_hdlr(char* buf)
{
    int index;
    ENG_LOG("%s: %s",__FUNCTION__, buf);
    if((index=eng_at2linux(buf))>=0) {
        memset(eng_rspbuf, 0, ENG_BUFFER_SIZE);
        eng_linuxcmd_hdlr(index, buf, eng_rspbuf);
        return 0;
    }
    return -1;
}

static void eng_atcali_thread(char *name)
{
    int fd, n, ret;
    fd_set readfds;

    ENG_LOG("%s",__FUNCTION__);

    while((cs_sim1_fd = eng_at_open(name,0)) < 0){
        ENG_LOG("%s: open server socket failed!, error[%d][%s]\n",\
                __FUNCTION__, errno, strerror(errno));
        usleep(500*1000);
    }

    eng_atauto_thread(name);
}

static void *eng_atauto_thread(void *par)
{
    int fd, usb_fd, n, j, ret;
    int usb_status=0;
    char buffer[32];
    fd_set readfds;
    char modem_dev[20];

    if (strcmp((char*)par,"engw") == 0){
        sprintf(modem_dev,"%s",ENG_ATAUTO_DEVW);
    } else {
        sprintf(modem_dev,"%s",ENG_ATAUTO_DEVT);

    }

    while((fd=open(modem_dev, O_RDWR|O_NONBLOCK)) < 0){
        ENG_LOG("%s: open %s failed!, error[%d][%s]\n",\
                __func__,  modem_dev, errno, strerror(errno));
        usleep(500*1000);
    }

    usb_fd = open(ENG_USBIN, O_RDONLY);
    ENG_LOG("%s: Thread Start\n",__FUNCTION__);

    for( ; ; ) {

        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);

        n = select(fd+1, &readfds, NULL, NULL, NULL);
        ENG_LOG("%s: At Auto Report\n",__func__);
        if(n > 0) {
            memset(eng_atautobuf, 0, ENG_BUFFER_SIZE);
            n = read(fd, eng_atautobuf, ENG_BUFFER_SIZE);
            ENG_LOG("%s: eng_atautobuf=%s; usb_fd=%d\n",__func__, eng_atautobuf, usb_fd);
            if (n > 0) {
                j = eng_atcali_hdlr(eng_atautobuf);
                ENG_LOG("%s: j=%d",__FUNCTION__,j);
                if(j==0) {
                    ENG_LOG("%s: Handle Auto AT",__FUNCTION__);
                } else {
                    if(usb_fd>0) {
                        memset(buffer, 0, sizeof(buffer));
                        lseek(usb_fd, 0, SEEK_SET);
                        ret = read(usb_fd, buffer, sizeof(buffer));
                        ENG_LOG("%s: %s",__func__, buffer);
                        if(strncmp(buffer, ENG_USBCONNECTD, strlen(ENG_USBCONNECTD))==0)
                            usb_status = 1;
                    }
                    ENG_LOG("%s: usb_status=%d\n",__func__, usb_status);
                    if(usb_status > 0) {
                        ENG_LOG("%s: write at auto report to PC\n",__func__);
                        write(pc_client_fd, eng_atautobuf, n);
                        usb_status = 0;
                    }
                }
            }
        } else {
            usleep(500*1000);
            ENG_LOG("%s: select error %d\n",__func__, n);
        }
    }

    if(usb_fd > 0)
        close(usb_fd);

    if(fd > 0)
        close(fd);

    return NULL;
}


#define MODEM_SOCKET_NAME	"modemd"
#define MODEM_SOCKET_BUFFER_SIZE	128
static void *eng_modemreset_thread(void *par)
{
    int pipe_fd = -1;
    int soc_fd, n, ret, status;
    char cmdrst[2]={'z',0x0a};
    char modemrst_property[PROP_VALUE_MAX];
    char buffer[MODEM_SOCKET_BUFFER_SIZE];

    memset(modemrst_property, 0, sizeof(modemrst_property));
    property_get(ENG_MODEMRESET_PROPERTY, modemrst_property, "");
    n = atoi(modemrst_property);
    ALOGD("%s: %s is %s, n=%d\n",__func__, ENG_MODEMRESET_PROPERTY, modemrst_property,n);

    if(n!=1) {
        ALOGD("%s: Modem Won't Reset after assert\n",__func__);
        return NULL;
    }

    soc_fd = socket_local_client( MODEM_SOCKET_NAME,
            ANDROID_SOCKET_NAMESPACE_ABSTRACT, SOCK_STREAM);

    while(soc_fd < 0) {
        //ALOGD("%s: Unable bind server %s, waiting...\n",__func__, MODEM_SOCKET_NAME);
        usleep(10*1000);
        soc_fd = socket_local_client( MODEM_SOCKET_NAME,
                ANDROID_SOCKET_NAMESPACE_ABSTRACT, SOCK_STREAM);
    }

    for(;;) {
        memset(buffer, 0, MODEM_SOCKET_BUFFER_SIZE);
        ALOGD("%s: waiting for server %s\n",__func__, MODEM_SOCKET_NAME);
        usleep(10*1000);
        n = read(soc_fd, buffer, MODEM_SOCKET_BUFFER_SIZE);
        ALOGD("%s: get %d bytes %s\n", __func__, n, buffer);
        if(n>0) {
            if(strstr(buffer, "Assert") != NULL) {
                if (pipe_fd < 0) {
                    pipe_fd = open("/dev/vbpipe0",O_WRONLY);
                    if(pipe_fd < 0) {
                        ALOGD("%s: cannot open vbpipe0\n",__func__);
                        close(soc_fd);
                        return NULL;
                    }
                }
write_again:
                ALOGD("%s, fd=%d, pipe_fd=%d\n",__func__, soc_fd, pipe_fd);
                n = write(pipe_fd, cmdrst, 2);
                ALOGD("%s: write vbpipe %d bytes RESET Modem\n",__func__, n);
                if (n < 0) {
                    if (errno == EPIPE) {
                        ALOGD("peer side of vbpipe is down, reopen it");
                        close(pipe_fd);
                        sleep(10);
                        pipe_fd = open("/dev/vbpipe0",O_WRONLY);
                        if(pipe_fd < 0) {
                            ALOGD("%s: cannot open vbpipe0\n",__func__);
                            close(soc_fd);
                            return NULL;
                        }
                    }
                    ALOGD("num write %d is lower than 0\n", n);
                    sleep(1);
                    goto write_again;
                }
            }
        }
    }

    close(soc_fd);
    close(pipe_fd);
}

void eng_check_factorymode_fornand(void)
{
    int ret;
    int fd;
    int status = eng_sql_string2int_get(ENG_TESTMODE);
    char status_buf[8];
    char config_property[PROP_VALUE_MAX];
    char usb_config_value[PROP_VALUE_MAX];
    int i;

#ifdef USE_BOOT_AT_DIAG
    ENG_LOG("%s: status=%x\n",__func__, status);
    property_get("persist.sys.usb.config", config_property, "");
    if((status==1)||(status == ENG_SQLSTR2INT_ERR)) {
        fd=open(ENG_FACOTRYMODE_FILE, O_RDWR|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR);
        if(fd >= 0)
            close(fd);
        /*
           if (strstr(config_property, "adb")) {
           property_set("sys.usb.config","adb,vser,gser");
           property_set("persist.sys.usb.config","mass_storage,adb,vser,gser");
           } else {
           property_set("sys.usb.config","vser,gser");
           property_set("persist.sys.usb.config","vser,gser");
           }
         */
        for(i = 0;i <5;i++){ //try 5 count
            property_get("sys.usb.config",usb_config_value,"not_find");
            if(strcmp(usb_config_value,"not_find") == 0){
                usleep(200*1000);
                ALOGD("%s: can not find sys.usb.config\n",__FUNCTION__);
                continue;
            }else{
                property_set("sys.usb.config","mass_storage,adb,vser,gser");
                ALOGD("%s: set usb property mass_storage,adb,vser,gser\n",__FUNCTION__);
                break;
            }
        }
        if(i >=5){
            property_set("sys.usb.config","mass_storage,adb,vser,gser");
            ALOGD("%s: time out for init.rc set system.usb.config ,we set it as default\n",__FUNCTION__);
        }
        ENG_LOG("%s: zds set sys.usb.config!!!\n",__func__);
    } else if (status == 0) {
        /*if (strstr(config_property, "vser,gser")) {
          if (strstr(config_property, "adb")) {
          property_set("sys.usb.config","adb");
          property_set("persist.sys.usb.config","adb");
          } else {
          property_set("sys.usb.config","");
          property_set("persist.sys.usb.config","");
          }
          }*/
        remove(ENG_FACOTRYMODE_FILE);
    } else {
        remove(ENG_FACOTRYMODE_FILE);
    }
#endif

    fd=open(ENG_FACOTRYSYNC_FILE, O_RDWR|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR);
    if(fd >= 0)
        close(fd);
}


void eng_check_factorymode_formmc(void)
{
    int ret;
    int fd;
    int status = eng_sql_string2int_get(ENG_TESTMODE);
    char status_buf[PROP_VALUE_MAX];
    char usb_config_value[PROP_VALUE_MAX];
    int i;

    do {
        usleep(100*1000);
        memset(status_buf, 0, sizeof(status_buf));
        property_get(RAWDATA_PROPERTY, status_buf, "");
        ret = atoi(status_buf);
        ALOGD("%s: %s is %s, n=%d\n",__FUNCTION__, RAWDATA_PROPERTY, status_buf,ret);
    }while(ret!=1);

    fd=open(ENG_FACOTRYMODE_FILE, O_RDWR|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR);

    ALOGD("%s: fd=%d, status=%x\n",__FUNCTION__, fd, status);

    if(fd >= 0) {
        if((status==1)||(status == ENG_SQLSTR2INT_ERR)) {
            sprintf(status_buf, "%s", "1");
            for(i = 0;i <5;i++){ //try 5 count
                property_get("sys.usb.config",usb_config_value,"not_find");
                if(strcmp(usb_config_value,"not_find") == 0){
                    usleep(200*1000);
                    ALOGD("%s: can not find sys.usb.config\n",__FUNCTION__);
                    continue;
                }else{
                    property_set("sys.usb.config","mass_storage,adb,vser,gser");
                    ALOGD("%s: set usb property mass_storage,adb,vser,gser\n",__FUNCTION__);
                    break;
                }
            }
            if(i >=5){
                property_set("sys.usb.config","mass_storage,adb,vser,gser");
                ALOGD("%s: time out for init.rc set system.usb.config ,we set it as default\n",__FUNCTION__);
            }

        } else if (status == 0) {
            sprintf(status_buf, "%s", "0");
        } else {
            sprintf(status_buf, "%s", "0");
        }

        ret = write(fd, status_buf, strlen(status_buf)+1);

        ALOGD("%s: write %d bytes to %s",__FUNCTION__, ret, ENG_FACOTRYMODE_FILE);

        close(fd);
    }

    fd=open(ENG_FACOTRYSYNC_FILE, O_RDWR|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR);
    if(fd >= 0)
        close(fd);
}

void eng_ctpcali(void)
{
    int fd;
    size_t ret;
    char cali_cmd[]="1";
    char cali_note[]="/sys/devices/platform/sc8810-i2c.2/i2c-2/2-005c/calibrate";

    fd = open(cali_note, O_RDWR);

    ALOGD("%s: fd=%d",__FUNCTION__, fd);
    if(fd >= 0) {
        ret = write(fd, cali_cmd, strlen(cali_cmd));
        ALOGD("%s: ret=%d",__FUNCTION__, ret);
        if(ret == strlen(cali_cmd))
            ALOGD("%s: Success!",__FUNCTION__);
        else
            ALOGD("%s: Fail!",__FUNCTION__);

        close(fd);
    }
}

/* Parse one parameter which is before a special char for string.
 * buf:[IN], string data to be parsed.
 * gap:[IN], char, get value before this charater.
 * value:[OUT] parameter value
 * return length of parameter
 */
static int cali_parse_one_para(char * buf, char gap, int* value)
{
    int len = 0;
    char *ch = NULL;
    char str[10] = {0};

    if(buf != NULL && value  != NULL){
        ch = strchr(buf, gap);
        if(ch != NULL){
            len = ch - buf ;
            strncpy(str, buf, len);
            *value = atoi(str);
        }
    }
    return len;
}

static int eng_parse_cmdline(struct eng_param * cmdvalue)
{
    int fd = 0;
    char cmdline[ENG_CMDLINE_LEN] = {0};
    char *str = NULL;
    int mode =  0;
    int freq = 0;
    int device = 0;
    int len = -1;

    if(cmdvalue == NULL)
        return -1;

    fd = open("/proc/cmdline", O_RDONLY);
    if (fd >= 0) {
        if (read(fd, cmdline, sizeof(cmdline)) > 0){
            ALOGD("eng_pcclient: cmdline %s\n",cmdline);
            /*calibration*/
            str = strstr(cmdline, "calibration");
            if ( str  != NULL){
                cmdvalue->califlag = 1;
		disconnect_vbus_charger();
                /*calibration= mode,freq, device. Example: calibration=8,10096,146*/
                str = strchr(str, '=');
                if(str != NULL){
                    str++;
                    /*get calibration mode*/
                    len = cali_parse_one_para(str, ',', &mode);
                    if(len > 0){
                        str = str + len +1;
                        /*get calibration freq*/
                        len = cali_parse_one_para(str, ',', &freq);
                        /*get calibration device*/
                        str = str + len +1;
                        len = cali_parse_one_para(str, ' ', &device);
                    }
                    switch(mode){
                        case 1:
                        case 5:
                        case 7:
                        case 8:
                            cmdvalue->cp_type = CP_TD;
                            break;
                        case 11:
                        case 12:
                        case 14:
                        case 15:
                            cmdvalue->cp_type = CP_WCDMA;
                            break;
                        default:
                            break;
                    }

                    /*Device[4:6] : device that AP uses;  0: UART 1:USB  2:SPIPE*/
                    cmdvalue->connect_type = (device >> 4) & 0x3;

                    if(device >>7)
                        cmdvalue->nativeflag = 1;
                    else
                        cmdvalue->nativeflag = 0;

                    ALOGD("eng_pcclient: cp_type=%d, connent_type(AP) =%d, is_native=%d\n",
                            cmdvalue->cp_type, cmdvalue->connect_type, cmdvalue->nativeflag );
                }
            }else{
                /*if not in calibration mode, use default */
                cmdvalue->cp_type = CP_TD;
                cmdvalue->connect_type = CONNECT_USB;
            }
            /*engtest*/
            if(strstr(cmdline,"engtest") != NULL)
                cmdvalue->engtest = 1;
        }
        close(fd);
    }
    return 0;
}

int main (int argc, char** argv)
{
    int fd, rc, califlag=0;
    int engtest=0;
    char cmdline[ENG_CMDLINE_LEN];
    eng_thread_t t1,t2, t3,t4, t5;
    int opt;
    int type;
    char name[10];
#if 0
    int index;
    index = eng_sql_string2int_get("index");
    if(index == ENG_SQLSTR2INT_ERR){
        index = 0;
    } else {
        index++;
    }
    eng_sql_string2int_set("index", index);
    memset(cmdline, 0, ENG_CMDLINE_LEN);
    sprintf(cmdline, "logcat > /data/eng_%d.log &", index);
    system(cmdline);

#endif

    while ( -1 != (opt = getopt(argc, argv, "t:"))) {
        switch (opt) {
            case 't':
                memset(name,0,10);
                type = atoi(optarg);
                if (type){
                    strcpy(name,"engtd");
                } else {
                    strcpy(name,"engw");
                }
                break;
            default:
                exit(EXIT_FAILURE);
        }
    }

    ALOGD("engpcclient name =%s",name);

    eng_sqlite_create();

    eng_parse_cmdline(&cmdparam);
    ENG_LOG("eng_pcclient califlag=%d, engtest=%d\n", cmdparam.califlag, cmdparam.engtest);

    if(cmdparam.engtest == 1) {
        eng_ctpcali();
    }

    if(cmdparam.califlag == 1 && cmdparam.nativeflag == 0){ //at handler in calibration mode
        eng_atcali_thread(name);
        return 0;
    }

#ifdef CONFIG_EMMC
    eng_check_factorymode_formmc();
#else
    eng_check_factorymode_fornand();
#endif

    set_vlog_priority();

    ENG_LOG("vlog thread start0");

    if (0 != eng_thread_create( &t1, eng_vlog_thread, &cmdparam)){
        ENG_LOG("vlog thread start error");
    }

    ENG_LOG("vlog thread start1");
    if (0 != eng_thread_create( &t2, eng_vdiag_thread, &cmdparam)){
        ENG_LOG("vdiag thread start error");
    }

    //modem reset move to modemd
    //ENG_LOG("vlog thread start2");
    //if (0 != eng_thread_create( &t3, eng_modemreset_thread, NULL)){
    //	ENG_LOG("vdiag thread start error");
    //}

    ENG_LOG("vlog thread start3");
    if (0 != eng_thread_create( &t4, eng_sd_log, NULL)){
        ENG_LOG("sd log thread start error");
    }

    ENG_LOG("at transparent transmission thread start");
    if (0 != eng_at_pcmodem()){
        ENG_LOG("at trans-thread start error");
    }

    ENG_LOG("vlog thread start4");

    if(cmdparam.califlag  != 1  || cmdparam.nativeflag  != 1)
    {
        rc = eng_pcclient_init(name);

        if(rc == -1) {
            ENG_LOG("%s: init fail, exit\n",__func__);
            return -1;
        }

        if (0 != eng_thread_create( &t5, eng_atauto_thread, name)){
            ENG_LOG("atauto thread start error");
        }

        eng_pcclient_hdlr(NULL);
    }else{
        /*calibration mode for native (AP dirrectily  communicates with PC tool)
          keep eng_pcclient thread alive*/

        ENG_LOG(" engpcclient wihle ");
        while(1){
            sleep(10000);
        }
    }

    return 0;
}
