#define LOG_TAG "refnotify"

#include <cutils/log.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <ctype.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <string.h>
#include <mtd/mtd-user.h>
#include <time.h>
#include <sys/reboot.h>
#include <linux/rtc.h>
#include <cutils/properties.h>

#define REF_DEBUG

#ifdef REF_DEBUG
#define REF_LOGD(x...) ALOGD( x )
#define REF_LOGE(x...) ALOGE( x )
#else
#define REF_LOGD(x...)
#define REF_LOGE(x...)
#endif

#define TD_NOTIFY_DEV "/dev/spipe_td8"
#define W_NOTIFY_DEV "/dev/spipe_w8"

#define RTC_DEV "/dev/rtc0"

/*used to get sleep/wake state of display*/
#define WakeFileName  "/sys/power/wait_for_fb_wake"
#define SleepFileName  "/sys/power/wait_for_fb_sleep"

#define IQ_DEV "/sys/module/sprd_iq/parameters/iq_base"
#define IQ_BASE_LENGTH 32

#define freq_fb_dev "/sys/class/graphics/fb0/dynamic_pclk"

enum {
	REF_PSFREQ_CMD,
	REF_SETTIME_CMD,
	REF_SETDATE_CMD,
	REF_GETTIME_CMD,
	REF_GETDATE_CMD,
	REF_AUTODLOADER_CMD,
	REF_SLEEP_CMD,
	REF_WAKE_CMD,
	REF_RESET_CMD,
	REF_IQ_CMD,
	REF_CMD_MAX
};

struct ref_date {
	uint8_t mday;
	uint8_t mon;
	uint16_t year;
	uint8_t wday;
};

struct ref_time {
	uint8_t sec;
	uint8_t min;
	uint8_t hour;
};

struct iq_info {
	uint32_t base;
	uint32_t length;
};

struct refnotify_cmd {
	int cmd_type;
	uint32_t length;
};

struct ref_lcdfreq {
	uint32_t clk;
	uint32_t divisor;
};

static void usage(void)
{
	fprintf(stderr,
	"\n"
	"Usage: refnotify [-t type] [-h]\n"
	"receive and do the notify from modem side \n"
	"  -t type     td type:0, wcdma type: other \n"
	"  -h           : show this help message\n\n");
}

void RefNotify_DoAutodloader(struct refnotify_cmd *pcmd)
{
	sync();
	__reboot(LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
						LINUX_REBOOT_CMD_RESTART2, "autodloader");
}

void RefNotify_DoReset(struct refnotify_cmd *pcmd)
{
	sync();
	__reboot(LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
						LINUX_REBOOT_CMD_RESTART2, "normal");
}

int RefNotify_rtc_readtm(struct tm *ptm)
{
	int fd;
	fd = open(RTC_DEV, O_RDWR);
	if(fd < 0) {
		REF_LOGE(" %s failed, error: %s", __func__, strerror(errno));
		return -1;
	}
	memset(ptm, 0, sizeof(*ptm));
	ioctl(fd, RTC_RD_TIME, ptm);
	close(fd);
	return 0;
}

int RefNotify_rtc_writetm(struct tm *ptm)
{
	int fd;
	fd = open(RTC_DEV, O_RDWR);
	if(fd < 0) {
		REF_LOGE(" %s failed, error: %s", __func__, strerror(errno));
		return -1;
	}
	ioctl(fd, RTC_SET_TIME, ptm);
	close(fd);
	return 0;
}


void RefNotify_DoGetTime(int fd, struct refnotify_cmd *cmd)
{
	int ret, length = sizeof(struct refnotify_cmd) + sizeof(struct ref_time);
	struct tm timenow;
	struct ref_time *ptime = NULL;
	struct refnotify_cmd *pcmd = NULL;
	if(RefNotify_rtc_readtm(&timenow) < 0) {
		REF_LOGE("error to get rtc time \n");
		return;
	}
	pcmd = (struct refnotify_cmd*)malloc(length);
	if(pcmd == NULL)
		return ;
	pcmd->cmd_type = REF_GETTIME_CMD;
	pcmd->length = length;
	ptime = (struct ref_time*)(pcmd+1);
	ptime->sec = timenow.tm_sec;
	ptime->min = timenow.tm_min;
	ptime->hour = timenow.tm_hour;
	ret = write(fd, pcmd, length);
	if(ret != length) {
		REF_LOGE("RefNotify write %d return %d, errno = %s", length , ret, strerror(errno));
	}
	free(pcmd);
}

