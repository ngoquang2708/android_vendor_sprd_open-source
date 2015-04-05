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
#include <termios.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <signal.h>
#include <cutils/sockets.h>
#include <cutils/properties.h>
#include <gps.h>
#include <pthread.h>
#include <stdio.h>
#include <cutils/log.h>
#include <stdlib.h>
#include <unistd.h>    
#include <sys/stat.h>   
#include <string.h>  
#include <semaphore.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <dlfcn.h>

#include "gps_pc_mode.h"
//==================gps pc mode set begin==============
#define INIT_MODE  0x07
#define STOP_MODE  0x03

//==================gps pc mode set end================

#define  GPS_DEBUG   1    

#ifdef LOG_TAG
#undef LOG_TAG
#endif

#define  LOG_TAG  "PC_libgps"
#if GPS_DEBUG
#define  E(...)   ALOGE(__VA_ARGS__)
#define  D(...)   ((void)0)
#else
#define  E(...)   printf
#define  D(...)   ((void)0)
#endif

static int first_open = 0;
static int eut_gps_state;
static int gps_search_state;
static char pc_mode = 0;
static int set_mode = 0;
static char buf[512];
static sem_t sem_a;
static pthread_mutex_t mutex_a = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mutex_b = PTHREAD_MUTEX_INITIALIZER;
static GpsSvStatus SvState;
static GpsLocation GPSloc;
static int fix_status;
/*============================ begin============================*/

//pthread_t create_thread(const char* name, void (*start)(void *), void* arg);

static void location_callback(GpsLocation* location)
{
    fix_status = 1;
	memcpy(&GPSloc,location,sizeof(GpsLocation));
    E("%s called\n",__FUNCTION__);
}

static void status_callback(GpsStatus* status)
{

    E("%s called\n",__FUNCTION__);
}

static void sv_status_callback(GpsSvStatus* sv_status)
{
    E("%s called\n",__FUNCTION__);
	pthread_mutex_lock(&mutex_b);
	memcpy(&SvState,sv_status,sizeof(GpsSvStatus));
	pthread_mutex_unlock(&mutex_b);
}
static report_ptr g_func;

void set_report_ptr(report_ptr func)
{
	E("set callback by engpc");
	g_func = func;
}

static void nmea_callback(GpsUtcTime timestamp, const char* nmea, int length)
{
    int ret = 0;
    if(first_open == 0)
    {
		E("not init,callback fail");
		return;
	}
	if(g_func != NULL)
	{
		g_func(nmea,length);
	}
#if 0
	//E("%s",nmea);
	pthread_mutex_lock(&mutex_a);
	memset(buf,0,sizeof(buf));
	memcpy(buf,nmea,length);
	pthread_mutex_unlock(&mutex_a);
	E("trigger sem post");
	sem_getvalue(&sem_a,&ret);
	if(ret < 5)
    	sem_post(&sem_a);   //this should protect
#endif
	//fwrite(nmea,length,1,fp);
	//fflush(fp);  
}

static void set_capabilities_callback(uint32_t capabilities)
{
    E("%s called\n",__FUNCTION__);
}

static void acquire_wakelock_callback()
{
    E("%s called\n",__FUNCTION__);
}

static void release_wakelock_callback()
{
    E("%s called\n",__FUNCTION__);
}

static void request_utc_time_callback()
{
    E("%s called\n",__FUNCTION__);
}

static pthread_t create_thread_callback(const char* name, void (*start)(void *), void* arg)
{
	pthread_t pid;
	E("%s called\n",__FUNCTION__); 
	pthread_create(&pid, NULL,(void *)start, arg);
	return pid;
}

GpsCallbacks sGpsCallbacks = {
	sizeof(GpsCallbacks),
	location_callback,
	status_callback,
	sv_status_callback,
	nmea_callback,
	set_capabilities_callback,
	acquire_wakelock_callback,
	release_wakelock_callback,
	create_thread_callback,
	request_utc_time_callback,
};

GpsInterface *pGpsface;
/*============================ end ============================*/



void set_pc_mode(char input_pc_mode)
{
	pc_mode = input_pc_mode;
	E("set_pc_mode is enter\n");
}

int start_engine(void)
{
    pid_t pid; 
    pid=fork();
    if(pid==0)
	{
	    E("start engine");
		system("GPSenseEngine");
    }
	return 0;
}

