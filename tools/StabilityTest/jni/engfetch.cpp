
#include "jni.h"
#include <cutils/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/input.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include "engfetch.h"
#include "engapi.h"

#define PHASE_CHECKE_FILE "/productinfo/productinfo.bin"


int eng_getphasecheck(SP09_PHASE_CHECK_T* phase_check)
{
    int ret = 0;
    int len;
    int fd = open(PHASE_CHECKE_FILE,O_RDONLY);
    if (fd <0)
    {
        ALOGE("open failed");
        return -1;
    }
    len = read(fd,phase_check,sizeof(SP09_PHASE_CHECK_T));


    if (len <= 0){
    ret = 1;
    }
    close(fd);
    return ret;
}

int engapi_getphasecheck(void* buf, int size)
{
    int readsize = 0;
    int ret = 0;
    char * str = (char *)buf;
    SP09_PHASE_CHECK_T phasecheck = {0};
    ALOGD("engapi_getphasecheck");    
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

        if(strlen(phasecheck.SN2) != 0)
        {
            memset((void*)sn, 0, SP09_MAX_SN_LEN + 1);
            strcat(str, "sn2:");
            strncpy(sn, phasecheck.SN2, SP09_MAX_SN_LEN);
            strcat(str, sn);
            strcat(str, "\r\n");
        }
        ALOGD("engapi_getphasecheck:%s", (char*)buf);
        readsize = strlen(str);
        return readsize;
    }
    else
    {
        strncpy(str, "read phase check error", 22);
        return 22;
    }
}

static jint
engf_getphasecheck(JNIEnv* env, jobject clazz, jbyteArray data, int size)
{
    int ret = 0;

    if (env->GetArrayLength(data) < (size)) {
        ALOGD("engf_getphasecheck size error");
        return 0;
    }

    jbyte* dataBytes = env->GetByteArrayElements(data, NULL);
    if (dataBytes == NULL) {
        ALOGD("engf_getphasecheck dataBytes is NULL");
        return 0;
    }

    ret = engapi_getphasecheck(dataBytes, size);
    ALOGD("engf_getphasecheck:%d", ret);
    env->ReleaseByteArrayElements(data, dataBytes, 0);

    return ret;
}
static void
vibrator_test(JNIEnv* env, jobject clazz,int test)
{
   ALOGD("test:%d", test);
     system("echo 1 > /sys/class/vibratortest/status"); 
}
static void
vibrator_stop(JNIEnv* env, jobject clazz,int test)
{
   ALOGD("test:%d", test);
     system("echo 0 > /sys/class/vibratortest/status"); 
}
static void
flash_off(JNIEnv* env, jobject clazz,int test)
{
   ALOGD("test:%d", test);
     system("echo 0 > /sys/class/vibratortest/flashled"); 
}
static void
flash_on(JNIEnv* env, jobject clazz,int test)
{
   ALOGD("test:%d", test);
     system("echo 1 > /sys/class/vibratortest/flashled"); 
}

static jint engf_open(JNIEnv* env, jobject obj, int type)
{
	int s = engapi_open(type);
	ALOGE("engf_open = %d type = %d", s, type);	
	return s;
}

static void engf_close(JNIEnv* env, jobject obj, jint fd)
{
	engapi_close(fd);
}

static jint
engf_write(JNIEnv* env, jobject clazz, int w, jbyteArray data, int size)//writeEntityData_native
{
    int err=0;

    if (env->GetArrayLength(data) < size) {
        // size mismatch
        return -1;
    }

    jbyte* dataBytes = env->GetByteArrayElements(data, NULL);
    if (dataBytes == NULL) {
        return -1;
    }

    //err = writer->WriteEntityData(dataBytes, size);
	err = engapi_write(w,dataBytes,size);
	ALOGE("engf_write ret=%d,size=%d", err,size);
	
    env->ReleaseByteArrayElements(data, dataBytes, JNI_ABORT);

    return err;
}

static jint
engf_read(JNIEnv* env, jobject clazz, int r, jbyteArray data, int size)//readEntityData_native
{
    int err=0;

    if (env->GetArrayLength(data) < (size)) {
        // size mismatch
        return -1;
    }

    jbyte* dataBytes = env->GetByteArrayElements(data, NULL);
    if (dataBytes == NULL) {
        return -2;
    }

    //err = reader->ReadEntityData(dataBytes+offset, size);
	//err = eng_read(r,dataBytes,size);
	err = engapi_read(r,dataBytes,size);
	ALOGE("engf_read err=%d,size=%d,%s", err,size,dataBytes);
    env->ReleaseByteArrayElements(data, dataBytes, 0);

    return err;
}

static const char *classPathName = "com/android/stability/engfetch";

static JNINativeMethod methods[] = {
  //{"add", "(II)I", (void*)add }, 
  { "engf_getphasecheck", "([BI)I", (void*)engf_getphasecheck}, 
  { "vibrator_test", "(I)V", (void*)vibrator_test}, 
  { "vibrator_stop", "(I)V", (void*)vibrator_stop}, 
  { "flash_off", "(I)V", (void*)flash_off}, 
  { "flash_on", "(I)V", (void*)flash_on}, 
  { "engf_open", "(I)I", (void*)engf_open },
  { "engf_close", "(I)V", (void*)engf_close },
  { "engf_write", "(I[BI)I", (void*)engf_write },
  { "engf_read", "(I[BI)I", (void*)engf_read }, 
};

/*
 * Register several native methods for one class.
 */
static int registerNativeMethods(JNIEnv* env, const char* className,
    JNINativeMethod* gMethods, int numMethods)
{
    jclass clazz;

    clazz = env->FindClass(className);
    if (clazz == NULL) {
        ALOGD("Native registration unable to find class '%s'", className);
        return JNI_FALSE;
    }
    if (env->RegisterNatives(clazz, gMethods, numMethods) < 0) {
        ALOGD("RegisterNatives failed for '%s'", className);
        return JNI_FALSE;
    }

    return JNI_TRUE;
}

/*
 * Register native methods for all classes we know about.
 *
 * returns JNI_TRUE on success.
 */
static int registerNatives(JNIEnv* env)
{
  if (!registerNativeMethods(env, classPathName,
                 methods, sizeof(methods) / sizeof(methods[0]))) {
    return JNI_FALSE;
  }

  return JNI_TRUE;
}


// ----------------------------------------------------------------------------

/*
 * This is called by the VM when the shared library is first loaded.
 */
 
typedef union {
    JNIEnv* env;
    void* venv;
} UnionJNIEnvToVoid;

jint JNI_OnLoad(JavaVM* vm, void* reserved)
{
    UnionJNIEnvToVoid uenv;
    uenv.venv = NULL;
    jint result = -1;
    JNIEnv* env = NULL;

    if (vm->GetEnv(&uenv.venv, JNI_VERSION_1_4) != JNI_OK) {
        ALOGD("ERROR: GetEnv failed");
        goto bail;
    }
    env = uenv.env;

    if (registerNatives(env) != JNI_TRUE) {
        ALOGD("ERROR: registerNatives failed");
        goto bail;
    }
    
    result = JNI_VERSION_1_4;
    
bail:
    return result;
}





