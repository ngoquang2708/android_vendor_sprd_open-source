package com.spreadtrum.android.eng;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;

import com.spreadtrum.android.eng.R;

import android.app.Notification;
import android.app.PendingIntent;
import android.app.Service;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.IBinder;
import android.util.Log;

/* SlogUI Added by Yuntao.xiao*/

public class SlogService extends Service {

    private static final Class[] mStartForegroundSignature = new Class[] {
            int.class, Notification.class };

    private static final Class[] mStopForegroundSignature = new Class[] { boolean.class };

    private Method mStartForeground;
    private Method mStopForeground;
    private Object[] mStartForegroundArgs = new Object[2];
    private Object[] mStopForegroundArgs = new Object[1];
    Notification notification;

    @Override
    public void onCreate() {

        // super.onCreate();
        try {
            mStartForeground = getClass().getMethod("startForeground",
                    mStartForegroundSignature);
            mStopForeground = getClass().getMethod("stopForeground",
                    mStopForegroundSignature);
        } catch (NoSuchMethodException e) {
            // Running on an older platform.
            mStartForeground = mStopForeground = null;
        }
        registerReceiver(mLocalChangeReceiver,
                new IntentFilter(Intent.ACTION_LOCALE_CHANGED));
    }

    private final BroadcastReceiver mLocalChangeReceiver = new BroadcastReceiver() {
        public void onReceive(Context context, Intent intent) {
            Log.d("SlogService", "language is change....");
            setNotification();
        }
    };
    @Override
    public void onStart(Intent intent, int startId) {

        // Set the icon, scrolling text and timestamp
        notification = new Notification(android.R.drawable.ic_dialog_alert,
                getText(R.string.notification_slogsvc_statusbarprompt), 0);

        setNotification();
    }
    private void setNotification() {
        // The PendingIntent to launch our activity if the user selects this
        // notification

        PendingIntent contentIntent = PendingIntent.getActivity(this, 0,
                new Intent(this, LogSettingSlogUITabHostActivity.class), 0);

        // Set the info for the views that show in the notification panel.
        notification.setLatestEventInfo(this,
                getText(R.string.notification_slogsvc_title),
                getText(R.string.notification_slogsvc_prompt), contentIntent);

        Runnable runSlogService = new Runnable() {

            public void run() {
                if (mStartForeground != null) {
                    mStartForegroundArgs[0] = Integer.valueOf(1);
                    mStartForegroundArgs[1] = notification;
                    try {
                        mStartForeground.invoke(SlogService.this,
                                mStartForegroundArgs);
                    } catch (InvocationTargetException e) {
                        // Should not happen.
                        Log.w("Slog", "Unable to invoke startForeground", e);
                    } catch (IllegalAccessException e) {
                        // Should not happen.
                        Log.w("Slog", "Unable to invoke startForeground", e);
                    }
                    return;
                }
            }
        };

        Thread threadSlogService = new Thread(null, runSlogService,
                "SlogService");
        threadSlogService.start();

    }

    @Override
    public void onDestroy() {
        // TODO Auto-generated method stub
        if (mStopForeground != null) {
            mStopForegroundArgs[0] = Boolean.TRUE;
            try {
                mStopForeground.invoke(this, mStopForegroundArgs);
            } catch (InvocationTargetException e) {
                // Should not happen.
                Log.w("Slog", "Unable to invoke stopForeground", e);
            } catch (IllegalAccessException e) {
                // Should not happen.
                Log.w("Slog", "Unable to invoke stopForeground", e);
            }
            return;
        }
        unregisterReceiver(mLocalChangeReceiver);
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        // TODO Auto-generated method stub
        return null;
    }

}
