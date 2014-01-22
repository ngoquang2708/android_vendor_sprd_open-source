#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <termios.h>
#include <sys/ioctl.h>
#include "cutils/sockets.h"
#include "cutils/properties.h"
#include <private/android_filesystem_config.h>
#include "eng_pcclient.h"
#include "eng_diag.h"
#include "engopt.h"
#include "eng_at.h"
#include "eng_sqlite.h"

#define VLOG_PRI  -20
#define SYS_CLASS_ANDUSB_ENABLE "/sys/class/android_usb/android0/enable"

extern void	disconnect_vbus_charger(void);

// current run mode: TD or W
int g_run_mode = ENG_RUN_TYPE_TD;

// devices' at nodes for PC
char* s_at_ser_path = "/dev/ttyGS0";

// devices' diag nodes for PC
char* s_connect_ser_path[] = {
    "/dev/ttyS1", //uart
    "/dev/vser", //usb
    NULL
};

// devices' nodes for cp
char* s_cp_pipe[] = {
    "/dev/slog_td", //cp_td : slog_td
    "/dev/slog_w", //cp_w
    "/dev/slog_wcn", //cp_btwifi
    NULL
};

static struct eng_param cmdparam = {
    .califlag = 0,
    .engtest = 0,
    .cp_type = CP_TD,
    .connect_type = CONNECT_USB,
    .nativeflag = 0
};

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

void eng_check_factorymode(int final)
{
    int ret;
    int fd;
    int status = eng_sql_string2int_get(ENG_TESTMODE);
    char status_buf[8];
    char modem_diag_value[PROPERTY_VALUE_MAX];
    char usb_config_value[PROPERTY_VALUE_MAX];
    char gser_config[] = {",gser"};
    char build_type[PROPERTY_VALUE_MAX];
    int usb_diag_set = 0;
    int i;
#ifdef USE_BOOT_AT_DIAG
    fd=open(ENG_FACOTRYMODE_FILE, O_RDWR|O_CREAT|O_TRUNC, 0660);

    if(fd >= 0){
        ENG_LOG("%s: status=%x\n",__func__, status);
        chmod(ENG_FACOTRYMODE_FILE, 0660);
        property_get("ro.build.type",build_type,"not_find");
        ENG_LOG("%s: build_type: %s", __FUNCTION__, build_type);
        property_get("persist.sys.modem.diag",modem_diag_value,"not_find");
        ENG_LOG("%s: modem_diag_value: %s\n", __FUNCTION__, modem_diag_value);
        if((status==1)||(status == ENG_SQLSTR2INT_ERR)) {
            sprintf(status_buf, "%s", "1");
            if(strcmp(modem_diag_value,",none") == 0) {
                usb_diag_set = 1;
            }
        }else {
            sprintf(status_buf, "%s", "0");
            if(strcmp(modem_diag_value,",none") != 0) {
                usb_diag_set = 1;
                memcpy(gser_config, ",none", 5);
            }
        }

        if(usb_diag_set && !final && 0 == strcmp(build_type, "userdebug")){
            do{
                property_get("sys.usb.config",usb_config_value,"not_find");
                if(strcmp(usb_config_value,"not_find") == 0){
                    usleep(200*1000);
                    ENG_LOG("%s: can not find sys.usb.config\n",__FUNCTION__);
                    continue;
                }else{
                    property_set("persist.sys.modem.diag", gser_config);
                    ENG_LOG("%s: set usb property mass_storage,adb,vser,gser\n",__FUNCTION__);
                    break;
                }
            }while(1);
        }
        ret = write(fd, status_buf, strlen(status_buf)+1);
        ENG_LOG("%s: write %d bytes to %s",__FUNCTION__, ret, ENG_FACOTRYMODE_FILE);

        close(fd);
    }else{
        ENG_LOG("%s: fd: %d, status: %d\n", __FUNCTION__, fd, status);
    }
#endif

    fd=open(ENG_FACOTRYSYNC_FILE, O_RDWR|O_CREAT|O_TRUNC, 0660);
    if(fd >= 0)
        close(fd);
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

    ENG_LOG("eng_pcclient califlag=%d \n", cmdparam.califlag);

    return 0;
}

static void eng_usb_enable(void)
{
    int fd  = -1;
    int ret = 0;

    fd = open(SYS_CLASS_ANDUSB_ENABLE, O_WRONLY);
    if(fd >= 0){
        ret = write(fd, "1", 1);
        ENG_LOG("%s: Write sys class androidusb enable file: %d\n", __FUNCTION__, ret);
        close(fd);
    }else{
        ENG_LOG("%s: Open sys class androidusb enable file failed!\n", __FUNCTION__);
    }
}

int main (int argc, char** argv)
{
    static char atPath[ENG_DEV_PATH_LEN];
    static char diagPath[ENG_DEV_PATH_LEN];
    char cmdline[ENG_CMDLINE_LEN];
    int opt;
    int type;
    int run_type = ENG_RUN_TYPE_TD;
    eng_thread_t t1,t2;

    while ( -1 != (opt = getopt(argc, argv, "t:a:d:"))) {
        switch (opt) {
            case 't':
                type = atoi(optarg);
                switch(type) {
                    case 0:
                        run_type = ENG_RUN_TYPE_WCDMA; // W Mode
                        break;
                    case 1:
                        run_type = ENG_RUN_TYPE_TD; // TD Mode
                        break;
                    case 2:
                        run_type = ENG_RUN_TYPE_BTWIFI; // BT WIFI Mode
                        break;
                    default:
                        ENG_LOG("engpcclient error run type: %d\n", run_type);
                        return 0;
                }
                break;
            case 'a': // AT port path
                strcpy(atPath, optarg);
                s_at_ser_path = atPath;
                break;
            case 'd': // Diag port path
                strcpy(diagPath, optarg);
                s_connect_ser_path[CONNECT_USB] = diagPath;
                break;
            default:
                exit(EXIT_FAILURE);
        }
    }

    ENG_LOG("engpcclient runtype:%d, atPath:%s, diagPath:%s", run_type,
            s_at_ser_path, s_connect_ser_path[CONNECT_USB]);

    // Remember the run type for at channel's choice.
    g_run_mode = run_type;

    // Create the sqlite database for factory mode.
    if(ENG_RUN_TYPE_BTWIFI != run_type){
        eng_sqlite_create();
    }

    // Get the status of calibration mode & device type.
    eng_parse_cmdline(&cmdparam);

    if(cmdparam.califlag != 1){
        cmdparam.cp_type = run_type;
        // Check factory mode and switch device mode.
        if(ENG_RUN_TYPE_BTWIFI != run_type){
            eng_check_factorymode(0);
        }
    }else{
        // Enable usb enum
        eng_usb_enable();
    }

    set_vlog_priority();

    // Create vlog thread for reading diag data from modem and send it to PC.
    if (0 != eng_thread_create( &t1, eng_vlog_thread, &cmdparam)){
        ENG_LOG("vlog thread start error");
    }

    // Create vdiag thread for reading diag data from PC, some data will be
    // processed by ENG/AP, and some will be pass to modem transparently.
    if (0 != eng_thread_create( &t2, eng_vdiag_thread, &cmdparam)){
        ENG_LOG("vdiag thread start error");
    }

    if(cmdparam.califlag != 1 || cmdparam.nativeflag != 1){
        eng_at_pcmodem(run_type);
    }

    while(1){
        sleep(10000);
    }

    return 0;
}
