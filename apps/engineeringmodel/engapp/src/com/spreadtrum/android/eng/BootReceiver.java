package com.spreadtrum.android.eng;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.os.SystemProperties;
import android.preference.PreferenceManager;
import android.util.Log;

public class BootReceiver extends BroadcastReceiver {

	public static final String TAG = "BootReceiver";
	public static boolean isBoot = false;//iqmode
	
	@Override
	public void onReceive(Context context, Intent intent) {
		isBoot = true;
		
		//for qos
		SharedPreferences pref = PreferenceManager.getDefaultSharedPreferences(context);
		boolean qos = pref.getBoolean("Qos_switch", false);
		if(qos){
			SystemProperties.set("persist.sys.qosstate", "1");
		}else{
			SystemProperties.set("persist.sys.qosstate", "0");
		}
	
//		Log.e(TAG, "----------------qos_state="+SystemProperties.get("persist.sys.qosstate", "") );
	}

}
//add by wangxiaobin 2011-09-21