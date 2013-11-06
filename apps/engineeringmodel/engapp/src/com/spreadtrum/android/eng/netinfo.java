package com.spreadtrum.android.eng;

import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.nio.charset.Charset;

import android.app.Activity;
import android.content.Context;
import android.net.ConnectivityManager;
import android.net.NetworkInfo.State;
import android.os.Bundle;
import android.os.Debug;
import android.os.Handler;
import android.util.Log;
import android.widget.TextView;

public class netinfo extends Activity {
    private static final boolean DEBUG = Debug.isDebug();
	private static final String TAG = "netinfo";
	private int sockid = 0;
	private engfetch mEf;
	private String str = "";
	private Handler mUiThread = new Handler();
	private TextView tv1;
	private TextView tv2;
	private TextView tv1_1;
	private TextView tv2_2;
	private static final int SCELL = 1;
	private static final int NCELL = 2;

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		setContentView(R.layout.netinfo);
		tv1 = (TextView) findViewById(R.id.scell_title);
		tv2 = (TextView) findViewById(R.id.ncell_title);
		tv1_1 = (TextView) findViewById(R.id.scell_value);
		tv2_2 = (TextView) findViewById(R.id.ncell_value);

		mEf = new engfetch();
		sockid = mEf.engopen();

		new Thread(new Runnable() {
			@Override
			public void run() {
				if (checkCurrentNetworkState()) {
					requestEvent(engconstents.ENG_AT_CCED, 0, SCELL);
					requestEvent(engconstents.ENG_AT_CCED, 0, NCELL);
				} else {
					tv1.setText("network is unavailable");
					if(DEBUG) Log.d(TAG, "network is unavailable");
				}
			}
		}).start();
	}

	private void requestEvent(int code,int arg1,int arg2){
		switch (code) {
		case engconstents.ENG_AT_CCED:
			ByteArrayOutputStream outputBuffer = new ByteArrayOutputStream();
			DataOutputStream outputBufferStream = new DataOutputStream(outputBuffer);
			if(DEBUG) Log.d(TAG, "engopen sockid=" + sockid);
/*Modify 20130205 Spreadst of 125480 change the method of creating cmd start*/
            //str = String.format("%d,%d,%d,%d", code, 2, arg1,arg2);
            str = new StringBuilder().append(code).append(",").append(2)
                  .append(",").append(arg1).append(",").append(arg2).toString();
/*Modify 20130205 Spreadst of 125480 change the method of creating cmd end*/
			try {
				outputBufferStream.writeBytes(str);
			} catch (IOException e) {
				Log.e(TAG, "writebytes error");
				return;
			}
			mEf.engwrite(sockid, outputBuffer.toByteArray(), outputBuffer.toByteArray().length);

			int dataSize = 256;
			byte[] inputBytes = new byte[dataSize];
			int showlen = mEf.engread(sockid, inputBytes, dataSize);
			String str = new String(inputBytes, 0, showlen,Charset.defaultCharset()).trim();
			if(DEBUG) Log.d(TAG, "get result : " + str);
			if (arg2 == SCELL) {
				String s = "";
				if (str.indexOf(",") != -1) {
					String[] strs = str.split(",");
/*Modify 20130205 Spreadst of 125480 change the method of creating cmd start*/
              //      s = String.format("        %s,%s,%s,%s", strs[0],
              //              strs[1], strs[5], strs[6]);
                    s = new StringBuilder().append("        ").append(strs[0]).append(",")
                        .append(strs[1]).append(",").append(strs[5]).append(",").append(strs[6])
                        .toString();
/*Modify 20130205 Spreadst of 125480 change the method of creating cmd end*/
					updateUi(tv1_1, s);
				} else {
					updateUi(tv1_1,"    "+str);
				}
			} else if (arg2 == NCELL) {
				if (str.indexOf(",") != -1) {
					String[] strs = str.split(",");
					String s = "";
					int nums = strs.length / 7;
					for (int i = 0; i < nums; i++) {
    /*Modify 20130205 Spreadst of 125480 change the method of creating cmd start*/
//                        s += String.format("        %s,%s,%s,%s\n",
//                                strs[i * 7], strs[i * 7 + 1],
//                                strs[i * 7 + 5], strs[i * 7 + 6]);
                        /*Modify 20130325 Spreadst of 141672 change the display method start */
                        s +=new StringBuilder().append("        ").append(strs[i * 7]).append(",")
                            .append(strs[i * 7 + 1]).append(",").append(strs[i * 7 + 5]).append(",")
                            .append(strs[i * 7 + 6]).append("\n").toString();
                        /*Modify 20130325 Spreadst of 141672 change the display method end */
    /*Modify 20130205 Spreadst of 125480 change the method of creating cmd end*/
					}
					updateUi(tv2_2, s);
				} else {
					updateUi(tv2_2, "    "+str);
				}
			}
			break;
		}
	}
	
	private void updateUi(final TextView tv,final String msg){
		mUiThread.post(new Runnable() {
			public void run() {
				tv1.setText("SCELL");
				tv2.setText("NCELL");
				tv1.setTextSize(20);
				tv1_1.setTextSize(20);
				tv2.setTextSize(20);
				tv2_2.setTextSize(20);
				tv.setText(msg);
			}
		});
	}

	private boolean checkCurrentNetworkState() {
		ConnectivityManager conMan = (ConnectivityManager) getSystemService(Context.CONNECTIVITY_SERVICE);
		if (conMan.getNetworkInfo(ConnectivityManager.TYPE_MOBILE).getState() == State.DISCONNECTED)
			return false;
		else
			return true;
	}

}
