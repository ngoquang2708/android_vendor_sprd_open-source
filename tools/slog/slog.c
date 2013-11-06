/*
 * Copyright (C) 2012 Spreadtrum Communications Inc.
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
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/uio.h>
#include <dirent.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <cutils/properties.h>

#include "slog.h"

int slog_enable = SLOG_ENABLE;
int screenshot_enable = 1;
int slog_start_step = 0;
int slog_init_complete = 0;
int stream_log_handler_started = 0;
int snapshot_log_handler_started = 0;
int notify_log_handler_started = 0;
int bt_log_handler_started = 0;
int tcp_log_handler_started = 0;
int kmemleak_handler_started = 0;
int modem_log_handler_started = 0;

int internal_log_size = 5 * 1024; /*M*/

int hook_modem_flag = 0;
int dev_shark_flag = 0;

char *config_log_path = INTERNAL_LOG_PATH;
char *current_log_path;
char top_logdir[MAX_NAME_LEN];
char external_storage[MAX_NAME_LEN];
char external_path[MAX_NAME_LEN];

struct slog_info *stream_log_head, *snapshot_log_head;
struct slog_info *notify_log_head, *misc_log;

pthread_t stream_tid, snapshot_tid, notify_tid, sdcard_tid, command_tid, bt_tid, tcp_tid, modem_tid, modem_dump_memory_tid, uboot_log_tid, kmemleak_tid;

static void handler_exec_cmd(struct slog_info *info, char *filepath)
{
	FILE *fp;
	char buffer[MAX_NAME_LEN];
	int ret;
	time_t t;
	struct tm tm;

	fp = fopen(filepath, "a+");
	if(fp == NULL) {
		err_log("open file %s failed!", filepath);
		return;
	}

	/* add timestamp */
	t = time(NULL);
	localtime_r(&t, &tm);
	fprintf(fp, "\n============ %s  %02d-%02d-%02d %02d:%02d:%02d  ==============\n",
			info->log_basename,
			tm.tm_year % 100,
			tm.tm_mon + 1,
			tm.tm_mday,
			tm.tm_hour,
			tm.tm_min,
			tm.tm_sec);

	fclose(fp);
	sprintf(buffer, "%s >> %s", info->content, filepath);
	system(buffer);
	return;
}

static void handler_dump_file(struct slog_info *info, char *filepath)
{
	FILE *fcmd, *fp;
	int ret;
	time_t t;
	struct tm tm;
	char buffer[4096];

	fp = fopen(filepath, "a+");
	if(fp == NULL) {
		err_log("open file %s failed!", filepath);
		return;
	}

	fcmd = fopen(info->content, "r");
	if(fcmd == NULL) {
		err_log("open target %s failed!", info->content);
		fclose(fp);
		return;
	}

	/* add timestamp */
	t = time(NULL);
	localtime_r(&t, &tm);
	fprintf(fp, "\n============ %s  %02d-%02d-%02d %02d:%02d:%02d  ==============\n",
			info->log_basename,
			tm.tm_year % 100,
			tm.tm_mon + 1,
			tm.tm_mday,
			tm.tm_hour,
			tm.tm_min,
			tm.tm_sec);
	/* recording... */
	while( (ret = fread(buffer, 1, 4096, fcmd)) > 0)
		fwrite(buffer, 1, ret, fp);

	/*Separate treating apanic, copy after delete apanic*/
	if(!strncmp("apanic_console", info->name, 14) || !strncmp("apanic_threads", info->name, 14)){
		sprintf(buffer, "rm -r %s", info->content);
		system(buffer);
	}

	fclose(fcmd);
	fclose(fp);

	return;
}

