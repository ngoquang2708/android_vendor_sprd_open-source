package com.spreadtrum.android.eng;

import android.os.Bundle;
import android.os.SystemProperties;
import android.preference.CheckBoxPreference;
import android.preference.Preference;
import android.preference.PreferenceActivity;
import android.preference.PreferenceScreen;

public class CMMBSettings extends PreferenceActivity {
	public static final String TEST_MODE = "test_mode";
	public static final String WIRE_TEST_MODE = "wire_test_mode";

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		addPreferencesFromResource(R.layout.cmmb_setting);
	}

	public boolean onPreferenceTreeClick(PreferenceScreen preferenceScreen,
			Preference preference) {
		final String key = preference.getKey();

		if (TEST_MODE.equals(key)) {
			if(preference instanceof CheckBoxPreference){
				SystemProperties.set("ro.hisense.cmcc.test",
					((CheckBoxPreference) preference).isChecked() ? "1" : "0");
			}
		} else if (WIRE_TEST_MODE.equals(key)) {
			if(preference instanceof CheckBoxPreference){
				SystemProperties.set("ro.hisense.cmcc.test.cmmb.wire",
					((CheckBoxPreference) preference).isChecked() ? "1" : "0");
			}
		} else {
			return false;
		}
		return true;
	}
}
//add by wangxiaobin 11-9 for cmmb set
