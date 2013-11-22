/*
 * Copyright (C) 2012 Spreadtrum Communications Inc.
//                 Last Change:  2013-03-06 09:39:00
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <pthread.h>
#include <time.h>
#include <sys/inotify.h>
#include <log/logger.h>
#include <log/logd.h>
#include <log/logprint.h>
#include <log/event_tag_map.h>
#include <cutils/sockets.h>
#include <cutils/properties.h>
#include "private/android_filesystem_config.h"

#include "slog.h"

#define FD_TIME "/sys/kernel/debug/power/sprd_timestamp"
static int get_timezone()
{
	time_t time_utc;
	struct tm tm_local, tm_gmt;
	int time_zone;

	time_utc = time(NULL);
	localtime_r(&time_utc, &tm_local);
	gmtime_r(&time_utc, &tm_gmt);
	time_zone = tm_local.tm_hour - tm_gmt.tm_hour;
	if (time_zone < -12) {
		time_zone += 24;
	} else if (time_zone > 12) {
		time_zone -= 24;
	}

	err_log("UTC: %02d-%02d-%02d %02d:%02d:%02d",
				tm_gmt.tm_year % 100,
				tm_gmt.tm_mon + 1,
				tm_gmt.tm_mday,
				tm_gmt.tm_hour,
				tm_gmt.tm_min,
				tm_gmt.tm_sec);

	err_log("LOCAL: %02d-%02d-%02d %02d:%02d:%02d",
				tm_local.tm_year % 100,
				tm_local.tm_mon + 1,
				tm_local.tm_mday,
				tm_local.tm_hour,
				tm_local.tm_min,
				tm_local.tm_sec);

	return time_zone;
}

static void write_modem_timestamp(struct slog_info *info, char *buffer)
{
	int fd, ret;
	FILE *fp;
	int time_zone;
	struct modem_timestamp *mts;

	if (strncmp(info->name, "modem", 5)) {
		return;
	}

	mts = calloc(1, sizeof(struct modem_timestamp));
	fd = open(FD_TIME, O_RDWR);
	if( fd < 0 ){
		err_log("Unable to open time stamp device '%s'", FD_TIME);
		free(mts);
		return;
	}
	ret = read(fd, (char*)mts + 4, 12);
	if(ret < 12) {
		close(fd);
		free(mts);
		return;
	}
	close(fd);

	mts->magic_number = 0x12345678;
	time_zone = get_timezone();
	mts->tv.tv_sec += time_zone * 3600;
	err_log("%lx, %lx, %lx, %lx", mts->magic_number, mts->tv.tv_sec, mts->tv.tv_usec, mts->sys_cnt);

	fp = fopen(buffer, "a+b");
	if(fp == NULL) {
		err_log("open file %s failed!", buffer);
		free(mts);
		exit(0);
	}
	fwrite(mts, sizeof(struct modem_timestamp), 1, fp);
	fclose(fp);

	free(mts);
}

static void gen_logfile(char *filename, struct slog_info *info)
{
	int ret;

	sprintf(filename, "%s/%s/%s", current_log_path, top_logdir, info->log_path);
	ret = mkdir(filename, S_IRWXU | S_IRWXG | S_IRWXO);
	if (-1 == ret && (errno != EEXIST)){
                err_log("mkdir %s failed.", filename);
                exit(0);
        }

	sprintf(filename, "%s/%s/%s/%s.log", current_log_path, top_logdir, info->log_path, info->log_basename);
}

void record_snap(struct slog_info *info)
{
	struct stat st;
	FILE *fcmd, *fp;
	char buffer[4096];
	char buffer_cmd[MAX_NAME_LEN];
	time_t t;
	struct tm tm;
	int ret;

	/* setup log file first */
	gen_logfile(buffer, info);
	fp = fopen(buffer, "a+");
	if(fp == NULL) {
		err_log("open file %s failed!", buffer);
		exit(0);
	}
	/* add timestamp */
	t = time(NULL);
	localtime_r(&t, &tm);
	fprintf(fp, "\n============  %02d-%02d-%02d %02d:%02d:%02d  ==============\n",
				tm.tm_year % 100,
				tm.tm_mon + 1,
				tm.tm_mday,
				tm.tm_hour,
				tm.tm_min,
				tm.tm_sec);

        if(!strncmp(info->opt, "cmd", 3)) {
		fclose(fp);
		sprintf(buffer_cmd, "%s >> %s", info->content, buffer);
		system(buffer_cmd);
		return;
	}

	fcmd = fopen(info->content, "r");

	if(fcmd == NULL) {
		err_log("open target %s failed!", info->content);
		fclose(fp);
		return;
	}

	/* recording... */
	while( (ret = fread(buffer, 1, 4096, fcmd)) > 0)
		fwrite(buffer, 1, ret, fp);

	fclose(fp);
	fclose(fcmd);

	return;
}

void *snapshot_log_handler(void *arg)
{
	struct slog_info *info;
	int tmp, sleep_secs = 0, next_sleep_secs = 0;

	/* misc_log on/off state will control all snapshot log */
	if(misc_log->state != SLOG_STATE_ON)
		return NULL;

	if(!snapshot_log_head)
		return NULL;

	snapshot_log_handler_started = 1;

	while(slog_enable == SLOG_ENABLE) {
		info = snapshot_log_head;
		while(info) {
			/* misc_log level decided which log will be restored */
			if(info->level > misc_log->level){
				info = info->next;
				continue;
			}

			/* internal equal zero means this log will trigger by user command */
			if(info->interval == 0){
				info = info->next;
				continue;
			}

			/* "state" field unused, so we store pass time at here */
			info->state += sleep_secs;
			if(info->state >= info->interval) {
				info->state = 0;
				record_snap(info);
			}
			tmp = info->interval - info->state;
			if(tmp < next_sleep_secs || !next_sleep_secs)
				next_sleep_secs = tmp;
			info = info-> next;
		}
		/* means no snapshot will be triggered in thread */
		if(!next_sleep_secs)
			break;

		/* update sleep times */
		sleep_secs = next_sleep_secs;
		while(next_sleep_secs-- > 0 && slog_enable == SLOG_ENABLE)
			sleep(1);
		next_sleep_secs = 0;
	}
	snapshot_log_handler_started = 0;
	return NULL;
}

