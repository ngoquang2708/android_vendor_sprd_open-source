
package com.spreadtrum.dm;

import android.app.Service;
import android.app.Notification;
import android.app.NotificationManager;

import android.content.Context;
import android.content.Intent;
import android.content.BroadcastReceiver;
import android.content.IntentFilter;

import android.content.SharedPreferences;
import android.telephony.PhoneStateListener;
import android.telephony.ServiceState;
import android.telephony.TelephonyManager;
import android.telephony.SmsManager;
import android.os.IBinder;
import android.util.Log;
import android.os.SystemProperties;

import com.android.internal.telephony.Phone;
import com.android.internal.telephony.PhoneFactory;
import com.android.internal.telephony.PhoneConstants;

import java.nio.ByteBuffer;
import android.os.Handler;
import android.os.Message;
import android.net.Uri;
import com.android.internal.telephony.TelephonyIntents;

//import com.redbend.vdm.NIAMsgHandler.UIMode;
import android.app.AlertDialog;
import android.content.DialogInterface;

import android.provider.Telephony;
import android.content.ContentValues;
import android.database.Cursor;
import com.android.internal.telephony.TelephonyProperties;
import com.spreadtrum.dm.transaction.DMTransaction;
import com.spreadtrum.dm.vdmc.MyTreeIoHandler;
import com.spreadtrum.dm.vdmc.Vdmc;
import com.android.internal.telephony.PhoneFactory;
import android.app.PendingIntent;
import android.app.Activity;

public class DmService extends Service {
    private String TAG = DmReceiver.DM_TAG + "DmService: ";

    //add for 105942 begin
    private static final boolean IS_CONFIG_SELFREG_REPLY = false;
    private static final String SENT_SMS_ACTION ="com.spreadtrum.dm.SENT_SMS";
    private static final String DELIVERED_SMS_ACTION ="com.spreadtrum.dm.DELIVERED_SMS";
    //add for 105942 end

    private static final String mLastImsiFile = "lastImsi.txt";

    private static TelephonyManager[] mTelephonyManager;

    private static boolean mIsHaveSendMsg = false; // if have send self registe
                                                   // message

    private static boolean mIsSelfRegOk = false; // if self registe ok

    private static boolean mSelfRegSwitch = true; // true: open self registe
                                                  // function false:close self
                                                  // registe function

    private static final String PREFERENCE_NAME = "LastImsi";

    private static int MODE = MODE_PRIVATE;

    // private static DmService mInstance = null;
    private static DmService mInstance = null;

    private static Context mContext;

    private static int pppstatus = PhoneConstants.APN_TYPE_NOT_AVAILABLE;

    private static String mSmsAddr;

    private static String mSmsPort;

    private static String mServerAddr;

    private static String mApn = null;

    private static String mApnTemp = null;

    private static String mProxyTemp = null;

    private static String mProxyPortTemp = null;

    private static String mManufactory;

    private static String mModel;

    private static String mSoftVer;

    private static String mImeiStr;

    private static boolean mIsDebugMode;

    private static boolean mIsRealNetParam; // if current is read net parameter

    private static final String DM_CONFIG = "DmConfig";
    //Fix 204720 on 20130822:the alert sound should be same to sms sound
    private static final String SMS_SOUND = "SMS_SOUND";

    private static final String ITEM_DEBUG_MODE = "DebugMode";

    private static final String ITEM_REAL_PARAM = "RealParam";

    private static final String ITEM_MANUFACTORY = "Manufactory";

    private static final String ITEM_MODEL = "Model";

    private static final String ITEM_SOFTVER = "SoftVer";

    private static final String ITEM_IMEI = "IMEI";

    private static final String ITEM_SERVER_ADDR = "ServerAddr";

    private static final String ITEM_SMS_ADDR = "SmsAddr";

    private static final String ITEM_SMS_PORT = "SmsPort";

    private static final String ITEM_APN = "APN";

    private static final String ITEM_PROXY = "Proxy";

    private static final String ITEM_PROXY_PORT = "ProxyPort";

    private static final String ITEM_SELFREG_SWITCH = "SelfRegSwitch";

    // add by lihui
    private static final String ITEM_SERVER_NONCE = "ServerNonce";

    private static final String ITEM_CLIENT_NONCE = "ClientNonce";

    public static final String APN_CMDM = "cmdm";

    public static final String APN_CMWAP = "cmwap";

    public static final String APN_CMNET = "cmnet";

    // Real net parameter
    private static final String REAL_SERVER_ADDR = "http://dm.monternet.com:7001";

    private static final String REAL_SMS_ADDR = "10654040";

    private static final String REAL_SMS_PORT = "16998";

    private static final String REAL_APN = APN_CMDM;

    // Lab net parameter
    private static final String LAB_SERVER_ADDR = "http://218.206.176.97:7001";

    // private static final String LAB_SERVER_ADDR = "http://218.206.176.97";
    private static final String LAB_SMS_ADDR = "1065840409";

    private static final String LAB_SMS_PORT = "16998";

    private static final String LAB_APN = APN_CMWAP;

    private final Object keepcurphone = new Object(); 
    //TODO: to be confirm
//    private DmJniInterface mDmInterface;

//    private DMTransaction mDmTransaction;

    private static DmNetwork mNetwork = null;

    private static MyTreeIoHandler mTreeIoHandler = null;

    private Handler mHandler;

    private Uri mUri;

    private Cursor mCursor;

    private boolean mSmsReady[];

    private boolean mInService[];

    private  DmNativeInterface mDmNativeInterface;

    private PhoneStateListener[] mPhoneStateListener;
    
    public static boolean SUPPORT_DM_SELF_REG = false;

    private int mPhoneCnt = 0;
    private int curPhoneId = 0;
    public int mStartid= 0;

