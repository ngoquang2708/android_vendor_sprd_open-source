package com.spreadtrum.android.eng;

/*
 * TODO: Using AIDL instead of LocalSocketServer
 */

import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.IPackageInstallObserver;
import android.content.pm.IPackageDeleteObserver;
import android.net.LocalServerSocket;
import android.net.LocalSocket;
import android.net.Uri;
import android.os.IBinder;
import android.os.SystemProperties;
import android.util.Log;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.PrintStream;

public class EngInstallHelperService extends Service {
    private static final String TAG = "EngInstallHelperService";
    private static final boolean DBG = true;

    private static final String AOTA_SOCKET_NAME = "aotad";
    private static final String KEY_AOTA_ALLOW = "persist.sys.synchronism.support";
    private static final String KEY_AOTA_ENBALE = "persist.sys.synchronism.enable";
    private static final String KEY_AOTA_SILENT = "persist.sys.silent.install";

    private static final boolean AOTA_ALLOW = SystemProperties.getBoolean(KEY_AOTA_ALLOW, DBG);
    private static final boolean AOTA_ENABLE = SystemProperties.getBoolean(KEY_AOTA_ENBALE, DBG);
    private static final boolean AOTA_SILENT = SystemProperties.getBoolean(KEY_AOTA_SILENT, DBG);;
    private static final String AOTA_INSTALL = "pm install -r ";
    private static final String AOTA_UPDATE = "synchronism download";

    private static final String REPLY_BAD_MESSAGE = "b";
    private static final String REPLY_EXCEPTION = "e";

    private LocalServerSocket mSocketServer = null;
    private Context mContext = this;
    private static Object mLock = new Object();

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public void onCreate() {
        super.onCreate();
        if (!AOTA_ENABLE) {
            return ;
        }
        try {
            mSocketServer = new LocalServerSocket(AOTA_SOCKET_NAME);
            if (DBG) {
                Log.d(TAG, "start local socket successful");
            }
        } catch (IOException ioException) {
            mSocketServer = null;
            Log.e(TAG, "failed start socket server\n " + ioException);
        }
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (AOTA_ENABLE) {
            startAotaSocketServer();
        } else {
            Log.i(TAG, "AOTA is not enable, exit");
            stopSelf();
        }
        return super.onStartCommand(intent,flags,startId);
    }

    private void startAotaSocketServer() {
        if (mSocketServer == null) {
            Log.e(TAG, "socket server haven't init, exit");
            stopSelf();
            return ;
        }
        Log.i(TAG, "now starting aotaserver");
        Thread otaThread = new Thread(null, new AotaSocketServerRunnable(), "AirpushDeamon");
        otaThread.start();
    }

    /** 
     * 
     */
    private class AotaSocketServerRunnable implements Runnable {
        class LocalSocketRunnable implements Runnable {
            LocalSocket client = null;
            LocalSocketRunnable(LocalSocket ls) {
                client = ls;
            }

            class PackageInstallObserver extends IPackageInstallObserver.Stub {
                PrintStream mOut;
                BufferedReader mBuf;
                PackageInstallObserver(PrintStream out, BufferedReader buf) {
                    mOut = out;
                    mBuf = buf;
                }
                public void packageInstalled(String packageName, int returnCode) {
                    writeMessage(mOut, mBuf, String.valueOf(returnCode));
                }
            };

            class PackageDeleteObserver extends IPackageDeleteObserver.Stub {
                PrintStream mOut;
                BufferedReader mBuf;
                PackageDeleteObserver(PrintStream out, BufferedReader buf) {
                    mOut = out;
                    mBuf = buf;
                }
                public void packageDeleted(String packageName, int returnCode) {
                    writeMessage(mOut, mBuf, String.valueOf(returnCode));
                }
            };

            boolean writeMessage(PrintStream out,BufferedReader buf, String message) {
                if (out != null && buf != null && message != null) {
                    Log.d(TAG, "message=" + message);
                    try {
                        out.println(message);
                        out.close();
                        buf.close();
                        client.close();
                        mOperationCode = 255;
                    } catch (Exception ioe) {
                        out.close();

                        Log.e(TAG, "writeMessage catch IOException\n" + ioe);
                        try {
                            out.close();
                            buf.close();
                            client.close();
                        } catch (Exception e) {
                            Log.e(TAG, "close socket failed" + e);
                        }
                        return false;
                    }
                }
                return true;
            }
            
            String readMessage(BufferedReader buf) {
                String message = null;
                if (buf != null) {
                    try {
                        message = buf.readLine();
                        //buf.close();
                    } catch (IOException ioe) {
                        try {
                            buf.close();
                        } catch (Exception e) {
                            Log.e(TAG, "close failed:120");
                        }
                        return REPLY_EXCEPTION;
                    }
                } else {
                    Log.e(TAG, "buf == null");
                    return REPLY_EXCEPTION;
                }
                return message;
            }
            
