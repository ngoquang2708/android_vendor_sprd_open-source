/*
 *  modem.c - main source file of CP log and mini dump.
 *
 *  Copyright (C) 2014-2015 Spreadtrum Communications Inc., Ltd.
 *
 *  History:
 *      2015-3-25 Zhang Ziyi
 *      Feature merge: log file size configuration, overwrite configuration.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <pthread.h>
#include <time.h>
#include <cutils/sockets.h>
#include <cutils/properties.h>
#include "private/android_filesystem_config.h"
#include <dirent.h>
#include <pthread.h>
#include <sys/time.h>
#include <signal.h>

#include "slog_modem.h"
#include "cp_config.h"
#include "modem_cmn.h"

#define SMSG "/d/sipc/smsg"
#define SBUF "/d/sipc/sbuf"
#define SBLOCK "/d/sipc/sblock"
#define INTERNET_LOG_SAVE_PATH "/data/slog/slog_cp"
#define MINIDUMP_SOURCE_NODE "mini_dump"
#define MINIDUMP_SOURCE_PATH "/proc/cptl" 

pthread_t modem_log_tid;
pthread_t modem_monitor_tid;
struct slog_info* cp_log_head;

struct client_conn s_cli_mgr;

char top_log_dir[MAX_NAME_LEN];
char whole_log_dir[MAX_NAME_LEN];
char current_log_dir[MAX_NAME_LEN]; 
char* g_external_path;
// If the EXTERNAL_STORAGE environment variable is set, s_sd_mounted
// indicates the external SD card mount state; otherwise s_sd_mounted
// will be 0.
static int s_sd_mounted = 0;

struct slog_info* find_device(const char* name)
{
	struct slog_info *info = cp_log_head;

	while(info) {
		if(!strcmp(info->name, name)) {
			break;
		} 
		info = info->next;
	}
	return info;
}

static int get_cp_on_num(const struct slog_info* head)
{
	int n = 0;

	while (head) {
		if (SLOG_STATE_ON == head->state) {
			++n;
		}
		head = head->next;
	}

	return n;
}

static void handle_dump_shark_sipc_info()
{
	char buffer[MAX_NAME_LEN];
	time_t t;
	struct tm tm;
	int ret;

	err_log("Start to dump SIPC info.");
	
	t = time(NULL);
	localtime_r(&t, &tm);
	
	sprintf(buffer, "%s/%s/misc",
			top_log_dir,
			current_log_dir);
	ret=mkdir(buffer,S_IRWXU | S_IRWXG | S_IRWXO);
	if (-1 == ret && (errno != EEXIST)){
		err_log("mkdir %s failed.", buffer);
		exit(0);
	}
	
	sprintf(buffer, "%s/sipc",buffer);
	
	ret = mkdir(buffer, S_IRWXU | S_IRWXG | S_IRWXO);
	
	if (-1 == ret && (errno != EEXIST)){
		err_log("mkdir %s failed.", buffer);
		exit(0);
	}

	sprintf(buffer, "%s/%s/misc/sipc/%02d-%02d-%02d_smsg.log",
			top_log_dir,
			current_log_dir,
			tm.tm_hour,
			tm.tm_min,
			tm.tm_sec);
	cp_file(SMSG, buffer);

	sprintf(buffer, "%s/%s/misc/sipc/%02d-%02d-%02d_sbuf.log",
			top_log_dir,
			current_log_dir,
			tm.tm_hour,
			tm.tm_min,
			tm.tm_sec);
	cp_file(SBUF, buffer);

	sprintf(buffer, "%s/%s/misc/sipc/%02d-%02d-%02d_sblock.log",
			top_log_dir,
			current_log_dir,
			tm.tm_hour,
			tm.tm_min,
			tm.tm_sec);
	cp_file(SBLOCK, buffer);
}

#define MODEM_W_DEVICE_PROPERTY "persist.modem.w.enable"
#define MODEM_TD_DEVICE_PROPERTY "persist.modem.t.enable"
#define MODEM_WCN_DEVICE_PROPERTY "ro.modem.wcn.enable"
#define MODEM_L_DEVICE_PROPERTY "persist.modem.l.enable"
#define MODEM_TL_DEVICE_PROPERTY "persist.modem.tl.enable"
#define MODEM_FL_DEVICE_PROPERTY "persist.modem.lf.enable"

#define MODEM_W_LOG_PROPERTY "ro.modem.w.log"
#define MODEM_TD_LOG_PROPERTY "ro.modem.t.log"
#define MODEM_WCN_LOG_PROPERTY "ro.modem.wcn.log"
#define MODEM_L_LOG_PROPERTY "ro.modem.l.log"
#define MODEM_TL_LOG_PROPERTY "ro.modem.tl.log"
#define MODEM_FL_LOG_PROPERTY "ro.modem.lf.log"

#define MODEM_W_DIAG_PROPERTY "ro.modem.w.diag"
#define MODEM_TD_DIAG_PROPERTY "ro.modem.t.diag"
#define MODEM_WCN_DIAG_PROPERTY "ro.modem.wcn.diag"
#define MODEM_L_DIAG_PROPERTY "ro.modem.l.diag"
#define MODEM_TL_DIAG_PROPERTY "ro.modem.tl.diag"
#define MODEM_FL_DIAG_PROPERTY "ro.modem.lf.diag"

#define MODME_WCN_DEVICE_RESET "persist.sys.sprd.wcnreset"
#define MODEM_WCN_DUMP_LOG "persist.sys.sprd.wcnlog"
#define MODEM_WCN_DUMP_LOG_COMPLETE "persist.sys.sprd.wcnlog.result"


#define MODEMRESET_PROPERTY "persist.sys.sprd.modemreset"
#define MODEM_SOCKET_NAME       "modemd"
#ifdef EXTERNAL_WCN
#define WCN_SOCKET_NAME       "external_wcn_slog"
#else
#define WCN_SOCKET_NAME       "wcnd"
#endif

#define MODEM_SOCKET_BUFFER_SIZE 128

static int modem_assert_flag = 0;
static int modem_reset_flag = 0;
static int modem_alive_flag = 0;
static struct slog_info *modem_info;

int minidump_log_file()
{
	char tmp_log_dir[MAX_NAME_LEN];
	char buffer[MAX_NAME_LEN];
	char modem_buf[MAX_NAME_LEN];
	int ret;

	sprintf(tmp_log_dir, "%s/%s/%s.dmp",
		top_log_dir, current_log_dir, MINIDUMP_SOURCE_NODE);
	sprintf(modem_buf, "%s/%s",
		MINIDUMP_SOURCE_PATH, MINIDUMP_SOURCE_NODE);

	sprintf(buffer, "cat %s > %s", modem_buf, tmp_log_dir);
	err_log("%s", buffer);
	ret = system(buffer);
	if(ret) {
		err_log("save mini dump file failed %d", ret);
	}
	return 0;
}

/*
 *  creat_top_path - create the "modem_log/<time>" directory.
 *  @path_flag: 1 indicates external SD card, 0 indicates internal storage.
 *
 *  Return Value:
 *      0: success
 *      -1: failure
 */
