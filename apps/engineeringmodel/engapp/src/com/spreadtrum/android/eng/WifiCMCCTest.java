package com.spreadtrum.android.eng;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;

import java.io.DataOutputStream;

import android.app.ProgressDialog;
import android.content.Context;
import android.net.wifi.WifiManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Message;
import android.preference.CheckBoxPreference;
import android.preference.Preference;
import android.preference.PreferenceActivity;
import android.preference.PreferenceScreen;
import android.provider.Settings;
import android.util.Log;

public class WifiCMCCTest extends PreferenceActivity {

	private static final String TAG = "WifiCMCCTest";

	private static final boolean debug = true;

	private static final int ENABLE_PREF = 1;

	private static final int DISABLE_PREF = 2;

	private static final int DISMISS_DIALOG = 3;

	private static final int WIFI_ON_FAIL = 4;

	private static final int WIFI_OFF_FAIL = 5;

	private static final int CMCC_START = 6;

	private static final int CMCC_STOP = 7;

	private static final int TEST_ENABLE = 8;

	private static final int TEST_DISABLE = 9;

	private engfetch mEf;

	private boolean isRunning = false;

	private WifiManager wifiManager;

	private int outWifiState;

	private int waitTimes = 10;

	private CheckBoxPreference mAnritsuCheckbox;

	private CheckBoxPreference mNonAnritsuCheckbox;

	private CheckBoxPreference mRunTestCheckbox;

	private ProgressDialog mDialog;

	public boolean isAnritsu;
	
	private static int WIFI_SLEEP_POLICY_ENG_MODE_NEVER = 3;

	private Handler mHandler = new Handler() {
		@Override
		public void handleMessage(Message msg) {
			CheckBoxPreference checkbox;
			int what = msg.what;
			if (isAnritsu) {
				checkbox = mAnritsuCheckbox;
			} else {
				checkbox = mNonAnritsuCheckbox;
			}
			switch (what) {
			case ENABLE_PREF:

				break;
			case DISABLE_PREF:

				break;
			case DISMISS_DIALOG:
				if (mDialog != null) {
					mDialog.dismiss();
				}
				break;
			case WIFI_ON_FAIL:
				checkbox.setSummary("wifi open failed");
				break;
			case WIFI_OFF_FAIL:
				checkbox.setSummary("wifi close failed");
				break;
			case CMCC_START:
				checkbox.setSummary("");
				break;
			case CMCC_STOP:
				checkbox.setSummary("");
				break;
			default:
				break;
			}
		}
	};

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		addPreferencesFromResource(R.layout.prefs_wifi_cmcc_test);
		//mAnritsuCheckbox = (CheckBoxPreference) findPreference("anritsu_test");
		mNonAnritsuCheckbox = (CheckBoxPreference) findPreference("non_anritsu_test");

		//mAnritsuCheckbox.setSummary("");
		mNonAnritsuCheckbox.setSummary("");

