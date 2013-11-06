package com.spreadtrum.android.eng;

import android.provider.Telephony;
import static android.provider.Telephony.Intents.SECRET_CODE_ACTION;
import android.content.Context;
import android.content.Intent;
import android.content.BroadcastReceiver;

public class  EngVersionBroadcastReceiver extends BroadcastReceiver {
private static final String ACTION_SIMPLECODE = "android.intent.action.SIMPLECODE";
    public EngVersionBroadcastReceiver() {
    }

    public void onReceive(Context context, Intent intent) {
        if (intent.getAction().equals(SECRET_CODE_ACTION)) {
            Intent i = new Intent(ACTION_SIMPLECODE);
            i.setClass(context, VersionInfoCustomer.class);
            i.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            context.startActivity(i);
        }
    }
}