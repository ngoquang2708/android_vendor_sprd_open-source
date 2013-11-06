package com.spreadtrum.android.eng;

import android.content.ComponentName;
import android.content.Intent;
import android.os.Bundle;
import android.preference.Preference;
import android.preference.PreferenceActivity;
import android.preference.PreferenceScreen;
import android.telephony.TelephonyManager;
import android.util.Log;

public class EngMobileSimChoose extends PreferenceActivity {
    private static String KEY1 = "mobile_sim1_settings";
    private static String KEY2 = "mobile_sim2_settings";
    private static String KEYOther = "mobile_other_settings";
    public static String PACKAGE_NAME = "package_name";
    public static String CLASS_NAME = "class_name";
    public static String CLASS_NAME_OTHER = "class_name_other";
    private Preference mSim1Pref;
    private Preference mSim2Pref;
    private Preference mOtherPref;

    private static final String SUB_ID = "sub_id";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        if (getIntent().getStringExtra(EngMobileSimChoose.CLASS_NAME_OTHER) == null) {
            addPreferencesFromResource(R.layout.mobile_sim_choose);
            // get UI object references
            PreferenceScreen prefSet = getPreferenceScreen();
            mSim1Pref = prefSet.findPreference(KEY1);
            mSim2Pref = prefSet.findPreference(KEY2);
        } else {
            addPreferencesFromResource(R.layout.mobile_sim_choose_other);
            // get UI object references
            PreferenceScreen prefSet = getPreferenceScreen();
            mSim1Pref = prefSet.findPreference(KEY1);
            mSim2Pref = prefSet.findPreference(KEY2);
            mOtherPref = prefSet.findPreference(KEYOther);
        }
    }

    @Override
    protected void onResume() {
        super.onResume();

        if (TelephonyManager.getDefault(0).getSimState() == TelephonyManager.SIM_STATE_READY) {
            mSim1Pref.setEnabled(true);
        } else {
            mSim1Pref.setEnabled(false);
        }
        if (TelephonyManager.getDefault(1).getSimState() == TelephonyManager.SIM_STATE_READY) {
            mSim2Pref.setEnabled(true);
        } else {
            mSim2Pref.setEnabled(false);
        }
    }

    @Override
    public boolean onPreferenceTreeClick(PreferenceScreen preferenceScreen,
            Preference preference) {
        Intent intent = new Intent(Intent.ACTION_MAIN);
        intent.setComponent(new ComponentName(getIntent().getStringExtra(EngMobileSimChoose.PACKAGE_NAME),
                                              getIntent().getStringExtra(EngMobileSimChoose.CLASS_NAME)));
        /**modify 146311 add sub_id start */
        if (preference == mSim1Pref) {
            intent.putExtra(SUB_ID, 0);
        } else if (preference == mSim2Pref) {
            intent.putExtra(SUB_ID, 1);
        } else if (preference == mOtherPref) {
        }
        /**modify 146311 add sub_id end */
        startActivity(intent);
        return true;
    }
}
