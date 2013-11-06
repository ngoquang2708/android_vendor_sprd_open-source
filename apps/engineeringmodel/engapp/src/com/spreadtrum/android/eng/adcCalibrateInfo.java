package com.spreadtrum.android.eng;

import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.nio.charset.Charset;

import android.app.Activity;
import android.os.Bundle;
import android.os.Debug;
import android.os.Handler;
import android.os.Looper;
import android.os.Message;
import android.util.Log;
import android.widget.TextView;

public class adcCalibrateInfo extends Activity {
    private static final boolean DEBUG = Debug.isDebug();
	private static final String LOG_TAG = "adcCalibrateInfo";
	private int sockid = 0;
	private engfetch mEf;
	private String str=null;
	private EventHandler mHandler;
	private TextView txtViewlabel01;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.adcinfo);
	txtViewlabel01= (TextView)findViewById(R.id.adc_id);
	txtViewlabel01.setTextSize(20);
	initial();

    }

    private void initial() {
		mEf = new engfetch();
		sockid = mEf.engopen();
		Looper looper;
		looper = Looper.myLooper();
		mHandler = new EventHandler(looper);
		mHandler.removeMessages(0);
		Message msg = mHandler.obtainMessage(engconstents.ENG_AT_SGMR, 0, 0, 0);
		mHandler.sendMessage(msg);
	}

	private class EventHandler extends Handler
    {
    	public EventHandler(Looper looper) {
    	    super(looper);
    	}

    	@Override
		public void handleMessage(Message msg) {
			switch (msg.what) {
			case engconstents.ENG_AT_SGMR:
				ByteArrayOutputStream outputBuffer = new ByteArrayOutputStream();
				DataOutputStream outputBufferStream = new DataOutputStream(
						outputBuffer);

				if (DEBUG)
					Log.d(LOG_TAG, "engopen sockid=" + sockid);

				/*
				 * Modify 20130205 Spreadst of 125480 change the method of
				 * creating cmd start
				 */
				// str=String.format("%d,%d,%d,%d,%d", msg.what,3,0,0,3);
				str = new StringBuilder().append(msg.what).append(",")
						.append(3).append(",").append(0).append(",").append(0)
						.append(",").append(3).toString();
				/*
				 * Modify 20130205 Spreadst of 125480 change the method of
				 * creating cmd end
				 */
				try {
					outputBufferStream.writeBytes(str);
				} catch (IOException e) {
					Log.e(LOG_TAG, "writebytes error");
					return;
				}
				mEf.engwrite(sockid, outputBuffer.toByteArray(),
						outputBuffer.toByteArray().length);

				int dataSize = 1024;
				byte[] inputBytes = new byte[dataSize];
				int showlen = mEf.engread(sockid, inputBytes, dataSize);
				String str = new String(inputBytes, 0, showlen,
						Charset.defaultCharset());
				Log.v(LOG_TAG, "str = " + str);
				int pos = str.indexOf("CMMB");
				String tempStr;
				int pos1,
				pos2;
				String tempStr1,
				tempStr2;
				if (-1 != pos) {
					tempStr = str.substring(0, pos);
					pos1 = tempStr.lastIndexOf('\n');
					tempStr1 = tempStr.substring(0, pos1 + 1);

					tempStr = str.substring(pos);
					pos2 = tempStr.indexOf('\n');
					tempStr2 = tempStr.substring(pos2 + 1);
					str = tempStr1 + tempStr2;
				}
				txtViewlabel01.setText(str);
				break;
			}
		}
    }    
}
