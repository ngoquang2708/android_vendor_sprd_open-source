package com.spreadtrum.android.eng;

import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;

import android.util.Log;
import android.os.Bundle;
import android.content.Intent;
import android.preference.PreferenceActivity;
import android.preference.CheckBoxPreference;
import android.preference.Preference;
import android.preference.PreferenceScreen;
import android.content.SharedPreferences;

public class engineeringmodel extends PreferenceActivity {
	private static final String LOG_TAG = "engineeringmodel";

	public static final String PREFS_NAME = "ENGINEERINGMODEL";

	
	
	
	/** Called when the activity is first created. */
	@Override
	public void onCreate(Bundle savedInstanceState){
		super.onCreate(savedInstanceState);
		addPreferencesFromResource(R.layout.main);

	}
	
	
	public boolean onPreferenceTreeClick(PreferenceScreen preferenceScreen, Preference preference) {


		return false;
	}	
}
