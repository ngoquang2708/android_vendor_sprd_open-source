package com.spreadtrum.android.eng;

import com.spreadtrum.android.eng.engconstents;
import com.spreadtrum.android.eng.engfetch;
import com.spreadtrum.android.eng.SlogProvider;

import com.android.internal.app.IMediaContainerService;

import android.content.BroadcastReceiver;
import android.content.ContentUris;
import android.content.ContentValues;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager.NameNotFoundException;
import android.database.Cursor;
import android.media.MediaScannerConnection;
import android.net.ConnectivityManager;
import android.net.Uri;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.Looper;
import android.os.Message;
import android.os.StatFs;
import android.os.storage.IMountService;
import android.os.SystemProperties;
import android.util.Log;
import android.widget.CheckBox;
import android.widget.RadioButton;
import android.widget.Toast;

import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;

import org.apache.http.util.EncodingUtils;
//import android.os.storage.StorageVolume;

public class SlogAction {
    // =========================Const=============================================================================
    // The name of package.
    private static final String PACKAGE_NAME = "com.spreadtrum.android.eng";
    // The tag for slog
    private static final String TAG = "SlogAction";
    // slog.conf StorageLocation
    private static final String SLOG_CONF_LOCATION = "/data/local/tmp/slog/slog.conf";
    // The common-control of logs and etc.
    private static final boolean DBG = true;
    // Command to run slog
    private static final String SLOG_COMMAND_START = "slog";
    // A tester to confirm slog is running.
    // private static final String SLOG_COMMAND_QUERY = "slogctl query";
    // Run slog control after set States
    private static final String SLOG_COMMAND_RESTART = "slogctl reload";
    // Command : Clear Log
    private static final String SLOG_COMMAND_CLEAR = "slogctl clear";
    // Command : Export Logs
    private static final String SLOG_COMMAND_DUMP = "slogctl dump ";

    private static final String SLOG_COMMAND_SCREENSHOT = "slogctl screen";

    private static final String SLOG_COMMAND_FETCH = "synchronism download";

    private static final String SLOG_COMMAND_CHECK = "synchronism check";

    private static final String SLOG_COMMAND_UNLINK = "synchronism unlink";
    // Engconfigs location.
    private static final String APPFILES = "/data/data/com.spreadtrum.android.eng/files/";
    // New Feature
    // public static String Options[] = null;

    /** Tags,which used to differ Stream States On->true **/
    public static final String ON = "on";
    /** Tags,which used to differ Stream States Off->false **/
    public static final String OFF = "off";

    /** Log's StorageLocation SDCard->external or NAND->internal **/
    public static final String STORAGEKEY = "logpath\t";
    public static final String STORAGENAND = "internal";
    public static final String STORAGESDCARD = "external";

    /** keyName->General **/
    // TODO:Bad reading method, need improve.
    public static final String GENERALKEY = "\n";
    public static final String GENERALON = "enable";
    public static final String GENERALOFF = "disable";
    public static final String GENERALLOWPOWER = "low_power";

    /** keyName->Options **/
    public static final String KERNELKEY = "stream\tkernel\t";
    public static final String SYSTEMKEY = "stream\tsystem\t";
    public static final String RADIOKEY = "stream\tradio\t";
    public static final String MODEMKEY = "stream\tmodem\t";
    public static final String MUTI_MODEM_0KEY = "stream\tmodem0\t";
    public static final String MUTI_MODEM_1KEY = "stream\tmodem1\t";
    public static final String MAINKEY = "stream\tmain\t";
    public static final String EVENTKEY = "stream\tevents\t";
    public static final String TCPKEY = "stream\ttcp\t";
    public static final String BLUETOOTHKEY = "stream\tbt\t";
    public static final String MISCKEY = "misc\tmisc\t";
    public static final String CLEARLOGAUTOKEY = "var\tslogsaveall\t";

    /** Android keyName **/
    public static final int ANDROIDKEY = 101;
    public static final String SERVICESLOG = "slogsvc.conf";
    public static final String SERVICESNAP = "snapsvc.conf";
    public static final String ANDROIDSPEC = "android";

    private static final String DECODE_ERROR = "decode error";
    // new feature
    // private static final int LENGTHOPTIONSTREAM = 3;

    // public static final int OptionStreamState = 0;
    // public static final int OptionStreamSize = 1;
    // public static final int OptionStreamLevel = 2;

    // public static final String SIZE[] = new
    // String[]{"0","50","100","150","200"};

    // public static int position;
    private static final int SLOG_COMMAND_RETURN_CHECK_FAILED = 255;
    private static final int SLOG_COMMAND_RETURN_FAILED = -1;
    private static final int SLOG_COMMAND_RETURN_OK = 0;
    private static final int SLOG_COMMAND_RETURN_EXCEPTION = -126;
    public static Context contextMainActivity;

    public static final int MESSAGE_START_READING = 11;
    public static final int MESSAGE_END_READING = 12;
    public static final int MESSAGE_START_WRITTING = 13;
    public static final int MESSAGE_END_WRITTING = 14;
    public static final int MESSAGE_START_RUN = 15;
    public static final int MESSAGE_END_RUN = 16;
    public static final int MESSAGE_DUMP_START = 17;
    public static final int MESSAGE_DUMP_STOP = 18;
    public static final int MESSAGE_CLEAR_START = 19;
    public static final int MESSAGE_CLEAR_END = 20;
    public static final int MESSAGE_CLEAR_FAILED = 25;
    public static final int MESSAGE_SNAP_SUCCESSED = 21;
    public static final int MESSAGE_SNAP_FAILED = 22;
    public static final int MESSAGE_DUMP_OUTTIME = 23;
    public static final int MESSAGE_DUMP_FAILED = 24;

    private static Context mContext;

    // Temp Solution.
    private static boolean MMC_SUPPORT = "1".equals(android.os.SystemProperties.get("ro.device.support.mmc"));

