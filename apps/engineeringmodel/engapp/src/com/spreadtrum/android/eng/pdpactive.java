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
import android.widget.Toast;

public class pdpactive extends PreferenceActivity 
implements Preference.OnPreferenceChangeListener{
    private static final boolean DEBUG = Debug.isDebug();
	private static final String LOG_TAG = "pdpact";
	private int sockid = 0;
	private engfetch mEf;
	private String str;
	private EventHandler mHandler;
	private CheckBoxPreference mCheckBoxPreference;
	private int active = 0;

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		addPreferencesFromResource(R.layout.pdpactive);

		mCheckBoxPreference = (CheckBoxPreference) findPreference(LOG_TAG);
		mCheckBoxPreference.setOnPreferenceChangeListener(this);

		initialpara();

	}
	private void initialpara()
	{
		mEf = new engfetch();
		sockid = mEf.engopen();
		Looper looper;
		looper = Looper.myLooper();
		mHandler = new EventHandler(looper);
		mHandler.removeMessages(0);
	}

	private class EventHandler extends Handler
	{
		public EventHandler(Looper looper) {
		    super(looper);
		}

		@Override
		public void handleMessage(Message msg) {
			switch(msg.what) {
				case engconstents.ENG_AT_PDPACTIVE:
				ByteArrayOutputStream outputBuffer = new ByteArrayOutputStream();
				DataOutputStream outputBufferStream = new DataOutputStream(outputBuffer);

				if(DEBUG) Log.d(LOG_TAG, "engopen sockid=" + sockid);
/*Modify 20130205 Spreadst of 125480 change the method of creating cmd start*/
                //str=String.format("%d,%d,%d,%d", msg.what,2,active,1);
                str = new StringBuilder().append(msg.what).append(",").append(2).append(",")
                      .append(active).append(",").append(1).toString();
/*Modify 20130205 Spreadst of 125480 change the method of creating cmd end*/
                if(DEBUG) Log.d(LOG_TAG, "str=" + str);
				try {
				outputBufferStream.writeBytes(str);
				} catch (IOException e) {
				if(DEBUG) Log.d(LOG_TAG, "writebytes error");
				return;
				}
				mEf.engwrite(sockid,outputBuffer.toByteArray(),outputBuffer.toByteArray().length);

				int dataSize = 128;
				byte[] inputBytes = new byte[dataSize];
				int showlen= mEf.engread(sockid,inputBytes,dataSize);
				String str1 =new String(inputBytes,0,showlen,Charset.defaultCharset());

				if(str1.equals("OK")){
					Toast.makeText(getApplicationContext(), "Set Success.",Toast.LENGTH_SHORT).show(); 
					if(1 == active)	{
						active = 0;
					}
					else{
						active = 1;
					}
				}
				else if(str1.equals("ERROR")){
					Toast.makeText(getApplicationContext(), "Set Failed.",Toast.LENGTH_SHORT).show(); 
				}
				else
					Toast.makeText(getApplicationContext(), "Unknown",Toast.LENGTH_SHORT).show(); 

				break;
		 	}
		}
	}

	public boolean onPreferenceChange(Preference preference, Object newValue) {
		// TODO Auto-generated method stub
		if(preference.getKey().equals(LOG_TAG)){
			Message m = mHandler.obtainMessage(engconstents.ENG_AT_PDPACTIVE, 0, 0, 0);
			mHandler.sendMessage(m);
			return true;
		}
		else
		return false;
	}


}