int creat_top_path(int path_flag)
{
	int ret = 0;	
	time_t when;
	struct tm start_tm;
	char path[MAX_NAME_LEN]; 

	when = time(NULL);
	localtime_r(&when, &start_tm);
	if (path_flag) {
		sprintf(top_log_dir, "%s/modem_log", g_external_path);
	} else {
		sprintf(top_log_dir,"%s/modem_log",INTERNAL_PATH);
	}
	ret = mkdir(top_log_dir, S_IRWXU | S_IRWXG | S_IRWXO);
	if (-1 == ret && EEXIST != errno) {
		err_log("mkdir %s failed.error: %s\n",
			top_log_dir, strerror(errno));
		return -1;
	}

	sprintf(path, "%s/%s", top_log_dir, current_log_dir);
	if(strlen(current_log_dir) > 0 && !access(path, W_OK)){
		return 0;
	}

	sprintf(current_log_dir, "%04d-%02d-%02d-%02d-%02d-%02d",
		start_tm.tm_year + 1900,
		start_tm.tm_mon + 1,
		start_tm.tm_mday,
		start_tm.tm_hour,
		start_tm.tm_min,
		start_tm.tm_sec);

	sprintf(path, "%s/%s", top_log_dir, current_log_dir);
	ret = mkdir(path, S_IRWXU | S_IRWXG | S_IRWXO);
	if (-1 == ret && EEXIST != errno) {
		err_log("mkdir %s failed.error: %s\n",
			current_log_dir, strerror(errno));
		return -1;
	}
	return 0;	
}

int monitor_sdcard_status()
{
	char value[PROPERTY_VALUE_MAX];
	int type;

	property_get("persist.storage.type", value, "-1");
	type = atoi(value);
	if (type == 1) {
		property_get("init.svc.fuse_sdcard0", value, "");
		if( !strncmp(value, "running", 7) )
			return 1;
		else
			return 0;
	}
	return 0;
}

int minidump_data_file(char *path)
{
	int path_flag=0;
	path_flag=monitor_sdcard_status();
	if(s_sd_mounted != path_flag){
		creat_top_path(path_flag);
	}
	
	minidump_log_file();		
	return 0;
}


static void handle_init_modem_state(struct slog_info *info)
{
	char buffer[MAX_NAME_LEN];
	char modem_property[MAX_NAME_LEN];
	int ret = 0;
	if (!strcmp(info->name, "cp_wcdma")) {
		property_get(MODEM_W_DEVICE_PROPERTY, modem_property, "");
		ret = atoi(modem_property);
	} else if (!strcmp(info->name, "cp_td-scdma")) {
		property_get(MODEM_TD_DEVICE_PROPERTY, modem_property, "");
		ret = atoi(modem_property);
	} else if (!strcmp(info->name, "cp_wcn")) {
		property_get(MODEM_WCN_DEVICE_PROPERTY, modem_property, "");
		ret = atoi(modem_property);
	} else if (!strcmp(info->name, "cp_td-lte")) {
		property_get(MODEM_L_DEVICE_PROPERTY, modem_property, "");
		ret = atoi(modem_property);
	} else if (!strcmp(info->name, "cp_tdd-lte")) {
		property_get(MODEM_TL_DEVICE_PROPERTY, modem_property, "");
		ret = atoi(modem_property);
	} else if (!strcmp(info->name, "cp_fdd-lte")) {
		property_get(MODEM_FL_DEVICE_PROPERTY, modem_property, "");
		ret = atoi(modem_property);
	}

	err_log("Init %s state is '%s'.", info->name, ret==0? "disable":"enable");
	if( ret == 0)
		info->state = SLOG_STATE_OFF;
}

