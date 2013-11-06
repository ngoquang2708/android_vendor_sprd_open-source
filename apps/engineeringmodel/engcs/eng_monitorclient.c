/*
 * File:         eng_monitorclient.c
 * Based on:
 * Author:       Yunlong Wang <Yunlong.Wang@spreadtrum.com>
 *
 * Created:	  2011-03-16
 * Description:  create pc client in android for engneer mode
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/delay.h>
#include <sys/socket.h>

#include "engopt.h"
#include "engat.h"
#include "engclient.h"

#include "cutils/sockets.h"
#include "cutils/properties.h"

/*******************************************************************************
* Function    :  eng_monitor_handshake
* Description :  client register to server
* Parameters  :  fd: socket fd
* Return      :    none
*******************************************************************************/
static int eng_monitor_handshake( int fd)
{
	struct eng_buf_struct data;
	
	memset(data.buf,0,ENG_BUF_LEN*sizeof(unsigned char));
	strcpy((char*)data.buf, ENG_MONITOR);
	eng_write(fd, data.buf, strlen(ENG_MONITOR));
	
	memset(data.buf,0,ENG_BUF_LEN*sizeof(unsigned char));
	eng_read(fd,data.buf, ENG_BUF_LEN);
	if ( strncmp((const char*)data.buf,ENG_WELCOME,strlen(ENG_WELCOME)) == 0){
		return 0;
	}
	ENG_LOG("%s: handshake error read=%s", __FUNCTION__,data.buf);
	return -1;
}

int eng_monitor_open(void)
{
	int counter=0;
	int soc_fd;
	int err=0;
	
	//connect to server
	soc_fd = eng_client(ENG_SOCKET_PORT, SOCK_STREAM);

	while(soc_fd <= 0) {
	    ENG_LOG ("%s: opening engmode server socket failed\n", __FUNCTION__);
		ENG_LOG("%s: soc_fd=%d",__func__, soc_fd);
		usleep(50*1000);
	    soc_fd = eng_client(ENG_SOCKET_PORT, SOCK_STREAM);
	}
	ENG_LOG("%s: soc_fd=%d",__func__, soc_fd);

	//confirm the connection
	while(eng_monitor_handshake(soc_fd)!=0) {
		counter++;
		if(counter>=3) {
			ENG_LOG("%s: handshake with server failed, retry %d times\n",__FUNCTION__, counter);
			err=-1;
			eng_close(soc_fd);
		}
	}

	return soc_fd;
}

void *eng_monitor_thread(void *x)
{
	int fd,n,readlen;
	fd_set readfds;
	unsigned char mbuf[ENG_BUF_LEN] = {0};
	fd = eng_monitor_open();
	//eng_monitor_handshake(fd);
	
	FD_ZERO(&readfds);
	FD_SET(fd, &readfds);

	for ( ;;){
		n = select(fd+1, &readfds, NULL, NULL, NULL);

		readlen=eng_read(fd, mbuf, ENG_BUF_LEN);

		if ( 0 == readlen ){
			ENG_LOG("eng_monitor_thread  break");
			eng_close(fd);
			break;
		}

		ENG_LOG("eng_monitor_thread  mbuf=%s,n=%d",mbuf,n);
		
		eng_write(fd,mbuf,ENG_BUF_LEN);
	}

	return NULL;
	
}

int main(void)
{
	pid_t pid;
	
	umask(0);

	/*
	* Become a session leader to lose controlling TTY.
	*/
	if ((pid = fork()) < 0)
		ENG_LOG("engservice can't fork");
	else if (pid != 0) /* parent */
		exit(0);
	setsid();

	if (chdir("/") < 0)
	ENG_LOG("can't change directory to /");
	
	eng_monitor_thread(NULL);

	return 0;
	
}
