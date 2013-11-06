/**
 * add by wangxiabin 09-15-11 for Wifi test
 */
package com.spreadtrum.android.eng;

import java.util.List;

import android.app.Activity;
import android.app.Dialog;
import android.app.ProgressDialog;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.net.wifi.ScanResult;
import android.net.wifi.WifiManager;
import android.os.Bundle;
import android.os.Debug;
import android.os.Handler;
import android.os.Message;
import android.widget.TextView;
import android.widget.Toast;

public class WifiTesting extends Activity {
    private static final boolean DEBUG = Debug.isDebug();
	private static final String TAG = "WifiTesting";
	private TextView wifiListTV;
	private TextView resultTV;
	private TextView statusTV;
	
	private WifiManager mWifiManager;
	
	private List<ScanResult> wifiList;
	private WifiReceiver wifiReceiver;
	private IntentFilter mIntentFilter;
	private Scanner mScanner;
	private boolean isWifiOff;
	private StringBuilder sb;
	private boolean isRestartWifi;
	private boolean isTesting;
	private static final int FINISH = 1;
	private Dialog dialog;

	/** Called when the activity is first created. */
	@Override
	public void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		setContentView(R.layout.wifitesting);

		wifiListTV = (TextView) findViewById(R.id.wifi_list);
		resultTV = (TextView) findViewById(R.id.result);
		statusTV = (TextView) findViewById(R.id.wifi_status);
		
		mWifiManager = (WifiManager) getSystemService(Context.WIFI_SERVICE);
		
		wifiReceiver = new WifiReceiver();
		mScanner = new Scanner();
		isTesting = true;
	}

	@Override
	protected void onPause() {
		if(wifiReceiver != null){
			unregisterReceiver(wifiReceiver);
		}
		if(isWifiOff){
			mWifiManager.setWifiEnabled(false);
		}
		mScanner.pause();
		super.onPause();
	}

	@Override
	protected void onResume() {
		mIntentFilter = new IntentFilter(WifiManager.WIFI_STATE_CHANGED_ACTION);
		mIntentFilter.addAction(WifiManager.SCAN_RESULTS_AVAILABLE_ACTION);
		registerReceiver(wifiReceiver, mIntentFilter);
		int wifiApState = mWifiManager.getWifiState();
		if (wifiApState == WifiManager.WIFI_STATE_ENABLING) {
			isWifiOff = false;
		}else if(wifiApState == WifiManager.WIFI_STATE_ENABLED){
			mWifiManager.setWifiEnabled(false);
			isRestartWifi = true;
			isWifiOff = false;
		}else if(wifiApState == WifiManager.WIFI_STATE_DISABLING){
			mWifiManager.setWifiEnabled(true);
			isWifiOff = false;
		}else{
			mWifiManager.setWifiEnabled(true);
			isWifiOff = true;
		}
		super.onResume();
	}

	private final class WifiReceiver extends BroadcastReceiver {
		@Override
		public void onReceive(Context context, Intent intent) {
			String action = intent.getAction();
            if (WifiManager.WIFI_STATE_CHANGED_ACTION.equals(action)) {
                handleWifiApStateChanged(intent.getIntExtra(
                        WifiManager.EXTRA_WIFI_STATE, WifiManager.WIFI_STATE_UNKNOWN));
            } else if (WifiManager.SCAN_RESULTS_AVAILABLE_ACTION.equals(action)) {
//            	Log.e(TAG, "-------------SCAN_RESULTS_AVAILABLE_ACTION");
            	updateAccessPoints();
            }
			
		}

	}
    private void handleWifiApStateChanged(int state) {
        switch (state) {
            case WifiManager.WIFI_STATE_ENABLING:
                statusTV.setText(R.string.wifi_starting);
                break;
            case WifiManager.WIFI_STATE_ENABLED:
            	statusTV.setText(R.string.wifi_start_scan);
            	mScanner.resume();
                break;
            case WifiManager.WIFI_STATE_DISABLING:
            	statusTV.setText(R.string.wifi_stopping);
                break;
            case WifiManager.WIFI_STATE_DISABLED:
            	statusTV.setText(R.string.wifi_stopped);
            	if(isRestartWifi){
            		mWifiManager.setWifiEnabled(true);
            		isRestartWifi = false;
            	}
                break;
            default:
            	resultTV.setText(R.string.wifi_error);
            	isTesting = false;
        }
    }
    
    private void updateAccessPoints() {
		sb = new StringBuilder();
		wifiList = mWifiManager.getScanResults();
		isTesting = false;
		if(wifiList != null && !wifiList.isEmpty()){
			ScanResult sr = wifiList.get(0);
			sb.append(new Integer(1).toString() + ".");
			sb.append("SSID:").append(sr.SSID).append("  level:").append(sr.level);
//			sb.append(sr.toString());
			wifiListTV.setText(sb.toString());
			resultTV.setText(R.string.wifi_pass);
			statusTV.setText("");
			
			if(wifiReceiver != null){
				unregisterReceiver(wifiReceiver);
				wifiReceiver = null;
			}
		}else{
			wifiListTV.setText("");
//			Log.e(TAG,"updateAccessPoints wifiList.isEmpty()");
			resultTV.setText(R.string.wifi_no_network);
		}
    }
    
    private class Scanner extends Handler {
        private int mRetry = 0;

        void resume() {
            if (!hasMessages(0)) {
                sendEmptyMessage(0);
            }
        }

        void pause() {
            mRetry = 0;
            removeMessages(0);
        }

        @Override
        public void handleMessage(Message message) {
//        	Log.e(TAG, "scanner handleMessage mRetry = "+mRetry);
            if (mWifiManager.startScanActive()) {
                mRetry = 0;
            } else if (++mRetry > 3) {
//            	Log.e(TAG, "scanner mWifiManager.startScanActive() = false mRetry="+mRetry);
                mRetry = 0;
                resultTV.setText(R.string.wifi_no_network);
                isTesting = false;
                return;
            }else{
//            	Log.e(TAG, "scanner sendEmptyMessageDelayed");
            	sendEmptyMessageDelayed(0, 6000);
            }
        }
    }
    
    @Override
    public void onBackPressed() {
    	if(isTesting){
    		Toast.makeText(this, R.string.wifi_istesting, Toast.LENGTH_SHORT).show();
    	}else{
    		dialog = ProgressDialog.show(this, getString(R.string.wifi_isclosing_tit), getString(R.string.wifi_isclosing));
    		if(wifiReceiver != null){
    			unregisterReceiver(wifiReceiver);
    			wifiReceiver = null;
    		}
    		if(isWifiOff){
    			mWifiManager.setWifiEnabled(false);
    		}
    		mScanner.pause();
    		Message msg = backHandler.obtainMessage();
    		msg.what = FINISH;
    		backHandler.sendMessageDelayed(msg, 5000);
    	}
    }
    
    private Handler backHandler = new Handler(){
    	public void handleMessage(Message msg) {
    		int w = msg.what;
    		if(w == FINISH){
    			dialog.dismiss();
    			finish();
    		}
    	}
    };
    
}