void exec_or_dump_content(struct slog_info *info, char *filepath)
{
	int ret;
	char buffer[MAX_NAME_LEN];

	/* slog_enable on/off state will control all snapshot log */
	if(slog_enable != SLOG_ENABLE)
		return;

	/* misc_log on/off state will control all snapshot log */
	if(misc_log->state != SLOG_STATE_ON)
		return;

	/* setup log file first */
	if( filepath == NULL ) {
		sprintf(buffer, "%s/%s/%s", current_log_path, top_logdir, info->log_path);
		ret = mkdir(buffer, S_IRWXU | S_IRWXG | S_IRWXO);
		if(-1 == ret && (errno != EEXIST)){
			err_log("mkdir %s failed.", buffer);
			exit(0);
		}
		sprintf(buffer, "%s/%s/%s/%s.log",
			current_log_path, top_logdir, info->log_path, info->log_basename);
	} else {
		strcpy(buffer, filepath);
	}

	if(!strncmp(info->opt, "cmd", 3)) {
		handler_exec_cmd(info, buffer);
	} else {
		handler_dump_file(info, buffer);
	}

	return;
}

int capture_by_name(struct slog_info *head, const char *name, char *filepath)
{
	struct slog_info *info = head;

	while(info) {
		if(!strncmp(info->name, name, strlen(name))) {
			exec_or_dump_content(info, filepath);
			return 0;
		}
		info = info->next;
	}
	return 0;
}

static int capture_snap_for_last(struct slog_info *head)
{
	struct slog_info *info = head;
	while(info) {
		if(info->level == 7)
			exec_or_dump_content(info, NULL);
		info = info->next;
	}
	return 0;
}


static int capture_all(struct slog_info *head)
{
	char filepath[MAX_NAME_LEN];
	int ret;
	time_t t;
	struct tm tm;
	struct slog_info *info = head;

	/* slog_enable on/off state will control all snapshot log */
	if(slog_enable != SLOG_ENABLE)
		return 0;

	if(!head)
		return 0;

	sprintf(filepath, "%s/%s/%s", current_log_path, top_logdir, info->log_path);
	ret = mkdir(filepath, S_IRWXU | S_IRWXG | S_IRWXO);
	if(-1 == ret && (errno != EEXIST)) {
		err_log("mkdir %s failed.", filepath);
		exit(0);
	}

	t = time(NULL);
	localtime_r(&t, &tm);
	sprintf(filepath, "%s/%s/%s/snapshot_%02d%02d%02d.log",
				current_log_path,
				top_logdir,
				info->log_path,
				tm.tm_hour,
				tm.tm_min,
				tm.tm_sec
	);

	while(info) {
		if(info->level <= 6)
			exec_or_dump_content(info, filepath);
		info = info->next;
	}
	return 0;
}

static void handler_last_dir()
{
	DIR *p_dir;
	struct dirent *p_dirent;
	int log_num = 0, last_flag =0, ret;
	char buffer[MAX_NAME_LEN];

	if(( p_dir = opendir(current_log_path)) == NULL) {
		err_log("can not open %s.", current_log_path);
		return;
	}

	while((p_dirent = readdir(p_dir))) {
		if( !strncmp(p_dirent->d_name, "20", 2) ) {
			log_num += 1;
		}else if( !strncmp(p_dirent->d_name, LAST_LOG, 3) ) {
			last_flag = 1;
		}
	}

	if(log_num < LOG_DIR_NUM){
		closedir(p_dir);
		return;
	}

	if(last_flag == 1) {
		sprintf(buffer, "rm -r %s/%s/", current_log_path, LAST_LOG);
		system(buffer);
	}

	sprintf(buffer, "%s/%s/", current_log_path, LAST_LOG);
	ret = mkdir(buffer, S_IRWXU | S_IRWXG | S_IRWXO);
	if(-1 == ret && (errno != EEXIST)) {
		err_log("mkdir %s failed.", buffer);
		exit(0);
	}

	sprintf(buffer, "%s %s/%s %s/%s", "mv", current_log_path, "20*", current_log_path, LAST_LOG);
	debug_log("%s\n", buffer);
	system(buffer);

	sprintf(buffer, "%s %s/%s", "rm -r", current_log_path, "20*");
	system(buffer);

	closedir(p_dir);
	return;
}

/*
 * cp /data/slog/20* to external_storage.
 */
