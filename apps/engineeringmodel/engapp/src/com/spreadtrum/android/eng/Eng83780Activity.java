package com.spreadtrum.android.eng;

import android.os.Bundle;
import android.preference.PreferenceActivity;

public class Eng83780Activity extends PreferenceActivity {

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		addPreferencesFromResource(R.layout.prefs_83780);
	}
}
