package com.spreadtrum.android.eng;

import com.spreadtrum.android.eng.EngInstallHelperService;

import java.io.File;

import android.net.Uri;
import android.os.Bundle;
import android.app.Activity;
import android.content.Intent;
import android.util.Log;
import android.view.Menu;
import android.view.View;

public class EngInstallActivity extends Activity {
    private static final String TAG = "EngInstallActivity";

    public static final int RETURN_EXCEPTION = -255;
    public static final int REQUEST_INSTALL = 10;
    public static final int REQUEST_DELETE = 12;
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // This activity should never start becasuse of security condition.
        /*
            Intent data = getIntent();
            if (data != null) {
                int requestCode = data.getIntExtra("action", 0);
                String extra = data.getStringExtra("name");
                if (extra == null || requestCode == 0) {
                    finish();
                }
                Intent intent = null;
                if (requestCode == REQUEST_INSTALL) {
                    intent = new Intent();
                    intent.setAction(Intent.ACTION_VIEW);
                    intent.setDataAndType(Uri.fromFile(new File(extra)), "application/vnd.android.package-archive");
                    intent.putExtra(Intent.EXTRA_RETURN_RESULT, true);
                    startActivityForResult(intent, requestCode);
                } else if (requestCode == REQUEST_DELETE) {
                    intent = new Intent(Intent.ACTION_DELETE,
                                Uri.parse("package:" + extra));
                    startActivityForResult(intent, requestCode);
                } else {
                    finish();
                }
            }
        */
        finish();
    }
/*
    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (data == null) {
            Log.e(TAG, "data is null, return");
            finish();
            super.onActivityResult(requestCode, resultCode, data);
            return ;
        }

        if (requestCode == REQUEST_INSTALL) {
            EngInstallHelperService.onResult(
                    data.getIntExtra("android.intent.extra.INSTALL_RESULT", RETURN_EXCEPTION));
        } else if (requestCode == REQUEST_DELETE) {
            EngInstallHelperService.onResult(
                    data.getIntExtra("android.intent.extra.INSTALL_RESULT", RETURN_EXCEPTION));
        } else {
            Log.e(TAG, "Unknow request code.");
            finish();
        }
        finish();
        super.onActivityResult(requestCode, resultCode, data);
    }
*/
}

