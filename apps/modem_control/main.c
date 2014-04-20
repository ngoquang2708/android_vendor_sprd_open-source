#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <cutils/sockets.h>
#include <pthread.h>
#include <utils/Log.h>
#include <errno.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <signal.h>
#include <hardware_legacy/power.h>
#include "cutils/properties.h"
#include "packet.h"

#define	MODEM_SOCKET_NAME	"modem_control"
#define	MAX_CLIENT_NUM		10
#define	MODEM_PATH		"/dev/vbpipe2"
#define	MODEM_STATE_PATH	"/sys/devices/platform/modem_interface/state"

#define	DLOADER_PATH		"/dev/dloader"
#define DATA_BUF_SIZE (128)
#define TEST_BUFFER_SIZE 300

#define  LTE_RELOAD_SVLTE_STR       "LTE Modem Reload,SVLTE"
#define  LTE_RELOAD_CSFB_STR         "LTE Modem Reload,_CSFB"

#define MODEM_POWEROFF_REQ          "Modem start poweroff"
#define MODEM_POWEROFF_ACK          "Modem poweroff ok"

#define  TD_MODEM_ALIVE_STR          "_TD Modem _Alive"
#define  LTE_MODEM_ALIVE_STR         "LTE Modem _Alive"
#define  MODEM_ALIVE_STR             "Modem Alive"
#define  LTE_MODEM_ASSERT_STR        "LTE Modem Assert"
#define  TD_MODEM_ASSERT_STR         "_TD Modem Assert"
#define  MODEM_ASSERT_STR            "Modem Assert"
#define  LTE_MODEM_RESET_STR         "LTE Modem _Reset"
#define  WTD_MODEM_RESET_STR         "WDG Modem _Reset"
#define  MODEM_RESET_STR              "Modem Reset"


#define MODEM_INTF_EVENT_ASSERT                 0
#define MODEM_INTF_EVENT_ALIVE                  1
#define MODEM_INTF_EVENT_CP_REQ_RESET           2
#define MODEM_INTF_EVENT_SHUTDOWN               4
#define MODEM_INTF_EVENT_FORCE_RESET            8



typedef enum _MOMDEM_STATE {
        MODEM_STA_SHUTDOWN,
        MODEM_STA_BOOT,
        MODEM_STA_INIT,
        MODEM_STA_ALIVE,
        MODEM_STA_ASSERT,
        MODEM_STA_BOOTCOMP,
        MODEM_STA_MAX
} MODEM_STA_E;

extern int modem_boot(void);
extern void  set_modem_capabilities( int flag);
extern void  set_modem_state(char * buf);
extern int  send_start_message(int fd,int size,unsigned long addr,int flag);
static char assert_info_buffer[TEST_BUFFER_SIZE]= {0};
static char log_data[DATA_BUF_SIZE];
static int  client_fd[MAX_CLIENT_NUM];
static MODEM_STA_E modem_state = MODEM_STA_SHUTDOWN;
static int flash_less_flag = 0xff;

#define MONITOR_APP	"/system/bin/sprd_monitor"
static int reset_modem_if_assert(void)
{
        char prop[PROPERTY_VALUE_MAX]= {0};
        property_get("persist.sys.sprd.modemreset", prop, "");
        MODEM_LOGD("reset_modem_if_assert modem reset: %s\n", prop);
        if(!strncmp(prop, "1", 2)) {
                return 1;
        }
        return 0;
}

