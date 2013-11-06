/*
 * =====================================================================================
 *
 *       Filename:  eng_sd_log.c
 *
 *    Description:  capture arm log to sd card
 *
 *        Version:  1.0
 *        Created:  11/21/2011 02:19:45 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Binary Yang <Binary.Yang@spreadtrum.com.cn>
 *        Company:  © Copyright 2010 Spreadtrum Communications Inc.
 *
 * =====================================================================================
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/time.h>
#include "engopt.h"
#include "cutils/properties.h"


#define ENG_CARDLOG_PROPERTY	"persist.sys.cardlog"

#define DATA_BUF_SIZE (4096*64)
#define MAX_COUNT 3
static char log_data[DATA_BUF_SIZE];
extern int is_sdcard_exist;
extern int pipe_fd;

void * eng_sd_log(void * args){
	int card_log_fd = -1;
	ssize_t r_cnt, w_cnt;
	int res,n,count;
	char cardlog_property[PROP_VALUE_MAX];

	ENG_LOG("ENG SD LOG");
	while ( 1 ) {
		if ( !is_sdcard_exist ) {
			sleep(500);
			ENG_LOG("%s:no sd card",__FUNCTION__);
			if (card_log_fd >= 0) {
				close(card_log_fd);
				card_log_fd = -1;
			}
			continue;
		}

		memset(cardlog_property, 0, sizeof(cardlog_property));
		property_get(ENG_CARDLOG_PROPERTY, cardlog_property, "");
		n = atoi(cardlog_property);

		if (card_log_fd < 0) {
			for (count=0; count<MAX_COUNT; count++) {
				card_log_fd = open("/dev/vhub.modem",O_WRONLY|O_APPEND);
				if (card_log_fd < 0) {
					usleep(50*1000);
					continue;
				}
				break; 
			}
		}

		if ( 1==n && card_log_fd >= 0) {
			if ( pipe_fd<0 ) {
				ENG_LOG("%s:pipe_fd<0",__FUNCTION__);
				sleep(500);
				continue;
				
			}
			r_cnt = read(pipe_fd, log_data, DATA_BUF_SIZE);
			if (r_cnt == 0) {
				close(pipe_fd);
				pipe_fd = open("/dev/vbpipe0",O_RDONLY);
				continue;
			} else if (r_cnt < 0) {
				ENG_LOG("no log data :%d\n", r_cnt);
				continue;
			}

			w_cnt = write(card_log_fd,log_data,r_cnt);
			ENG_LOG("write %d bytes",w_cnt);
		}else{
			sleep(1);
		}
	}

	if (card_log_fd >= 0) {
		close(card_log_fd);
		card_log_fd = -1;
	}
}
