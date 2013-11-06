#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/time.h>
#include "engopt.h"
#include "cutils/properties.h"

#define ENG_CARDLOG_PROPERTY	"persist.sys.cardlog"
#define DATA_BUF_SIZE (4096 * 64)
static char log_data[DATA_BUF_SIZE];
static int vser_fd = 0;

int is_sdcard_exist=1;
int pipe_fd;
extern int g_ass_start;

static int s_connect_type = 0;
static char* s_connect_ser_path[]={
    "/dev/ttyS1", //uart
    "/dev/vser", //usb
    NULL
};
static char* s_cp_pipe[]={
    "/dev/vbpipe0", //cp_td : slog_td
    "/dev/slog_w", //cp_w
    NULL
};

static void dump_mem_len_print(int r_cnt, int* dumplen)
{
    unsigned int head, len, tail;

    if (r_cnt == 12) {
        head = (log_data[3] << 24)|(log_data[2] << 16)|(log_data[1] << 8)|log_data[0];
        len  = (log_data[7] << 24)|(log_data[6] << 16)|(log_data[5] << 8)|log_data[4];
        tail = (log_data[11] << 24)|(log_data[10] << 16)|(log_data[9] << 8)|log_data[8];

        ENG_LOG("eng_vlog: get 12 bytes, let's check if dump finished.\n");

        if(tail == (len^head)) {
            ENG_LOG("eng_vlog: cp dump memory len: %d, ap dump memory len: %d\n", len, *dumplen);
            *dumplen  = 0;
            g_ass_start = 0;
        }
    }

    if (g_ass_start) {
        *dumplen += r_cnt;
    }
}

int get_vser_fd(void)
{
    return vser_fd;
}

int restart_vser(void)
{
    ENG_LOG("eng_vlog close usb serial:%d\n", vser_fd);
    close(vser_fd);

    vser_fd = open(s_connect_ser_path[s_connect_type],O_WRONLY);
    if(vser_fd < 0) {
        ALOGE("eng_vlog cannot open general serial\n");
        return 0;
    }
    ENG_LOG("eng_vlog reopen usb serial:%d\n", vser_fd);
    return 0;
}

#define MAX_OPEN_TIMES  10
//int main(int argc, char **argv)
void *eng_vlog_thread(void *x)
{
    int ser_fd;
    int sdcard_fd;
    int card_log_fd = -1;
    int r_cnt, w_cnt;
    int res,n;
    char cardlog_property[PROP_VALUE_MAX];
    char log_name[40];
    time_t now;
    int wait_cnt = 0;
    struct tm *timenow;
    struct eng_param * param = (struct eng_param *)x;
    int i = 0;
    int dumpmemlen = 0;

    ENG_LOG("eng_vlog  start\n");

    if(param == NULL){
        ALOGE("eng_vlog invalid input\n");
        return NULL;
    }
    /*open usb/uart*/
    ENG_LOG("eng_vlog  open  serial...\n");
    s_connect_type = param->connect_type;
    ser_fd = open(s_connect_ser_path[s_connect_type], O_WRONLY);
    if(ser_fd < 0) {
        ALOGE("eng_vlog cannot open general serial:%s\n",strerror(errno));
        return NULL;
    }
    vser_fd = ser_fd;


    ENG_LOG("eng_vlog open vitual pipe...\n");
    /*open vbpipe/spipe*/
    do{
        pipe_fd = open(s_cp_pipe[param->cp_type], O_RDONLY);
        if(pipe_fd < 0) {
            ALOGE("eng_vlog cannot open %s, times:%d, %s\n", s_cp_pipe[param->cp_type],wait_cnt,strerror(errno));

            sleep(5);
        }
    }while(pipe_fd < 0);

    sdcard_fd = open("/dev/block/mmcblk0",O_RDONLY);
    if ( sdcard_fd < 0 ) {
        is_sdcard_exist = 0;
        ENG_LOG("eng_vlog No sd card!!!");
    }else{
        is_sdcard_exist = 1;
    }
    if(sdcard_fd >= 0){
        close(sdcard_fd);
    }

    ENG_LOG("eng_vlog put log data from pipe to serial\n");
    while(1) {
        if ( is_sdcard_exist ) {
            memset(cardlog_property, 0, sizeof(cardlog_property));
            property_get(ENG_CARDLOG_PROPERTY, cardlog_property, "");
            n = atoi(cardlog_property);

            if ( 1==n ){
                sleep(1);
                continue;
            }
        }

        r_cnt = read(pipe_fd, log_data, DATA_BUF_SIZE);
        if (r_cnt < 0) {
            ENG_LOG("eng_vlog read no log data : r_cnt=%d, %s\n",  r_cnt, strerror(errno));
            continue;
        }

        // printf dump memory len 
        dump_mem_len_print(r_cnt, &dumpmemlen);

        if(r_cnt > 8192) {
            ENG_LOG("eng_vlog: read from modem %d\n", r_cnt);
        }

        w_cnt = 0;
        if(((r_cnt % 64 )==0) && r_cnt){
            if(param->califlag  && param->connect_type == CONNECT_USB){
                w_cnt = write(ser_fd, log_data, r_cnt-32);
                if(w_cnt < 0)
                    goto write_fail;
            }
        } 
        w_cnt = write(ser_fd, log_data+w_cnt, r_cnt-w_cnt);
        if (w_cnt < 0) {
write_fail:		ENG_LOG("eng_vlog no log data write:%d ,%s\n", w_cnt, strerror(errno));
                close(ser_fd);

                ser_fd = open(s_connect_ser_path[s_connect_type], O_WRONLY);
                if(ser_fd < 0) {
                    ALOGE("eng_vlog cannot open general serial\n");
                    close(pipe_fd);
                    return NULL;
                }
                vser_fd = ser_fd;
                ENG_LOG("eng_vlog reopen usb serial:%d\n", ser_fd);
        }

        if(w_cnt > 8192) {
            ENG_LOG("eng_vlog: read %d, write %d\n", r_cnt, w_cnt);
        }
    }
out:
    close(pipe_fd);
    close(ser_fd);
    return 0;
}
