
package com.spreadtrum.android.eng;

import android.os.Debug;

import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.io.IOException;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Debug;
import android.os.SystemProperties;
import android.preference.PreferenceManager;
import android.util.Log;
import android.widget.Toast;

public class AutoAnswerReceiver extends BroadcastReceiver {
    private final String TAG = "AutoAnswerReceiver";
    private static final boolean DEBUG=Debug.isDebug();

    public static final String PREFS_NAME = "ENGINEERINGMODEL";

    private int mSocketID = 0;
    private engfetch mEf;
    private String mATline;
    private String mATResponse;

    @Override
    public void onReceive(Context context, Intent intent) {
        if (intent.getAction().equals(Intent.ACTION_BOOT_COMPLETED)) {
            SharedPreferences settings = context.getSharedPreferences(PREFS_NAME, 0);
            boolean is_answer = settings.getBoolean("autoanswer_call", false);

            if(DEBUG) Log.d(TAG, "start AutoAnswerService being" + is_answer);

            if (is_answer) {
                if(DEBUG) Log.d(TAG, "start AutoAnswerService");
                context.startService(new Intent(context, AutoAnswerService.class));
            }
            // add by wangxiaobin 11-9 for cmmb set begin
            SharedPreferences defaultSettings = PreferenceManager
                    .getDefaultSharedPreferences(context);
            boolean testIsOn = defaultSettings.getBoolean(CMMBSettings.TEST_MODE, false);
            boolean wireTestIsOn = defaultSettings.getBoolean(CMMBSettings.WIRE_TEST_MODE, false);
            if (testIsOn) {
                SystemProperties.set("ro.hisense.cmcc.test", "1");
            } else {
                SystemProperties.set("ro.hisense.cmcc.test", "0");
            }
            if (wireTestIsOn) {
                SystemProperties.set("ro.hisense.cmcc.test.cmmb.wire", "1");
            } else {
                SystemProperties.set("ro.hisense.cmcc.test.cmmb.wire", "0");
            }
            // add by wangxiaobin 11-9 cmmb set end
            /* Modify 20130311 Spreadst of 127737 start the slog when boot start */
            String mode = SystemProperties.get("ro.product.hardware");
            if(mode!=null) {// && mode.contains("77")){
                atSlog(context);
            }
            /* Modify 20130311 Spreadst of 127737 start the slog when boot end */
        }
        // 2013/4/23@spreast for bug138559 start
        if (intent.getAction().equals("com.android.modemassert.MODEM_STAT_CHANGE")) {
            if(DEBUG) Log.d(TAG, "modem state changed:" + intent.getExtra("modem_stat"));
            if (intent.getExtra("modem_stat").equals("modem_alive")) {
                String mode = SystemProperties.get("ro.product.hardware");
                if (mode != null ) {//&& mode.contains("77")) {
                    while(!atSlog(context)){
                        try {
                            Thread.sleep(1000);
                        } catch (InterruptedException e) {
                            e.printStackTrace();
                        }
                    }
                }
            }
        }
        // 2013/4/23@spreast for bug138559 end
    }

    private boolean atSlog(Context context) {
        mEf = new engfetch();
        mSocketID = mEf.engopen();
        mATline = new String();
        String re = SystemProperties.get("persist.sys.modem_slog");
        if(re.isEmpty()&&SystemProperties.get("ro.build.type").equalsIgnoreCase("user")){
            re="0";
        }else if(re.isEmpty()&&SystemProperties.get("ro.build.type").equalsIgnoreCase("userdebug")) {
            re="1";
        }
        int state = Integer.parseInt(re);

        ByteArrayOutputStream outputBuffer = new ByteArrayOutputStream();
        DataOutputStream outputBufferStream = new DataOutputStream(outputBuffer);
        /*Modify 20130527 spreadst of 166285:close the modem log in user-version start*/
        if(DEBUG) Log.d(TAG, "Engmode socket open, id:" + mSocketID);

        if (state == 1) {
            SystemProperties.set("persist.sys.modem_slog", "1");
            mATline = String.format("%d,%d,%s", engconstents.ENG_AT_NOHANDLE_CMD, 1, "AT+SLOG=2");
            if(DEBUG) Log.d(TAG, "set persist.sys.modem_slog 1");
        } else {
            SystemProperties.set("persist.sys.modem_slog", "0");
            mATline = String.format("%d,%d,%s", engconstents.ENG_AT_NOHANDLE_CMD, 1, "AT+SLOG=3");
            if(DEBUG) Log.d(TAG, "set persist.sys.modem_slog 0");
        }
        /*Modify 20130527 spreadst of 166285:close the modem log in user-version end*/
        try {
            outputBufferStream.writeBytes(mATline);
        } catch (IOException e) {
            Log.e(TAG, "writeBytes() error!");
            return false;
        }
        mEf.engwrite(mSocketID, outputBuffer.toByteArray(), outputBuffer.toByteArray().length);

        int dataSize = 128;
        byte[] inputBytes = new byte[dataSize];

        int showlen = mEf.engread(mSocketID, inputBytes, dataSize);
        mATResponse = new String(inputBytes, 0, showlen);

        if(DEBUG) Log.d(TAG, "AT response:" + mATResponse);
        if (mATResponse.contains("OK")) {
            return true;
        }
        /* Modify 20130311 Spreadst of 127737 start the slog when boot start */
//        if (mATResponse.contains("OK")) {
//            Toast.makeText(context, "Success!", Toast.LENGTH_SHORT).show();
//        } else {
//            Toast.makeText(context, "Fail!", Toast.LENGTH_SHORT).show();
//        }
        mEf.engclose(mSocketID);
        return false;
        /* Modify 20130311 Spreadst of 127737 start the slog when boot end  */
    }
}
