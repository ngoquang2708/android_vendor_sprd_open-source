/*
 * Copyright (C) 2012 Spreadtrum Communications Inc.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <time.h>
#include <dirent.h>
#include <cutils/sockets.h>
#include <cutils/properties.h>
#include "private/android_filesystem_config.h"

#include "slog.h"

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

void gen_logpath(char *filename, struct slog_info *info)
{
	int ret;

	sprintf(filename, "%s/%s/%s", current_log_path, top_logdir, info->log_path);
	ret = mkdir(filename, S_IRWXU | S_IRWXG | S_IRWXO);
	if (-1 == ret && (errno != EEXIST)){
                err_log("mkdir %s failed.", filename);
                exit(0);
	}
}

void gen_logfile(char *filename, struct slog_info *info)
{
	int ret;
	int retry_count=5;
	char buffer[MAX_NAME_LEN];
	DIR *p_dir;
	struct dirent *p_dirent;
	time_t when;
	struct tm start_tm;

	sprintf(filename, "%s/%s/%s", current_log_path, top_logdir, info->log_path);
	sprintf(buffer, "0-%s", info->log_basename);
	ret = mkdir(filename, S_IRWXU | S_IRWXG | S_IRWXO);
	if (-1 == ret && (errno != EEXIST)){
                err_log("mkdir %s failed.", filename);
                exit(0);
	}
	while(retry_count){
		if(( p_dir = opendir(filename)) == NULL) {
			if(errno==EINTR||errno==EAGAIN){
				sleep(1);
				retry_count--;
			}else{
				err_log("can not open %s.", filename);
				exit(0);
			}
		}else
			break;
	}
	while((p_dirent = readdir(p_dir))) {
		if( !strncmp(p_dirent->d_name, buffer, strlen(buffer)) ) {
			sprintf(filename, "%s/%s/%s/%s", current_log_path, top_logdir, info->log_path, p_dirent->d_name);
			closedir(p_dir);
			return;
		}
	}
	when = time(NULL);
	localtime_r(&when, &start_tm);
	if( !strncmp(info->name, "tcp", 3))
		sprintf(filename, "/%s/%s/%s/0-%s-%02d-%02d-%02d.pcap",
						current_log_path,
						top_logdir,
						info->log_path,
						info->log_basename,
						start_tm.tm_hour,
						start_tm.tm_min,
						start_tm.tm_sec);
	else
		sprintf(filename, "/%s/%s/%s/0-%s-%02d-%02d-%02d.log",
						current_log_path,
						top_logdir,
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
	fflush(fp_dest);

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
	int retry_count = 5;
	int fd;
	while(retry_count){
		fd = open(path, O_RDWR);
		if(fd < 0){
			if(errno==EINTR||errno==EAGAIN){
				sleep(1);
				retry_count--;
			}else{
				err_log("Unable to open log device '%s'.", path);
				exit(0);
			}
		}else
			break;
	}
	return fd;
}

/*
 * open output file
 *
 */
FILE *gen_outfd(struct slog_info *info)
{
	int cur;
	int retry_count=5;
	FILE *fp;
	char buffer[MAX_NAME_LEN];

	gen_logfile(buffer, info);
	while(retry_count){
		fp = fopen(buffer, "a+b");
		if(fp == NULL){
			if(errno==EINTR||errno==EAGAIN){
				sleep(1);
				retry_count--;
			}else{
				err_log("Unable to open file %s.",buffer);
				exit(0);
			}
		}else
			break;
	}
	if(info->setvbuf == NULL)
		info->setvbuf = malloc(SETV_BUFFER_SIZE);

	setvbuf(fp, info->setvbuf, _IOFBF, SETV_BUFFER_SIZE);
	info->outbytecount = ftell(fp);
	info->file_path = strdup(buffer);
	return fp;
}

/*
 * The file name to upgrade
 */
void file_name_rotate(struct slog_info *info, int num, char *buffer)
{
	int i, err;
	DIR *p_dir;
	struct dirent *p_dirent;
	char filename[MAX_NAME_LEN], buf[MAX_NAME_LEN];

	for (i = num; i >= 0 ; i--) {
		char *file0, *file1;

		if(( p_dir = opendir(buffer)) == NULL) {
			err_log("can not open %s.", buffer);
			return;
		}
		sprintf(filename, "%d-%s", i, info->log_basename);
		while((p_dirent = readdir(p_dir))) {
			if( !strncmp(p_dirent->d_name, filename, strlen(filename)) ) {
				err = asprintf(&file1, "%s/%s/%s/%s", current_log_path, top_logdir, info->log_path, p_dirent->d_name);
				if(err == -1){
					err_log("asprintf return err!");
					exit(0);
				}
				if (i + 1 > num) {
					remove(file1);
					free(file1);
				} else {
					sprintf(filename, "%s", p_dirent->d_name);
					err = asprintf(&file0, "%s/%s/%s/%d%s",
						current_log_path, top_logdir, info->log_path, i + 1, filename + 1 + i/10);
					if(err == -1) {
						err_log("asprintf return err!");
						exit(0);
					}
					err = rename (file1, file0);
					if (err < 0 && errno != ENOENT) {
						perror("while rotating log files");
					}
					free(file1);
					free(file0);
				}
			}
		}

		closedir(p_dir);
	}
}

/*
 *  File volume
 *
 *  When the file is written full, rename file to file.1
 *  and rename file.1 to file.2, and so on.
 */
void rotatelogs(int num, struct slog_info *info)
{
	char buffer[MAX_NAME_LEN];

	err_log("slog rotatelogs");
	fclose(info->fp_out);
	gen_logpath(buffer, info);
	file_name_rotate(info, num, buffer);
	info->fp_out = gen_outfd(info);
	info->outbytecount = 0;
}

/*
 * Handler log file size according to the configuration file.
 *
 *
 */
void log_size_handler(struct slog_info *info)
{
	if( !strncmp(current_log_path, INTERNAL_LOG_PATH, strlen(INTERNAL_LOG_PATH)) ) {
		if(info->outbytecount >= internal_log_size * 1024) {
			rotatelogs(INTERNAL_ROLLLOGS, info);
		}
		return;
	}
	if(info->outbytecount >= DEFAULT_LOG_SIZE_AP * 1024 * 1024)
			rotatelogs(MAXROLLLOGS_FOR_AP, info);
}

void log_buffer_flush(void)
{
	struct slog_info *info;

	if(slog_enable != SLOG_ENABLE)
		return;

	info = stream_log_head;
	while(info){
		if(info->state != SLOG_STATE_ON){
			info = info->next;
			continue;
		}

		if(info->fp_out != NULL)
			fflush(info->fp_out);

		info = info->next;
	}

	return;
}
