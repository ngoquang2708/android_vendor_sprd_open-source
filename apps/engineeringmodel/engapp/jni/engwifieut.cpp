#define LOG_TAG "engmodeljni"
#include <utils/Log.h>
#include <string.h>
#include <stdlib.h>

#include "engwifieut.h"
#include "eng_wifi_ptest.h"

PTEST_CMD_T  ptr;
WIFI_PTEST_CMD_E cmd;

JNIEXPORT jint JNICALL Java_com_spreadtrum_android_eng_EngWifieut_ptestCw
  (JNIEnv * env , jobject obj, jobject cw)
{
	//LOGE("cw code");
	jclass clazz_cw;
	int re;
	PTEST_RES_T * res;

	clazz_cw = env->GetObjectClass(cw);
	ptr.type = WIFI_PTEST_CW;
	ptr.band = env->GetIntField(cw,env->GetFieldID(clazz_cw,"band","I"));
	ptr.channel = env->GetIntField(cw,env->GetFieldID(clazz_cw,"channel","I"));
	ptr.sFactor = env->GetIntField(cw,env->GetFieldID(clazz_cw,"sFactor","I"));
	ptr.ptest_cw.frequency = env->GetIntField(cw,env->GetFieldID(clazz_cw,"frequency","I"));
	ptr.ptest_cw.frequencyOffset = env->GetIntField(cw,env->GetFieldID(clazz_cw,"frequencyOffset","I"));
	ptr.ptest_cw.amplitude = env->GetIntField(cw,env->GetFieldID(clazz_cw,"amplitude","I"));

	env->DeleteLocalRef(clazz_cw);
	//LOGE("cw type=%i,band=%i,channel=%i,sFactor=%i,cw.frequency=%i,cw.frequencyOffSet=%i,cw.amplitude=%i.",ptr.type,ptr.band,ptr.channel,ptr.sFactor,ptr.ptest_cw.frequency,ptr.ptest_cw.frequencyOffset,ptr.ptest_cw.amplitude);

	res = wifi_ptest_cw(&ptr);
	//LOGE("cw result=%i",res->result);
	if(!res)
	{
		return 1;
	}
	return res->result;
}

JNIEXPORT jint JNICALL Java_com_spreadtrum_android_eng_EngWifieut_ptestTx
  (JNIEnv * env, jobject obj, jobject tx)
{
	//LOGE("tx code");
	jclass clazz_tx;
	int re;
	jstring macs;
	const char * macc;
	CsrUint8 mac[6];
	CsrWifiMacAddress macaddr;
	PTEST_RES_T * res;

	cmd = WIFI_PTEST_TX;
	clazz_tx = env->GetObjectClass(tx);
	ptr.type = WIFI_PTEST_TX;
	ptr.band = env->GetIntField(tx,env->GetFieldID(clazz_tx,"band","I"));
	ptr.channel = env->GetIntField(tx,env->GetFieldID(clazz_tx,"channel","I"));
	ptr.sFactor = env->GetIntField(tx,env->GetFieldID(clazz_tx,"sFactor","I"));
	ptr.ptest_tx.rate = env->GetIntField(tx,env->GetFieldID(clazz_tx,"rate","I"));
	ptr.ptest_tx.powerLevel = env->GetIntField(tx,env->GetFieldID(clazz_tx,"powerLevel","I"));
	ptr.ptest_tx.length = env->GetIntField(tx,env->GetFieldID(clazz_tx,"length","I"));
	ptr.ptest_tx.enable11bCsTestMode = env->GetIntField(tx,env->GetFieldID(clazz_tx,"enablelbCsTestMode","I"));
	ptr.ptest_tx.interval = env->GetIntField(tx,env->GetFieldID(clazz_tx,"interval","I"));
	macs = (jstring)env->GetObjectField(tx,env->GetFieldID(clazz_tx,"destMacAddr","Ljava/lang/String;"));
	macc = env->GetStringUTFChars(macs,0);
	//LOGE("macc=%s=",macc);
	int i;
	char temp[2];
	for(i=0;i<6;i++){
		temp[0] = macc[2*i];
		temp[1]= macc[2*i+1];
		mac[i] = strtol(temp,NULL,16);
	}

	env->ReleaseStringUTFChars(macs,macc);
	memcpy(macaddr.a,mac,6);
	ptr.ptest_tx.destMacAddr = macaddr;
	//LOGE("macaddr.a=%x:%x:%x:%x:%x:%x",macaddr.a[0],macaddr.a[1],macaddr.a[2],macaddr.a[3],macaddr.a[4],macaddr.a[5]);
	ptr.ptest_tx.preamble = env->GetIntField(tx,env->GetFieldID(clazz_tx,"preamble","I"));

	//LOGE("tx type=%i,band=%i,channel=%i,sFactor=%i,tx.rate=%i,tx.powerLevel=%i,tx.length=%i,tx.enable11bCsTestMode=%i,tx.interval=%i,tx.preamble=%i",ptr.type,ptr.band,ptr.channel,ptr.sFactor,ptr.ptest_tx.rate,ptr.ptest_tx.powerLevel,ptr.ptest_tx.length,ptr.ptest_tx.enable11bCsTestMode,ptr.ptest_tx.interval,ptr.ptest_tx.preamble);

	env->DeleteLocalRef(clazz_tx);
	res = wifi_ptest_tx(&ptr);
	//LOGE("tx result=%i",res->result);
	if(!res)
	{
		return 1;
	}
	return res->result;
}