int search_for_engine(void)
{
	char rsp[256];
    FILE *stream;
	int ret = 0;

	memset(rsp,0,sizeof(rsp));

	stream = popen("ps | grep GPSenseEngine","r");

	fgets(rsp,sizeof(rsp),stream);
	if(strstr(rsp,"GPSenseEngine") != NULL)
	{
		E("GPSenseEngine is start success\n");
		ret = 1;
	}
	else{
		E("GPSenseEngine is start fail\n");
		ret = 0;
	}
	pclose(stream);
	return ret;
}

int gps_export_start(void)
{
	int ret = 0;
	E("gps_export_start is enter\n");
    if(pGpsface == NULL)
	{
		E("start fail,for not init");
		return ret;
	}
   	//pGpsface->start(); 
	//in future,should add ge2 option,such as if((eut_gps_state == 1) && (hardware = CELLGUIDE))
	if(eut_gps_state == 1)
	{
		//ret = search_for_engine();
		//if(ret == 0)
		E("begin start engine");
		ret = start_engine();
		sleep(1);
		pGpsface->start(); 
	}
	else
	{
		pGpsface->start(); 
	}
	sleep(1);
	return ret;
}

int gps_export_stop(void)
{
	int ret =0;
	E("gps_export_stop ret is %d\n",ret);

    if(pGpsface == NULL)
	{
		E("stop fail,for not init");
		return ret;
	}
   	pGpsface->stop(); 
	sleep(4);
	return ret;
}
 int set_gps_mode(unsigned int mode)
 {
	int ret = 0;

	if(pGpsface == NULL)
	{
		E("set mode fail,for not init");
		return 0;
	}
	switch(mode)
	{
	case 0: // Hot start
		set_mode = HOT_START;
		pGpsface->delete_aiding_data(set_mode);   //trigger cs/hs/ws
		ret = 1;
		break;
	case 1: // Warm start
		set_mode = WARM_START;
		pGpsface->delete_aiding_data(set_mode);   //trigger cs/hs/ws
		ret = 1;
		break;
	case 2: // Cold start
		set_mode = COLD_START;
		pGpsface->delete_aiding_data(set_mode);   //trigger cs/hs/ws
		ret = 1;
		break;
	case 20: // Fac start
		set_mode = FAC_START;
		pGpsface->delete_aiding_data(set_mode);   //trigger cs/hs/ws
		ret = 1;
		break;
	default:
		break;
	}

	return ret;
 }

int get_nmea_data(char *nbuff)
{
	int len = 0;
    E("get nmea data enter");
#if 0
	sem_wait(&sem_a);
	pthread_mutex_lock(&mutex_a);
	len = strlen(buf);  //don't add 1 for '\0'
	if(len > 9)
	{
		memcpy(nbuff,buf,len);
	}
	pthread_mutex_unlock(&mutex_a);
#endif
	return len;
}

int get_init_mode(void)
{
	if(first_open == 0)
	{
		void *handle;
		GpsInterface* (*get_interface)(struct gps_device_t* dev);
		char *error;
		int i = 0;

		E("begin gps init\n");
		if(access("/data/cg",0) == -1)
		{
			E("===========>>>>>>>>>>cg file is not exit");
			system("mkdir /data/cg");
			system("mkdir /data/cg/supl");
			system("mkdir /data/cg/online");
			chmod("/data/cg",0777);
			chmod("/data/cg/supl",0777);
			chmod("/data/cg/online",0777);
		}
		E("before dlopen");
		handle = dlopen("/system/lib/hw/gps.default.so", RTLD_LAZY);
		if (!handle) {
		   E("%s\n", dlerror());
		   return INIT_MODE;
		}
		E("after dlopen\n");
		dlerror();    /* Clear any existing error */

		get_interface = dlsym(handle, "gps_get_hardware_interface");
		E("get gps_get_hardware_interface\n");
		pGpsface = get_interface(NULL);
		pGpsface->init(&sGpsCallbacks);
		first_open = 1;
		sem_init(&sem_a,0,0);
	}
	return INIT_MODE;
}
int get_stop_mode(void)
{
    return STOP_MODE;
}
/*====================== add for pc at interface end======================*/

