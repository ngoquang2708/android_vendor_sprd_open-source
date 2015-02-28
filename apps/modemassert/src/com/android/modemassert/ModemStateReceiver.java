package com.android.modemassert;

import android.app.Notification;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.util.Log;
import android.os.SystemProperties;

public class ModemStateReceiver extends BroadcastReceiver {
    // private static final String ACTION_MODEM_ASSERT =
    // "android.intent.action.MODEM_ASSERT";
    private static final String MODEM_STAT_CHANGE = "com.android.modemassert.MODEM_STAT_CHANGE";
    // extra data key in intent for assert broadcast
    private static final String MODEM_STAT = "modem_stat";
    // SPRD: add modem assert reason for PhoneInfo feature
    private static final String MODEM_INFO = "modem_info";
    private final String TAG = "ModemStateReceiver";
    // notification id to cancel
    private static final int MODEM_ASSERT_ID = 1;
    private static final int WCND_ASSERT_ID = 2;
    private static final int MODEM_BLOCK_ID = 3;
    private Context mContext;
    public ModemStateReceiver() {
    }

    @Override
    public void onReceive(Context context, Intent intent) {
        String action = intent.getAction();
        mContext = context;
        Log.d(TAG, "onReceive: enter");
        if (MODEM_STAT_CHANGE.equals(action)) {
            Log.d(TAG, "onReceive: MODEM_STAT_CHANGE.");
            String modemState = intent.getExtras().getString(MODEM_STAT);
            String modemInfo = intent.getExtras().getString(MODEM_INFO);
            Log.d(TAG, modemState + "  " + modemInfo);
            if (modemInfo.isEmpty()) {
                Log.d(TAG, "onReceive:modemInfo is Empty");
                return;
            }
            if (modemInfo.contains("Modem Alive")) {
                hideNotification(MODEM_ASSERT_ID);
                hideNotification(MODEM_BLOCK_ID);
            } else if (modemInfo.contains("Modem Assert")) {
                String value = SystemProperties.get("persist.sys.sprd.modemreset", "default");
                Log.d(TAG, " modemreset ? : " + value);
                if (!value.equals("1")) {
                    showNotification(MODEM_ASSERT_ID, "modem assert", modemInfo);
                }
                Intent intentLteReady = new Intent("android.intent.action.ACTION_LTE_READY");
                intentLteReady.putExtra("lte", false);
                Log.i(TAG, "modem assert Send ACTION_LTE_READY false");
                mContext.sendBroadcast(intentLteReady);
            } else if (modemInfo.contains("Modem Blocked")) {
                showNotification(MODEM_BLOCK_ID, "modem block", modemInfo);
            } else {
                Log.d(TAG, "do nothing with info :" + modemInfo);
            }
        }
    }

    private void showNotification(int notificationId, String title, String info) {
        Log.v(TAG, "show assert Notefication.");
        int icon = R.drawable.modem_assert;
        NotificationManager manager = (NotificationManager) mContext.getSystemService(Context.NOTIFICATION_SERVICE);
        long when = System.currentTimeMillis();
        Notification notification = new Notification(icon, info, when);
        CharSequence contentTitle = title;
        CharSequence contentText = info;
        Intent notificationIntent = new Intent(mContext,AssertInfoActivity.class);
        notificationIntent.putExtra("assertInfo", info);
        PendingIntent contentIntent = PendingIntent.getActivity(mContext, 0,notificationIntent, 0);
        long[] vibrate = { 0, 10000 };
        notification.vibrate = vibrate;
        notification.flags |= Notification.FLAG_NO_CLEAR;
        notification.defaults |= Notification.DEFAULT_SOUND;
        notification.setLatestEventInfo(mContext, contentTitle, contentText,contentIntent);
        manager.notify(notificationId, notification);
    }

    private void hideNotification(int notificationId) {
        Log.v(TAG, "hideNotification");
        NotificationManager manager = (NotificationManager) mContext.getSystemService(Context.NOTIFICATION_SERVICE);
        manager.cancel(notificationId);
    }

}