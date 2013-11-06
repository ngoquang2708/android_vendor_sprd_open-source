package com.spreadtrum.android.eng;

import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.nio.charset.Charset;
import java.util.HashMap;
import java.util.Iterator;
import java.util.Map.Entry;
import java.util.Set;

import android.app.AlertDialog.Builder;
import android.content.DialogInterface;
import android.content.Intent;
import android.os.Bundle;
import android.os.Debug;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.Looper;
import android.os.Message;
import android.os.SystemProperties;
import android.preference.CheckBoxPreference;
import android.preference.Preference;
import android.preference.PreferenceActivity;
import android.preference.PreferenceScreen;
import android.util.Log;
import android.widget.Toast;


class BandSelectDecoder {
    private static BandSelectDecoder mInstance;
    private HashMap<Integer,Integer>mDecoderMap; //<CMD,BANDS>

    public static final int GSM900 = 1;
    public static final int DCS1800 = 2;
    public static final int PCS1900 = 4;
    public static final int GSM850 = 8;


    private BandSelectDecoder() {
        mDecoderMap = new HashMap<Integer,Integer>();
        mDecoderMap.put(0,GSM900);
        mDecoderMap.put(1,DCS1800);
        mDecoderMap.put(2,PCS1900);
        mDecoderMap.put(3,GSM850);
        mDecoderMap.put(4,GSM900|DCS1800);
        mDecoderMap.put(5,GSM850|GSM900);
        mDecoderMap.put(6,GSM850|DCS1800);
        mDecoderMap.put(7,GSM850|PCS1900);
        mDecoderMap.put(8,GSM900|PCS1900);
        mDecoderMap.put(9,GSM850|GSM900|DCS1800);
        mDecoderMap.put(10,GSM850|GSM900|PCS1900);
        mDecoderMap.put(11,DCS1800|PCS1900);
        mDecoderMap.put(12,GSM850|DCS1800|PCS1900);
        mDecoderMap.put(13,GSM900|DCS1800|PCS1900);
        mDecoderMap.put(14,GSM850|GSM900|DCS1800|PCS1900);
    }

    public static BandSelectDecoder getInstance(){
        if(mInstance == null) {
            mInstance = new BandSelectDecoder();
        }

        return mInstance;
    }

    public int getBandsFromCmdParam(int cmd) {
        return mDecoderMap.get(cmd);
    }

    public int getCmdParamFromBands(int bands) {
        Set<Entry<Integer, Integer>> set = mDecoderMap.entrySet();
        Iterator<Entry<Integer, Integer>> itor = set.iterator();
        while(itor.hasNext())
        {
            Entry<Integer, Integer> entry = itor.next();
            if(entry.getValue() == bands) {
                  return entry.getKey();
            }
        }
        return -1;
    }

}

public class DebugParam extends PreferenceActivity {
    private static final boolean DEBUG = Debug.isDebug();
	private static final String TAG = "DebugParam";
	private static final String TEXT_INFO = "text_info";

    private static final String BAND_SELECT = "key_bandselect";
    private static final String FORBIDPLMN = "key_forbidplmn";
    private static final String PLMNSELECT = "key_plmnselect";
    private static final String ASSERT_MODE = "key_assertmode";
    private static final String MANUAL_MODE = "key_manualassert";
    private static final String RILD_SETTING = "key_rildsetting";

    private static final int GET_BAND_SELECT = 0;
    private static final int SET_BAND_SELECT = 1;
    private static final int GET_ASSERT_MODE = 2;
    private static final int SET_ASSERT_MODE = 3;
    private static final int SET_MANUAL_ASSERT = 4;
    private static final int INIT_BAND_REFERENCE = 5;
    private static final int INIT_ASSERT_REFERENCE = 6;
    private static final int SETTING_RILD = 7;


    private static final int ASSERT_DEBUG_MODE = 0;
    private static final int ASSERT_RELEASE_MODE = 1;

    private static final String RESULT_OK = "OK";
    private static final String RESULT_ERROR = "ERROR";
    private static final int ERROR = -1;
    private engfetch mEf;
    private ByteArrayOutputStream outputBuffer;
    private DataOutputStream outputBufferStream;
    private String mATline;
    private int mSocketID;

    private boolean []mBandCheckList = {false,false,false,false};
    private int []mBandShowList = {BandSelectDecoder.GSM900,
                                   BandSelectDecoder.DCS1800,
                                   BandSelectDecoder.PCS1900,
                                   BandSelectDecoder.GSM850};

