package com.spreadtrum.android.eng;

import android.app.Activity;
import android.os.Build;
import android.os.Bundle;
import android.widget.TextView;

public class BuildNumber extends Activity{
	private TextView tv=null;
	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		setContentView(R.layout.buildnum);
		tv = (TextView) findViewById(R.id.buildnuminfo);
		tv.setText(Build.DISPLAY);
		
	}

}
