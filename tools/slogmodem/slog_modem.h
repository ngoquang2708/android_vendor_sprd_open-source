/*
 * Copyright (C) 2012 Spreadtrum Communications Inc.
 *
 */

#ifndef _SLOG_MODEM_H_
#define _SLOG_MODEM_H_

#ifdef __cplusplus
extern "C" {
#endif

#define LOG_TAG "SLOGCP"

#include <errno.h>
#include <pthread.h>
#include <cutils/log.h>

#define ANDROID_VERSION_442
#define LOW_POWER_MODE

/* "type" field */
#define SLOG_TYPE_STREAM	0x1

/* "state" field */
#define SLOG_STATE_ON 0
#define SLOG_STATE_OFF 1

/*"slog state" field*/
#define SLOG_ENABLE 1
#define SLOG_DISABLE 0

/* "opt" field */
#define SLOG_EXEC_OPT_EXEC	0

#define err_log(fmt, arg...) ALOGE("%s: " fmt " [%d]\n", __func__, ## arg, errno)
#define debug_log(fmt, arg...) ALOGD("%s: " fmt, __func__, ## arg)
//#define debug_log(fmt, arg...)

#define INTERNAL_LOG_PATH		"/data/slog"
#define INTERNAL_PATH 			"/data"
#define DEFAULT_DEBUG_SLOG_CONFIG	"/system/etc/slog.conf"
#define DEFAULT_USER_SLOG_CONFIG	"/system/etc/slog.conf.user"
#define TMP_FILE_PATH			"/data/local/tmp/slog/"
#define SLOG_SOCKET_FILE		TMP_FILE_PATH "slog_sock"
#define TMP_SLOG_CONFIG			TMP_FILE_PATH "slog.conf"
#define DEFAULT_DUMP_FILE_NAME		"slog.tgz"
#define FB_DEV_NODE  			"/dev/graphics/fb0"
#define MINIDUMP_LOG_SOURCE     "/proc/cptl"

#define MAX_NAME_LEN			256
#define MAX_LINE_LEN			2048
#define DEFAULT_LOG_SIZE_AP		256 /* MB */
#define DEFAULT_LOG_SIZE_CP		512 /* MB */
#define MAXROLLLOGS_FOR_AP		9
#define MAXROLLLOGS_FOR_CP		100
#define INTERNAL_ROLLLOGS		1
#define TIMEOUT_FOR_SD_MOUNT		10 /* seconds */
#define BUFFER_SIZE			(32 * 1024) /* 32k */
#define SETV_BUFFER_SIZE		(64 * 1024) /* 64k */

#define MODEM_LOG_SOURCE		"/dev/vbpipe0"

#define CLINET_NR_MAX			8

struct client_conn
{
	int srv_socket;
	int client_socket[CLINET_NR_MAX];
	int dirty;
};

/* main data structure */
struct slog_info {
	struct slog_info	*next;

	/* log type */
	unsigned long	type;

	/* control log on/off */
	int		state;

	/* for different logs, level give several meanings */
	int		level;

	/* control snapshot record */
	int		interval;

	/* used for "snapshot" type */
	char		*opt;

	/* log file size limit */
	int		max_size;

	char path_name[MAX_NAME_LEN];

	/* Whether log of the CP is being saved on external storage. */
	int sd_mounted;

	/* identify a certain log entry, must uniq */
	char		*name;

	/* define directly log path, sometimes need update according to external storage changed */
	char		*log_path;

	/* filename without timestamp */
	char		*log_basename;

	/* used for "snap" type*/
	char		*content;

	/* used for "stream" type, log source handle */
	int		fd_device;

	/* used for "stream" type, modem memory source handle */
	int		fd_dump_cp;

	/* log file handle */
	FILE		*fp_out;

	/* setvbuf need buffer*/
	char		*setvbuf;

	/* current log file size count */
	int		outbytecount;

	/* for handle anr */
	struct timeval last, current;
};

struct slog_cmd
{
	int type;
	char content[MAX_LINE_LEN * 2];
};

struct modem_timestamp
{
	long unsigned int magic_number;       /* magic number for verify the structure */
	struct timeval tv;      /* clock time, seconds since 1970.01.01 */
	long unsigned int sys_cnt;            /* modem's time */
};

/* var */
extern char* config_log_path;
extern char top_log_dir[MAX_NAME_LEN];
extern char current_log_dir[MAX_NAME_LEN]; 
extern char external_storage[MAX_NAME_LEN];
extern struct slog_info* cp_log_head;
extern int internal_log_size;

extern struct client_conn s_cli_mgr;

extern char* g_external_path;

// Public functions from modem.c
struct slog_info* find_device(const char* name);

#ifdef __cplusplus
}
#endif

#endif /* !_SLOG_MODEM_H_ */
