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

public class autoattachatpoweron extends PreferenceActivity 
implements Preference.OnPreferenceChangeListener{
    private static final boolean DEBUG = Debug.isDebug();
	private static final String LOG_TAG = "autoattachatpoweron";
	private int sockid = 0;
	private engfetch mEf;
	private String str;
	private EventHandler mHandler;
	private CheckBoxPreference mCheckBoxPreference;
	private int onoroff = 0;
	private boolean hasPara;
	@Override
	protected void onCreate(Bundle savedInstanceState){
		super.onCreate(savedInstanceState);
		addPreferencesFromResource(R.layout.autoattach);

		mCheckBoxPreference = (CheckBoxPreference) findPreference("autoattach_value");
		mCheckBoxPreference.setOnPreferenceChangeListener(this);
		hasPara = false;
		initialpara();

	}

	private void initialpara(){
		mEf = new engfetch();
		sockid = mEf.engopen();
		Looper looper;
		looper = Looper.myLooper();
		mHandler = new EventHandler(looper);
		mHandler.removeMessages(0);
		Message m = mHandler.obtainMessage(engconstents.ENG_AT_AUTOATTACH, 0, 0, 0);
		mHandler.sendMessage(m);
	}

	private class EventHandler extends Handler{
		public EventHandler(Looper looper) {
		    super(looper);
		}

		@Override
		public void handleMessage(Message msg) {
			switch(msg.what){
				case engconstents.ENG_AT_AUTOATTACH:
				case engconstents.ENG_AT_SETAUTOATTACH:	

				ByteArrayOutputStream outputBuffer = new ByteArrayOutputStream();
				DataOutputStream outputBufferStream = new DataOutputStream(outputBuffer);

				if(DEBUG) Log.d(LOG_TAG, "engopen sockid=" + sockid);
				if(hasPara)
 /*Modify 20130205 Spreadst of 125480 change the method of creating cmd start*/
            //   str=String.format("%d,%d,%d", msg.what, 1, onoroff);
                str=new StringBuilder().append(msg.what).append(",").append(1).append(",").append(onoroff).toString();
				else
            //    str=String.format("%d,%d", msg.what,0);
                str=new StringBuilder().append(msg.what).append(",").append(0).toString();
/*Modify 20130205 Spreadst of 125480 change the method of creating cmd end*/
				try {
				outputBufferStream.writeBytes(str);
				} catch (IOException e) {
				Log.e(LOG_TAG, "writebytes error");
				return;
				}
				mEf.engwrite(sockid,outputBuffer.toByteArray(),outputBuffer.toByteArray().length);

				int dataSize = 128;
				byte[] inputBytes = new byte[dataSize];
				int showlen= mEf.engread(sockid,inputBytes,dataSize);
				String str1 =new String(inputBytes,0,showlen,Charset.defaultCharset());
				if(DEBUG) Log.d(LOG_TAG, "str1=" + str1);
				if(str1.equals("0")){
					mCheckBoxPreference.setChecked(false);
					onoroff = 1;	
				}else if(str1.equals("1")){
					mCheckBoxPreference.setChecked(true);
					onoroff = 0;
				}else if(str1.equals("OK")){
					Toast.makeText(getApplicationContext(), "Set Success.",Toast.LENGTH_SHORT).show(); 
					if(1 == onoroff){
						onoroff = 0;
					}else{
						onoroff = 1;
					}
				}else if(str1.equals("error")){
					Toast.makeText(getApplicationContext(), "Set Failed.",Toast.LENGTH_SHORT).show(); 
				}else
					Toast.makeText(getApplicationContext(), "Unknown",Toast.LENGTH_SHORT).show(); 
				break;

		 	}
		}
	}

	public boolean onPreferenceChange(Preference preference, Object newValue) {
		// TODO Auto-generated method stub
		if(preference.getKey().equals("autoattach_value")){
			Message m = mHandler.obtainMessage(engconstents.ENG_AT_SETAUTOATTACH, 0, 0, 0);
			mHandler.sendMessage(m);
			hasPara = true;
			return true;
		}else
		return false;
	}


}
