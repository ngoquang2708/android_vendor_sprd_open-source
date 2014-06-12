///============================================================================
/// Copyright 2012-2014  spreadtrum  --
/// This program be used, duplicated, modified or distributed
/// pursuant to the terms and conditions of the Apache 2 License.
/// ---------------------------------------------------------------------------
/// file gps_lib.c
/// for converting NMEA to Android like APIs
/// ---------------------------------------------------------------------------
/// zhouxw mod 20130920,version 1.00,include test,need add supl so on...
///============================================================================

#include <errno.h>
#include <pthread.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <signal.h>
#include <math.h>
#include <time.h>
#include <cutils/sockets.h>
#include <cutils/properties.h>
//#include <gps.h>
#include <pthread.h>

#include <stdio.h>
#include <cutils/log.h>
#include <stdlib.h>
#include <unistd.h>    
#include <sys/stat.h>   
#include <string.h>  

#include <semaphore.h>
#include "gps_pc_mode.h"


#define open_flag  1
#define close_flag 0
#define true 1
#define false 0   
#define IDLE_ON_SUCCESS    1001
#define IDLE_ON_FAIL      1002
#define IDLE_OFF_SUCCESS  1003
#define IDLE_OFF_FAIL     1004
#define POWEROFF          1005
#define GET_ENGINE_STATUS 5000 
#define  GPS_DEBUG   0    

#ifdef LOG_TAG
#undef LOG_TAG
#endif

#define  LOG_TAG  "CG_libgps"
#if GPS_DEBUG
#define  E(...)   LOGE(__VA_ARGS__)
#define  D(...)   ((void)0)
#else
#define  E(...)   ((void)0)
#define  D(...)   ((void)0)
#endif

static char *libgps_version = "libgps for pc test,v1.0.0\r\n";

FILE *tfp = NULL;
static int gps_open = false;
static char gps_log_enable = 1;   //log print enable
static char pc_mode = 0;
static char set_mode = 0;

char *gps_idle_on = "$PCGDC,IDLEON,1,*1\r\n";
char *gps_idle_off="$PCGDC,IDLEOFF,1,*1\r\n"; 
static char *cold_cmd = "$PCGDC,CS*55\r\n";
static char *warm_cmd = "$PCGDC,WS*55\r\n";
static char *hot_cmd = "$PCGDC,HS*55\r\n";
static char *fac_cmd = "$PCGDC,FAC*55\r\n";


static int nmea_fd = -1;

static int gps_stop_complete(int value)
{
	int eval = 0;
	char time = 0;
	int ReadElements = 0;
	E("gps_stop complete enter");
	
	while((time < 10) && (eval != value))
	{
		if(access("/data/cg/online/stasync",0) == -1)
		return eval;
		if(tfp == NULL)
		tfp = fopen("/data/cg/online/stasync","r");
		if(tfp != NULL){
			fseek(tfp,0L,SEEK_SET);
	        ReadElements = fread(&eval,4,1,tfp);
			if(ReadElements <= 0)
			  E("fread fail!");
			fclose(tfp);
			tfp = NULL;
		}
		if(value == GET_ENGINE_STATUS)
		return eval;

		if(eval != value){
			time++;
			if(value == IDLE_ON_SUCCESS)
			usleep(500000);
			if(value == IDLE_OFF_SUCCESS)
			usleep(100000);
			else
			usleep(250000);
		}
	}
	E("stop_cmplete,time=%d,value=%d,eval=%d",time,value,eval);

	return eval;
}

/*====================== add for pc at interface begin ====================*/
// pc_mode : 0 ----disable, 1--------enable
void set_gps_flag(int flag)
{
 
   if(flag == open_flag)
   {
	if(property_set("ctl.start", "GPSenseEngine") < 0) 
	E("Failed to start GPSenseEngine");       
   }
   
   if(flag == close_flag)
   {
	if (property_set("ctl.stop", "GPSenseEngine") < 0) 
	E("Failed to stop GPSenseEngine"); 
	gps_open = false;
   }
   return;
}
static void  set_fdattr(int fd )
{
        struct termios termios;

   	tcflush(fd, TCIOFLUSH);
	tcgetattr(fd, &termios);

	termios.c_iflag = (IGNBRK | IGNPAR);
	termios.c_oflag = 0;                        /* Raw output (no postproc), no delays */
	termios.c_cflag = B115200 | CS8 | CREAD | HUPCL | CLOCAL;
	termios.c_lflag = 0;                        /* Raw, no echo,this is important */
	
	tcsetattr(fd, TCSANOW, &termios);
	tcflush(fd, TCIOFLUSH);
}
static void open_engine(void)
{
   int retries = 2;
   int eval = 0;
   char time = 0;
   int ReadElements = 0;

   
   if(gps_open == false)
   {
	set_gps_flag(open_flag);  //zhouxw add 
	while(time < 20)
	{
		usleep(100*1000);     //100ms wait
		time++;
		if(tfp == NULL)
		tfp = fopen("/data/cg/online/stasync","r");
		if(tfp != NULL){
			fseek(tfp,0L,SEEK_SET);
			ReadElements = fread(&eval,4,1,tfp);
			if(ReadElements <= 0)
			  E("fread fail!\n");
			fclose(tfp);
			tfp = NULL;
		}
		if(eval == IDLE_ON_SUCCESS)
		break;
	}
	E("time used open engine is %d,value is %d\n",time,eval);
	gps_open = true;
	E("The libgps version is %s\n",libgps_version); 
   }
   else
   		E("already start engine\n");
 
	if(nmea_fd < 0)
	{
		do {           
		if ((nmea_fd = open("/dev/ttyV1", O_RDWR | O_NOCTTY)) >= 0)            
		break;
		sleep(1);
		} while (retries--);
		E("now open ttyv is over\n");
		if (nmea_fd < 0)
		E("%s: no gps Hardware detected, errno = %d, %s", __FUNCTION__, errno, strerror(errno));
		else
		set_fdattr(nmea_fd);
    	} 
	else
		tcflush(nmea_fd, TCIOFLUSH);    //clear buffer data
	E("open engine  is ok\n");
    return;
}

