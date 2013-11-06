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
import android.preference.ListPreference;
import android.preference.Preference;
import android.preference.PreferenceActivity;
import android.util.Log;
import android.widget.Toast;

public class smsserver extends PreferenceActivity 
implements Preference.OnPreferenceChangeListener{
    private static final boolean DEBUG = Debug.isDebug();
	private static final String LOG_TAG = "smsserver";
	private int sockid = 0;
	private int valueofsms = 0;
	private engfetch mEf;
	private EventHandler mHandler;
	private ListPreference mListPreference;
	

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		addPreferencesFromResource(R.layout.smsserver);
		
		mListPreference = (ListPreference) findPreference("smsserver");
		mListPreference.setOnPreferenceChangeListener(this);

		mListPreference.setSummary(mListPreference.getEntry());
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

	private class EventHandler extends Handler
	{
		public EventHandler(Looper looper) {
		    super(looper);
		}

		@Override
		public void handleMessage(Message msg) {
			 switch(msg.what){
			    case engconstents.ENG_AT_CGSMS:
				ByteArrayOutputStream outputBuffer = new ByteArrayOutputStream();
				DataOutputStream outputBufferStream = new DataOutputStream(outputBuffer);

				if(DEBUG) Log.d(LOG_TAG, "engopen sockid=" + sockid);
				try {
             /*Modify 20130205 Spreadst of 125480 change the method of creating cmd start*/
            //    outputBufferStream.writeBytes(String.format("%d,%d,%d", msg.what,1,valueofsms));
                outputBufferStream.writeBytes(new StringBuilder().append(msg.what).append(",")
                        .append(1).append(",").append(valueofsms).toString());
                /*Modify 20130205 Spreadst of 125480 change the method of creating cmd end*/
				} catch (IOException e) {
				Log.e(LOG_TAG, "writebytes error");
				return;
				}
				mEf.engwrite(sockid,outputBuffer.toByteArray(),outputBuffer.toByteArray().length);

				int dataSize = 128;
				byte[] inputBytes = new byte[dataSize];
				int showlen= mEf.engread(sockid,inputBytes,dataSize);
				String str1 =new String(inputBytes,0,showlen,Charset.defaultCharset());
	
				if(str1.equals("OK"))
				{
				Toast.makeText(getApplicationContext(), "Send Success.",Toast.LENGTH_SHORT).show(); 

				}
				else if(str1.equals("ERROR"))
				{
				Toast.makeText(getApplicationContext(), "Send Failed.",Toast.LENGTH_SHORT).show(); 
				}
				else
				Toast.makeText(getApplicationContext(), "Unknown",Toast.LENGTH_SHORT).show(); 
				break;
			 }
		}
	}

	private void updateSelectList(Object value){
		CharSequence[] summaries = getResources().getTextArray(R.array.smsinfo);
		CharSequence[] mEntryValue = mListPreference.getEntryValues();
		for (int i=0; i<mEntryValue.length; i++) 
		{
		    if (mEntryValue[i].equals(value)) 
		    {
		    	mListPreference.setSummary(summaries[i]);
		    	mListPreference.setValueIndex(i);
			valueofsms = i;
		        break;
		    }
		}		
		
	}
	public boolean onPreferenceChange(Preference preference, Object newValue){
		// TODO Auto-generated method stub
		if (preference.getKey().equals(mListPreference.getKey())){
			updateSelectList(newValue);
			Message m = mHandler.obtainMessage(engconstents.ENG_AT_CGSMS, 0, 0, 0);
			mHandler.sendMessage(m);
		}
			return false;
	}


}

