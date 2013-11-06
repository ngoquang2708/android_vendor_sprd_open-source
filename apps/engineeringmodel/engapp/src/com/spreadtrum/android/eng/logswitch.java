package com.spreadtrum.android.eng;

import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.nio.charset.Charset;

import android.os.Bundle;
import android.os.Debug;
import android.os.Handler;
import android.os.Looper;
import android.os.Message;
import android.preference.CheckBoxPreference;
import android.preference.Preference;
import android.preference.PreferenceActivity;
import android.util.Log;
import android.view.Gravity;
import android.widget.Toast;

public class logswitch extends PreferenceActivity 
implements Preference.OnPreferenceChangeListener{
    private static final boolean DEBUG = Debug.isDebug();
	private static final String LOG_TAG = "logswitch";
	private static final String KEY_INTEG = "integrity_set";
	private static final String KEY_FBAND = "fband_set";
	public static final String KEY_CAPLOG = "cap_log";
	private static final int setIntegrity = 1;
	private static final int setFBAND = 11;
	private int openlog = 0;
	private int sockid = 0;
	private engfetch mEf;
	private EventHandler mHandler;
	private CheckBoxPreference mPreference03,mPreference04,mPreference05;

	

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		addPreferencesFromResource(R.layout.logswitch);

		mPreference03 = (CheckBoxPreference) findPreference(KEY_INTEG);
		mPreference03.setOnPreferenceChangeListener(this);    
		mPreference04 = (CheckBoxPreference) findPreference(KEY_FBAND);
		mPreference04.setOnPreferenceChangeListener(this);    
		mPreference05 = (CheckBoxPreference) findPreference(KEY_CAPLOG);
		mPreference05.setOnPreferenceChangeListener(this);
		initialpara();
	}

	private void initialpara(){
		mEf = new engfetch();
		sockid = mEf.engopen();
		Looper looper;
		looper = Looper.myLooper();
		mHandler = new EventHandler(looper);
		mHandler.removeMessages(0);
	}

	@Override
	protected void onStop(){
	super.onStop();
	}

	@Override
	protected void onDestroy() {
	super.onDestroy();
	}

	private class EventHandler extends Handler{
		public EventHandler(Looper looper){
		    super(looper);
		}

		@Override
		public void handleMessage(Message msg) {
			switch(msg.what){
				case engconstents.ENG_AT_SETARMLOG:
				case engconstents.ENG_AT_SETSPTEST:
				case engconstents.ENG_AT_SETCAPLOG:

				ByteArrayOutputStream outputBuffer = new ByteArrayOutputStream();
				DataOutputStream outputBufferStream = new DataOutputStream(outputBuffer);

				if(DEBUG) Log.d(LOG_TAG, "engopen sockid=" + sockid);
				try {
    /*Modify 20130205 Spreadst of 125480 change the method of creating cmd start*/
					if(engconstents.ENG_AT_SETARMLOG == msg.what)
                        //outputBufferStream.writeBytes(String.format("%d,%d,%d", msg.what,1,openlog));
                        outputBufferStream.writeBytes(new StringBuilder().append(msg.what).append(",")
                                .append(1).append(",").append(openlog).toString());
					else if(engconstents.ENG_AT_SETCAPLOG == msg.what)
                       // outputBufferStream.writeBytes(String.format("%d,%d,%d", msg.what,1,openlog));
                        outputBufferStream.writeBytes(new StringBuilder().append(msg.what).append(",")
                                .append(1).append(",").append(openlog).toString());
					else
                       // outputBufferStream.writeBytes(String.format("%d,%d,%d,%d", msg.what,2,msg.arg1,msg.arg2));
                        outputBufferStream.writeBytes(new StringBuilder().append(msg.what).append(",")
                                .append(2).append(",").append(msg.arg1).append(",").append(msg.arg2).toString());
   /*Modify 20130205 Spreadst of 125480 change the method of creating cmd end*/
				} catch (IOException e) {
				Log.e(LOG_TAG, "writebytes error");
				return;
				}
				mEf.engwrite(sockid,outputBuffer.toByteArray(),outputBuffer.toByteArray().length);
				if(DEBUG) Log.d(LOG_TAG, "after engwrite");
				int dataSize = 512;
				byte[] inputBytes = new byte[dataSize];
				int showlen= mEf.engread(sockid,inputBytes,dataSize);
				String str1 =new String(inputBytes,0,showlen,Charset.defaultCharset());

				if(str1.equals("OK")){
				DisplayToast("Set Success.");
				}
				else if(str1.equals("ERROR")){
				DisplayToast("Set Failed.");
				}
				else
				DisplayToast("Unknown");
			    break;

			 }
		}
	}

	private void DisplayToast(String str) {
		Toast mToast = Toast.makeText(this, str, Toast.LENGTH_SHORT);
        /*
         * Delete 20130605 Spreadst of 108373 the toast's location is too high
         * start
         */
        //mToast.setGravity(Gravity.TOP, 0, 100);
        /*
         * Delete 20130225 Spreadst of 108373 the toast's location is too high
         * end
         */
		mToast.show();
	}

	public boolean onPreferenceChange(Preference preference, Object newValue) {
		// TODO Auto-generated method stub
		final String key = preference.getKey();
		if(DEBUG) Log.d(LOG_TAG, "onPreferenceChange newValue.toString() = "+newValue.toString());
		
		if(newValue.toString().equals("true"))
			openlog = 1;
			else if(newValue.toString().equals("false"))
			openlog = 0;

		if(KEY_INTEG.equals(key)){
			Message m = mHandler.obtainMessage(engconstents.ENG_AT_SETSPTEST, setIntegrity, openlog, 0);
			mHandler.sendMessage(m);
			return true;
		}else if(KEY_FBAND.equals(key)){
			Message m = mHandler.obtainMessage(engconstents.ENG_AT_SETSPTEST, setFBAND, openlog, 0);
			mHandler.sendMessage(m);
			return true;
		}else if(KEY_CAPLOG.equals(key)){
			Message m = mHandler.obtainMessage(engconstents.ENG_AT_SETCAPLOG, 0, 0, 0);
			mHandler.sendMessage(m);
			return true;
		}
		else
			return false;
	}

}

