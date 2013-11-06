/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "engmodeljni"
#include <utils/Log.h>

#include <stdio.h>

#include "jni.h"

#include "engapi.h"

/*
static jint
add(JNIEnv *env, jobject thiz, jint a, jint b) {
int result = a + b;
    LOGI("%d + %d = %d", a, b, result);
    return result;
}
*/

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

static jint
engf_getphasecheck(JNIEnv* env, jobject clazz, jbyteArray data, int size)
{
    int ret = 0;

    if (env->GetArrayLength(data) < (size)) {
        ALOGE("engf_getphasecheck size error");
        return 0;
    }

    jbyte* dataBytes = env->GetByteArrayElements(data, NULL);
    if (dataBytes == NULL) {
        ALOGE("engf_getphasecheck dataBytes is NULL");
        return 0;
    }

    ret = engapi_getphasecheck(dataBytes, size);
    ALOGE("engf_getphasecheck:%d", ret);
    env->ReleaseByteArrayElements(data, dataBytes, 0);

    return ret;
}

static const char *classPathName = "com/spreadtrum/android/eng/engfetch";

static JNINativeMethod methods[] = {
  //{"add", "(II)I", (void*)add },
  { "engf_open", "(I)I", (void*)engf_open },
  { "engf_close", "(I)V", (void*)engf_close },
  { "engf_write", "(I[BI)I", (void*)engf_write },
  { "engf_read", "(I[BI)I", (void*)engf_read }, 
  { "engf_getphasecheck", "([BI)I", (void*)engf_getphasecheck}, 
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
        ALOGE("Native registration unable to find class '%s'", className);
        return JNI_FALSE;
    }
    if (env->RegisterNatives(clazz, gMethods, numMethods) < 0) {
        ALOGE("RegisterNatives failed for '%s'", className);
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
    
    ALOGI("JNI_OnLoad");

    if (vm->GetEnv(&uenv.venv, JNI_VERSION_1_4) != JNI_OK) {
        ALOGE("ERROR: GetEnv failed");
        goto bail;
    }
    env = uenv.env;

    if (registerNatives(env) != JNI_TRUE) {
        ALOGE("ERROR: registerNatives failed");
        goto bail;
    }
    
    result = JNI_VERSION_1_4;
    
bail:
    return result;
}