void cp_file(char *path, char *new_path)
{
	FILE *fp_src, *fp_dest;
	char buffer[4096];
	int ret;

	fp_src = fopen(path, "r");
	if(fp_src == NULL) {
		err_log("open src file failed!");
		return;
	}
	fp_dest = fopen(new_path, "w");
	if(fp_dest == NULL) {
		err_log("open dest file failed!");
		fclose(fp_src);
		return;
	}

	while( (ret = fread(buffer, 1, 4096, fp_src)) > 0)
		fwrite(buffer, 1, ret, fp_dest);

	fclose(fp_src);
	fclose(fp_dest);

	return;
}

static int capture_snap_for_notify(struct slog_info *head, char *filepath)
{
        struct slog_info *info = head;

        while(info) {
		if(info->level <= 6)
			exec_or_dump_content(info, filepath);
		info = info->next;
        }

        return 0;
}

static void handle_notify_file(int wd, const char *name)
{
	struct slog_info *info;
	struct stat st;
	char src_file[MAX_NAME_LEN], dest_file[MAX_NAME_LEN];
	time_t t;
	struct tm tm;
	int ret;

	t = time(NULL);
	localtime_r(&t, &tm);

	info = notify_log_head;
	while(info) {
		if(wd != info->interval){
			info = info->next;
			continue;
		}

		gettimeofday(&info->current, NULL);
		if(info->current.tv_sec > info->last.tv_sec + 20) {
			info->last.tv_sec = info->current.tv_sec;
		} else {
			return;
		}

		sprintf(src_file, "%s/%s", info->content, name);
		if(stat(src_file, &st)) {
			return;
		}

		sprintf(dest_file, "%s/%s/%s", current_log_path, top_logdir, info->log_path);
		ret = mkdir(dest_file, S_IRWXU | S_IRWXG | S_IRWXO);
		if (-1 == ret && (errno != EEXIST)) {
			err_log("mkdir %s failed.", dest_file);
			exit(0);
		}

		sprintf(dest_file, "%s/%s/%s/%s", current_log_path, top_logdir, info->log_path, info->name);
		ret = mkdir(dest_file, S_IRWXU | S_IRWXG | S_IRWXO);
		if (-1 == ret && (errno != EEXIST)) {
			err_log("mkdir %s failed.", dest_file);
			exit(0);
		}

		sprintf(dest_file, "%s/%s/%s/%s/screenshot_%02d%02d%02d.jpg",
				current_log_path,
				top_logdir,
				info->log_path,
				info->name,
				tm.tm_hour,
				tm.tm_min,
				tm.tm_sec);

		screen_shot(dest_file);

		sprintf(dest_file, "%s/%s/%s/%s/snapshot_%02d%02d%02d.log",
				current_log_path,
				top_logdir,
				info->log_path,
				info->name,
				tm.tm_hour,
				tm.tm_min,
				tm.tm_sec);

		capture_snap_for_notify(snapshot_log_head, dest_file);

		/* for collect log */
		sleep(3);

		sprintf(src_file, "%s/%s", info->content, name);
		sprintf(dest_file, "%s/%s/%s/%s/%s_%02d%02d%02d.log",
				current_log_path,
				top_logdir,
				info->log_path,
				info->name,
				name,
				tm.tm_hour,
				tm.tm_min,
				tm.tm_sec
		);

		debug_log("%s, %s", src_file, dest_file);
		cp_file(src_file, dest_file);
	}
	return;
}

void *notify_log_handler(void *arg)
{
	ssize_t size;
	char buffer[MAX_LINE_LEN];
	int notify_fd, wd, len;
	struct slog_info *info;
	struct inotify_event *event;
	int ret;
	fd_set readset;
	int result;
	struct timeval timeout;

	/* misc_log on/off state will control all snapshot log */
	if(misc_log->state != SLOG_STATE_ON)
		return NULL;

	if(!notify_log_head)
		return NULL;

	notify_log_handler_started = 1;

	/* init notify list */
	notify_fd = inotify_init();
	if(notify_fd < 0) {
		err_log("inotify_init() failed!");
		notify_log_handler_started = 0;
		return NULL;
	}

	/* add all watch dir to list */
	info = notify_log_head;
	while(info) {
		/* misc_log level decided which log will be restored */
		if(info->level > misc_log->level) {
			info = info->next;
			continue;
		}
		ret = mkdir(info->content, S_IRWXU | S_IRWXG | S_IXOTH);
		if (-1 == ret && (errno != EEXIST)) {
			err_log("mkdir %s failed.", info->content);
			exit(0);
		}

		ret = chown(info->content, AID_SYSTEM, AID_SYSTEM);
		if (ret < 0) {
			err_log("chown failed.");
			exit(0);
		}

		debug_log("notify add watch %s\n", info->content);
		wd = inotify_add_watch(notify_fd, info->content, IN_MODIFY);
		if(wd == -1) {
			err_log("inotify_add_watch failed!");
			notify_log_handler_started = 0;
			return NULL;
		}
		info->interval = wd;
		info->current.tv_sec = 0;
		info->last.tv_sec = 0;
		info = info->next;
	}

	while(slog_enable == SLOG_ENABLE) {

		FD_ZERO(&readset);
		FD_SET(notify_fd, &readset);
		timeout.tv_sec = 3;
		timeout.tv_usec = 0;
		result = select(notify_fd + 1, &readset, NULL, NULL, &timeout);

		if(result == 0)
			continue;

		if(result < 0) {
			sleep(1);
			continue;
		}

		if(FD_ISSET(notify_fd, &readset) <= 0){
			continue;
                }

		/* when watched dir changed, event will be read */
		size = read(notify_fd, buffer, MAX_LINE_LEN);
		if(size <= 0) {
			debug_log("read inotify fd failed, %d.", (int)size);
			sleep(1);
			continue;
		}
		event = (struct inotify_event *)buffer;
		while(size > 0) {
			len = sizeof(struct inotify_event) + event->len;
			debug_log("notify event: wd: %d, mask %d, ret %d, len %d, file %s\n",
					event->wd, event->mask, (int)size, len, event->name);
			handle_notify_file(event->wd, event->name);
			event = (struct inotify_event *)((char *)event + len);
			size -= len;
		}
	}
	close(notify_fd);
	notify_log_handler_started = 0;
	return NULL;
}

