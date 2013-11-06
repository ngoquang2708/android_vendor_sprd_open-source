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
import android.text.method.NumberKeyListener;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.Toast;

public class sendgprsdata extends Activity {
    private static final boolean DEBUG = Debug.isDebug();
	private static final String LOG_TAG = "sendgprsdata";
	private int sockid = 0;
	private engfetch mEf;
	private String str=null;
	private EventHandler mHandler;
	private EditText  mET01;
	private EditText  mET02;
	private int  mInt01;

	private Button mButton;
	private Button mButton01;
	
	private String strInput="";
	
	private boolean bHasContent;
	@Override
	protected void onCreate(Bundle savedInstanceState) {
	super.onCreate(savedInstanceState);
	setContentView(R.layout.sendgprsdata);
	initialpara();

	}

	private void initialpara() {
		// TODO Auto-generated method stub
	    	bHasContent = true;
	    	mET01 = (EditText)findViewById(R.id.editText1);	
	    	mET02 = (EditText)findViewById(R.id.editText2);	
	    	mET01.setKeyListener(numberKeyListener);
	    	mET02.setKeyListener(numberKeyListener);
	    	clearEditText();
	    	mButton = (Button)findViewById(R.id.send_button);	
	    	mButton01 = (Button)findViewById(R.id.clear_button1);	
	    	mButton.setText("Send Data");
	    	mButton01.setText("Clear Data");
	 
	    	mEf = new engfetch();
	    	sockid = mEf.engopen();
	    	Looper looper;
	    	looper = Looper.myLooper();
	    	mHandler = new EventHandler(looper);
	    	mHandler.removeMessages(0);
	    	
		mButton.setOnClickListener(new Button.OnClickListener(){

				public void onClick(View v) {
					// TODO Auto-generated method stub
					Message m = mHandler.obtainMessage(engconstents.ENG_AT_SGPRSDATA, 0, 0, 0);
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
		}

	private class EventHandler extends Handler
	{
		public EventHandler(Looper looper) {
		    super(looper);
		}

		@Override
		public void handleMessage(Message msg) {
			 switch(msg.what){
			    case engconstents.ENG_AT_SGPRSDATA:

				ByteArrayOutputStream outputBuffer = new ByteArrayOutputStream();
				DataOutputStream outputBufferStream = new DataOutputStream(outputBuffer);

				if(!getEditTextValue()) return;

				if(DEBUG) Log.d(LOG_TAG, "engopen sockid=" + sockid);
				if(bHasContent){
    /*Modify 20130205 Spreadst of 125480 change the method of creating cmd start*/
                //str=String.format("%d,%d,%d,%d,%s", msg.what,3,100,1,strInput);
                str=new StringBuilder().append(msg.what).append(",").append(3).append(",")
                    .append(100).append(",").append(1).append(",").append(strInput).toString();
				}else{
                //str=String.format("%d,%d,%d", msg.what,1,mInt01);
                str=new StringBuilder().append(msg.what).append(",").append(1).append(",")
                    .append(mInt01).toString();
    /*Modify 20130205 Spreadst of 125480 change the method of creating cmd end*/
				}
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
					Toast.makeText(getApplicationContext(), "Send Success.",Toast.LENGTH_SHORT).show(); 

				}else if(str.equals("ERROR")){
					Toast.makeText(getApplicationContext(), "Send Failed.",Toast.LENGTH_SHORT).show(); 

				}
				else
					Toast.makeText(getApplicationContext(), "Unknown",Toast.LENGTH_SHORT).show(); 
				
				break;
		 }
	}

		private boolean getEditTextValue() {
			// TODO Auto-generated method stub
			if((mET01.getText().toString()).equals("")){
				mInt01 =0;
			}else{
				try{
					mInt01 = Integer.parseInt(mET01.getText().toString());
				}catch(NumberFormatException nfe){
					mInt01 = 0;
					mET01.selectAll();
					DisplayToast("data number is not a valid number");
					return false;
				}
			}

			if((mET02.getText().toString()).equals("")){
				bHasContent=false;
			}else{
				strInput = mET02.getText().toString();
			}
			return true;
		}
	}

	private void DisplayToast(String str) {
		Toast mToast = Toast.makeText(this, str, Toast.LENGTH_SHORT);
		mToast.setGravity(Gravity.BOTTOM, 0, 100);
		mToast.show();
	}
	
    private NumberKeyListener numberKeyListener = new NumberKeyListener() {
        private char[] numberChars = {
				 '0', '1', '2', '3', '4', '5', '6', '7', '8', '9'
        };

        public int getInputType() {
            return android.text.InputType.TYPE_CLASS_PHONE;
        }

        protected char[] getAcceptedChars() {
            return numberChars;
        }
    };


}

