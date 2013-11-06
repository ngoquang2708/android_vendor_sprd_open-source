package com.spreadtrum.android.eng;

/* Actually engconstents and engfetch are em... incorrect or bad coding style */
import com.spreadtrum.android.eng.engconstents;
import com.spreadtrum.android.eng.engfetch;
import com.spreadtrum.android.eng.R;

import android.app.Activity;
import android.os.Bundle;
import android.view.View;
import android.view.View.OnClickListener;
import android.widget.CheckBox;
import android.widget.Toast;

/* SlogUI Added by Yuntao.xiao*/

public class LogSettingSlogUIModemPage extends Activity implements SlogUISyncState {

    private CheckBox chkModem, chkBlueTooth, chkTcp, chkMisc;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_modem);

        chkModem = (CheckBox) findViewById(R.id.chk_modem_branch);
        chkBlueTooth = (CheckBox) findViewById(R.id.chk_modem_bluetooth);
        chkTcp = (CheckBox) findViewById(R.id.chk_modem_tcp);
        chkMisc = (CheckBox) findViewById(R.id.chk_modem_misc);

        // TRIM CODE
        // no need to run syncState in onCreate
        // syncState();

        // TODO: Should better using setOnClickListener(this) instead of newing a class.
        ClkListenner clickListen = new ClkListenner();
        VersionListener verListen = new VersionListener();
        chkModem.setOnClickListener(clickListen);
        chkBlueTooth.setOnClickListener(clickListen);
        chkTcp.setOnClickListener(clickListen);
        chkMisc.setOnClickListener(clickListen);
        chkMisc.setOnLongClickListener(verListen);

    }

    @Override
    protected void onResume() {
        super.onResume();
        syncState();
    }

    protected class VersionListener implements View.OnLongClickListener {
        @Override
        public boolean onLongClick(View v) {
            switch (v.getId()) {
            case R.id.chk_modem_misc:
                Toast.makeText(LogSettingSlogUIModemPage.this,
                        R.string.slog_version_info, Toast.LENGTH_LONG).show();
            break;
            default:
            }
            return false;
        }
    }

    protected class ClkListenner implements View.OnClickListener {
        public void onClick(View onClickView) {
            switch (onClickView.getId()) {
            case R.id.chk_modem_branch:
                SlogAction.SetState(SlogAction.MODEMKEY, chkModem.isChecked(),
                        false);
                new Thread() {
                    @Override
                    public void run() {
                        SlogAction.sendATCommand(engconstents.ENG_AT_SETARMLOG, chkModem.isChecked());
                    }
                } .start();
                break;
            case R.id.chk_modem_bluetooth:
                SlogAction.SetState(SlogAction.BLUETOOTHKEY,
                        chkBlueTooth.isChecked(), false);
                break;
            case R.id.chk_modem_tcp:
                SlogAction.SetState(SlogAction.TCPKEY, chkTcp.isChecked(),
                        false);
                new Thread() {
                    @Override
                    public void run() {
                        SlogAction.sendATCommand(engconstents.ENG_AT_SETCAPLOG, chkTcp.isChecked());
                    }
                }.start();
                break;
            case R.id.chk_modem_misc:
                SlogAction.SetState(SlogAction.MISCKEY, chkMisc.isChecked(),
                        false);
                break;

            }
        }
    }

    @Override
    public void syncState() {
        boolean tempHostOn = SlogAction.GetState(SlogAction.GENERALKEY, true)
                                        .equals(SlogAction.GENERALON);
        boolean tempHostLowPower = SlogAction.GetState(SlogAction.GENERALKEY, true)
                                        .equals(SlogAction.GENERALLOWPOWER);
        boolean tempHost = tempHostOn || tempHostLowPower;
        SlogAction.SetCheckBoxBranchState(chkModem, tempHost,
                SlogAction.GetState(SlogAction.MODEMKEY));
        SlogAction.SetCheckBoxBranchState(chkBlueTooth,
                tempHost && SlogAction.GetState(SlogAction.STORAGEKEY),
                SlogAction.GetState(SlogAction.BLUETOOTHKEY));
        SlogAction.SetCheckBoxBranchState(chkTcp, tempHost,
                SlogAction.GetState(SlogAction.TCPKEY));
        SlogAction.SetCheckBoxBranchState(chkMisc, tempHost,
                SlogAction.GetState(SlogAction.MISCKEY));

    }

    // TODO: Underconstruction
    @Override
    public void onSlogConfigChanged() {
        syncState();
    }

}
