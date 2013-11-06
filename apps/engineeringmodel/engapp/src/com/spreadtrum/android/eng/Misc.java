
package com.spreadtrum.android.eng;


import android.app.AlertDialog;
import android.app.Dialog;
import android.app.ProgressDialog;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.Bundle;
import android.os.Debug;
import android.os.Handler;
import android.os.Message;
import android.os.SystemProperties;
import android.preference.CheckBoxPreference;
import android.preference.Preference;
import android.preference.PreferenceActivity;
import android.preference.PreferenceScreen;
import android.util.Log;
import android.widget.Toast;
public class Misc extends PreferenceActivity {
    private static final boolean DEBUG = Debug.isDebug();
	private static final String TAG = "Misc";
    private engfetch mEf;
    private static final int MEM_STOP = 1;

    private Dialog dialog;
    private CheckBoxPreference smsFillMemoryPref ;
    private CheckBoxPreference mmsFillMemoryPref ;
    private CheckBoxPreference showMMSReplyChoicePref ;

    BroadcastReceiver receiver = new BroadcastReceiver() {

        @Override
        public void onReceive(Context context, Intent intent) {
        	if(DEBUG) Log.d(TAG, "receiver : device storage lower !");
            mHandler.removeMessages(1);
            mHandler.sendEmptyMessage(1);
        }
    };

    private Handler mHandler = new Handler(){

        @Override
        public void handleMessage(Message msg) {
            if(dialog != null){
                dialog.dismiss();
                Toast.makeText(Misc.this, R.string.memory_low,Toast.LENGTH_SHORT).show();
            }
        }

    };
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        addPreferencesFromResource(R.layout.prefs_misc);
        smsFillMemoryPref = (CheckBoxPreference)findPreference("sms_fill_memory");
        mmsFillMemoryPref = (CheckBoxPreference)findPreference("mms_fill_memory");
        showMMSReplyChoicePref = (CheckBoxPreference)findPreference("show_mms_reply_choice");
        boolean status= SystemProperties.getBoolean("persist.sys.mms.showreplypath", false);
        if(status){
            Log.d(TAG, "show showMMSReplyChoicePref");
            showMMSReplyChoicePref.setChecked(true);
            showMMSReplyChoicePref.setSummary("Show MMS Reply Choice");
        }else{
            Log.d(TAG, "Don't show showMMSReplyChoicePref");
            showMMSReplyChoicePref.setChecked(false);
            showMMSReplyChoicePref.setSummary("NO MMS Reply Choice");
        }
    }

    @Override
    protected void onStart() {
        IntentFilter filter = new IntentFilter(Intent.ACTION_DEVICE_STORAGE_LOW);
        registerReceiver(receiver, filter);
        super.onStart();
    }

    @Override
    protected void onResume() {
        if(smsFillMemoryPref.isChecked()){
            mmsFillMemoryPref.setEnabled(false);
        }

        if(mmsFillMemoryPref.isChecked()){
            smsFillMemoryPref.setEnabled(false);
        }
        super.onResume();
    }

    @Override
    public boolean onPreferenceTreeClick(PreferenceScreen preferenceScreen, Preference preference) {
        String key = preference.getKey();
        if ("sms_fill_memory".equals(key)) {
	    if(preference instanceof CheckBoxPreference){
                CheckBoxPreference checkbox = (CheckBoxPreference) preference;
                if (checkbox.isChecked()) {
                    writeCmd("SMS MEM START");
                    mmsFillMemoryPref.setEnabled(false);
                    dialog = ProgressDialog.show(this, "", getString(R.string.fill_memory_alert_msg));
                    preference.setSummary("uncheck to delete memory");
                } else {
                    writeCmd("MEM STOP");
                    showDialog(MEM_STOP);
                    mmsFillMemoryPref.setEnabled(true);
                    preference.setSummary("");
                }
	    }
        }else if("mms_fill_memory".equals(key)){
            CheckBoxPreference checkbox = (CheckBoxPreference) preference;
            if (checkbox.isChecked()) {
                dialog = ProgressDialog.show(this, "", getString(R.string.fill_memory_alert_msg));
                writeCmd("MMS MEM START");
                smsFillMemoryPref.setEnabled(false);
                mHandler.removeMessages(1);
                mHandler.sendEmptyMessageDelayed(1, 150000);
                preference.setSummary("uncheck to delete memory");
            } else {
                writeCmd("MEM STOP");
                showDialog(MEM_STOP);
                smsFillMemoryPref.setEnabled(true);
                preference.setSummary("");
            }
        }else if("show_mms_reply_choice".equals(key)){
            CheckBoxPreference checkbox = (CheckBoxPreference) preference;
            if(checkbox.isChecked()){
                SystemProperties.set("persist.sys.mms.showreplypath", "true");
                showMMSReplyChoicePref.setSummary("Show MMS Reply Choice");
            }else {
                SystemProperties.set("persist.sys.mms.showreplypath", "false");
                showMMSReplyChoicePref.setSummary("NO MMS Reply Choice");
            }
        }
        return super.onPreferenceTreeClick(preferenceScreen, preference);
    }

    @Override
    protected Dialog onCreateDialog(int id, Bundle args) {
        Dialog dialog = null;
        AlertDialog.Builder builder = new AlertDialog.Builder(this);

        switch (id) {
            case MEM_STOP: {
                dialog = builder.setMessage(R.string.fill_memory_stop_msg).setNegativeButton(
                        R.string.fill_memory_alert_ok, null).create();
                break;
            }
            default:
                break;
        }
        return dialog;
    }

    @Override
    protected void onStop() {
        unregisterReceiver(receiver);
        super.onStop();
    }

    private void writeCmd(String cmd) {

        if (mEf == null) {
            mEf = new engfetch();
        }
        mEf.writeCmd(cmd);

    }

}