    private static Object mLock = new Object();
    private static Object mResetLock = new Object();

    private static HandlerThread sHandlerThread;
    private static Handler sHandler;
    private static HandlerThread sTimeoutThread;
    private static Handler sTimeoutHandler;

    static {
        sHandlerThread = new HandlerThread("SlogActionHandler");
        sHandlerThread.start();
        sHandler = new Handler(sHandlerThread.getLooper());
        sTimeoutThread = new HandlerThread("TimeoutWatchingHandler");
        sTimeoutThread.start();
        sTimeoutHandler = new Handler(sTimeoutThread.getLooper());
    }

    static class TimeoutRunnable extends ToastRunnable {
        TimeoutRunnable(Context context) {
            super(context, context.getString(R.string.toast_snap_timeout));
        }
    }

    static class ToastRunnable implements Runnable {
        Context mContext;
        String mPrompt;
        ToastRunnable(Context context, String prompt) {
            mContext = context;
            mPrompt = prompt;
        }

        @Override
        public void run() {
            Log.d(TAG, "show toast now, the prompt is " + mPrompt);
            Toast.makeText(mContext, mPrompt, Toast.LENGTH_SHORT).show();
        }
    }

    static class ScreenshotRunnable implements Runnable {
        Context mContext;
        TimeoutRunnable mTimeout;
        ScreenshotRunnable(Context context, TimeoutRunnable timeout) {
            mContext = context;
            mTimeout = timeout;
        }

        @Override
        public void run() {
            synchronized (mLock) {
                Log.d(TAG, "start screenshot now");
                screenShot(mContext, mTimeout);
            }
        }
    }

    // ========================================================================================================

    // ==================================GetState=================================================

    /* Old Feature---------------------> */
    /**
     * Reload GetState Function, get states and compared, finally return the
     * result. PS:SDCard isChecked =>true
     **/
    public static boolean GetState(String keyName) {
        // null pointer handler
        if (keyName == null) {
            Log.e("slog",
                    "You have give a null to GetState():boolean,please check");
            return false;
        }

        try {
            if (keyName.equals(GENERALKEY)
                    && GetState(keyName, true).equals(GENERALON)) {
                return true;
            }
            if (keyName.equals(STORAGEKEY)
                    && GetState(keyName, true).equals(STORAGESDCARD)) {
                return true;
            }
            if (keyName.equals(CLEARLOGAUTOKEY)
                    && GetState(keyName, true).equals(ON)) {
                return true;
            } else if (GetState(keyName, false).equals(ON)) {
                return true;
            } else {
                return false;
            }
        } catch (NullPointerException nullPointer) {
            Log.e("GetState",
                    "Maybe you change GetState(),but don't return null.Log:\n"
                    + nullPointer);
            return false;
        }
    }

    /** Reload GetState function, for other condition **/
    public static boolean GetState(int otherCase) {
        // TODO if you have another Conditions, please use it,add the code under
        // switch with case
        // !
        try {
            switch (otherCase) {
            case ANDROIDKEY:
                if (GetState(KERNELKEY, false).equals(ON))
                    return true;
                else if (GetState(SYSTEMKEY, false).equals(ON))
                    return true;
                else if (GetState(RADIOKEY, false).equals(ON))
                    return true;
                else if (GetState(MAINKEY, false).equals(ON))
                    return true;
                else if (GetState(EVENTKEY, false).equals(ON))
                    return true;
                break;
            default:
                Log.e("GetState(int)", "You have given a invalid case");
                break;
            }
        } catch (NullPointerException nullPointer) {
            Log.e("GetState(int)",
                    "Maybe you change GetState(),but don't return null.Log:\n"
                            + nullPointer);
            return false;
        }
        return false;
    }

    /**
     * Finally, we'll run the this GetState. It will return a result(String)
     * which you want to search after "keyName"
     * FIXME: The getState and setState has many problems, may change the
     * function of get and set States.
     **/
    public static synchronized String GetState(String keyName, boolean isLastOption) {
        // recieve all text from slog.conf
        StringBuilder conf = null;
        FileInputStream freader = null;
        //
        char result[] = null;
        try {
            // Create a fileInputStream with file-location
            freader = new FileInputStream(SLOG_CONF_LOCATION);

            // Dim a byte[] to get file we read.
            byte[] buffer = new byte[freader.available()];
            result = new char[freader.available()];
            if (freader.available() < 10) {
                freader.close();
                resetSlogConf();

                return DECODE_ERROR;
            }

            // Begin reading
            try {
                freader.read(buffer);
                // Decoding
                conf = new StringBuilder(EncodingUtils.getString(buffer,
                        "UTF-8"));

            } catch (Exception e) {
                // Although I'm dead, I close it!
                freader.close();
                Log.e(TAG, "Read buffer failed, because " + e.getMessage() + "Now print stack");
                e.printStackTrace();
                return DECODE_ERROR;
            }
            freader.close();
        } catch (java.io.FileNotFoundException fileNotFound) {
            Log.e(TAG ,"File not found, reset slog.conf");
            resetSlogConf();
            return DECODE_ERROR;
        } catch (Exception e) {
            Log.e(TAG, "Failed reading file. Now dump stack");
            e.printStackTrace();
            return DECODE_ERROR;
        }

        if (conf == null || conf.length() < 1) {
            Log.d(TAG, "conf.lenght < 1, return decode_error");
            return DECODE_ERROR;
        }

        try {
            conf.getChars(
                    conf.indexOf(keyName) + keyName.length(), // start cursor
                    conf.indexOf(isLastOption ? "\n" : "\t", conf.indexOf(keyName)
                            + keyName.length() + 1), // ending cursor
                    result, 0);//
        } catch(Exception e) {
            Log.d(TAG, "Catch exception");
            e.printStackTrace();
            return DECODE_ERROR;
        }
        return String.valueOf(result).trim();
    }