static int cp_internal_to_external()
{
	DIR *p_dir;
	struct dirent *p_dirent;
	char buffer[MAX_NAME_LEN];

	if(( p_dir = opendir(INTERNAL_LOG_PATH)) == NULL) {
		err_log("can not open %s.", current_log_path);
		return 0;
	}

	while((p_dirent = readdir(p_dir))) {
		if( !strncmp(p_dirent->d_name, "20", 2) ) {
			strcpy(top_logdir, p_dirent->d_name);
			sprintf(buffer, "tar c %s | tar x -C %s/", p_dirent->d_name, external_storage);
			if(chdir(INTERNAL_LOG_PATH) == -1){
				err_log("chdir %s failed!", INTERNAL_LOG_PATH);
			}
			system(buffer);
			debug_log("%s", buffer);
			closedir(p_dir);
			return 1;
		}
	}

	closedir(p_dir);
	return 0;
}

static void handler_modem_memory_log()
{
	char path[MAX_NAME_LEN];
	struct stat st;

	sprintf(path, "%s/modem_memory.log", external_path);
	if(!stat(path, &st)) {
		sprintf(path, "mv %s/modem_memory.log %s/%s/misc/", external_path, current_log_path, top_logdir);
		system(path);
	}
}

static void create_log_dir()
{
	time_t when;
	struct tm start_tm;
	char path[MAX_NAME_LEN];
	int ret = 0;

	handler_last_dir();

	if(slog_start_step == 1 && cp_internal_to_external() == 1)
		return;

	/* generate log dir */
	when = time(NULL);
	localtime_r(&when, &start_tm);
	sprintf(top_logdir, "20%02d%02d%02d%02d%02d%02d",
						start_tm.tm_year % 100,
						start_tm.tm_mon + 1,
						start_tm.tm_mday,
						start_tm.tm_hour,
						start_tm.tm_min,
						start_tm.tm_sec);


	sprintf(path, "%s/%s", current_log_path, top_logdir);

	ret = mkdir(path, S_IRWXU | S_IRWXG | S_IRWXO);
	if (-1 == ret && (errno != EEXIST)) {
		err_log("mkdir %s failed.", path);
		exit(0);
	}

	debug_log("%s\n",top_logdir);
	return;
}

static void use_ori_log_dir()
{
	DIR *p_dir;
	struct dirent *p_dirent;
	int log_num = 0;

	if(( p_dir = opendir(current_log_path)) == NULL) {
		err_log("Can't open %s.", current_log_path);
		return;
	}

	while((p_dirent = readdir(p_dir))) {
		if( !strncmp(p_dirent->d_name, "20", 2) ) {
			strcpy(top_logdir, p_dirent->d_name);
			log_num = 1;
			debug_log("%s\n",top_logdir);
			break;
		}
	}

	if(log_num == 0)
		create_log_dir();

	closedir(p_dir);
	return;
}

static int handle_low_power()
{
	if(slog_enable == SLOG_DISABLE)
		return 0;

	if(!modem_log_handler_started) {
		pthread_create(&modem_tid, NULL, modem_log_handler, NULL);
	}
	return 0;
}

static int start_sub_threads()
{
	if(slog_enable != SLOG_ENABLE)
		return 0;

	if(!stream_log_handler_started)
		pthread_create(&stream_tid, NULL, stream_log_handler, NULL);
	if(!snapshot_log_handler_started)
		pthread_create(&snapshot_tid, NULL, snapshot_log_handler, NULL);
	if(!notify_log_handler_started)
		pthread_create(&notify_tid, NULL, notify_log_handler, NULL);
	if(!bt_log_handler_started)
		pthread_create(&bt_tid, NULL, bt_log_handler, NULL);
	if(!tcp_log_handler_started)
		pthread_create(&tcp_tid, NULL, tcp_log_handler, NULL);

	pthread_create(&uboot_log_tid, NULL, uboot_log_handler, NULL);
	if(!kmemleak_handler_started)
		pthread_create(&kmemleak_tid, NULL, kmemleak_handler, NULL);
	return 0;
}

static int stop_sub_threads()
{
	return 0;
}

