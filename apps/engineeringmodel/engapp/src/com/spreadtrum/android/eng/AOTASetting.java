package com.spreadtrum.android.eng;

import android.content.Context;
import android.preference.CheckBoxPreference;
import android.preference.Preference;
import android.preference.PreferenceActivity;
import android.preference.PreferenceScreen;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import android.os.Bundle;
import android.os.SystemProperties;
import android.util.Log;
import android.widget.Toast;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.PrintStream;

public class AOTASetting extends PreferenceActivity {
    private static final String TAG = "duke";
    private CheckBoxPreference mEnable;
    private CheckBoxPreference mPreload;
//    private CheckBoxPreference mSilent;
    private CheckBoxPreference mUser;
    private Preference mClear;
//    private Preference mInstall;
//    private Preference mRemove;
    private Context mContext = this;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        addPreferencesFromResource(R.layout.aota_setting);
        mEnable = (CheckBoxPreference) findPreference("aota_support");
        mPreload = (CheckBoxPreference) findPreference("authorized_preload");
//        mSilent = (CheckBoxPreference) findPreference("aota_silent");
        mClear = (Preference) findPreference("aota_clear");
        mUser = (CheckBoxPreference) findPreference("aota_user_enable");
        //mInstall = (Preference) findPreference("aota_install");
        //mRemove = (Preference) findPreference("aota_remove");

    }

    private void runCommand(final int command,final boolean isEnable) {
        final String option = isEnable ? " enable " : " disable ";
        Log.e(TAG, "isEnable = " + option + "command = " + command);
        Thread t = new Thread() {
            public void run() {
                try {
                    if (command == 0) {
                        Log.e(TAG, "pm enable/disable");
                        Runtime.getRuntime().exec("pm" + option + "com.android.synchronism");
                    }
                    if (command == 1) {
                        Log.e(TAG, "rm command");
                        Process p1 = Runtime.getRuntime().exec("rm /data/preloadapp/Synchronism.apk");
                        p1.waitFor();
                        Process p2 = Runtime.getRuntime().exec("rm /data/dalvik-cache/data@preloadapp@Synchronism.apk@classes.dex");
                    }
                } catch (Exception e) {
                    android.util.Log.e(TAG, "run command crashed " + e);
                }
            }
        };
        t.start();
    }

    @Override
    public boolean onPreferenceTreeClick(PreferenceScreen preferenceScreen, Preference preference) {
        //LocalSocket clientSocket = new LocalSocket();
        if (preference == mUser) {
            SystemProperties.set("persist.sys.synchronism.enable", mUser.isChecked() ? "1" : "0");  
            boolean isAOTAEnable = SystemProperties.getBoolean("persist.sys.synchronism.enable", false);
            Log.e(TAG, "isAOTAEnable = " + String.valueOf(isAOTAEnable));
            runCommand(0, mUser.isChecked());
            return true;
        } else if (preference == mClear) {
            Log.e(TAG, "mClear == preference");
            runCommand(1, false);
            return true;
        } else if (preference == mEnable) {
            Log.e(TAG, "mEnable == preference");
            SystemProperties.set("persist.sys.synchronism.support", mEnable.isChecked() ? "1" : "0");
            return true;
        } else if (preference == mPreload) {
            Log.e(TAG, "mPreload");
            SystemProperties.set("persist.sys.authorized.preload", mPreload.isChecked() ? "1" : "0");
            return true;
/*        } else if (preference == mSilent) {
            Log.e(TAG, "mSilent");
            SystemProperties.set("persist.sys.silent.install", mSilent.isChecked() ? "1" : "0");
            return true;*/
/*        } else if (preference == mInstall) {
            try {
                clientSocket.connect(new LocalSocketAddress("aotad"));
                PrintStream out = new PrintStream(clientSocket.getOutputStream());
                BufferedReader buf = new BufferedReader(new InputStreamReader(clientSocket.getInputStream()));
                out.println("10/data/local/tmp/test.apk");
                //out.println("11com.noshufou.android.su");
                Toast.makeText(mContext, "Install Result code is" + buf.readLine(), Toast.LENGTH_SHORT).show();
                buf.close();
                out.close();
                clientSocket.close();
            } catch (IOException e) {
                // TODO Auto-generated catch block
                e.printStackTrace();
            }
            return true;
        } else if (preference == mRemove) {
            try {
                clientSocket.connect(new LocalSocketAddress("aotad"));
                PrintStream out = new PrintStream(clientSocket.getOutputStream());
                BufferedReader buf = new BufferedReader(new InputStreamReader(clientSocket.getInputStream()));
                //out.println("10/data/local/tmp/test.apk");
                out.println("11com.noshufou.android.su");
                Toast.makeText(mContext, "Remove Result code is" + buf.readLine(), Toast.LENGTH_SHORT).show();
                buf.close();
                out.close();
                clientSocket.close();
            } catch (IOException e) {
                // TODO Auto-generated catch block
                e.printStackTrace();
            }
            return true;*/
        } else {
            return false;
        }
    }
}