static void handle_open_modem_device(struct slog_info *info)
{
	char buffer[MAX_NAME_LEN];
	char modem_property[MAX_NAME_LEN];

	if (!strncmp(info->name, "cp_wcdma", 8)) {
		property_get(MODEM_W_DIAG_PROPERTY, modem_property, "not_find");
		info->fd_device = open_device(info, modem_property);
		info->fd_dump_cp = info->fd_device;
		if(info->fd_device < 0)
			info->state = SLOG_STATE_OFF;
	} else if (!strncmp(info->name, "cp_td-scdma", 8)) {
		property_get(MODEM_TD_LOG_PROPERTY, modem_property, "not_find");
		info->fd_device = open_device(info, modem_property);
		if(info->fd_device < 0) {
			property_get(MODEM_TD_DIAG_PROPERTY, modem_property, "not_find");
			info->fd_device = open_device(info, modem_property);
			info->fd_dump_cp = info->fd_device;
			if(info->fd_device < 0)
				info->state = SLOG_STATE_OFF;
		} else {
			property_get(MODEM_TD_DIAG_PROPERTY, modem_property, "not_find");
			info->fd_dump_cp = open_device(info, modem_property);
		}
	} else if (!strncmp(info->name, "cp_wcn", 6)) {
		property_get(MODEM_WCN_DIAG_PROPERTY, modem_property, "not_find");
		#ifdef EXTERNAL_WCN
		info->fd_device = open(modem_property, O_RDWR|O_NONBLOCK);
		#else
		info->fd_device = open_device(info, modem_property);
		#endif
		info->fd_dump_cp = info->fd_device;
		if(info->fd_device < 0){
			info->state = SLOG_STATE_OFF;
		}
	} else if (!strncmp(info->name, "cp_td-lte", 8)) {
		property_get(MODEM_L_LOG_PROPERTY, modem_property, "not_find");
		info->fd_device = open_device(info, modem_property);
		if(info->fd_device < 0) {
			property_get(MODEM_L_DIAG_PROPERTY, modem_property, "not_find");
			info->fd_device = open_device(info, modem_property);
			info->fd_dump_cp = info->fd_device;
		} else {
			property_get(MODEM_L_DIAG_PROPERTY, modem_property, "not_find");
			info->fd_dump_cp = open_device(info, modem_property);
		}
	} else if (!strncmp(info->name, "cp_tdd-lte", 8)) {
		property_get(MODEM_TL_LOG_PROPERTY, modem_property, "not_find");
		info->fd_device = open_device(info, modem_property);
		if(info->fd_device < 0) {
			property_get(MODEM_TL_DIAG_PROPERTY, modem_property, "not_find");
			info->fd_device = open_device(info, modem_property);
			info->fd_dump_cp = info->fd_device;
		} else {
			property_get(MODEM_TL_DIAG_PROPERTY, modem_property, "not_find");
			info->fd_dump_cp = open_device(info, modem_property);
		}
	} else if (!strncmp(info->name, "cp_fdd-lte", 8)) {
		property_get(MODEM_FL_LOG_PROPERTY, modem_property, "not_find");
		info->fd_device = open_device(info, modem_property);
		if(info->fd_device < 0) {
			property_get(MODEM_FL_DIAG_PROPERTY, modem_property, "not_find");
			info->fd_device = open_device(info, modem_property);
			info->fd_dump_cp = info->fd_device;
		} else {
			property_get(MODEM_FL_DIAG_PROPERTY, modem_property, "not_find");
			info->fd_dump_cp = open_device(info, modem_property);
		}
	}
}

static void reopen_devices(void)
{
	struct slog_info* info = cp_log_head;

	while(info) {
		if(SLOG_STATE_ON != info->state &&
		   info->fd_device >= 0) {
			close(info->fd_device);
			if (info->fd_dump_cp >= 0 &&
			    info->fd_dump_cp != info->fd_device) {
				close(info->fd_dump_cp);
			}
			info->fd_device = info->fd_dump_cp = -1;
			debug_log("close log %s", info->name);
		} else if (SLOG_STATE_ON == info->state &&
			   -1 == info->fd_device) {
			handle_open_modem_device(info);
			debug_log("open log %s", info->name);
		}
		info = info->next;
	}
}