    private int mAssertMdoe;
    //private Preference mAssertModePreference;
    private Preference mBandSelectPreference;
    private CheckBoxPreference rildSettingPref ;
    private Handler mThread;
    private Handler mUiThread;
    private boolean mHaveFinish = true;
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        mEf = new engfetch();
        mSocketID = mEf.engopen();
        addPreferencesFromResource(R.layout.debugparam);
        //mAssertModePreference = (Preference)findPreference(ASSERT_MODE);
        mBandSelectPreference = (Preference)findPreference(BAND_SELECT);
        /*Add 20130523 Spreadst of add RILD  control UI start*/
        rildSettingPref=(CheckBoxPreference)findPreference(RILD_SETTING);
        boolean rildStatus =SystemProperties.getBoolean("persist.sys.sprd.attest", false);
        rildSettingPref.setChecked(rildStatus);
        if(rildStatus){
            rildSettingPref.setSummary("RILD will be closed after restart");
        }else{
            rildSettingPref.setSummary("RILD will work after restart");
        }
        /*Add 20130523 Spreadst of add RILD  control UI end*/
        /*Add 20130206 Spreadst of 122017 8810 7710 don't support PLMN start*/
//        String mode = SystemProperties.get("ro.product.hardware");
        /*Modify 20130306 Spreadst of 130799 77XX dont support PLMN start */
//        if(mode.contains("8810") ||mode.contains("77")||mode.contains("8825")){
//            getPreferenceScreen().removePreference((Preference)findPreference("key_forbidplmn"));
//            getPreferenceScreen().removePreference((Preference)findPreference("key_plmnselect"));
//        }
        /*Modify 20130306 Spreadst of 130799 77XX dont support PLMN end  */
        /*Add 20130206 Spreadst of 122017 8810 7710 don't support PLMN end*/
        /*Add 20130718 Spreadst of 181960 modem don't support this AT cmd start*/
        String baseBandVersion = SystemProperties.get("gsm.version.baseband");
        Log.v("zwx" , "baseBandVersion  = " + baseBandVersion);
        if(baseBandVersion.contains("sc8830")){
            getPreferenceScreen().removePreference((Preference)findPreference("key_forbidplmn"));
            getPreferenceScreen().removePreference((Preference)findPreference("key_plmnselect"));
        }
        /*Add 20130718 Spreadst of 181960 modem don't support this AT cmd  end*/
        mUiThread = new Handler();
        HandlerThread t = new HandlerThread("debugparam");
        t.start();
        mThread = new AsyncnonizeHandler(t.getLooper());

        Message msg1 = mThread.obtainMessage(INIT_ASSERT_REFERENCE);
    	msg1.sendToTarget();

