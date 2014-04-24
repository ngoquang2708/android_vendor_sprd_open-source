/**********************************************************************\
*                Copyright (C) Spreadtrum Ltd, 2011.                   *
*                                                                      *
* This program is free software. You may use, modify, and redistribute *
* it under the terms of the GNU Affero General Public License as       *
* published by the Free Software Foundation, either version 3 or (at   *
* your option) any later version. This program is distributed without  *
* any warranty. See the file COPYING for details.                      *
* Author Yingchun Li (Yingchun.li@spreadtrum.com)
\**********************************************************************/

/* vlog_sv.c

   A simple Internet stream socket server. Our service is to send modem
   log to clients.

   Usage:  vlog_sv
*/
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/resource.h>
#define LOG_TAG "vlog-sv"
#include <cutils/log.h>

#define LOG_SERVER	"192.168.42.129"
#define PORT_NUM 36667        /* Port number for server */

#define BACKLOG 50
#define DATA_BUF_SIZE (4096)
#define CMD_BUF_SIZE (2048)

static char log_data_buf[DATA_BUF_SIZE];
static char cmd_data_buf[CMD_BUF_SIZE];

#define CLIENT_DEBUG

//#define DBG printf
#define DBG ALOGI
struct channel{
	int from;
	int to;
}log_chan, diag_chan;

static volatile int wire_connected = 0;
static void *log_handler(void *args)
{
	struct channel *log = (struct channel *)args;
	static char *code = "log channel exit";
	struct timeval tx_timeout;

	tx_timeout.tv_sec = 2;
	tx_timeout.tv_usec = 0;
	if (setsockopt(log->to, SOL_SOCKET, SO_SNDTIMEO, &tx_timeout, sizeof(struct timeval))
			== -1) {
		 DBG("setsockopt tx timeout error\n");
		 exit (-1);
	}
	/* Read client request, send sequence number back */
	for (;;) {
		int cnt, res;
		cnt = read(log->from, log_data_buf, DATA_BUF_SIZE);
		//DBG("read from log %d\n", cnt);
		res = send(log->to, log_data_buf, cnt, 0);
		//DBG("write to socket %d\n", res);
		if (res < 0) {
			DBG("write socket error %s\n", strerror(errno));
			wire_connected = 0;
			break;
		}
	}
	return code;
}

static void *diag_handler(void *args)
{
	struct channel *diag = (struct channel *)args;
	static char *code = "diag channel exit";
	fd_set rfds;
	struct timeval tv;

	FD_ZERO(&rfds);
	FD_SET(diag->from, &rfds);

	/* Read client request, send sequence number back */
	while (wire_connected) {
		int cnt, res;
		/* Wait up to  two seconds. */
		tv.tv_sec = 2;
		tv.tv_usec = 0;
		FD_SET(diag->from, &rfds);
		res = select(diag->from + 1, &rfds, NULL, NULL, &tv);
		if (res <= 0) { //timeout or other error
		//	DBG("No data within five seconds. res:%d\n", res);
			continue;
		}
		//cnt = read(diag->from, cmd_data_buf, DATA_BUF_SIZE);
		cnt = recv(diag->from, cmd_data_buf, CMD_BUF_SIZE,
				MSG_DONTWAIT);
		//DBG("read from socket %d\n", cnt);
		if (cnt <= 0) {
			DBG("read socket error %s\n", strerror(errno));
			break;
		}
		res = write(diag->to, cmd_data_buf, cnt);
		//DBG("write to  modem %d\n", res);
	}
	//pthread_exit(code);
	return code;
}

#define VLOG_PRI  -20
static void set_app_priority(void)
{
	int inc = VLOG_PRI;
	int res = 0;

	errno = 0;
	res = nice(inc);
	if (res < 0){
		DBG("cannot set vlog priority, res:%d ,%s\n", res,
				strerror(errno));
	}
	return;
}

int
main(int argc, char *argv[])
{
	struct sockaddr claddr;
	int lfd, cfd, optval;
	int log_fd;
	struct sockaddr_in sock_addr;
	socklen_t addrlen;
#ifdef CLIENT_DEBUG
#define ADDRSTRLEN (128)
	char addrStr[ADDRSTRLEN];
	char host[50];
	char service[30];
#endif
	pthread_t tlog, tdiag;
	pthread_attr_t attr;

	DBG("vlog server version 1.0\n");
	set_app_priority();

	memset(&sock_addr, 0, sizeof (struct sockaddr_in));
	sock_addr.sin_family = AF_INET;        /* Allows IPv4*/
	sock_addr.sin_addr.s_addr = INADDR_ANY;/* Wildcard IP address;*/
	//res = inet_pton(AF_INET, LOG_SERVER, &sock_addr.sin_addr.s_addr);
	//DBG("convers net address res:%d\n", res);
	sock_addr.sin_port = htons(PORT_NUM);

	lfd = socket(sock_addr.sin_family, SOCK_STREAM, 0);
	if (lfd == -1) {
		 DBG("socket error\n");
		 exit (-1);
	}

	optval = 1;
	if (setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval))
			== -1) {
		 DBG("setsockopt error\n");
		 exit (-1);
	}

	if (bind(lfd, (struct sockaddr *)&sock_addr, sizeof (struct sockaddr_in)) != 0){
		DBG("bind error\n");
		exit (-1);
	}


	if (listen(lfd, BACKLOG) == -1){
		DBG("listen error\n");
		exit (-1);
	}


	log_fd = open("/dev/vbpipe0", O_RDWR);
	if (log_fd < 0){
		DBG("can not open vpbipe 0\n");
		exit(-1);
	}

	pthread_attr_init(&attr);
	for (;;) {                  /* Handle clients iteratively */
		void * res;
		int ret;

		DBG("log server waiting client dail in...\n");
		/* Accept a client connection, obtaining client's address */
		addrlen = sizeof(struct sockaddr);
		cfd = accept(lfd, &claddr, &addrlen);
		if (cfd == -1) {
			DBG("accept error %s\n", strerror(errno));
			continue;
		}
		DBG("log server connected with client\n");
		wire_connected = 1;
		/* Ignore the SIGPIPE signal, so that we find out about broken
		 * connection errors via a failure from write().
		 */
		if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
			DBG("signal error\n");
#ifdef CLIENT_DEBUG
		addrlen = sizeof(struct sockaddr);
		if (getnameinfo(&claddr, addrlen, host, 50, service,
			 30, NI_NUMERICHOST) == 0)
			snprintf(addrStr, ADDRSTRLEN, "(%s, %s)", host, service);
		else
			snprintf(addrStr, ADDRSTRLEN, "(?UNKNOWN?)");
		DBG("Connection from %s\n", addrStr);
#endif
		//create a thread for loging;
		log_chan.from = log_fd;
		log_chan.to = cfd;
		ret = pthread_create(&tlog, &attr, log_handler, &log_chan);
		if (ret != 0) {
			DBG("log thread create success\n");
			break;
		}

		//create a thread for recv cmd
		diag_chan.from = cfd;
		diag_chan.to = log_fd;
		ret = pthread_create(&tdiag, &attr, diag_handler, &diag_chan);
		if (ret != 0) {
			DBG("diag thread create success\n");
			break;
		}

		//we wait the thread exit;
		pthread_join(tlog, &res);
		DBG("log thread exit success %s\n", (char *)res);

		pthread_join(tdiag, &res);
		DBG("diag thread exit success %s\n", (char *)res);
		if (close(cfd) == -1)           /* Close connection */
			DBG("close socket error\n");
	}
	pthread_attr_destroy(&attr);
	return 0;
}
