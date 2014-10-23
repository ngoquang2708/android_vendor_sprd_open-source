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
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/uio.h>
#include <dirent.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <cutils/properties.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <cutils/log.h>
#include "profiledaemon.h"

static void do_ftrace(unsigned long tracetime)
{
   char ftrace[256];
   sprintf(ftrace , "/system/bin/capture_ftrace.sh %lu" , tracetime);
   system(ftrace);
}

int start_ftrace(unsigned long time)
{
    int client;
    profileinfo info;
    char property[PROPERTY_VALUE_MAX];
    struct sockaddr_un addr;
    int slen = sizeof(addr);
    property_get(FTRACE_DEBUG_SWITCHER , property , "0");
    if(atoi(property) == 1)
    {
        client = socket(AF_UNIX , SOCK_DGRAM , 0);
        if(client == -1)
        {
            ALOGD("start ftrace faild creat socket error");
            return -1;
        }
        memset(&addr , 0 , sizeof(addr));
        addr.sun_family = AF_UNIX;
        strcpy(addr.sun_path , PROFILE_SOCKET_NAME);
        info.cmd = FTRACE_START;
        info.profiletime = time;
        int ret = sendto(client , &info , sizeof(info) , 0 , (struct sockaddr*)&addr , slen);
        close(client);
        if(ret < 0)
        {
            ALOGD("start ftrace failed");
            return -1;
        }
        else
        {
            ALOGD("start ftrace");
            return 0;
        }
    }
    else
    {
        return -2;
    }
}

static void do_oprofile(unsigned long profiletime)
{
   char profile[256];
   sprintf(profile , "/system/bin/capture_oprofile.sh %lu" , profiletime);
   system(profile);
}

int start_oprofile(unsigned long time)
{
    int client;
    profileinfo info;
    char property[PROPERTY_VALUE_MAX];
    struct sockaddr_un addr;
    int slen = sizeof(addr);
    property_get(OPROFILE_DEBUG_SWITCHER , property , "0");
    if(atoi(property) == 1)
    {
        client = socket(AF_UNIX , SOCK_DGRAM , 0);
        if(client == -1)
        {
            ALOGD("start oprofile faild creat socket error");
            return -1;
        }
        memset(&addr , 0 , sizeof(addr));
        addr.sun_family = AF_UNIX;
        strcpy(addr.sun_path , PROFILE_SOCKET_NAME);
        info.cmd = OPROFILE_START;
        info.profiletime = time;
        int ret = sendto(client , &info , sizeof(info) , 0 , (struct sockaddr*)&addr , slen);
        close(client);
        if(ret < 0)
        {
            ALOGD("start oprofile failed");
            return -1;
        }
        else
        {
            ALOGD("start oprofile");
            return 0;
        }
    }
    else
    {
        return -2;
    }
}

static int parse_config()
{

	FILE *fp;

	fp = fopen(BLKTRACE_CONF_FILE, "r");
	if(!fp) {
                ALOGE("Err open config file %s\n",BLKTRACE_CONF_FILE);
		return 0;
	}
	fgets(blk_opt,sizeof(blk_opt),fp);
	if(blk_opt[strlen(blk_opt)-1] == '\n')
		blk_opt[strlen(blk_opt)-1] = ' ';
	fclose(fp);
	return 1;
}
static void do_blktrace()
{
    time_t now = time(NULL);
    struct tm *tm;
    int ret;

    ret = mkdir(BLKTRACE_LOG_PATH, S_IRWXU | S_IRWXG | S_IRWXO);
    if (-1 == ret && (errno != EEXIST)){
		ALOGE("mkdir %s failed\n",BLKTRACE_LOG_PATH);
		return;
    }

    tm = localtime(&now);
    sprintf(blkcapture, "btcapture %s --output-dir %s/blk-%04d-%02d-%02d-%02d-%02d-%02d -c" ,
			blk_opt, BLKTRACE_LOG_PATH,
			tm->tm_year + 1900,
			tm->tm_mon + 1,
			tm->tm_mday,
			tm->tm_hour,
			tm->tm_min,
			tm->tm_sec);
    system(blkcapture);
    system("/system/bin/capture_blktrace.sh");
}

