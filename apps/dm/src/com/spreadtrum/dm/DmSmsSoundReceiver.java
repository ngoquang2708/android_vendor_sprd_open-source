package com.spreadtrum.dm;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.SharedPreferences.Editor;
import android.os.Bundle;
import android.preference.PreferenceManager;
import android.util.Log;

public class DmSmsSoundReceiver extends BroadcastReceiver{
	private static final String SMSSOUND_ACTION = "com.android.dm.SmsSound";
	private String TAG = DmReceiver.DM_TAG + "DmSmsSoundReceiver: ";
	@Override
	public void onReceive(Context context, Intent intent) {
		// TODO Auto-generated method stub
		String action = intent.getAction();  
		if(SMSSOUND_ACTION.equals(action)){	         
			Bundle bundle= intent.getExtras();
			String smsSoundUrlString = bundle.getString("smssound");
			Log.i(TAG, "DmSmsSoundReceiver  smsSoundUrlString = " + smsSoundUrlString);
			if(null != smsSoundUrlString){				
				DmService.getInstance().setSMSSoundUri(smsSoundUrlString);
			}else {
				Log.i(TAG, "DmSmsSoundReceiver:smsSoundUrlString is null");
			}	
			
		}		
	}

}
