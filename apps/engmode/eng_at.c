#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "engopt.h"
#include "eng_pcclient.h"
#include "eng_cmd4linuxhdlr.h"

// SIPC interfaces in AP linux for AT CMD
static char *at_mux_devname[] = {
    "/dev/stty_td31", // AT channel in TD mode
    "/dev/stty_w31", // AT channel in W mode
    NULL // BT/WIFI mode
};

static int at_mux_fd = -1;
static int pc_fd=-1;

static int start_gser()
{
    struct termios ser_settings;

    if (pc_fd>=0){
        ENG_LOG("%s ERROR : %s\n", __FUNCTION__, strerror(errno));
        close(pc_fd);
    }

    ENG_LOG("open serial\n");
    pc_fd = open(s_at_ser_path,O_RDWR);
    if(pc_fd < 0) {
        ENG_LOG("cannot open vendor serial\n");
        return -1;
    }

    tcgetattr(pc_fd, &ser_settings);
    cfmakeraw(&ser_settings);

    ser_settings.c_lflag |= (ECHO | ECHONL);
    ser_settings.c_lflag &= ~ECHOCTL;
    tcsetattr(pc_fd, TCSANOW, &ser_settings);

    return 0;
}

static void *eng_readpcat_thread(void *par)
{
    int len;
    int written;
    int cur;
    char engbuf[ENG_BUFFER_SIZE];
    char databuf[ENG_BUFFER_SIZE];
    int i, offset_read, length_read, status;

    for(;;){
        ENG_LOG("%s: wait pcfd=%d\n",__func__,pc_fd);
read_again:
        memset(engbuf, 0, ENG_BUFFER_SIZE);
        if (pc_fd >= 0){
            len = read(pc_fd, engbuf, ENG_BUFFER_SIZE);
            ENG_LOG("%s: wait pcfd=%d buf=%s len=%d",__func__,pc_fd,engbuf,len);
            for (i=0;i<len;i++){
                ENG_LOG("%c %x",engbuf[i],engbuf[i]);
            }
            if (len <= 0) {
                ENG_LOG("%s: read length error %s",__FUNCTION__,strerror(errno));
                sleep(1);
                start_gser();
                goto read_again;
            }else{
                // Just send to modem transparently.
                if(at_mux_fd >= 0) {
                    cur = 0;
                    while(cur < len) {
                        do {
                            written = write(at_mux_fd, engbuf + cur, len -cur);
                            ENG_LOG("muxfd=%d, written=%d\n", at_mux_fd, written);
                        }while(written < 0 && errno == EINTR);

                        if(written < 0) {
                            ENG_LOG("%s: write length error %s\n", __FUNCTION__, strerror(errno));
                            break;
                        }
                        cur += written;
                    }
                }else {
                    ENG_LOG("muxfd fail?");
                }
            }
        }else{
            sleep(1);
            start_gser();
        }
    }
    return NULL;
}

static void *eng_readmodemat_thread(void *par)
{
    int ret;
    int len;
    char engbuf[ENG_BUFFER_SIZE];

    for(;;){
        ENG_LOG("%s: wait pcfd=%d\n",__func__,pc_fd);
        memset(engbuf, 0, ENG_BUFFER_SIZE);
        len = read(at_mux_fd, engbuf, ENG_BUFFER_SIZE);
        ENG_LOG("muxfd=%d buf=%s,len=%d\n",at_mux_fd,engbuf,len);
        if (len <= 0) {
            ENG_LOG("%s: read length error %s\n",__FUNCTION__,strerror(errno));
            sleep(1);
            continue;
        }else{
write_again:
            if (pc_fd>=0){
                ret = write(pc_fd,engbuf,len);
                if (ret <= 0) {
                    ENG_LOG("%s: write length error %s\n",__FUNCTION__,strerror(errno));
                    sleep(1);
                    start_gser();
                    goto write_again;
                }
            }else{
                sleep(1);
            }
        }
    }
    return NULL;
}

int eng_at_pcmodem(int run_type)
{
    eng_thread_t t1,t2;

    ENG_LOG("%s",__func__);

    start_gser();

    at_mux_fd = open(at_mux_devname[run_type], O_RDWR);
    if(at_mux_fd < 0){
        ENG_LOG("%s: open %s fail [%s]\n",__FUNCTION__, at_mux_devname[run_type],strerror(errno));
        return -1;
    }

    if (0 != eng_thread_create( &t1, eng_readpcat_thread, (void*)run_type)){
        ENG_LOG("read pcat thread start error");
    }

    if (0 != eng_thread_create( &t2, eng_readmodemat_thread, (void*)run_type)){
        ENG_LOG("read modemat thread start error");
    }
    return 0;
}
