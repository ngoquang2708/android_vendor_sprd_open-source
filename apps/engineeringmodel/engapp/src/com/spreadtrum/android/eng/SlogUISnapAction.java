package com.spreadtrum.android.eng;

import com.spreadtrum.android.eng.R;

import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.widget.Toast;

/* SlogUI Added by Yuntao.xiao*/

public class SlogUISnapAction extends Activity {
    private static final String TAG = "SlogUISnapAction";
/*    private HandlerThread mThread;
    private Handler mHandler;
    private Runnable mTimeOutRunnable ;
    {
        mThread = new HandlerThread("SnapHandler");
        mThread.start();
        mHandler = new Handler (mThread.getLooper());
    }
*/

    @Override
    protected void onCreate(Bundle snap) {
        super.onCreate(snap);
        /*mTimeOutRunnable = new Runnable() {
            @Override
            public void run() {
                Toast.makeText(SlogUISnapAction.this
                    , getText(R.string.toast_snap_timeout)
                    , Toast.LENGTH_SHORT )
                .show();
            }
        };
        mHandler.postDelayed(mTimeOutRunnable, 2000);
        //if (true)
        try {
            SlogAction.snap(this, mHandler, mTimeOutRunnable);
        } catch (ExceptionInInitializerError error) {
            // TODO: NEED IMPROVE. If the activity have not started, we can't
            // send message to the main thread of application, because it have
            // not init yet.
            // The sender must be fixed.
            android.util.Log.e(TAG, "Illegal state because the activity was uninitialized. Need improve");
        }*/
        finish();
        //
        //setTheme()
    }
}
