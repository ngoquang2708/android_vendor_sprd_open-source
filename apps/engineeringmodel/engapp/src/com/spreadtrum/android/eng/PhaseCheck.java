package com.spreadtrum.android.eng;

import android.widget.TextView;
import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.os.Message;

import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.nio.charset.Charset;
import android.util.Log;


public class PhaseCheck extends Activity
{
    static final String LogTag = "PhaseCheck";

    private TextView mTextView;
    private String mText;

    private engfetch mEf;

    /** Called when the activity is first created. */
    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.phasecheck);
        mTextView = (TextView) findViewById(R.id.text_view);

        mEf = new engfetch();
        
        int dataSize = 2048;
        byte[] inputBytes = new byte[dataSize];

        int showlen= mEf.enggetphasecheck(inputBytes, dataSize);
        mText =  new String(inputBytes, 0, showlen,Charset.defaultCharset());
        String str = getIntent().getStringExtra("textFilter");
        if(str != null && str.equals("filter"))
        {//filter sn1/sn2
          String mTextFilter =mText.replaceAll("(?s)DOWNLOAD.*", "").trim();
          mTextView.setText(mTextFilter);
        }else{
           mTextView.setText(mText);
        }
    } 
}


