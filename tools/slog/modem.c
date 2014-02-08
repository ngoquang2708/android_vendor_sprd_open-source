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
#include <sys/time.h>
#include <sys/stat.h>
#include <pthread.h>
#include <time.h>
#include <cutils/sockets.h>
#include <cutils/properties.h>
#include "private/android_filesystem_config.h"

#include "slog.h"


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

	sprintf(buffer, "%s/%s/misc/sipc/%02d-%02d-%02d_smsg.log",
			current_log_path,
			top_logdir,
			tm.tm_hour,
			tm.tm_min,
			tm.tm_sec);
	cp_file(SMSG, buffer);

	sprintf(buffer, "%s/%s/misc/sipc/%02d-%02d-%02d_sbuf.log",
			current_log_path,
			top_logdir,
			tm.tm_hour,
			tm.tm_min,
			tm.tm_sec);
	cp_file(SBUF, buffer);

	sprintf(buffer, "%s/%s/misc/sipc/%02d-%02d-%02d_sblock.log",
			current_log_path,
			top_logdir,
			tm.tm_hour,
			tm.tm_min,
			tm.tm_sec);
	cp_file(SBLOCK, buffer);
}

#ifdef ANDROID_VERSION_442
#define MODEM_W_DEVICE_PROPERTY "persist.modem.w.enable"
#define MODEM_TD_DEVICE_PROPERTY "persist.modem.t.enable"
#else
#define MODEM_W_DEVICE_PROPERTY "ro.modem.w.enable"
#define MODEM_TD_DEVICE_PROPERTY "ro.modem.t.enable"
#endif
#define MODEM_WCN_DEVICE_PROPERTY "ro.modem.wcn.enable"

#define MODEM_W_DIAG_PROPERTY "ro.modem.w.diag"
#define MODEM_TD_DIAG_PROPERTY "ro.modem.t.diag"
#define MODEM_WCN_DIAG_PROPERTY "ro.modem.wcn.diag"

#define MODME_WCN_DEVICE_RESET "persist.sys.sprd.wcnreset"
#define MODEM_WCN_DUMP_LOG "persist.sys.sprd.wcnlog"
#define MODEM_WCN_DUMP_LOG_COMPLETE "persist.sys.sprd.wcnlog.result"

#define MODEMRESET_PROPERTY "persist.sys.sprd.modemreset"
#define MODEM_SOCKET_NAME       "modemd"
#define WCN_SOCKET_NAME       "wcnd"
#define MODEM_SOCKET_BUFFER_SIZE 128

static int modem_assert_flag = 0;
static int modem_reset_flag = 0;
static int modem_alive_flag = 0;
static struct slog_info *modem_info;


