#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <cutils/sockets.h>
#include <pthread.h>
#include <utils/Log.h>
#include "engopt.h"
#include "eng_hardware_test.h"

static char socket_read_buf[SOCKET_BUF_LEN] = {0};
static char socket_write_buf[SOCKET_BUF_LEN] = {0};

static void hardware_broadcom_wifi_test(char* buf)
{
	char req[32] = {0};
	int channel = 0;
	int rate = 0;
	int powerLevel = 0;
	char cmd[16] = {0};

	memset(req, 0, sizeof(req));
	memset(cmd, 0, sizeof(cmd));
	if (0 == strncmp(buf+TYPE_OFFSET,"START",strlen("START"))) {
		ALOGE("hardware_broadcom_wifi_test START");
		wifieut(OPEN_WIFI,req);
		if (!strcmp(req,EUT_WIFI_OK)) {
			memcpy(socket_write_buf,TEST_OK,strlen(TEST_OK)+1);
		} else {
			memcpy(socket_write_buf,TEST_ERROR,strlen(TEST_ERROR)+1);
		}
	} else if (0 == strncmp(buf+TYPE_OFFSET,"STOP",strlen("STOP"))) {
		ALOGE("hardware_broadcom_wifi_test STOP");
		wifieut(CLOSE_WIFI,req);
		if (!strcmp(req,EUT_WIFI_OK)) {
			memcpy(socket_write_buf,TEST_OK,strlen(TEST_OK)+1);
		} else {
			memcpy(socket_write_buf,TEST_ERROR,strlen(TEST_ERROR)+1);
		}
	} else if (0 == strncmp(buf+TYPE_OFFSET,"CWTEST",strlen("CWTEST"))) {
		ALOGE("hardware_broadcom_wifi_test CWTEST");
		PTEST_CMD_T *ptest = (PTEST_CMD_T *)(buf+TYPE_OFFSET+CMD_OFFSET);
		channel = ptest->channel;

		sprintf(cmd,"wl crusprs %d", channel);
		system(cmd);
		ALOGE("cmd = %s",cmd);
		memcpy(socket_write_buf,TEST_OK,strlen(TEST_OK)+1);
	} else if (0 == strncmp(buf+TYPE_OFFSET,"TXTEST",strlen("TXTEST"))) {
		ALOGE("hardware_broadcom_wifi_test type TXTEST");
		PTEST_CMD_T *ptest = (PTEST_CMD_T *)(buf+TYPE_OFFSET+CMD_OFFSET);
		channel = ptest->channel;
		rate = ptest->ptest_tx.rate;
		powerLevel = ptest->ptest_tx.powerLevel;

		set_wifi_ratio(rate,req);
		set_wifi_ch(channel,req);
		set_wifi_tx_factor_83780(powerLevel,req);

		wifi_tx(OPEN_WIFI,req);
		if (!strcmp(req,EUT_WIFI_OK)) {
			memcpy(socket_write_buf,TEST_OK,strlen(TEST_OK)+1);
		} else {
			memcpy(socket_write_buf,TEST_ERROR,strlen(TEST_ERROR)+1);
		}
	} else if (0 == strncmp(buf+TYPE_OFFSET,"RXTEST",strlen("RXTEST"))) {
		ALOGE("hardware_broadcom_wifi_test RXTEST");
		PTEST_CMD_T *ptest = (PTEST_CMD_T *)(buf+TYPE_OFFSET+CMD_OFFSET);
		channel = ptest->channel;

		set_wifi_ch(channel,req);
		wifi_rx(OPEN_WIFI,req);
		if (!strcmp(req,EUT_WIFI_OK)) {
			memcpy(socket_write_buf,TEST_OK,strlen(TEST_OK)+1);
		} else {
			memcpy(socket_write_buf,TEST_ERROR,strlen(TEST_ERROR)+1);
		}
	} else {
		memcpy(socket_write_buf,TEST_ERROR,strlen(TEST_ERROR)+1);
	}
}

