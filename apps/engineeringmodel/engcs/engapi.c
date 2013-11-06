#include <stdlib.h>
#include "engopt.h"
#include "engapi.h"
#include "eng_modemclient.h"
#include "eng_appclient.h"
#include "engat.h"
#include <pthread.h>
#include "engphasecheck.h"
#include <cutils/properties.h>

int engapi_open(int type)
{
	int fd;
	char modemtype[PROP_VALUE_MAX];
	int modemw;

	memset(modemtype, 0, sizeof(modemtype));
	property_get("ro.modem.w.enable", modemtype, "0");
	modemw = atoi(modemtype);

	if (modemw){
		fd = eng_at_open("engw",type);
	}else{
		fd = eng_at_open("engtd",type);
	}

	return fd;
}

void engapi_close(int fd)
{

	eng_close(fd);
}

int  engapi_read(int  fd, void*  buf, size_t  len)
{
	int ret = eng_at_read(fd, (char*)buf, len);
	return ret;
}

int  engapi_write(int  fd, const void*  buf, size_t  len)
{
	return eng_at_write(fd,(char*)buf,len);
}

int engapi_getphasecheck(void* buf, int size)
{
    int readsize = 0;
    int ret = 0;
    char * str = buf;
    SP09_PHASE_CHECK_T phasecheck ;
    memset(&phasecheck,0,sizeof(phasecheck));

    ENG_LOG("engapi_getphasecheck");    
    ret = eng_getphasecheck(&phasecheck);

    if(ret == 0)
    {
        const unsigned short stationFlag = 0x0001; 
        int i = 0;
        char name[SP09_MAX_STATION_NAME_LEN + 1] = {0};
        char lastFail[SP09_MAX_LAST_DESCRIPTION_LEN + 1] = {0};
	    char sn[SP09_MAX_SN_LEN + 1] = {0};
        
        strcat(str, "sn1:");
        strncpy(sn, phasecheck.SN1, SP09_MAX_SN_LEN);
        strcat(str, sn);
        strcat(str, "\r\n");

        memset((void*)sn, 0, SP09_MAX_SN_LEN + 1);
        strcat(str, "sn2:");
        strncpy(sn, phasecheck.SN2, SP09_MAX_SN_LEN);
        strcat(str, sn);
        strcat(str, "\r\n");
        ENG_LOG("engapi_getphasecheck:%s", (char*)buf);

        for(i = 0; i < phasecheck.StationNum; i++)
        {
            memset((void*)name, 0, SP09_MAX_STATION_NAME_LEN + 1);
            strncpy(name, phasecheck.StationName[i], SP09_MAX_STATION_NAME_LEN);
            strcat(str, name);
            strcat(str, ":");
            if((phasecheck.iTestSign & (stationFlag << i)) == 0)
            {
                if((phasecheck.iItem & (stationFlag << i)) == 0)
                {
                    strcat(str, "Pass \r\n");
                } 
                else
                {
                    strcat(str, "Fail \r\n");
                }
            }
            else
            {
                strcat(str, "No Tested \r\n");
            }
        }
        ENG_LOG("engapi_getphasecheck:%s", (char*)buf);
        strncpy(lastFail, phasecheck.szLastFailDescription, SP09_MAX_LAST_DESCRIPTION_LEN);
        strcat(str, "\r\nLast:");
        strcat(str, lastFail);

        ENG_LOG("engapi_getphasecheck:%s iTestSign:%x iItem:%x", (char*)buf, phasecheck.iTestSign, phasecheck.iItem);
        readsize = strlen(str);
        return readsize;
    }
    else
    {
        strncpy(str, "read phase check error", 22);
        return 22;
    }
}


