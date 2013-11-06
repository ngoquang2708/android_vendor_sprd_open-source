package com.spreadtrum.android.eng;

import java.lang.reflect.Method;

import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.os.Debug;
import android.os.RemoteException;
import android.preference.CheckBoxPreference;
import android.preference.Preference;
import android.preference.PreferenceActivity;
import android.preference.PreferenceScreen;
import android.telephony.TelephonyManager;
import android.util.Log;

import com.android.internal.telephony.ITelephony;

public class ApnActivepdpFilter extends PreferenceActivity{
    private static final boolean DEBUG = Debug.isDebug();
	private String LOG_TAG = "ApnActivepdpFilter";
	//private int mPrefCount = 0;
	private boolean mChecked = false;
	private CheckBoxPreference mFilterAll;
	private CheckBoxPreference mFilterDefault;
	private CheckBoxPreference mFilterMms;
	private CheckBoxPreference mFilterSupl;
	private CheckBoxPreference mFilterDun;
	private CheckBoxPreference mFilterHipri;

    static final String APN_TYPE_ALL = "all";
    /** APN type for default data traffic */
    static final String APN_TYPE_DEFAULT = "default";
    /** APN type for MMS traffic */
    static final String APN_TYPE_MMS = "mms";
    /** APN type for SUPL assisted GPS */
    static final String APN_TYPE_SUPL = "supl";
    /** APN type for DUN traffic */
    static final String APN_TYPE_DUN = "dun";
    /** APN type for HiPri traffic */
    static final String APN_TYPE_HIPRI = "hipri";

    static final String ALL_TYPE_APN = "*";

    //private TelephonyManager mTelephonyManager;
    private ITelephony  mTelephony;
    boolean mFilterAllStatus ;
    boolean mFilterDefaultStatus;
    boolean mFilterMmsStatus;
    boolean mFilterSuplStatus;
    boolean mFilterDunStatus;
    boolean mFilterHipriStatus;

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		// TODO Auto-generated method stub
		super.onCreate(savedInstanceState);


		// ITelephony mTelephony =(ITelephony)ITelephony.Stub.asInterface(ServiceManager.getService("phone"));
        //mTelephonyManager = (TelephonyManager)getSystemService(TELEPHONY_SERVICE);
        mTelephony = getITelephony(this);

        addPreferencesFromResource(R.layout.apn_activepdp_filter);

		//mPrefCount = getPreferenceScreen().getPreferenceCount();
		mFilterAll = (CheckBoxPreference) findPreference(APN_TYPE_ALL);
		mFilterDefault= (CheckBoxPreference) findPreference(APN_TYPE_DEFAULT);
		mFilterMms= (CheckBoxPreference) findPreference(APN_TYPE_MMS);
		mFilterSupl= (CheckBoxPreference) findPreference(APN_TYPE_SUPL);
		mFilterDun= (CheckBoxPreference) findPreference(APN_TYPE_DUN);
		mFilterHipri= (CheckBoxPreference) findPreference(APN_TYPE_HIPRI);

