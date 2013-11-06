package com.android.modemassert;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
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
    private Handler assertHandler;
    private HandlerThread assertHandlerThread;
    private static final String MODEM_STAT_CHANGE = "com.android.modemassert.MODEM_STAT_CHANGE";
    private static final String MODEM_STAT = "modem_stat";
    private static final String MODEM_SOCKET_NAME = "modemd";
    private static final String MODEM_ALIVE = "modem_alive";
    private static final String MODEM_ASSERT = "modem_assert";
    private static final int MODEM_ASSERT_ID = 1;
    private static final int BUILD_SOCKET = 2;
    private static final int BUF_SIZE = 128;
    private NotificationManager mNm;
    private CharSequence AssertInfo;

    private final IBinder mBinder = new Binder() {
        @Override
        protected boolean onTransact(int code, Parcel data, Parcel reply,
                int flag) throws RemoteException {
            return super.onTransact(code, data, reply, flag);
        }
    };

    @Override
    public void onCreate() {
        super.onCreate();
        Log.d(MTAG, "onCreate()...");
        assertHandlerThread = new HandlerThread("assertHandlerThread");
        assertHandlerThread.start();
        assertHandler = new assertHandler(assertHandlerThread.getLooper());
        assertHandler.sendEmptyMessage(BUILD_SOCKET);
    }

    private class assertHandler extends Handler {
        public assertHandler(Looper looper) {
            super(looper);
        }
        @Override
        public void handleMessage(Message msg) {
            Log.d(MTAG, "handleMessage : "+msg.what);
            switch (msg.what) {
                case BUILD_SOCKET:
                    setupSocket();
                    break;
                default:
                    break;
            }
        }
    }

    private void setupSocket() {
        LocalSocket socket = null;
        LocalSocketAddress socketAddr = null;

        Log.d(MTAG, "setupSocket()...");
        // create local socket
        try {
            socket = new LocalSocket();
            socketAddr = new LocalSocketAddress(MODEM_SOCKET_NAME,
                    LocalSocketAddress.Namespace.ABSTRACT);
        } catch (Exception ex) {
            Log.w(MTAG, "create client socket Exception" + ex);
            if (socket == null)
                Log.w(MTAG, "create client socket error\n");
        }
        runSocket(socket, socketAddr);
    }

    private void connectToSocket(LocalSocket socket, LocalSocketAddress socketAddr) {
        for (;;) {
            try {
                Log.d(MTAG, "connectToSocket()...");
                socket.connect(socketAddr);
                break;
            } catch (IOException ioe) {
                Log.w(MTAG, "connect server error\n");
                SystemClock.sleep(10000);
                continue;
            }
        }
    }

    private void runSocket(LocalSocket socket, LocalSocketAddress socketAddr) {
        byte[] buf = new byte[BUF_SIZE];
        connectToSocket(socket, socketAddr);

        Log.d(MTAG, " -runSocket");
        synchronized (mBinder) {
            for (;;) {
                int cnt = 0;
                InputStream is = null;
                try {
                    // mBinder.wait(endtime - System.currentTimeMillis());
                    is = socket.getInputStream();
                    Log.d(MTAG, "read from socket is : "+is.toString());
                    cnt = is.read(buf, 0, BUF_SIZE);
                    Log.d(MTAG, "read " + cnt + " bytes from socket: \n" );
                } catch (IOException e) {
                    Log.w(MTAG, "read exception\n");
                }
                if (cnt > 0) {
                    String tempStr = "";
                    try {
                        tempStr = new String(buf, 0, cnt, "US-ASCII");
                    } catch (UnsupportedEncodingException e) {
                        // TODO Auto-generated catch block
                        Log.w(MTAG, "buf transfer char fail\n");
                    } catch (StringIndexOutOfBoundsException e) {
                        Log.w(MTAG, "StringIndexOutOfBoundsException\n");
                    }
                    AssertInfo = tempStr;
                    Log.d(MTAG, "read something: "+ tempStr);
                    if (tempStr.contains("Modem Alive")) {
                        sendModemStatBroadcast(MODEM_ALIVE);
                        hideNotification();
                    } else if (tempStr.length() > 0) {
                        String value = SystemProperties.get("persist.sys.sprd.modemreset", "default");
                        Log.d(MTAG, "SystemProperties value is equals 1...after modem assert, modem will be reset.");
                        if(!value.equals("1")){
                            showNotification();
                        }
                        sendModemStatBroadcast(MODEM_ASSERT);
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

    private void showNotification() {
        Log.v(MTAG, "showNotefication");
        int icon = R.drawable.modem_assert;
        mNm = (NotificationManager)getSystemService(NOTIFICATION_SERVICE);
        long when = System.currentTimeMillis();
        Notification notification = new Notification(icon, AssertInfo, when);

        Context context = getApplicationContext();
        CharSequence contentTitle = "modem assert";
        CharSequence contentText = AssertInfo;
        /** modify 145779 add show assertinfo page **/
        Intent notificationIntent = new Intent(this, AssertInfoActivity.class);
        notificationIntent.putExtra("assertInfo", AssertInfo);
        PendingIntent contentIntent =  PendingIntent.getActivity(context, 0, notificationIntent, 0);

        Log.e(MTAG, "Modem Assert!!!!\n");
        Log.e(MTAG, "" + contentText.toString());
        //notification.defaults |= Notification.DEFAULT_VIBRATE;
        long[] vibrate = {0, 10000};
        notification.vibrate = vibrate;
        notification.flags |= Notification.FLAG_NO_CLEAR;// no clear
        /** modify 145779 add show assertinfo page **/
        notification.defaults |= Notification.DEFAULT_SOUND;
        //notification.sound = Uri.parse("file:///sdcard/assert.mp3");

        notification.setLatestEventInfo(context, contentTitle, contentText, contentIntent);
        mNm.notify(MODEM_ASSERT_ID, notification);
    }

    private void hideNotification() {
        Log.v(MTAG, "hideNotification");
        mNm = (NotificationManager)getSystemService(NOTIFICATION_SERVICE);
        mNm.cancel(MODEM_ASSERT_ID);
    }

    private void sendModemStatBroadcast(String modemStat) {
        Log.d(MTAG, "sendModemStatBroadcast.....modemStat : "+modemStat);
        Intent intent = new Intent(MODEM_STAT_CHANGE);
        intent.putExtra(MODEM_STAT, modemStat);
        sendBroadcast(intent);
    }

}
