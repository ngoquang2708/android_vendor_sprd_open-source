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
import com.spreadtrum.android.eng.R;

public class RFMaxPower extends Activity {
	private static final String TAG = "RFMaxPower";
	private int sockid = 0;
	private engfetch mEf;
	private String str = null;
	private TextView txtViewlabel01;
	private Handler mUiThread = new Handler();
	

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		setContentView(R.layout.rfmaxpower);
		txtViewlabel01 = (TextView) findViewById(R.id.rfmaxpower_id);
		txtViewlabel01.setTextSize(20);

		mEf = new engfetch();
		sockid = mEf.engopen();

		new Thread(new Runnable() {
			@Override
			public void run() {
				requestEvent(engconstents.ENG_AT_SSMP);
			}
		}).start();
	}
	
	@Override
	protected void onDestroy() {
		super.onDestroy();
	}
	
	private void requestEvent(int code){
		switch (code) {
		case engconstents.ENG_AT_SSMP:
			ByteArrayOutputStream outputBuffer = new ByteArrayOutputStream();
			DataOutputStream outputBufferStream = new DataOutputStream(
					outputBuffer);
            /*Modify 20130205 Spreadst of 125480 change the method of creating cmd start*/
            //str = String.format("%d,%d", code, 0);
            str = new StringBuilder().append(code).append(",").append(0).toString();
            /*Modify 20130205 Spreadst of 125480 change the method of creating cmd end*/
			try {
				outputBufferStream.writeBytes(str);
			} catch (IOException e) {
				Log.e(TAG, "writebytes error");
				return;
			}
			mEf.engwrite(sockid, outputBuffer.toByteArray(),
					outputBuffer.toByteArray().length);

			int dataSize = 256;
			byte[] inputBytes = new byte[dataSize];
			int showlen = mEf.engread(sockid, inputBytes, dataSize);
			final String str = new String(inputBytes, 0, showlen,Charset.defaultCharset());
			mUiThread.post(new Runnable() {
				public void run() {
					txtViewlabel01.setText(str);
				}
			});
			break;
		}
	}
}
