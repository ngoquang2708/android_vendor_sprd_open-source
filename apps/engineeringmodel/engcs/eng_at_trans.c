#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "engopt.h"
#include "eng_pcclient.h"
#include "eng_at_trans.h"

static char* s_at_mux_path = "/dev/ts0710mux18";
static char* s_at_ser_path = "/dev/ttyGS1";

static int at_mux_fd = -1;
static int pc_fd=-1;

static char eng_pc_buf[ENG_BUFFER_SIZE << 3] = {0}; // 16K
static char eng_modem_buf[ENG_BUFFER_SIZE << 3] = {0}; // 16K

static int start_gser(char* name)
{
    struct termios ser_settings;

    if (pc_fd>=0){
        ENG_LOG("%s ERROR : %s\n", __FUNCTION__, strerror(errno));
        close(pc_fd);
    }

    if(0 != access(s_at_ser_path, F_OK))
    {
        ENG_LOG("%s : %s is not exist", __FUNCTION__, s_at_ser_path);
        return -2;
    }

    ENG_LOG("open serial\n");
    pc_fd = open(s_at_ser_path,O_RDWR);
    if(pc_fd < 0) {
        ENG_LOG("cannot open vendor serial\n");
        return -1;
    }

    tcgetattr(pc_fd, &ser_settings);
    cfmakeraw(&ser_settings);

    //ser_settings.c_lflag |= (ECHO | ECHONL);
    //ser_settings.c_lflag &= ~ECHOCTL;
    tcsetattr(pc_fd, TCSANOW, &ser_settings);

    return 0;
}

static void *eng_readpcat_thread(void *par)
{
    int len;
    int written;
    int cur;
    int i;

    for(;;){
        ENG_LOG("%s: wait pcfd=%d\n",__func__,pc_fd);
read_again:
        memset(eng_pc_buf, 0, ENG_BUFFER_SIZE << 3);
        if (pc_fd >= 0){
            len = read(pc_fd, eng_pc_buf, ENG_BUFFER_SIZE << 3);
#if 0
            ENG_LOG("%s: wait pcfd=%d buf=%s len=%d",__func__,pc_fd,eng_pc_buf,len);
            for (i=0;i<len;i++){
                ENG_LOG("%c %x",eng_pc_buf[i],eng_pc_buf[i]);
            }
#endif
            if (len <= 0) {
                ENG_LOG("%s: read length error %s",__FUNCTION__,strerror(errno));
                sleep(1);
                start_gser(s_at_ser_path);
                goto read_again;
            }else{
                if (at_mux_fd>=0){
                    cur = 0;
                    while (cur < len) {
                        do {
                            written = write(at_mux_fd, eng_pc_buf + cur, len - cur);
                            ENG_LOG("muxfd=%d written=%d",at_mux_fd,written);
                        } while (written < 0 && errno == EINTR);
                        if (written < 0) {
                            ENG_LOG("%s: write length error %s",__FUNCTION__,strerror(errno));
                            break;
                        }
                        cur += written;
                    }
                }else{
                    ENG_LOG("muxfd fail?");
                }
            }
        }else{
            sleep(1);
            start_gser(s_at_ser_path);
        }
    }
    return NULL;
}

static void *eng_readmodemat_thread(void *par)
{
    int ret;
    int len;

    for(;;){
        ENG_LOG("%s: wait pcfd=%d\n",__func__,pc_fd);
        memset(eng_modem_buf, 0, ENG_BUFFER_SIZE);
        len = read(at_mux_fd, eng_modem_buf, ENG_BUFFER_SIZE);
#if 0
        ENG_LOG("muxfd=%d buf=%s,len=%d\n",at_mux_fd,eng_modem_buf,len);
#endif
        if (len <= 0) {
            ENG_LOG("%s: read length error %s\n",__FUNCTION__,strerror(errno));
            sleep(1);
            continue;
        }else{
write_again:
            if (pc_fd>=0){
                ret = write(pc_fd,eng_modem_buf,len);
                if (ret <= 0) {
                    ENG_LOG("%s: write length error %s\n",__FUNCTION__,strerror(errno));
                    sleep(1);
                    start_gser(s_at_ser_path);
                    goto write_again;
                }
            }else{
                sleep(1);
            }
        }
    }
    return NULL;
}

int eng_at_pcmodem(void)
{
    eng_thread_t t1,t2;

    ENG_LOG("%s",__func__);

    if(-2 == start_gser(s_at_ser_path)){
        ENG_LOG("%s: device not exist, so don't run the dialling thread \n",__FUNCTION__);
        return -1;
    }

    do {
        at_mux_fd = open(s_at_mux_path,O_RDWR);
        if(at_mux_fd < 0){
            ENG_LOG("%s: open %s fail [%s]\n",__FUNCTION__, s_at_mux_path,strerror(errno));
            sleep(1);
        }
    } while(at_mux_fd < 0);

    if (0 != eng_thread_create( &t1, eng_readpcat_thread, 0)){
        ENG_LOG("read pcat thread start error");
    }

    if (0 != eng_thread_create( &t2, eng_readmodemat_thread, 0)){
        ENG_LOG("read modemat thread start error");
    }

    return 0;
}
