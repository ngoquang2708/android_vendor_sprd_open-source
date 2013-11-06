package com.spreadtrum.android.eng;

import android.app.Activity;
import android.os.Build;
import android.os.Bundle;
import android.widget.TextView;
import android.os.SystemProperties;

public class VersionInfoCustomer extends Activity{
	private TextView tv=null;
	public static final String DEFAULT_INFO = "4G_W4_TD_MocoDroid2.2_W11.xx";
    public static final String CUSTOMINFO = SystemProperties.get("ro.build.display.spid", DEFAULT_INFO);
	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		setContentView(R.layout.versioninfocustomer);
		tv = (TextView) findViewById(R.id.sprdversioninfo);
		tv.setText(CUSTOMINFO);
	}

}
