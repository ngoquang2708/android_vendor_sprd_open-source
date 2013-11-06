package com.spreadtrum.android.eng;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.ProgressDialog;
import android.os.Bundle;
import android.os.Debug;
import android.os.Handler;
import android.os.Message;
import android.os.SystemProperties;
import android.util.Log;
import android.view.View;
import android.view.View.OnClickListener;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.Toast;

public class BtEUT extends Activity implements OnClickListener{
    private static final boolean DEBUG = Debug.isDebug();
	public static final String TAG = "BtEUT";
	public static final int START_BT = 1;
	public static final int STOP_BT = 2;
	public static final int DEINIT_FLAG_NOUI = 3;

	private Button swicthBtn;

	private EngWifieut engWifieut;

	private AlertDialog dialog;
	private ProgressDialog progressDialog;
	//private String notnum = " is not a num";
	//private String title = "Test Result";
	private boolean mHaveFinish = true;

	private boolean isTesting;

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		setContentView(R.layout.bt_eut);
		mHaveFinish = false;
		getWindow().setSoftInputMode(
				WindowManager.LayoutParams.SOFT_INPUT_ADJUST_PAN);
		initui();
		try {
			Thread.sleep(500);
		} catch (InterruptedException e) {
			e.printStackTrace();
		}
		SystemProperties.set("ctl.start", "enghardwaretest");
	}

	private void initui() {
		setTitle(R.string.btut_test);
		swicthBtn = (Button) findViewById(R.id.bt_eut_switch);
		swicthBtn.setOnClickListener(this);
	}

	public Handler handler = new Handler() {

		public void handleMessage(android.os.Message msg) {
			if (progressDialog != null) {
				progressDialog.dismiss();
			}
			int what = msg.what;
			int arg = msg.arg1;
			String message = arg == 0 ? "Success" : "Fail";

			switch (what) {
			case START_BT:
				if (arg != 0) {
					showDialog("init fail", "init");
				}else{
					swicthBtn.setText(R.string.wifi_eut_stop);
					isTesting = true;
				}
				break;
			case STOP_BT:
				if (arg != 0) {
					showDialog("stop fail", "stop");
				}else{
					swicthBtn.setText(R.string.wifi_eut_start);
					isTesting = false;
				}
				break;
			case DEINIT_FLAG_NOUI:
					isTesting = false;
					finish();
				break;
			default:
				break;
			}
		}
	};

	@Override
	public void onClick(View v) {
		
		if(v == swicthBtn){
			if(isTesting){
				stopTest();
			}else{
				if(engWifieut == null){
					engWifieut = new EngWifieut();
				}
				startTest();
			}
		}
	}

	

	@Override
	protected void onStart() {
		mHaveFinish = false;
		super.onStart();
	}

	@Override
	protected void onStop() {
		if (progressDialog != null) {
			progressDialog.dismiss();
			progressDialog = null;
		}
		if (dialog != null) {
			dialog.dismiss();
			dialog = null;
		}
		mHaveFinish = true;
		super.onStop();
	}
	
	@Override
	protected void onDestroy() {
		deinitTestNoUi();
		super.onDestroy();
		SystemProperties.set("ctl.stop", "enghardwaretest");
	}

	private void showMessage(String msg) {
		Toast.makeText(this, msg, Toast.LENGTH_LONG).show();
	}

	private void showDialog(String msg, String title) {
		if(mHaveFinish){
			if(DEBUG) Log.d(TAG, "activity have finished !");
			return;
		}
		AlertDialog.Builder builder = new AlertDialog.Builder(BtEUT.this);
		dialog = builder.setTitle(title).setMessage(msg).setNegativeButton(
				"Sure", null).create();
		dialog.show();
	}

	private void startTest() {
		progressDialog = ProgressDialog.show(BtEUT.this, "init",
				"please wait, initing...");
		new Thread(new Runnable() {

			@Override
			public void run() {
				try {
					Thread.sleep(500);
				} catch (InterruptedException e) {
					e.printStackTrace();
				}
				int re = engWifieut.testBtStart();
				Message msg = handler.obtainMessage();
				msg.what = START_BT;
				msg.arg1 = re;
				msg.sendToTarget();
			}
		}).start();
	}

	private void stopTest() {
		progressDialog = ProgressDialog.show(BtEUT.this, "stop",
		"please wait, stoping...");
		if(isTesting){
			new Thread(new Runnable() {
				
				@Override
				public void run() {
					try {
						Thread.sleep(500);
					} catch (InterruptedException e) {
						e.printStackTrace();
					}
					int re = engWifieut.testBtStop();
					Message msg = handler.obtainMessage();
					msg.what = STOP_BT;
					msg.arg1 = re;
					msg.sendToTarget();
					
				}
			}).start();
		}
	}

	private void deinitTestNoUi(){
		if(isTesting){
			new Thread(new Runnable() {
				
				@Override
				public void run() {
					engWifieut.testBtStop();
				}
			}).start();
		}
		
	}

	@Override
	public void onBackPressed() {
		if(isTesting){
			if (engWifieut != null) {
				progressDialog = ProgressDialog.show(BtEUT.this, "stop",
				"please don't force colse, stoping...");
					new Thread(new Runnable() {
						
						@Override
						public void run() {
							try {
								Thread.sleep(500);
							} catch (InterruptedException e) {
								e.printStackTrace();
							}
							int re = engWifieut.testBtStop();
							Message msg = handler.obtainMessage();
							msg.what = DEINIT_FLAG_NOUI;
							msg.arg1 = re;
							msg.sendToTarget();
							
						}
					}).start();
			}
		}else {
			super.onBackPressed();
		}
	}
}