int start_blktrace()
{
    int client;
    profileinfo info;
    char property[PROPERTY_VALUE_MAX];
    struct sockaddr_un addr;
    int slen = sizeof(addr);
    property_get(BLKTRACE_DEBUG_SWITCHER , property , "0");
    if(atoi(property) == 1)
    {
        client = socket(AF_UNIX , SOCK_DGRAM , 0);
        if(client == -1)
        {
            ALOGD("start blktrace faild creat socket error");
            return -1;
        }
        memset(&addr , 0 , sizeof(addr));
        addr.sun_family = AF_UNIX;
        strcpy(addr.sun_path,PROFILE_SOCKET_NAME);
        info.cmd = BLKTRACE_START;
        int ret = sendto(client , &info , sizeof(info) , 0 , (struct sockaddr*)&addr , slen);
        close(client);
        if(ret < 0)
        {
            ALOGD("start blktrace failed");
            return -1;
        }
        else
        {
            ALOGD("start blktrace");
            return 0;
        }
    }
    else
    {
        return -2;
    }
}
void* profile_daemon(void* param)
{
    int serv;
    profileinfo pinfo;
    int len;
    char prop_oprofile[PROPERTY_VALUE_MAX];
    char prop_ftrace[PROPERTY_VALUE_MAX];
    char prop_blktrace[PROPERTY_VALUE_MAX];
    struct sockaddr_un addr;
    memset(&addr , 0 , sizeof(addr));
    socklen_t addr_len = sizeof(addr);
    umask(0);
    int ret = mkdir(PROFILE_SOCKET_PATH , S_IRWXU | S_IRWXG | S_IRWXO);
    if (-1 == ret && (errno != EEXIST)) {
        ALOGE("oprofile daemon create socket path failed");
        return NULL;
    }

    serv = socket(AF_UNIX , SOCK_DGRAM , 0);
    if(serv == -1)
    {
        ALOGE("socket create fail in oprofile daemon");
        return NULL;
    }
    //setsockopt();
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path , PROFILE_SOCKET_NAME);
    unlink(addr.sun_path);
    if(bind(serv , (struct sockaddr*) &addr , sizeof(addr)) < 0)
    {
        close(serv);
        ALOGE("start oprofile daemon failed");
        return NULL;
    }

    //if blktrace is enabled,start blktrace
    property_get(BLKTRACE_DEBUG_SWITCHER , prop_blktrace , "0");
    if(1 == atoi(prop_blktrace)) {
	if(parse_config()) {
		sprintf(blkcapture,"btcapture %s",blk_opt);
		system(blkcapture);
	}
    }
    for(;;) {
        len = recvfrom(serv , &pinfo , sizeof(pinfo) , 0 , (struct sockaddr*)&addr , &addr_len);
        ALOGD("----------------------receive from client---------------------");
        property_get(OPROFILE_DEBUG_SWITCHER , prop_oprofile , "0");
	property_get(FTRACE_DEBUG_SWITCHER , prop_ftrace , "0");
        // if property is not set continue
        if(atoi(prop_oprofile) != 1 && atoi(prop_ftrace) != 1 && atoi(prop_blktrace) != 1)
            continue;
        if(len < 0)
            continue;
        ALOGD("----------------------receive from client--------------------- cmd is:%d" , pinfo.cmd);
        switch(pinfo.cmd)
        {
        case OPROFILE_START:
             ALOGD("do_oprofile time is:%lu" , pinfo.profiletime);
             do_oprofile(pinfo.profiletime);
             break;
	case FTRACE_START:
             ALOGD("do_ftrace time is:%lu", pinfo.profiletime);
             do_ftrace(pinfo.profiletime);
             break;
	case BLKTRACE_START:
             do_blktrace();
             break;
	default:
             ALOGW("oprofiledaemon cmd invalid");
             break; 
        }
    }
    if(1 == atoi(prop_blktrace)) {
	sprintf(blkcapture,"btcapture %s -k",blk_opt);
    }
    return NULL;
}