static void hardware_broadcom_bt_test(char* buf)
{
	char req[32] = {0};
	
	memset(req, 0, sizeof(req));
	if (0 == strncmp(buf+TYPE_OFFSET,"START",strlen("START"))) {
		ALOGE("hardware_broadcom_bt_test START");
		bteut(OPEN_BT, req);
		if (!strcmp(req,EUT_BT_OK)) {
			memcpy(socket_write_buf,TEST_OK,strlen(TEST_OK));
                        socket_write_buf[strlen(TEST_OK)] = '\0';
		} else {
			memcpy(socket_write_buf,TEST_ERROR,strlen(TEST_ERROR));
                        socket_write_buf[strlen(TEST_ERROR)] = '\0';
		}
	} else if (0 == strncmp(buf+TYPE_OFFSET,"STOP",strlen("STOP"))) {
		ALOGE("hardware_broadcom_bt_test STOP");
		bteut(CLOSE_BT, req);
		if (!strcmp(req,EUT_BT_OK)) {
			memcpy(socket_write_buf,TEST_OK,strlen(TEST_OK));
                        socket_write_buf[strlen(TEST_OK)] = '\0';
		} else {
			memcpy(socket_write_buf,TEST_ERROR,strlen(TEST_ERROR));
                        socket_write_buf[strlen(TEST_ERROR)] = '\0';
		}
	} else {
		memcpy(socket_write_buf,TEST_ERROR,strlen(TEST_ERROR));
                socket_write_buf[strlen(TEST_ERROR)] = '\0';
	}
}

static int hardware_test_function(char* buf)
{
	int type;

	type = buf[0];
	ALOGE("hardware_test_function type = %d", type);
	switch(type) {
		case BROADCOM_WIFI:
			hardware_broadcom_wifi_test(buf);
			break;
		case BROADCOM_BT:
			hardware_broadcom_bt_test(buf);
			break;
		case CLOSE_SOCKET:
			ALOGE("hardware_test_function  CLOSE SOCKET");
			break;
		default:
			break;
	}

	return type;
}

static void hardwaretest_thread(void *fd)
{
	int soc_fd;
	int ret;
	int length;
	int type;
	fd_set readfds;

	soc_fd = *(int *)fd;
	ALOGE("hardwaretest_thread  soc_fd = %d", soc_fd);
	while(1) { 
		FD_ZERO(&readfds);
		FD_SET(soc_fd,&readfds);
		ret = select(soc_fd+1,&readfds,NULL,NULL,NULL);
		if (ret < 0) {
			ALOGE("hardwaretest_thread  ret = %d, break",ret);
			break;
		}
		memset(socket_read_buf,0,SOCKET_BUF_LEN);
		memset(socket_write_buf,0,SOCKET_BUF_LEN);
		if (FD_ISSET(soc_fd,&readfds)) {
			length = eng_read(soc_fd,socket_read_buf,SOCKET_BUF_LEN);
			if (length <= 0) {
				ALOGE("hardwaretest_thread  length = %d, break",length);
				break;
			}
			type = hardware_test_function(socket_read_buf);
			if (type == CLOSE_SOCKET) {
				ALOGE("hardwaretest_thread  CLOSE_SOCKET break");
				break;
			}
			eng_write(soc_fd,socket_write_buf,strlen(socket_write_buf));
		}
	}
	ALOGE("hardwaretest_thread  CLOSE_SOCKET");
	close(soc_fd);
}

static int hardwaretest_server(void)
{
	int ret;
	ret = socket_local_server ("hardwaretest",
				0, SOCK_STREAM);

	if (ret < 0) {
		ALOGE("hardwaretest server Unable to bind socket errno:%d", errno);
		exit (-1);
	}
	return ret;
}

int main(void)
{
	int socket;
	int fd;
	struct sockaddr addr;
	socklen_t alen;
	pthread_t thread_id;

	socket = hardwaretest_server();
	if (socket == -1) {
		return -1;
	}

	ENG_LOG("hardwaretest server start listen\n");

	alen = sizeof(addr);

	for (; ;) {
		if ((fd=accept(socket,&addr,&alen)) == -1) {
			ALOGE("hardwaretest server accept error\n");
			continue;
		}

		if (0 != pthread_create(&thread_id, NULL, (void *)hardwaretest_thread, &fd)) {
			ALOGE("hardwaretest thread create error\n");
		}
	}

	return 0;
}