    private static synchronized void resetSlogConf() {
        new Thread() {
            @Override
            public void run() {
                synchronized (mResetLock) {
                    runSlogCommand("rm " + SLOG_CONF_LOCATION);
                    runCommand();
                }
            }

        }.start();
    }
    /* <----------------------Old Feature */

    /*
     * New Feature----------------------> public static void GetStates(String
     * keyName){ int maxLength =
     * (keyName.equals(GENERALKEY)||keyName.equals(STORAGEKEY
     * ))?1:LENGTHOPTIONSTREAM; Options = new String[maxLength];
     *
     * StringBuilder conf = null; char result[] = new char[20];
     *
     * try{ FileInputStream freader = new FileInputStream(SLOG_CONF_LOCATION);
     * byte[] buffer = new byte[freader.available()]; freader.read(buffer); conf
     * = new StringBuilder(EncodingUtils.getString(buffer, "UTF-8"));
     * freader.close();
     *
     * } catch (Exception e) {
     * System.err.println("--->   GetState has problems, logs are followed:<---"
     * ); System.err.println(e); return ; } int counter = conf.indexOf(keyName);
     * int jump = keyName.length();
     *
     * for(int i=0 ;i<maxLength;){ conf.getChars( counter+jump,
     * conf.indexOf(i==maxLength-1?"\n":"\t",counter+jump+1), result, 0);
     *
     * //
     * System.out.println("i="+i+" counter="+counter+"jump="+jump+"result="+String
     * .valueOf(result).trim()); Options[i] = String.valueOf(result).trim();
     * counter += jump; jump = Options[i++].length()+1;
     *
     * } } /*<-----------------------New Feature
     */

    // =========================================================================================================GetState

    // ===============================SetState===============================================================

    /***/
    public static void SetState(String keyName, boolean status,
            boolean isLastOption) {
        if (keyName == null) {
            Log.e("SetState(String,boolean,boolean):void",
                    "Do NOT give me null");
            return;
        }
        // load files, and set the option
        if (keyName.equals(GENERALKEY)) {
            if (status) {
                SetState(keyName, GENERALON, true);
            } else {
                SetState(keyName, GENERALOFF, true);
            }
        } else if (keyName.equals(STORAGEKEY)) {
            if (status) {
                SetState(keyName, STORAGESDCARD, true);
            } else {
                SetState(keyName, STORAGENAND, true);
            }
        } else {
            SetState(keyName, status ? ON : OFF, isLastOption);
        }

        // RunThread runit = new RunThread();
        // new Thread(runit).start();

    }

    /** handle other case **/
    public static void SetState(int otherCase, boolean status) {
        // TODO if you have otherCondition, please add the code under switch
        // with case
        switch (otherCase) {
        case ANDROIDKEY:
            Message msg = new Message();
            msg.what = ANDROIDKEY;
            LogSettingSlogUITabHostActivity.mTabHostHandler.sendMessage(msg);
            SetState(SYSTEMKEY, status, false);
            SetState(KERNELKEY, status, false);
            SetState(RADIOKEY, status, false);
            SetState(MAINKEY, status, false);
            SetState(EVENTKEY, status, false);
            break;
        default:
            Log.w("SetState(int,boolean)", "You have given a invalid case");
        }
    }

    public static synchronized void SetState(String keyName, String status,
            boolean isLastOption) {
        final String MethodName = "SetState(String,String,boolean):void";
        if (keyName == null) {
            Log.e(MethodName, "Do NOT give keyName null");
            return;
        }
        if (status == null) {
            Log.e(MethodName, "Do NOT give status null");
            return;
        }
        if (LogSettingSlogUITabHostActivity.mTabHostHandler != null) {
            Message msg = new Message();
            msg.what = MESSAGE_START_RUN;
            LogSettingSlogUITabHostActivity.mTabHostHandler.sendMessage(msg);
        }

        // load files, and set the option
        try {
            StringBuilder conf = null;

            FileInputStream freader = new FileInputStream(SLOG_CONF_LOCATION);
            byte[] buffer = new byte[freader.available()];
            try {// Make sure=============================
                freader.read(buffer);
                freader.close();
            } catch (Exception e) {
                // ==============If dead,close file first.
                freader.close();
                Log.e(MethodName, "Reading file failed,now close,log:\n" + e);
                return;
            }
            conf = new StringBuilder(EncodingUtils.getString(buffer, "UTF-8"));
            int searchCursor = conf.indexOf(keyName);
            if (searchCursor < 0) {
                Log.e(TAG, "start index < 0, reset slog.conf");
                resetSlogConf();
                return;
            }
            int keyNameLength = keyName.length();
            // ==================Judge Complete
            conf.replace(
                    searchCursor + keyNameLength,
                    conf.indexOf(isLastOption ? "\n" : "\t", searchCursor
                            + keyNameLength + 1), status);
            buffer = null;

            FileOutputStream fwriter = new FileOutputStream(SLOG_CONF_LOCATION);

            buffer = conf.toString().getBytes("UTF-8");
            try {
                fwriter.write(buffer);
                fwriter.close();
            } catch (Exception e) {
                fwriter.close();
                Log.e(MethodName, "Writing file failed,now close,log:\n" + e);
                return;
            }
        } catch (java.io.FileNotFoundException fileNotFound){
            Log.e(TAG, "Init FileInputStream failed, reset slog.conf");
            resetSlogConf();
        } catch (Exception e) {

            e.printStackTrace();
            Log.e(MethodName, "Catch Excepton,log:\n" + e);
            return;
        }

        RunThread runCommand = new RunThread();
        Thread runThread = new Thread(null, runCommand, "RunThread");
        runThread.start();
    }

    // ====================================================================================================SetState

    public static void setAllStates(Context context, int id) {
        Cursor c = context.getContentResolver().query(
                ContentUris.withAppendedId(
                    SlogProvider.URI_ID_MODES, id), null, null, null, null);
        if (c.moveToNext()) {
            setAllStatesWithCursor(c);
        }
        c.close();
    }

