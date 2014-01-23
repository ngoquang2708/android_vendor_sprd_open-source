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

#include "slog.h"

#if 1

#define BT_CONF_PATH "/etc/bluetooth/bt_stack.conf"
#define BT_LOG_STATUS "BtSnoopLogOutput="
#define BT_LOG_PATH "BtSnoopFileName="

void operate_bt_status(char *status, char* path)
{
	FILE *fp;
	char line[MAX_NAME_LEN], buffer[MAX_LINE_LEN];
	int len = 0;

	fp = fopen(BT_CONF_PATH, "r+");
	if(fp == NULL) {
		err_log("open %s failed!", BT_CONF_PATH);
	}

	while (fgets(line, MAX_NAME_LEN, fp)) {

		if(!strncmp(BT_LOG_STATUS, line, strlen(BT_LOG_STATUS))) {
			sprintf(line, "%s%s", BT_LOG_STATUS, status);
		}

		if(!strncmp(BT_LOG_STATUS, line, strlen(BT_LOG_PATH))) {
			if(path != NULL)
				sprintf(line, "%s%s", BT_LOG_PATH, path);
		}

		len += sprintf(buffer + len, "%s", line);
	}

	fclose(fp);

	fp = fopen(BT_CONF_PATH, "r+");
	if(fp == NULL) {
		err_log("open %s failed!", BT_CONF_PATH);
	}

	fprintf(fp, "%s", buffer);
	fclose(fp);
	return;
}


void *bt_log_handler(void *arg)
{
	struct slog_info *info = NULL, *bt = NULL;
	char buffer[MAX_NAME_LEN];
	int ret;

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
		operate_bt_status("false", NULL);
		return NULL;
	}

	sprintf(buffer, "%s/%s/%s/", current_log_path, top_logdir, bt->log_path);
	operate_bt_status("ture", buffer);

	return NULL;
}

#else

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

	if( !strncmp(current_log_path, INTERNAL_LOG_PATH, strlen(INTERNAL_LOG_PATH)) )
		return NULL;

	bt_log_handler_started = 1;
	pid = fork();
	if(pid < 0){
		err_log("fork error!");
	}

	if(pid == 0){
		gen_logfile(buffer, info);
		execl("/system/xbin/hcidump", "hcidump", "-w", buffer, (char *)0);
		exit(0);
	}

	while(slog_enable == SLOG_ENABLE){
		sleep(1);
	}

	kill(pid, SIGTERM);
	bt_log_handler_started = 0;

	return NULL;
}

#endif
