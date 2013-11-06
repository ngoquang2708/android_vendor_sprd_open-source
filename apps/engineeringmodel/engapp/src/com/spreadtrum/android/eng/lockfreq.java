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
import android.widget.Toast;

public class lockfreq extends Activity {
    private static final boolean DEBUG = Debug.isDebug();
	private static final String LOG_TAG = "lockfreq";
	private int sockid = 0;
	private engfetch mEf;
	private String str=null;
	private EventHandler mHandler;
	private EditText  mET01;
	private EditText  mET02;
	private EditText  mET03;
	private EditText  mET04;
	private int  mInt01;
	private int  mInt02;
	private int  mInt03;
	private int  mInt04;
	private Button mButton;
	private Button mButton01;
	@Override
	protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.lockfreq);
        initialpara();
	}

	private void initialpara() {
		// TODO Auto-generated method stub
    	mET01 = (EditText)findViewById(R.id.editText1);	
    	mET02 = (EditText)findViewById(R.id.editText2);	
    	mET03 = (EditText)findViewById(R.id.editText3);	
    	mET04 = (EditText)findViewById(R.id.editText4);
    	clearEditText();
    	mButton = (Button)findViewById(R.id.lock_button);	
    	mButton01 = (Button)findViewById(R.id.clear_button);	
		mButton.setText("Lock");
		mButton01.setText("Clear");
    	
    	mEf = new engfetch();
    	sockid = mEf.engopen();
    	Looper looper;
    	looper = Looper.myLooper();
    	mHandler = new EventHandler(looper);
    	mHandler.removeMessages(0);
    	
    	mButton.setOnClickListener(new Button.OnClickListener(){
		public void onClick(View v) {
			// TODO Auto-generated method stub
			Message m = mHandler.obtainMessage(engconstents.ENG_AT_SETSPFRQ, 0, 0, 0);
			mHandler.sendMessage(m);
		}
    		
    	});
    	mButton01.setOnClickListener(new Button.OnClickListener(){

			public void onClick(View v) {
				// TODO Auto-generated method stub
		    	clearEditText();
			}
    		
    	});
	}

	private void clearEditText() {
		// TODO Auto-generated method stub
    	mET01.setText("0");
    	mET02.setText("0");
    	mET03.setText("0");
    	mET04.setText("0");
	}

	private class EventHandler extends Handler
	{
		public EventHandler(Looper looper) {
		    super(looper);
		}

		@Override
		public void handleMessage(Message msg) {
			 switch(msg.what)
			 {
			    case engconstents.ENG_AT_SETSPFRQ:
				    ByteArrayOutputStream outputBuffer = new ByteArrayOutputStream();
				    DataOutputStream outputBufferStream = new DataOutputStream(outputBuffer);

				    if(DEBUG) Log.d(LOG_TAG, "engopen sockid=" + sockid);
				    getEditTextValue();
    /*Modify 20130205 Spreadst of 125480 change the method of creating cmd start*/
                    //str=String.format("%d,%d,%d,%d,%d,%d", msg.what,4,mInt01,mInt02,mInt03,mInt04);
                    str= new StringBuilder().append(msg.what).append(",").append(4).append(",")
                         .append(mInt01).append(",").append(mInt02).append(",").append(mInt03)
                         .append(",").append(mInt04).toString();
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
					if(str.equals("OK")){
						Toast.makeText(getApplicationContext(), "Lock Success.",Toast.LENGTH_SHORT).show(); 

					}else if(str.equals("ERROR")){
						Toast.makeText(getApplicationContext(), "Lock Failed.",Toast.LENGTH_SHORT).show(); 

					}else
						Toast.makeText(getApplicationContext(), "Unknown",Toast.LENGTH_SHORT).show(); 
					
				    break;
			 }
		}

		private void getEditTextValue() {
			// TODO Auto-generated method stub
			if((mET01.getText().toString()).equals(""))
				mInt01 =0;
			else
			mInt01 = Integer.parseInt(mET01.getText().toString());
			
			if((mET02.getText().toString()).equals(""))
				mInt02 =0;
			else
			mInt02 = Integer.parseInt(mET02.getText().toString());
			
			if((mET03.getText().toString()).equals(""))
				mInt03 =0;
			else
			mInt03 = Integer.parseInt(mET03.getText().toString());
			
			if((mET04.getText().toString()).equals(""))
				mInt04 =0;
			else
			mInt04 = Integer.parseInt(mET04.getText().toString());
		}
	}


}

