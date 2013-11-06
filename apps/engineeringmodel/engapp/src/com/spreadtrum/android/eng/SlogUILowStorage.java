package com.spreadtrum.android.eng;

import android.os.Bundle;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.DialogInterface;

public class SlogUILowStorage extends Activity {

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        AlertDialog lowDialog = new AlertDialog.Builder(this)
                .setTitle(getString(R.string.low_free_space_title))
                .setMessage(getString(R.string.low_free_space_message))
                .setPositiveButton(
                    getString(android.R.string.ok),
                    new DialogInterface.OnClickListener() {

                        @Override
                        public void onClick(DialogInterface dialog, int which) {
                            finish();

                        }
                    }
                ).setCancelable(false).create();
        lowDialog.show();
    }

}