/*****************************************************************************
=========================== func eut begin====================================
*****************************************************************************/

#define ENG_GPS_NUM     8

int eut_parse(int data,char *rsp)
{
	if(data == 1)
	{
		eut_gps_state = 1;
		if(first_open == 0)
		{
			get_init_mode();
		}
		gps_export_start();
		strcpy(rsp,EUT_GPS_OK);
	}
	else if(data == 0)
	{
		eut_gps_state = 0;
		gps_export_stop();   //block,for test
		strcpy(rsp,EUT_GPS_OK);
	}

	return 0;
}
int eut_eq_parse(int data,char *rsp)
{
	sprintf(rsp,"%s%d",EUT_GPS_REQ,eut_gps_state);
	return 0;
}
int search_eq_parse(int data,char *rsp)
{
	sprintf(rsp,"%s%d",EUT_GPS_SEARCH_REQ,gps_search_state);
	return 0;
}
int search_parse(int data,char *rsp)
{	
	if(data != eut_gps_state)
	{
		eut_parse(data,rsp);
	}
	gps_search_state = eut_gps_state;
	strcpy(rsp,EUT_GPS_OK);
	return 0;
}

int get_prn_list(char *rsp)
{
	int i = 0,lenth = 0;
	memcpy(rsp,EUT_GPS_PRN_REQ,strlen(EUT_GPS_PRN_REQ));
	lenth = strlen(EUT_GPS_PRN_REQ);
	pthread_mutex_lock(&mutex_b);
	for(i = 0; i < SvState.num_svs; i++)
	{
		lenth = lenth + sprintf(rsp + lenth,"%d,",SvState.sv_list[i].prn);
	}
	pthread_mutex_unlock(&mutex_b);
	return SvState.num_svs;
}
int prnstate_parse(int data,char *rsp)
{
	if((gps_search_state == 0) && (eut_gps_state == 0))
	{
		sprintf(rsp,"%s%d",EUT_GPS_ERROR,EUT_GPSERR_PRNSEARCH);
		return 0;
	}

	if(get_prn_list(rsp) == 0)
	{
		sprintf(rsp,"%s%s",EUT_GPS_PRN_REQ,EUT_GPS_NO_FOUND_STAELITE);
	}
	return 0;
}
int snr_parse(int data,char *rsp)
{
	int max_id = 0,i = 0;
	E("snr parse is enter");
	if((gps_search_state == 0) && (eut_gps_state == 0)){
		E("gps has not search");
		sprintf(rsp,"%s%d",EUT_GPS_ERROR,EUT_GPSERR_PRNSEARCH);
		return 0;
	}
	if(SvState.num_svs == 0)
	{
		E("SvState is NULL,return");
		sprintf(rsp,"%s no sv_num is found",EUT_GPS_SNR_REQ);
		return 0;
	}
	pthread_mutex_lock(&mutex_b);
	for(i = 0; i < SvState.num_svs; i++)
	{
		if(SvState.sv_list[i].snr > SvState.sv_list[max_id].snr)
		{
			max_id = i;
		}
	}
	sprintf(rsp,"%s%f %s%d %s%d",EUT_GPS_SNR_REQ,SvState.sv_list[max_id].snr,
								EUT_GPS_SV_ID,SvState.sv_list[max_id].prn,
								EUT_GPS_SV_NUMS,SvState.num_svs);
	pthread_mutex_unlock(&mutex_b);
	E("%s",rsp);
	return 0;
}
int prn_parse(int data,char *rsp)
{
	int i = 0,found = 0;
	if((gps_search_state == 0) && (eut_gps_state == 0))
	{
		sprintf(rsp,"%s%d",EUT_GPS_ERROR,EUT_GPSERR_PRNSEARCH);
		return 0;
	}

	pthread_mutex_lock(&mutex_b);
	for(i = 0; i < SvState.num_svs; i++)
	{
		if(SvState.sv_list[i].prn == data)
		{
			sprintf(rsp,"%s%d,%f",EUT_GPS_SNR_REQ,data,SvState.sv_list[i].snr);
			found = 1;
			break;
		}
	}
	pthread_mutex_unlock(&mutex_b);

	if(found == 0)
	{
		E("cannot find prn and it's snr");
		sprintf(rsp,"%s%s",EUT_GPS_SNR_REQ,EUT_GPS_NO_FOUND_STAELITE);
	}
	return 0;
}
int fix_parse(int data,char *rsp)
{
	int i = 0,found = 0;
	if((gps_search_state == 0) && (eut_gps_state == 0))
	{
		sprintf(rsp,"%s%d",EUT_GPS_ERROR,EUT_GPSERR_PRNSEARCH);
		return 0;
	}

 	if(fix_status == 0)
	{
		E("cannot fix location");
		sprintf(rsp,"fix result:false");
	}	
	else
	{
		E("fix over");
		sprintf(rsp,"fix result:success,%f,%f",GPSloc.latitude,GPSloc.longitude);
	}
	return 0;
}
typedef int (*eut_function)(int data,char *rsp);
typedef struct
{
	char *name;
	eut_function func;
}eut_data; 