static int reload()
{
	kill(getpid(), SIGTERM);
	return 0;
}

static void init_external_storage()
{
	char *p;
	int type;
	char value[PROPERTY_VALUE_MAX];

	p = getenv("SECOND_STORAGE_TYPE");
	if(p){
		type = atoi(p);
		p = NULL;
		if(type == 0 || type == 1){
			p = getenv("EXTERNAL_STORAGE");
		} else if(type == 2) {
			p = getenv("SECONDARY_STORAGE");
		}

		if(p){
			strcpy(external_path, p);
			sprintf(external_storage, "%s/slog", p);
			debug_log("the external storage : %s", external_storage);
			return;
		} else {
			err_log("SECOND_STORAGE_TYPE is %d, but can't find the external storage environment", type);
			exit(0);
		}

	}

	property_get("persist.storage.type", value, "3");
	type = atoi(value);
	if( type == 0 || type == 1 || type == 2) {
		p = NULL;
		if(type == 0 || type == 1){
			p = getenv("EXTERNAL_STORAGE");
		} else if(type == 2) {
			p = getenv("SECONDARY_STORAGE");
		}

		if(p){
			strcpy(external_path, p);
			sprintf(external_storage, "%s/slog", p);
			debug_log("the external storage : %s", external_storage);
			return;
		} else {
			err_log("SECOND_STORAGE_TYPE is %d, but can't find the external storage environment", type);
			exit(0);
		}
	}

	p = getenv("SECONDARY_STORAGE");
	if(p == NULL)
		p = getenv("EXTERNAL_STORAGE");
	if(p == NULL){
		err_log("Can't find the external storage environment");
		exit(0);
	}
	strcpy(external_path, p);
	sprintf(external_storage, "%s/slog", p);
	debug_log("the external storage : %s", external_storage);
	return;
}

static int sdcard_mounted()
{
	FILE *str;
	char buffer[MAX_LINE_LEN];

	str = fopen("/proc/mounts", "r");
	if(str == NULL) {
		err_log("can't open '/proc/mounts'");
		return 0;
	}

	while(fgets(buffer, MAX_LINE_LEN, str) != NULL) {
		if(strstr(buffer, external_path)){
			fclose(str);
			return 1;
		}
	}

	fclose(str);
	return 0;
}

static void check_available_volume()
{
	struct statfs diskInfo;
	char cmd[MAX_NAME_LEN];
	unsigned int ret;

	if(slog_enable != SLOG_ENABLE)
		return;

	if(!strncmp(current_log_path, external_storage, strlen(external_storage))) {
		sleep(3); /* wait 3s to slog reload */
		if( statfs(external_path, &diskInfo) < 0 ) {
			err_log("statfs %s return err!", external_path);
			return;
		}
		ret = diskInfo.f_bavail * diskInfo.f_bsize >> 20;
		if(ret > 0 && ret < 50) {
			err_log("sdcard available %dM", ret);
			sprintf(cmd, "%s", "am start -n com.spreadtrum.android.eng/.SlogUILowStorage");
			system(cmd);
			sleep(300);
		}
	} else {
		if( statfs(current_log_path, &diskInfo) < 0 ) {
			err_log("statfs %s return err!", current_log_path);
			return;
		}
		ret = diskInfo.f_bavail * diskInfo.f_bsize >> 20;
		if(ret < 5 && slog_enable != SLOG_DISABLE) {
			err_log("internal available %dM is not enough, disable slog", ret);
			slog_enable = SLOG_DISABLE;
		}
	}
}

static int recv_socket(int sockfd, void* buffer, int size)
{
	int received = 0, result;
	while(buffer && (received < size)) {
		result = recv(sockfd, (char *)buffer + received, size - received, MSG_NOSIGNAL);
		if (result > 0) {
			received += result;
		} else {
			received = result;
			break;
		}
	}
	return received;
}

static int send_socket(int sockfd, void* buffer, int size)
{
	int result = -1;
	int ioffset = 0;
	while(sockfd > 0 && ioffset < size) {
		result = send(sockfd, (char *)buffer + ioffset, size - ioffset, MSG_NOSIGNAL);
		if (result > 0) {
			ioffset += result;
		} else {
			break;
		}
	}
	return result;
}