    public static void setAllStatesWithCursor(Cursor cursor) {

        SetState(GENERALKEY, cursor.getString(
                cursor.getColumnIndex(
                        SlogProvider.Contract.COLUMN_GENERAL)), true);
        SetState(KERNELKEY, (cursor.getInt(
                cursor.getColumnIndex(
                        SlogProvider.Contract.COLUMN_KERNEL)) == 1), false);
        SetState(MAINKEY, (cursor.getInt(
                cursor.getColumnIndex(
                        SlogProvider.Contract.COLUMN_MAIN)) == 1), false);
        SetState(EVENTKEY, (cursor.getInt(
                cursor.getColumnIndex(
                        SlogProvider.Contract.COLUMN_EVENT)) == 1), false);
        SetState(RADIOKEY, (cursor.getInt(
                cursor.getColumnIndex(
                        SlogProvider.Contract.COLUMN_RADIO)) == 1), false);
        SetState(SYSTEMKEY, (cursor.getInt(
                cursor.getColumnIndex(
                        SlogProvider.Contract.COLUMN_SYSTEM)) == 1), false);
        SetState(MODEMKEY, (cursor.getInt(
                cursor.getColumnIndex(
                        SlogProvider.Contract.COLUMN_MODEM)) == 1), false);
        SetState(TCPKEY, (cursor.getInt(
                cursor.getColumnIndex(
                        SlogProvider.Contract.COLUMN_TCP)) == 1), false);
        SetState(BLUETOOTHKEY, (cursor.getInt(
                cursor.getColumnIndex(
                        SlogProvider.Contract.COLUMN_BLUETOOTH)) == 1), false);
        SetState(MISCKEY, (cursor.getInt(
                cursor.getColumnIndex(
                        SlogProvider.Contract.COLUMN_MISC)) == 1), false);
        SetState(CLEARLOGAUTOKEY, (cursor.getInt(
                cursor.getColumnIndex(
                        SlogProvider.Contract.COLUMN_CLEAR_AUTO)) == 1), true);
        SetState(STORAGEKEY, (cursor.getInt(
                cursor.getColumnIndex(
                        SlogProvider.Contract.COLUMN_STORAGE)) == 1), true);

    }

    public static void saveAsNewMode(String name, Context context) {
        ContentValues values = getAllStatesInContentValues();
        values.put(SlogProvider.Contract.COLUMN_MODE, name);
        context.getContentResolver()
                .insert(SlogProvider.URI_MODES, values);
    }

    public static void updateMode(Context context, int id) {
        context.getContentResolver().
                update(ContentUris.withAppendedId(
                        SlogProvider.URI_ID_MODES, id),
                        getAllStatesInContentValues(),
                        null, null);
    }

    public static void deleteMode(Context context, int id) {
        context.getContentResolver().delete(
                ContentUris.withAppendedId(SlogProvider.URI_ID_MODES, id), null, null);
    }

    private static ContentValues getAllStatesInContentValues() {
        ContentValues cv = new ContentValues();

        cv.put(SlogProvider.Contract.COLUMN_GENERAL, GetState(GENERALKEY, true));
        cv.put(SlogProvider.Contract.COLUMN_KERNEL, GetState(KERNELKEY) ? 1:0);
        cv.put(SlogProvider.Contract.COLUMN_MAIN, GetState(MAINKEY) ? 1:0);
        cv.put(SlogProvider.Contract.COLUMN_EVENT, GetState(EVENTKEY) ? 1:0);
        cv.put(SlogProvider.Contract.COLUMN_RADIO, GetState(RADIOKEY) ? 1:0);
        cv.put(SlogProvider.Contract.COLUMN_SYSTEM, GetState(SYSTEMKEY) ? 1:0);
        cv.put(SlogProvider.Contract.COLUMN_MODEM, GetState(MODEMKEY) ? 1:0);
        cv.put(SlogProvider.Contract.COLUMN_TCP, GetState(TCPKEY) ? 1:0);
        cv.put(SlogProvider.Contract.COLUMN_BLUETOOTH, GetState(BLUETOOTHKEY) ? 1:0);
        cv.put(SlogProvider.Contract.COLUMN_CLEAR_AUTO, GetState(CLEARLOGAUTOKEY) ? 1:0);
        cv.put(SlogProvider.Contract.COLUMN_MISC, GetState(MISCKEY) ? 1:0);
        cv.put(SlogProvider.Contract.COLUMN_STORAGE, GetState(STORAGEKEY) ? 1:0);

        return cv;
    }

    // Temp Solution for MMC

    private static final File EXTERNAL_STORAGE_DIRECTORY
            = getDirectory(getMainStoragePath(), "/mnt/sdcard/");

    private static String getExternalStorageState() {
        try {
            IMountService mountService = IMountService.Stub.asInterface(android.os.ServiceManager
                    .getService("mount"));
            return mountService.getVolumeState(EXTERNAL_STORAGE_DIRECTORY
                    .toString());
        } catch (Exception rex) {
            return "removed";
        }
    }

    private static String getMainStoragePath() {
        try {
            switch (Integer.parseInt(System.getenv("SECOND_STORAGE_TYPE"))) {
                case 0:
                    return "EXTERNAL_STORAGE";
                case 1:
                    return "EXTERNAL_STORAGE";
                case 2:
                    return "SECONDARY_STORAGE";
                default:
                    Log.e("SlogUI","Please check \"SECOND_STORAGE_TYPE\" "
                         + "\'S value after parse to int in System.getenv for framework");
                    if (MMC_SUPPORT) {
                        return "SECONDARY_SOTRAGE";
                    }
                    return "EXTERNAL_STORAGE";
            }
        } catch (Exception parseError) {
            Log.e("SlogUI","Parsing SECOND_STORAGE_TYPE crashed.\n" + parseError );
            switch (SystemProperties.getInt("persist.storage.type", -1)) {
                case 0:
                    return "EXTERNAL_STORAGE";
                case 1:
                    return "EXTERNAL_STORAGE";
                case 2:
                    return "SECONDARY_STORAGE";
                default:
                    if (MMC_SUPPORT) {
                        return "SECONDARY_SOTRAGE";
                    }
            }
            return "EXTERNAL_STORAGE";
        }

    }

