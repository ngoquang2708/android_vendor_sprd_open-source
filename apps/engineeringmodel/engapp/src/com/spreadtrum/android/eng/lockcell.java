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
import android.view.View;
import android.widget.Button;
import android.widget.EditText;

public class lockcell extends Activity {
    private static final boolean DEBUG = Debug.isDebug();
	private static final String LOG_TAG = "engnetinfo";
	private int sockid = 0;
	private engfetch mEf;
	private String str=null;
	private EventHandler mHandler;
	private Button mButton;
	private static EditText[]mET = new EditText[16];
	private static String[]ss = new String[16];
	
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.lockcell);

    	mET[0] = (EditText)findViewById(R.id.editText1);
    	mET[1] = (EditText)findViewById(R.id.editText11);
    	mET[2] = (EditText)findViewById(R.id.editText12);
    	mET[3] = (EditText)findViewById(R.id.editText13);
    	
    	mET[4] = (EditText)findViewById(R.id.editText2);
    	mET[5] = (EditText)findViewById(R.id.editText21);
    	mET[6] = (EditText)findViewById(R.id.editText22);
    	mET[7] = (EditText)findViewById(R.id.editText23);
    	
    	mET[8] = (EditText)findViewById(R.id.editText3);
    	mET[9] = (EditText)findViewById(R.id.editText31);
    	mET[10] = (EditText)findViewById(R.id.editText32);
    	mET[11] = (EditText)findViewById(R.id.editText33);
    	
    	mET[12] = (EditText)findViewById(R.id.editText4);
    	mET[13] = (EditText)findViewById(R.id.editText41);
    	mET[14] = (EditText)findViewById(R.id.editText42);
    	mET[15] = (EditText)findViewById(R.id.editText43);

    	mButton = (Button)findViewById(R.id.Button_get);	
    	mButton.setText("Get data");
    	mButton.setOnClickListener(new Button.OnClickListener(){

			public void onClick(View v) {
				// TODO Auto-generated method stub
				initialpara();
			}
    		
    	});
    }
    private void initialpara() {
	// TODO Auto-generated method stub
    	mEf = new engfetch();
    	sockid = mEf.engopen();
    	Looper looper;
    	looper = Looper.myLooper();
    	mHandler = new EventHandler(looper);
    	mHandler.removeMessages(0);
	Message m = mHandler.obtainMessage(engconstents.ENG_AT_GETSPFRQ, 0, 0, 0);
	mHandler.sendMessage(m);
	}

	private class EventHandler extends Handler{
		public EventHandler(Looper looper){
		    super(looper);
		}
		@Override
		public void handleMessage(Message msg) {

			switch(msg.what){
				case engconstents.ENG_AT_GETSPFRQ:
					ByteArrayOutputStream outputBuffer = new ByteArrayOutputStream();
					DataOutputStream outputBufferStream = new DataOutputStream(outputBuffer);

					if(DEBUG) Log.d(LOG_TAG, "engopen sockid=" + sockid);
/*Modify 20130205 Spreadst of 125480 change the method of creating cmd start*/
                    //str=String.format("%d,%d", msg.what,0,0);
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
					String str =new String(inputBytes,0,showlen,Charset.defaultCharset());
					ss = str.split(","); 
					for(int i=0;i<ss.length;i++){
					mET[i].setText(ss[i]);
					}
				    break;
			}
		}
	}


}