static void handle_init_modem_state(struct slog_info *info)
{
	char buffer[MAX_NAME_LEN];
	char modem_property[MAX_NAME_LEN];
	int ret = 0;

	if (!strncmp(info->name, "cp0", 3)) {
		property_get(MODEM_W_DEVICE_PROPERTY, modem_property, "");
		ret = atoi(modem_property);
	} else if (!strncmp(info->name, "cp1", 3)) {
		property_get(MODEM_TD_DEVICE_PROPERTY, modem_property, "");
		ret = atoi(modem_property);
	} else if (!strncmp(info->name, "cp2", 3)) {
		property_get(MODEM_WCN_DEVICE_PROPERTY, modem_property, "");
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

	if (!strncmp(info->name, "cp0", 3)) {
		property_get(MODEM_W_DIAG_PROPERTY, modem_property, "");
		open_device(info, modem_property);
	} else if (!strncmp(info->name, "cp1", 3)) {
		property_get(MODEM_TD_DIAG_PROPERTY, modem_property, "");
		open_device(info, modem_property);
	} else if (!strncmp(info->name, "cp2", 3)) {
		property_get(MODEM_WCN_DIAG_PROPERTY, modem_property, "");
		open_device(info, modem_property);
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
	if(!strncmp(info->name, "cp0", 3)) {
		sprintf(buffer, "cat /proc/cpw/mem > %s/%s/%s/%s_memory_%d%02d-%02d-%02d-%02d-%02d.log", current_log_path, top_logdir, info->name, info->name,
				tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	} else if(!strncmp(info->name, "cp1", 3)) {
		sprintf(buffer, "cat /proc/cpt/mem > %s/%s/%s/%s_memory_%d%02d-%02d-%02d-%02d-%02d.log", current_log_path, top_logdir, info->name, info->name,
				tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	} else if(!strncmp(info->name, "cp2", 3)){
		sprintf(buffer, "cat /proc/cptwcn/mem > %s/%s/%s/%s_memory_%d%02d%02d%02d%02d%02d.log", current_log_path, top_logdir, info->name, info->name,
				tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	}
	if(buffer[0] != 0)
	{
		system(buffer);
	}
	err_log("Dump %s memory completed for shark.", info->name);
}

static int handle_correspond_modem(char *buffer)
{
	char modem_buffer[MAX_NAME_LEN];

	if(!strncmp(buffer, "WCN", 3)) {
		strcpy(modem_buffer, "cp2");
	} else if (!strncmp(buffer, "TD", 2)) {
		strcpy(modem_buffer, "cp1");
	} else if (!strncmp(buffer, "W ", 2)) {
		strcpy(modem_buffer, "cp0");
	} else {
		return 0;
	}

	modem_info = stream_log_head;
	while(modem_info) {
		if (!strncmp(modem_info->name, modem_buffer, 3))
			break;
		modem_info = modem_info->next;
	}

	return 1;
}


int connect_socket_server(char *server_name)
{
	int fd = 0;

	fd = socket_local_client(server_name, ANDROID_SOCKET_NAMESPACE_ABSTRACT, SOCK_STREAM);
	if(fd < 0) {
		err_log("slog bind server %s failed, try again.", server_name)
		sleep(1);
		fd = socket_local_client(server_name, ANDROID_SOCKET_NAMESPACE_ABSTRACT, SOCK_STREAM);
		if(fd < 0) {
			err_log("bind server %s failed.", server_name);
			return -1;
		}
	}

	err_log("bind server %s success", server_name);

	return fd;
}

void handle_socket_modem(char *buffer)
{
	int reset = 0;
	char modemrst_property[MODEM_SOCKET_BUFFER_SIZE];

	memset(modemrst_property, 0, sizeof(modemrst_property));
	property_get(MODEMRESET_PROPERTY, modemrst_property, "0");
	reset = atoi(modemrst_property);


	if(strstr(buffer, "Modem Assert") != NULL) {
		if(reset == 0) {
			if (handle_correspond_modem(buffer) == 1) {
				modem_assert_flag = 1;
				handle_dump_shark_sipc_info();
			}
		} else {
			if (handle_correspond_modem(buffer) == 1)
				modem_reset_flag =1;
				err_log("waiting for Modem Alive.");
		}
	} else if(strstr(buffer, "Modem Alive") != NULL) {
		if (handle_correspond_modem(buffer) == 1)
			modem_alive_flag = 1;
	} else if(strstr(buffer, "Modem Blocked") != NULL) {
		if (handle_correspond_modem(buffer) == 1) {
			modem_assert_flag = 1;
			handle_dump_shark_sipc_info();
		}
	}

}

void handle_socket_wcn(char *buffer)
{
	int dump = 0, reset = 0;
	char modemrst_property[MODEM_SOCKET_BUFFER_SIZE];

	memset(modemrst_property, 0, sizeof(modemrst_property));
	property_get(MODEM_WCN_DUMP_LOG,  modemrst_property, "0");
	dump = atoi(modemrst_property);

	memset(modemrst_property, 0, sizeof(modemrst_property));
	property_get(MODME_WCN_DEVICE_RESET,  modemrst_property, "0");
	reset = atoi(modemrst_property);


	if(strstr(buffer, "WCN-CP2-EXCEPTION") != NULL) {
		if(dump != 0) {
			if (handle_correspond_modem(buffer) == 1) {
				modem_assert_flag = 1;
				handle_dump_shark_sipc_info();
			}
		} else if(reset != 0) {
			if (handle_correspond_modem(buffer) == 1)
				modem_reset_flag =1;
				err_log("waiting for Modem Alive.");
		}
	} else if(strstr(buffer, "WCN-CP2-ALIVE") != NULL) {
		if (handle_correspond_modem(buffer) == 1)
			modem_alive_flag = 1;
	}
}

void *handle_modem_state_monitor(void *arg)
{
	int fd_modem, fd_wcn;
	int  ret, n;
	char buffer[MODEM_SOCKET_BUFFER_SIZE];
	fd_set readset, readset_tmp;
	int result, max = 0;
	struct timeval timeout;

	FD_ZERO(&readset_tmp);
	fd_modem = connect_socket_server(MODEM_SOCKET_NAME);
	if(fd_modem > 0) {
		max = fd_modem;
		FD_SET(fd_modem, &readset_tmp);
	}
	fd_wcn = connect_socket_server(WCN_SOCKET_NAME);
	if(fd_wcn > 0) {
		max = fd_wcn > max ? fd_wcn : max;
		FD_SET(fd_wcn, &readset_tmp);
	}

	while (slog_enable == SLOG_ENABLE) {
		timeout.tv_sec = 3;
		timeout.tv_usec = 0;
		FD_ZERO(&readset);
		memcpy(&readset, &readset_tmp, sizeof(readset_tmp));
		result = select(max + 1, &readset, NULL, NULL, &timeout);
		if(result == 0)
			continue;

		if(result < 0) {
			sleep(1);
			continue;
		}

		memset(buffer, 0, MODEM_SOCKET_BUFFER_SIZE);
		if(FD_ISSET(fd_modem, &readset)) {
			n = read(fd_modem, buffer, MODEM_SOCKET_BUFFER_SIZE);
			if(n > 0) {
				err_log("get %d bytes %s", n, buffer);
				handle_socket_modem(buffer);
			} else if(n <= 0) {
				err_log("get 0 bytes, sleep 10s, reconnect socket.");
				FD_CLR(fd_modem, &readset_tmp);
				close(fd_modem);
				sleep(10);
				fd_modem = connect_socket_server(MODEM_SOCKET_NAME);
				if(fd_modem > 0) {
					max = fd_modem > max ? fd_modem : max;
					FD_SET(fd_modem, &readset_tmp);
				}
			}
		} else if(FD_ISSET(fd_wcn, &readset)) {
			n = read(fd_wcn, buffer, MODEM_SOCKET_BUFFER_SIZE);
			if(n > 0) {
				err_log("get %d bytes %s", n, buffer);
				handle_socket_wcn(buffer);
			} else if(n <= 0) {
				err_log("get 0 bytes, sleep 10s, reconnect socket.");
				FD_CLR(fd_wcn, &readset_tmp);
				close(fd_wcn);
				sleep(10);
				fd_wcn = connect_socket_server(MODEM_SOCKET_NAME);
				if(fd_wcn > 0) {
					max = fd_wcn > max ? fd_wcn : max;
					FD_SET(fd_wcn, &readset_tmp);
				}
			}
		}
	}

	if(fd_modem > 0)
		close(fd_modem);
	if(fd_wcn > 0)
		close(fd_wcn);

	return NULL;
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

	err_log("Start to dump %s memory.", info->name);
	if( strncmp(info->name, "cp0", 3) && strncmp(info->name, "cp1", 3) && strncmp(info->name, "cp2", 3) )
	{
		err_log("info name error %s.", info->name);
		return;
	}
write_cmd:
	n = write(info->fd_device, cmddumpmemory, 2);
	if (n <= 0) {
		close(info->fd_device);
		sleep(1);
		handle_open_modem_device(info);
		goto write_cmd;
	}

	/* add timestamp */
	t = time(NULL);
	localtime_r(&t, &tm);
	sprintf(path, "%s/%s/%s/%s_memory_%d%02d-%02d-%02d-%02d-%02d.log", current_log_path, top_logdir, info->name, info->name,
				tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	fp = fopen(path, "a+b");
	if (fp == NULL) {
		err_log("open modem memory file failed!");
		return;
	}

	do {
		memset(buffer, 0, BUFFER_SIZE);
                FD_ZERO(&readset);
                FD_SET(info->fd_device, &readset);
                timeout.tv_sec = 3;
                timeout.tv_usec = 0;
		ret = select(info->fd_device + 1, &readset, NULL, NULL, &timeout);

		if( 0 == ret ){
			/* for shark, when CP can not send integral log to AP, slog will use another way to save CP memory*/
			if( receive_from_cp < 10 )
				handle_dump_modem_memory_from_proc(info);
			err_log("select timeout ->save finsh");
			finish = 1;
		} else if( ret > 0 ) {
read_again:
			n = read(info->fd_device, buffer, BUFFER_SIZE);
			if (n == 0) {
				close(info->fd_device);
				sleep(1);
				handle_open_modem_device(info);
			} else if (n < 0) {
				err_log("fd=%d read %d is lower than 0", info->fd_device, n);
				sleep(1);
				goto read_again;
			} else {
				receive_from_cp ++;
				fwrite(buffer, n, 1, fp);
			}
		} else {
			err_log("select error");
		}
	} while( finish == 0 );

	err_log("Dump %s memory completed.", info->name);

	return;
}

void *modem_log_handler(void *arg)
{
	struct slog_info *info;
	char cp_buffer[BUFFER_SIZE];
	int ret = 0, totalLen = 0, max = 0;
	fd_set readset_tmp, readset;
	int result;
	struct timeval timeout;
#ifdef LOW_POWER_MODE
#define MODEM_CIRCULAR_SIZE		(2 * 1024 * 1024) /* 2 MB */
	time_t t;
	struct tm tm;
	char buffer[MAX_NAME_LEN];
	FILE *data_fp;
	unsigned char *ring_buffer_start;
	unsigned char *ring_buffer_head;
	unsigned int ring_buffer_full = 0;

	if(slog_enable == SLOG_LOW_POWER) {
		ring_buffer_start = malloc(MODEM_CIRCULAR_SIZE);
		ring_buffer_head = ring_buffer_start;
		memset(ring_buffer_start, 0, MODEM_CIRCULAR_SIZE);
	}
#endif
	if(slog_enable == SLOG_DISABLE)
		return NULL;

	info = stream_log_head;
	FD_ZERO(&readset_tmp);
	while (info) {
		if(info->state != SLOG_STATE_ON) {
			info = info->next;
			continue;
		}

		if (!strncmp(info->name, "cp0", 3)) {
			handle_init_modem_state(info);
			if(info->state == SLOG_STATE_ON) {
#ifdef LOW_POWER_MODE
				if(slog_enable != SLOG_LOW_POWER)
#endif
				info->fp_out = gen_outfd(info);
				handle_open_modem_device(info);
			} else {
				info = info->next;
				continue;
			}
		} else if (!strncmp(info->name, "cp1", 3)) {
			handle_init_modem_state(info);
			if(info->state == SLOG_STATE_ON) {
#ifdef LOW_POWER_MODE
				if(slog_enable != SLOG_LOW_POWER)
#endif
				info->fp_out = gen_outfd(info);
				handle_open_modem_device(info);
			} else {
				info = info->next;
				continue;
			}
		} else if (!strncmp(info->name, "cp2", 3)) {
			handle_init_modem_state(info);
			if(info->state == SLOG_STATE_ON) {
#ifdef LOW_POWER_MODE
				if(slog_enable != SLOG_LOW_POWER)
#endif
				info->fp_out = gen_outfd(info);
				handle_open_modem_device(info);
			} else {
				info = info->next;
				continue;
			}
		} else {
			info = info->next;
			continue;
		}

		FD_SET(info->fd_device, &readset_tmp);
		if(info->fd_device > max)
			max = info->fd_device;

		info = info->next;
	}

	if(max == 0) {
		err_log("modem all disabled!");
		return NULL;
	}

#ifdef LOW_POWER_MODE
	if(slog_enable != SLOG_LOW_POWER)
#endif
	pthread_create(&modem_state_monitor_tid, NULL, handle_modem_state_monitor, NULL);

	modem_log_handler_started = 1;
	while(slog_enable != SLOG_DISABLE) {

		if(modem_assert_flag == 1) {
			err_log("Modem %s Assert!", modem_info->name);
			handle_dump_modem_memory(modem_info);
			FD_CLR(modem_info->fd_device, &readset_tmp);
			close(modem_info->fd_device);
			modem_assert_flag = 0;
			property_set(MODEM_WCN_DUMP_LOG_COMPLETE, "1");
		}

		if(modem_reset_flag == 1) {
			err_log("Modem %s Reset!", modem_info->name);
			FD_CLR(modem_info->fd_device, &readset_tmp);
			close(modem_info->fd_device);
			modem_reset_flag = 0;

		}

		if(modem_alive_flag == 1) {
			err_log("Modem %s Alive!", modem_info->name);
			handle_open_modem_device(modem_info);
			FD_SET(modem_info->fd_device, &readset_tmp);
			if(modem_info->fd_device > max)
				max = modem_info->fd_device;
			modem_alive_flag = 0;
		}

		timeout.tv_sec = 3;
		timeout.tv_usec = 0;
		FD_ZERO(&readset);
		memcpy(&readset, &readset_tmp, sizeof(readset_tmp));
		result = select(max + 1, &readset, NULL, NULL, &timeout);

		if(result == 0)
			continue;

		if(result < 0) {
			sleep(1);
			continue;
		}

		info = stream_log_head;
		while(info) {

			if(info->state != SLOG_STATE_ON){
				info = info->next;
				continue;
			}

			if(FD_ISSET(info->fd_device, &readset) <= 0) {
				info = info->next;
				continue;
			}

			if( !strncmp(info->name, "cp0", 3) || !strncmp(info->name, "cp1", 3) || !strncmp(info->name, "cp2", 3) ) {
				memset(cp_buffer, 0, BUFFER_SIZE);
				ret = read(info->fd_device, cp_buffer, BUFFER_SIZE);
				if(ret <= 0) {
					if ( (ret == -1 && (errno == EINTR || errno == EAGAIN) ) || ret == 0 ) {
						info = info->next;
						continue;
					}
					err_log("read %s log failed!", info->name);
					FD_CLR(info->fd_device, &readset_tmp);
					close(info->fd_device);
					sleep(1);
					handle_open_modem_device(info);
					FD_SET(info->fd_device, &readset_tmp);
					if(info->fd_device > max)
						max = info->fd_device;
					info = info->next;
					continue;
				}
#ifdef LOW_POWER_MODE
				if(slog_enable == SLOG_LOW_POWER && !strncmp(info->name, "cp0", 3)) {
					if((ring_buffer_head + ret) < (ring_buffer_start + MODEM_CIRCULAR_SIZE)) {
						memcpy(ring_buffer_head, cp_buffer, ret);
						ring_buffer_head += ret;
					} else {
						memset(ring_buffer_head, 0, ring_buffer_start + MODEM_CIRCULAR_SIZE - ring_buffer_head);
						ring_buffer_head = ring_buffer_start;
						memcpy(ring_buffer_head, cp_buffer, ret);
						ring_buffer_head += ret;
						ring_buffer_full = 1;
					}

					if(hook_modem_flag == 1) {
						hook_modem_flag = 0;
						/* add timestamp */
						t = time(NULL);
						localtime_r(&t, &tm);
						sprintf(buffer, "%s/%s_%02d-%02d-%02d.log", HOOK_MODEM_TARGET_DIR,
							info->name, tm.tm_hour, tm.tm_min, tm.tm_sec);
						data_fp = fopen(buffer, "a+b");
						if(data_fp == NULL) {
							err_log("open cp log file failed!");
							info = info->next;
							continue;
						}
						if(ring_buffer_full == 1) {
							fwrite(ring_buffer_head, ring_buffer_start + MODEM_CIRCULAR_SIZE - ring_buffer_head, 1, data_fp);
							fwrite(ring_buffer_start, ring_buffer_head - ring_buffer_start, 1, data_fp);
						} else
							fwrite(ring_buffer_start, ring_buffer_head - ring_buffer_start, 1, data_fp);
						fclose(data_fp);
					}

					info = info->next;
					continue;
				}
#endif
				totalLen = fwrite(cp_buffer, ret, 1, info->fp_out);
				if ( totalLen != 1 ) {
					fclose(info->fp_out);
					sleep(1);
					info->fp_out = gen_outfd(info);
				} else {
					info->outbytecount += ret;
					log_size_handler(info);
				}
			}

			info = info->next;
		}

	}

	modem_log_handler_started = 0;

	return NULL;

}