void RefNotify_DoGetDate(int fd, struct refnotify_cmd *cmd)
{
	int ret, length = sizeof(struct refnotify_cmd) + sizeof(struct ref_date);
	struct tm timenow;
	struct ref_date *pDate = NULL;
	struct refnotify_cmd *pcmd = NULL;

	if(RefNotify_rtc_readtm(&timenow) < 0) {
		REF_LOGE("error to get rtc time \n");
		return;
	}
	pcmd = (struct refnotify_cmd*)malloc(length);
	if(pcmd == NULL)
		return;

	pcmd->cmd_type = REF_GETDATE_CMD;
	pcmd->length = length;
	pDate = (struct ref_date*)(pcmd+1);
	pDate->mday= timenow.tm_mday;
	pDate->mon = timenow.tm_mon + 1;
	pDate->year = timenow.tm_year + 1900;
	pDate->wday = timenow.tm_wday;
	ret = write(fd, pcmd, length);
	if(ret != length) {
		REF_LOGE("RefNotify write %d return %d, errno = %s", length , ret, strerror(errno));
	}
	free(pcmd);
}

static int RefNotify_DoSetTime(struct refnotify_cmd *pcmd)
{
	int ret;
	time_t timer;
	struct timeval tv;
	struct tm timenow;
	struct ref_time *ptime = NULL;

	if(RefNotify_rtc_readtm(&timenow) < 0) {
		REF_LOGE("error to get rtc time \n");
		return -1;
	}
	ptime = (struct ref_time*)(pcmd+1);
	REF_LOGD("refnotify set time %d, %d, %d \n", ptime->sec, ptime->min, ptime->hour);
	timenow.tm_sec = ptime->sec;
	timenow.tm_min = ptime->min;
	timenow.tm_hour = ptime->hour;
	if(RefNotify_rtc_writetm(&timenow) < 0) {
		REF_LOGE("error to set rtc time \n");
		return -1;
	}
	timer = mktime(&timenow);
	tv.tv_sec = timer;
	tv.tv_usec = 0;
	if(settimeofday(&tv, (struct timezone*)0) < 0){
		REF_LOGE("Set timer error \n");
		return -1;
	}
	return 0;
}


static int RefNotify_DoSetDate(struct refnotify_cmd *pcmd)
{
	int ret;
	time_t timer;
	struct timeval tv;
	struct tm timenow;
	struct ref_date *pdate = NULL;

	if(RefNotify_rtc_readtm(&timenow) < 0) {
		REF_LOGE("error to get rtc time \n");
		return -1;
	}

	pdate = (struct ref_date*)(pcmd+1);

	timenow.tm_mday= pdate->mday;
	timenow.tm_mon = pdate->mon - 1;
	timenow.tm_year = pdate->year - 1900;
	timenow.tm_wday = pdate->wday;
	if(RefNotify_rtc_writetm(&timenow) < 0) {
		REF_LOGE("error to set rtc time \n");
		return -1;
	}
	timer = mktime(&timenow);
	tv.tv_sec = timer;
	tv.tv_usec = 0;
	if(settimeofday(&tv, (struct timezone*)0) < 0){
		REF_LOGE("Set timer error \n");
		return -1;
	}
	return 0;
}