int clear_all_log()
{
	char cmd[MAX_NAME_LEN];

	slog_enable = SLOG_DISABLE;
	stop_sub_threads();
	sleep(3);
	sprintf(cmd, "rm -r %s", INTERNAL_LOG_PATH);
	system(cmd);
	sprintf(cmd, "rm -r %s", external_storage);
	system(cmd);
	reload();
	return 0;
}

int dump_all_log(const char *name)
{
	char cmd[MAX_NAME_LEN];
	if(!strncmp(current_log_path ,INTERNAL_LOG_PATH, strlen(INTERNAL_LOG_PATH)))
		return -1;
	capture_all(snapshot_log_head);
	capture_by_name(snapshot_log_head, "getprop", NULL);
	sprintf(cmd, "tar czf %s/../%s /%s %s", current_log_path, name, current_log_path, INTERNAL_LOG_PATH);
	return system(cmd);
}

/* monitoring sdcard status thread */
static void *monitor_sdcard_fun()
{
	char *last = current_log_path;

	while( !strncmp (config_log_path, external_storage, strlen(external_storage))) {
		if(sdcard_mounted()) {
			current_log_path = external_storage;
			if(last != current_log_path)
				reload();
			last = current_log_path;
		} else {
			current_log_path = INTERNAL_LOG_PATH;
			if(last != current_log_path)
				reload();
			last = current_log_path;
		}
		sleep(TIMEOUT_FOR_SD_MOUNT);
	}
	return 0;
}

/*
 *handler log size according to internal available space
 *
 */
static void handler_internal_log_size()
{
	struct statfs diskInfo;
	unsigned int internal_availabled_size;
	int ret;

	if( strncmp(current_log_path, INTERNAL_LOG_PATH, strlen(INTERNAL_LOG_PATH)))
		return;

	ret = mkdir(current_log_path, S_IRWXU | S_IRWXG | S_IRWXO);
	if(-1 == ret && (errno != EEXIST)) {
		err_log("mkdir %s failed.", current_log_path);
		exit(0);
	}

	if( statfs(current_log_path, &diskInfo) < 0) {
		slog_enable = SLOG_DISABLE;
		err_log("statfs %s return err, disable slog", current_log_path);
		return;
	}
	internal_availabled_size = diskInfo.f_bavail * diskInfo.f_bsize / 1024;
	if( internal_availabled_size < 10 ) {
		slog_enable = SLOG_DISABLE;
		err_log("internal available space %dM is not enough, disable slog", internal_availabled_size);
		return;
	}

	/* setting internal log size = (available size - 5M) * 80% */
	internal_log_size = ( internal_availabled_size - 5 * 1024 ) / 5 * 4 / 12;
	err_log("set internal log size %dKB", internal_log_size);

	return;
}

/*
 * handle dropbox
 *
 */
static void handle_dropbox()
{
	char cmd[MAX_NAME_LEN];
	sprintf(cmd, "tar czf %s/%s/dropbox.tgz /data/system/dropbox", current_log_path, top_logdir);
	err_log("%s", cmd);
	system(cmd);
}

/*
 * handle top_logdir
 *
 */
static void handle_top_logdir()
{
	int ret;
	char value[PROPERTY_VALUE_MAX];

	if(slog_enable != SLOG_ENABLE)
		return;

	property_get("slog.step", value, "0");
	slog_start_step = atoi(value);

	ret = mkdir(current_log_path, S_IRWXU | S_IRWXG | S_IRWXO);
	if(-1 == ret && (errno != EEXIST)) {
		err_log("mkdir %s failed.", current_log_path);
		exit(0);
	}

	if( !strncmp(current_log_path, INTERNAL_LOG_PATH, strlen(INTERNAL_LOG_PATH))) {
		err_log("slog use internal storage");
		switch(slog_start_step){
		case 0:
			create_log_dir();
			capture_snap_for_last(snapshot_log_head);
			handle_dropbox();
			property_set("slog.step", "1");
			break;
		default:
			use_ori_log_dir();
			break;
		}
	} else {
		err_log("slog use external storage");
		switch(slog_start_step){
		case 0:
			create_log_dir();
			capture_snap_for_last(snapshot_log_head);
			handle_dropbox();
			handler_modem_memory_log();
			property_set("slog.step", "2");
			break;
		case 1:
			create_log_dir();
			handler_modem_memory_log();
			property_set("slog.step", "2");
			break;
		default:
			use_ori_log_dir();
			break;
		}
	}
}

