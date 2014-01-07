/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
 
package com.sprd.APCT;

import com.sprd.APCT.APCTService;
import com.sprd.APCT.APCTSettings;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.util.Log;
import java.io.FileWriter;
import java.io.FileReader;
import java.io.IOException;
import java.lang.Float;
import android.os.SystemClock;
import android.telephony.ServiceState;
import com.android.internal.telephony.TelephonyIntents;
import android.provider.Settings;
import android.content.ContentResolver;
import android.app.Activity;
import android.content.SharedPreferences;
import com.android.internal.telephony.Phone;
import android.telephony.TelephonyManager;

public class APCTReceiver extends BroadcastReceiver
{
    static final String NET_ACTION     = "android.intent.action.SERVICE_STATE";
    static final String TAG = "APCTReceiver";
    boolean[] first_flg;
    long[] sim_time;
    int sim_count;

    public APCTReceiver() {
        sim_count = TelephonyManager.getPhoneCount();
        Log.d("APCT_DEBUG", "APCTReceiver sim_count = " + sim_count);
        sim_time = new long[sim_count << 1];
        first_flg = new boolean[sim_count];
        for (int i = 0; i < sim_count; i++)
        {
            first_flg[i] = true;
        }
    }
    public boolean IsFloatWinShow(Context context)
    {
        boolean ret  = false;
        boolean ret1 = false;
        boolean ret2 = false;

        SharedPreferences sp = context.getSharedPreferences("checkbox", context.MODE_PRIVATE);

        if (  sp.getBoolean("APP_LAUNCH_TIME", false) || sp.getBoolean("FPS_DISPLAY",false) || sp.getBoolean("MEMINFO_DISPLAY",false)
           || sp.getBoolean("BOOT_TIME",false) || sp.getBoolean("CAM_INIT_TIME",false) || sp.getBoolean("NET_TIME",false)
           || sp.getBoolean("APP_DATA",false) || sp.getBoolean("BOOT_DATA",false) || sp.getBoolean("PWR_OFF_TIME",false)
           || sp.getBoolean("PWR_CHARGE_TIME",false) || sp.getBoolean("HOME_IDLE_TIME",false) || sp.getBoolean("TOP_RECORDER",false)
           || sp.getBoolean("PROCRANK_RECORDER",false))
        {
            ret1 = true;
        }

        if (  APCTSettings.isApctAppTimeSupport() || APCTSettings.isApctFpsSupport() || APCTSettings.isApctBootTimeSupport() 
           || APCTSettings.isApctCameraInitSupport() || APCTSettings.isApctNetSearchSupport() || APCTSettings.isApctAppDataSupport()
           || APCTSettings.isApctBootDataSupport() || APCTSettings.isApctPowerOffSupport() || APCTSettings.isApctChargeSupport()
           || APCTSettings.isApctHome2IdleSupport() || APCTSettings.isApctMemInfoSupport() || APCTSettings.isApctTopSupport()
           || APCTSettings.isApctProcrankSupport())
        {
            ret2 = true;
        }
       ret = ret1 && ret2;
       return ret;
    }

    public void onReceive(Context context, Intent intent)
    {
        if (NET_ACTION.equals(intent.getAction()))
        {
            int sim_count = TelephonyManager.getPhoneCount();
            int phoneId = intent.getIntExtra(Phone.PHONE_ID, 0);
            ServiceState serviceState = ServiceState.newFromBundle(intent.getExtras());

            if (first_flg[phoneId] && serviceState.getState() == ServiceState.STATE_POWER_OFF)
            {
                sim_time[phoneId<<1] = SystemClock.elapsedRealtime();
            }
            else
            if (first_flg[phoneId] && serviceState.getState() == ServiceState.STATE_IN_SERVICE)
            {
                sim_time[phoneId<<1 + 1] = SystemClock.elapsedRealtime();
                first_flg[phoneId] = false;
                writeNetTimeToProc();
            }
        }
    }

    public final void writeNetTimeToProc()
    {
        String net_str = "Net Search Time: ";

        for (int i = 0; i < sim_count; i++)
        {
            long net_time = sim_time[i<<1 + 1] - sim_time[i<<1];
            Long net_time2 = new Long(net_time);
            net_str += Long.toString(net_time2);
            net_str += " ";
        }

        net_str += " ms";
        char[] buffer = net_str.toCharArray();
        final String NET_TIME_PROC = "/proc/benchMark/net_time";

        FileWriter fr = null;
        try
        {
            fr = new FileWriter(NET_TIME_PROC, false);

            if (fr != null)
            {
                fr.write(buffer);
                fr.close();
            }
        }
        catch (IOException e)
        {
            Log.d(TAG, "+++APCT Cannot write /proc/benchMark/net_time");
            e.printStackTrace();
        }
    }
}