void do_gps_mode(unsigned int mode)
{
	E("set_gps_mode is enter\n");
	if((mode > 0) && (nmea_fd >= 0)){	
	//usleep(100000);						  
		switch(mode)
		{
		case 1:
			E("zhouxw:set gps_mode=3,warm start now");
			write(nmea_fd,warm_cmd,strlen(warm_cmd));  
			break; 
		case 125:
			E("zhouxw:set gps_mode=1,cold start now");
			write(nmea_fd,cold_cmd,strlen(cold_cmd)); 
			break;
		case 136:
			E("zhouxw:enable log save");
			gps_log_enable = 1; 
			break;
		case 520:
			E("zhouxw:disable log save");
			gps_log_enable = 0; 
			break;
		case 1024: 
			E("zhouxw:set gps_mode=2,hot start now");
			write(nmea_fd,hot_cmd,strlen(hot_cmd));
			break;
		case 65535:
			E("zhouxw:set gps_mode=65535");
			write(nmea_fd,fac_cmd,strlen(fac_cmd)); 
			break; 
		default:
			break;
		}
	}

}

void set_pc_mode(char input_pc_mode)
{
	nmea_fd = -1;
	pc_mode = input_pc_mode;
	E("set_pc_mode is enter\n");
}
int gps_export_start(void)
{
	int ret =0;
	
	E("gps_export_start is enter\n");
	open_engine();
	//usleep(20000);

	if(nmea_fd >= 0){
		do_gps_mode(set_mode);
		write(nmea_fd,gps_idle_off,strlen(gps_idle_off));
	}

	return ret;
}
int pc_gps_stop(void)
{
	int ret = 0;

	if(nmea_fd >= 0)
	write(nmea_fd,gps_idle_on,strlen(gps_idle_on)); 
	E("====zhouxw,will enter gps stop complete\n");
	ret = gps_stop_complete(IDLE_ON_SUCCESS);
	return ret;
}
int gps_export_stop(void)
{
	int ret =0;
	ret = pc_gps_stop();
	E("gps_export_stop ret is %d\n",ret);
	return ret;
}
 void set_gps_mode(unsigned int mode)
 {
	set_mode = mode;
 }

int get_nmea_data(char *nbuff)
{
	int ret = 0;

	if((nbuff != NULL) && (nmea_fd >= 0))
	{
		ret = read(nmea_fd, nbuff, 32 );

		//E("%s",nbuff);
	}
	else{
		E("open ttyv fail\n");
		return -1;
	}
	return ret;
}

/*====================== add for pc at interface end= ====================*/

/*========================== this is for test =======================*/
#if 0
char ndata[32] = {0};
pthread_t  thread;
sem_t nmea_sem;
FILE *nmea_fp = NULL;
char run_status = 0;
void save_nmea_file(void)
{
	struct stat temp;
	if(access("/data/cg/online/nmea.log",0) == -1)
	temp.st_size = 0;
	else
	stat("/data/cg/online/nmea.log", &temp);

	if(temp.st_size >= 2000000)
	{
		if(nmea_fp != NULL){ 
			fclose(nmea_fp);
			nmea_fp = NULL;
		}
	}
	else if(nmea_fp == NULL){
		//E("create nmea log file now\n");
		nmea_fp = fopen("/data/cg/online/nmea.log","a+");   //apend open
	}
	if(nmea_fp != NULL)
	{
		fwrite(ndata,1,strlen(ndata),nmea_fp);
		fclose(nmea_fp);
		nmea_fp = NULL;
	}
}
void *nmea_thread(void *arg)
{
	E("this thread is for read and save nmea log\n");
	sem_wait(&nmea_sem);
	E("=============>>>>>>>>>now we will enter while\n");
	while(run_status)
	{
	memset(ndata,0,32);
	get_nmea_data(ndata);
	save_nmea_file();
	}
	E("this thread is over\n");
	return ((void*)2);
}
int main(void)
{
	int cmd = 0;
	unsigned int mode = 0;
	char time = 0;
	void *dummy;

	sem_init(&nmea_sem, 0, 0);
	if ( pthread_create( &thread, NULL, nmea_thread, NULL) != 0 )
	E("create gps thread is fail\n");
	while(cmd != 5)
	{
		E("\n");
		E("now enter gps commad:\n");
		E("1:gps set start mode\n");
		E("2:gps start\n");
		E("3:gps stop\n");
		E("4:get nmea log,10 logs\n");
		E("5:exit this function\n");
		scanf("%d",&cmd);
		if(cmd == 1){
			E("enter start mode:1-warm start,125-cold start,1024-hot start\n");
			scanf("%d",&mode);
			set_gps_mode(mode);
			mode = 0;
			//now begin send mode to engine
			gps_export_stop();
			gps_export_start();
			continue;
		}
		if(cmd == 2){
			gps_export_start();
			continue;
		}
		if(cmd == 3){
			gps_export_stop();
			continue;
		}
		if(cmd == 4){
			run_status = 1;
			sem_post(&nmea_sem);
			continue;
		}
		
	}
	run_status = 0;
	pthread_join(thread, &dummy);
	return 0;

}
#endif