    private static File getDirectory(String variableName, String defaultPath) {
        String path = System.getenv(variableName);
        return path == null ? new File(defaultPath) : new File(path);
    }
    private static File getExternalStorage() {
        return EXTERNAL_STORAGE_DIRECTORY;
    }
    // Get it from framework ===============================================================================

    /** Make sure that SDCard is mounted **/
    public static boolean IsHaveSDCard() {
        if (android.os.Environment.getExternalStorageState() == null || getExternalStorageState() == null) {
            Log.e("SlogUI.isHaveSDCard():boolean",
                    "Your enviroment has something wrong,please check.\nReason:android.os.Environment.getExternalStorageState()==null");
            return false;
        }
//        if (MMC_SUPPORT) {
//            return getExternalStorageState().equals("mounted");
//        }
        return getExternalStorageState().equals(
                "mounted");
    }

    public static boolean IsSDCardSelected(RadioButton radioButton) {
        if (radioButton == null) {
            Log.e("IsSDCardSelected", "Do NOT    give RadioButton null");
            return false;
        }
        if (IsHaveSDCard()) {
            if (radioButton.isChecked()) {
                return true;
            }
        }
        return false;
    }

    /** Get free space **/
    public static long GetFreeSpace(IMediaContainerService imcs, String storageLocation) {
        // Judge null==========
        final String MethodName = "GetFreeSpace(String):long";
        if (android.os.Environment.getExternalStorageDirectory() == null
                || android.os.Environment.getDataDirectory() == null
                || EXTERNAL_STORAGE_DIRECTORY == null) {
            Log.e(MethodName, "Your environment has problem,please check");
            return 0;
        }
        if (storageLocation == null) {
            Log.e(MethodName, "Do NOT give storageLocation null");
            return 0;
        }
        // ========Judge Complete

        File path;
        long size = 0;
        // give location=========
        if (storageLocation.equals(STORAGESDCARD)) {
            path =  EXTERNAL_STORAGE_DIRECTORY;
            if (path != null) {
                try {
                    size = imcs.getFileSystemStats(path.getPath())[1];
                } catch (Exception e) {
                    if (imcs == null) {
                        Log.e("SlogUI","mediaContainerService is null");
                    } else {
                        Log.e("SlogUI","failed calculateDirectorySize(\""+path.getPath()+"\")");
                    }
                }
            }
        } else {
            path = android.os.Environment.getDataDirectory();
            if (path != null) {
                StatFs statFs = new StatFs(path.getPath());
                long blockSize = statFs.getBlockSize();
                long availableBlocks = statFs.getAvailableBlocks();
                size = availableBlocks * blockSize;
            }
        }

        return size / 1024 / 1024;
    }

    public static void SetCheckBoxBranchState(CheckBox tempCheckBox,
            boolean tempHost, boolean tempBranch) {
        // judge null
        final String MethodName = "SetCheckBoxBranchState(CheckBox,boolean,boolean):void";
        if (tempCheckBox == null) {
            Log.e(MethodName, "Do NOT give checkbox null");
            return;
        }
        // =========judge complete

        if (tempHost) {
            tempCheckBox.setEnabled(tempHost);
            tempCheckBox.setChecked(tempBranch);
        } else {
            tempCheckBox.setEnabled(tempHost);
        }
    }


    /** **/
    public static boolean isAlwaysRun(String keyName) {
        if (keyName == null) {
            return false;

        }
        if (keyName.equals(SERVICESLOG) || keyName.equals(SERVICESNAP)) {

        } else {
            return false;
        }

        byte[] buffer;
        String conf;
        FileInputStream freader = null;
        try {
            freader = new FileInputStream(APPFILES + keyName);

            buffer = new byte[freader.available()];
            freader.read(buffer);
            freader.close();
            conf = new String(EncodingUtils.getString(buffer, "UTF-8"));
            if (conf.trim().equals(String.valueOf(true))) {
                return true;
            }
            return false;

        } catch (Exception e) {
            if(freader!=null){
                try {
                    freader.close();
                } catch(IOException ioException){
                    Log.e("isAlwaysRun(String):boolean","Freader close error"+ioException);
                    return false;
                }
        }
            System.err.println("Maybe it is first Run,now try to create one.\n"+e);
        FileOutputStream fwriter = null;
            try {
                fwriter = contextMainActivity.openFileOutput(
                        keyName, Context.MODE_PRIVATE);
                fwriter.write(String.valueOf(false).toString()
                        .getBytes("UTF-8"));
                fwriter.close();
            } catch (Exception e1) {
                System.err.println("No!! Create file failed, logs followed\n"
                        + e1);
        if(fwriter!=null) {
            try {
                fwriter.close();
            } catch (IOException e2) {
                return false;
           }
        }
                return false;
            }
            System.err
                    .println("--->SetAlways Run failed,logs are followed:<---");
            System.err.println(e);
            return false;
        }
    }

    /** Make SlogService run in foreground all the time **/
    public static void setAlwaysRun(String keyService, boolean isChecked) {
    FileOutputStream fwriter = null;
        try {
            fwriter = contextMainActivity.openFileOutput(
                    keyService, Context.MODE_PRIVATE);
            fwriter.write(String.valueOf(isChecked).toString()
                    .getBytes("UTF-8"));
            fwriter.close();
        } catch (Exception e1) {
            Log.e("SetAlwaysRun", "Write file failed, see logs\n" + e1);
            return;
        } finally {
            if (fwriter != null) {
                try {
                    fwriter.close();
                } catch (IOException e1) {
                    Log.e("SetAlwaysRun(String,boolean):void","try to close fwriter failed"+e1);
                }
            }
        }
    }