/*
 * write form buffer.
 *
 */
static int write_from_buffer(int fd, char *buf, int len)
{
	int result = 0, err = 0;

	if(buf == NULL || fd < 0)
		return -1;

	if(len <= 0)
		return 0;

	while(result < len) {
		err = write(fd, buf + result, len - result);
		if(err < 0 && errno == EINTR)
			continue;
		if(err < 0)
			return err;
		result += err;
	}
	return result;
}

/*
 * open log devices
 *
 */
static void open_device(struct slog_info *info, char *path)
{
	info->fd_device = open(path, O_RDWR);
	if(info->fd_device < 0){
		err_log("Unable to open log device '%s', close '%s' log.", path, info->name);
		info->state = SLOG_STATE_OFF;
	}

	return;
}

/*
 * open output file
 *
 */
static int gen_outfd(struct slog_info *info)
{
	int cur, fd;
	char buffer[MAX_NAME_LEN];

	gen_logfile(buffer, info);
	write_modem_timestamp(info, buffer);
	fd = open(buffer, O_WRONLY | O_CREAT, S_IRUSR | S_IRGRP | S_IROTH);
	if(fd < 0){
		err_log("Unable to open file %s.",buffer);
		exit(0);
	}

	lseek(fd, 0, SEEK_END);
	cur = lseek(fd, 0, SEEK_CUR);
	info->outbytecount = cur;

	return fd;
}

/*
 *
 *
 */
static char* mstrncpy(char* dest, const char* src, size_t pos)
{
	strncpy(dest, src, pos);
	dest += pos;
	return dest;
}

/*
 * get current time and return strings
 *
 */
static void obtime(char *src, int len)
{
	time_t when;
	struct tm* tm;
	struct tm tmBuf;
	struct timeval tv;
	int ret;

	/* add timestamp */
	when = time(NULL);
	gettimeofday(&tv, NULL);
	tm = localtime_r(&when, &tmBuf);
	ret = strftime(src, len, "%m-%d %H:%M:%S", tm);
	sprintf(src + ret, ".%03d ", tv.tv_usec / 1000);
	return;
}

/*
 * search character '\n' in dest, insert "src" before all of '\n' into "dest"
 *
 */
static void strinst(char* dest, char* src)
{
	char *pos1, *pos2, *p;
	char time[MAX_NAME_LEN];

	p = dest;
	pos2 = src;

	obtime(time, sizeof(time));
	p = mstrncpy(p, time, strlen(time));
	pos1 = strchr(pos2, '\n');

	while(pos1 != NULL){
		p = mstrncpy(p, pos2, pos1 - pos2 + 1);
		if(strchr(pos1 + 1, '\n') == NULL)
			break;
		p = mstrncpy(p, time, strlen(time));
		pos2 = pos1 + 1;
		pos1 = strchr(pos2, '\n');
	}

	return;
}

/*
 * The file name to upgrade
 */
static void file_name_rotate(int num, char *buf)
{
	int i, err;

	for (i = num; i > 0 ; i--) {
		char *file0, *file1;

		err = asprintf(&file1, "%s.%d", buf, i);
		if(err == -1){
			err_log("asprintf return err!");
			exit(0);
		}
		if (i - 1 == 0) {
			err = asprintf(&file0, "%s", buf);
			if(err == -1){
				err_log("asprintf return err!");
				exit(0);
			}
		} else {
			err = asprintf(&file0, "%s.%d", buf, i - 1);
			if(err == -1){
				err_log("asprintf return err!");
				exit(0);
			}
		}

		err = rename (file0, file1);

		if (err < 0 && errno != ENOENT) {
			perror("while rotating log files");
		}

		free(file1);
		free(file0);
	}
}

/*
 *  File volume
 *
 *  When the file is written full, rename file to file.1
 *  and rename file.1 to file.2, and so on.
 */
static void rotatelogs(int num, struct slog_info *info)
{
	int err, i;
	char buffer[MAX_NAME_LEN];

	gen_logfile(buffer, info);

	close(info->fd_out);

	file_name_rotate(num, buffer);
	info->fd_out = gen_outfd(info);
	info->outbytecount = 0;
}

/*
 * Handler log file size according to the configuration file.
 *
 *
 */
static void log_size_handler(struct slog_info *info)
{
	if( !strncmp(current_log_path, INTERNAL_LOG_PATH, strlen(INTERNAL_LOG_PATH)) ) {
		if(info->outbytecount >= internal_log_size * 1024) {
			rotatelogs(INTERNAL_ROLLLOGS, info);
		}
		return;
	}

	if(info->max_size == 0) {
		if(info->outbytecount >= DEFAULT_MAX_LOG_SIZE * 1024 * 1024)
			rotatelogs(MAXROLLLOGS, info);
	} else {
		if(info->outbytecount >= info->max_size * 1024 * 1024) {
			lseek(info->fd_out, 0, SEEK_SET);
			info->outbytecount = 0;
		}
	}
}

