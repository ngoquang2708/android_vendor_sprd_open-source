/*
 * Copyright (C) 2012 Spreadtrum Communications Inc.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <dirent.h>
#include <cutils/sockets.h>
#include <cutils/properties.h>
#include "private/android_filesystem_config.h"

#include "slog_modem.h"
#include "modem_cmn.h"
#include "modem_cmn_imp.h"
#include "cp_config.h"

int internal_log_size = 5 * 1024;

static struct RenameEntry* create_ren_entry(void)
{
	struct RenameEntry* e = (struct RenameEntry*)malloc(sizeof *e);
	if (e) {
		e->num = 0;
		e->num_len = 0;
		e->name[0] = '\0';
		e->new_name[0] = '\0';
		e->prev = e->next = 0;
	}

	return e;
}

static void init_ren_list(struct RenameList* plist)
{
	plist->head = 0;
	plist->tail = 0;
}

static void free_ren_list(struct RenameList* plist)
{
	struct RenameEntry* e = plist->head;

	while (e) {
		struct RenameEntry* pnext = e->next;
		free(e);
		e = pnext;
	}

	plist->head = 0;
	plist->tail = 0;
}

/*
 *  insert_ren_entry - insert the RenameEntry in the decending order.
 */
static void insert_ren_entry(struct RenameList* plist,
			     struct RenameEntry* e)
{
	struct RenameEntry* p = plist->head;

	if (!p) {  // Empty list
		e->next = 0;
		e->prev = 0;
		plist->head = e;
		plist->tail = e;
		return;
	}

	while (p) {
		if (e->num > p->num) {
			break;
		}
		p = p->next;
	}

	if (p) {
		e->next = p;
		e->prev = p->prev;
		if (p->prev) {
			p->prev->next = e;
		} else {  // Insert before the head
			plist->head = e;
		}
		p->prev = e;
	} else {  // Add to tail
		plist->tail->next = e;
		e->prev = plist->tail;
		e->next = 0;
		plist->tail = e;
	}
}

int send_socket(int sockfd, void* buffer, int size)
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