		updateApnFilterState();

	}


	void updateApnFilterState()
	{
		try {
			mFilterAllStatus = mTelephony.getApnActivePdpFilter(ALL_TYPE_APN);
			mFilterDefaultStatus =getITelephony(this).getApnActivePdpFilter(APN_TYPE_DEFAULT);
			mFilterMmsStatus = getITelephony(this).getApnActivePdpFilter(APN_TYPE_MMS);
			mFilterSuplStatus = getITelephony(this).getApnActivePdpFilter(APN_TYPE_SUPL);
			mFilterDunStatus = getITelephony(this).getApnActivePdpFilter(APN_TYPE_DUN);
			mFilterHipriStatus = getITelephony(this).getApnActivePdpFilter(APN_TYPE_HIPRI);

		} catch (RemoteException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
		} catch (SecurityException e) {
            e.printStackTrace();
        }

		if(mFilterDefaultStatus)
		{
			mFilterDefault.setChecked(true);
			mFilterDefault.setSummary("enable filter");
		}
		else
		{
			mFilterDefault.setChecked(false);
			mFilterDefault.setSummary("disable filter");
		}

		if(mFilterMmsStatus)
		{
			mFilterMms.setChecked(true);
			mFilterMms.setSummary("enable filter");
		}
		else
		{
			mFilterMms.setChecked(false);
			mFilterMms.setSummary("disable filter");
		}


		if(mFilterSuplStatus)
		{
			mFilterSupl.setChecked(true);
			mFilterSupl.setSummary("enable filter");
		}
		else
		{
			mFilterSupl.setChecked(false);
			mFilterSupl.setSummary("disable filter");
		}


		if(mFilterDunStatus)
		{
			mFilterDun.setChecked(true);
			mFilterDun.setSummary("enable filter");

		}
		else
		{
			mFilterDun.setChecked(false);
			mFilterDun.setSummary("disable filter");
		}

		if(mFilterHipriStatus)
		{
			mFilterHipri.setChecked(true);
			mFilterHipri.setSummary("enable filter");
		}
		else
		{
			mFilterHipri.setChecked(false);
			mFilterHipri.setSummary("disable filter");
		}


		if(mFilterAllStatus)
		{
			mFilterAll.setChecked(true);
			mFilterAll.setSummary("enable filter");
			mFilterDefault.setEnabled(false);
			mFilterMms.setEnabled(false);
			mFilterSupl.setEnabled(false);
			mFilterDun.setEnabled(false);
			mFilterHipri.setEnabled(false);
		}
		else
		{
			mFilterAll.setChecked(false);
			mFilterAll.setSummary("disable filter");
			mFilterDefault.setEnabled(true);
			mFilterMms.setEnabled(true);
			mFilterSupl.setEnabled(true);
			mFilterDun.setEnabled(true);
			mFilterHipri.setEnabled(true);
		}
	}


	@Override
	protected void onResume() {
		// TODO Auto-generated method stub
		super.onResume();
		updateApnFilterState();
	}


	@Override
	public boolean onPreferenceTreeClick(PreferenceScreen preferenceScreen,
			Preference preference) {
		String key = preference.getKey();
		if("networkinfo".equals(key))
		{
			Intent startIntent = new Intent();
	        startIntent.setClassName("com.android.settings", "com.android.settings.RadioInfo");
	        startActivity(startIntent);
			return super.onPreferenceTreeClick(preferenceScreen, preference);
		}
		if(preference instanceof CheckBoxPreference){
			mChecked = ((CheckBoxPreference)preference).isChecked();
		}
		if(APN_TYPE_ALL.equals(key))
		{
			key = ALL_TYPE_APN;
		}

		if(DEBUG) Log.d(LOG_TAG, "onPreferenceChange(), " + key+mChecked);

	   	try {
			 mTelephony.setApnActivePdpFilter(key,mChecked);
			 }
			 catch (RemoteException e) {
				// TODO Auto-generated catch block
				e.printStackTrace();
			} catch (SecurityException e) {
	            e.printStackTrace();
		}


		/*if (APN_TYPE_ALL.equals(key))
		{
			mFilterAllStatus = mChecked;
        }
		else if (APN_TYPE_DEFAULT.equals(key))
		{
			mFilterDefaultStatus = mChecked;
		}
        else if(APN_TYPE_MMS.equals(key))
        {
        	mFilterMmsStatus = mChecked;
        }
        else if(APN_TYPE_SUPL.equals(key))
        {
        	mFilterSuplStatus = mChecked;
        }
        else if(APN_TYPE_DUN.equals(key))
        {
        	mFilterDunStatus = mChecked;
        }
        else if(APN_TYPE_HIPRI.equals(key))
        {
        	mFilterHipriStatus = mChecked;
        }
        else
        {
        }*/
		updateApnFilterState();
		return super.onPreferenceTreeClick(preferenceScreen, preference);
	}

	@Override
	protected void onDestroy() {
		// TODO Auto-generated method stub
		super.onDestroy();
	}

    private static ITelephony getITelephony(Context context) {
        TelephonyManager mTelephonyManager = (TelephonyManager) context
                .getSystemService(TELEPHONY_SERVICE);
        Class<TelephonyManager> c = TelephonyManager.class;
        Method getITelephonyMethod = null;
        ITelephony iTelephony = null ;
        try {
            getITelephonyMethod = c.getDeclaredMethod("getITelephony",
                    (Class[]) null);
            getITelephonyMethod.setAccessible(true);
        } catch (SecurityException e) {
            e.printStackTrace();
        } catch (NoSuchMethodException e) {
            e.printStackTrace();
        }

        try {
            iTelephony = (ITelephony) getITelephonyMethod.invoke(
                    mTelephonyManager, (Object[]) null);
            return iTelephony;
        } catch (Exception e) {
            e.printStackTrace();
        }
        return iTelephony;
    }



}