    private final BroadcastReceiver mReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            String action = intent.getAction();
            //TelephonyIntents.ACTION_IS_SIM_SMS_READY
            if (action.startsWith("android.intent.action.ACTION_IS_SIM_SMS_READY")) {//|| action.equals(TelephonyIntents.ACTION_IS_SIM2_SMS_READY)) {
                int phoneId = intent.getIntExtra("phoneId", 0);
                mSmsReady[phoneId] = intent.getBooleanExtra("isReady", false);

                Log.d(TAG, "[sms]onReceive ACTION_IS_SIM_SMS_READY mSmsReady["+ phoneId + "] = " + mSmsReady[phoneId]);
                if (mSmsReady[phoneId]) {
                    if (TelephonyManager.SIM_STATE_READY == mTelephonyManager[phoneId].getSimState()
                            && mInService[phoneId]) {

                        if (getIsCmccCard(phoneId)) {
	                    curPhoneId = phoneId;
                            Log.d(TAG, "[sms]onReceive ACTION_IS_SIM_SMS_READY: is cmcc card!");

                            // send self registe message
                            if(mPhoneCnt > 1 /*TelephonyManager.isMultiSim() */){
                                int otherPhoneId = phoneId == 0 ? 1 : 0;
                                if(!mTelephonyManager[otherPhoneId].hasIccCard() || mSmsReady[otherPhoneId] && mInService[otherPhoneId]){
                                    sendSelfRegMsg();
                                }
                            }else{
                                sendSelfRegMsg();
                            }
                        } else {
                            Log.d(TAG, "[sms]onReceive ACTION_IS_SIM_SMS_READY: not cmcc card!");
                            stopListeningServiceState(phoneId);
                            if(mPhoneCnt > 1 ){
                                int otherPhoneId = (phoneId == 0) ? 1 : 0;
				curPhoneId = otherPhoneId;
                                if (mSmsReady[otherPhoneId] && mInService[otherPhoneId] && getIsCmccCard(otherPhoneId)
                                    && TelephonyManager.SIM_STATE_READY == mTelephonyManager[otherPhoneId].getSimState()){
                                    Log.d(TAG, "[sms]onReceive ACTION_IS_SIM_SMS_READY: use other sim selfReg PhoneId="+otherPhoneId);
                                    curPhoneId = otherPhoneId;
                                    sendSelfRegMsg();
                                }
                            }
                        }
                    } else {
                        Log.d(TAG, "[sms]onReceive ACTION_IS_SIM_SMS_READY: sim state = "
                                + mTelephonyManager[phoneId].getSimState());
                    }
                }
            }

        }
    };

    private boolean getIsCmccCard(int phoneId){
        String cmccStr1 = "46000";
        String cmccStr2 = "46002";
        String cmccStr3 = "46007";
        String curOper = mTelephonyManager[phoneId].getNetworkOperator();

        Log.d(TAG, "getIsCmccCard  phoneId ="+phoneId+" curOper:"+curOper);
        if (curOper.equals(cmccStr1) || curOper.equals(cmccStr2) || curOper.equals(cmccStr3)){
            return true; 
        }else{
            return false; 
        }
    }

    private PhoneStateListener getPhoneStateListener(final int phoneId) {
        PhoneStateListener phoneStateListener = new PhoneStateListener() {
            @Override
            public void onServiceStateChanged(ServiceState serviceState) {
                Log.d(TAG, "onServiceStateChanged: phoneId = " + phoneId + ",current state = " + serviceState.getState());
		synchronized(keepcurphone)
		{
                // judge if network is ready
                if (ServiceState.STATE_IN_SERVICE == serviceState.getState()) {
                    Log.d(TAG, "onServiceStateChanged: STATE_IN_SERVICE");
                    mInService[phoneId] = true;
                    // sim card is ready
                    if (TelephonyManager.SIM_STATE_READY == mTelephonyManager[phoneId].getSimState()
                            && mSmsReady[phoneId]) {
      
                        if (getIsCmccCard(phoneId)) {
	                    curPhoneId = phoneId;
                            Log.d(TAG, "onServiceStateChanged: is cmcc card!");

                            // send self registe message
                            if(mPhoneCnt > 1 /*TelephonyManager.isMultiSim()*/){
                                int otherPhoneId = phoneId == 0 ? 1 : 0;
                                if(!mTelephonyManager[otherPhoneId].hasIccCard() || mSmsReady[otherPhoneId] && mInService[otherPhoneId]){
                                    sendSelfRegMsg();
                                }
                            }else{
                                sendSelfRegMsg();
                            }
                        } else {
                            Log.d(TAG, "onServiceStateChanged: not cmcc card!");
                            stopListeningServiceState(phoneId);
                            if(mPhoneCnt > 1 ){
                                int otherPhoneId = (phoneId == 0) ? 1 : 0;
                                curPhoneId = otherPhoneId;
                                if (mSmsReady[otherPhoneId] && mInService[otherPhoneId] && getIsCmccCard(otherPhoneId)
                                    && TelephonyManager.SIM_STATE_READY == mTelephonyManager[otherPhoneId].getSimState()){
                                    Log.d(TAG, "onServiceStateChanged use other sim selfReg PhoneId="+otherPhoneId);
                                    curPhoneId = otherPhoneId;
                                    sendSelfRegMsg();
                                }
                            }
                        }
                    } else {
                        Log.d(TAG, "onServiceStateChanged: sim state = "
                                + mTelephonyManager[phoneId].getSimState());
                    }
                }
		} //synchronized
            }
        };
        return phoneStateListener;
    }


    private class DMHandler extends Handler {

        public void handleMessage(Message msg) {

        }
    };

    @Override
    public void onCreate() {
        mPhoneCnt = TelephonyManager.getPhoneCount();
        mSmsReady = new boolean[mPhoneCnt];
        mInService = new boolean[mPhoneCnt];

	Log.d(TAG, "onCreate: mPhoneCnt "+mPhoneCnt);
	
        mTelephonyManager = new TelephonyManager[mPhoneCnt];
        mPhoneStateListener = new PhoneStateListener[mPhoneCnt];

        for(int phoneId=0; phoneId<mPhoneCnt; phoneId++){
            mTelephonyManager[phoneId] = (TelephonyManager) getSystemService(PhoneFactory.
                    getServiceName(Context.TELEPHONY_SERVICE,phoneId));
            mPhoneStateListener[phoneId] = getPhoneStateListener(phoneId);
            mTelephonyManager[phoneId].listen(mPhoneStateListener[phoneId],
                    PhoneStateListener.LISTEN_SERVICE_STATE);
        }

        mContext = getBaseContext();

        mInstance = this;
        Log.d(TAG, "OnCreate: mInstance = " + mInstance);
/**** set foreground **************/
	NotificationManager mNotificationManager=null;
        mNotificationManager = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
	Notification notify = new Notification();
	notify.flags |= Notification.FLAG_FOREGROUND_SERVICE;
	super.startForeground(10,notify);
        Log.d(TAG, "OnCreate: setForeground ");
	mNotificationManager.notify(10, notify);
/**** set foreground **************/

        mHandler = new DMHandler();
        //TODO: to be confirm
        mDmNativeInterface = new DmNativeInterface(mContext, mHandler);
    
        initParam();
        /*
        if (!mIsHaveInit)
        // control this init process only run once
        { // init dm relative parameters
            initParam(); // Start listening to service state
            if (getSelfRegSwitch()) {
                if (isNeedSelfReg()) {
                    mTelephonyManager.listen(mPhoneStateListener,
                            PhoneStateListener.LISTEN_SERVICE_STATE);
                } else {
                    setSelfRegState(true);
                }
            }
            mIsHaveInit = true;
        }
         */

//        mDmInterface = new DmJniInterface(mContext);
//        DMNativeMethod.JsaveDmJniInterfaceObject(mDmInterface);
//
//        mDmTransaction = new DMTransaction(mContext, mHandler);
//        DMNativeMethod.JsaveDmTransactionObject(mDmTransaction);

        if (mNetwork == null) {
            mNetwork = new DmNetwork(mContext);
        } else {
        }
		
        if (mTreeIoHandler == null) {
            mTreeIoHandler = new MyTreeIoHandler(mContext);
        } else {
        }


        // Listen for broadcast intents that indicate the SMS is ready
/*        
        IntentFilter filter = new IntentFilter(TelephonyIntents.ACTION_IS_SIM_SMS_READY);
        IntentFilter filter2 = new IntentFilter(TelephonyIntents.ACTION_IS_SIM2_SMS_READY);
        Intent intent1 = registerReceiver(mReceiver, filter);
		Log.d(TAG, "onCreate: ACTION_IS_SIM_SMS_READY register ");
        if(mPhoneCnt == 2){
            Intent intent2 = registerReceiver(mReceiver, filter2);
		Log.d(TAG, "onCreate: ACTION_IS_SIM2_SMS_READY register ");
        }
*/
	int num;
        IntentFilter filter = new IntentFilter("android.intent.action.ACTION_IS_SIM_SMS_READY");//TelephonyIntents.ACTION_IS_SIM_SMS_READY
        Intent intent1 = registerReceiver(mReceiver, filter);
		Log.d(TAG, "onCreate: ACTION_IS_SIM_SMS_READY register ");
	for (num= 0; num < mPhoneCnt; num++)
	{
         filter = new IntentFilter(PhoneFactory.getAction("android.intent.action.ACTION_IS_SIM_SMS_READY",num));//TelephonyIntents.ACTION_IS_SIM_SMS_READY
         intent1 = registerReceiver(mReceiver, filter);
		Log.d(TAG, "onCreate: ACTION_IS_SIM_SMS_READY register "+num);
	}
    }

    @Override
    public void onDestroy() {
        // Stop listening for service state
        stopListeningServiceState();
        unregisterReceiver(mReceiver);
        Log.d(TAG, "onDestroy: DmService is killed!");
        mInstance = null;
        mContext = null;
	mDmNativeInterface = null;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Deprecated
    public void onStart(Intent intent, int startId) {
        Log.d(TAG, "onStart: intent = " + intent + ", startId = " + startId);
        mStartid = startId;
        if (intent == null) {
            return;
        }

        if (intent.getAction().equals("com.android.dm.SelfReg")) {
            Log.d(TAG, "onStart: com.android.dm.SelfReg");
            // Start listening to service state
            if (getSelfRegSwitch()) {
                setIsHaveSendSelfRegMsg(mContext, false);
                for(int phoneId = 0; phoneId < mPhoneCnt; phoneId++){
//                    mTelephonyManager[phoneId].listen(mPhoneStateListener[phoneId],
//                            PhoneStateListener.LISTEN_SERVICE_STATE);
                }
                /*
                 * if (isNeedSelfReg()) { setIsHaveSendSelfRegMsg(mContext,
                 * false); mTelephonyManager.listen(mPhoneStateListener,
                 * PhoneStateListener.LISTEN_SERVICE_STATE); } else {
                 * setSelfRegState(mContext, true); //mContext.stopService(new
                 * Intent("com.android.dm.SelfReg")); }
                 */
            }
        } else if (intent.getAction().equals("com.android.dm.NIA")) {
            Log.d(TAG, "onStart: com.android.dm.NIA");
		if ( isSelfRegOk())
		{
	           initConnectParam(); // insure dm connect network param is properly
	                                // set

	            byte[] body = intent.getByteArrayExtra("msg_body");
	            String origin = intent.getStringExtra("msg_org");

	            Log.d(TAG, "onStart: mInstance = " + mInstance);
	            Log.d(TAG, "onStart: mContext = " + mContext);
	            Log.d(TAG, "onStart: this = " + this);
	            Vdmc.getInstance().startVDM(mContext, Vdmc.SessionType.DM_SESSION_SERVER, body, origin);
		}
		else
			Log.d(TAG, "onStart: selfregister not ok");
        }
    }

    public static DmService getInstance() {
        if (null == mInstance) {
            mInstance = new DmService();
            // Log.d("DM ==> DmService: ",
            // "getInstance: new DmService() , mInstance = " + mInstance);
        }
        // Log.d("DM ==> DmService: ", "getInstance: mInstance = " + mInstance);
        return mInstance;
    }

    //TODO: to be confirm
//    public DmJniInterface getDmInterface() {
//        return mDmInterface;
//    }

    public DmNativeInterface getDmNativeInterface() {
        if(mDmNativeInterface == null)
        {
	Log.d(TAG, "getDmNativeInterface is null, reCreate DMNativeInterface");
        mDmNativeInterface = new DmNativeInterface(mContext, mHandler);			
        	}
        return mDmNativeInterface;
    }
    public void clearDmNativeInterface() {
	Log.d(TAG, "clearDmNativeInterface");
	mDmNativeInterface = null;
    }
    public static Context getContext() {
        // Log.d("DM ==> DmService: ", "getContext: mContext = " + mContext);
        return mContext;
    }

    // Stop listening for service state
    public void stopListeningServiceState() {
        for(int phoneId = 0; phoneId < mPhoneCnt; phoneId++){
            if (null != mTelephonyManager[phoneId]) {
                mTelephonyManager[phoneId].listen(mPhoneStateListener[phoneId], 0);
            }
        }
        Log.d(TAG, "stop listen service state for all phone");
    }

    public void stopListeningServiceState(int phoneId) {
        if (null != mTelephonyManager[phoneId]) {
            mTelephonyManager[phoneId].listen(mPhoneStateListener[phoneId], 0);
        }
        Log.d(TAG, "stop listen service state for phone " + phoneId);
    }
    //Fix 204720 on 20130822:the alert sound should be same to sms sound start
    public void setSMSSoundUri(String newSmsSoundString){
    	SharedPreferences smsSoundSP;        
        smsSoundSP = getSharedPreferences(SMS_SOUND,MODE);
        String smsSoundString = smsSoundSP.getString("smssound_dm", "Default");
        if(!smsSoundString.equals(newSmsSoundString)){
        	SharedPreferences.Editor editor = smsSoundSP.edit();
            editor.putString("smssound_dm", newSmsSoundString);
            editor.commit();
        }        
    }
    //Fix 204720 on 20130822:the alert sound should be same to sms sound end
    // init dm relative parameter
    private void initParam() {
        SharedPreferences sharedPreferences;
      //Fix 204720 on 20130822:the alert sound should be same to sms sound
        SharedPreferences smsSoundSP;
        sharedPreferences = getSharedPreferences(DM_CONFIG, MODE);
        
        smsSoundSP = getSharedPreferences(SMS_SOUND,MODE);                
        Log.i(TAG, "initParam smssound_dm = " + smsSoundSP.getString("smssound_dm", "Default"));
        
        // init self registe switch
        if ((SystemProperties.get("ro.hisense.cmcc.test.datong", "0").equals("1"))
                || (SystemProperties.get("ro.hisense.cta.test", "0").equals("1"))) {
            // close self registe function
            setSelfRegSwitch(mContext, false);
        } else {
            // default is open
            mSelfRegSwitch = sharedPreferences.getBoolean(ITEM_SELFREG_SWITCH, true);
        }

        // init debug mode
        mIsDebugMode = sharedPreferences.getBoolean(ITEM_DEBUG_MODE, false);

        if (mIsDebugMode) {
            // init manufacture
            mManufactory = sharedPreferences.getString(ITEM_MANUFACTORY, "K-Touch");

            // init model
            mModel = sharedPreferences.getString(ITEM_MODEL, "K-Touch T621");

            // init software version
            // String softVer =
            // SystemProperties.get("ro.hisense.software.version",
            // Build.UNKNOWN);
            mSoftVer = sharedPreferences.getString(ITEM_SOFTVER, "4G_W4_TD_MocorDroid2.2_W11.32");

            // init imei
            //mTelephonyManager = (TelephonyManager) getSystemService(Context.TELEPHONY_SERVICE);
            mImeiStr = sharedPreferences.getString(ITEM_IMEI, mTelephonyManager[0].getDeviceId());
        } else {
            // init manufacture
            // mManufactory = "4G";
            mManufactory = sharedPreferences.getString(ITEM_MANUFACTORY, "K-Touch");
            // init model
            // mModel = "W4";
            mModel = sharedPreferences.getString(ITEM_MODEL, "K-Touch T621");

            // init software version
            mSoftVer = SystemProperties.get("ro.hisense.software.version",
                    "T72_V2.0_111230_CMCC_120104_W11.49_p3.2");

            // init imei
            //mTelephonyManager = (TelephonyManager) getSystemService(Context.TELEPHONY_SERVICE);
            mImeiStr = mTelephonyManager[0].getDeviceId();
        }

        createDmApn();
        // according to cmcc test flag to decide current server relative
        // parameters
        if (SystemProperties.get("ro.hisense.cmcc.test", "0").equals("1")) {
            setRealNetParam(mContext, false, false);
        } else {
            // init if use real net parameter
            mIsRealNetParam = sharedPreferences.getBoolean(ITEM_REAL_PARAM, true);
        }

        // init server address/sms address/sms port
        if (mIsRealNetParam) {
            mServerAddr = sharedPreferences.getString(ITEM_SERVER_ADDR, REAL_SERVER_ADDR);
            mSmsAddr = sharedPreferences.getString(ITEM_SMS_ADDR, REAL_SMS_ADDR);
            mSmsPort = sharedPreferences.getString(ITEM_SMS_PORT, REAL_SMS_PORT);
        } else {
            mServerAddr = sharedPreferences.getString(ITEM_SERVER_ADDR, LAB_SERVER_ADDR);
            mSmsAddr = sharedPreferences.getString(ITEM_SMS_ADDR, LAB_SMS_ADDR);
            mSmsPort = sharedPreferences.getString(ITEM_SMS_PORT, LAB_SMS_PORT);
        }

        // init apn/proxy/port
        //initConnectParam();
    }

    // init dm connect network param,include apn/proxy/port
    private void initConnectParam() {

       //add for 106648 begin
        int attempts = 0;
        
        while (attempts < 60) {
          String numeric = android.os.SystemProperties.get(
                  PhoneFactory.getProperty(TelephonyProperties.PROPERTY_ICC_OPERATOR_NUMERIC, curPhoneId), "");
            
            if (numeric != null && numeric.length() >= 5) {
                Log.d(TAG, "initConnectParam numeric: " + numeric);
                break;
            }
            
            try {
                Thread.sleep(1000);
            } catch (InterruptedException ie) {
                // Keep on going until max attempts is reached.
                Log.e(TAG, "initConnectParam attempts error!!");
            }
            attempts++;
        }
        Log.d(TAG, "initConnectParam attempts: " + attempts);
        //add for 106648 end

	
        createDmApn();

        // according to cmcc test flag to decide current server relative
        // parameters
        SharedPreferences sharedPreferences;
        sharedPreferences = getSharedPreferences(DM_CONFIG, MODE);
        if (SystemProperties.get("ro.hisense.cmcc.test", "0").equals("1")) {
            setRealNetParam(mContext, false, true);
        } else {
            // init if use real net parameter
            mIsRealNetParam = sharedPreferences.getBoolean(ITEM_REAL_PARAM, true);
        }

        if (mIsRealNetParam) {
            // mApn = sharedPreferences.getString(ITEM_APN, REAL_APN);
            mApn = getInitApn(mContext);
            if (mApn == null) {
                mApn = REAL_APN;
                setAPN(mContext, mApn);
            }
        } else {
            // mApn = sharedPreferences.getString(ITEM_APN, LAB_APN);
            mApn = getInitApn(mContext);
            if (mApn == null || mApn.equals(REAL_APN) ) {
                mApn = LAB_APN;
                setAPN(mContext, mApn);
            }
        }
        if (mApn.equals(APN_CMWAP)) {
            // cmwap apn, need set proxy and proxy port
            String str = null;
            str = getProxy(mContext);
            if (str == null || !str.equals("10.0.0.172")) {
                setProxy(mContext, "10.0.0.172");
            }
            str = getProxyPort(mContext);
            if (str == null || !str.equals("80")) {
                setProxyPort(mContext, "80");
            }
        } else {
            // cmnet or cmdm apn, no need set proxy and proxy port
            if (getProxy(mContext) != null) {
                setProxy(mContext, null);
            }
            if (getProxyPort(mContext) != null) {
                setProxyPort(mContext, null);
            }
        }
    }

    private void createDmApn() {
        // Add new apn
          String numeric = android.os.SystemProperties.get(
                  PhoneFactory.getProperty(TelephonyProperties.PROPERTY_ICC_OPERATOR_NUMERIC, curPhoneId), "");
	
        if (numeric == null || numeric.length() < 5) {
            Log.d(TAG, "createDMApn numeric: " + numeric);
            return;
        }
        final String selection = "name = 'CMCC DM' and numeric=\"" + numeric + "\"";

        Log.d(TAG, "createDmApn: selection = " + selection);

        mCursor = mContext.getContentResolver().query(
		(curPhoneId == 0)?Telephony.Carriers.CONTENT_URI:Telephony.Carriers.getContentUri(curPhoneId,null), null,
                selection, null, null);
		

        if (mCursor != null && mCursor.getCount() > 0) {
            Log.d(TAG, "createDMApn mCursor.getCount(): " + mCursor.getCount());
            mCursor.close();
            return;
        }

        ContentValues values = new ContentValues();
        values.put(Telephony.Carriers.NAME, "CMCC DM");
        if (numeric != null && numeric.length() > 4 ) {
            // Country code
            String mcc = numeric.substring(0, 3);
            // Network code
            String mnc = numeric.substring(3);
            // Auto populate MNC and MCC for new entries, based on what SIM
            // reports
            values.put(Telephony.Carriers.MCC, mcc);
            values.put(Telephony.Carriers.MNC, mnc);
            values.put(Telephony.Carriers.NUMERIC, numeric);
        }
        values.put(Telephony.Carriers.TYPE, "dm");
	 if ( numeric.equals("46002") || numeric.equals("46000") || numeric.equals("46007"))
	{
	        mUri = getContentResolver().insert((curPhoneId == 0)?Telephony.Carriers.CONTENT_URI:Telephony.Carriers.getContentUri(curPhoneId,null), values);
			
	        if (mUri == null) {
	            Log.w(TAG, "Failed to insert new telephony provider into " +
	                   Telephony.Carriers.CONTENT_URI +"curPhoneId"+ curPhoneId);
	            return;
	        }
	 }
    }

    // get last successful self registe card imsi
    private String getLastImsi() {
        SharedPreferences sharedPreferences = getSharedPreferences(PREFERENCE_NAME, MODE);
        String imsi = sharedPreferences.getString("IMSI", "");
        Log.d(TAG, "getLastImsi : imsi = " + imsi);
        return imsi;
    }

    // save current self registe success card imsi
    protected boolean saveImsi(Context context, String imsi) {
        Log.d(TAG, "saveImsi: imsi = " + imsi);
        SharedPreferences sharedPreferences = context.getSharedPreferences(PREFERENCE_NAME, MODE);
        SharedPreferences.Editor editor = sharedPreferences.edit();
        editor.putString("IMSI", imsi);
        editor.commit();
        return true;
    }

    // get current self registe state
    protected boolean isSelfRegOk() {
        SharedPreferences sharedPreferences = getSharedPreferences(PREFERENCE_NAME, MODE);
        mIsSelfRegOk = sharedPreferences.getBoolean("IsSelfRegOk", false);
        Log.d(TAG, "isSelfRegOk: return " + mIsSelfRegOk);
        return mIsSelfRegOk;
    }

    // set current self registe state
    protected void setSelfRegState(Context context, boolean isSuccess) {
        Log.d(TAG, "setSelfRegState: to " + isSuccess);
        mIsSelfRegOk = isSuccess;
        SharedPreferences sharedPreferences = context.getSharedPreferences(PREFERENCE_NAME, MODE);
        SharedPreferences.Editor editor = sharedPreferences.edit();
        editor.putBoolean("IsSelfRegOk", isSuccess);
        editor.commit();
    }

    /* 
     * get selfreg sim index
     * return
     *  0xff    not display sim index in DM State Menu
    */
    public int getSelfRegSimIndex(){
            String lastImsi = getLastImsi();

            if (mPhoneCnt > 1 && isSelfRegOk() && null != lastImsi){
                for(int phoneId=0; phoneId < mPhoneCnt; phoneId++){
                    String simImsi = mTelephonyManager[phoneId].getSubscriberId();
                    if (null != simImsi && simImsi.equals(lastImsi)){
                        Log.d(TAG, "getSelfRegSimIndex display SIM"  + (phoneId+1));
                        return phoneId;
                    }
                }
            }
             Log.d(TAG, "getSelfRegSimIndex dont display sim index");
            return 0xff;
    }

    // judge if simcard change since last successful self registe
    private boolean isSimcardChange() {
        boolean result = false;
        String curImsi[] = new String[2];
	int phoneId = 0;
        for(phoneId=0; phoneId < mPhoneCnt; phoneId++){
            curImsi[phoneId] = mTelephonyManager[phoneId].getSubscriberId();
        }

        String lastImsi = getLastImsi();

        Log.d(TAG, "isSimcardChange: curImsi = " + curImsi[0] + " " + curImsi[1]);
        Log.d(TAG, "isSimcardChange: lastImsi = " + lastImsi);

        // NOTE: if string compare is ok , should use memcmp etc.
        if (curImsi[0] == null && curImsi[1] == null) {
            Log.d(TAG, "isSimcardChange: Error !!! curImsi is null! ");
            result = false; // if can't get imsi, no need to send selfreg
                            // message
        } else {
            if ((lastImsi.equals(curImsi[0])) /*|| (lastImsi.equals(curImsi[1]))*/) {
		   curPhoneId = 0;
		    Log.d(TAG, "isSimcardChange: selfregok: "+curPhoneId);
                result = false;
            } else 
              if ((lastImsi.equals(curImsi[1])) /*|| (lastImsi.equals(curImsi[1]))*/) {
		   curPhoneId = 1;
		    Log.d(TAG, "isSimcardChange: selfregok: "+curPhoneId);
                result = false;
            } else {
		
                result = false;
	        for(phoneId=0; phoneId < mPhoneCnt; phoneId++)
 	         {
		if (getIsCmccCard(phoneId)) 
			{
			result = true;
			curPhoneId = phoneId;
			Log.d(TAG, "isSimcardChange: Changed and select phonid = " + phoneId );
			break;
			}
	        }
            }
        }
        Log.d(TAG, "isSimcardChange: result = " + result );

        return result;
    }

    // judge if is need self registe
    protected boolean isNeedSelfReg() {
        boolean result = false;

        if (isSimcardChange()) {
            result = true;
        }
        Log.d(TAG, "isNeedSelfReg: " + result);
        return result;
    }

    // judge is have send self registe message
    protected boolean isHaveSendSelfRegMsg() {
        SharedPreferences sharedPreferences = getSharedPreferences(PREFERENCE_NAME, MODE);
        mIsHaveSendMsg = sharedPreferences.getBoolean("IsHaveSendSelfRegMsg", false);
        Log.d(TAG, "isHaveSendSelfRegMsg: return " + mIsHaveSendMsg);
        return mIsHaveSendMsg;
    }

    private void setIsHaveSendSelfRegMsg(Context context, boolean isHaveSend) {
        Log.d(TAG, "setIsHaveSendSelfRegMsg: to " + isHaveSend);
        mIsHaveSendMsg = isHaveSend;
        SharedPreferences sharedPreferences = context.getSharedPreferences(PREFERENCE_NAME, MODE);
        SharedPreferences.Editor editor = sharedPreferences.edit();
        editor.putBoolean("IsHaveSendSelfRegMsg", isHaveSend);
        editor.commit();
    }

    // get self registe switch
    protected boolean getSelfRegSwitch() {
        Log.d(TAG, "getSelfRegSwitch: " + mSelfRegSwitch);
        return mSelfRegSwitch;
    }

    // set self registe switch
    protected void setSelfRegSwitch(Context context, boolean isOpen) {
        Log.d(TAG, "setSelfRegSwitch: " + isOpen);
        mSelfRegSwitch = isOpen;

        SharedPreferences sharedPreferences = context.getSharedPreferences(DM_CONFIG, MODE);
        SharedPreferences.Editor editor = sharedPreferences.edit();
        editor.putBoolean(ITEM_SELFREG_SWITCH, isOpen);
        editor.commit();
    }

    public boolean isDebugMode() {
        Log.d(TAG, "isDebugMode: " + mIsDebugMode);
        return mIsDebugMode;
    }

    protected void setDebugMode(Context context, boolean isDebugMode) {
        Log.d(TAG, "setDebugMode: " + isDebugMode);
        mIsDebugMode = isDebugMode;

        SharedPreferences sharedPreferences = context.getSharedPreferences(DM_CONFIG, MODE);
        SharedPreferences.Editor editor = sharedPreferences.edit();
        editor.putBoolean(ITEM_DEBUG_MODE, isDebugMode);
        editor.commit();
    }

    protected boolean isRealNetParam() {
        Log.d(TAG, "isRealParam: " + mIsRealNetParam);
        return mIsRealNetParam;
    }

    protected void setRealNetParam(Context context, boolean isRealParam, boolean isSetApn) {
        Log.d(TAG, "setRealNetParam: " + isRealParam);
        mIsRealNetParam = isRealParam;

        SharedPreferences sharedPreferences = context.getSharedPreferences(DM_CONFIG, MODE);
        SharedPreferences.Editor editor = sharedPreferences.edit();
        editor.putBoolean(ITEM_REAL_PARAM, isRealParam);
        editor.commit();

        if (mIsRealNetParam) {
            setServerAddr(context, REAL_SERVER_ADDR);
            setSmsAddr(context, REAL_SMS_ADDR);
            setSmsPort(context, REAL_SMS_PORT);
            if (isSetApn) {
                setAPN(context, REAL_APN);
                DmService.getInstance().setProxy(mContext, null);
                DmService.getInstance().setProxyPort(mContext, null);
            }
        } else {
            setServerAddr(context, LAB_SERVER_ADDR);
            setSmsAddr(context, LAB_SMS_ADDR);
            setSmsPort(context, LAB_SMS_PORT);
            if (isSetApn) {
                setAPN(context, LAB_APN);
                DmService.getInstance().setProxy(mContext, "10.0.0.172");
                DmService.getInstance().setProxyPort(mContext, "80");
            }
        }
    }

    public String getServerAddr() {
        Log.d(TAG, "getServerAddr: " + mServerAddr);
        return mServerAddr;
    }

    protected void setServerAddr(Context context, String serverAddr) {
        Log.d(TAG, "setServerAddr: " + serverAddr);
        mServerAddr = serverAddr;

        SharedPreferences sharedPreferences = context.getSharedPreferences(DM_CONFIG, MODE);
        SharedPreferences.Editor editor = sharedPreferences.edit();
        editor.putString(ITEM_SERVER_ADDR, serverAddr);
        editor.commit();
    }

    protected String getSmsAddr() {
        Log.d(TAG, "getSmsAddr: " + mSmsAddr);
        return mSmsAddr;
    }

    protected void setSmsAddr(Context context, String smsAddr) {
        Log.d(TAG, "setSmsAddr: " + smsAddr);
        mSmsAddr = smsAddr;

        SharedPreferences sharedPreferences = context.getSharedPreferences(DM_CONFIG, MODE);
        SharedPreferences.Editor editor = sharedPreferences.edit();
        editor.putString(ITEM_SMS_ADDR, smsAddr);
        editor.commit();
    }

    protected String getSmsPort() {
        Log.d(TAG, "getSmsPort: " + mSmsPort);
        return mSmsPort;
    }

    protected void setSmsPort(Context context, String smsPort) {
        Log.d(TAG, "setSmsPort: " + smsPort);
        mSmsPort = smsPort;

        SharedPreferences sharedPreferences = context.getSharedPreferences(DM_CONFIG, MODE);
        SharedPreferences.Editor editor = sharedPreferences.edit();
        editor.putString(ITEM_SMS_PORT, smsPort);
        editor.commit();
    }

    private String getInitApn(Context context) {
        String str = null;
         String numeric = android.os.SystemProperties.get(
                  PhoneFactory.getProperty(TelephonyProperties.PROPERTY_ICC_OPERATOR_NUMERIC, curPhoneId), "");
        final String selection = "name = 'CMCC DM' and numeric=\""
                + numeric + "\"";
        Log.d(TAG, "getInitApn: selection = " + selection);
        Cursor cursor = context.getContentResolver().query((curPhoneId == 0)?Telephony.Carriers.CONTENT_URI:Telephony.Carriers.getContentUri(curPhoneId,null), null,
                selection, null, null);

        if (cursor != null) {
            if (cursor.getCount() > 0 && cursor.moveToFirst()) {
                str = cursor.getString(cursor.getColumnIndexOrThrow(Telephony.Carriers.APN));
            }
            cursor.close();
        }

        Log.d(TAG, "getInitApn: " + str);
        return str;
    }

    public String getSavedAPN() {
        Log.d(TAG, "getSavedAPN: " + mApnTemp);
        return mApnTemp;
    }

    public String getAPN() {
        Log.d(TAG, "getAPN: " + mApn);
	mApn = getInitApn(mContext);
        return mApn;
    }

    public int getCurrentPhoneID(){
        Log.d(TAG, "getCurrentPhoneID: " + curPhoneId);
        return curPhoneId;
   	}

    public void setCurrentPhoneID(int pid){
        Log.d(TAG, "getCurrentPhoneID: " + pid);
 	 curPhoneId = pid;
        return ;
   	}
    public void setOnlyAPN(Context context, String apn)
    {
	     Log.d(TAG, "setOnlyAPN: " + apn);
            mApn = apn;
            mApnTemp = null;			
    }
    public void setOnlyProxy(Context context, String proxy)
    {
	     Log.d(TAG, "setOnlyProxy: " + proxy);
            mProxyTemp = null;
    }
    public void setOnlyProxyPort(Context context, String port)
    {
	     Log.d(TAG, "setOnlyProxyPort: " + port);
            mProxyPortTemp= null;
    }
    public void setAPN(Context context, String apn) {
        Log.d(TAG, "setAPN: " + apn);

        // SharedPreferences sharedPreferences =
        // context.getSharedPreferences(DM_CONFIG, MODE);
        // SharedPreferences.Editor editor = sharedPreferences.edit();
        // editor.putString(ITEM_APN, apn);
        // editor.commit();
     //   if (!Vdmc.getInstance().isVDMRunning()) {
            mApn = apn;
            final String selection = "name = 'CMCC DM' and numeric=\""
                    + android.os.SystemProperties.get(
                           PhoneFactory.getProperty(TelephonyProperties.PROPERTY_ICC_OPERATOR_NUMERIC, curPhoneId), "") + "\"";
            Log.d(TAG, "setAPN: selection = " + selection);

            ContentValues values = new ContentValues();

            values.put(Telephony.Carriers.APN, apn);
            int c = values.size() > 0 ? context.getContentResolver().update(
                    (curPhoneId == 0)?Telephony.Carriers.CONTENT_URI:Telephony.Carriers.getContentUri(curPhoneId,null), values, selection, null) : 0;
            Log.d(TAG, "setAPN: values.size() = " + values.size());
            Log.d(TAG, "setAPN: update count = " + c);

            mApnTemp = null;
   /*     } else {
            mApnTemp = apn;
            Log.d(TAG, "setAPN: dm session is running, save value temporarily!");
        }
  */        
    }

    public String getSavedProxy() {
        Log.d(TAG, "getSavedProxy: " + mProxyTemp);
        return mProxyTemp;
    }

    public String getProxy(Context context) {
        // SharedPreferences sharedPreferences =
        // context.getSharedPreferences(DM_CONFIG, MODE);
        // SharedPreferences.Editor editor = sharedPreferences.edit();
        // String str = sharedPreferences.getString(ITEM_PROXY, "10.0.0.172");

        String str = null;
        final String selection = "name = 'CMCC DM' and numeric=\""
               + android.os.SystemProperties.get(
                            PhoneFactory.getProperty(TelephonyProperties.PROPERTY_ICC_OPERATOR_NUMERIC, curPhoneId), "") + "\"";

	
        Cursor cursor = 	context.getContentResolver().query(
        (curPhoneId==0)? Telephony.Carriers.CONTENT_URI:Telephony.Carriers.getContentUri(curPhoneId,null),
        null, selection, null, null);

        if (cursor != null) {
            if (cursor.getCount() > 0 && cursor.moveToFirst()) {
                str = cursor.getString(cursor.getColumnIndexOrThrow(Telephony.Carriers.PROXY));
            }
            cursor.close();
        }

        Log.d(TAG, "getProxy: " + str);
        return str;
    }

    public void setProxy(Context context, String proxy) {
        Log.d(TAG, "setProxy: " + proxy);

        // SharedPreferences sharedPreferences =
        // context.getSharedPreferences(DM_CONFIG, MODE);
        // SharedPreferences.Editor editor = sharedPreferences.edit();
        // editor.putString(ITEM_PROXY, proxy);
        // editor.commit();

  //      if (!Vdmc.getInstance().isVDMRunning()) {
            final String selection = "name = 'CMCC DM' and numeric=\""
                    + android.os.SystemProperties.get(
                            PhoneFactory.getProperty(TelephonyProperties.PROPERTY_ICC_OPERATOR_NUMERIC, curPhoneId), "") + "\"";
            ContentValues values = new ContentValues();

            values.put(Telephony.Carriers.PROXY, proxy);
            int c = values.size() > 0 ? context.getContentResolver().update(
                    (curPhoneId== 0)?Telephony.Carriers.CONTENT_URI:Telephony.Carriers.getContentUri(curPhoneId,null), 
                    values, selection, null) : 0;
            Log.d(TAG, "setProxy: values.size() = " + values.size());
            Log.d(TAG, "setProxy: update count = " + c);

            mProxyTemp = null;
/*        } else {
            mProxyTemp = proxy;
            Log.d(TAG, "setProxy: dm session is running, save value temporarily!");
        }
*/        
    }

    public String getSavedProxyPort() {
        Log.d(TAG, "getSavedProxyPort: " + mProxyPortTemp);
        return mProxyPortTemp;
    }

    public String getProxyPort(Context context) {
        // SharedPreferences sharedPreferences =
        // context.getSharedPreferences(DM_CONFIG, MODE);
        // SharedPreferences.Editor editor = sharedPreferences.edit();
        // String str = sharedPreferences.getString(ITEM_PROXY_PORT, "80");

        String str = null;
        // final String selection = "name = 'CMCC DM' and numeric=\""
        final String selection = "name = 'CMCC DM' and numeric=\""
                + android.os.SystemProperties.get(
                            PhoneFactory.getProperty(TelephonyProperties.PROPERTY_ICC_OPERATOR_NUMERIC, curPhoneId), "") + "\"";

        Cursor cursor = context.getContentResolver().query(
        (curPhoneId== 0)?Telephony.Carriers.CONTENT_URI:Telephony.Carriers.getContentUri(curPhoneId,null),
        null,      selection, null, null);

        if (cursor != null) {
            if (cursor.getCount() > 0 && cursor.moveToFirst()) {
                str = cursor.getString(cursor.getColumnIndexOrThrow(Telephony.Carriers.PORT));
            }
            cursor.close();
        }

        Log.d(TAG, "getProxyPort: " + str);
        return str;
    }

    public void setProxyPort(Context context, String port) {
        Log.d(TAG, "setProxyPort: " + port);

        // SharedPreferences sharedPreferences =
        // context.getSharedPreferences(DM_CONFIG, MODE);
        // SharedPreferences.Editor editor = sharedPreferences.edit();
        // editor.putString(ITEM_PROXY_PORT, port);
        // editor.commit();

 //       if (!Vdmc.getInstance().isVDMRunning()) {
            final String selection = "name = 'CMCC DM' and numeric=\""
                    + android.os.SystemProperties.get(
                            PhoneFactory.getProperty(TelephonyProperties.PROPERTY_ICC_OPERATOR_NUMERIC, curPhoneId), "") + "\"";
            ContentValues values = new ContentValues();

            values.put(Telephony.Carriers.PORT, port);
            int c = values.size() > 0 ? context.getContentResolver().update(
                    (curPhoneId== 0)?Telephony.Carriers.CONTENT_URI:Telephony.Carriers.getContentUri(curPhoneId,null), 
                    values, selection, null) : 0;
            Log.d(TAG, "setProxyPort: values.size() = " + values.size());
            Log.d(TAG, "setProxyPort: update count = " + c);

            mProxyPortTemp = null;
/*        } else {
            mProxyPortTemp = port;
            Log.d(TAG, "setProxyPort: dm session is running, save value temporarily!");
        }
*/        
    }

    // add by lihui
    public void getServerNonce(Context context, byte[] data) {
        ByteBuffer buf = null;
        SharedPreferences sharedPreferences = context.getSharedPreferences(DM_CONFIG, MODE);
        String str = sharedPreferences.getString(ITEM_SERVER_NONCE, "ffff");
        Log.d(TAG, "getServerNonce:= " + str);
        if (data == null) {
            Log.d(TAG, "read: data is null!");
            return;
        }
        buf = ByteBuffer.wrap(data);
        Log.d(TAG, "read: buf = " + buf);
        buf.put(str.getBytes());

    }

    public void setServerNonce(Context context, byte[] data) {
        String ServerNonce = new String(data);
        Log.d(TAG, "setServerNonce:=" + ServerNonce);

        SharedPreferences sharedPreferences = context.getSharedPreferences(DM_CONFIG, MODE);
        SharedPreferences.Editor editor = sharedPreferences.edit();
        editor.putString(ITEM_SERVER_NONCE, ServerNonce);
        editor.commit();
    }

    public void getClientNonce(Context context, byte[] data) {
        ByteBuffer buf = null;
        SharedPreferences sharedPreferences = context.getSharedPreferences(DM_CONFIG, MODE);
        String str = sharedPreferences.getString(ITEM_CLIENT_NONCE, "ffff");
        Log.d(TAG, "getClientNonce= " + str);

        if (data == null) {
            Log.d(TAG, "read: data is null!");
            return;
        }
        buf = ByteBuffer.wrap(data);
        Log.d(TAG, "read: buf = " + buf);
        buf.put(str.getBytes());

    }

    public void setClientNonce(Context context, byte[] data) {
        String ClientNonce = new String(data);
        Log.d(TAG, "setClientNonce= " + ClientNonce);

        SharedPreferences sharedPreferences = context.getSharedPreferences(DM_CONFIG, MODE);
        SharedPreferences.Editor editor = sharedPreferences.edit();
        editor.putString(ITEM_CLIENT_NONCE, ClientNonce);
        editor.commit();
    }

    public String getManufactory() {
        Log.d(TAG, "getManufactory: " + mManufactory);
        return mManufactory;
    }

    protected void setManufactory(Context context, String manufactory) {
        Log.d(TAG, "setManufactory: " + manufactory);
        mManufactory = manufactory;

        SharedPreferences sharedPreferences = context.getSharedPreferences(DM_CONFIG, MODE);
        SharedPreferences.Editor editor = sharedPreferences.edit();
        editor.putString(ITEM_MANUFACTORY, manufactory);
        editor.commit();
    }

    public String getModel() {
        Log.d(TAG, "getModel: " + mModel);
        return mModel;
    }

    protected void setModel(Context context, String model) {
        Log.d(TAG, "setModel: " + model);
        mModel = model;

        SharedPreferences sharedPreferences = context.getSharedPreferences(DM_CONFIG, MODE);
        SharedPreferences.Editor editor = sharedPreferences.edit();
        editor.putString(ITEM_MODEL, model);
        editor.commit();
    }

    public String getImei() {
        Log.d(TAG, "getImei: " + mImeiStr);
	if (null == mImeiStr)
		{
            mImeiStr = mTelephonyManager[0].getDeviceId();
        	Log.d(TAG, "getImei: " + mImeiStr);
		}
        //todo
        //need to fix
       // return "861683010001601";
        return mImeiStr;
    }

    protected void setImei(Context context, String imei) {
        Log.d(TAG, "setImei: " + imei);
        mImeiStr = imei;

        SharedPreferences sharedPreferences = context.getSharedPreferences(DM_CONFIG, MODE);
        SharedPreferences.Editor editor = sharedPreferences.edit();
        editor.putString(ITEM_IMEI, imei);
        editor.commit();
    }

    public String getSoftwareVersion() {
        Log.d(TAG, "getSoftwareVersion: " + mSoftVer);
        return mSoftVer;
    }

    protected void setSoftwareVersion(Context context, String softVer) {
        Log.d(TAG, "setSoftwareVersion: " + softVer);
        mSoftVer = softVer;

        SharedPreferences sharedPreferences = context.getSharedPreferences(DM_CONFIG, MODE);
        SharedPreferences.Editor editor = sharedPreferences.edit();
        editor.putString(ITEM_SOFTVER, softVer);
        editor.commit();
    }

    // send message body
    private void sendMsgBody() {
        String destAddr = getSmsAddr();
        short destPort = (short) Integer.parseInt(getSmsPort());
        short srcPort = destPort;
        SmsManager smsManager = SmsManager.getDefault(curPhoneId);
        byte[] data; // sms body byte stream
        String smsBody; // sms body string format
        String imei = getImei();
        String softVer = getSoftwareVersion();
        String manStr = getManufactory();
        String modStr = getModel();

        Log.d(TAG, "sendMsgBody: Enter!");

        // smsbody: IMEI:860206000003972/Hisense/TS7032/TI7.2.01.22.00
        smsBody = "IMEI:" + imei + "/" + manStr + "/" + modStr + "/" + softVer;
        Log.d(TAG, "sendMsgBody: " + smsBody);
        data = smsBody.getBytes();
        for (int i = 0; i < smsBody.length(); i++) {
            Log.d(TAG, "sendMsgBody: ============= data[" + i + "] = " + data[i] + "\n");
        }

        Log.d(TAG, "sendMsgBody: dest addr = " + destAddr);
        Log.d(TAG, "sendMsgBody: dest port = " + destPort);

        //add for 105942 begin
        Log.d(TAG, "sendMsgBody: IS_CONFIG_SELFREG_REPLY = " + IS_CONFIG_SELFREG_REPLY);
        if (false == IS_CONFIG_SELFREG_REPLY){
            Intent sentIntent = new Intent(SENT_SMS_ACTION);  
	    sentIntent.putExtra("phone_id", curPhoneId);
            PendingIntent sentPI = PendingIntent.getBroadcast(this, 0, sentIntent,  0);  
              
            Intent deliverIntent = new Intent(DELIVERED_SMS_ACTION);  
	    deliverIntent.putExtra("phone_id", curPhoneId);
            PendingIntent deliverPI = PendingIntent.getBroadcast(this, 0,  deliverIntent, 0);

            IntentFilter mFilter = new IntentFilter(SENT_SMS_ACTION);
            mFilter.addAction(DELIVERED_SMS_ACTION);
            registerReceiver(sendMessageReceiver, mFilter);

            smsManager.sendDmDataMessage(destAddr, null, destPort, srcPort, data, sentPI, deliverPI );
            return;
         }
        //add for 105942 end
       

        smsManager.sendDmDataMessage(destAddr, null, /* use the default SMSC */
        destPort, srcPort, data, null, /* do not need listen to send result */
        null /* do not require delivery report */);
    }

    
   //add for 105942 begin
    private BroadcastReceiver sendMessageReceiver = new BroadcastReceiver() {  
            @Override  
            public void onReceive(Context context, Intent intent) {
                        String actionName = intent.getAction();
                        int resultCode = getResultCode();
			int phoneid = intent.getIntExtra("phone_id", 0);
			Log.d(TAG, "sendMessageReceiver onReceive phoneid=" + phoneid);
			Log.d(TAG, "sendMessageReceiver onReceive actionName=" + actionName + " resultCode="+resultCode);
                        if (actionName.equals(SENT_SMS_ACTION) 
                            || actionName.equals(DELIVERED_SMS_ACTION) ){
                                switch (resultCode) {
                                case Activity.RESULT_OK:
					curPhoneId = phoneid;
                                        TelephonyManager mTelephonyManager = (TelephonyManager) mContext.getSystemService(
                                                PhoneFactory.getServiceName(mContext.TELEPHONY_SERVICE, curPhoneId));
                                        
                                        String imsi = mTelephonyManager.getSubscriberId();

                                        saveImsi(mContext, imsi);
                                        setSelfRegState(mContext, true);
                                        stopListeningServiceState();
                                        break;
                                default:
                                         Log.d(TAG, "sendMessageReceiver Send Error ");
                                        break;
                                }
                        } 
                }
   };
    //add for 105942 end
    

    // Send self registe message
    private void sendSelfRegMsg() {
        if(SUPPORT_DM_SELF_REG){
            Log.d(TAG, "enter sendSelfRegMsg()");
            initConnectParam();
           if (!getSelfRegSwitch()) {
               Log.d(TAG, "sendSelfRegMsg: self registe switch is closed, no need send self registe message!");
               stopListeningServiceState();
               return;
           }

           if (isHaveSendSelfRegMsg()) {
               Log.d(TAG, "sendSelfRegMsg: have send self registe message!");
               stopListeningServiceState();
               return;
           }

           Log.d(TAG, "sendSelfRegMsg: Enter!");
           stopListeningServiceState();
           if (isNeedSelfReg()) {
           synchronized( keepcurphone)
               {
                   sendMsgBody();
               setIsHaveSendSelfRegMsg(mContext, true);
               }
           } else {
               setSelfRegState(mContext, true);
           }
        }               
    }

    // Send self registe message for debug mode
    protected void sendSelfRegMsgForDebug() {
        // send message directly under debug mode
        Log.d(TAG, "sendSelfRegMsgForDebug: Enter!");
        sendMsgBody();
        setIsHaveSendSelfRegMsg(mContext, true);
    }
}
