/*
 * =====================================================================================
 *
 *       Filename:  eng_wifi_ptest.c
 *
 *    Description:  Csr wifi production test in engineering mode
 *
 *        Version:  1.0
 *        Created:  10/02/2011 03:25:03 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Binary Yang <Binary.Yang@spreadtrum.com.cn>
 *        Company:  © Copyright 2010 Spreadtrum Communications Inc.
 *
 * =====================================================================================
 */


#include	<stdint.h>
#include	<stddef.h>
#include	<stdarg.h>
#include	<unistd.h>
#include	<stdlib.h>
#include	<stdio.h>
#include	<sys/types.h>
#include	<sys/stat.h>
#include	<sys/socket.h>
#include	<sys/un.h>
#include	<fcntl.h>
#include	<string.h>
#include	<pthread.h>
#include	<errno.h>
#include	"eng_wifi_ptest.h"
#include	"engopt.h"




#define	        WIFI_PTEST_PIPE          "/etc/wifi_ptest"			/* the pipe used to send cmd to synergy ptest */
#define OPEN_WIFI   1
#define CLOSE_WIFI  0
#define OPEN_BT   1
#define CLOSE_BT  0

#define SOCKET_BUF_LEN	1024

#define TEST_OK	"test_ok"
#define TEST_ERROR	"test_err"

#define TYPE_OFFSET 1
#define CMD_OFFSET 7

#define BROADCOM_WIFI	1
#define BROADCOM_BT		2
#define CLOSE_SOCKET	3

static PTEST_RES_T wifi_cfm;
static int cli_fd = -1;
static char socket_write_buf[SOCKET_BUF_LEN] = {0};


void set_value(int val){
	close(cli_fd);
}

int hardwaretest_client(void)
{
	int fd;
	fd = socket_local_client( "hardwaretest",0, SOCK_STREAM);
	if (fd < 0) {
		ENG_LOG("hardwaretest Unable to bind socket errno:%d[%s]", errno, strerror(errno));
	}

	return fd;
}

//for wifi eut test
PTEST_RES_T *wifi_ptest_init(PTEST_CMD_T *ptest){
	int res = 0;
	char req[32] = {0}; 

	ENG_LOG("===========%s",__func__);
	if(cli_fd < 0){
		cli_fd = hardwaretest_client();
		if(cli_fd < 0){
			return NULL;
		}
	}

	memset(req, 0, sizeof(req));
	memset(&wifi_cfm, 0, sizeof(PTEST_RES_T));
	memset(socket_write_buf, 0, SOCKET_BUF_LEN);
	socket_write_buf[0] = BROADCOM_WIFI;
	memcpy(socket_write_buf+TYPE_OFFSET,"START",strlen("START"));
	res = write(cli_fd,socket_write_buf,SOCKET_BUF_LEN);
	if (res <= 0) {
		close(cli_fd);
		cli_fd = -1;
		return NULL;
	}
	res = read(cli_fd,req,sizeof(req));
	if (res > 0 && !strcmp(req,TEST_OK)) {
		wifi_cfm.result = 0;	
		return &wifi_cfm;
	}

	ENG_LOG("===========%s failed",__func__);
	return NULL;
}

PTEST_RES_T *wifi_ptest_cw(PTEST_CMD_T *ptest){
	int res = 0;
	char req[32] = {0};
	char cmd[16] = {0};
 
	ENG_LOG("===========%s",__func__);
	if(cli_fd < 0){
		cli_fd = hardwaretest_client();
		if(cli_fd < 0){
			return NULL;
		}
	}
	memset(req, 0, sizeof(req));
	memset(cmd, 0, sizeof(cmd));
	memset(&wifi_cfm, 0, sizeof(PTEST_RES_T));
	memset(socket_write_buf, 0, SOCKET_BUF_LEN);

	socket_write_buf[0] = BROADCOM_WIFI;
	memcpy(socket_write_buf+TYPE_OFFSET,"CWTEST",strlen("CWTEST"));
	memcpy(socket_write_buf+TYPE_OFFSET+CMD_OFFSET,ptest,sizeof(PTEST_CMD_T));
	res = write(cli_fd,socket_write_buf,SOCKET_BUF_LEN);
	if (res <= 0) {
		close(cli_fd);
		cli_fd = -1;
		return NULL;
	}
	res = read(cli_fd,req,sizeof(req));
	if (res > 0 && !strcmp(req,TEST_OK)) {
		wifi_cfm.result = 0;
		return &wifi_cfm;
	}

	ENG_LOG("===========%s failed",__func__);
	return NULL;
}

PTEST_RES_T *wifi_ptest_tx(PTEST_CMD_T *ptest){
	int res = 0;
	char req[32] = {0}; 

	ENG_LOG("===========%s",__func__);
	if(cli_fd < 0){
		cli_fd = hardwaretest_client();
		if(cli_fd < 0){
			return NULL;
		}
	}

	memset(req, 0, sizeof(req));
	memset(&wifi_cfm, 0, sizeof(PTEST_RES_T));
	memset(socket_write_buf, 0, SOCKET_BUF_LEN);

	socket_write_buf[0] = BROADCOM_WIFI;
	memcpy(socket_write_buf+TYPE_OFFSET,"TXTEST",strlen("TXTEST"));
	memcpy(socket_write_buf+TYPE_OFFSET+CMD_OFFSET,ptest,sizeof(PTEST_CMD_T));
	res = write(cli_fd,socket_write_buf,SOCKET_BUF_LEN);
	if (res <= 0) {
		close(cli_fd);
		cli_fd = -1;
		return NULL;
	}
	res = read(cli_fd,req,sizeof(req));
	if (res > 0 && !strcmp(req,TEST_OK)) {
		wifi_cfm.result = 0;	
		return &wifi_cfm;
	}
	ENG_LOG("===========%s failed",__func__);
	return NULL;
}

