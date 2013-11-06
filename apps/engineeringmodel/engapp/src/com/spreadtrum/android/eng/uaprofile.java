package com.spreadtrum.android.eng;

import android.app.Activity;
import android.widget.TextView;
import android.os.Bundle;


public class uaprofile extends Activity {

	private TextView  mTextView;
	
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
	        setContentView(R.layout.uaagent);
	        mTextView = (TextView)findViewById(R.id.uaagent);
	        //String mUserAgent = WebView.getSettings().getUserAgentString()
	        mTextView.setText(getUserProfile());
    }

	private String getUserProfile() {
		// TODO Auto-generated method stub
		return "ANDROID 2.2 MASTONE_TD300_TD/1.0 ThreadX/4.0 SPRD_MOCOR/8800H5 Release/20.03.2011 Browser/NF3.5 Profile/MIDP-2.0 Configuration/CLDC-1.1";
		
	}


}