/*
 * add timestamp when start logging.
 */
static void add_timestamp(struct slog_info *info)
{
	char buffer[MAX_NAME_LEN];
	int ret;
	time_t t;
	struct tm tm;
	t = time(NULL);
	localtime_r(&t, &tm);
	sprintf(buffer, "\n============  %02d-%02d-%02d %02d:%02d:%02d  ==============\n",
				tm.tm_year % 100,
				tm.tm_mon + 1,
				tm.tm_mday,
				tm.tm_hour,
				tm.tm_min,
				tm.tm_sec);
	write_from_buffer(info->fd_out, buffer, strlen(buffer));

	return;
}

/**********************************************************************************/
/***************************** for shark start ************************************/
/**********************************************************************************/

#define SMSG "/d/sipc/smsg"
#define SBUF "/d/sipc/sbuf"
#define SBLOCK "/d/sipc/sblock"

static void handle_dump_shark_sipc_info()
{
	char buffer[MAX_NAME_LEN];
	time_t t;
	struct tm tm;
	int ret;

	err_log("Start to dump SIPC info.");
	t = time(NULL);
	localtime_r(&t, &tm);

	sprintf(buffer, "%s/%s/misc/sipc",
			current_log_path,
			top_logdir);
	ret = mkdir(buffer, S_IRWXU | S_IRWXG | S_IRWXO);
	if (-1 == ret && (errno != EEXIST)){
		err_log("mkdir %s failed.", buffer);
		exit(0);
	}

	sprintf(buffer, "%s/%s/misc/sipc/%02d%02d%02d_smsg.log",
			current_log_path,
			top_logdir,
			tm.tm_hour,
			tm.tm_min,
			tm.tm_sec);
	cp_file(SMSG, buffer);

	sprintf(buffer, "%s/%s/misc/sipc/%02d%02d%02d_sbuf.log",
			current_log_path,
			top_logdir,
			tm.tm_hour,
			tm.tm_min,
			tm.tm_sec);
	cp_file(SBUF, buffer);

	sprintf(buffer, "%s/%s/misc/sipc/%02d%02d%02d_sblock.log",
			current_log_path,
			top_logdir,
			tm.tm_hour,
			tm.tm_min,
			tm.tm_sec);
	cp_file(SBLOCK, buffer);
}


#define MODEM_TD_DEVICE_PROPERTY "persist.modem.t.enable"
#define MODEM_W_DEVICE_PROPERTY "persist.modem.w.enable"

