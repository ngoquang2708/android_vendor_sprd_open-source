package com.spreadtrum.android.eng;

import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.nio.charset.Charset;

import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.util.Log;
import android.widget.TextView;

public class Layer1Monitor extends Activity {
    
	static final String LogTag = "Layer1Monitor";

	static final int MSG_REFRESH = 1;
	static final int REFESH_INTERVAL = 5 * 1000;
	private TextView mTextView;
	private engfetch mEf;
	private String mATline;
	private int mSocketID;
	private boolean mRunnable = false;

	private Handler mHandler = new Handler();

	/** Called when the activity is first created. */
	@Override
	public void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		setContentView(R.layout.layer1monitor);
		mTextView = (TextView) findViewById(R.id.text_view);
		mEf = new engfetch();
		mSocketID = mEf.engopen();
		mRunnable = true;
		new Thread(new Runnable() {
			public void run() {
				while(mRunnable){
					final String text = getL1MonitorText();
					mHandler.post(new Runnable() {
						public void run() {
							mTextView.setText(text);
						}
					});
					try {
						Thread.sleep(REFESH_INTERVAL);
					} catch (InterruptedException e) {
						e.printStackTrace();
					}
				}
			}
		}).start();
	}

	private String getL1MonitorText() {
		ByteArrayOutputStream outputBuffer = new ByteArrayOutputStream();
		DataOutputStream outputBufferStream = new DataOutputStream(outputBuffer);
/*Modify 20130205 Spreadst of 125480 change the method of creating cmd start*/
        //mATline = String.format("%d,%d", engconstents.ENG_AT_L1MON, 0);
        mATline = new StringBuilder().append(engconstents.ENG_AT_L1MON).append(",").append(0).toString();
/*Modify 20130205 Spreadst of 125480 change the method of creating cmd end*/
		try {
			outputBufferStream.writeBytes(mATline);
		} catch (IOException e) {
			Log.e(LogTag, "writeBytes() error!");
			return "";
		}
		mEf.engwrite(mSocketID, outputBuffer.toByteArray(),outputBuffer.toByteArray().length);
		int dataSize = 2048;
		byte[] inputBytes = new byte[dataSize];
		int showlen = mEf.engread(mSocketID, inputBytes, dataSize);
		final String mATResponse = new String(inputBytes, 0, showlen,Charset.defaultCharset());
		return mATResponse;
	}
	
	@Override
	protected void onDestroy() {
		mRunnable = false;
		super.onDestroy();
	}
}
