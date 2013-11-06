package com.spreadtrum.android.eng;

import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.util.HashMap;
import java.util.Map;
import java.util.Set;

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
import android.view.View.OnFocusChangeListener;
import android.view.WindowManager;
import android.widget.AdapterView;
import android.widget.AdapterView.OnItemSelectedListener;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.Spinner;
import android.widget.Toast;

public class WifiEUT extends Activity implements OnClickListener,
		OnItemSelectedListener, OnFocusChangeListener {
    private static final boolean DEBUG = Debug.isDebug();
	public static final String TAG = "WifiEUT";
	public static final int CW_FLAG = 1;
	public static final int TX_FLAG = 2;
	public static final int RX_FLAG = 3;
	public static final int INIT_FLAG = 4;
	public static final int DEINIT_FLAG = 5;
	public static final int DEINIT_FLAG_NOUI = 6;
	private Spinner band;
	private Spinner channel;
	private EditText sFactor;
	private EditText frequency;
	private EditText frequencyOffset;
	private Spinner amplitude;
	private Spinner rate;
	private Spinner powerLevel;
	private EditText length;
	private CheckBox enablelbCsTestMode;
	private EditText interval;
	private EditText destMacAddr;
	private Spinner preamble;
	private CheckBox filteringEnable;
	private EditText frequencyRx;

	private Button swicthBtn;
	private Button cwBtn;
	private Button txBtn;
	private Button rxBtn;

	private EngWifieut engWifieut;
	private EngWifieut.PtestCw cw;
	private EngWifieut.PtestTx tx;
	private EngWifieut.PtestRx rx;

	private AlertDialog dialog;
	private ProgressDialog progressDialog;
	private Map<String, String> errorMap;
	private String notnum = " is not a num";
	private String title = "Test Result";
	private boolean mHaveFinish = true;

	private boolean isTesting;
	private int sockid;

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		setContentView(R.layout.wifi_eut);
		mHaveFinish = false;
		getWindow().setSoftInputMode(
				WindowManager.LayoutParams.SOFT_INPUT_ADJUST_PAN);
		errorMap = new HashMap<String, String>();
		initui();
		initEvent();
		disableView();
		try {
			Thread.sleep(500);
		} catch (InterruptedException e) {
			e.printStackTrace();
		}
		//startcmd("WIFI");
		SystemProperties.set("ctl.start", "enghardwaretest");
	}

	private void initui() {
		band = (Spinner) findViewById(R.id.wifi_eut_band);
		channel = (Spinner) findViewById(R.id.wifi_eut_channel);
		sFactor = (EditText) findViewById(R.id.wifi_eut_sfactor);
		frequency = (EditText) findViewById(R.id.wifi_eut_frequency);
		frequencyOffset = (EditText) findViewById(R.id.wifi_eut_frequencyOffset);
		amplitude = (Spinner) findViewById(R.id.wifi_eut_amplitude);
		rate = (Spinner) findViewById(R.id.wifi_eut_rate);
		powerLevel = (Spinner) findViewById(R.id.wifi_eut_powerLevel);
		length = (EditText) findViewById(R.id.wifi_eut_length);
		enablelbCsTestMode = (CheckBox) findViewById(R.id.wifi_eut_enablelbCsTestMode);
		interval = (EditText) findViewById(R.id.wifi_eut_interval);
		destMacAddr = (EditText) findViewById(R.id.wifi_eut_destMacAddr);
		preamble = (Spinner) findViewById(R.id.wifi_eut_preamble);
		filteringEnable = (CheckBox) findViewById(R.id.wifi_eut_filteringEnable);
		frequencyRx = (EditText) findViewById(R.id.wifi_eut_frequency_rx);
		cwBtn = (Button) findViewById(R.id.wifi_eut_cw);
		txBtn = (Button) findViewById(R.id.wifi_eut_tx);
		rxBtn = (Button) findViewById(R.id.wifi_eut_rx);
		swicthBtn = (Button) findViewById(R.id.wifi_eut_switch);

		ArrayAdapter<String> adapter = new ArrayAdapter<String>(this,
				android.R.layout.simple_spinner_item, getResources()
						.getStringArray(R.array.band_arr));
		adapter
				.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
		band.setAdapter(adapter);
		band.setPrompt("band");

		ArrayAdapter<String> amplitudeAdapter = new ArrayAdapter<String>(this,
				android.R.layout.simple_spinner_item, getResources()
						.getStringArray(R.array.amplitude_arr));
		amplitudeAdapter
				.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
		amplitude.setAdapter(amplitudeAdapter);
		amplitude.setPrompt("amplitude");

		ArrayAdapter<String> powerLevelAdapter = new ArrayAdapter<String>(this,
				android.R.layout.simple_spinner_item, getResources()
						.getStringArray(R.array.power_level_arr));
		powerLevelAdapter
				.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
		powerLevel.setAdapter(powerLevelAdapter);
		powerLevel.setPrompt("power level");

		ArrayAdapter<String> preambleAdapter = new ArrayAdapter<String>(this,
				android.R.layout.simple_spinner_item, getResources()
						.getStringArray(R.array.preamble_arr));
		preambleAdapter
				.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
		preamble.setAdapter(preambleAdapter);
		preamble.setPrompt("preamble");

		ArrayAdapter<String> rateAdapter = new ArrayAdapter<String>(this,
				android.R.layout.simple_spinner_item, getResources()
						.getStringArray(R.array.rate_str_arr));
		rateAdapter
				.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
		rate.setAdapter(rateAdapter);
		rate.setPrompt("rate");
		
		ArrayAdapter<String> channelAdapter = new ArrayAdapter<String>(this,
				android.R.layout.simple_spinner_item, getResources()
						.getStringArray(R.array.channel_arr));
		channelAdapter
				.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
		channel.setAdapter(channelAdapter);
		channel.setPrompt("channnel");

		sFactor.setEnabled(false);
		swicthBtn.setOnClickListener(this);

		channel.setSelection(6);
		rate.setSelection(11);
		powerLevel.setSelection(6);
		
	}

	private void initEvent() {
		cwBtn.setOnClickListener(this);
		txBtn.setOnClickListener(this);
		rxBtn.setOnClickListener(this);

		band.setOnItemSelectedListener(this);

		channel.setOnFocusChangeListener(this);// addTextChangedListener(this);
		sFactor.setOnFocusChangeListener(this);
		frequency.setOnFocusChangeListener(this);
		frequencyOffset.setOnFocusChangeListener(this);
		// rate.addTextChangedListener(this);
		length.setOnFocusChangeListener(this);
		interval.setOnFocusChangeListener(this);
		// destMacAddr.addTextChangedListener(this);
		frequencyRx.setOnFocusChangeListener(this);
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
			case CW_FLAG:
				showDialog(message, title);
				break;
			case TX_FLAG:
				showDialog(message, title);
				break;
			case RX_FLAG:
				showDialog(message, title);
				break;
			case INIT_FLAG:
				if (arg != 0) {
					showDialog("init fail", "init");
				}else{
					enableView();
					swicthBtn.setText(R.string.wifi_eut_stop);
					isTesting = true;
				}
				break;
			case DEINIT_FLAG:
				if(arg == 0){
					disableView();
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
		
		if (v == cwBtn) {
			if (haveNoErr()) {
				cw = new EngWifieut.PtestCw();
				cw.band = band.getSelectedItemPosition() + 1;
				cw.channel = channel.getSelectedItemPosition()+1;
				cw.sFactor = Utils
						.parseInt(sFactor.getText().toString().trim());
				cw.frequency = Utils.parseInt(frequency.getText().toString()
						.trim());
				cw.frequencyOffset = Utils.parseInt(frequencyOffset.getText()
						.toString().trim());
				cw.amplitude = amplitude.getSelectedItemPosition();

				progressDialog = ProgressDialog.show(WifiEUT.this, "CW Test",
						"Testing...");
				new Thread(new Runnable() {

					@Override
					public void run() {
						int re = engWifieut.testCw(cw);
						Message msg = handler.obtainMessage();
						msg.what = CW_FLAG;
						msg.arg1 = re;
						msg.sendToTarget();
					}
				}).start();
			}

		} else if (v == txBtn) {
			int[] list = getResources().getIntArray(R.array.rate_int_arr);

			if (haveNoErr()) {
				tx = new EngWifieut.PtestTx();
				tx.band = band.getSelectedItemPosition() + 1;
				tx.channel = channel.getSelectedItemPosition()+1;//Utils.parseInt(channel.getText().toString().trim());
				tx.sFactor = Utils
						.parseInt(sFactor.getText().toString().trim());
				tx.rate = list[rate.getSelectedItemPosition()];// Utils.parseInt(rate.getText().toString());
				tx.powerLevel = powerLevel.getSelectedItemPosition()+1;
				tx.length = Utils.parseInt(length.getText().toString().trim());
				tx.enablelbCsTestMode = enablelbCsTestMode.isChecked() ? 1 : 0;
				tx.interval = Utils.parseInt(interval.getText().toString()
						.trim());

				tx.destMacAddr = destMacAddr.getText().toString().trim()
						.replace("-", "").replace(" ", "").replace(":", "");
				tx.preamble = preamble.getSelectedItemPosition();// Utils.parseInt(preamble.getText().toString());
				progressDialog = ProgressDialog.show(WifiEUT.this, "TX Test",
						"Testing...");
				new Thread(new Runnable() {

					@Override
					public void run() {
						int re;
						re = engWifieut.testTx(tx);
						Message msg = handler.obtainMessage();
						msg.what = TX_FLAG;
						msg.arg1 = re;
						msg.sendToTarget();

					}
				}).start();
			}

		} else if (v == rxBtn) {

			if (haveNoErr()) {
				rx = new EngWifieut.PtestRx();
				rx.band = band.getSelectedItemPosition() + 1;
				rx.channel = channel.getSelectedItemPosition()+1;//Utils.parseInt(channel.getText().toString().trim());
				rx.sFactor = Utils
						.parseInt(sFactor.getText().toString().trim());
				rx.frequency = Utils.parseInt(frequencyRx.getText().toString()
						.trim());
				rx.filteringEnable = filteringEnable.isChecked() ? 1 : 0;
				progressDialog = ProgressDialog.show(WifiEUT.this, "RX Test",
						"Testing...");
				new Thread(new Runnable() {

					@Override
					public void run() {
						int re = engWifieut.testRx(rx);
						Message msg = handler.obtainMessage();
						msg.what = RX_FLAG;
						msg.arg1 = re;
						msg.sendToTarget();
					}
				}).start();
			}
		}else if(v == swicthBtn){
			
			if(isTesting){
				deinitTest();
			}else{
				if(engWifieut == null){
					engWifieut = new EngWifieut();
				}
				initTest();
			}
		}
	}

	
	@Override
	public void onItemSelected(AdapterView<?> parent, View view, int position,
			long id) {
		if (position == 1) {
			sFactor.setEnabled(true);
		} else {
			sFactor.setEnabled(false);
		}

	}

	@Override
	public void onNothingSelected(AdapterView<?> parent) {
		sFactor.setEnabled(false);
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
//		deinitTestNoUi();
		super.onStop();
	}
	
	@Override
	protected void onDestroy() {
		deinitTestNoUi();
		//startcmd("STOP");
		SystemProperties.set("ctl.stop", "enghardwaretest");
		super.onDestroy();
	}

	private boolean haveNoErr() {
		Set<String> set = errorMap.keySet();
//		Log.e(TAG, "err num=" + set.size());
		if (set.isEmpty()) {
			return true;
		} else {
			StringBuffer err = new StringBuffer();
			for (String s : set) {
				err.append(errorMap.get(s) + "\n");
			}
			showMessage(err.toString());
			return false;
		}
	}

	private void showMessage(String msg) {
		Toast.makeText(this, msg, Toast.LENGTH_LONG).show();
	}

	private void showDialog(String msg, String title) {
		if(mHaveFinish){
			if(DEBUG) Log.d(TAG, "activity have finished !");
			return;
		}
		AlertDialog.Builder builder = new AlertDialog.Builder(WifiEUT.this);
		dialog = builder.setTitle(title).setMessage(msg).setNegativeButton(
				"Sure", null).create();
		dialog.show();
	}

	private void betweenNum(int a, int b, int v, String key, int resid) {
		if (v >= a && v <= b) {
			if (errorMap.containsKey(key))
				errorMap.remove(key);
		} else {
			errorMap.put(key, getString(resid));
			Toast.makeText(this, getString(resid), Toast.LENGTH_LONG).show();
		}
	}

	private boolean checkIsInt(String str, String key) {

		if ("".equals(str)) {
			if (errorMap.containsKey(key))
				errorMap.remove(key);
			return false;
		}
		if (!Utils.isInt(str)) {
			Toast.makeText(this, key + notnum, Toast.LENGTH_LONG).show();
			errorMap.put(key, key + notnum);
			return false;
		}
		return true;
	}

	@Override
	public void onFocusChange(View v, boolean hasFocus) {
		if (!hasFocus) {
			EditText et = (EditText) v;
			String str = et.getText().toString().trim();

			int n;

			if (v == channel) {
				if (checkIsInt(str, "channel")) {
					n = Integer.parseInt(str);
					if (band.getSelectedItemPosition() == 0) {
						betweenNum(1, 14, n, "channel",
								R.string.wifi_eut_band_err1);
					} else if (band.getSelectedItemPosition() == 1) {
						betweenNum(1, 200, n, "channel",
								R.string.wifi_eut_band_err2);
					}
				}
			} else if (v == sFactor) {
				if (checkIsInt(str, "sFactor")) {
					n = Integer.parseInt(str);
					if (n % 500 == 0) {
						betweenNum(8000, 10000, n, "sFactor",
								R.string.wifi_eut_sFactor_err);
					} else {
						errorMap.put("sFactor",
								getString(R.string.wifi_eut_sFactor_err));
						Toast.makeText(this,
								getString(R.string.wifi_eut_sFactor_err),
								Toast.LENGTH_LONG).show();
					}
				}
			} else if (v == frequency) {
				if (checkIsInt(str, "frequency")) {
					n = Integer.parseInt(str);
					if (n != 0) {
						betweenNum(1790, 6000, n, "frequency",
								R.string.wifi_eut_frequency_err);
					}
				}
			} else if (v == frequencyOffset) {
				if (checkIsInt(str, "frequencyOffset")) {
					n = Integer.parseInt(str);
					betweenNum(-20000, 20000, n, "frequencyOffset",
							R.string.wifi_eut_frequencyOffset_err);
				}
			} else if (v == length) {
				if (checkIsInt(str, "length")) {
					n = Integer.parseInt(str);
					betweenNum(0, 2304, n, "length",
							R.string.wifi_eut_length_err);
				}
			} else if (v == interval) {
				checkIsInt(str, "interval");
			} else if (v == frequencyRx) {
				if (checkIsInt(str, "frequencyRx")) {
					n = Integer.parseInt(str);
					if (n != 0) {
						betweenNum(1790, 6000, n, "frequency",
								R.string.wifi_eut_frequency_err);
					}
				}
			}

		}

	}

	private void initTest() {
		progressDialog = ProgressDialog.show(WifiEUT.this, "init",
				"please wait, initing...");
		new Thread(new Runnable() {

			@Override
			public void run() {
				try {
					Thread.sleep(500);
				} catch (InterruptedException e) {
					e.printStackTrace();
				}
				int re = engWifieut.testInit();
				Message msg = handler.obtainMessage();
				msg.what = INIT_FLAG;
				msg.arg1 = re;
				msg.sendToTarget();
			}
		}).start();
	}

	private void deinitTest() {
		progressDialog = ProgressDialog.show(WifiEUT.this, "stop",
		"please wait, stoping...");
		if(isTesting){
			new Thread(new Runnable() {
				
				@Override
				public void run() {
					int re = engWifieut.testDeinit();
					Message msg = handler.obtainMessage();
					msg.what = DEINIT_FLAG;
					msg.arg1 = re;
					msg.sendToTarget();
					
				}
			}).start();
		}
	}
	private void deinitTestNoUi(){
		if(engWifieut != null)engWifieut.testSetValue(0);
		//Log.e(TAG, "testSetValue 0");
		if(isTesting){
			new Thread(new Runnable() {
				
				@Override
				public void run() {
					engWifieut.testDeinit();
				}
			}).start();
		}
		
	}
	private void enableView(){
		band.setEnabled(true);
		channel.setEnabled(true);
		sFactor.setEnabled(true);
		frequency.setEnabled(true);
		frequencyOffset.setEnabled(true);
		amplitude.setEnabled(true);
		rate.setEnabled(true);
		powerLevel.setEnabled(true);
		length.setEnabled(true);
		enablelbCsTestMode.setEnabled(true);
		interval.setEnabled(true);
		destMacAddr.setEnabled(true);
		preamble.setEnabled(true);
		filteringEnable.setEnabled(true);
		frequencyRx.setEnabled(true);
		cwBtn.setEnabled(true);
		txBtn.setEnabled(true);
		rxBtn.setEnabled(true);
	}
	private void disableView(){
		band.setEnabled(false);
		channel.setEnabled(false);
		sFactor.setEnabled(false);
		frequency.setEnabled(false);
		frequencyOffset.setEnabled(false);
		amplitude.setEnabled(false);
		rate.setEnabled(false);
		powerLevel.setEnabled(false);
		length.setEnabled(false);
		enablelbCsTestMode.setEnabled(false);
		interval.setEnabled(false);
		destMacAddr.setEnabled(false);
		preamble.setEnabled(false);
		filteringEnable.setEnabled(false);
		frequencyRx.setEnabled(false);
		cwBtn.setEnabled(false);
		txBtn.setEnabled(false);
		rxBtn.setEnabled(false);
	}
	
	private void startcmd(String cmd){
		engfetch mEf = new engfetch();
		sockid = mEf.engopen();
		
		ByteArrayOutputStream outputBuffer = new ByteArrayOutputStream();
		DataOutputStream outputBufferStream = new DataOutputStream(outputBuffer);

/*Modify 20130205 Spreadst of 125480 change the method of creating cmd start*/
        //String str=String.format("%s%s", "CMD:",cmd);
        String str = new StringBuilder().append("CMD:").append(cmd).toString();
/*Modify 20130205 Spreadst of 125480 change the method of creating cmd end*/
		try {
			outputBufferStream.writeBytes(str);
		} catch (IOException e) {
			Log.e(TAG, "writebytes error");
		   return;
		}
		mEf.engwrite(sockid,outputBuffer.toByteArray(),outputBuffer.toByteArray().length);
		byte[] data;
//		mEf.engread(sockid, data, size);
		//Log.e(TAG, "startcmd---------engwrite cmd = " + cmd);
		mEf.engclose(sockid);
		//Log.e(TAG, "startcmd---------colse sockid");
	}
	
	@Override
	public void onBackPressed() {
		if(isTesting){
			if (engWifieut != null) {
				progressDialog = ProgressDialog.show(WifiEUT.this, "stop",
				"please don't force colse, stoping...");
					new Thread(new Runnable() {
						
						@Override
						public void run() {
							int re = engWifieut.testDeinit();
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
// add by wangxiaoibin 2011-10-04 for wifi eut 
