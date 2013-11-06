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
#include <termios.h>
#include "engopt.h"
#include "cutils/properties.h"
#include "eng_pcclient.h"
#include "vlog.h"
#include "eng_util.h"

#define DATA_BUF_SIZE (4096 * 64)
#define MAX_OPEN_TIMES  10

extern int g_ass_start;
static char log_data[DATA_BUF_SIZE];
static int s_ser_fd = 0;
static int s_connect_type = 0;

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

int get_ser_fd(void)
{
    return s_ser_fd;
}

int restart_gser(void)
{
    struct termios ser_settings;

    if(s_ser_fd > 0) {
        ENG_LOG("%s: eng_vlog close usb serial:%d\n", __FUNCTION__, s_ser_fd);
        close(s_ser_fd);
    }

    s_ser_fd = eng_open_dev(s_connect_ser_path[s_connect_type], O_WRONLY);
    if(s_ser_fd < 0) {
        ENG_LOG("%s: eng_vlog cannot open general serial\n", __FUNCTION__);
        return -1;
    }

    ENG_LOG("%s: eng_vlog reopen usb serial:%d\n", __FUNCTION__, s_ser_fd);
    return 0;
}

void *eng_vlog_thread(void *x)
{
    int ser_fd, sipc_fd;
    int r_cnt, w_cnt, offset;
    int retry_num = 0;
    int dumpmemlen = 0;
    struct eng_param * param = (struct eng_param *)x;

    ENG_LOG("eng_vlog thread start\n");

    if(param == NULL) {
        ENG_LOG("eng_vlog invalid input\n");
        return NULL;
    }

    /*open usb/uart*/
    ENG_LOG("eng_vlog open serial...\n");
    s_connect_type = param->connect_type;
    ser_fd = eng_open_dev(s_connect_ser_path[s_connect_type], O_WRONLY);
    if(ser_fd < 0) {
        ENG_LOG("eng_vlog open serial failed, error: %s\n", strerror(errno));
        return NULL;
    }

    s_ser_fd = ser_fd;

    /*open vbpipe/spipe*/
    ENG_LOG("eng_vlog open SIPC channel...\n");
    do{
        sipc_fd = open(s_cp_pipe[param->cp_type], O_RDONLY);
        if(sipc_fd < 0) {
            ENG_LOG("eng_vlog cannot open %s, error: %s\n", s_cp_pipe[param->cp_type], strerror(errno));
            sleep(5);
        }

        if((++retry_num) > MAX_OPEN_TIMES) {
            ENG_LOG("eng_vlog SIPC open times exceed the max times, vlog thread stopped.\n");
            goto out;
        }
    }while(sipc_fd < 0);

    ENG_LOG("eng_vlog put log data from SIPC to serial\n");
    while(1) {
        int split_flag = 0;
        memset(log_data, 0, sizeof(log_data));
        r_cnt = read(sipc_fd, log_data, DATA_BUF_SIZE);
        if (r_cnt <= 0) {
            ENG_LOG("eng_vlog read no log data : r_cnt=%d, %s\n",  r_cnt, strerror(errno));
            continue;
        }

        // printf dump memory len
        dump_mem_len_print(r_cnt, &dumpmemlen);

        offset = 0; //reset the offset

        if((r_cnt%64==0) && param->califlag && (param->connect_type==CONNECT_USB))
            split_flag = 1;
        do {
            if(split_flag)
                w_cnt = write(ser_fd, log_data + offset, r_cnt-32);
            else
                w_cnt = write(ser_fd, log_data + offset, r_cnt);
	    if (w_cnt < 0) {
                if(errno == EBUSY)
                    usleep(59000);
                else {
                    ENG_LOG("eng_vlog no log data write:%d ,%s\n", w_cnt, strerror(errno));

                    // FIX ME: retry to open
                    retry_num = 0; //reset the try number.
                    while (-1 == restart_gser()) {
                        ENG_LOG("eng_vlog open gser port failed\n");
                        sleep(1);
                        retry_num ++;
                        if(retry_num > MAX_OPEN_TIMES) {
                            ENG_LOG("eng_vlog: vlog thread stop for gser error !\n");
                            return 0;
                        }
                    }
                }
                ser_fd = s_ser_fd;
            } else {
                r_cnt -= w_cnt;
                offset += w_cnt;
                split_flag = 0;
                //ENG_LOG("eng_vlog: r_cnt: %d, w_cnt: %d, offset: %d\n", r_cnt, w_cnt, offset);
            }
        } while(r_cnt > 0);
    }

out:
    ENG_LOG("eng_vlog thread end\n");
    if (sipc_fd >= 0)
        close(sipc_fd);
    close(ser_fd);

    return 0;
}
