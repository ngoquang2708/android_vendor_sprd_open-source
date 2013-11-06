package com.spreadtrum.android.eng;

import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.nio.charset.Charset;

import android.app.Activity;
import android.os.Bundle;
import android.os.Debug;
import android.os.Handler;
import android.os.Message;
import android.util.Log;
import android.widget.TextView;

public class psverinfo extends Activity {
    private static final boolean DEBUG = Debug.isDebug();
	private static final String LOG_TAG = "psverinfo";
	private TextView  mTextView01;
	private int sockid = 0;
	private engfetch mEf;
	private String str;

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		setContentView(R.layout.psverinfo);
		
		mTextView01 = (TextView)findViewById(R.id.ps01_view);
		mTextView01.setText("Wait...");
		initialpara();
		Thread myThread = new Thread(runnable);
		myThread.setName("new thread");
		myThread.start();
		if(DEBUG) Log.d(LOG_TAG, "thread name ="+myThread.getName()+" id ="+myThread.getId());
	}

	private void initialpara() {
		mEf = new engfetch();
		sockid = mEf.engopen();
	}

    Handler handler = new Handler(){
		@Override
		public void handleMessage(Message msg) {
			// TODO Auto-generated method stub
			if(msg.what == engconstents.ENG_AT_SPVER){
				String str = (String) msg.obj;
				mTextView01.setText(str);
			}
		}
    };

    Runnable runnable = new Runnable(){
		public void run() {
			// TODO Auto-generated method stub
		    if(DEBUG) Log.d(LOG_TAG, "run is Runnable~~~");
			Message msg = handler.obtainMessage();
			msg.what = engconstents.ENG_AT_SPVER;
			msg.obj = writeAndReadDateFromServer(msg.what);
			if(DEBUG) Log.d(LOG_TAG, "msg.obj = <" + msg.obj+">");
			handler.sendMessage(msg);
		}
    };

    private String writeAndReadDateFromServer(int what) {
			ByteArrayOutputStream outputBuffer = new ByteArrayOutputStream();
			DataOutputStream outputBufferStream = new DataOutputStream(outputBuffer);
			if(DEBUG) Log.d(LOG_TAG, "engopen sockid=" + sockid);
/*Modify 20130205 Spreadst of 125480 change the method of creating cmd start*/
            //str=String.format("%d,%d,%d", what,1,0);
            str = new StringBuilder().append(what).append(",").append(1).append(",").append(0).toString();
/*Modify 20130205 Spreadst of 125480 change the method of creating cmd end*/
            try {
			    outputBufferStream.writeBytes(str);
			} catch (IOException e) {
			    Log.e(LOG_TAG, "writebytes error");
			    return "error";
			}
			mEf.engwrite(sockid,outputBuffer.toByteArray(),outputBuffer.toByteArray().length);
			int dataSize = 128;
			byte[] inputBytes = new byte[dataSize];
			int showlen= mEf.engread(sockid,inputBytes,dataSize);
			String str123 =  new String(inputBytes, 0, showlen,Charset.defaultCharset());
			if(DEBUG) Log.d(LOG_TAG, "str123" + str123);
			return str123;
	}
}