    	Message msg2 = mThread.obtainMessage(INIT_BAND_REFERENCE);
    	msg2.sendToTarget();
    }

    @Override
    protected void onStart() {
    	mHaveFinish = false;
    	super.onStart();
    }

	@Override
	protected void onStop() {
		mHaveFinish = true;
		super.onStop();
	}

    private void showDialog(final Builder builder,final String title){
        mUiThread.post(new Runnable() {
			public void run() {
				if (!mHaveFinish) {
					builder.setTitle(title);
					builder.create().show();
				}
			}
		});
    }

    class AsyncnonizeHandler extends Handler{
    	public AsyncnonizeHandler(Looper looper ){
    		super(looper);
    	}
    	@Override
    	public void handleMessage(Message msg) {
    		switch(msg.what){
    		case GET_BAND_SELECT:{
    			final Builder builder=new android.app.AlertDialog.Builder(DebugParam.this);
                int value = getSelectedBand();
				if (value == ERROR) {
					Toast.makeText(DebugParam.this, "Error", Toast.LENGTH_SHORT).show();
					break;
				}
                int param = BandSelectDecoder.getInstance().getBandsFromCmdParam(value);
                for(int i= 0;i<mBandShowList.length;i++){
                    if((param & mBandShowList[i])!= 0) {
                        mBandCheckList[i] = true;
                    }
                }

                builder.setMultiChoiceItems(R.array.band_select_choices, mBandCheckList, new DialogInterface.OnMultiChoiceClickListener(){
                    public void onClick(DialogInterface dialog, int which, boolean isChecked) {
                        mBandCheckList[which]=isChecked;
                    }
                });

                builder.setPositiveButton("OK ", new DialogInterface.OnClickListener(){
                    public void onClick(DialogInterface dialog, int which) {
                    	Message msg = mThread.obtainMessage(SET_BAND_SELECT);
                    	msg.sendToTarget();
                    }
                });
                final String title = String.valueOf(msg.obj);
                showDialog(builder, title);
    			break;
    		}
    		case SET_BAND_SELECT:{
    			int selectBands = 0;
                for(int i= 0;i<mBandCheckList.length;i++){
                    if(mBandCheckList[i]) {
						selectBands = selectBands | mBandShowList[i];
                    }
                }
                int cmdFromBands = BandSelectDecoder.getInstance().getCmdParamFromBands(selectBands);
                if(cmdFromBands == -1){
                	Toast.makeText(DebugParam.this, R.string.set_bands_error, Toast.LENGTH_SHORT).show();
                	Log.e(TAG, "Error, cmdFromBands = -1");
                	return;
                }
                final int select = selectBands;
                if(setSelectedBand(cmdFromBands)){
                	mUiThread.post(new Runnable() {
						public void run() {
							mBandSelectPreference
							.setSummary(getBandSelectSummary(BandSelectDecoder
									.getInstance().getCmdParamFromBands(select)));
						}
					});
                }
    			break;
    		}
    		case GET_ASSERT_MODE:{
    			final Builder builder = new android.app.AlertDialog.Builder(DebugParam.this);
                int mode = getAssertMode();
                if(mode == ERROR){
                	Toast.makeText(DebugParam.this, "Error", Toast.LENGTH_SHORT).show();
                	break;
                }
                mAssertMdoe = mode;
                builder.setSingleChoiceItems(R.array.assert_mode, mAssertMdoe, new DialogInterface.OnClickListener(){
                    public void onClick(DialogInterface dialog, int which) {
                        mAssertMdoe = which;
                    }
                });

                builder.setPositiveButton("OK ", new DialogInterface.OnClickListener(){
                    public void onClick(DialogInterface dialog, int which) {
                    	Message msg = mThread.obtainMessage(SET_ASSERT_MODE);
                    	msg.arg1 = mAssertMdoe;
                    	msg.sendToTarget();
                    }
                });
                final String title = String.valueOf(msg.obj);
                showDialog(builder, title);
                break;
            }
    		case SET_ASSERT_MODE:{
//    			final int mode = msg.arg1;
//    			boolean success = setAssertMode(mode);
//				if (success) {
//					mUiThread.post(new Runnable() {
//						public void run() {
//							mAssertModePreference.setSummary(getAssertModeSummary(mode));
//						}
//					});
//				}else{
//					Toast.makeText(DebugParam.this, "Error", Toast.LENGTH_SHORT).show();
//				}
    			break;
    		}
			case SET_MANUAL_ASSERT:
	            setManualAssert();
				break;
			case INIT_BAND_REFERENCE:{
				final String summary = getBandSelectSummary(getSelectedBand());
				mUiThread.post(new Runnable() {
					public void run() {
						mBandSelectPreference.setSummary(summary);
					}
				});
				break;
			}
			case INIT_ASSERT_REFERENCE:{
//		        final String summary = getAssertModeSummary(getAssertMode());
//		        mUiThread.post(new Runnable() {
//					public void run() {
//						mAssertModePreference.setSummary(summary);
//					}
//				});
				break;
			}
			default:
				break;
			}
    		super.handleMessage(msg);
    	}
    }

    public boolean onPreferenceTreeClick(PreferenceScreen preferenceScreen, Preference preference) {
        final String key = preference.getKey();
        if (BAND_SELECT.equals(key)) {
        	Message msg = mThread.obtainMessage(GET_BAND_SELECT);
        	msg.obj = preference.getTitle();
        	msg.sendToTarget();
        } else if(ASSERT_MODE.equals(key)) {
        	Message msg = mThread.obtainMessage(GET_ASSERT_MODE);
        	msg.obj = preference.getTitle();
        	msg.sendToTarget();
        } else if(MANUAL_MODE.equals(key)) {
        	Message msg = mThread.obtainMessage(SET_MANUAL_ASSERT);
        	msg.obj = preference.getTitle();
        	msg.sendToTarget();
        }else if(FORBIDPLMN.equals(key)) {
            Intent intent = new Intent(this, TextInfo.class);
            startActivity(intent.putExtra(TEXT_INFO, 1));
        } else if(PLMNSELECT.equals(key)) {
            Intent intent = new Intent(this, TextInfo.class);
            startActivity(intent.putExtra(TEXT_INFO, 2));
            /*Add 20130523 Spreadst of add RILD  control UI start*/
        } else if(RILD_SETTING.equals(key)){
            if(rildSettingPref.isChecked()){
                SystemProperties.set("persist.sys.sprd.attest", "true");
                rildSettingPref.setSummary("RILD will be closed after restart");
            }else{
                SystemProperties.set("persist.sys.sprd.attest", "false");
                rildSettingPref.setSummary("RILD will work after restart");
            }
        }
        /*Add 20130523 Spreadst of add RILD  control UI end*/
        return true;
    }

    private boolean setSelectedBand(int bands) {
		/* SPRD: Add 20130823 of bug 200304, comment @{ */
		mEf.engclose(mSocketID);
		mSocketID = mEf.engopen();
		/* @} */
        //Sim1
        outputBuffer = new ByteArrayOutputStream();
        outputBufferStream = new DataOutputStream(outputBuffer);
        /*Modify 20130205 Spreadst of 125480 change the method of creating cmd start*/
        //mATline =String.format("%d,%d,%d", engconstents.ENG_AT_SELECT_BAND, 1,bands);
        mATline = new StringBuilder().append(engconstents.ENG_AT_SELECT_BAND).append(",").
                              append(1).append(",").append(bands).toString();
        /*Modify 20130205 Spreadst of 125480 change the method of creating cmd end*/

        try {
            outputBufferStream.writeBytes(mATline);
        } catch (IOException e) {
            Log.e(TAG, "writeBytes() error!");
            return true;
        }

        mEf.engwrite(mSocketID,outputBuffer.toByteArray(),outputBuffer.toByteArray().length);

        int dataSize = 128;
        byte[] inputBytes = new byte[dataSize];

        int showlen= mEf.engread(mSocketID, inputBytes, dataSize);
        String mATResponse =  new String(inputBytes, 0, showlen,Charset.defaultCharset());
        if(mATResponse.indexOf("ERROR") != -1) {
            Toast.makeText(this,R.string.set_bands_error,Toast.LENGTH_LONG).show();
            return false;
        }
        return true;
    }

    private int getSelectedBand() {
		/* SPRD: Add 20130823 of bug 200304, comment @{ */
		mEf.engclose(mSocketID);
		mSocketID = mEf.engopen();
		/* @} */
        outputBuffer = new ByteArrayOutputStream();
        outputBufferStream = new DataOutputStream(outputBuffer);
        /*Modify 20130205 Spreadst of 125480 change the method of creating cmd start*/
//        mATline =String.format("%d,%d", engconstents.ENG_AT_CURRENT_BAND, 0);
        mATline = new StringBuilder().append(engconstents.ENG_AT_CURRENT_BAND).append(",")
                              .append(0).toString();
        /*Modify 20130205 Spreadst of 125480 change the method of creating cmd end*/

        try {
            outputBufferStream.writeBytes(mATline);
        } catch (IOException e) {
            Log.e(TAG, "writeBytes() error!");
            return ERROR;
        }

        mEf.engwrite(mSocketID,outputBuffer.toByteArray(),outputBuffer.toByteArray().length);

        int dataSize = 128;
        byte[] inputBytes = new byte[dataSize];

        int showlen= mEf.engread(mSocketID, inputBytes, dataSize);
        String mATResponse =  new String(inputBytes, 0, showlen,Charset.defaultCharset());
        if(DEBUG) Log.d(TAG, "getSelectedBand result : " + mATResponse);
        int value = ERROR;
        try{
        	value = Integer.parseInt(mATResponse);
        }catch(Exception e){
        	Log.e(TAG, "Format String " + mATResponse + " to Integer Error!");
        }
        return value;
    }

    private int getAssertMode() {
		/* SPRD: Add 20130823 of bug 200304, comment @{ */
		mEf.engclose(mSocketID);
		mSocketID = mEf.engopen();
		/* @} */
        outputBuffer = new ByteArrayOutputStream();
        outputBufferStream = new DataOutputStream(outputBuffer);
        /*Modify 20130205 Spreadst of 125480 change the method of creating cmd start*/
//        mATline =String.format("%d,%d", engconstents.ENG_AT_GET_ASSERT_MODE, 0);
        mATline = new StringBuilder().append(engconstents.ENG_AT_GET_ASSERT_MODE).append(",")
        .append(0).toString();
        /*Modify 20130205 Spreadst of 125480 change the method of creating cmd end*/

        try {
            outputBufferStream.writeBytes(mATline);
        } catch (IOException e) {
            Log.e(TAG, "writeBytes() error!");
            return ERROR;
        }
        int size = mEf.engwrite(mSocketID,outputBuffer.toByteArray(),outputBuffer.toByteArray().length);
	if(DEBUG) Log.d(TAG, "engwrite size: " + size);
        int dataSize = 128;
        byte[] inputBytes = new byte[dataSize];

        int showlen= mEf.engread(mSocketID, inputBytes, dataSize);
        String mATResponse =  new String(inputBytes, 0, showlen,Charset.defaultCharset());
        if(DEBUG) Log.d(TAG, "getAssertMode result : " + mATResponse);
        if(mATResponse.indexOf("+SDRMOD: 1") != ERROR) {
            return ASSERT_RELEASE_MODE;
        }else if(mATResponse.indexOf("+SDRMOD: 0") != ERROR) {
            return ASSERT_DEBUG_MODE;
        }

        return ERROR;
    }

    private boolean setAssertMode(int mode) {
		/* SPRD: Add 20130823 of bug 200304, comment @{ */
		mEf.engclose(mSocketID);
		mSocketID = mEf.engopen();
		/* @} */
        outputBuffer = new ByteArrayOutputStream();
        outputBufferStream = new DataOutputStream(outputBuffer);
        /*Modify 20130205 Spreadst of 125480 change the method of creating cmd start*/
//        mATline =String.format("%d,%d,%d", engconstents.ENG_AT_SET_ASSERT_MODE, 1,mode);
        mATline = new StringBuilder().append(engconstents.ENG_AT_SET_ASSERT_MODE).append(",")
        .append(1).append(",").append(mode).toString();
        /*Modify 20130205 Spreadst of 125480 change the method of creating cmd end*/
        System.out.println("onPreferenceChange At Line is " + mATline);
        try {
            outputBufferStream.writeBytes(mATline);
        } catch (IOException e) {
            Log.e(TAG, "writeBytes() error!");
            return false;
        }

        mEf.engwrite(mSocketID,outputBuffer.toByteArray(),outputBuffer.toByteArray().length);

        int dataSize = 128;
        byte[] inputBytes = new byte[dataSize];

        int showlen= mEf.engread(mSocketID, inputBytes, dataSize);
        String mATResponse =  new String(inputBytes, 0, showlen,Charset.defaultCharset());
        if(DEBUG) Log.d(TAG, "setAssertMode result is " + mATResponse);
        if(mATResponse.contains("OK")) {
            return true;
        }
        return false;
    }

    private void setManualAssert() {
		/* SPRD: Add 20130823 of bug 200304, comment @{ */
		mEf.engclose(mSocketID);
		mSocketID = mEf.engopen();
		/* @} */
        outputBuffer = new ByteArrayOutputStream();
        outputBufferStream = new DataOutputStream(outputBuffer);
        /*Modify 20130205 Spreadst of 125480 change the method of creating cmd start*/
//        mATline =String.format("%d,%d", engconstents.ENG_AT_SET_MANUAL_ASSERT, 0);
        mATline = new StringBuilder().append(engconstents.ENG_AT_SET_MANUAL_ASSERT)
                  .append(",").append(0).toString();
        /*Modify 20130205 Spreadst of 125480 change the method of creating cmd end*/

        try {
            outputBufferStream.writeBytes(mATline);
        } catch (IOException e) {
            Log.e(TAG, "writeBytes() error!");
        }

        int datasize = outputBuffer.toByteArray().length;
        int iRet = mEf.engwrite(mSocketID,outputBuffer.toByteArray(), datasize);
        if(DEBUG) Log.d(TAG, "setManualAssert engwrite size: " + iRet);
        if(datasize == iRet) {
        	Toast.makeText(DebugParam.this, "Success", Toast.LENGTH_SHORT).show();
        }else{
        	Toast.makeText(DebugParam.this, "Error", Toast.LENGTH_SHORT).show();
        }
    }

    private String getAssertModeSummary(int which) {
    	String mode = "";
    	try{
    		mode = getResources().getStringArray(R.array.assert_mode)[which];
    	}catch (Exception e) {
    		Log.e(TAG, "Error, ArrayIndexOutOfBoundsException !");
    	}
        return mode;
    }

    private String getBandSelectSummary(int band) {
        String bandString = null;
        if(band == ERROR){
        	return "";
        }
        int param = BandSelectDecoder.getInstance().getBandsFromCmdParam(band);

        for(int i= 0;i<mBandShowList.length;i++){
            if((param&mBandShowList[i])!= 0) {
                if(bandString == null) {
                    bandString = this.getResources().getStringArray(R.array.band_select_choices)[i];
                }else {
                    bandString += "|" + this.getResources().getStringArray(R.array.band_select_choices)[i];
                }
                //mBandCheckList[i] = true;
            }
        }
        return bandString;
    }
}