    /** Clear Log **/
    public static void ClearLog() {
        Message msg = new Message();
        msg.what = MESSAGE_CLEAR_START;
        LogSettingSlogUITabHostActivity.mTabHostHandler.sendMessage(msg);

        ClearThread clearCommand = new ClearThread();
        Thread clearThread = new Thread(null, clearCommand, "clearThread");
        clearThread.start();

    }

    private static void runClearLog(){
        Message msg = new Message();
        msg.what = MESSAGE_CLEAR_END;
        if (SLOG_COMMAND_RETURN_OK != runSlogCommand(SLOG_COMMAND_CLEAR)) {
            msg.what = MESSAGE_CLEAR_FAILED;
        }
        try {
            LogSettingSlogUITabHostActivity.mTabHostHandler.sendMessage(msg);
        } catch (Exception e) {
            Log.e(TAG, "Clear log failed, sendMessage failed");
        }
    }

    private static int mTryTimes = 0;
    private static final int MAX_TRY_TIMES = 20;

    private static class FetchRunnable implements Runnable {
        private BroadcastReceiver mBroadcastReceiver;
        FetchRunnable (BroadcastReceiver br) {
            mBroadcastReceiver = br;
        }
        @Override
        public void run() {
            synchronized(mLock) {
                /*if (DBG) {
                    Log.i(TAG, "in FetchThread::run() and mTryTimes = " + mTryTimes);
                }*/
                int check = runSlogCommand(SLOG_COMMAND_CHECK);
                if ( SLOG_COMMAND_RETURN_OK == check ) {Log.d(TAG, "check == OK return");return ;}
                int fetch = runSlogCommand(SLOG_COMMAND_FETCH);
                /*if (DBG) {
                    Log.i(TAG, "fetch return = " + fetch);
                }*/
                if (SLOG_COMMAND_RETURN_OK == fetch) {
                    // Log.i(TAG, "fetch ok, unregisterReceiver netReceiver");
                    mContext.unregisterReceiver(mBroadcastReceiver);
                    registBroadcast(mContext, SLOG_COMMAND_FETCH);
                } else {
                    Log.e(TAG, "Failed fetch");
                    mTryTimes++;
                }

            }
        }
    }

