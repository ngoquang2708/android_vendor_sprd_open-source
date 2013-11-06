package com.spreadtrum.android.eng;

import android.app.Activity;
import android.os.Bundle;
import android.os.Debug;
import android.util.Log;
import android.webkit.WebView;
import android.widget.TextView;

public class uaagent extends Activity {
    private static final boolean DEBUG = Debug.isDebug();
    private static final String LOG_TAG = "uaagent";
    private TextView tv = null;
    private WebView mWebView = null;
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.uaagent);
        tv = (TextView)findViewById(R.id.uaagent);
        mWebView = new WebView(this);
        String mUserAgent =mWebView.getSettings().getUserAgentString();
        if(DEBUG) Log.d(LOG_TAG, "UserAgent is <"+mUserAgent+">");
        tv.setText(mUserAgent);
    }
}