		wifiManager = (WifiManager) getSystemService(Context.WIFI_SERVICE);
		outWifiState = wifiManager.getWifiState();
	}

	@Override
	public boolean onPreferenceTreeClick(PreferenceScreen preferenceScreen,
			Preference preference) {
		String key = preference.getKey();

		if ("anritsu_test".equals(key)) {
			isAnritsu = true;
			CheckBoxPreference checkbox = (CheckBoxPreference) preference;
			if (checkbox.isChecked()) {
				// radioCheckbox(key);
				new Thread(new Runnable() {
					public void run() {
						cmccStart();
					}
				}).start();
			} else {
				// releaseCheckbox();
				new Thread(new Runnable() {

					@Override
					public void run() {
						cmccStop();
					}
				}).start();
			}
		} else if ("non_anritsu_test".equals(key)) {
			CheckBoxPreference checkbox = (CheckBoxPreference) preference;
			isAnritsu = false;
			if (checkbox.isChecked()) {
				// radioCheckbox(key);
				new Thread(new Runnable() {
					public void run() {
						cmccStart();
					}
				}).start();
			} else {
				// releaseCheckbox();
				new Thread(new Runnable() {

					@Override
					public void run() {
						cmccStop();
					}
				}).start();
			}
		}
		return super.onPreferenceTreeClick(preferenceScreen, preference);
	}

	private void radioCheckbox(String prefsKey) {
		if ("anritsu_test".equals(prefsKey)) {
			mNonAnritsuCheckbox.setEnabled(false);
		} else {
			mAnritsuCheckbox.setEnabled(false);
		}
	}

	private void releaseCheckbox() {
		mNonAnritsuCheckbox.setEnabled(true);
		mAnritsuCheckbox.setEnabled(true);
	}

	@Override
	protected void onStop() {
		super.onStop();
	}

	private void GetErrotResult(Process proc) {
		BufferedReader reader = new BufferedReader(new InputStreamReader(
				proc.getErrorStream()));
		int read;
		char[] buffer = new char[4096];
		StringBuffer output = new StringBuffer();
		try {
			while ((read = reader.read(buffer)) > 0) {
				output.append(buffer, 0, read);
			}
			Log.i(TAG, "CMD ERROR RESULT=" + output.toString());
		} catch (IOException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
		}
		try {
			reader.close();
		} catch (IOException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
		}
	}

	private void ReadResult(Process proc) {
		BufferedReader reader = new BufferedReader(new InputStreamReader(
				proc.getInputStream()));
		int read;
		char[] buffer = new char[4096];
		StringBuffer output = new StringBuffer();
		try {
			while ((read = reader.read(buffer)) > 0) {
				output.append(buffer, 0, read);
			}
			Log.i(TAG, "CMD RESULT=" + output.toString());
		} catch (IOException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
		}
		try {
			reader.close();
		} catch (IOException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
		}
	}

	private void RunScript(String cmd) {
		DataOutputStream dos = null;
		Runtime runtime = Runtime.getRuntime();
		Process proc;
		try {
			proc = runtime.exec(cmd);
			ReadResult(proc);
			GetErrotResult(proc);
		} catch (IOException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
		}

	}

	private void cmccStop() {

		if (isAnritsu) {
			Settings.System.putInt(getContentResolver(),
					Settings.System.WIFI_SLEEP_POLICY,
					Settings.System.WIFI_SLEEP_POLICY_DEFAULT);
			Log.i(TAG, "STOP SLEEP="
					+ Settings.System.WIFI_SLEEP_POLICY_DEFAULT);

			writeCmd("UNSET MAX RATE");
			Log.i(TAG, "STOP MAX RATE");

		} else {
			writeCmd("UNSET MAX POWER");
			Log.i(TAG, "STOP UNSET MAX POWER");
		}
	}

	private void cmccStart() {
		if (isAnritsu) {
			Settings.System.putInt(getContentResolver(),
					Settings.System.WIFI_SLEEP_POLICY,
					WIFI_SLEEP_POLICY_ENG_MODE_NEVER);
			Log.i(TAG, "START SLEEP=" + Settings.System.WIFI_SLEEP_POLICY_NEVER);
			writeCmd("SET MAX RATE");
			Log.i(TAG, "START SET RATE");

		} else {
			writeCmd("SET MAX POWER");
			Log.i(TAG, "START SET MAX POWER");
		}
	}

	private void cmccTest() {
		writeCmd("CMCC TEST");
		mHandler.sendEmptyMessage(CMCC_START);
		if (debug)
			Log.d(TAG, "xbin:CMCC TEST");

	}

	private void writeCmd(String cmd) {

		if (mEf == null) {
			mEf = new engfetch();
		}
		Log.i(TAG, "write cmd = " + cmd);
		mEf.writeCmd(cmd);

	}

	private boolean waitForWifiOn() {
		int wifiState;
		while (waitTimes-- > 0) {
			if (debug)
				Log.d(TAG, "on waitTimes=" + waitTimes);
			wifiState = wifiManager.getWifiState();
			if (debug)
				Log.d(TAG, " wait on wifiState=" + wifiState);
			if (wifiState == WifiManager.WIFI_STATE_ENABLED) {
				break;
			} else {
				try {
					Thread.sleep(2000);
				} catch (InterruptedException e) {
					e.printStackTrace();
				}
			}
		}
		if (waitTimes > 0) {
			return true;
		} else {
			return false;
		}
	}

	private boolean waitForWifiOff() {
		int wifiState;
		while (waitTimes-- > 0) {
			if (debug)
				Log.d(TAG, "off waitTimes=" + waitTimes);
			wifiState = wifiManager.getWifiState();
			if (debug)
				Log.d(TAG, "off wifiState=" + wifiState);
			if (wifiState == WifiManager.WIFI_STATE_DISABLED) {
				break;
			} else {
				try {
					Thread.sleep(2000);
				} catch (InterruptedException e) {
					e.printStackTrace();
				}
			}
		}

		if (waitTimes > 0) {
			return true;
		} else {
			return false;
		}

	}

}