static void RefNotify_DoGetIqInfo(int fd, struct refnotify_cmd *cmd)
{
	int iq_fd;
	int ret, length = sizeof(struct refnotify_cmd) + sizeof(struct iq_info);
	uint32_t base = 0xffffffff;
	char cbase[IQ_BASE_LENGTH + 1] = {0};
	struct refnotify_cmd *pcmd = NULL;
	struct iq_info *piq = NULL;
	iq_fd = open(IQ_DEV, O_RDONLY);
	if(iq_fd < 0) {
		REF_LOGE(" %s failed, error: %s", __func__, strerror(errno));
		return;
	}
	ret = read(iq_fd, cbase, IQ_BASE_LENGTH);
	REF_LOGD("iq_base %s \n", cbase);
	close(iq_fd);
	REF_LOGD("close \n");
	base = strtoul(cbase, (char**)NULL, 10);
	REF_LOGD("iq_base %d \n", base);

	if(ret < 0)
		return;
	pcmd = (struct refnotify_cmd*)malloc(length);
	if(pcmd == NULL)
		return;

	pcmd->cmd_type = REF_IQ_CMD;
	pcmd->length = length;
	piq = (struct iq_info*)(pcmd+1);
	piq->base = base;
	piq->length = 128*1024*1024;
	ret = write(fd, pcmd, length);
	if(ret != length) {
		REF_LOGE("RefNotify write %d return %d, errno = %s", length , ret, strerror(errno));
	}
	free(pcmd);
}

static void RefNotify_DoFreqCmd(struct refnotify_cmd *pcmd)
{
	struct ref_lcdfreq *p_freq = (struct ref_lcdfreq *)(pcmd + 1);
	REF_LOGD("%s: %d, %d\n",__func__, p_freq->clk, p_freq->divisor);
	int fd, wr;
	char freq_str[32] = {"0,0"};//default value 0, if suspended
#if 0
	char freq_fb_dev[64];
	if(property_get("ro.refnotify.freqdev",freq_fb_dev,NULL)>0){
		REF_LOGE("ro.refnotify.freqdev: %s", freq_fb_dev);
	}else{
		REF_LOGE("get ro.refnotify.freqdev failed");
		return;
	}
#endif
	fd = open(freq_fb_dev, O_RDWR, 0);
	if(fd < 0) {
		REF_LOGE("open %s failed, error: %s", freq_fb_dev, strerror(errno));
		return;
	}
	if(p_freq->clk){
		sprintf(freq_str, "%d,%d", p_freq->clk, p_freq->divisor);
	}
	wr = write(fd, freq_str, strlen(freq_str));
	if(wr < 0) {
		REF_LOGE("write %s failed, error: %s", freq_fb_dev, strerror(errno));
	}
	close(fd);
}

void RefNotify_DoCmd(int fd, struct refnotify_cmd *pcmd)
{
	REF_LOGD("%s, %d \n", __func__, pcmd->cmd_type);
	switch(pcmd->cmd_type) {
		case REF_PSFREQ_CMD:
			RefNotify_DoFreqCmd(pcmd);
			break;
		case REF_SETTIME_CMD:
			(void)RefNotify_DoSetTime(pcmd);
			break;
		case REF_SETDATE_CMD:
			(void)RefNotify_DoSetDate(pcmd);
			break;
		case REF_GETTIME_CMD:
			RefNotify_DoGetTime(fd, pcmd);
			break;
		case REF_GETDATE_CMD:
			RefNotify_DoGetDate(fd, pcmd);
			break;
		case REF_AUTODLOADER_CMD:
			RefNotify_DoAutodloader(pcmd);
			break;
		case REF_RESET_CMD:
			RefNotify_DoReset(pcmd);
			break;
		case REF_IQ_CMD:
			RefNotify_DoGetIqInfo(fd, pcmd);
		default:
			break;
	}
}

