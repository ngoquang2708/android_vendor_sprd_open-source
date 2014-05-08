#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <utils/Log.h>
#include <cutils/properties.h>

#ifdef  LOG_TAG
#undef  LOG_TAG
#define LOG_TAG "sprd_res_monitor"
#endif

#define PROP_CONFIG_FILE "/system/etc/sprd_monitor_property.conf"

extern void *start_monitor(void *arg);
extern void *oprofile_daemon(void* param);

int main(int argc, char *argv[])
{
	pthread_t tid_monitor;
	pthread_t tid_oprofile_monitor;
        FILE *fp;
	char name[PROPERTY_VALUE_MAX]; 
	char status[PROPERTY_KEY_MAX];
        fp = fopen(PROP_CONFIG_FILE, "rw");
        if(!fp) {
                ALOGE("Err open config file\n");
                exit(0);
        }

        while(fscanf(fp,"%s %s",name,status) != EOF) {

		property_set(name,status);		
        }

	if(!pthread_create(&tid_monitor,NULL,start_monitor,NULL))
		ALOGD("res_monitor thread created!\n");
	if(!pthread_create(&tid_oprofile_monitor , NULL , oprofile_daemon , NULL))
		ALOGD("oprofile daemon created!");
        else
		ALOGW("oprofile daemon create failed!");
        
        pthread_join(tid_monitor , NULL);
        pthread_join(tid_oprofile_monitor , NULL);
	return 0;
}