static void handle_dump_modem_memory_from_proc(struct slog_info *info)
{
	char buffer[MAX_NAME_LEN];
	time_t t;
	struct tm tm;

	err_log("Start to dump %s memory for shark.", info->name);

	/* add timestamp */
	t = time(NULL);
	localtime_r(&t, &tm);
	memset(buffer, 0, MAX_NAME_LEN);
	if(!strncmp(info->name, "cp_wcdma", 8)) {
		sprintf(buffer, "cat /proc/cpw/mem > %s/%s/%s/%s_memory_%d%02d-%02d-%02d-%02d-%02d.dmp",
			top_log_dir, current_log_dir, info->name, info->name,
			tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	} else if(!strncmp(info->name, "cp_td-scdma", 8)) {
		sprintf(buffer, "cat /proc/cpt/mem > %s/%s/%s/%s_memory_%d%02d-%02d-%02d-%02d-%02d.dmp",
			top_log_dir, current_log_dir, info->name, info->name,
			tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	} else if(!strncmp(info->name, "cp_wcn", 6)){
		sprintf(buffer, "cat /proc/cpwcn/mem > %s/%s/%s/%s_memory_%d%02d%02d%02d%02d%02d.dmp",
			top_log_dir, current_log_dir, info->name, info->name,
			tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	} else if(!strncmp(info->name, "cp_td-lte", 8)){
		sprintf(buffer, "cat /proc/cpl/mem > %s/%s/%s/%s_memory_%d%02d%02d%02d%02d%02d.dmp",
			top_log_dir, current_log_dir, info->name, info->name,
			tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	} else if(!strncmp(info->name, "cp_tdd-lte", 8)){
		sprintf(buffer, "cat /proc/cptl/mem > %s/%s/%s/%s_memory_%d%02d%02d%02d%02d%02d.dmp",
			top_log_dir, current_log_dir, info->name, info->name,
			tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	} else if(!strncmp(info->name, "cp_fdd-lte", 8)){
		sprintf(buffer, "cat /proc/cptl/mem > %s/%s/%s/%s_memory_%d%02d%02d%02d%02d%02d.dmp",
			top_log_dir, current_log_dir, info->name, info->name,
			tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	}
	if(buffer[0] != 0)
	{
		err_log("Dump %s memory %s", info->name,buffer);
		system(buffer);
	}
}

static int handle_correspond_modem(char *buffer)
{
	char modem_buffer[MAX_NAME_LEN];

	if(!strncmp(buffer, "WCN", 3)) {
		strcpy(modem_buffer, "cp_wcn");
	} else if (!strncmp(buffer, "TD Modem Assert", 15) || !strncmp(buffer, "_TD Modem Assert", 15)) {
		strcpy(modem_buffer, "cp_td-scdma");
	} else if (!strncmp(buffer, "W ", 2)) {
		strcpy(modem_buffer, "cp_wcdma");
	} else if (!strncmp(buffer, "LTE", 3)) {
		strcpy(modem_buffer, "cp_td-lte");
	} else if (!strncmp(buffer, "TDD", 3)) {
		strcpy(modem_buffer, "cp_tdd-lte");
	} else if (!strncmp(buffer, "FDD", 3)) {
		strcpy(modem_buffer, "cp_fdd-lte");
	} else if (!strncmp(buffer, "LF", 2)) {
		strcpy(modem_buffer, "cp_fdd-lte");
	}else {
		return 0;
	}

	modem_info = cp_log_head;
	while(modem_info) {
		if (!strncmp(modem_info->name, modem_buffer, strlen(modem_buffer)))
			break;
		modem_info = modem_info->next;
	}
	return 1;
}


int connect_socket_server(char *server_name)
{
	int fd = 0;

	fd = socket_local_client(server_name,
				 ANDROID_SOCKET_NAMESPACE_ABSTRACT,
				 SOCK_STREAM);
	if(fd < 0) {
		sleep(1);
		fd = socket_local_client(server_name,
					 ANDROID_SOCKET_NAMESPACE_ABSTRACT,
					 SOCK_STREAM);
	}

	if(fd < 0) {
		err_log("connect server %s failed.", server_name);
	} else {
		debug_log("connect server %s success", server_name);
	}

	return fd;
}

int handle_socket_modem(char *buffer)
{
	int reset = 0;
	int minidump_flag=0;
	char modemrst_property[MODEM_SOCKET_BUFFER_SIZE];

	memset(modemrst_property, 0, sizeof(modemrst_property));
	property_get(MODEMRESET_PROPERTY, modemrst_property, "0");
	reset = atoi(modemrst_property);

	if(strstr(buffer, "Modem Assert") != NULL) {
		if(reset == 0) {
			if (handle_correspond_modem(buffer) == 1) {
				minidump_flag=1;
				modem_assert_flag = 1;
				err_log("assert flag. %s", modem_info->name);
			}
		} else {
			if (handle_correspond_modem(buffer) == 1){
				minidump_flag=1;
				err_log("waiting for Modem Alive.");
				modem_reset_flag =1;	
			}
		}
	} else if(strstr(buffer, "Modem Alive") != NULL) {
		if (handle_correspond_modem(buffer) == 1)
			modem_alive_flag = 1;
	} else if(strstr(buffer, "Modem Blocked") != NULL) {
		minidump_flag=1;
		if (handle_correspond_modem(buffer) == 1) {
			modem_assert_flag = 1;
		}
	}
	if(minidump_flag==1){
		minidump_data_file(MINIDUMP_LOG_SOURCE);
		minidump_flag=0;
		return 1;
	}	
	return 0;
}

#ifdef EXTERNAL_WCN

static void handle_dump_external_wcn(struct slog_info *info)
{
	FILE *file_p;
	int fd_dev;
	int n,dump_size;
	char buffer[BUFFER_SIZE];
	char new_path[MAX_NAME_LEN];
	char modem_property[MAX_NAME_LEN];
	time_t t;
	struct tm tm;
	int ret;

	if (info->fd_dump_cp < 0) {
		err_log("dump dev is not open");
	}

	if (info->fp_out) {
		fflush(info->fp_out);
		debug_log("flush wcn log");
	}

	creat_top_path(monitor_sdcard_status());
	sprintf(new_path,"%s/%s/%s",top_log_dir,current_log_dir,info->name);
	ret=mkdir(new_path, S_IRWXU | S_IRWXG | S_IRWXO);
	if (-1 == ret && (errno != EEXIST)) {
		err_log("mkdir %s failed.error: %s\n", new_path,strerror(errno));
		return;
	}
	/* add timestamp */
	t = time(NULL);
	localtime_r(&t, &tm);
	sprintf(new_path, "%s/%s/%s/%s_memory_%d%02d-%02d-%02d-%02d-%02d.dmp", top_log_dir, current_log_dir, info->name, info->name,
				tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	file_p = fopen(new_path, "a+b");
	if (file_p == NULL) {
		err_log("open modem memory file failed!");
		return;
	}

	property_get(MODEM_WCN_DIAG_PROPERTY, modem_property, "not_find");

	int retry_count = 0;
retry:
	fd_dev = open(modem_property, O_RDWR);
	if(fd_dev < 0) {
		if( (errno == EINTR || errno == EAGAIN ) && retry_count < 5) {
			retry_count ++;
			sleep(1);
			goto retry;
		}
		err_log("Unable to open log device '%s'.", modem_property);
		return;
	}
	close(info->fd_device);
	info->fd_device = -1;

	debug_log("wcn start dump");
	dump_size = 0;
	do {
		n = read(fd_dev, buffer, BUFFER_SIZE);
		if(n > 0 && n < 4096) {
			debug_log("wcn read %d",n);
			if(!strncmp(buffer,"marlin_memdump_finish",21)) {
				break;
			}
		}

		if (n < 0) {
			debug_log("info->fd_dump_cp=%d read %d is lower than 0",fd_dev, n);
			sleep(1);
		} else if (!n) {
			debug_log("read 0");
			break;
		} else {
			fwrite(buffer, n, 1, file_p);
		}
		dump_size += n;
	} while(1);

	debug_log("wcn finish dump_size:%d",dump_size);
}

void handle_socket_wcn(char *buffer)
{
	struct slog_info *log_info;

	err_log("external WCN");

	if(strstr(buffer, "WCN-EXTERNAL-ALIVE") != NULL) {
		if (handle_correspond_modem(buffer) == 1) {
			log_info = cp_log_head;
			while(log_info) {
				if (!strcmp(log_info->name, "cp_wcn")) {
					if(log_info->state == SLOG_STATE_OFF) {
						log_info->state = SLOG_STATE_ON;
						modem_alive_flag = 1;
					}
					break;
				}
				log_info = log_info->next;
			}
		}
	} else if (strstr(buffer, "WCN-EXTERNAL-DUMP") != NULL) {
		if (handle_correspond_modem(buffer) == 1) {
			log_info = cp_log_head;
			while(log_info) {
				if (!strcmp(log_info->name, "cp_wcn")) {
					if(log_info->state == SLOG_STATE_ON) {
						log_info->state = SLOG_STATE_OFF;
					}
					handle_dump_external_wcn(log_info);
					property_set(MODEM_WCN_DUMP_LOG_COMPLETE, "1");
					break;
				}
				log_info = log_info->next;
			}
		}
	}
}

#else  // !EXTERNAL_WCN

void handle_socket_wcn(char *buffer)
{
	int dump = 0, reset = 0;
	char modemrst_property[MODEM_SOCKET_BUFFER_SIZE];

	err_log("internal WCN");

	memset(modemrst_property, 0, sizeof(modemrst_property));
	property_get(MODEM_WCN_DUMP_LOG,  modemrst_property, "1");
	dump = atoi(modemrst_property);

	memset(modemrst_property, 0, sizeof(modemrst_property));
	property_get(MODME_WCN_DEVICE_RESET,  modemrst_property, "0");
	reset = atoi(modemrst_property);

	if(strstr(buffer, "WCN-CP2-EXCEPTION") != NULL) {
		if(dump > 0) {
			if (handle_correspond_modem(buffer) == 1) {
				modem_assert_flag = 1;
				handle_dump_shark_sipc_info();
			}
		} else if(reset != 0) {
			if (handle_correspond_modem(buffer) == 1) {
				modem_reset_flag =1;
				err_log("waiting for Modem Alive.");
			}
		}
	} else if(strstr(buffer, "WCN-CP2-ALIVE") != NULL) {
		if (handle_correspond_modem(buffer) == 1)
			modem_alive_flag = 1;
	}
}

#endif

int handle_modem_state_monitor(int* fd_modem, int* fd_wcn,
			       fd_set* readset_tmp, fd_set* readset)
{	
	int  ret,  m, flag;
	int  n=0;
	char buffer[MODEM_SOCKET_BUFFER_SIZE];
	char re_buffer[MODEM_SOCKET_BUFFER_SIZE];
	int result, max = -1;
	struct timeval timeout;

	if(*fd_modem >= 0&&FD_ISSET(*fd_modem,readset)) {
		n = read(*fd_modem, buffer, MODEM_SOCKET_BUFFER_SIZE-1);
		if(n > 0) {
			buffer[n]='\0';
			err_log("get %d bytes %s", n, buffer);
			flag=handle_socket_modem(buffer);
			if(flag==1){
				sprintf(re_buffer,"MINIDUMP COMPLETE");   
				m=write(*fd_modem,re_buffer,sizeof(re_buffer));
				if(m>0){
					err_log("write %d bytes %s",m,re_buffer);
					}
				else
					err_log("write failed");
			}
		} else if(n <= 0) {
			err_log("get 0 bytes, sleep 10s, reconnect socket.");
			FD_CLR(*fd_modem, readset_tmp);
			close(*fd_modem);
			*fd_modem=-1;
	
		}
	} else if(*fd_wcn >=0&&FD_ISSET(*fd_wcn, readset))  {
		err_log("modem assert from wcnd");
		n = read(*fd_wcn, buffer, MODEM_SOCKET_BUFFER_SIZE - 1);
		if(n > 0) {
			buffer[n]='\0';
			err_log("get %d bytes %s", n, buffer);
			handle_socket_wcn(buffer);
		} else if(n <= 0) {
			err_log("get 0 bytes, sleep 10s, reconnect socket.");
			FD_CLR(*fd_wcn, readset_tmp);
			close(*fd_wcn);
			*fd_wcn = -1;
		}
	}
	return n;
}

static void handle_dump_modem_memory(struct slog_info *info)
{
	FILE *fp;
	int ret,n;
	int finish = 0, receive_from_cp = 0;
	char buffer[BUFFER_SIZE];
	char path[MAX_NAME_LEN];
	time_t t;
	struct tm tm;
	fd_set readset;
	int result;
	struct timeval timeout;
	char cmddumpmemory[2]={'3',0x0a};

	if(info->fd_dump_cp < 0) {
		err_log("Dumping cp memory device node is closed.");
		return;
	}

#if 0
write_cmd:
	n = write(info->fd_dump_cp, cmddumpmemory, 2);
	if (n <= 0) {
		sleep(1);
		goto write_cmd;
	}
#endif

	/* add timestamp */
	t = time(NULL);
	localtime_r(&t, &tm);
	sprintf(path,"%s/%s/%s",top_log_dir, current_log_dir,info->name);
	mkdir(path, S_IRWXU | S_IRWXG | S_IRWXO);

	sprintf(path,"%s/%s/%s",top_log_dir, current_log_dir,info->name);
	mkdir(path, S_IRWXU | S_IRWXG | S_IRWXO);
	
	handle_dump_modem_memory_from_proc(info);
	return;
}

/*
 *  reopen_out_file - reopen the log file because the external storage
 *                    is mounted or unmounted.
 *  @is_ext: 1 indicates the external storage is mounted, 0 indicates
 *           unmounted.
 *  @info: pointer to the CP slog_info.
 */
static void reopen_out_file(int is_ext, struct slog_info* info)
{
	int ret = creat_top_path(is_ext);
	if(!ret) {
		if (info->fp_out) {
			fclose(info->fp_out);
		}
		info->fp_out = gen_outfd(info);
		if(!info->fp_out) {
			err_log("CP %s create file failed!", info->name);
		}
	} else {
		err_log("create log directory failed!");
	}
}

static int fd_set_new(int modem_fd,int wcn_fd,fd_set *read_set)
{
	struct slog_info *info;
	int max = -1;

	FD_ZERO(read_set);
	if(modem_fd >=0) {
		FD_SET(modem_fd,read_set);
	}
	if(wcn_fd >= 0) {
		FD_SET(wcn_fd, read_set);
	}
	max = modem_fd > wcn_fd ? modem_fd : wcn_fd;

	info = cp_log_head;
	while (info) {
		if (SLOG_STATE_ON == info->state &&
		    info->fd_device >= 0) {
			debug_log("add %s to fdset", info->name);
			FD_SET(info->fd_device, read_set);
			if(info->fd_device > max) {
				max = info->fd_device;
			}
		}
		info = info->next;
	}
	if(s_cli_mgr.srv_socket >= 0) {
		debug_log("add server %d to fdset",s_cli_mgr.srv_socket);
		FD_SET(s_cli_mgr.srv_socket, read_set);
		if(s_cli_mgr.srv_socket > max) {
			max = s_cli_mgr.srv_socket;
		}
		int i;
		for(i = 0;i < CLINET_NR_MAX;i ++) {
			if(s_cli_mgr.client_socket[i] >= 0) {
				debug_log("add client %d to fdset",
					  s_cli_mgr.client_socket[i]);
				FD_SET(s_cli_mgr.client_socket[i], read_set);
				if(s_cli_mgr.client_socket[i] > max){
					max = s_cli_mgr.client_socket[i];
				}
			}
		}
	}
	return max;
}

static void process_client_data(struct client_conn* conns, int index)
{
	int fd = conns->client_socket[index];
	uint8_t buf[32];
	ssize_t nr;

	nr = read(fd, buf, 32);
	debug_log("read %d bytes", (int)nr);
	if(nr >= 14 && !memcmp(buf, "slogctl reload", 14)) {
		/* reload config file */
		debug_log("reload config");
		cp_parse_config();
		reopen_devices();
	} else if (nr >=13 && !memcmp(buf, "slogctl clear", 13)) {
		/* Clear log files */
		debug_log("clear log");
		clear_log();
	} else if (nr > 0) {
		buf[31] = '\0';
		err_log("invalid cmd %s", buf);
	} else {
		err_log("read error %d, close fd %d", (int)nr, fd);
		close(fd);
		conns->client_socket[index] = -1;
		conns->dirty = 1;
	}
}

/*
 *  check_media_change - check storage media change
 *  @cp_head: head pointer of the CP slog_info list.
 *
 *  Return Value:
 *      0: no change
 *      1: media changed and CP log files changed
 *      -1: error occurs
 */
static int check_media_change(struct slog_info* cp_head)
{
	int dir_changed = 0;
	int ret;

	int sd_state = monitor_sdcard_status();
	if (sd_state != s_sd_mounted) {
		debug_log("SD mount state changed to %d",
			  sd_state);
		// mount state changed
		ret = creat_top_path(sd_state);
		if(ret) {
			err_log("create top dir failed");
		}
		dir_changed = 1;
		s_sd_mounted = sd_state;
	}

	ret = dir_changed;
	if (dir_changed) {
		while (cp_head) {
			// If the log dir changes, reopen the log file.
			debug_log("CP %s log file changed to %d",
				  cp_head->name, s_sd_mounted);
			fclose(cp_head->fp_out);
			cp_head->fp_out = gen_outfd(cp_head);
			cp_head->sd_mounted = s_sd_mounted;
			if (!cp_head->fp_out) {
				ret = -1;
			}

			cp_head = cp_head->next;
		}
	}

	return ret;
}

void* modem_log_handler(void* arg)
{
	struct slog_info* info;
	static char cp_buffer[BUFFER_SIZE];
	char buffer[MAX_NAME_LEN];
	int ret = 0;
	int flag = 0;
	int n = 0;
	int totalLen = 0;
	int max = -1;
	fd_set readset_tmp;
	fd_set readset;
	int fd_modem = -1;
	int fd_wcn = -1;
	int result;
	ssize_t nr;
	struct timeval timeout;

	// Create the modem_log/<time> directory first.
	if (get_cp_on_num(cp_log_head)) {
		debug_log("create %s log dir",
			  s_sd_mounted ? "external" : "internal");
		ret = creat_top_path(s_sd_mounted);
		if(ret) {
			err_log("create top dir failed");
		}
	}

	// Open CP log devices and the log files
	info = cp_log_head;
	FD_ZERO(&readset_tmp);
	while (info) {
		if(info->state != SLOG_STATE_ON) {
			info->fd_device = -1;
		} else {
			if (!strcmp(info->name, "cp_wcdma") ||
			    !strcmp(info->name, "cp_td-scdma") ||
			    !strcmp(info->name, "cp_wcn") ||
			    !strcmp(info->name, "cp_td-lte") ||
			    !strcmp(info->name, "cp_tdd-lte") ||
			    !strcmp(info->name, "cp_fdd-lte")) {
				handle_init_modem_state(info);
				if(SLOG_STATE_ON == info->state) {
					handle_open_modem_device(info);
					info->fp_out = gen_outfd(info);
					if(!info->fp_out) {
						err_log("create file %s failed!", info->name);
					}
					info->sd_mounted = s_sd_mounted;
					FD_SET(info->fd_device, &readset_tmp);
					if (info->fd_device > max) {
						max = info->fd_device;
					}
				}
			} else {
				info->fd_device = -1;
			}
		}
		info = info->next;
	}
	info = cp_log_head;

	if(-1 == max && SLOG_DISABLE == g_minidump_enable) {
		err_log("modem disabled and no connection with modemd");
		return NULL;
	}
	
	while (1) {
		if(fd_modem < 0 ) {
			fd_modem = connect_socket_server(MODEM_SOCKET_NAME);
			if (fd_modem >= 0) {
				if(fd_modem > max){
					max=fd_modem;
				}		
				max = fd_set_new(fd_modem,fd_wcn,&readset_tmp);
			}
		}
		if(fd_wcn < 0) {
			fd_wcn = connect_socket_server(WCN_SOCKET_NAME);
			if(fd_wcn >= 0) {
				if(fd_wcn > max) {
					max = fd_wcn;
				}
				max = fd_set_new(fd_modem, fd_wcn, &readset_tmp);
			}
		}

		if(s_cli_mgr.srv_socket < 0) {
			s_cli_mgr.srv_socket = socket_local_server("slogmodem",
									ANDROID_SOCKET_NAMESPACE_ABSTRACT,
									SOCK_STREAM);
			if(s_cli_mgr.srv_socket < 0) {
				err_log("slocp:create server socket failed.");
			} else {
				long flags = fcntl(s_cli_mgr.srv_socket, F_GETFL);
				int err = -1;

				flags |= O_NONBLOCK;
				err = fcntl(s_cli_mgr.srv_socket, F_SETFL, flags);
				max = fd_set_new(fd_modem,fd_wcn,&readset_tmp);
			}
		}

		if(s_cli_mgr.dirty) {
			max = fd_set_new(fd_modem, fd_wcn, &readset_tmp);
			s_cli_mgr.dirty = 0;
		}

		FD_ZERO(&readset);
		memcpy(&readset, &readset_tmp, sizeof(readset_tmp));

		if (fd_modem < 0 || fd_wcn < 0 || s_cli_mgr.srv_socket < 0 ||
		    (g_external_path && !s_sd_mounted)) {
			timeout.tv_sec = 3;
			timeout.tv_usec = 0;
			result = select(max + 1, &readset, NULL, NULL, &timeout);
		} else {
			result = select(max + 1, &readset, NULL, NULL, NULL);
		}

		// Check the mount state
		if (g_external_path) {
			check_media_change(cp_log_head);
		}

		if (!result) {
			continue;
		}
		if(result < 0) {
			err_log("select failed %d(%s)",
				errno, strerror(errno));
			sleep(1);
			continue;
		}

		if(FD_ISSET(s_cli_mgr.srv_socket, &readset)) {
			/* add fd to client manager */
			int socket = accept(s_cli_mgr.srv_socket,0,0);
			int i;

			if(socket >= 0) {
				long flags = fcntl(socket, F_GETFL);
				int err = -1;

				err_log("client %d connection established.",socket);
				for(i = 0; i < CLINET_NR_MAX;i ++) {
					if(s_cli_mgr.client_socket[i] == -1) {
						s_cli_mgr.client_socket[i] = socket;
						break;
					}
				}
				if (CLINET_NR_MAX == i) {
					err_log("too many client connection,refuse %d",socket);
					close(socket);
				} else {
					s_cli_mgr.dirty = 1;

					flags |= O_NONBLOCK;
					err = fcntl(socket, F_SETFL, flags);
				}
			}
		}

		int i;
		for(i = 0; i < CLINET_NR_MAX; i++) {
			if(s_cli_mgr.client_socket[i] >= 0) {
				if(FD_ISSET(s_cli_mgr.client_socket[i], &readset)) {
					process_client_data(&s_cli_mgr, i);
					max = fd_set_new(fd_modem, fd_wcn,
							 &readset_tmp);
				}
			}
		}

		info = cp_log_head;
		while (info) {
			struct timeval t1;
			struct timeval t2;
			int tdiff;
			int wcn_tdiff;

			if (SLOG_STATE_ON != info->state ||
			    info->fd_device < 0) {
				info = info->next;
				continue;
			}

			if(!FD_ISSET(info->fd_device, &readset)) {
				info = info->next;
				continue;
			}

			//{Debug
			gettimeofday(&t1, 0);
			//}Debug
			nr = read(info->fd_device, cp_buffer, BUFFER_SIZE);
			gettimeofday(&t2, 0);
			tdiff = (int)((t2.tv_sec - t1.tv_sec) * 1000);
			tdiff += ((int)t2.tv_usec - (int)t1.tv_usec) / 1000;
			if (tdiff > 300) {
				debug_log("read %s too long time %d", info->name, tdiff);
			}

			if(nr <= 0) {
				if ((nr == -1 && (EINTR == errno || EAGAIN == errno))
				    || !nr) {
					info = info->next;
					continue;
				}
				err_log("read %s log failed!", info->name);
				FD_CLR(info->fd_device, &readset_tmp);
				close(info->fd_device);
				info->fd_device = -1;
				sleep(1);
				handle_open_modem_device(info);
				max = fd_set_new(fd_modem, fd_wcn,
						 &readset_tmp);
				info = info->next;
				continue;
			}

			if (!info->fp_out) {
				info->fp_out = gen_outfd(info);
			}

			// If the log file or directory is removed, recreate
			// it.
			if (access(info->path_name, W_OK)) {
				err_log("%s does not exist", info->path_name);	
				fclose(info->fp_out);
				info->fp_out = NULL;
				ret = creat_top_path(info->sd_mounted);
				if(!ret) {
					info->fp_out = gen_outfd(info);
				}
			}

			if(info->fp_out) {
				totalLen = fwrite(cp_buffer, nr, 1, info->fp_out);
				if (1 == totalLen) {
					info->outbytecount += nr;
					log_size_handler(info);
					//{Debug
					//test_overwrite_log(info);
					//}Debug
				} else {
					if(SLOG_ENABLE == g_overwrite_enable) {
						// Delete the oldest log
						del_oldest_log(info);
					} else {  // Don't overwrite old log
						err_log("fwrite error and not overwrite");
					}
				}
			}
			info = info->next;
		}

		n = handle_modem_state_monitor(&fd_modem, &fd_wcn,
					       &readset_tmp, &readset);

		if (n <= 0) {
			continue;
		} else {
			max = fd_set_new(fd_modem, fd_wcn, &readset_tmp);
		}

		if (modem_assert_flag == 1) {
			err_log("Modem %s Assert!", modem_info->name);
			sprintf(buffer, "%s", "am broadcast -a slogui.intent.action.DUMP_START");
			system(buffer);
			handle_dump_modem_memory(modem_info);
			sprintf(buffer, "%s", "am broadcast -a slogui.intent.action.DUMP_END");
			system(buffer);

			FD_CLR(modem_info->fd_device, &readset_tmp);
			close(modem_info->fd_device);
			modem_info->fd_device = -1;
			modem_info->state = SLOG_STATE_OFF;
			modem_assert_flag = 0;
			property_set(MODEM_WCN_DUMP_LOG_COMPLETE, "1");
		}

		if(modem_reset_flag == 1) {
			err_log("Modem %s Reset!", modem_info->name);
			FD_CLR(modem_info->fd_device, &readset_tmp);
			close(modem_info->fd_device);
			modem_info->fd_device = -1;
			modem_reset_flag = 0;
		}

		if(modem_alive_flag == 1) {
			err_log("Modem %s Alive!", modem_info->name);
			if(modem_info->fd_device > 0) {
				continue;
			}
			handle_open_modem_device(modem_info);
			if(modem_info->fd_device >= 0) {
				max = fd_set_new(fd_modem,fd_wcn,&readset_tmp);		
				err_log("open device %d max %d success",modem_info->fd_device,max);
				modem_alive_flag = 0;
			}else{
				err_log("critical error,open device %s failded",modem_info->name);
			}
		}
	}
	return NULL;
}

void init_modem_log_path()
{
	int flag;
	flag=monitor_sdcard_status();
	creat_top_path(flag);
}

static void init_client_mgr(void)
{
	int i;
	
	s_cli_mgr.srv_socket = -1;
	for(i = 0;i < CLINET_NR_MAX;i ++){
		s_cli_mgr.client_socket[i] = -1;
	}
	s_cli_mgr.dirty = 0;
}

int main(int argc, char *argv[])
{
	struct sigaction siga;
	
	// Ignore SIGPIPE signal
	memset(&siga, 0, sizeof siga);
	siga.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &siga, 0);

	cp_parse_config();
	g_external_path = getenv("EXTERNAL_STORAGE");
	if (g_external_path && monitor_sdcard_status()) {
		s_sd_mounted = 1;
	}
	init_client_mgr();
	modem_log_handler(0);

	return 0;
}