/*a daemon to notify cp of sleeping to prevent cp sending request*/
void* sleep_monitor(void *arg)
{
	int fdw, fds, fd, r;
	struct refnotify_cmd cmd;
	char buf[1];
	fd = (int)arg;
	cmd.length = sizeof(struct refnotify_cmd);

	while(1){
		fds = open(SleepFileName, O_RDONLY);
		if (fds < 0) {
			REF_LOGE("Couldn't open file" SleepFileName);
		}else{
			/*we are awake, waiting for sleep*/
			r = read(fds, buf, 1);
			if(r < 0){
				REF_LOGE("wait_for_fb_sleep read error: %s", strerror(errno));
				close(fds);
				continue;
			}
			/*here, we are in sleep state*/
			cmd.cmd_type = REF_SLEEP_CMD;
			write(fd, &cmd, cmd.length);
			close(fds);
		}
		fdw = open(WakeFileName, O_RDONLY);
		if (fdw < 0) {
			REF_LOGE("Couldn't open file" WakeFileName);
		}else{
			/*we are sleeping, waiting for wake*/
			r = read(fdw, buf, 1);
			if(r < 0){
				REF_LOGE("wait_for_fb_wake read error: %s", strerror(errno));
				close(fdw);
				continue;
			}
			/*here, we are awake*/
			cmd.cmd_type = REF_WAKE_CMD;
			write(fd, &cmd, cmd.length);
			close(fdw);
		}
	}

	return NULL;
}

int main(int argc, char *argv[])
{
	char buf[128];
	char *pbuf;
	int opt;
	int type;
	int td_flag = 1 , fd, num;
	uint32_t numRead;
	char *path;
	struct refnotify_cmd *pcmd;
	pthread_t tid;
	REF_LOGD("Enter RefNotify main \n");
	if (argc == 1 || (strcmp(argv[1], "-t") && strcmp(argv[1], "-h"))) {
		usage();
		exit(EXIT_FAILURE);
	}
	while ( -1 != (opt = getopt(argc, argv, "t:h"))) {
		switch (opt) {
			case 't':
				type = atoi(optarg);
				if (type == 0){
					td_flag = 1;
				} else {
					td_flag = 0;
				}
				break;
			case 'h':
				usage();
				exit(0);
			default:
				usage();
				exit(EXIT_FAILURE);
		}
	}
	REF_LOGD("Enter RefNotify main td_flag %d \n", td_flag);
	if(td_flag == 1) {
		path = TD_NOTIFY_DEV;
	} else {
		path = W_NOTIFY_DEV;
	}
	fd = open(path/*TD_NOTIFY_DEV*/, O_RDWR);
	if (fd < 0) {
		REF_LOGE("RefNotify open %s failed, error: %s", path, strerror(errno));
		exit(EXIT_FAILURE);
	}
	/*start a service to notify cp of sleep/wake state*/
	pthread_create(&tid, NULL,sleep_monitor, (void *)fd);

	//RefNotify_DoGetIqInfo(fd, NULL);

	for (;;) {
		pbuf = buf;
		numRead = 0;
		REF_LOGD("RefNotify %s: wait for modem notify event ...", __func__);
		memset(buf, 0, sizeof(buf));
readheader:
		num = read(fd, pbuf + numRead, sizeof(struct refnotify_cmd));
		if(num < 0 && errno == EINTR) {
			REF_LOGE("RefNotify read %d return %d, errno = %s", fd , numRead, strerror(errno));
			sleep(1);
			goto readheader;
		} else if(num < 0) {
			REF_LOGE("RefNotify read %d return %d, errno = %s", fd , numRead, strerror(errno));
			exit(EXIT_FAILURE);
		}
		numRead += num;
		if (numRead < sizeof(struct refnotify_cmd)) {
			goto readheader;
		}
		pcmd = (struct refnotify_cmd*)buf;

		if(numRead < pcmd->length) {
readcontent:
			num = read(fd, pbuf + numRead, pcmd->length - numRead);
			if(num < 0 && errno == EINTR) {
				REF_LOGE("RefNotify read content %d return %d, errno = %s", fd , numRead, strerror(errno));
				sleep(1);
				goto readcontent;
			} else if(num < 0) {
				REF_LOGE("RefNotify read %d return %d, errno = %s", fd , numRead, strerror(errno));
				exit(EXIT_FAILURE);
			}
			numRead += num;
			if (numRead < pcmd->length) {
				goto readcontent;
			}
		}
		REF_LOGD("RefNotify ready to do cmd \n");
		RefNotify_DoCmd(fd, (struct refnotify_cmd*)buf);
	}
	return 0;
}


