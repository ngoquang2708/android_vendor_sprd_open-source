package com.android.modemassert;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.text.TextUtils;
import android.util.Log;
import android.app.ActivityManager;
import android.app.Application;
import android.app.Notification;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import android.os.Binder;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.os.Looper;
import android.os.Message;
import android.os.Parcel;
import android.os.RemoteException;
import android.os.SystemClock;
import android.os.SystemProperties;

import java.io.IOException;
import java.io.InputStream;
import java.io.UnsupportedEncodingException;
import java.lang.reflect.Method;

public class AssertApplication extends Application {
    private final String MTAG = "AssertApplication";

    //Action for assert broadcast
    private static final String MODEM_STAT_CHANGE = "com.android.modemassert.MODEM_STAT_CHANGE";
    //extra data key in intent for assert broadcast
    private static final String MODEM_STAT = "modem_stat";
    //values of extra data in intent.
    private static final String MODEM_ALIVE = "modem_alive";
    private static final String MODEM_ASSERT = "modem_assert";

    //name of socket to listen to modem state
    private static final String MODEM_SOCKET_NAME = "modemd";

    //notification id to cancel
    private static final int MODEM_ASSERT_ID = 1;

    private static final int BUF_SIZE = 128;

    private final Object mObjectLock = new Object();
    private Handler mAssertHandler;
    private HandlerThread mAssertThread;

    @Override
    public void onCreate() {
        super.onCreate();
        Log.d(MTAG, "onCreate()...");
        mAssertThread = new HandlerThread("assertHandlerThread");
        mAssertThread.start();
        mAssertHandler = new assertHandler(mAssertThread.getLooper());
        mAssertHandler.sendEmptyMessage(0);
    }

    private class assertHandler extends Handler {
        public assertHandler(Looper looper) {
            super(looper);
        }

        @Override
        public void handleMessage(Message msg) {
            try {
                LocalSocket socket = new LocalSocket();
                LocalSocketAddress socketAddr = new LocalSocketAddress(MODEM_SOCKET_NAME,
                        LocalSocketAddress.Namespace.ABSTRACT);
                runSocket(socket, socketAddr);
            } catch (Exception ex) {
                ex.printStackTrace();
            }
        }
    }

    private void connectToSocket(LocalSocket socket, LocalSocketAddress socketAddr) {
        for (;;) {
            try {
                socket.connect(socketAddr);
                break;
            } catch (IOException ioe) {
                ioe.printStackTrace();
                SystemClock.sleep(10000);
                continue;
            }
        }
    }

    private void runSocket(LocalSocket socket, LocalSocketAddress socketAddr) {
        byte[] buf = new byte[BUF_SIZE];
        connectToSocket(socket, socketAddr);

        Log.d(MTAG, " -runSocket");
        synchronized (mObjectLock) {
            for (;;) {
                int cnt = 0;
                InputStream is = null;
                try {
                    // mBinder.wait(endtime - System.currentTimeMillis());
                    is = socket.getInputStream();
                    cnt = is.read(buf, 0, BUF_SIZE);
                    Log.d(MTAG, "read " + cnt + " bytes from socket: \n" );
                } catch (IOException e) {
                    Log.w(MTAG, "read exception\n");
                }
                if (cnt > 0) {
                    String info = "";
                    try {
                        info = new String(buf, 0, cnt, "US-ASCII");
                    } catch (UnsupportedEncodingException e) {
                        e.printStackTrace();
                    } catch (StringIndexOutOfBoundsException e) {
                        e.printStackTrace();
                    }
                    Log.d(MTAG, "read something: "+ info);
                    if (TextUtils.isEmpty(info)) {
                        continue;
                    }
                    if (info.contains("Modem Alive")) {
                        sendModemStatBroadcast(MODEM_ALIVE);
                        hideNotification();
                    } else if (info.contains("Modem Assert")) {
                        String value = SystemProperties.get("persist.sys.sprd.modemreset", "default");
                        Log.d(MTAG, " modemreset ? : " + value);
                        if(!value.equals("1")){
                            showNotification(info);
                        }
                        sendModemStatBroadcast(MODEM_ASSERT);
                    } else {
                        Log.d(MTAG, "do nothing with info :" + info);
                    }
                    continue;
                } else if (cnt < 0) {
                    try {
                        is.close();
                        socket.close();
                    } catch (IOException e) {
                        Log.w(MTAG, "close exception\n");
                    }
                    socket = new LocalSocket();
                    connectToSocket(socket, socketAddr);
                }
            }
        }
    }

    private void showNotification(String info) {
        Log.v(MTAG, "show assert Notefication.");
        int icon = R.drawable.modem_assert;
        NotificationManager manager = (NotificationManager)getSystemService(NOTIFICATION_SERVICE);
        long when = System.currentTimeMillis();
        Notification notification = new Notification(icon, info, when);

        Context context = getApplicationContext();
        CharSequence contentTitle = "modem assert";
        CharSequence contentText = info;
        /** modify 145779 add show assertinfo page **/
        Intent notificationIntent = new Intent(this, AssertInfoActivity.class);
        notificationIntent.putExtra("assertInfo", info);
        PendingIntent contentIntent =  PendingIntent.getActivity(context, 0, notificationIntent, 0);

        //notification.defaults |= Notification.DEFAULT_VIBRATE;
        long[] vibrate = {0, 10000};
        notification.vibrate = vibrate;
        notification.flags |= Notification.FLAG_NO_CLEAR;// no clear
        /** modify 145779 add show assertinfo page **/
        notification.defaults |= Notification.DEFAULT_SOUND;
        //notification.sound = Uri.parse("file:///sdcard/assert.mp3");

        notification.setLatestEventInfo(context, contentTitle, contentText, contentIntent);
        manager.notify(MODEM_ASSERT_ID, notification);
    }

    private void hideNotification() {
        Log.v(MTAG, "hideNotification");
        NotificationManager manager = (NotificationManager)getSystemService(NOTIFICATION_SERVICE);
        manager.cancel(MODEM_ASSERT_ID);
    }

    private void sendModemStatBroadcast(String modemStat) {
        Log.d(MTAG, "sendModemStatBroadcast : " + modemStat);
        Intent intent = new Intent(MODEM_STAT_CHANGE);
        intent.putExtra(MODEM_STAT, modemStat);
        sendBroadcast(intent);
    }

}