JNIEXPORT jint JNICALL Java_com_spreadtrum_android_eng_EngWifieut_ptestRx
  (JNIEnv * env, jobject obj , jobject rx)
{
	//LOGE("rx code");
	jclass clazz_rx;
	int re;
	PTEST_RES_T * res;
	
	cmd = WIFI_PTEST_RX;
	clazz_rx = env->GetObjectClass(rx);
	ptr.type = cmd;
	ptr.band = env->GetIntField(rx,env->GetFieldID(clazz_rx,"band","I"));
	ptr.channel = env->GetIntField(rx,env->GetFieldID(clazz_rx,"channel","I"));
	ptr.sFactor = env->GetIntField(rx,env->GetFieldID(clazz_rx,"sFactor","I"));
	ptr.ptest_rx.frequency = env->GetIntField(rx,env->GetFieldID(clazz_rx,"frequency","I"));
	ptr.ptest_rx.filteringEnable = env->GetIntField(rx,env->GetFieldID(clazz_rx,"filteringEnable","I"));

	env->DeleteLocalRef(clazz_rx);
	//LOGE("rx type=%i,band=%i,channel=%i,sFactor=%i,rx.frequency=%i,rx.filteringEnable=%i.",ptr.type,ptr.band,ptr.channel,ptr.sFactor,ptr.ptest_rx.frequency,ptr.ptest_rx.filteringEnable);

	res = wifi_ptest_rx(&ptr);

	if(!res)
	{
		return 1;
	}
	return res->result;
/*
	if(!res)
	{
	PTEST_RES_T ress ;
	res = &ress;	
	res->result=0;
	res->total = 100;
	res->totalGood = 50;
	res->rssiAv = 300;
	}

	jintArray args = env->NewIntArray(4);
	jint num[4];
	num[0] = (jint)res->result;
	num[1] = (jint)res->total;
	num[2] = (jint)res->totalGood;
	num[3] = (jint)res->rssiAv;
	//LOGE("RX result result=%i,total=%i,totalGood=%i,rssiAV=%i",res->result,res->total,res->totalGood,res->rssiAv);
	env->SetIntArrayRegion(args,0,4,num);
	env->DeleteLocalRef(args);
	return args;*/
}

JNIEXPORT jint JNICALL Java_com_spreadtrum_android_eng_EngWifieut_ptestInit
  (JNIEnv * env, jobject obj)
{
	//LOGE("ptest init......");
	PTEST_RES_T * res;

	ptr.type = WIFI_PTEST_INIT;
	res = wifi_ptest_init(&ptr);

	if(!res)
	{
		return 1;
	}
	//LOGE("ptest init......re=%i",res->result);
	return res->result;
}

JNIEXPORT jint JNICALL Java_com_spreadtrum_android_eng_EngWifieut_ptestDeinit
  (JNIEnv * env, jobject obj)
{
	//LOGE("ptest Deinit......");
	PTEST_RES_T * res;

	ptr.type = WIFI_PTEST_DEINIT;
	res = wifi_ptest_deinit(&ptr);

	if(!res)
	{
		return 1;
	}
	//LOGE("ptest Deinit......re=%i",res->result);
	return res->result;
}


JNIEXPORT void JNICALL Java_com_spreadtrum_android_eng_EngWifieut_ptestSetValue
  (JNIEnv * env, jobject obj)
{
	
	//LOGE("set value begin");
	set_value(0);
	//LOGE("set value end");
}

JNIEXPORT jint JNICALL Java_com_spreadtrum_android_eng_EngWifieut_ptestBtStart
  (JNIEnv * env, jobject obj)
{
	return bt_ptest_start();
}

JNIEXPORT jint JNICALL Java_com_spreadtrum_android_eng_EngWifieut_ptestBtStop
  (JNIEnv * env, jobject obj)
{
	return bt_ptest_stop();
}
