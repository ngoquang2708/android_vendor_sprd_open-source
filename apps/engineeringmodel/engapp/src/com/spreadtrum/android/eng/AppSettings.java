
package com.spreadtrum.android.eng;

import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.os.Debug;
import android.os.SystemProperties;
import android.preference.CheckBoxPreference;
import android.preference.Preference;
import android.preference.PreferenceActivity;
import android.preference.PreferenceScreen;
import android.provider.Settings;
import android.util.Log;

public class AppSettings extends PreferenceActivity {
    private static final boolean DEBUG = Debug.isDebug();
    private static final String LOG_TAG = "engineeringmodel";

    private static final String CALL_FORWARD_QUERY = "call_forward_query";
    private static final String AUTO_RETRY_DIAL = "emergency_call_retry";
    private static final String NOT_SHOW_DIALOG = "long_press_not_showDialog";

    private static final String CARD_LOG = "card_log";

    public static final String PREFS_NAME = "ENGINEERINGMODEL";

    private static final String AUTO_ANSWER = "autoanswer_call";
    private static final String ENABLE_MASS_STORAGE = "enable_mass_storage";
    private static final String ENABLE_VSER_GSER = "enable_vser_gser"; // add by
                                                                       // liguxiang
                                                                       // 07-12-11
                                                                       // for
                                                                       // engineeringmoodel
                                                                       // usb
                                                                       // settings

    private static final String ENABLE_USB_FACTORY_MODE = "enable_usb_factory_mode";
    private static final String ACCELEROMETER = "accelerometer_rotation";

    private static final String RETRY_SMS_CONTROL="persist.sys.msms_retry_control";
    private static final String MMS_REPORT_CONTROL="persist.sys.mms_read_report";
    private static final String MODEM_RESET = "modem_reset";

    private static final String ENG_TESTMODE = "engtestmode";

    private CheckBoxPreference mAutoAnswer;
    private CheckBoxPreference mEnableMassStorage;
    private CheckBoxPreference mEnableVserGser; // add by liguxiang 07-12-11 for
                                                // engineeringmoodel usb
                                                // settings
    private CheckBoxPreference mAcceRotation;
    private CheckBoxPreference mEnableUsbFactoryMode;
    private CheckBoxPreference mModemReset;
    private CheckBoxPreference mNotShowDialog;
    private CheckBoxPreference mSmsRetryControl;
    private CheckBoxPreference mMmsReprotControl;
    private EngSqlite mEngSqlite;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        addPreferencesFromResource(R.layout.appset);

        mAutoAnswer = (CheckBoxPreference) findPreference(AUTO_ANSWER);
        mEnableMassStorage = (CheckBoxPreference) findPreference(ENABLE_MASS_STORAGE);
        mEnableVserGser = (CheckBoxPreference) findPreference(ENABLE_VSER_GSER); // add
                                                                                 // by
                                                                                 // liguxiang
                                                                                 // 07-12-11
                                                                                 // for
                                                                                 // engineeringmoodel
                                                                                 // usb
                                                                                 // settings
        mAcceRotation = (CheckBoxPreference) findPreference(ACCELEROMETER);
        mEnableUsbFactoryMode = (CheckBoxPreference) findPreference(ENABLE_USB_FACTORY_MODE);
        mModemReset = (CheckBoxPreference) findPreference(MODEM_RESET);
        mNotShowDialog = (CheckBoxPreference) findPreference(NOT_SHOW_DIALOG);
        mSmsRetryControl = (CheckBoxPreference) findPreference("msms_retry_control");
        mMmsReprotControl = (CheckBoxPreference) findPreference("mms_report_control");

        String result = SystemProperties.get("persist.sys.sprd.modemreset");
        /*Add 20130805 spreadst of 198464 add SMS retry control start*/
        int smsControl= SystemProperties.getInt(RETRY_SMS_CONTROL,3);
        if(smsControl ==3){
            mSmsRetryControl.setChecked(true);
        }else{
            mSmsRetryControl.setChecked(false);
        }
        /*Add 20130805 spreadst of 198464 add SMS retry control end*/

        /*Add 20130821 spreadst of 203235 add MMS report control start*/
        boolean mmsReportControl= SystemProperties.getBoolean(MMS_REPORT_CONTROL, false);
        if(mmsReportControl == true){
            mMmsReprotControl.setChecked(true);
        }else{
            mMmsReprotControl.setChecked(false);
        }
        /*Add 20130821 spreadst of 203235 add MMS report control end*/

