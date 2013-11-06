package com.spreadtrum.android.eng;

import android.os.Bundle;
import android.os.Debug;
import android.os.SystemProperties;
import android.preference.CheckBoxPreference;
import android.preference.Preference;
import android.preference.PreferenceActivity;
import android.util.Log;

public class EngModeAllActivity extends PreferenceActivity implements
        Preference.OnPreferenceClickListener{
    private static final boolean DEBUG = Debug.isDebug();
	private static final String TAG = "EngModeAllActivity";
    static final String KEY_QOS_SWITCH = "Qos_switch";
    
	private static final boolean bIsLog = false; 
    private CheckBoxPreference mQosSwitch;
	/** Called when the activity is first created. */
	
	@Override
	public void onCreate(Bundle savedInstanceState){
		super.onCreate(savedInstanceState);
		if(bIsLog){
			if(DEBUG) Log.d(TAG, "---onCreate---");
		}
		addPreferencesFromResource(R.layout.engmodeallactivity);
        mQosSwitch =  (CheckBoxPreference)findPreference(KEY_QOS_SWITCH);
        if (null != mQosSwitch) {
            mQosSwitch.setOnPreferenceClickListener(this);
        }
	}
	
    public boolean onPreferenceClick(Preference preference) {
        final String key = preference.getKey();
		if(DEBUG) Log.d(TAG, "onPreferenceClick(), " + key);
        if (KEY_QOS_SWITCH.equals(key)) {
    		if(mQosSwitch.isChecked()){
    			SystemProperties.set("persist.sys.qosstate", "1");
    		}else{
    			SystemProperties.set("persist.sys.qosstate", "0");
    		}
		    return true;
        }
        return false;
    }
	

}
