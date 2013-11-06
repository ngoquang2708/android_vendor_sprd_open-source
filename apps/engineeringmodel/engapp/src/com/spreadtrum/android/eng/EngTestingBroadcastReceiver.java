/*
package com.spreadtrum.android.eng;

import android.provider.Telephony;
import static android.provider.Telephony.Intents.SECRET_CODE_ACTION;

import android.content.Context;
import android.content.Intent;
import android.content.BroadcastReceiver;
import android.util.Config;
import android.util.Log;
import android.view.KeyEvent;


public class EngTestingBroadcastReceiver extends BroadcastReceiver {
  
    public EngTestingBroadcastReceiver() {
    }
    
    public void onReceive(Context context, Intent intent) {
        if (intent.getAction().equals(SECRET_CODE_ACTION)) {
            Intent i = new Intent(Intent.ACTION_MAIN);
            //i.setClass(context, versioninfo.class);            
            i.setClass(context, sprd_engmode.class);
            i.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            context.startActivity(i);
        }
    }
}

*/
//package com.gy.engmodepackage;
package com.spreadtrum.android.eng;

import static android.provider.Telephony.Intents.SECRET_CODE_ACTION;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.Debug;
import android.util.Log;


public class  EngTestingBroadcastReceiver extends BroadcastReceiver {
    private static final boolean DEBUG = Debug.isDebug();
    public EngTestingBroadcastReceiver() {
    }

    @Override
    public void onReceive(Context context, Intent intent) {
        if (intent.getAction().equals(SECRET_CODE_ACTION)) {
            Uri uri = intent.getData();
            String host = uri.getHost();
            
            if(DEBUG) Log.d("abel", "uri="+uri);
            Intent i = new Intent(Intent.ACTION_MAIN);
            i.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            
            if("83780".equals(host)){
                i.setClass(context, Eng83780Activity.class);
                context.startActivity(i);
            }else if("83781".equals(host)){
                i.setClass(context, Eng83781Activity.class);
                context.startActivity(i);
            }
			//heng.xiao add for can riveiw patch list
            else if("72824".equals(host)){
                i.setClass(context, EngPatchRecordActivity.class);
                context.startActivity(i);
            }
            
        }
    }
}

