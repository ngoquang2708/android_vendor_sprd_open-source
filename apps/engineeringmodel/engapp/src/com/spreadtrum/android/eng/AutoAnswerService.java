package com.spreadtrum.android.eng;

import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.os.Debug;
import android.os.Handler;
import android.os.IBinder;
import android.os.Message;
import android.os.RemoteException;
import android.os.ServiceManager;
import android.telephony.PhoneStateListener;
import android.telephony.TelephonyManager;
import android.util.Log;

import com.android.internal.telephony.ITelephony;

public class AutoAnswerService extends Service{
    private static final boolean DEBUG = Debug.isDebug();
    private final String TAG = "AutoAnswerService";
    /*Modify 20130204 spreadst of 123833 let sim1's auto answer start */
//    private TelephonyManager mTele1;
//    private TelephonyManager mTele2;
    private TelephonyManager mTele;
    /*Modify 20130204 spreadst of 123833 let sim1's auto answer end */
    private MyPhoneStateListener mListener;

    private static final int MSG_DELAY_AUTOANSWER = 0;
    private MyHandler mHandler = new MyHandler();

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public void onCreate(){
        super.onCreate();
    }

    @Override
    public void onStart(Intent intent,int startId) {
        super.onStart(intent, startId);

        if(DEBUG) Log.d(TAG, "listen PhoneState");
        /*Modify 20130204 spreadst of 123833 let sim1's auto answer start */
//        mTele1 = (TelephonyManager)getSystemService(TELEPHONY_SERVICE + 0);
//        mTele2 = (TelephonyManager)getSystemService(TELEPHONY_SERVICE + 1);
//        mTele1 = (TelephonyManager) getSystemService(PhoneFactory
//                .getServiceName(Context.TELEPHONY_SERVICE, 0));
//        mTele2 = (TelephonyManager) getSystemService(PhoneFactory
//                .getServiceName(Context.TELEPHONY_SERVICE, 1));
        mTele = (TelephonyManager) getSystemService(Context.TELEPHONY_SERVICE);
        mListener = new MyPhoneStateListener();
//        mTele1.listen(mListener, PhoneStateListener.LISTEN_CALL_STATE);
//        mTele2.listen(mListener, PhoneStateListener.LISTEN_CALL_STATE);
        mTele.listen(mListener, PhoneStateListener.LISTEN_CALL_STATE);
        /*Modify 20130204 spreadst of 123833 let sim1's auto answer end */
     }

    @Override
    public void onDestroy() {
        super.onDestroy();
        if(DEBUG) Log.d(TAG, "AutoAnswerService onDestroy");
        /*Modify 20130204 spreadst of 123833 let sim1's auto answer start */
//        mTele1.listen(mListener, PhoneStateListener.LISTEN_NONE);
//        mTele2.listen(mListener, PhoneStateListener.LISTEN_NONE);
        mTele.listen(mListener, PhoneStateListener.LISTEN_NONE);
        /*Modify 20130204 spreadst of 123833 let sim1's auto answer end */
    }

    private final class MyHandler extends Handler {
        @Override
        public void handleMessage(Message msg) {
            switch(msg.what) {
                case MSG_DELAY_AUTOANSWER:{
                        if(DEBUG) Log.d(TAG, "MSG_DELAY_AUTOANSWER");
                        ITelephony iTele = ITelephony.Stub.asInterface(ServiceManager.getService("phone"));
                        try {
                            if (iTele != null && iTele.isRinging() && iTele.isVTCall()) {
                                iTele.silenceRinger();
                                iTele.answerRingingCall();
                            }
                        } catch (RemoteException e) {
                            e.printStackTrace();
                        }
                    }
                    break;
                default:
                    break;
            }
        }
    }
    class MyPhoneStateListener extends PhoneStateListener {

        @Override
        public void onCallStateChanged(int state, String incomingNumber) {
            super.onCallStateChanged(state, incomingNumber);
            switch(state) {
                case TelephonyManager.CALL_STATE_IDLE:
                    if(DEBUG) Log.d(TAG, "IDLE");
                    //Toast.makeText(context, "IDLE", Toast.LENGTH_SHORT).show();
                    break;

                case TelephonyManager.CALL_STATE_OFFHOOK:
                    //Toast.makeText(context,"OFFHOOK", Toast.LENGTH_SHORT).show();
                    if(DEBUG) Log.d(TAG, "OFFHOOK");
                    break;

                case TelephonyManager.CALL_STATE_RINGING:
                    //Toast.makeText(context,"RINGING", Toast.LENGTH_SHORT).show();
                    if(DEBUG) Log.d(TAG, "RINGING");
                    try {
                        //StartCall();
                        answerPhoneAidl();
                    } catch (Exception e) {

                    }
                    break;
            }
        }
    }

    private void answerPhoneAidl() throws Exception {
        // Class c = Class.forName(telephonyMgr.getClass().getName());
        // Method m = c.getDeclaredMethod("getITelephony", (Class[])null);
        // m.setAccessible(true);
        // ITelephony telephonyService;
        // telephonyService = (ITelephony)m.invoke(telephonyMgr,(Object[])null);
        ITelephony iTele = ITelephony.Stub.asInterface(ServiceManager.getService("phone"));

        if(iTele != null){
            // Silence the ringer and answer the call!
            // telephonyService.silenceRinger();
            if (iTele.isVTCall()) {
                // for vt call, mediaphone need some time to initialize, so delay 4500ms to answer.
                if(DEBUG) Log.d(TAG, "is VT call");
                mHandler.removeMessages(MSG_DELAY_AUTOANSWER);
                mHandler.sendEmptyMessageDelayed(MSG_DELAY_AUTOANSWER , 4500);
            } else if (iTele.isRinging()){
                if(DEBUG) Log.d(TAG, "answer ringing call");
                iTele.silenceRinger();
                iTele.answerRingingCall();
            }
       }
    }
}