eut_data eut_gps_at_table[ENG_GPS_NUM] = {
	{"EUT?",eut_eq_parse},
	{"EUT",eut_parse},
	{"SEARCH?",search_eq_parse},
	{"SEARCH",search_parse},
	{"PRNSTATE?",prnstate_parse},
	{"SNR?",snr_parse},
	{"PRN",prn_parse},	
	{"FIX?",fix_parse},	
};
#if 0
int (*state[ENG_GPS_NUM])(int data,char *rsp) = {
	eut_parse,
	eut_eq_parse,
	search_eq_parse,
	search_parse,
	prnstate_parse,
	snr_parse,
	prn_parse,
};
static char *eut_gps_name[ENG_GPS_NUM] = {
	"EUT",
	"EUT?",
	"SEARCH?",
	"SEARCH",
	"PRNSTATE?",
	"SNR?",
	"PRN",	
};

//EUT
int get_cmd_index(char *buf)
{
    int i;
    for(i = 0;i < ENG_GPS_NUM;i++)
	{
        if(strstr(buf,eut_gps_name[i]) != NULL)
        {
            break;
        }
    }
    return i;
}
#endif

int get_sub_str(char *buf, char **revdata, char a, char *delim, unsigned char count, unsigned char substr_max_len)
{
    int len, len1, len2;
    char *start = NULL;
    char *substr = NULL;
    char *end = buf;
    int str_len = strlen(buf);

    start = strchr(buf, a);
    substr = strstr(buf, delim);
    
    if(!substr)
    {
        /* if current1 not exist, return this function.*/
        return 0;
    }

    while (end && *end != '\0')
    {
        end++;
    }

    if((NULL != start) && (NULL != end))
    {
        char *tokenPtr = NULL;
        unsigned int index = 1; /*must be inited by 1, because data[0] is command name */

        start++;
        substr++;
        len = substr - start - 1;

        /* get cmd name */
        memcpy(revdata[0], start, len);

        /* get sub str by delimeter */
        tokenPtr = strtok(substr, delim);
        while(NULL != tokenPtr && index < count) 
        {
            strncpy(revdata[index++], tokenPtr, substr_max_len);

            /* next */
            tokenPtr = strtok(NULL, delim);
        }
    }

    return 0;
}

void gps_eut_parse(char *buf,char *rsp)
{
	int i = 0;   //data is get from buf,used arg1,arg2.
    static char args0[32+1];
    static char args1[32+1];
    static char args2[32+1];
    static char args3[32+1];
//should init to 0
	memset(args0,0,sizeof(args0));
	memset(args1,0,sizeof(args1));
	memset(args2,0,sizeof(args2));
	memset(args3,0,sizeof(args3));

    char *data[4] = {args0, args1, args2, args3};
    get_sub_str(buf, data, '=', ",", 4, 32);
	E("gps_eut_parse enter");
#if 0
	index = get_cmd_index(buf);
	if((index > ENG_GPS_NUM - 1) || (index < 0))
	{
		E("get index error!!\n");
		return;
	}
	state[index](data,rsp);
#endif
    for(i = 0;i < ENG_GPS_NUM;i++)
	{
        if(strstr(buf,eut_gps_at_table[i].name) != NULL)
        {
			E("arg1 is %d",atoi(data[1]));
			eut_gps_at_table[i].func(atoi(data[1]),rsp);
            break;
        }
    }
	return;
}

/*****************************************************************************
=========================== func eut end======================================
*****************************************************************************/