static int poweron_by_charger(void)
{
        char charge_prop[PROPERTY_VALUE_MAX]= {0};
        property_get("ro.bootmode", charge_prop, "");

        MODEM_LOGD("charge_prop: %s\n", charge_prop);

        if(!strncmp(charge_prop, "charge", 6)) {
                return 1;
        }
        return 0;
}
/* helper function to get pid from process name */
static int get_task_pid(char *name)
{
        DIR *d;
        struct dirent *de;
        char cmdline[1024];

        d = opendir("/proc");
        if (d == 0) return -1;

        while ((de = readdir(d)) != 0) {
                if(isdigit(de->d_name[0])) {
                        int pid = atoi(de->d_name);
                        int fd, ret;
                        sprintf(cmdline, "/proc/%d/cmdline", pid);
                        fd = open(cmdline, O_RDONLY);
                        if (fd < 0) continue;
                        ret = read(fd, cmdline, 1023);
                        close(fd);
                        if (ret < 0) ret = 0;
                        cmdline[ret] = 0;
                        if (strcmp(name, cmdline) == 0) {
                                closedir(d);
                                return pid;
                        }
                }
        }
        closedir(d);
        return -1;
}
static void print_log_data(char *buf, int cnt)
{
        int i;

        if (cnt > DATA_BUF_SIZE)
                cnt = DATA_BUF_SIZE;

        MODEM_LOGD("received:\n");
        for(i = 0; i < cnt; i++) {
                MODEM_LOGD("%c ", buf[i]);
        }
        MODEM_LOGD("\n");
}
static int get_modem_state(char *buffer,size_t size)
{
        int modem_state_fd = 0;
        ssize_t numRead;
        modem_state_fd = open(MODEM_STATE_PATH, O_RDONLY);
        if(modem_state_fd < 0)
                return 0;
        numRead = read(modem_state_fd,buffer,size);
        close(modem_state_fd);
        if(numRead > 0) {
                MODEM_LOGD("get_modem_state modem_event = %s\n",buffer);
        } else {
                MODEM_LOGE("modem_state read MODEM_STATE_PATH ret:%d, %s\n", numRead, strerror(errno));
                return 0;
        }
        return numRead;
}

static void *modemd_listenaccept_thread(void *par)
{
        int sfd, n, i;
        char buffer[64] = {0};
        unsigned short *data = (unsigned short *)buffer;

        for(i=0; i<MAX_CLIENT_NUM; i++)
                client_fd[i]=-1;

        sfd = socket_local_server(MODEM_SOCKET_NAME,
                                  0,/*ANDROID_SOCKET_NAMESPACE_RESERVED,*/ SOCK_STREAM);
        if (sfd < 0) {
                MODEM_LOGE("modemctrl_listenaccept_thread: cannot create local socket server\n");
                exit(-1);
        }

        for(; ;) {

                MODEM_LOGD("modemctrl_listenaccept_thread: Waiting for new connect ...\n");
                if ( (n=accept(sfd,NULL,NULL)) == -1) {
                        MODEM_LOGE("engserver accept error\n");
                        continue;
                }

                MODEM_LOGD("modemctrl_listenaccept_thread: accept client n=%d\n",n);
                for(i=0; i<MAX_CLIENT_NUM; i++) {
                        if(client_fd[i]==-1) {
                                client_fd[i]=n;
                                MODEM_LOGE("modemctrl_listenaccept_thread: fill %d to client[%d]\n", n, i);
                                break;
                        }
                }
        }
}

static void *modemd_listen_reloader_thread(void *par)
{
        int i,ret;
        fd_set rfds;
        char prop[256]= {0};
        while(1) {
                for(i=0; i<MAX_CLIENT_NUM; i++) {
                        if(client_fd[i] > 0) {
                                MODEM_LOGD("client_fd[%d]=%d\n",i, client_fd[i]);
                                FD_ZERO(&rfds);
                                FD_SET(client_fd[i], &rfds);
                                select(client_fd[i] + 1, &rfds, NULL, NULL, NULL);
                                if (FD_ISSET(client_fd[i], &rfds)) {
                                        ret = read(client_fd[i], prop, 256);
                                        if(ret > 0)
                                                MODEM_LOGD("read %d bytes to client socket [%d] \n", ret, client_fd[i]);
                                        else {
                                                MODEM_LOGE("%s error: %s\n",__func__,strerror(errno));
                                                close(client_fd[i]);
                                                client_fd[i] = -1;
                                        }
                                        MODEM_LOGD("read %d bytes to client socket [%d] to  , prop = %s \n", ret, client_fd[i], prop);
                                        if(!strncmp(prop, LTE_RELOAD_SVLTE_STR, strlen(LTE_RELOAD_SVLTE_STR)) ) {
                                                set_modem_capabilities(1);
                                                set_modem_state("8");
                                        } else if (!strncmp(prop, LTE_RELOAD_CSFB_STR, strlen(LTE_RELOAD_CSFB_STR)) ) {
                                                set_modem_capabilities(2);
                                                set_modem_state("8");
                                        }
                                }
                        }
                }
        }
        return 0;
}
static int get_modem_assert_information(char *assert_info,int size)
{
        extern int open_uart_device(int mode, int speed);
        char ch, *buffer;
        int     read_len=0,ret=0;
        unsigned long timeout;

        if((assert_info == NULL) || (size == 0))
                return 0;

        int uart_fd = open_uart_device(1, 115200);

        if(uart_fd < 0) {
                MODEM_LOGE("open_uart_device failed \n");
                return 0;
        }
        ch = 'P';
        ret = write(uart_fd,&ch,1);

        buffer = assert_info;
        read_len = 0;
        timeout=0;
        do {
                ret = read(uart_fd,buffer,size);
                timeout++;
                if(ret > 0) {
                        MODEM_LOGD("read_len[%d] = %d \n",timeout,read_len);
                        size -= ret;
                        read_len += ret;
                        buffer += ret;
                }
                if(timeout > 100000)
                        break;
        } while(size >0);
        ch = 'O';
        write(uart_fd,&ch,1);
        close(uart_fd);
        return read_len;
}