        boolean isShowDialog = Settings.System.getInt(this.getContentResolver(),
                Settings.System.LONG_PRESS_POWER_KEY, 0) == 1;
        if (isShowDialog) {
            mNotShowDialog.setChecked(true);
        } else {
            mNotShowDialog.setChecked(false);
        }
        if (DEBUG)
            Log.d(LOG_TAG, "result: " + result + ", result.equals(): " + (result.equals("1")));
        mModemReset.setChecked(result.equals("1"));
        mEngSqlite = EngSqlite.getInstance(this);
    }

    @Override
    protected void onResume() {
        boolean check = Settings.System.getInt(getContentResolver(),
                Settings.System.ACCELEROMETER_ROTATION, 1) == 1;
        if (check) {
            SystemProperties.set("persist.sys.acce_enable", check ? "1" : "0");
        }
        mAcceRotation.setChecked(check);
        String usbMode = SystemProperties.get("sys.usb.config", "");
        if (DEBUG)
            Log.d(LOG_TAG, " usbMode = " + usbMode);
        mEnableVserGser.setChecked(usbMode.endsWith("vser,gser"));
        mEnableMassStorage.setChecked(usbMode.startsWith("mass_storage"));
        boolean test = mEngSqlite.queryData(ENG_TESTMODE);
        if (!test) {
            mEnableUsbFactoryMode.setChecked(true);
        } else {
            int mode = mEngSqlite.queryFactoryModeDate(ENG_TESTMODE);
            mEnableUsbFactoryMode.setChecked(mode == 1);
        }

        super.onResume();
    }

    @Override
    protected void onDestroy() {
        mEngSqlite.release();
        super.onDestroy();
    }

    public boolean onPreferenceTreeClick(PreferenceScreen preferenceScreen, Preference preference) {

        if (preference == mAutoAnswer) {
            boolean newState = mAutoAnswer.isChecked();

            SharedPreferences settings = getSharedPreferences(PREFS_NAME, 0);
            // boolean silent = settings.getBoolean("autoanswer_call", false);
            SharedPreferences.Editor editor = settings.edit();
            editor.putBoolean("autoanswer_call", newState);
            editor.commit();

            if (newState) {
                getApplicationContext().startService(
                        new Intent(getApplicationContext(), AutoAnswerService.class));
            } else {
                getApplicationContext().stopService(
                        new Intent(getApplicationContext(), AutoAnswerService.class));
            }

            if (DEBUG)
                Log.d(LOG_TAG, "auto answer state " + newState);
            return true;
        }else if(preference == mEnableMassStorage){
            boolean newState = mEnableMassStorage.isChecked();
            Settings.Secure.putInt(getContentResolver(),
                    Settings.Secure.USB_MASS_STORAGE_ENABLED, newState ? 1 : 0);
            return true;
            // add by liguxiang 07-12-11 for engineeringmoodel usb settings
            // begin
        } else if (preference == mEnableVserGser) {
            boolean newState = mEnableVserGser.isChecked();
            Settings.Secure.putInt(getContentResolver(), Settings.Secure.VSER_GSER_ENABLED,
                    newState ? 1 : 0);
            return true;
        } else if (preference == mAcceRotation) {
            boolean checked = mAcceRotation.isChecked();
            // set accelerometer listener not be called in
            // SensorManager#ListenerDelegate
            SystemProperties.set("persist.sys.acce_enable", checked ? "1" : "0");
            // set Orientation of screen not be changed
            Settings.System.putInt(getContentResolver(), Settings.System.ACCELEROMETER_ROTATION,
                    checked ? 1 : 0);
            return true;
        } else if (preference == mEnableUsbFactoryMode) {
            boolean checked = mEnableUsbFactoryMode.isChecked();
            mEngSqlite.updataFactoryModeDB(ENG_TESTMODE, checked ? 1 : 0);
            return true;
        }
        // add by liguxiang 07-12-11 for engineeringmoodel usb settings end

        final String key = preference.getKey();

        if (CALL_FORWARD_QUERY.equals(key)) {
            if (preference instanceof CheckBoxPreference) {
                SystemProperties.set("persist.sys.callforwarding",
                        ((CheckBoxPreference) preference).isChecked() ? "1" : "0");
            }
            return true;
        } else if (AUTO_RETRY_DIAL.equals(key)) {
            if (preference instanceof CheckBoxPreference) {
                SystemProperties.set("persist.sys.emergencyCallRetry",
                        ((CheckBoxPreference) preference).isChecked() ? "1" : "0");
            }
            return true;
        } else if (CARD_LOG.equals(key)) {
            if (preference instanceof CheckBoxPreference) {
                SystemProperties.set("persist.sys.cardlog",
                        ((CheckBoxPreference) preference).isChecked() ? "1" : "0");
            }
            return true;
        } else if ("modem_reset".equals(key)) {
            if (preference instanceof CheckBoxPreference) {
                SystemProperties.set("persist.sys.sprd.modemreset",
                        ((CheckBoxPreference) preference).isChecked() ? "1" : "0");
            }
            return true;
        } else if (NOT_SHOW_DIALOG.equals(key)) {
            if (preference instanceof CheckBoxPreference) {
                Settings.System.putInt(this.getContentResolver(),
                        Settings.System.LONG_PRESS_POWER_KEY,
                        ((CheckBoxPreference) preference).isChecked() ? 1 : 0);
            }
            return true;
            /*Add 20130805 spreadst of 198464 add SMS retry control start*/
        } else if ("msms_retry_control".equals(key)) {
            if (preference instanceof CheckBoxPreference) {
                SystemProperties.set(RETRY_SMS_CONTROL,
                        ((CheckBoxPreference) preference).isChecked() ? "3" : "0");
            }
            return true;
            /*Add 20130805 spreadst of 198464 add SMS retry control end*/
            /*Add 20130821 spreadst of 203235 add MMS report control start*/
        } else if ("mms_report_control".equals(key)) {
            if (preference instanceof CheckBoxPreference) {
                SystemProperties.set(MMS_REPORT_CONTROL,
                        ((CheckBoxPreference) preference).isChecked() ? "1" : "0");
            }
            return true;
            /*Add 20130821 spreadst of 203235 add MMS report control end*/
        }else {
            return false;
        }

    }
}
