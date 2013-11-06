package com.spreadtrum.android.eng;

import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.io.IOException;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.os.Debug;
import android.util.Log;
import android.widget.TextView;

public class TextInfo extends Activity{
    private static final boolean DEBUG = Debug.isDebug();
    static final String TAG = "TextInfo";

    private TextView mTextView;
    private String mText;

    private engfetch mEf;
    private String mATline;
    private int mSocketID;
    /*Add 20130201 Spreadst of 123080 ANR after press mannual assert  start */
    private int mStartN;
    /*Add 20130201 Spreadst of 123080 ANR after press mannual assert  end */
    private ByteArrayOutputStream outputBuffer;
    private DataOutputStream outputBufferStream;

    /** Called when the activity is first created. */
    @Override
    public void onCreate(Bundle savedInstanceState){
        super.onCreate(savedInstanceState);
        setContentView(R.layout.textinfo);

        mTextView = (TextView) findViewById(R.id.text_view);
        mEf = new engfetch();
        Intent intent = this.getIntent();
        mStartN = intent.getIntExtra("text_info", 0);

		switch (mStartN) {
		case 1:
			setTitle(R.string.sim_forbid_plmn);
			mSocketID = mEf.engopen();
			break;
		case 2:
			setTitle(R.string.sim_equal_plmn);
			mSocketID = mEf.engopen();
			break;
		default:
			Log.e(TAG, "mStartN:" + mStartN);
		}
        /*Add 20130201 Spreadst of 123080 ANR after press mannual assert  start */
        this.setDisplayText();
        /*Add 20130201 Spreadst of 123080 ANR after press mannual assert  end */
    }
    /*Add 20130201 Spreadst of 123080 ANR after press mannual assert  start */
    private void  setDisplayText(){
        new Thread(
                new Runnable(){
                    public void run(){
                        outputBuffer = new ByteArrayOutputStream();
                        outputBufferStream = new DataOutputStream(outputBuffer);

                        switch (mStartN) {
                        case 1:
                            /*Modify 20130205 Spreadst of 125480 change the method of creating cmd start*/
                            //mATline = String.format("%d,%d", engconstents.ENG_AT_SFPL, 0);
                            mATline = new StringBuilder().append(engconstents.ENG_AT_SFPL).append(",").append(0).toString();
                            break;

                        case 2:
                           // mATline = String.format("%d,%d", engconstents.ENG_AT_SEPL, 0);
                            mATline = new StringBuilder().append(engconstents.ENG_AT_SEPL).append(",").append(0).toString();
                            /*Modify 20130205 Spreadst of 125480 change the method of creating cmd end*/
                            break;
                        default:
                            mTextView.setText("ERROR");
                        }
                        if(DEBUG) Log.d(TAG, "mATline :" + mATline);
                        try {
                            outputBufferStream.writeBytes(mATline);
                        } catch (IOException e) {
                            Log.e(TAG, "writeBytes() error!");
                            mTextView.setText("ERROR");
                        }

                        mEf.engwrite(mSocketID,outputBuffer.toByteArray(),outputBuffer.toByteArray().length);

                        int dataSize = 512;
                        byte[] inputBytes = new byte[dataSize];

                        int showlen= mEf.engread(mSocketID, inputBytes, dataSize);
                        String mATResponse =  new String(inputBytes, 0, showlen);
                        if(mATResponse.length() >= 10){
                            mATResponse = mATResponse.substring(10);//delete "+SFPL: " line
                        }
                        if(DEBUG) Log.d(TAG, "mATResponse:" + mATResponse);
                        if (mATResponse.length() > 0) {
                            mTextView.setText(mATResponse);
                        } else {
                            mTextView.setText("NULL");
                        }
                    }
                }).start();
        /*Add 20130201 Spreadst of 123080 ANR after press mannual assert  end */
    }
}