void set_modem_assert_information(char *assert_info,int size)
{
        memset(assert_info_buffer, 0, TEST_BUFFER_SIZE);
        memcpy(assert_info_buffer, assert_info, size);
}

static int translate_modem_state_message(char *message)
{
        int modem_gpio_mask;
        sscanf(message, "%d", &modem_gpio_mask);
        return modem_gpio_mask;
}

void broadcast_modem_state(char *message,int size)
{
        int i,ret;
        for(i=0; i<MAX_CLIENT_NUM; i++) {
                if(client_fd[i] > 0) {
                        MODEM_LOGD("client_fd[%d]=%d\n",i, client_fd[i]);
                        ret = write(client_fd[i], message, size);
                        if(ret > 0) {
                                MODEM_LOGD("write %d bytes to client socket [%d] to inform modemd\n", ret, client_fd[i]);
                                MODEM_LOGD("inform string:[%s]\n", message);
                        } else {
                                MODEM_LOGE("%s error: %s\n",__func__,strerror(errno));
                                close(client_fd[i]);
                                client_fd[i] = -1;
                        }
                }
        }
}

static void process_modem_state_message(char *message,int size)
{
        int alive_status=0,reset_status = 0,force_reset = 0;
        int evt=0;
        int ret = 0;
        int first_alive = 0;

        evt = translate_modem_state_message(message);
        MODEM_LOGD("process_modem_state_message  message = %s,  ret = %d\n",message, evt);
        if(evt < 0)
                return;
        alive_status = evt & 0x1;
        reset_status = (evt >> 1) & 0x1;
        force_reset = evt & 0x8;
        // check flash less or not and first alive
        if(flash_less_flag == 0xff) {
                if(alive_status == 1) {
                        flash_less_flag = 0;
                        first_alive = 1; // first alive for nand
                } else
                        flash_less_flag = 1;
        } else if(flash_less_flag == 1) {
                if(alive_status == 1) {
                        flash_less_flag = 2;
                        first_alive = 1; // first alive for flash less
                }
        }
        MODEM_LOGD("alive=%d reset=%d force_reset=%dflash_less = %d, modem_state = %d \n",alive_status,reset_status,force_reset,flash_less_flag, modem_state);

        if(MODEM_INTF_EVENT_FORCE_RESET == evt) {
                int pid;

                pid = get_task_pid(MONITOR_APP);

                if(pid > 0) {
                        kill(pid, SIGUSR1);
                }
                MODEM_LOGD("modem_state2 = MODEM_STA_BOOT\n");
                modem_state = MODEM_STA_BOOT;
                return;
        }

        switch(modem_state) {
        case MODEM_STA_SHUTDOWN:
                break;
        case MODEM_STA_INIT: {
                if(MODEM_INTF_EVENT_ALIVE == evt) {

                        int pid;
                        modem_state = MODEM_STA_ALIVE;
                        MODEM_LOGD("modem_state0 = MODEM_STA_ALIVE\n");
                        pid = get_task_pid(MONITOR_APP);
                        if((pid > 0)&&(!first_alive)) {
                                kill(pid, SIGUSR2);
                                MODEM_LOGD("Send SIGUSR2 to MONITOR_APP\n");
                        }
                } else if(MODEM_INTF_EVENT_SHUTDOWN == evt) {

                        modem_state = MODEM_STA_BOOT;
                        MODEM_LOGD("modem_state1 = MODEM_STA_BOOT\n");
                }
        }
        break;
        case MODEM_STA_ASSERT:
                if(MODEM_INTF_EVENT_CP_REQ_RESET == evt) {

                        broadcast_modem_state(MODEM_RESET_STR,strlen(MODEM_RESET_STR));
                }
                break;
        case MODEM_STA_BOOTCOMP:
                if(MODEM_INTF_EVENT_ALIVE == evt) {
                        int pid;
                        modem_state = MODEM_STA_ALIVE;
                        MODEM_LOGD("modem_state5 = MODEM_STA_ALIVE\n");
                        broadcast_modem_state(MODEM_ALIVE_STR, strlen(MODEM_ALIVE_STR));
                        pid = get_task_pid(MONITOR_APP);
                        if((pid > 0)&&(!first_alive)) {
                                kill(pid, SIGUSR2);
                                MODEM_LOGD("Send SIGUSR2 to MONITOR_APP\n");
                        }
                } else if(MODEM_INTF_EVENT_CP_REQ_RESET == evt) {

                        broadcast_modem_state(MODEM_RESET_STR,strlen(MODEM_RESET_STR));
                }
                break;
        case MODEM_STA_ALIVE:
                if(MODEM_INTF_EVENT_ASSERT == evt) {
                        modem_state = MODEM_STA_ASSERT;
                        memset(assert_info_buffer,0,300);
                        ret = get_modem_assert_information(assert_info_buffer,256);
                        MODEM_LOGD("modem_state4 = MODEM_STA_ASSERT(%d)\n",ret);
                        MODEM_LOGD("modem_ctrl ASSERT INFO %s\n",assert_info_buffer);
                        if(ret > 0) {
                                broadcast_modem_state(assert_info_buffer,strlen(assert_info_buffer));
                        } else {
                                broadcast_modem_state(MODEM_ASSERT_STR,strlen(MODEM_ASSERT_STR));
                        }
                }
                if(MODEM_INTF_EVENT_CP_REQ_RESET == evt) {

                        broadcast_modem_state(MODEM_RESET_STR,strlen(MODEM_RESET_STR));
                }
                break;
        default:
                break;
        }


}
void signal_pipe_handler(int sig_num)
{
        MODEM_LOGD("receive signal SIGPIPE\n");
}
int main(int argc, char *argv[])
{
        int ret, i;
        char buf[DATA_BUF_SIZE]= {0};
        pthread_t t1;
        pthread_t t2;
        int priority;
        pid_t pid;
        struct sigaction act;

        int modem_state_fd = open(MODEM_STATE_PATH, O_RDONLY);

        MODEM_LOGD(">>>>>> start modem manager program ......\n");
        if(poweron_by_charger() == 1) {
                MODEM_LOGD(">>>>>> power on by charger,modem_reboot exits...\n");
                return 0;
        }
        if(modem_state_fd < 0) {
                MODEM_LOGE("!!! Open %s failed, modem_reboot exit\n",MODEM_STATE_PATH);
                return 0;
        }
        memset (&act, 0x00, sizeof(act));
        act.sa_handler = &signal_pipe_handler;
        act.sa_flags = SA_NODEFER;
        sigfillset(&act.sa_mask);   //block all signals when handler is running.
        ret = sigaction (SIGPIPE, &act, NULL);
        if (ret < 0) {
                perror("sigaction() failed!\n");
                exit(1);
        }

        pid = getpid();
        priority = getpriority(PRIO_PROCESS,pid);
        setpriority(PRIO_PROCESS,pid,-15);

        close(modem_state_fd);

        {
                extern int get_modem_images_info(void);
                extern void print_modem_image_info(void);
                get_modem_images_info();
                print_modem_image_info();
        }

        if(0 != pthread_create(&t1, NULL, (void*)modemd_listenaccept_thread, NULL)) {
                MODEM_LOGE(" modem_listenaccept_thread create error!\n");
        }

        if(0 != pthread_create(&t2, NULL, (void*)modemd_listen_reloader_thread, NULL)) {
                MODEM_LOGE(" modemd_listen_reloader_thread create error!\n");
        }

        modem_state = MODEM_STA_INIT;
        do {
                memset(buf,0,sizeof(buf));
                ret = get_modem_state(buf,sizeof(buf));
                if(ret > 0) {
                        process_modem_state_message(buf,ret);
                }
                if(modem_state == MODEM_STA_BOOT) {
                        acquire_wake_lock(PARTIAL_WAKE_LOCK, MODEM_SOCKET_NAME);
                        modem_boot();
                        release_wake_lock(MODEM_SOCKET_NAME);
                        sleep(1);
                        modem_state = MODEM_STA_BOOTCOMP;
                        continue;
                }
        } while(1);
        return 0;
}