    private static BroadcastReceiver mNetReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            //synchronized (mLock) {
            /*Log.i(TAG, "receive net broadcast and mTryTimes=" + mTryTimes);
            Log.i(TAG, "Connectivity action=" + ConnectivityManager.CONNECTIVITY_ACTION);
            Log.i(TAG, "intent.getAction=" + intent.getAction());
            Log.i(TAG, "case1" + String.valueOf(ConnectivityManager.CONNECTIVITY_ACTION.equals(intent.getAction())));
            Log.i(TAG, "case2" + String.valueOf(intent.getBooleanExtra(ConnectivityManager.EXTRA_NO_CONNECTIVITY, false)));*/
            if (intent != null
                && ConnectivityManager.CONNECTIVITY_ACTION.equals(intent.getAction())
                && !intent.getBooleanExtra(ConnectivityManager.EXTRA_NO_CONNECTIVITY, false)) {
                Log.d(TAG, "receive net broadcast, start fetch thread");
                FetchRunnable fr = new FetchRunnable(this);
                Thread ft = new Thread(null, fr, "FetchThread");
                ft.start();

            }
            if (mTryTimes > MAX_TRY_TIMES) {
                Log.d(TAG, "unregisterRecier netReceiver");
                mContext.unregisterReceiver(this);
                //debug
                registBroadcast(mContext, SLOG_COMMAND_FETCH);
                //release
                //mContext = null;
            }
        }
        //}
    };

    private static BroadcastReceiver mInstallReceiver = new BroadcastReceiver() {
        public void onReceive(Context context, Intent intent) {
            // Log.i(TAG, "receive intent and install package=" + intent.getData().getSchemeSpecificPart());
            if (intent != null
                && Intent.ACTION_PACKAGE_ADDED.equals(intent.getAction())
                && "com.android.synchronism".equals(intent.getData().getSchemeSpecificPart()) ) {
                //context.startService(); or context.sendBroadcast();
                if (mContext != null ) {
                    mContext.unregisterReceiver(this);
                }
                mContext = null;
                Thread t = new Thread() {
                    @Override
                    public void run() {
                        SystemProperties.set("persist.sys.synchronism.exist", "1");
                        runSlogCommand("am startservice -n com.android.synchronism/com.android.synchronism.service.CoreService");
                    }
                };
                t.start();
            }
        }
    };

    private static void registBroadcast(Context context, String command) {
        if (command == null) {
            Log.e(TAG, "Failed registBroadcast, command == null");
            return;
        }
        // Log.i(TAG, "In registBroadcast command = " + command);
        if (SLOG_COMMAND_CHECK.equals(command)) {
            try {
                // Log.i(TAG, "create context and registerReceiver");
                mContext = context.createPackageContext(PACKAGE_NAME
                                , Context.CONTEXT_IGNORE_SECURITY);
                IntentFilter filterConn = new IntentFilter();
                filterConn.addAction(ConnectivityManager.CONNECTIVITY_ACTION);
                mContext.registerReceiver(mNetReceiver, filterConn);
            } catch (NameNotFoundException e) {
                Log.e(TAG, "NameNotFoundException " + e);
                return ;
            }

        } else if (SLOG_COMMAND_FETCH.equals(command)) {
            try {
                if (mContext == null) {
                    mContext = context.createPackageContext(PACKAGE_NAME
                                    , Context.CONTEXT_IGNORE_SECURITY);
                }
                IntentFilter filterInstall = new IntentFilter();
                filterInstall.addAction(Intent.ACTION_PACKAGE_ADDED);
                filterInstall.addDataScheme("package");
                mContext.registerReceiver(mInstallReceiver, filterInstall);
            } catch (NameNotFoundException e) {
                Log.e(TAG, "NameNotFoundException " + e);
                return ;
            }
        } else {
            Log.e(TAG, "Failed registBroadcast, unknown command " + command);
        }
    }

    /** Make logs into package. **/
    private static void runDump(String filename) {
        final String NowMethodName = "SlogUIDump";
        Message msg = new Message();
        msg.what = MESSAGE_DUMP_STOP;

        if (filename == null) {
            Log.e(NowMethodName, "Do NOT give me null");
            msg.what = MESSAGE_DUMP_FAILED;
            LogSettingSlogUITabHostActivity.mTabHostHandler.sendMessage(msg);
            return;
        }

        if (SLOG_COMMAND_RETURN_OK != runSlogCommand(SLOG_COMMAND_DUMP + " " +filename)) {
            msg.what = MESSAGE_DUMP_FAILED;
        }
        try {
            LogSettingSlogUITabHostActivity.mTabHostHandler.sendMessage(msg);
        } catch (Exception e) {
            Log.e(TAG, "Failed to send message!");
        }
    }

    public static void dump(String filename){
        if(filename == null){
            Log.e("SlogUIDump()", "Do not give nulll");
            return ;
        }
        Message msg = new Message();
        msg.what = MESSAGE_DUMP_START;
        LogSettingSlogUITabHostActivity.mTabHostHandler.sendMessage(msg);
        DumpThread dumpCommand = new DumpThread(filename);
        Thread runThread = new Thread(null, dumpCommand, "DumpThread");
        runThread.start();
    }

    /**
      * This function can change InputStream to String, if catching
      * exception, I'll return DECODE_ERROR.
      */
    public static String decodeInputStream(InputStream input) {
        if (input == null) {
            Log.e(TAG, DECODE_ERROR + ", InputStream is null.");
            return DECODE_ERROR;
        }
        byte[] buffer = null;
        try {
            buffer = new byte[input.available()];
            input.read(buffer);
            input.close();
        } catch (IOException ioe) {
            try {
                if (input != null) {
                    input.close();
                }
            } catch(Exception e) {
                Log.e(TAG, "Close file failed, \n" + e.toString());
            }
            Log.e(TAG, DECODE_ERROR + ", see log:" + ioe.toString());
            return DECODE_ERROR;
        }

        String result = EncodingUtils.getString(buffer, "UTF-8");
        if (result != null) {
            return result;
        } else {
            Log.e(TAG, DECODE_ERROR + ", result == null.");
            return DECODE_ERROR;
        }
    }

    /**
     * XXX: A time casting method, should not use in main thread.
     */
    public static boolean sendATCommand(int atCommandCode, boolean openLog) {
        /* require set AT Control to modem and this may be FIXME
        * 1. Why write byte can catch IOException?
        * 2. Whether Setting AT Command in main thread can cause ANR or not?
        */

        // Feature changed, remove close action of openLog now.
        synchronized (mLock) {
            if (!openLog) {
                return false;
            }

            ByteArrayOutputStream baos = new ByteArrayOutputStream();
            DataOutputStream dos = new DataOutputStream(baos);
            try {
                dos.writeBytes(String.format("%d,%d,%d", atCommandCode , 1, openLog ? 1 : 0));
            } catch (IOException ioException) {
                Log.e(TAG, "IOException has catched, see logs " + ioException);
                return false;
            }
            /* engfetch em.. is a class! may be a bad coding style */
            engfetch ef = new engfetch();
            int sockid = ef.engopen();
            ef.engwrite(sockid, baos.toByteArray(), baos.toByteArray().length);
            /* Whether engfetch has free function? */

            // Bring from logswitch ...
            byte[] inputBytes = new byte[512];
            int showlen= ef.engread(sockid, inputBytes, 512);
            String result = new String(inputBytes, 0, showlen);

            if ("OK".equals(result)) {
                return true;
            } else if ("Unknown".equals(result)) {
                Log.w("SlogUI","ATCommand has catch a \"Unknow\" command!");
                return false;
            } else {
                return false;
            }
        }
    }

    /**
      * XXX: Please do NOT give this function null.
      * See SLOG_COMMAND_ , after executing commands, return
      * a boolean result , true : success , false : fail.
      * This function *must NOT* run in main thread or
      * broadcast receiver.
      */
    private static int runSlogCommand(String command) {
        if (command == null) {
            Log.e(TAG, "runSlogCommand catch null command.");
            return SLOG_COMMAND_RETURN_EXCEPTION;
        }

        // Security confirm
        /*if (!command.equals(SLOG_COMMAND_RESTART | SLOG_COMMAND_CLEAR
                            | SLOG_COMMAND_DUMP  | SLOG_COMMAND_SCREENSHOT
                            | SLOG_COMMAND_FETCH | SLOG_COMMAND_CHECK
                            | SLOG_COMMAND_UNLINK)) {
            Log.d("duke","Check result");
            return -255;
        }*/
        Log.i(TAG, "runSlogCommand command=" + command);
        try {
            Process proc = Runtime.getRuntime().exec(command);
            proc.waitFor();
            Log.d(TAG, decodeInputStream(proc.getInputStream()));
            // Log.i(TAG, "after proc.waitFor");
            return proc.exitValue();
        } catch (IOException ioException) {
            Log.e(TAG, "Catch IOException.\n" + ioException.toString());
            return SLOG_COMMAND_RETURN_EXCEPTION;
        } catch (InterruptedException interruptException) {
            Log.e(TAG, "Catch InterruptedException.\n" + interruptException.toString());
            return SLOG_COMMAND_RETURN_EXCEPTION;
        } catch (Exception other) {
            Log.e(TAG, "Catch InterruptedException.\n" + other.toString());
            return SLOG_COMMAND_RETURN_EXCEPTION;
        }
    }

    /**
     * When receive BOOT_COMPLETE , we ensure slog and ota
     * surviving.
     */
    public static void SlogStart(Context context) {
        int check = runSlogCommand(SLOG_COMMAND_CHECK);
        int start = runSlogCommand(SLOG_COMMAND_START);
        boolean isDirty = false;
        // TODO:Maybe a double check, or other check method.
        try {
            context.getPackageManager().getPackageInfo(
                            "com.android.synchronism", 0);
        } catch (NameNotFoundException error) {
            Log.e(TAG, "The package was not installed."
                        + "Set is dirty");
            isDirty = true;
        }
        //if (check != null) {
            //Log.i(TAG, "check result is " + check + " (-126:Exception, 0 OK , -1 Failed)");
            if (!isDirty && SLOG_COMMAND_RETURN_OK == check) {
                SystemProperties.set("persist.sys.synchronism.exist", "1");
                if (DBG) {
                    Log.i(TAG, "Check ok, set persist.sys.synchronism.exist = true");
                }
                return;
            } else if (SLOG_COMMAND_RETURN_CHECK_FAILED == check) {
                //Log.i(TAG, "start registBroadcast");
                if (DBG) {
                    Log.i(TAG, "Check failed, set persist.sys.synchronism.exist = false");
                }
                SystemProperties.set("persist.sys.synchronism.exist", "0");
                registBroadcast(context, SLOG_COMMAND_CHECK);
            } else {
                // Command synchronism can't work as expected, disabled com.android.synchronism
                if (DBG) {
                    Log.i(TAG, "Check catch exception, set persist.sys.synchronism = false");
                }
                SystemProperties.set("persist.sys.synchronism", "0");
                Log.e(TAG, "Failed checking. Stop");
                return;
            }
        /*} else {
            Log.e(TAG, "SlogUI check failed.");
        }*/
    }

    /**
     * This function will remove aota.
     */
    public static void SlogStart(boolean kill) {
        if (kill) {
            runSlogCommand(SLOG_COMMAND_UNLINK);
            runSlogCommand(SLOG_COMMAND_START);
        } else {
            Log.w(TAG, "Run me (kill) but doesn't kill aota?");
        }
    }

    public static void SlogStart() {
        // Feature changed, remove effect of slog in C.
        // runSlogCommand(SLOG_COMMAND_START);
    }

    /**
     * This function may be removed in future versions.
     */
    private static void runCommand() {
        Message msg = new Message();
        msg.what = MESSAGE_END_RUN;

        // Make sure that, users can not touch the ui many times
        try {
            Thread.sleep(200);
        } catch (InterruptedException e1) {
            e1.printStackTrace();
        }

        try {
            Runtime runtime = Runtime.getRuntime();
            Process proc = runtime.exec(SLOG_COMMAND_RESTART);
            try {
                if (proc.waitFor() != 0) {
                    System.err.println("Exit value=" + proc.exitValue());
                }
            } catch (InterruptedException e) {
                System.err.println(e);
            }

        } catch (Exception e) {
            Log.e("run", SLOG_COMMAND_RESTART
                    + " has Exception, log followed\n" + e);

            if (LogSettingSlogUITabHostActivity.mTabHostHandler != null) {
                LogSettingSlogUITabHostActivity.mTabHostHandler.sendMessage(msg);
            }
            return;
        }
        if (LogSettingSlogUITabHostActivity.mTabHostHandler != null) {
            LogSettingSlogUITabHostActivity.mTabHostHandler.sendMessage(msg);
        }
        return;

    }

    /**
     * Screenshot
     */
    public static void snap(final Context context) {
        if (context == null) {
            return ;
        }

        TimeoutRunnable tr = new TimeoutRunnable(context);
        ScreenshotRunnable sr = new ScreenshotRunnable(context, tr);
        sHandler.postDelayed(sr, 500);
        sTimeoutHandler.postDelayed(tr, 1500);
        return ;
    }

    private static void screenShot(Context context, TimeoutRunnable timeout) {
        Log.d(TAG, "in screenshot, the handler will post the delayed runnable");
        Log.d(TAG, "the delayed message has been posted");
        try {
            if (runSlogCommand(SLOG_COMMAND_SCREENSHOT) == 0) {
                sTimeoutHandler.removeCallbacks(timeout);
                sHandler.post(new ToastRunnable(context, context.getString(R.string.toast_snap_success)));
            } else {
                sTimeoutHandler.removeCallbacks(timeout);
                sHandler.post(new ToastRunnable(context, context.getString(R.string.toast_snap_failed)));
            }
        } catch (Exception e) {
            Log.e(TAG, "screen shot catch exception", e.getCause());
        }
        File screenpath;
        if (GetState(STORAGEKEY) && IsHaveSDCard()) {
            screenpath = getExternalStorage();
        } else {
            screenpath = android.os.Environment.getDataDirectory();
        }
        scanScreenShotFile(new File(screenpath.getAbsolutePath() + File.separator + "slog"), context);
    }

    private static void scanScreenShotFile(File file, Context context) {
        if (file == null) {
            Log.i(TAG, "scanFailed!");
            return;
        }
        if ("last_log".equals(file.getName())) {
            return;
        }
        if (file.isDirectory()) {
            File[] files = file.listFiles();
            for (File f : files) {
                scanScreenShotFile(f, context);
            }
        }
        if (file.getName().endsWith("jpg")) {
            MediaScannerConnection.scanFile(context,
                    new String[] { file.getAbsolutePath() }, null, null);
        }
    }

    private static class RunThread implements Runnable {
        public void run() {
            runCommand();
        }
    }

    private static class DumpThread implements Runnable {
        String filename;
        public DumpThread(String fname) {
            super();
            filename = fname;
        }
        public void run() {
            if(filename == null){
                Message msg = new Message();
                msg.what = MESSAGE_DUMP_STOP;
                Log.d("SlogUIDumpThreadRun()", "filename==null");
                return;
            }
            runDump(filename);
        }
    }

    private static class ClearThread implements Runnable{
        public void run(){
            runClearLog();
        }
    }

}
