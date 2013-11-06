package com.spreadtrum.android.eng;

import com.spreadtrum.android.eng.R;
import com.spreadtrum.android.eng.EngInstallHelperService;
import static com.spreadtrum.android.eng.SlogUISnapService.ACTION_SCREEN_SHOT;

import com.android.internal.telephony.IccCard;
import com.android.internal.telephony.TelephonyIntents;
import com.android.internal.telephony.TelephonyProperties;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.SystemProperties;
import android.util.Log;
import android.widget.Toast;

public class SlogUIServerReceiver extends BroadcastReceiver {

    private static boolean isCancelSynchronism = false;
    private static Object mLock = new Object();

    @Override
    public void onReceive(Context receiverContext, Intent receivedIntent) {
        if (receivedIntent.getAction().equals(ACTION_SCREEN_SHOT)) {
            // the context need long time life-cycle, so put application context
            SlogAction.snap(receiverContext.getApplicationContext());
            return ;
        }
        /*
         * Feature changed.
         *if ( !"1".equals(android.os.SystemProperties.get("ro.debuggable")) ) {
         *   return ;
        }*/
        if (receivedIntent.getAction().equals(Intent.ACTION_BOOT_COMPLETED)) {
            LogSettingSlogUITabHostActivity.mTabHostHandler.setContext(receiverContext);
            SlogServiceRunnable ssr = new SlogServiceRunnable(receiverContext, receivedIntent);
            Thread slogSvcThread = new Thread(null, ssr, "SlogServicePreload");
            slogSvcThread.start();
        }
        if (receivedIntent.getAction().equals(TelephonyIntents.ACTION_SIM_STATE_CHANGED)) {
            String state = receivedIntent.getStringExtra(IccCard.INTENT_KEY_ICC_STATE);
            int phoneId = receivedIntent.getIntExtra(IccCard.INTENT_KEY_PHONE_ID,0);
            if ("LOADED".equals(state)) {
                boolean enable = "cn".equals(SystemProperties.get("gsm.sim.operator.iso-country" + phoneId, "nil"));
//                Log.e("duke", "Loaded sim"+phoneId + " and iso-contry=" + SystemProperties.get("gsm.sim.operator.iso-country" + phoneId, "cn"));
                synchronized (mLock) {
                    PackageSettingRunnable psr = new PackageSettingRunnable(receiverContext, enable);
                    if (!isCancelSynchronism) {
                        Thread ensureSyncThread = new Thread(null, psr, "EnsuringSyncThread");
                        ensureSyncThread.start();
                    }
                    if (enable) {
                        isCancelSynchronism = true;
                    }
                }

            }
        }

    }

    private class SlogServiceRunnable implements Runnable {
        Context receiverContext;
        Intent receivedIntent;
        SlogServiceRunnable(Context tmpReceiverContext, Intent tmpReceivedIntent) {
            receiverContext = tmpReceiverContext;
            receivedIntent = tmpReceivedIntent;
        }
        public void run() {
            runSlogService();
        }

        private void runSlogService() {
            if (receivedIntent.getAction().equals(Intent.ACTION_BOOT_COMPLETED)) {
                if (receiverContext == null) {
                    return;
                }
                SlogAction.contextMainActivity = receiverContext;
                if (SlogAction.isAlwaysRun(SlogAction.SERVICESLOG)) {
                    // start service
                    if (receiverContext != null && receiverContext.startService(new Intent(receiverContext,
                           SlogService.class)) == null) {
                       /*Toast.makeText(
                            receiverContext,
                            receiverContext
                                    .getText(R.string.toast_receiver_failed),
                            Toast.LENGTH_LONG).show();*/
                    Log.e("Slog->ServerReceiver",
                            "Start service when BOOT_COMPLETE failed");
                    }
                }
                // start snap service
                if (SlogAction.isAlwaysRun(SlogAction.SERVICESNAP) 
                        && SlogAction.GetState(SlogAction.GENERALKEY, true)
                                .equals(SlogAction.GENERALON)) {
                    if (receiverContext != null && receiverContext.startService(new Intent(receiverContext,
                        SlogUISnapService.class)) == null) {
                        /*Toast.makeText(
                            receiverContext,
                            receiverContext
                                    .getText(R.string.toast_receiver_failed),
                            Toast.LENGTH_LONG).show();*/
                        Log.e("Slog->ServerReceiver",
                            "Start service when BOOT_COMPLETE failed");
                    }
                }
            }

            // Send AT Command to control switch of modem log.
            if (SlogAction.GetState(SlogAction.GENERALKEY)) {
                SlogAction.sendATCommand(engconstents.ENG_AT_SETARMLOG, SlogAction.GetState(SlogAction.MODEMKEY));
                SlogAction.sendATCommand(engconstents.ENG_AT_SETCAPLOG, SlogAction.GetState(SlogAction.TCPKEY));
            }

            if (SystemProperties.getBoolean("persist.sys.synchronism.support", false)) { // TODO Here will be a macro control aota checking and running.
                receiverContext.startService(new Intent(receiverContext, EngInstallHelperService.class));
                SlogAction.SlogStart(receiverContext);
            } else { // Normal start.
                SlogAction.SlogStart();
            }

        }
    }

    private class PackageSettingRunnable implements Runnable {
        Context receiveContext;
        boolean isEnabled;
        final String packageName = "com.android.synchronism";
        // init context and enable state
        PackageSettingRunnable(Context context, boolean enable) {
            receiveContext = context;
            isEnabled = enable;
        }

        @Override
        public void run() {
            try {
                int enable = isEnabled ? PackageManager.COMPONENT_ENABLED_STATE_ENABLED : PackageManager.COMPONENT_ENABLED_STATE_DISABLED;
                SystemProperties.set("persist.sys.synchronism.enable" , isEnabled ? "1" : "0");
                receiveContext.getPackageManager().setApplicationEnabledSetting(packageName, enable, 0);
            } catch (IllegalArgumentException iae) {
                Log.e("Synchronism", "Package " + packageName + " not exist." + iae);
            }
        }
    }
}