/*
 *  monitoring sdcard status
 */
static int start_monitor_sdcard_fun()
{
	/* handle sdcard issue */
	if(!strncmp(config_log_path, external_storage, strlen(external_storage))) {
		if(!sdcard_mounted())
			current_log_path = INTERNAL_LOG_PATH;
		else {
			/*avoid can't unload SD card*/
			sleep(TIMEOUT_FOR_SD_MOUNT *2);
			current_log_path = external_storage;
		}
		/* create a sdcard monitor thread */
		if(slog_enable != SLOG_ENABLE)
			return 0;
		pthread_create(&sdcard_tid, NULL, monitor_sdcard_fun, NULL);
	} else
		current_log_path = INTERNAL_LOG_PATH;

	return 0;
}

/*
 * 1.start running slog system(stream,snapshot,inotify)
 * 2.monitoring sdcard status
 */
static void do_init()
{
	if(slog_enable != SLOG_ENABLE)
		return;

	handler_internal_log_size();

	handle_top_logdir();

	capture_all(snapshot_log_head);

	/* all backend log capture handled by follows threads */
	start_sub_threads();

	slog_init_complete = 1;

	return;
}

void *handle_request(void *arg)
{
	int ret, client_sock;
	struct slog_cmd cmd;
	char filename[MAX_NAME_LEN];
	time_t t;
	struct tm tm;

	client_sock = * ((int *) arg);
	ret = recv_socket(client_sock, (void *)&cmd, sizeof(cmd));
	if(ret <  0) {
		err_log("recv data failed!");
		close(client_sock);
		return 0;
	}
	if(cmd.type == CTRL_CMD_TYPE_RELOAD) {
		cmd.type = CTRL_CMD_TYPE_RSP;
		sprintf(cmd.content, "OK");
		send_socket(client_sock, (void *)&cmd, sizeof(cmd));
		close(client_sock);
		reload();
	}

	switch(cmd.type) {
	case CTRL_CMD_TYPE_SNAP:
		ret = capture_by_name(snapshot_log_head, cmd.content, NULL);
		break;
	case CTRL_CMD_TYPE_SNAP_ALL:
		ret = capture_all(snapshot_log_head);
		break;
	case CTRL_CMD_TYPE_EXEC:
		/* not implement */
		ret = -1;
		break;
	case CTRL_CMD_TYPE_ON:
		/* not implement */
		ret = -1;
		break;
	case CTRL_CMD_TYPE_OFF:
		slog_enable = SLOG_DISABLE;
		ret = stop_sub_threads();
		sleep(3);
		break;
	case CTRL_CMD_TYPE_QUERY:
		ret = gen_config_string(cmd.content);
		break;
	case CTRL_CMD_TYPE_CLEAR:
		ret = clear_all_log();
		break;
	case CTRL_CMD_TYPE_DUMP:
		ret = dump_all_log(cmd.content);
		break;
	case CTRL_CMD_TYPE_HOOK_MODEM:
		ret = mkdir("/data/log", S_IRWXU | S_IRWXG | S_IRWXO);
		if (-1 == ret && (errno != EEXIST)){
			err_log("mkdir /data/log failed.");
		}
		ret = 0;
		hook_modem_flag = 1;
		break;
	case CTRL_CMD_TYPE_SCREEN:
		if(slog_enable != SLOG_ENABLE || slog_init_complete == 0)
			break;
		if(cmd.content[0])
			ret = screen_shot(cmd.content);
		else {
			sprintf(filename, "%s/%s/misc", current_log_path, top_logdir);
			ret = mkdir(filename, S_IRWXU | S_IRWXG | S_IRWXO);
			if(-1 == ret && (errno != EEXIST)){
				err_log("mkdir %s failed.", filename);
				exit(0);
			}
			t = time(NULL);
			localtime_r(&t, &tm);
			sprintf(filename, "%s/%s/misc/screenshot_%02d%02d%02d.jpg",
					current_log_path, top_logdir,
					tm.tm_hour, tm.tm_min, tm.tm_sec);
			ret = screen_shot(filename);
		}
		break;
	default:
		err_log("wrong cmd cmd: %d %s.", cmd.type, cmd.content);
		break;
	}
	cmd.type = CTRL_CMD_TYPE_RSP;
	if(ret && cmd.content[0] == 0)
		sprintf(cmd.content, "FAIL");
	else if(!ret && cmd.content[0] == 0)
		sprintf(cmd.content, "OK");
	send_socket(client_sock, (void *)&cmd, sizeof(cmd));
	close(client_sock);

	return 0;
}

