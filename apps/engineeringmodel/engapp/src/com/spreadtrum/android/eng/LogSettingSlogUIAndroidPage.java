package com.spreadtrum.android.eng;

import com.spreadtrum.android.eng.R;

import android.app.Activity;
import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.view.View.OnClickListener;
import android.widget.CheckBox;

/* SlogUI Added by Yuntao.xiao*/

public class LogSettingSlogUIAndroidPage extends Activity implements SlogUISyncState {
    // Dim views
    private CheckBox chkGeneral;
    private CheckBox chkSystem;
    private CheckBox chkRadio;
    private CheckBox chkKernel;
    private CheckBox chkMain;
    private CheckBox chkEvent;

    // public static Handler mHandler;
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_android);

        // Init views
        chkGeneral = (CheckBox) findViewById(R.id.chk_android_general);
        chkSystem = (CheckBox) findViewById(R.id.chk_android_system);
        chkRadio = (CheckBox) findViewById(R.id.chk_android_radio);
        chkKernel = (CheckBox) findViewById(R.id.chk_android_kernel);
        chkMain = (CheckBox) findViewById(R.id.chk_android_main);
        chkEvent = (CheckBox) findViewById(R.id.chk_android_event);

        // Make sure that, views match to slog.conf
        // No need to run syncState in onCreate.
        // syncState();

        // Set onclick listenner
        ClkListenner chklisten = new ClkListenner();
        chkGeneral.setOnClickListener(chklisten);
        chkSystem.setOnClickListener(chklisten);
        chkRadio.setOnClickListener(chklisten);
        chkKernel.setOnClickListener(chklisten);
        chkMain.setOnClickListener(chklisten);
        chkEvent.setOnClickListener(chklisten);

    }

    // Dim On click Listenner
    class ClkListenner implements OnClickListener {
        public void onClick(View onClickView) {

            switch (onClickView.getId()) {
            case R.id.chk_android_general:
                SlogAction.SetState(SlogAction.ANDROIDKEY,
                        chkGeneral.isChecked());
                syncState();
                break;

            case R.id.chk_android_system:
                SlogAction.SetState(SlogAction.SYSTEMKEY,
                        chkSystem.isChecked(), false);
                break;

            case R.id.chk_android_kernel:
                SlogAction.SetState(SlogAction.KERNELKEY,
                        chkKernel.isChecked(), false);
                break;

            case R.id.chk_android_main:
                SlogAction.SetState(SlogAction.MAINKEY, chkMain.isChecked(),
                        false);
                break;

            case R.id.chk_android_event:
                SlogAction.SetState(SlogAction.EVENTKEY, chkEvent.isChecked(),
                        false);
                break;

            case R.id.chk_android_radio:
                SlogAction.SetState(SlogAction.RADIOKEY, chkRadio.isChecked(),
                        false);
                break;

            default:
                Log.w("Slog->AndroidPage", "Wrong id given.");
            }

            return;
        }

    }

    @Override
    protected void onResume() {
        super.onResume();

        syncState();
    }

    @Override
    public void syncState() {
        boolean tempHost = SlogAction.GetState(SlogAction.ANDROIDKEY);
        chkGeneral.setEnabled(true);
        boolean tempHostOn = SlogAction.GetState(SlogAction.GENERALKEY, true).equals(SlogAction.GENERALON);
        boolean tempHostLowPower = SlogAction.GetState(SlogAction.GENERALKEY, true).equals(SlogAction.GENERALLOWPOWER);
        boolean tempHostGen = tempHostOn || tempHostLowPower;
        if (!tempHostGen) {
            chkGeneral.setEnabled(false);
            tempHost = false;
        }

        chkGeneral.setChecked(tempHost);

        SlogAction.SetCheckBoxBranchState(chkSystem, tempHost,
                SlogAction.GetState(SlogAction.SYSTEMKEY));
        SlogAction.SetCheckBoxBranchState(chkRadio, tempHost,
                SlogAction.GetState(SlogAction.RADIOKEY));
        SlogAction.SetCheckBoxBranchState(chkKernel, tempHost,
                SlogAction.GetState(SlogAction.KERNELKEY));
        SlogAction.SetCheckBoxBranchState(chkMain, tempHost,
                SlogAction.GetState(SlogAction.MAINKEY));
        SlogAction.SetCheckBoxBranchState(chkEvent, tempHost,
                SlogAction.GetState(SlogAction.EVENTKEY));

    }
    
    @Override
    public void onSlogConfigChanged() {
        syncState();
    }
}