static void handle_dump_shark_modem_memory()
{
	char buffer[MAX_NAME_LEN];
	time_t t;
	struct tm tm;
	char modem_property[PROPERTY_VALUE_MAX];
	int ret_t, ret_w;

	err_log("Start to dump CP memory for shark.");

	property_get(MODEM_TD_DEVICE_PROPERTY, modem_property, "");
	ret_t = atoi(modem_property);
	property_get(MODEM_W_DEVICE_PROPERTY, modem_property, "");
	ret_w = atoi(modem_property);

	/* add timestamp */
	t = time(NULL);
	localtime_r(&t, &tm);
	if(ret_t == 1) {
		sprintf(buffer, "cat /proc/cpt/mem > %s/%s/modem/modem_memory_%d%02d%02d%02d%02d%02d.log", current_log_path, top_logdir,
				tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	} else if(ret_w == 1) {
		sprintf(buffer, "cat /proc/cpw/mem > %s/%s/modem/modem_memory_%d%02d%02d%02d%02d%02d.log", current_log_path, top_logdir,
				tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	} else {
		err_log("Modem device is not TD and W!");
	}
	system(buffer);
	err_log("Dump CP memory completed for shark.");
}

/**********************************************************************************/
/***************************** for shark end **************************************/
/**********************************************************************************/

#define MODEMRESET_PROPERTY "persist.sys.sprd.modemreset"
#define MODEM_SOCKET_NAME       "modemd"
#define MODEM_SOCKET_BUFFER_SIZE 128
static int dump_modem_memory_flag = 0;
static int modem_reset_flag = 0;
static int modem_thread_first_start = 1;
static struct slog_info *info_modem;

void *modem_dump_memory_handler(void *arg)
{
	char modemrst_property[PROPERTY_VALUE_MAX];
        char cmddumpmemory[2]={'t',0x0a};
	int soc_fd, ret, n, err;
	char buffer[MODEM_SOCKET_BUFFER_SIZE];

connect_socket:
	memset(modemrst_property, 0, sizeof(modemrst_property));
	property_get(MODEMRESET_PROPERTY, modemrst_property, "");
	ret = atoi(modemrst_property);
	err_log("%s is %s, ret=%d", MODEMRESET_PROPERTY, modemrst_property, ret);

        do {
                soc_fd = socket_local_client( MODEM_SOCKET_NAME,
                                ANDROID_SOCKET_NAMESPACE_ABSTRACT, SOCK_STREAM);
                err_log("bind server %s,soc_fd=%d", MODEM_SOCKET_NAME, soc_fd);
                sleep(10);
        } while(soc_fd < 0);

        for(;;) {
                memset(buffer, 0, MODEM_SOCKET_BUFFER_SIZE);
                sleep(1);
                n = read(soc_fd, buffer, MODEM_SOCKET_BUFFER_SIZE);
                if( n > 0 ) {
			err_log("get %d bytes %s", n, buffer);
			if(strstr(buffer, "Modem Assert") != NULL) {
				if(ret == 0) {
					dump_modem_memory_flag = 1;
					/* for shark dump sipc info */
					if(dev_shark_flag == 1)
						handle_dump_shark_sipc_info();
				} else {
					modem_reset_flag =1;
					err = pthread_kill(modem_tid, SIGUSR1);
					if(err != 0)
						err_log("pthread_kill failed");
					if(info_modem->fd_device)
						close(info_modem->fd_device);
					if(info_modem->fd_out)
						close(info_modem->fd_out);
					modem_log_handler_started = 0;
					err_log("waiting for Modem Alive.");
				}
			} else if(strstr(buffer, "Modem Alive") != NULL) {
				if(modem_reset_flag == 1)
					pthread_create(&modem_tid, NULL, modem_log_handler, NULL);
				modem_reset_flag = 0;
			} else if(strstr(buffer, "Modem Blocked") != NULL) {
				dump_modem_memory_flag = 1;
				/* for shark dump sipc info */
				if(dev_shark_flag == 1)
					handle_dump_shark_sipc_info();
			}
                } else if(n == 0) {
			err_log("get 0 bytes, sleep 10s, reconnect socket.");
			sleep(10);
			close(soc_fd);
			goto connect_socket;
		}
        }

        close(soc_fd);
}

static void handle_dump_modem_file(struct slog_info *info)
{
	int fd;
	int ret,n;
	int finish = 0, receive_from_cp = 0;
	char buffer[SINGLE_BUFFER_SIZE];
	char path[MAX_NAME_LEN];
	time_t t;
	struct tm tm;
	fd_set readset;
	int result;
	struct timeval timeout;
	char cmddumpmemory[2]={'3',0x0a};

	err_log("Start to dump CP memory.");

write_cmd:
	n = write(info->fd_device, cmddumpmemory, 2);
	if (n <= 0) {
		close(info->fd_device);
		sleep(1);
		open_device(info, MODEM_LOG_SOURCE);
		goto write_cmd;
	}

	/* add timestamp */
	t = time(NULL);
	localtime_r(&t, &tm);
	sprintf(path, "%s/%s/modem/modem_memory_%d%02d%02d%02d%02d%02d.log", current_log_path, top_logdir,
				tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	fd = open(path, O_WRONLY | O_CREAT, S_IRUSR | S_IRGRP | S_IROTH);
	if (fd < 0) {
		err_log("open modem memory file failed!");
		return;
	}

	do {
		memset(buffer,0,SINGLE_BUFFER_SIZE);
                FD_ZERO(&readset);
                FD_SET(info->fd_device, &readset);
                timeout.tv_sec = 3;
                timeout.tv_usec = 0;
		ret = select(info->fd_device + 1, &readset, NULL, NULL, &timeout);

		if( 0 == ret ){
			/* for shark, when CP can not send integral log to AP, slog will use another way to save CP memory*/
			if( (receive_from_cp < 10 ) && (dev_shark_flag == 1) )
				handle_dump_shark_modem_memory();
			err_log("select timeout ->save finsh");
			finish = 1;
		} else if( ret > 0 ) {
read_again:
			n = read(info->fd_device, buffer, SINGLE_BUFFER_SIZE);

			if (n == 0) {
				close(info->fd_device);
				sleep(1);
				open_device(info, MODEM_LOG_SOURCE);
			} else if (n < 0) {
				err_log("fd=%d read %d is lower than 0", info->fd_device, n);
				sleep(1);
				goto read_again;
			} else {
				receive_from_cp ++;
				write_from_buffer(fd, buffer, n);
			}
		} else {
			err_log("select error");
		}
	} while( finish == 0 );

	err_log("Dump CP memory completed.");

	return;
}

static void modem_thread_sig_handler(int sig)
{
	err_log("get a signal %d.", sig);
	pthread_exit(0);
}

static void setup_signal()
{
	struct sigaction act;

	memset(&act, 0, sizeof(act));
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;

#define SIGNAL(s, handler)      do { \
		act.sa_handler = handler; \
		if (sigaction(s, &act, NULL) < 0) \
			err_log("Couldn't establish signal handler (%d): %m", s); \
	} while (0)

	SIGNAL(SIGUSR1, modem_thread_sig_handler);

	return;
}

void *modem_log_handler(void *arg)
{
	struct slog_info *info;
	int buffer_start = 0, buffer_end = 0;
	int buffer_len[RING_BUFFER_NUM];
	unsigned char *ring_buffer_table;
	int ret;
	int data_fd;
	char buffer[MAX_NAME_LEN];
	time_t t;
	struct tm tm;
	fd_set readset;
	int result;
	struct timeval timeout;

	setup_signal();

	if(slog_enable == SLOG_DISABLE)
		return NULL;

	info = stream_log_head;
	while (info) {
		if (strncmp(info->name, "modem", 5)) {
			info = info->next;
			continue;
		}

		if(info->state == SLOG_STATE_ON) {
			if(slog_enable == SLOG_ENABLE)
				info->fd_out = gen_outfd(info);
			else
				info->fd_out = -1;
		} else
			return NULL;

		open_device(info, MODEM_LOG_SOURCE);

		break;
	}

	if(info == NULL) {
		err_log("modem log disabled!");
		return NULL;
	}

	info_modem = info;
	if(modem_thread_first_start == 1) {
		pthread_create(&modem_dump_memory_tid, NULL, modem_dump_memory_handler, NULL);
		modem_thread_first_start = 0;
	}

	ring_buffer_table = malloc(MODEM_CIRCULAR_SIZE);

	modem_log_handler_started = 1;
	while(slog_enable != SLOG_DISABLE) {

		if(dump_modem_memory_flag == 1) {
			err_log("Modem Assert!");
			handle_dump_modem_file(info);
			dump_modem_memory_flag = 0;
		}

		FD_ZERO(&readset);
		FD_SET(info->fd_device, &readset);
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;
		result = select(info->fd_device + 1, &readset, NULL, NULL, &timeout);

		if(result == 0)
			continue;

		if(result < 0) {
			sleep(1);
			continue;
		}

		if(FD_ISSET(info->fd_device, &readset) <= 0){
			continue;
                }

		memset((char *)ring_buffer_table + buffer_end * SINGLE_BUFFER_SIZE, 0, SINGLE_BUFFER_SIZE);

		ret = read(info->fd_device, (char *)ring_buffer_table + buffer_end * SINGLE_BUFFER_SIZE, SINGLE_BUFFER_SIZE);
		if (ret <= 0) {
			if(errno != EAGAIN) {
				err_log("read modem log failed.");
				close(info->fd_device);
				sleep(20);
				open_device(info, MODEM_LOG_SOURCE);
			}
			sleep(1);
			continue;
		}
		buffer_len[buffer_end] = ret;

		if(info->fd_out != -1) {
			ret = write_from_buffer(info->fd_out, (char *)ring_buffer_table + buffer_end * SINGLE_BUFFER_SIZE,
				buffer_len[buffer_end]);
			info->outbytecount += ret;
			log_size_handler(info);
		}

		/* handle ring_buffer pointer */
		buffer_end = (buffer_end + 1) % RING_BUFFER_NUM;
		if(buffer_end == buffer_start)
			buffer_start = (buffer_start + 1) % RING_BUFFER_NUM;

		/* hook modem log */
		if(hook_modem_flag != 1)
			continue;

		/* add timestamp */
		t = time(NULL);
		localtime_r(&t, &tm);
		sprintf(buffer, "/data/log/modem_%d%02d%02d%02d%02d%02d.log",
					tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

		data_fd = open(buffer, O_WRONLY | O_CREAT, S_IRUSR | S_IRGRP | S_IROTH);
		if(data_fd < 0) {
			err_log("open modem log failed!");
			break;
		}

		/*handle hook_modem_log*/
		while(1) {
			ret = write_from_buffer(data_fd, (char *)ring_buffer_table + buffer_start * SINGLE_BUFFER_SIZE,
				buffer_len[buffer_start]);
			buffer_start = (buffer_start + 1) % RING_BUFFER_NUM;
			if(buffer_end == buffer_start)
				break;
		}
		close(data_fd);
		hook_modem_flag = 0;
	}

	free(ring_buffer_table);
	ring_buffer_table = NULL;

	/* close all open fds */
	if(info->fd_device)
		close(info->fd_device);
	if(info->fd_out)
		close(info->fd_out);
	modem_log_handler_started = 0;

	return NULL;

}

static AndroidLogFormat * g_logformat;
static EventTagMap* g_eventTagMap = NULL;

void *stream_log_handler(void *arg)
{
	struct slog_info *info;
	int max = 0, ret, result;
	fd_set readset_tmp, readset;
	char buf[LOGGER_ENTRY_MAX_LEN+1], buf_kmsg[LOGGER_ENTRY_MAX_LEN], wbuf_kmsg[LOGGER_ENTRY_MAX_LEN *2];
	struct logger_entry *entry;
	AndroidLogEntry entry_write;
	static AndroidLogPrintFormat format;
	char devname[MAX_NAME_LEN];
	struct timeval timeout;

	stream_log_handler_started = 1;

	info = stream_log_head;
	FD_ZERO(&readset_tmp);
	/*open all of the stream devices*/
	while(info){
		if(info->state != SLOG_STATE_ON){
			info = info->next;
			continue;
		}
		if(!strncmp(info->name, "kernel", 6)) {
			open_device(info, KERNEL_LOG_SOURCE);
			info->fd_out = gen_outfd(info);
			add_timestamp(info);
		} else if( !strncmp(info->name, "main", 4) || !strncmp(info->name, "system", 6)
			|| !strncmp(info->name, "radio", 5) || !strncmp(info->name, "events", 6) ) {
			sprintf(devname, "%s/%s", "/dev/log", info->name);
			open_device(info, devname);
			info->fd_out = gen_outfd(info);
			add_timestamp(info);
			if ( !strncmp(info->name, "events", 6) )
				g_eventTagMap = android_openEventTagMap(EVENT_TAG_MAP_FILE);
		} else {
			info = info->next;
			continue;
		}

		FD_SET(info->fd_device, &readset_tmp);

		/*find the max fd*/
		if(info->fd_device > max)
			max = info->fd_device;

		info = info->next;
	}

	/*Initialize android log format*/
	g_logformat = android_log_format_new();
	format = android_log_formatFromString("threadtime");
	android_log_setPrintFormat(g_logformat, format);

	while(slog_enable == SLOG_ENABLE) {
		FD_ZERO(&readset);
		timeout.tv_sec = 3;
		timeout.tv_usec = 0;
		memcpy(&readset, &readset_tmp, sizeof(readset_tmp));
		result = select(max + 1, &readset, NULL, NULL, &timeout);

		/* timeout */
		if (result == 0)
			continue;

		/* error */
		if (result < 0) {
			sleep(1);
			continue;
		}

		info = stream_log_head;
		while(info) {

			if(info->state != SLOG_STATE_ON){
				info = info->next;
				continue;
			}

			if(FD_ISSET(info->fd_device, &readset) <= 0){
				info = info->next;
				continue;
			}

			if(!strncmp(info->name, "kernel", 6)){
				memset(buf_kmsg, 0, LOGGER_ENTRY_MAX_LEN);
				memset(wbuf_kmsg, 0, LOGGER_ENTRY_MAX_LEN *2);
				ret = read(info->fd_device, buf_kmsg, LOGGER_ENTRY_MAX_LEN);
				if(ret <= 0) {
					if ( (ret == -1 && (errno == EINTR || errno == EAGAIN) ) || ret == 0 ) {
						info = info->next;
						continue;
					}
					err_log("read %s log failed!", info->name);
					close(info->fd_device);
					sleep(1);
					open_device(info, KERNEL_LOG_SOURCE);
					info = info->next;
					continue;
				}
				strinst(wbuf_kmsg, buf_kmsg);

				ret = write_from_buffer(info->fd_out, wbuf_kmsg, strlen(wbuf_kmsg));
				if(ret == -1) {
					close(info->fd_out);
					sleep(1);
					info->fd_out = gen_outfd(info);
					add_timestamp(info);
					info = info->next;
					continue;
				}
				info->outbytecount += ret;
				log_size_handler(info);
			} else if(!strncmp(info->name, "main", 4) || !strncmp(info->name, "system", 6)
				|| !strncmp(info->name, "radio", 5) || !strncmp(info->name, "events", 6) ) {
				ret = read(info->fd_device, buf, LOGGER_ENTRY_MAX_LEN);
				if(ret <= 0) {
					if ( (ret == -1 && (errno == EINTR || errno == EAGAIN) ) || ret == 0 ) {
						info = info->next;
						continue;
					}
					err_log("read %s log failed!", info->name);
					close(info->fd_device);
					sleep(1);
					sprintf(devname, "%s/%s", "/dev/log", info->name);
					open_device(info, devname);
					info = info->next;
					continue;
                		}

				entry = (struct logger_entry *)buf;
				entry->msg[entry->len] = '\0';
				/*add android log 'tag' and other format*/
				if ( !strncmp(info->name, "events", 6) )
					ret = android_log_processBinaryLogBuffer(entry, &entry_write, g_eventTagMap, buf_kmsg, sizeof(buf_kmsg));
				else
					ret = android_log_processLogBuffer(entry, &entry_write);
				if ( ret < 0 ) {
					info = info->next;
					continue;
				}
				/* write log to file */
				ret = android_log_printLogLine(g_logformat, info->fd_out, &entry_write);
				if ( ret < 0 ) {
					close(info->fd_out);
					sleep(1);
					info->fd_out = gen_outfd(info);
					add_timestamp(info);
					info = info->next;
					continue;
				}

				info->outbytecount += ret;
				log_size_handler(info);
			}
			info = info->next;
		}
	}

	/* close all open fds */
	info = stream_log_head;
	while(info) {
		if(info->fd_device)
			close(info->fd_device);
		if(info->fd_out)
			close(info->fd_out);
		info = info->next;
	}

	android_closeEventTagMap(g_eventTagMap);
	stream_log_handler_started = 0;

	return NULL;
}

void *bt_log_handler(void *arg)
{
	struct slog_info *info = NULL, *bt = NULL;
	char buffer[MAX_NAME_LEN];
	int ret, i, err;
	pid_t pid;

	info = stream_log_head;
	while(info){
		if( (info->state == SLOG_STATE_ON) && !strncmp(info->name, "bt", 2) ){
			bt = info;
			break;
		}
		info = info->next;
	}

	if( !bt)
		return NULL;

	if( !strncmp(current_log_path, INTERNAL_LOG_PATH, strlen(INTERNAL_LOG_PATH)) ) {
		bt->state = SLOG_STATE_OFF;
		return NULL;
	}

	bt_log_handler_started = 1;
	pid = fork();
	if(pid < 0){
		err_log("fork error!");
	}

	if(pid == 0){
		sprintf(buffer, "%s/%s/%s", current_log_path, top_logdir, bt->log_path);
		ret = mkdir(buffer, S_IRWXU | S_IRWXG | S_IRWXO);
		if (-1 == ret && (errno != EEXIST)){
			err_log("mkdir %s failed.", buffer);
			exit(0);
		}
		sprintf(buffer, "%s/%s/%s/%s.log",
			current_log_path, top_logdir, bt->log_path, bt->log_basename);
		file_name_rotate(MAXROLLLOGS, buffer);

#ifdef SLOG_BTLOG_235
		execl("/system/xbin/hcidump", "hcidump", "-Bw", buffer, (char *)0);
#else
		execl("/system/xbin/hcidump", "hcidump", "-w", buffer, (char *)0);
#endif
		exit(0);
	}

	while(slog_enable == SLOG_ENABLE){
		sleep(1);
	}

	kill(pid, SIGTERM);
	bt_log_handler_started = 0;

	return NULL;
}

void *tcp_log_handler(void *arg)
{
	struct slog_info *info = NULL, *tcp = NULL;
	char buffer[MAX_NAME_LEN];
	int ret, i, err;
	pid_t pid;

	info = stream_log_head;
	while(info){
		if((info->state == SLOG_STATE_ON) && !strncmp(info->name, "tcp", 2)) {
			tcp = info;
			break;
		}
		info = info->next;
	}

	if( !tcp)
		return NULL;

	if(!strncmp(current_log_path, INTERNAL_LOG_PATH, strlen(INTERNAL_LOG_PATH))) {
		tcp->state = SLOG_STATE_OFF;
		return NULL;
	}

	tcp_log_handler_started = 1;
	pid = fork();
	if(pid < 0){
		err_log("fork error!");
	}

	if(pid == 0){
		sprintf(buffer, "%s/%s/%s", current_log_path, top_logdir, tcp->log_path);
		ret = mkdir(buffer, S_IRWXU | S_IRWXG | S_IRWXO);
		if (-1 == ret && (errno != EEXIST)){
			err_log("mkdir %s failed.", buffer);
			exit(0);
		}
		sprintf(buffer, "%s/%s/%s/%s.pcap",
			current_log_path, top_logdir, tcp->log_path, tcp->log_basename);
		file_name_rotate(MAXROLLLOGS, buffer);

		execl("/system/bin/tcp", "tcp", "-i", "any", "-p", "-s 0", "-w", buffer, (char *)0);
		exit(0);
	}

	while(slog_enable == SLOG_ENABLE){
		sleep(1);
	}

	kill(pid, SIGTERM);
	tcp_log_handler_started = 0;

	return NULL;
}

void *uboot_log_handler(void *arg)
{
	int fd = -1;
	FILE *fout = NULL;
	void *va = NULL;
	char buffer[MAX_LINE_LEN];
	char *s1, *s2;
	unsigned long pa_start = 0;
	unsigned long data_sz = 0;
	int ret;

        /* misc_log on/off state will control all snapshot log */
        if(misc_log->state != SLOG_STATE_ON)
                return NULL;

	debug_log("Start dump uboot log..");

	/* Check cmdline to find the location of uboot log */
	if ((fd = open("/proc/cmdline", O_RDONLY)) == -1) {
		err_log("open proc file cmdline error!");
		return NULL;
	}

	ret = read(fd, buffer, sizeof(buffer));
	if (ret < 0) {
		err_log("read cmdline error");
		return NULL;
	}

	/* parse address and size, format: boot_ram_log=0x..., 0x...*/
	if ((s1 = strstr(buffer, "boot_ram_log=")) != NULL) {
		s2= strsep(&s1, "="); /*0x=...*/
		if ((s2 = strsep(&s1, ",")) != NULL) {
			pa_start = strtoll(s2, NULL, 16);
			if ((s2 = strsep(&s1, " ")) != NULL)
				data_sz = strtoll(s2, NULL, 16);
		}
	}
	else {
		err_log("can not get boot_ram_log flag in cmdline");
		return NULL;
	}

	close(fd);

	debug_log("boot_ram_log base=%#010x, size=%#x", (unsigned int)pa_start, (unsigned int)data_sz);

	/* map memory region from physcial address */
        if ((fd = open("/dev/mem", O_RDWR | O_SYNC)) == -1) {
                err_log("could not open /dev/mem/");
                return NULL;
        }

        va = (char*)mmap(0, data_sz, PROT_READ | PROT_WRITE,
                        MAP_SHARED, fd, pa_start);
        if (va == MAP_FAILED) {
                err_log("can not map region!");
		return NULL;
        }

	sprintf(buffer, "%s/%s/misc/uboot.log",
			current_log_path,
			top_logdir);

	if ((fout = fopen(buffer, "w")) == NULL) {
		err_log("create log file error!");
		return NULL;
	}

	ret = fwrite(va, 1, data_sz, fout);
	debug_log("%#x bytes were written to file.", ret);

	if (munmap(va, data_sz) == MAP_FAILED) {
		err_log("munmap failed!");
	}

	close(fd);

	fclose(fout);

	return NULL;
}


extern int enable_kmemleak;
void *kmemleak_handler(void *arg)
{
	int i = 0;
	int retry = 0;
	int retry_open = 0;
	int ret;
	int n_read, n_write;
	char writecmd[16];
	char buf[1024];
	int fd_ml = 0;
	char buffer[MAX_NAME_LEN];
	int fd_dmp;
	struct slog_info *info = NULL,*kmemleak = NULL;

	info = stream_log_head;
	while(info){
		if((info->state == SLOG_STATE_ON) && !strncmp(info->name, "kmemleak", 8)) {
			kmemleak = info;
			break;
		}
		info = info->next;
	}

	kmemleak_handler_started = 1;
	memset(writecmd, 0, 16);
	strcpy(writecmd, "scan");

	while(1)
	{
		if(!enable_kmemleak)
		{
			debug_log("kmemleak disable\n");
			sleep(60);
			continue;
		}

		fd_ml = open("/sys/kernel/debug/kmemleak", O_RDWR);
label0:
		if(fd_ml == -1)
		{
			err_log("open device error, errno=%d\n", errno);
			if(errno == ENOENT)
			{
				retry_open++;
				sleep(10);
				if(retry_open == 60)
				{
					err_log("need open kconfig support\n");
					break;
				}
				continue;
			}
			else
			{
				break;
			}
		}
		retry_open = 0;
		n_write = write(fd_ml, (char *)writecmd, strlen((char *)writecmd));
		if(n_write <= 0){
			err_log("write device error, need rewrite, errno=%d\n", errno);
			if(errno == EBUSY)
			{
				retry++;
				close(fd_ml);
				sleep(1);
				if(retry == 4)
				{
					err_log("device always busy, exit\n");					
					break;
				}
				fd_ml = open("/sys/kernel/debug/kmemleak", O_RDWR);
				goto label0;
			}
	    }
	    retry = 0;
label1:
		n_read = read(fd_ml, buf, 1024);
		if(n_read > 0)
		{
			memset(buffer, 0, MAX_NAME_LEN);
			sprintf(buffer, "%s/%s/%s", current_log_path, top_logdir, kmemleak->log_path);
			ret = mkdir(buffer, S_IRWXU | S_IRWXG | S_IRWXO);
			if (-1 == ret && (errno != EEXIST)){
				err_log("mkdir %s failed.\n", buffer);
				return NULL;
			}
			sprintf(buffer, "%s/%s/%s/%s_%d", current_log_path, top_logdir, kmemleak->log_path, kmemleak->log_basename, i);
			fd_dmp = open((const char *)buffer, O_CREAT|O_RDWR, 0x666);
			write_from_buffer(fd_dmp, buf, n_read);
			while(n_read)
			{
label2:
				n_read = read(fd_ml, buf, 1024);
				if(n_read > 0)
				{
					write_from_buffer(fd_dmp, buf, n_read);
				}
				else if(n_read < 0)
				{
					err_log("1:fd=%d read %d is lower than 0\n", fd_ml, n_read);
					sleep(1);
					goto label2;
				}
			}
			close(fd_dmp);
		}
		else if(n_read < 0)
		{
			err_log("2:fd=%d read %d is lower than 0\n", fd_ml, n_read);
			sleep(1);
			goto label1;
		}
		close(fd_ml);
		fd_ml = 0;
		sleep(60);
		i++;
	}
	kmemleak_handler_started = 0;
	return NULL;
}