int recv_socket(int sockfd, void* buffer, int size)
{
        int received = 0, result;
        while(buffer && (received < size))
        {
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

#define W_TIME "/data/w_timesyncfifo"
#define TD_TIME "/data/td_timesyncfifo"
#define L_TIME "/data/l_timesyncfifo"

int get_timezone()
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

void write_modem_timestamp(struct slog_info *info, char *buffer)
{
	int fd;
	FILE* fp;
	int time_zone;
	struct modem_timestamp *mts;
	char cp_time[MAX_NAME_LEN];

        if (!strncmp(info->name, "cp_wcdma", 8)) {
                strcpy(cp_time, W_TIME);
        } else if (!strncmp(info->name, "cp_td-scdma", 8)) {
                strcpy(cp_time, TD_TIME);
        } else if (!strncmp(info->name, "cp_td-lte", 8)) {
                strcpy(cp_time, L_TIME);
        } else if (!strncmp(info->name, "cp_tdd-lte", 8)) {
                strcpy(cp_time, L_TIME);
	 } else if (!strncmp(info->name, "cp_fdd-lte", 8)) {
                strcpy(cp_time, L_TIME);
	} else {
                return;
	}

	mts = (struct modem_timestamp*)calloc(1, sizeof *mts);
	if (!mts) {
		return;
	}

	int retry_count = 0;
	while (retry_count < 5) {
		fd = open(cp_time, O_RDWR | O_NONBLOCK);
		if (fd >= 0) {
			break;
		}

		if (EINTR != errno && EAGAIN != errno) {
			err_log("Unable to open time stamp device '%s'",
				cp_time);
			free(mts);
			return;
		}

		sleep(1);
		++retry_count;
	}

	ssize_t ret;

	ret = read(fd, (char*)mts + offsetof(struct modem_timestamp, tv), 12);
	close(fd);
	if (ret < 12) {
		free(mts);
		err_log("not enough data from %s", cp_time);
		return;
	}

	mts->magic_number = 0x12345678;
	time_zone = get_timezone();
	mts->tv.tv_sec += time_zone * 3600;
	debug_log("%lx, %lx, %lx, %lx",
		  mts->magic_number,
		  mts->tv.tv_sec, mts->tv.tv_usec,
		  mts->sys_cnt);

	fp = fopen(buffer, "a+b");
	if (fp == NULL) {
		err_log("open file %s failed!", buffer);
		free(mts);
		return;
	}
	fwrite(mts, sizeof *mts, 1, fp);
	fclose(fp);

	free(mts);
}

#define MODEM_VERSION "gsm.version.baseband"

void write_modem_version(struct slog_info *info)
{
	char buffer[MAX_NAME_LEN];
	char modem_property[MAX_NAME_LEN];
	FILE *fp;
	int ret;

	if (strncmp(info->name, "cp", 2)) {
		return;
	}
	memset(modem_property, '0', MAX_NAME_LEN);
	property_get(MODEM_VERSION, modem_property, "not_find");
	if(!strncmp(modem_property, "not_find", 8)) {
		err_log("%s not find.", MODEM_VERSION);
		return;
	}

	sprintf(buffer, "%s/%s/%s/%s.version", top_log_dir, current_log_dir, info->log_path, info->log_basename);
	fp = fopen(buffer, "w+");
	if(fp == NULL) {
		err_log("open file %s failed!", buffer);
		return;
	}
	fwrite(modem_property, strlen(modem_property), 1, fp);
	fclose(fp);
}

static int get_log_path(char* log_path, size_t len,
			const struct slog_info* info)
{
	return snprintf(log_path, len, "%s/%s/%s",
			top_log_dir, current_log_dir, info->log_path);
}

void gen_logpath(char* log_path, const struct slog_info* info)
{
	int ret;

	sprintf(log_path, "%s/%s/%s",
		top_log_dir, current_log_dir, info->log_path);
	ret = mkdir(log_path, S_IRWXU | S_IRWXG | S_IRWXO);
	if (-1 == ret && EEXIST != errno){
                err_log("mkdir %s failed.", log_path);
                exit(0);
	}
}

void gen_logfile(char *filename, struct slog_info *info)
{
	int ret;
	char buffer[MAX_NAME_LEN];
	DIR *p_dir;
	struct dirent *p_dirent;
	time_t when;
	struct tm start_tm;

	sprintf(filename, "%s/%s/%s", top_log_dir, current_log_dir, info->log_path);
	sprintf(buffer, "0-%s", info->log_basename);
	ret = mkdir(filename, S_IRWXU | S_IRWXG | S_IRWXO);
	if (-1 == ret && (errno != EEXIST)){
                err_log("mkdir %s failed.", filename);
                exit(0);
	}

	if(( p_dir = opendir(filename)) == NULL) {
		err_log("can not open %s.", filename);
		return;
	}
	while((p_dirent = readdir(p_dir))) {
		if( !strncmp(p_dirent->d_name, buffer, strlen(buffer)) ) {
			sprintf(filename, "%s/%s/%s/%s", top_log_dir, current_log_dir, info->log_path, p_dirent->d_name);
			closedir(p_dir);
			return;
		}
	}
	when = time(NULL);
	localtime_r(&when, &start_tm);
	if( !strncmp(info->name, "tcp", 3))
		sprintf(filename, "/%s/%s/%s/0-%s-%02d-%02d-%02d.pcap",
						top_log_dir,
						current_log_dir,
						info->log_path,
						info->log_basename,
						start_tm.tm_hour,
						start_tm.tm_min,
						start_tm.tm_sec);
	else
		sprintf(filename, "/%s/%s/%s/0-%s-%02d-%02d-%02d.log",
						top_log_dir,
						current_log_dir,
						info->log_path,
						info->log_basename,
						start_tm.tm_hour,
						start_tm.tm_min,
						start_tm.tm_sec);



	closedir(p_dir);
	return;
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

/*
 * write form buffer.
 *
 */
int write_from_buffer(int fd, char *buf, int len)
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
int open_device(struct slog_info *info, char *path)
{
	int retry_count = 0;
	int fd;

retry:
	fd = open(path, O_RDWR | O_NONBLOCK);
	if(fd < 0){
		if( (errno == EINTR || errno == EAGAIN ) && retry_count < 5) {
			retry_count ++;
			sleep(1);
			goto retry;
		}
		err_log("Unable to open log device '%s'.", path);
		return -1;
	}

	return fd;
}

/*
 * open output file
 *
 */
FILE* gen_outfd(struct slog_info* info)
{
	int cur;
	FILE* fp;
	char* buffer;

	buffer = info->path_name;
	gen_logfile(buffer, info);
	write_modem_version(info);
	write_modem_timestamp(info, buffer);
	fp = fopen(buffer, "a+b");
	if (!fp) {
		err_log("Unable to open file %s.", buffer);
		return NULL;
	}
	if(!info->setvbuf) {
		info->setvbuf = (char*)malloc(SETV_BUFFER_SIZE);
	}

	setvbuf(fp, info->setvbuf, _IOFBF, SETV_BUFFER_SIZE);
	info->outbytecount = ftell(fp);

	return fp;
}

/*
 *  parse_file_name - parse log file name.
 *  @path_name: full path name of the log file.
 *  @fname: base name of the log file
 *  @cp_name: CP name pointer
 *  @nlen: CP name length in byte
 *  @num: pointer to the integer to hold the file number in the name
 *  @suf_off: pointer to the integer to hold the suffix offset
 *
 *  Return Value:
 *      Return -1 if the file is not a log file, otherwise return 0.
 */
static int parse_file_name(const char* path_name,
			   const char* fname,
			   const char* cp_name,
			   size_t nlen,
			   unsigned* num, size_t* suf_off)
{
	const char* p = fname;

	while (*p) {
		if ('-' == *p) {
			break;
		}
		++p;
	}
	if ('-' != *p) {
		return -1;
	}
	// Get the file number.
	unsigned fnum;
	size_t num_len = p - fname;

	if (str2unsigned(fname, num_len, &fnum)) {
		return -1;
	}

	// Check the CP name
	++p;
	for (unsigned i = 0; i < nlen; ++i) {
		if (p[i] != cp_name[i]) {
			return -1;
		}
	}

	p += nlen;
	for (int i = 0; i < 3; ++i, p += 3) {
		if ('-' != *p || !isdigit(p[1]) || !isdigit(p[2])) {
			return -1;
		}
	}
	if (strcmp(".log", p)) {
		return -1;
	}

	// The base name is like a log, check whether it's a file.
	struct stat log_stat;

	if (stat(path_name, &log_stat) || !S_ISREG(log_stat.st_mode)) {
		return -1;
	}

	*num = fnum;
	*suf_off = num_len;
	return 0;
}

static void get_new_log_name(char* new_name, size_t len,
		 	     const char* dir_name,
			     unsigned num,
			     const char* suffix)
{
	snprintf(new_name, len, "%s/%u%s", dir_name, num, suffix);
}

static void ren_logs_and_free(struct RenameList* plist)
{
	struct RenameEntry* pe = plist->head;

	while (pe) {
		struct RenameEntry* next = pe->next;

		if (rename(pe->name, pe->new_name)) {
			err_log("slogcp: rename %s -> %s failed %d",
				pe->name, pe->new_name, errno);
		}

		free(pe);
		pe = next;
	}

	plist->head = plist->tail = 0;
}

/*
 *  file_name_rotate - rotate the log file name.
 *  @dir_name: the directory name
 *
 *  When the file is written full, rename file to file.1
 *  and rename file.1 to file.2, and so on.
 *
 *  Return Value:
 *      Return 0 on success, -1 otherwise.
 */
static int file_name_rotate(const struct slog_info* info, char* dir_name)
{
	DIR* p_dir;
	struct dirent* p_dirent;

	p_dir = opendir(dir_name);
	if(!p_dir) {
		err_log("slogcp: rotate can not open dir %s.",
			dir_name);
		return -1;
	}

	struct RenameList ren_list;
	size_t nlen = strlen(info->name);

	init_ren_list(&ren_list);
	while (p_dirent = readdir(p_dir)) {
		char path_name[MAX_NAME_LEN];
		unsigned num;
		size_t num_len;

		snprintf(path_name, MAX_NAME_LEN, "%s/%s",
			 dir_name, p_dirent->d_name);
		if (parse_file_name(path_name, p_dirent->d_name,
				    info->name, nlen,
				    &num, &num_len)) {
			continue;
		}

		struct RenameEntry* pe = create_ren_entry();
		if (!pe) {
			free_ren_list(&ren_list);
			break;
		}
		pe->num = num;
		pe->num_len = num_len;
		strcpy(pe->name, path_name);
		get_new_log_name(pe->new_name, MAX_NAME_LEN,
				 dir_name,
				 num + 1,
				 p_dirent->d_name + num_len);
		insert_ren_entry(&ren_list, pe);
	}

	ren_logs_and_free(&ren_list);

	return 0;
}

/*
 *  rotatelogs - rotate log file names.
 *  @num: the maximum number of log files under the log directory.
 *  @info: the pointer to the CP entity whose log is to be rotated.
 *
 *  When the file is written full, rename file to file.1
 *  and rename file.1 to file.2, and so on.
 */
void rotatelogs(int num, struct slog_info* info)
{
	char log_path[MAX_NAME_LEN];

	err_log("log rotation starts");
	fclose(info->fp_out);
	gen_logpath(log_path, info);
	file_name_rotate(info, log_path);
	info->fp_out = gen_outfd(info);
	info->outbytecount = 0;
}

/*
 *  del_oldest_log_file - delete the oldest log file in the current log
 *                        directory.
 *  @cp: the pointer to the related CP entity.
 *
 *  Return Value:
 *      1: oldest file deleted.
 *      0: no file can be deleted.
 *      -1: error occurs.
 */
static int del_oldest_log_file(const struct slog_info* cp)
{
	char log_path[MAX_NAME_LEN];
	char oldest[MAX_NAME_LEN];
	DIR* pd;
	struct dirent* pent;
	int len;

	len = get_log_path(log_path, MAX_NAME_LEN, cp);
	pd = opendir(log_path);
	if (!pd) {
		return -1;
	}

	unsigned max_num = 0;
	size_t nlen = strlen(cp->name);

	oldest[0] = '\0';
	while (pent = readdir(pd)) {
		char path_name[MAX_NAME_LEN];
		unsigned num;
		size_t num_len;

		snprintf(path_name, MAX_NAME_LEN, "%s/%s",
			 log_path, pent->d_name);
		if (!parse_file_name(path_name, pent->d_name,
				     cp->name, nlen,
				     &num, &num_len)) {
			err_log("file number %u", num);
			if (num > max_num) {
				max_num = num;
				strcpy(oldest, path_name);
			}
		}
	}

	int ret = 0;

	if (oldest[0]) {
		err_log("deleting %s", oldest);
		ret = unlink(oldest);
		if (!ret) {  // Log file deleted.
			ret = 1;
		}
	}

	return ret;
}

static int parse_dir_name(const char* name)
{
	int i;

	for (i = 0; i < 4; ++i) {
		if (!isdigit(name[i])) {
			return -1;
		}
	}

	for (int j = 0; j < 5; ++j, i += 3) {
		if ('-' != name[i] || !isdigit(name[i + 1]) ||
		    !isdigit(name[i + 2])) {
			return -1;
		}
	}

	return !name[i] ? 0 : -1;
}

static int is_dir(const char* path)
{
	struct stat pstat;
	int n = 0;

	if (!stat(path, &pstat) && S_ISDIR(pstat.st_mode)) {
		n = 1;
	}

	return n;
}

static int del_oldest_log_dir(const char* par_dir,
			      const char* cur_dir)
{
	DIR* pd;
	struct dirent* pent;

	pd = opendir(par_dir);
	if (!pd) {
		return -1;
	}

	char oldest_path[MAX_NAME_LEN];
	char oldest_base[20];

	oldest_path[0] = '\0';
	// Set oldest_base to maximum
	memset(oldest_base, 0xff, 19);
	while (pent = readdir(pd)) {
		char path[MAX_NAME_LEN];

		snprintf(path, MAX_NAME_LEN, "%s/%s", par_dir, pent->d_name);
		if (!strcmp(pent->d_name, cur_dir)) {
			continue;
		}
		if (!parse_dir_name(pent->d_name) &&
		    is_dir(path) &&
		    memcmp(pent->d_name, oldest_base, 19) < 0) {
			memcpy(oldest_base, pent->d_name, 20);
			strcpy(oldest_path, path);
		}
	}

	closedir(pd);

	int ret = 0;

	if (oldest_path[0]) {
		char cmd[MAX_NAME_LEN + 16];

		snprintf(cmd, MAX_NAME_LEN + 16, "rm -fr %s", oldest_path);
		ret = system(cmd);
		if (ret) {
			ret = -1;
		}
	}

	return ret;
}

int del_oldest_log(const struct slog_info* cp)
{
	int ret = del_oldest_log_file(cp);

	if (!ret) {  // No file can be deleted
		// Try to delete the oldest log directory
		ret = del_oldest_log_dir(top_log_dir, current_log_dir);
	}

	if (1 == ret) {
		ret = 0;
	}

	return ret;
}

/*
 * Handler log file size according to the configuration file.
 *
 *
 */
void log_size_handler(struct slog_info* info)
{
	if(!strncmp(top_log_dir, INTERNAL_LOG_PATH, strlen(INTERNAL_LOG_PATH))) {
		if(info->outbytecount >= g_log_size * 1024) {
			rotatelogs(INTERNAL_ROLLLOGS, info);
		}
		return;
	}

	if (!strncmp(info->name, "cp", 2)) {
		if(info->outbytecount >= g_log_size * 1024 * 1024)
			rotatelogs(MAXROLLLOGS_FOR_CP, info);
	} else {
		if(info->outbytecount >= DEFAULT_LOG_SIZE_AP * 1024 * 1024)
			rotatelogs(MAXROLLLOGS_FOR_AP, info);
	}
}

void clear_log(void)
{
	system("rm -fr /data/modem_log/*");
	if (g_external_path) {
		char cmd[MAX_NAME_LEN + 16];

		snprintf(cmd, sizeof cmd, "rm -fr %s/modem_log/*",
			 g_external_path);
		system(cmd);
	}
}

#if 0
//{Debug
void test_overwrite_log(struct slog_info* info)
{
	char log_path[MAX_NAME_LEN];
	DIR* pd;
	struct dirent* pent;

	if (SLOG_ENABLE != g_overwrite_enable) {
		return;
	}

	get_log_path(log_path, MAX_NAME_LEN, info);
	pd = opendir(log_path);
	if (!pd) {
		return;
	}

	int fnum = 0;
	size_t nlen = strlen(info->name);
	while (pent = readdir(pd)) {
		char path_name[MAX_NAME_LEN];
		unsigned num;
		size_t num_len;

		snprintf(path_name, MAX_NAME_LEN, "%s/%s",
			 log_path, pent->d_name);
		if (!parse_file_name(path_name, pent->d_name,
				     info->name, nlen,
				     &num, &num_len)) {
			++fnum;
		}
	}

	if (fnum >= 3) {
		err_log("deleting oldest file ...");
		if (del_oldest_log(info)) {
			err_log("del_oldest_log failed");
		}
	}
}
//}Debug
#endif
