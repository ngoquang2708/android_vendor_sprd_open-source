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

public class cellinfo extends Activity {
    private static final boolean DEBUG = Debug.isDebug();
	private static final String LOG_TAG = "engnetinfo";
	private int sockid = 0;
	private engfetch mEf;
	private String str=null;
	private EventHandler mHandler;
	public TextView  tv;
	
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

	mEf = new engfetch();
	sockid = mEf.engopen();

	Looper looper;
	looper = Looper.myLooper();
	mHandler = new EventHandler(looper);
	mHandler.removeMessages(0);

	Message m = mHandler.obtainMessage(engconstents.ENG_AT_CURRENT_BAND, 0, 0, 0);

	mHandler.sendMessage(m);

	tv = new TextView(this);
	tv.setText("");
	setContentView(tv);

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
		    case engconstents.ENG_AT_CURRENT_BAND:
			    ByteArrayOutputStream outputBuffer = new ByteArrayOutputStream();
			    DataOutputStream outputBufferStream = new DataOutputStream(outputBuffer);

			    if(DEBUG) Log.d(LOG_TAG, "engopen sockid=" + sockid);
			    String strtemp = new String("?");
    /*Modify 20130205 Spreadst of 125480 change the method of creating cmd start*/
                //str=String.format("%d,%s", msg.what,strtemp);
                str=new StringBuilder().append(msg.what).append(",").append(strtemp).toString();
     /*Modify 20130205 Spreadst of 125480 change the method of creating cmd end*/
			    try {
			    	//outputBufferStream.writeBytes("pcclient");
			    	outputBufferStream.writeBytes(str);
			    } catch (IOException e) {
			        //Slog.e(TAG, "Unable to write package deletions!");
			    	Log.e(LOG_TAG, "writebytes error");
			       return;
			    }
			    mEf.engwrite(sockid,outputBuffer.toByteArray(),outputBuffer.toByteArray().length);

			    int dataSize = 128;
			    byte[] inputBytes = new byte[dataSize];
			    int showlen= mEf.engread(sockid,inputBytes,dataSize);
			    String str =new String(inputBytes,0,showlen,Charset.defaultCharset());
			    tv.setText(	str	);
			    break;
		 }
	}
}


}

