package com.spreadtrum.android.eng;

import android.content.ActivityNotFoundException;
import android.content.ComponentName;
import android.content.Intent;
import android.os.Bundle;
import android.os.Debug;
import android.preference.Preference;
import android.preference.PreferenceActivity;
import android.preference.PreferenceScreen;
import android.telephony.TelephonyManager;
import android.util.Log;

public class Eng83781Activity extends PreferenceActivity {
    private static final boolean DEBUG = Debug.isDebug();
    private static final String TAG = "Eng83781Activity&EngMobileSimChoose";
    private static final String KEY_GPRS = "key_gprs";
    private Preference mSerialPref;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        addPreferencesFromResource(R.layout.prefs_83781);

        PreferenceScreen prefSet = getPreferenceScreen();
        mSerialPref = prefSet.findPreference(KEY_GPRS);
        mSerialPref
                .setOnPreferenceClickListener(new Preference.OnPreferenceClickListener() {
                    public boolean onPreferenceClick(Preference preference) {
                        Intent intent = new Intent(Intent.ACTION_MAIN);
                        if (TelephonyManager.isMultiSim()) {
                            if(DEBUG) Log.d("TAG","TelephonyManager is MultiSim========================");
                            intent.setComponent(new ComponentName(
                                    "com.spreadtrum.android.eng",
                                    "com.spreadtrum.android.eng.EngMobileSimChoose"));
                            intent.putExtra(EngMobileSimChoose.PACKAGE_NAME,
                                    "com.android.phone");
                            intent.putExtra(EngMobileSimChoose.CLASS_NAME,
                                    "com.android.phone.Settings");
                        } else {
                            if(DEBUG) Log.d("TAG","TelephonyManager is  single Sim========================");
                            intent.setComponent(new ComponentName(
                                    "com.android.phone",
                                    "com.android.phone.Settings"));
                        }
                        try {
                            startActivity(intent);
                        } catch (ActivityNotFoundException e) {
                            Log.e("TAG", "Not found Activity !");
                        }
                        return true;
                    }
                });

    }

}