void *command_handler(void *arg)
{
	struct sockaddr_un serv_addr;
	int ret, server_sock, client_sock;
	pthread_t thread_pid;

	/* init unix domain socket */
	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sun_family=AF_UNIX;
	strcpy(serv_addr.sun_path, SLOG_SOCKET_FILE);
	unlink(serv_addr.sun_path);

	server_sock = socket(AF_UNIX, SOCK_STREAM, 0);
        if (server_sock < 0) {
		err_log("create socket failed!");
		return NULL;
	}

	if (bind(server_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
		err_log("bind socket failed!");
		close(server_sock);
		return NULL;
	}

	if (listen(server_sock, 5) < 0) {
		err_log("listen socket failed!");
		close(server_sock);
		return NULL;
	}

	while(1) {
		client_sock = accept(server_sock, NULL, NULL);
		if (client_sock < 0) {
			err_log("accept failed!");
			sleep(1);
			continue;
		}

		if ( 0 != pthread_create(&thread_pid, NULL, handle_request, (void *) &client_sock) ) {
			err_log("sock thread create error");
		}
	}
}

static void sig_handler1(int sig)
{
	err_log("get a signal %d.", sig);
	exit(0);
}

static void sig_handler2(int sig)
{
	err_log("get a signal %d.", sig);
	return;
}

/*
 * setup_signals - initialize signal handling.
 */
static void setup_signals()
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

	SIGNAL(SIGTERM, sig_handler1);
	SIGNAL(SIGBUS, sig_handler1);
	SIGNAL(SIGSEGV, sig_handler1);
	SIGNAL(SIGHUP, sig_handler1);
	SIGNAL(SIGQUIT, sig_handler1);
	SIGNAL(SIGABRT, sig_handler1);
	SIGNAL(SIGILL, sig_handler1);

	SIGNAL(SIGFPE, sig_handler2);
	SIGNAL(SIGPIPE, sig_handler2);
	SIGNAL(SIGALRM, sig_handler2);

	return;
}

/*
 * the main function
 */
int main(int argc, char *argv[])
{
	int opt;

	err_log("Slog begin to work.");
	/* for shark opt:t*/
	while ( -1 != (opt = getopt(argc, argv, "t"))) {
		switch (opt) {
			case 't':
				dev_shark_flag = 1;
				break;
			default:
				break;
		}
	}

	/* sets slog process's file mode creation mask */
	umask(0);

	/* handle signal */
	setup_signals();

	/* read and parse config file */
	parse_config();

	/* even backend capture threads disabled, we also accept user command */
	pthread_create(&command_tid, NULL, command_handler, NULL);

	/*find the external storage environment*/
	init_external_storage();

	/*start monitor sdcard*/
	start_monitor_sdcard_fun();

	/* backend capture threads started here */
	do_init();

	handle_low_power();

	while(1) {
		sleep(10);
		check_available_volume();
	}
	return 0;
}