            @Override
            public void run() {
                synchronized(mLock) {
                    if (client == null) {
                        return ;
                    }
                    PrintStream out = null;
                    BufferedReader buf = null;
                    try {
                        out = new PrintStream(client.getOutputStream());
                        buf = new BufferedReader(new InputStreamReader(client.getInputStream()));
                    } catch (Exception error) {
                        Log.e(TAG, "PrintStream or BufferedRead init failed\n" + error.toString());
                        try {
                            client.close();
                        } catch (Exception e) {
                            Log.e(TAG, "close failed");
                        }
                        return ;
                    }
                    StringBuilder message = new StringBuilder(readMessage(buf));
                //try {
                    // If catch a invalid command, return an error code.
                    if (message == null || message.length() < 3) {
                        writeMessage(out, buf, message.toString());
                        return ;
                    }
                    //int exec = -127;
                    String execResult = null;
                    Log.e(TAG, "get message" + message.toString());
                    char[] ex = new char[message.toString().length()];
                    message.getChars(0,2,ex,0);
                    if ("1".equals(String.valueOf(ex[0]))) {
                        String trimedMessage = message.delete(0,2).toString().trim();
                        Log.i(TAG, "message trimed =" + trimedMessage);
                        boolean silentMode = SystemProperties.getBoolean(KEY_AOTA_SILENT, DBG);
                        Intent intent = null;
                        if ("0".equals(String.valueOf(ex[1]))) {
                            if (silentMode) { // TODO: Delete this if in future. No silent mode here.
                                mContext.getPackageManager()
                                        .installPackage(Uri.parse(trimedMessage),
                                                new PackageInstallObserver(out, buf),
                                                0 | PackageManager.INSTALL_REPLACE_EXISTING,
                                                "com.spreadtrum.android.eng");
                            } else {
                                intent = new Intent(mContext, EngInstallActivity.class);
                                intent.putExtra("name", trimedMessage);
                                intent.putExtra("action", EngInstallActivity.REQUEST_INSTALL);
                                intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_EXCLUDE_FROM_RECENTS);
                                mContext.getApplicationContext().startActivity(intent);
                                try {
                                    mLock.wait();
                                } catch (Exception e) {
                                    Log.e(TAG, "mLock failed wait " + e);
                                }
                                writeMessage(out, buf, String.valueOf(mOperationCode));
                            }
                        } else if ("1".equals(String.valueOf(ex[1]))) {
                            if (silentMode) { // TODO: Delete this if in future. No silent mode here.
                                mContext.getPackageManager()
                                        .deletePackage(trimedMessage,
                                                new PackageDeleteObserver(out, buf),
                                                0);
                            } else {
                                intent = new Intent(mContext, EngInstallActivity.class);
                                intent.putExtra("name", trimedMessage);
                                intent.putExtra("action", EngInstallActivity.REQUEST_DELETE);
                                intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_EXCLUDE_FROM_RECENTS);
                                mContext.getApplicationContext().startActivity(intent);
                                try {
                                    mLock.wait();
                                } catch (Exception e) {
                                    Log.e(TAG, "mLock failed wait " + e);
                                }
                                writeMessage(out, buf, String.valueOf(mOperationCode));
                            }
                        }
                    } else if ("2".equals(String.valueOf(ex[0]))) {
                          try {
                                Process proc = Runtime.getRuntime().exec(AOTA_UPDATE);
                                proc.waitFor();
                                execResult = SlogAction.decodeInputStream(proc.getInputStream());

                            } catch (Exception error) {

                                Log.e(TAG, "exec catch excpetion" + error);
                            }
                                writeMessage(out, buf, execResult == null ? REPLY_EXCEPTION: execResult);
                    } else {
                        writeMessage(out, buf, REPLY_BAD_MESSAGE);
                    }

                }
            }
            
        };

        @Override
        public void run() {
            while (true) {
                LocalSocket ls = null;
                try {
                    ls = mSocketServer.accept();
                } catch (IOException ioe) {
                    Log.e(TAG, "mSocketServer has catch a IOException\n"+ ioe);
                    continue;
                }
                Thread workingThread = new Thread(null, new LocalSocketRunnable(ls), "AirpushWorkingThread");
                workingThread.start();
            }
        }
    }

    // User install
    private static int mOperationCode = 255;
    public static void onResult (int returnCode) {
        synchronized (mLock) {
            mOperationCode = returnCode;
            mLock.notify();
        }
    }
    
}