PTEST_RES_T *wifi_ptest_rx(PTEST_CMD_T *ptest){
	int res = 0;
	char req[32] = {0};

	ENG_LOG("===========%s",__func__);
	if(cli_fd < 0){
		cli_fd = hardwaretest_client();
		if(cli_fd < 0){
			return NULL;
		}
	}
	memset(req, 0, sizeof(req));
	memset(&wifi_cfm, 0, sizeof(PTEST_RES_T));
	memset(socket_write_buf, 0, SOCKET_BUF_LEN);

	socket_write_buf[0] = BROADCOM_WIFI;
	memcpy(socket_write_buf+TYPE_OFFSET,"RXTEST",strlen("RXTEST"));
	memcpy(socket_write_buf+TYPE_OFFSET+CMD_OFFSET,ptest,sizeof(PTEST_CMD_T));
	res = write(cli_fd,socket_write_buf,SOCKET_BUF_LEN);
	if (res <= 0) {
		close(cli_fd);
		cli_fd = -1;
		return NULL;
	}
	res = read(cli_fd,req,sizeof(req));
	if (res > 0 && !strcmp(req,TEST_OK)) {
		wifi_cfm.result = 0;	
		return &wifi_cfm;
	}

	ENG_LOG("===========%s failed",__func__);
	return NULL;
}

PTEST_RES_T *wifi_ptest_deinit(PTEST_CMD_T *ptest){
	int res = 0;
	char req[32] = {0}; 

	ENG_LOG("===========%s",__func__);
	if(cli_fd < 0){
		cli_fd = hardwaretest_client();
		if(cli_fd < 0){
			return NULL;
		}
	}
	memset(req, 0, sizeof(req));
	memset(&wifi_cfm, 0, sizeof(PTEST_RES_T));
	memset(socket_write_buf, 0, SOCKET_BUF_LEN);

	socket_write_buf[0] = BROADCOM_WIFI;
	memcpy(socket_write_buf+TYPE_OFFSET,"STOP",strlen("STOP"));
	res = write(cli_fd,socket_write_buf,SOCKET_BUF_LEN);
	if (res <= 0) {
		close(cli_fd);
		cli_fd = -1;
		return NULL;
	}
	res = read(cli_fd,req,sizeof(req));
	if (res > 0 && !strcmp(req,TEST_OK)) {
		memset(socket_write_buf, 0, SOCKET_BUF_LEN);
		socket_write_buf[0] = CLOSE_SOCKET;
		write(cli_fd,socket_write_buf,SOCKET_BUF_LEN);
		if (cli_fd >= 0) {
			close(cli_fd);
			cli_fd = -1;
		}
		wifi_cfm.result = 0;	
		return &wifi_cfm;
	}

	ENG_LOG("===========%s failed",__func__);
	return NULL;
}

//for  bt eut test
int bt_ptest_start(void){
	int res = 0;
	char req[32] = {0}; 

	ENG_LOG("===========%s",__func__);
	if(cli_fd < 0){
		cli_fd = hardwaretest_client();
		if(cli_fd < 0){
			return 1;
		}
	}
	memset(req, 0, sizeof(req));
	memset(&wifi_cfm, 0, sizeof(PTEST_RES_T));
	memset(socket_write_buf, 0, SOCKET_BUF_LEN);

	socket_write_buf[0] = BROADCOM_BT;
	memcpy(socket_write_buf+TYPE_OFFSET,"START",strlen("START"));
	res = write(cli_fd,socket_write_buf,SOCKET_BUF_LEN);
	if (res <= 0) {
		close(cli_fd);
		cli_fd = -1;
		return 1;
	}
	res = read(cli_fd,req,sizeof(req));
	if (res > 0 && !strcmp(req,TEST_OK)) {
		return 0;
	}

	ENG_LOG("===========%s failed",__func__);
	return 1;
}

int bt_ptest_stop(void){
	int res = 0;
	char req[32] = {0}; 

	ENG_LOG("===========%s",__func__);
	if(cli_fd < 0){
		cli_fd = hardwaretest_client();
		if(cli_fd < 0){
			return NULL;
		}
	}
	memset(req, 0, sizeof(req));
	memset(&wifi_cfm, 0, sizeof(PTEST_RES_T));
	memset(socket_write_buf, 0, SOCKET_BUF_LEN);

	socket_write_buf[0] = BROADCOM_BT;
	memcpy(socket_write_buf+TYPE_OFFSET,"STOP",strlen("STOP"));
	res = write(cli_fd,socket_write_buf,SOCKET_BUF_LEN);
	if (res <= 0) {
		close(cli_fd);
		cli_fd = -1;
		return 1;
	}
	res = read(cli_fd,req,sizeof(req));
	if (res > 0 && !strcmp(req,TEST_OK)) {
		memset(socket_write_buf, 0, SOCKET_BUF_LEN);
		socket_write_buf[0] = CLOSE_SOCKET;
		write(cli_fd,socket_write_buf,SOCKET_BUF_LEN);
		if (cli_fd >= 0) {
			close(cli_fd);
			cli_fd = -1;
		}
		return 0;
	}

	ENG_LOG("===========%s failed",__func__);
	return 1;
}
