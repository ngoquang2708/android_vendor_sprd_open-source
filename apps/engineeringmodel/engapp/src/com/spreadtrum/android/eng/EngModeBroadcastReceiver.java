
package com.spreadtrum.android.eng;

import static android.provider.Telephony.Intents.SECRET_CODE_ACTION;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Debug;
import android.preference.PreferenceManager;
import android.util.Log;

public class EngModeBroadcastReceiver extends BroadcastReceiver {
    private static final boolean DEBUG = Debug.isDebug();
    private static final String TAG = "EngModeBroadcastReceiver";

    @Override
    public void onReceive(Context context, Intent intent) {
        String action = intent.getAction();
        /*Delete 20130626 Spreadst of 180419 remove the 83782 secret code start*/
        /*
         * if (SECRET_CODE_ACTION.equals(action)) {// SECRET_CODE_ACTION Intent
         * i = new Intent(Intent.ACTION_MAIN); // i.setClass(context,
         * sprd_engmode.class); // i.setClass(context, versioninfo.class);
         * i.setClass(context, engineeringmodel.class);
         * i.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK); context.startActivity(i);
         * } else
         */
        /*Delete 20130626 Spreadst of 180419 remove the 83782 secret code end*/
        if (Intent.ACTION_BOOT_COMPLETED.equals(action)) {
            SharedPreferences pref = PreferenceManager.getDefaultSharedPreferences(context);
            if (pref != null) {
                boolean exist = pref.contains(logswitch.KEY_CAPLOG);
                if (exist) {
                    SharedPreferences.Editor editor = pref.edit();
                    boolean v = pref.getBoolean(logswitch.KEY_CAPLOG, false);
                    if (DEBUG)
                        Log.d(TAG, "cap_log values : " + v);
                    editor.putBoolean(logswitch.KEY_CAPLOG, false);
                    editor.apply();
                } else {
                    Log.e(TAG, "cap_log values not exist !");
                }
            }
        }
    }
}
