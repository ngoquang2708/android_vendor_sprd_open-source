#define LOG_TAG "IAtChannel"

#include <android/log.h>
#include <binder/IServiceManager.h>
#include <utils/String8.h>
#include <utils/String16.h>
#include "IAtChannel.h"
#include "AtChannel.h"
#include <cutils/properties.h>

using namespace android;

const int MAX_SERVICE_NAME = 100;

#define  MSMS_PHONE_COUNT_PROP             "persist.msms.phone_count"
#define  MODEM_TD_ENABLE_PROP              "persist.modem.t.enable"
#define  MODEM_TD_ID_PROP                  "ro.modem.t.id"
#define  MODEM_TD_COUNT_PROP               "ro.modem.t.count"
#define  MODEM_WCDMA_ENABLE_PROP           "persist.modem.w.enable"
#define  MODEM_WCDMA_ID_PROP               "ro.modem.w.id"
#define  MODEM_WCDMA_COUNT_PROP            "ro.modem.w.count"


static int getPhoneId(int modemId, int simId)
{
    /*
     * Because there is no docs, LTE should be supported later.
     */
    char prop[PROPERTY_VALUE_MAX] = "";
    int tdEnable, wEnable;
    int tdCount,  wCount;

    property_get(MODEM_TD_ENABLE_PROP, prop, "0");
    tdEnable = atoi(prop);
    memset(prop, '\0', sizeof(prop));
    property_get(MODEM_WCDMA_ENABLE_PROP, prop, "0");
    wEnable = atoi(prop);

    memset(prop, '\0', sizeof(prop));
    property_get(MODEM_TD_COUNT_PROP, prop, "0");
    tdCount = atoi(prop);
    memset(prop, '\0', sizeof(prop));
    property_get(MODEM_WCDMA_COUNT_PROP, prop, "0");
    wCount = atoi(prop);

    if (tdEnable) {
        if (wEnable) {
            return modemId * tdCount + simId;
        } else {
            return simId;
        }
    } else {
        if(wEnable) {
            return simId;
        } else {
            ALOGE("Both TD modem and WCDMA modem are disable, use default phoneID\n");
        }
    }

    return 0;
}

static String16 getServiceName(int modemId, int simId)
{
    char serviceName[MAX_SERVICE_NAME];
    char phoneCount[PROPERTY_VALUE_MAX] = "";

    memset(serviceName, 0, sizeof(serviceName));
    property_get(MSMS_PHONE_COUNT_PROP, phoneCount, "1");

    if (atoi(phoneCount) > 1){
        snprintf(serviceName, MAX_SERVICE_NAME - 1, "atchannel%d", getPhoneId(modemId, simId));
    } else {
        strcpy(serviceName,  "atchannel");
    }

    return String16(serviceName);
}

#ifdef FFOS_TEMP_AT

#define SOCKET_NAME_RIL_DEBUG_SIM1	"rild-debug"	/* from ril.cpp */
#define SOCKET_NAME_RIL_DEBUG_SIM2	"rild-debug1"	/* from ril.cpp */
char rec_buf[1024]={};

const char* sendAt(int modemId, int simId, const char* atCmd)
{
//    sp<IServiceManager> sm = defaultServiceManager();
//    if (sm == NULL) {
//        ALOGI("Couldn't get default ServiceManager\n");
//        return "ERROR1";
//    }
//
//    sp<IAtChannel> atChannel;
//    String16 serviceName = getServiceName(modemId, simId);
//    atChannel = interface_cast<IAtChannel>(sm->getService(serviceName));
//    if (atChannel == NULL) {
//        ALOGI("Couldn't get connection to %s\n", String8(serviceName).string());
//        return "ERROR2";
//    }
//
//    return atChannel->sendAt(atCmd);

    int fd;
    int i  = 0;
    const char* result = "OK";
    int ret =0;
    int num_socket_args = 2;
    char argv[3][100]={};
    

    ALOGI("sendAt: simid is %d, %s \n",simId,atCmd);

    if (simId == 0 ){
        fd = socket_local_client(SOCKET_NAME_RIL_DEBUG_SIM1,
                             ANDROID_SOCKET_NAMESPACE_RESERVED,
                             SOCK_STREAM);
    }else{
        fd = socket_local_client(SOCKET_NAME_RIL_DEBUG_SIM2,
                             ANDROID_SOCKET_NAMESPACE_RESERVED,
                             SOCK_STREAM);
    }

    ALOGI("sendAt: fd is %d\n",fd);
    if (fd < 0) {
        perror ("opening radio socket");
        goto error;
    }

    memset(rec_buf,sizeof(rec_buf),0);
    //int send( SOCKET s,      const char FAR *buf,      int len,      int flags );
    ret = send(fd, (const void *)&num_socket_args, sizeof(int), 0);
    if(ret != sizeof(int)) {
        perror ("Socket write error when sending num args");
        close(fd);
        goto error;
    }
    sprintf(argv[0],"11");
    sprintf(argv[1],"%s",atCmd);

    for (i = 0; i < num_socket_args; i++) {
        // Send length of the arg, followed by the arg.
        int len = strlen(argv[i]);
        ret = send(fd, &len, sizeof(int), 0);
        if (ret != sizeof(int)) {
            perror("Socket write Error: when sending arg length");
            close(fd);
            goto error;
        }
        ret = send(fd, argv[i], sizeof(char) * len, 0);
        if (ret != len * sizeof(char)) {
            perror ("Socket write Error: When sending arg");
            close(fd);
            goto error;
        }
    }

    //wait response
    for (i=0;i<3;i++)
    {
        sleep(1);
        ret = recv(fd,rec_buf,sizeof(rec_buf),0);
        if (ret > 0 )   //get response
        {
            ALOGI("sendAt: response is %s\n",rec_buf);
            return  rec_buf;
        }
        ALOGI("sendAt: still no response %d",i);
        sleep(1);
    }

    close(fd);
error:
    return result;
}
#else
const char* sendAt(int modemId, int simId, const char* atCmd)
{
    sp<IServiceManager> sm = defaultServiceManager();
    if (sm == NULL) {
        ALOGI("Couldn't get default ServiceManager\n");
        return "ERROR1";
    }

    sp<IAtChannel> atChannel;
    String16 serviceName = getServiceName(modemId, simId);
    atChannel = interface_cast<IAtChannel>(sm->getService(serviceName));
    if (atChannel == NULL) {
        ALOGI("Couldn't get connection to %s\n", String8(serviceName).string());
        return "ERROR2";
    }

    return atChannel->sendAt(atCmd);
}
#endif
