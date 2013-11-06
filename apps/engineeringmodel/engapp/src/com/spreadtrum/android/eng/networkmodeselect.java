package com.spreadtrum.android.eng;

import android.os.AsyncResult;
import android.os.Bundle;
import android.os.Debug;
import android.os.Handler;
import android.os.Looper;
import android.os.Message;
import android.preference.ListPreference;
import android.preference.Preference;
import android.preference.PreferenceActivity;
import android.telephony.TelephonyManager;
import android.util.Log;

import com.android.internal.telephony.Phone;
import com.android.internal.telephony.PhoneFactory;

public class networkmodeselect extends PreferenceActivity
implements Preference.OnPreferenceChangeListener{
    private static final boolean DEBUG = Debug.isDebug();
    private static final String LOG_TAG = "networkmodeselect";
    private static final boolean DBG = true;
    private static final String KEY_NETW = "preferred_network_mode_key";
    static final int preferredNetworkMode = Phone.PREFERRED_NT_MODE;
    //private int valueofsms = 0;
    private int mModemType = 0;
//    private Phone mPhone;
    private Phone mPhone[];
    private MyHandler mHandler;
    private ListPreference mButtonPreferredNetworkMode;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
//    addPreferencesFromResource(R.layout.networkselect);
        mModemType = TelephonyManager.getDefault().getModemType();
        log("modem type: " + mModemType);
        if ((mModemType == TelephonyManager.MODEM_TYPE_TDSCDMA)
            || (mModemType == TelephonyManager.MODEM_TYPE_WCDMA)) {
            addPreferencesFromResource(R.layout.networkselect);
        } else {
            addPreferencesFromResource(R.layout.networkselect_gsm_only);
        }
        mButtonPreferredNetworkMode = (ListPreference) findPreference(KEY_NETW);
        mButtonPreferredNetworkMode.setOnPreferenceChangeListener(this);
        if (mModemType == TelephonyManager.MODEM_TYPE_WCDMA) {
            mButtonPreferredNetworkMode.setEntries(R.array.preferred_network_mode_choices_wcdma);
        }

        Looper looper;
        looper = Looper.myLooper();
        mHandler = new MyHandler(looper);

        mPhone = new Phone[2];
        //phone = PhoneFactory.getDefaultPhone();
        boolean isCardReady = PhoneFactory.isCardReady(0);
        if (isCardReady) {
            mPhone[0] = PhoneFactory.getPhone(0);
            isCardReady = PhoneFactory.isCardReady(1);
            if (isCardReady) {
                mPhone[1] = PhoneFactory.getPhone(1);
            }
            else {
                mPhone[1] = null;
            }
        }
        else {
            mPhone[0] = null;
            isCardReady = PhoneFactory.isCardReady(1);
            if (isCardReady) {
                mPhone[1] = PhoneFactory.getPhone(1);
            }
            else {
                mPhone[1] = null;
            }
        }
       // int settingsNetworkMode = android.provider.Settings.Secure.getInt(mPhone.getContext().
         //       getContentResolver(),android.provider.Settings.Secure.PREFERRED_NETWORK_MODE,
           //     preferredNetworkMode);

//    mPhone.getPreferredNetworkType(mHandler
//                        .obtainMessage(MyHandler.MESSAGE_GET_PREFERRED_NETWORK_TYPE));

        for(int i=0; i<2; i++) {
            if(mPhone[i] != null) {
                mPhone[i].getPreferredNetworkType(mHandler
                     .obtainMessage(MyHandler.MESSAGE_GET_PREFERRED_NETWORK_TYPE));
            }
        }
    }

    private class MyHandler extends Handler {

        private static final int MESSAGE_GET_PREFERRED_NETWORK_TYPE = 0;
        private static final int MESSAGE_SET_PREFERRED_NETWORK_TYPE = 1;
        public MyHandler(Looper looper) {
            super(looper);
        }
        @Override
        public void handleMessage(Message msg) {
            switch (msg.what) {
                case MESSAGE_GET_PREFERRED_NETWORK_TYPE:
                    handleGetPreferredNetworkTypeResponse(msg);
                    break;

                case MESSAGE_SET_PREFERRED_NETWORK_TYPE:
                    handleSetPreferredNetworkTypeResponse(msg);
                    break;
            }
        }

        private void handleGetPreferredNetworkTypeResponse(Message msg) {
            AsyncResult ar = (AsyncResult) msg.obj;
            if (ar.exception == null) {
                int modemNetworkMode = ((int[])ar.result)[0];

            if (DBG) {
                log ("handleGetPreferredNetworkTypeResponse: modemNetworkMode = " + modemNetworkMode);
            }

                //int settingsNetworkMode = android.provider.Settings.Secure.getInt(
                  //      mPhone.getContext().getContentResolver(),
                  //      android.provider.Settings.Secure.PREFERRED_NETWORK_MODE,
                   //     preferredNetworkMode);
            int settingsNetworkMode = 0;
            for(int i=0; i<2; i++) {
                if(mPhone[i] != null) {
                    settingsNetworkMode = android.provider.Settings.Secure.getInt(
                        mPhone[i].getContext().getContentResolver(),
                        android.provider.Settings.Secure.PREFERRED_NETWORK_MODE,
                        preferredNetworkMode);
                }
            }

            if (DBG) {
                log("handleGetPreferredNetworkTypeReponse: settingsNetworkMode = " +
                    settingsNetworkMode);
            }

            if (modemNetworkMode == Phone.PREFERRED_NT_MODE ||
                modemNetworkMode == Phone.NT_MODE_GSM_ONLY ||
                modemNetworkMode == Phone.NT_MODE_WCDMA_ONLY ) {
                if (DBG) {
                    log("handleGetPreferredNetworkTypeResponse: if 1: modemNetworkMode = " +
                        modemNetworkMode);
                }

                //check changes in modemNetworkMode and updates settingsNetworkMode
                if (modemNetworkMode != settingsNetworkMode) {
                    if (DBG) {
                        log("handleGetPreferredNetworkTypeResponse: if 2: " +
                                    "modemNetworkMode != settingsNetworkMode");
                    }

                    settingsNetworkMode = modemNetworkMode;

                    if (DBG) { log("handleGetPreferredNetworkTypeResponse: if 2: " +
                                "settingsNetworkMode = " + settingsNetworkMode);
                    }

                    //changes the Settings.System accordingly to modemNetworkMode
                    for(int i=0; i<2; i++) {
                        if(mPhone[i] != null) {
                            android.provider.Settings.Secure.putInt(
                                mPhone[i].getContext().getContentResolver(),
                                android.provider.Settings.Secure.PREFERRED_NETWORK_MODE,
                                settingsNetworkMode );
                            }
                        }
                    }
                    if (DBG) { log (" 4 "); }
                    UpdatePreferredNetworkModeSummary(modemNetworkMode);
                    // changes the mButtonPreferredNetworkMode accordingly to modemNetworkMode
                    mButtonPreferredNetworkMode.setValue(Integer.toString(modemNetworkMode));
                } else {
                    if (DBG) log("handleGetPreferredNetworkTypeResponse: else: reset to default");
                    resetNetworkModeToDefault();
                }
            }
        }

        private void handleSetPreferredNetworkTypeResponse(Message msg) {
            AsyncResult ar = (AsyncResult) msg.obj;
            if (ar.exception == null) {
                int networkMode = Integer.valueOf(
                    mButtonPreferredNetworkMode.getValue()).intValue();
                for(int i=0; i<2; i++) {
                    if(mPhone[i] != null) {
                        android.provider.Settings.Secure.putInt(mPhone[i].getContext().getContentResolver(),
                        android.provider.Settings.Secure.PREFERRED_NETWORK_MODE,
                        networkMode );
                    }
                }
            } else {
                for(int i=0; i<2; i++) {
                    if(mPhone[i] != null) {
                        mPhone[i].getPreferredNetworkType(obtainMessage(MESSAGE_GET_PREFERRED_NETWORK_TYPE));
                    }
                }
            }
        }
        private void resetNetworkModeToDefault() {
            //set the mButtonPreferredNetworkMode
            mButtonPreferredNetworkMode.setValue(Integer.toString(preferredNetworkMode));
            //set the Settings.System
            for(int i=0; i<2; i++) {
                if(mPhone[i] != null) {
                    android.provider.Settings.Secure.putInt(mPhone[i].getContext().getContentResolver(),
                        android.provider.Settings.Secure.PREFERRED_NETWORK_MODE,
                        preferredNetworkMode );
                    //Set the Modem
                    mPhone[i].setPreferredNetworkType(preferredNetworkMode,
                        this.obtainMessage(MyHandler.MESSAGE_SET_PREFERRED_NETWORK_TYPE));
                 }
            }
        }

    }


    private void UpdatePreferredNetworkModeSummary(int NetworkMode) {
        switch(NetworkMode) {
            case Phone.PREFERRED_NT_MODE:
                mButtonPreferredNetworkMode.setSummary("Preferred network mode: Auto");
                break;
            case Phone.NT_MODE_GSM_ONLY:
                mButtonPreferredNetworkMode.setSummary("Preferred network mode: GSM only");
                break;
            case Phone.NT_MODE_WCDMA_ONLY:
                if (mModemType == TelephonyManager.MODEM_TYPE_TDSCDMA) {
                    mButtonPreferredNetworkMode.setSummary("Preferred network mode: TD-SCDMA only");
                } else if (mModemType == TelephonyManager.MODEM_TYPE_WCDMA) {
                    mButtonPreferredNetworkMode.setSummary("Preferred network mode: WCDMA only");
                }
                break;
            default:
                mButtonPreferredNetworkMode.setSummary("Preferred network mode: Auto");
                break;
        }
    }

    public boolean onPreferenceChange(Preference preference, Object objValue){
        // TODO Auto-generated method stub
        if (preference == mButtonPreferredNetworkMode) {
            //NOTE onPreferenceChange seems to be called even if there is no change
            //Check if the button value is changed from the System.Setting
            mButtonPreferredNetworkMode.setValue((String) objValue);
            int buttonNetworkMode;
            buttonNetworkMode = Integer.valueOf((String) objValue).intValue();
            int settingsNetworkMode = 0;
            for(int i=0; i<2; i++) {
                if(mPhone[i] != null) {
                    settingsNetworkMode = android.provider.Settings.Secure.getInt(
                        mPhone[i].getContext().getContentResolver(),
                        android.provider.Settings.Secure.PREFERRED_NETWORK_MODE, preferredNetworkMode);
                }
            }
            if (buttonNetworkMode != settingsNetworkMode) {
                int modemNetworkMode;
                switch(buttonNetworkMode) {
                case Phone.PREFERRED_NT_MODE:
                    modemNetworkMode = Phone.PREFERRED_NT_MODE;
                    break;
                case Phone.NT_MODE_GSM_ONLY:
                    modemNetworkMode = Phone.NT_MODE_GSM_ONLY;
                    break;
                case Phone.NT_MODE_WCDMA_ONLY:
                    modemNetworkMode = Phone.NT_MODE_WCDMA_ONLY;
                    break;
                default:
                    modemNetworkMode = Phone.PREFERRED_NT_MODE;
                }
                UpdatePreferredNetworkModeSummary(buttonNetworkMode);

                for(int i=0; i<2; i++) {
                    if(mPhone[i] != null) {
                        android.provider.Settings.Secure.putInt(mPhone[i].getContext().getContentResolver(),
                            android.provider.Settings.Secure.PREFERRED_NETWORK_MODE,
                            buttonNetworkMode );
                        //Set the modem network mode
                        mPhone[i].setPreferredNetworkType(modemNetworkMode, mHandler
                             .obtainMessage(MyHandler.MESSAGE_SET_PREFERRED_NETWORK_TYPE));
                    }
                }
            }
            /*Add 20130129 Spreadst of 121769 check whether the dialog is dismiss start  */
            if(mButtonPreferredNetworkMode.getDialog()!=null){
                mButtonPreferredNetworkMode.getDialog().dismiss();
            }
            /*Add 20130129 Spreadst of 121769 check whether the dialog is dismiss end   */
            finish();
        }

        // always let the preference setting proceed.
        return true;
    }

    private static void log(String msg) {
        if(DEBUG) Log.d(LOG_TAG, msg);
    }

}

