package com.spreadtrum.android.eng;

import com.spreadtrum.android.eng.R;
import com.spreadtrum.android.eng.SlogProvider;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.Dialog;
import android.content.ComponentName;
import android.content.ContentValues;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.res.Configuration;
import android.content.ServiceConnection;
import android.database.Cursor;
import android.os.Bundle;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.Message;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.view.View.OnClickListener;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.CursorAdapter;
import android.widget.EditText;
import android.widget.RadioButton;
import android.widget.TextView;
import android.widget.Toast;

import com.android.internal.app.IMediaContainerService;

public class LogSettingSlogUICommonControl extends Activity implements SlogUISyncState {
    private static final int SET_STATE_COMPLETE = 1;

    private Button btnClear;
    private Button btnDump;
    private RadioButton rdoGeneralOn;
    private RadioButton rdoGeneralOff;
    private RadioButton rdoGeneralLowPower;
    private CheckBox chkAndroid;
    private CheckBox chkModem;
    private CheckBox chkAlwaysRun;
    private CheckBox chkSnap;
    private CheckBox chkClearLogAuto;
    private CheckBox chkTCP;
    private CheckBox chkBlueTooth;
    private RadioButton rdoNAND;
    private RadioButton rdoSDCard;
    private Intent intentSvc;
    private Intent intentSnap;
    private Intent intentMedia;

    private AlertDialog.Builder mModeListDialogBuilder;
    private EditText mNewModeNameEditText;
    private AlertDialog mNewModeDialog;
    private CursorAdapter mModeListAdapter;
    private DialogInterface.OnClickListener mUpdateListener;
    private DialogInterface.OnClickListener mSettingListener;
    private DialogInterface.OnClickListener mDeleteListener;

    private MainThreadHandler mHandler;
    private class MainThreadHandler extends Handler {
        public MainThreadHandler(Looper looper) {
            super(looper);
        }

        @Override
        public void handleMessage(Message msg) {
            switch(msg.what) {
                case SET_STATE_COMPLETE:
                    syncState();
                    break;
                default:
                    break;
            }
        }
    };

    private IMediaContainerService mMediaContainer;
    private static final ComponentName DEFAULT_CONTAINER_COMPONENT = new ComponentName(
            "com.android.defcontainer", "com.android.defcontainer.DefaultContainerService");
    // Only to prepare to get freespace from sdcard because engmode is a system process.
    final private ServiceConnection mMediaContainerConn = new ServiceConnection() {
        public void onServiceConnected(ComponentName name, IBinder service) {
            final IMediaContainerService imcs = IMediaContainerService.Stub
            .asInterface(service);
            mMediaContainer = imcs;
        }

        public void onServiceDisconnected(ComponentName name) {
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_general_commoncontrol);

        mHandler = new MainThreadHandler(getMainLooper());

        // Init views
        rdoGeneralOn = (RadioButton) findViewById(R.id.rdo_general_on);
        rdoGeneralOff = (RadioButton) findViewById(R.id.rdo_general_off);
        rdoGeneralLowPower = (RadioButton) findViewById(R.id.rdo_general_lowpower);
        chkAndroid = (CheckBox) findViewById(R.id.chk_general_android_switch);
        chkModem = (CheckBox) findViewById(R.id.chk_general_modem_switch);
        chkAlwaysRun = (CheckBox) findViewById(R.id.chk_general_alwaysrun);
        chkSnap = (CheckBox) findViewById(R.id.chk_general_snapsvc);
        chkClearLogAuto = (CheckBox) findViewById(R.id.chk_general_autoclear);
        chkTCP = (CheckBox) findViewById(R.id.chk_general_tcp);
        chkBlueTooth = (CheckBox) findViewById(R.id.chk_general_bluetooth);
        rdoNAND = (RadioButton) findViewById(R.id.rdo_general_nand);
        rdoSDCard = (RadioButton) findViewById(R.id.rdo_general_sdcard);
        btnClear = (Button) findViewById(R.id.btn_general_clearall);
        btnDump = (Button) findViewById(R.id.btn_general_dump);

        // Init intents
        intentSvc = new Intent("svcSlog");
        intentSvc.setClass(this, SlogService.class);
        intentSnap = new Intent("svcSnap");
        intentSnap.setClass(LogSettingSlogUICommonControl.this,
                SlogUISnapService.class);
        intentMedia = new Intent().setComponent(DEFAULT_CONTAINER_COMPONENT);
        boolean success = getApplicationContext().bindService(intentMedia, mMediaContainerConn, Context.BIND_AUTO_CREATE);
        if (!success) {
            Log.e("SlogUI", "Unable to bind MediaContainerService!");
        }
        
        final LayoutInflater inflater = (LayoutInflater) getSystemService(
                                            Context.LAYOUT_INFLATER_SERVICE);

        mModeListAdapter = new CursorAdapter(this, getContentResolver()
                .query(SlogProvider.URI_MODES, null,null, null, null), true) {

            @Override
            public View newView(Context context, Cursor cursor, ViewGroup parent) {
                View view = inflater.inflate(
                        android.R.layout.simple_list_item_1, parent, false);
                return view;
            }

            @Override
            public void bindView(View view, Context context, Cursor cursor) {
                TextView text = (TextView) view
                        .findViewById(android.R.id.text1);
                text.setText(cursor.getString(cursor.getColumnIndex(
                        SlogProvider.Contract.COLUMN_MODE)));

            }
        };

        // Sync view's status
        // No need to run syncState in onCreate
        // syncState();
        prepareModeListDialogs();

        chkAlwaysRun.setChecked (SlogAction.isAlwaysRun (SlogAction.SERVICESLOG));
        chkSnap.setChecked (SlogAction.isAlwaysRun (SlogAction.SERVICESNAP));

        if (chkAlwaysRun.isChecked()) {
            startService(intentSvc);
        }

        // Fetch onclick listenner
        ClkListenner clickListen = new ClkListenner();
        rdoGeneralOn.setOnClickListener(clickListen);
        rdoGeneralOff.setOnClickListener(clickListen);
        rdoGeneralLowPower.setOnClickListener(clickListen);
        chkAndroid.setOnClickListener(clickListen);
        chkModem.setOnClickListener(clickListen);
        chkTCP.setOnClickListener(clickListen);
        chkBlueTooth.setOnClickListener(clickListen);
        rdoNAND.setOnClickListener(clickListen);
        rdoSDCard.setOnClickListener(clickListen);
        btnClear.setOnClickListener(clickListen);
        btnDump.setOnClickListener(clickListen);
        chkAlwaysRun.setOnClickListener(clickListen);
        chkSnap.setOnClickListener(clickListen);
        chkClearLogAuto.setOnClickListener(clickListen);

    }

    private void prepareModeListDialogs() {

        mNewModeNameEditText = new EditText(
                LogSettingSlogUICommonControl.this);
        if (mNewModeNameEditText == null) {
            return ;
        }
        mNewModeNameEditText.setSingleLine(true);
        mNewModeDialog = new AlertDialog.Builder(LogSettingSlogUICommonControl.this)
                .setTitle(R.string.mode_dialog_add_title)
                .setView(mNewModeNameEditText)
                .setPositiveButton(R.string.alert_dump_dialog_ok,
                        new DialogInterface.OnClickListener() {
                            @Override
                            public void onClick(DialogInterface dialog,
                                    int whichButton) {
                                if (null == mNewModeNameEditText.getText()) {
                                   return;
                                }
                                String modeName = mNewModeNameEditText.getText().toString();
                                if ("".equals(modeName)) {
                                    return;
                                }
                                Cursor cursor = LogSettingSlogUICommonControl.this.
                                        getContentResolver().query(
                                                SlogProvider.URI_MODES, null, null, null, null);
                                while (cursor.moveToNext()) {
                                    if (modeName.equals(
                                            cursor.getString(cursor.getColumnIndex(
                                                    SlogProvider.Contract.COLUMN_MODE)))) {
                                        final int id = cursor.getInt(cursor.getColumnIndex(
                                                SlogProvider.Contract._ID));
                                        showDuplicatedModeDialog(modeName, id);

                                        cursor.close();
                                        return;
                                    }
                                }
                                cursor.close();
                                SlogAction.saveAsNewMode(modeName,
                                        LogSettingSlogUICommonControl.this);

                        }
                })
                .setNegativeButton(R.string.alert_dump_dialog_cancel,
                    new DialogInterface.OnClickListener() {
                            public void onClick(DialogInterface dialog,
                                    int whichButton) {

                                /* User clicked cancel so do some stuff */
                            }
                        }).create();
        mModeListDialogBuilder = new AlertDialog
                                .Builder(LogSettingSlogUICommonControl.this);
        mUpdateListener = new DialogInterface.OnClickListener() {
            @Override
            public void onClick(DialogInterface dialog, int which) {
                int id = (int) mModeListAdapter.getItemId(which);
                SlogAction.updateMode(LogSettingSlogUICommonControl.this, id);
            }
        };

        mSettingListener = new DialogInterface.OnClickListener() {
            @Override
            public void onClick(DialogInterface dialog, final int which) {
                Thread setThread = new Thread() {
                    @Override
                    public void run() {
                        SlogAction.setAllStates(LogSettingSlogUICommonControl.this,
                                    (int) mModeListAdapter.getItemId(which));
                        mHandler.sendEmptyMessage(SET_STATE_COMPLETE);
                    }
                };
                setThread.start();
            }
        };
        
        mDeleteListener = new DialogInterface.OnClickListener() {
            @Override
            public void onClick(DialogInterface dialog, final int which) {
                int id = (int) mModeListAdapter.getItemId(which);
                SlogAction.deleteMode(LogSettingSlogUICommonControl.this, id);
            }
        };
    }

    private void showDuplicatedModeDialog(String modeName, final int modeId) {
        DialogInterface.OnClickListener listener =
                new DialogInterface.OnClickListener() {
            @Override
            public void onClick(DialogInterface dialog, final int which) {
                SlogAction.updateMode(LogSettingSlogUICommonControl.this, modeId);
            }
        };
        DialogInterface.OnClickListener listenerCancel =
                new DialogInterface.OnClickListener() {
            @Override
            public void onClick(DialogInterface dialog, final int which) {
                mNewModeDialog.show();
            }
        };
        new AlertDialog.Builder(LogSettingSlogUICommonControl.this)
                .setTitle(R.string.mode_dialog_duplicate_title)
                .setMessage(R.string.mode_dialog_duplicate_message)
                .setPositiveButton(
                        R.string.mode_dialog_duplicate_positive, listener)
                .setNegativeButton(
                        R.string.mode_dialog_duplicate_negative, listenerCancel)
                .create().show();

    }

    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        getMenuInflater().inflate(R.menu.slog_mode_menu, menu);
        return true;
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        switch (item.getItemId()) {
        case R.id.slog_add_mode:
            mNewModeDialog.show();
            return true;
        case R.id.slog_delete:
            mModeListAdapter.notifyDataSetChanged();
            mModeListDialogBuilder.setTitle(R.string.mode_dialog_delete_title)
                .setAdapter(mModeListAdapter, mDeleteListener).create().show();
            return true;
        case R.id.slog_select:
            mModeListAdapter.notifyDataSetChanged();
            mModeListDialogBuilder.setTitle(R.string.mode_dialog_select_title)
                .setAdapter(mModeListAdapter, mSettingListener).create().show();
            return true;
        case R.id.slog_update:
            mModeListAdapter.notifyDataSetChanged();
            mModeListDialogBuilder.setTitle(R.string.mode_dialog_update_title)
                .setAdapter(mModeListAdapter, mUpdateListener).create().show();
            return true;
        default:
            return super.onOptionsItemSelected(item);
        }

    }

    @Override
    protected void onResume() {
        super.onResume();
        syncState();
    }

    @Override
    public void syncState() {
        // Set checkbox status
        boolean tempHostOn = SlogAction.GetState(SlogAction.GENERALKEY, true).equals(SlogAction.GENERALON);
        boolean tempHostLowPower = SlogAction.GetState(SlogAction.GENERALKEY, true).equals(SlogAction.GENERALLOWPOWER);
        if (tempHostOn) {
            rdoGeneralOn.setChecked(true);
        } else if (tempHostLowPower) {
            rdoGeneralLowPower.setChecked(true);
        } else {
            rdoGeneralOff.setChecked(true);
        }

        boolean tempHost = tempHostOn || tempHostLowPower;
        boolean isSDCard = SlogAction.GetState(SlogAction.STORAGEKEY);
        boolean isMountSDCard = SlogAction.IsHaveSDCard();
        SlogAction.SetCheckBoxBranchState(chkAndroid, tempHost,
                SlogAction.GetState(SlogAction.ANDROIDKEY));

        SlogAction.SetCheckBoxBranchState(chkModem, tempHost,
                SlogAction.GetState(SlogAction.MODEMKEY));
        SlogAction.SetCheckBoxBranchState(chkClearLogAuto, tempHost,
                SlogAction.GetState(SlogAction.CLEARLOGAUTOKEY));
        SlogAction.SetCheckBoxBranchState(chkTCP, tempHost && isSDCard && isMountSDCard,
                SlogAction.GetState(SlogAction.TCPKEY));
        SlogAction.SetCheckBoxBranchState(chkBlueTooth, tempHost && isSDCard && isMountSDCard,
                SlogAction.GetState(SlogAction.BLUETOOTHKEY));

        btnDump.setEnabled(isSDCard && isMountSDCard);

        // Set Radio buttons
        rdoSDCard.setEnabled(SlogAction.IsHaveSDCard() ? true : false);
        rdoSDCard.setChecked(isSDCard);
        rdoNAND.setChecked(!isSDCard);

        chkSnap.setEnabled(tempHostOn);

        if (chkSnap.isChecked() && tempHostOn) {
            startService(intentSnap);
        } else {
            stopService(intentSnap);
        }

        // set clear all logs
        if (tempHost) {
            btnClear.setEnabled(false);
        } else {
            btnClear.setEnabled(true);
        }

    }

    protected class ClkListenner implements OnClickListener {

        public void onClick(View v) {

            switch (v.getId()) {
            case R.id.rdo_general_on:
                SlogAction.SetState(SlogAction.GENERALKEY,
                        SlogAction.GENERALON, true);
                syncState();
                break;
                
            case R.id.rdo_general_off:
                SlogAction.SetState(SlogAction.GENERALKEY,
                        SlogAction.GENERALOFF, true);
                if (rdoGeneralOff.isChecked()) {
                    requestDumpLog();
                }
                syncState();
            break;
            
            case R.id.rdo_general_lowpower:
                SlogAction.SetState(SlogAction.GENERALKEY,
                        SlogAction.GENERALLOWPOWER, true);
                syncState();
            break;
            
            case R.id.chk_general_android_switch:
                SlogAction.SetState(SlogAction.ANDROIDKEY,
                        chkAndroid.isChecked());
            break;

            case R.id.chk_general_modem_switch:
                SlogAction.SetState(SlogAction.MODEMKEY, chkModem.isChecked(),
                        false);
                new Thread() {
                    @Override
                    public void run() {
                        SlogAction.sendATCommand(engconstents.ENG_AT_SETARMLOG, chkModem.isChecked());
                    }
                } .start();
            break;

            case R.id.chk_general_bluetooth:
                SlogAction.SetState(SlogAction.BLUETOOTHKEY,
                        chkBlueTooth.isChecked(), false);
            break;

            case R.id.chk_general_tcp:
                SlogAction.SetState(SlogAction.TCPKEY, chkTCP.isChecked(),
                        false);
                new Thread() {
                    @Override
                    public void run() {
                        SlogAction.sendATCommand(engconstents.ENG_AT_SETCAPLOG, chkTCP.isChecked());
                    }
                }.start();
            break;

            case R.id.chk_general_alwaysrun:
                SlogAction.setAlwaysRun(SlogAction.SERVICESLOG,
                        chkAlwaysRun.isChecked());
                if (chkAlwaysRun.isChecked()) {
                    // start slog service
                    if (startService(intentSvc) == null) {
                        Toast.makeText(
                                LogSettingSlogUICommonControl.this,
                                getText(R.string.toast_service_slog_start_failed),
                                Toast.LENGTH_SHORT).show();
                    }
                } else {
                    // stop slog service
                    if (!stopService(intentSvc)) {
                        Toast.makeText(
                                LogSettingSlogUICommonControl.this,
                                getText(R.string.toast_service_slog_end_failed),
                                Toast.LENGTH_SHORT).show();
                    }
                }
                break;

            case R.id.chk_general_snapsvc:
                if (chkSnap.isChecked()) {
                    // start snap service
                    Toast.makeText(LogSettingSlogUICommonControl.this,
                            getText(R.string.toast_snap_prompt),
                            Toast.LENGTH_SHORT).show();
                    if (startService(intentSnap) == null) {
                        Toast.makeText(
                                LogSettingSlogUICommonControl.this,
                                getText(R.string.toast_service_snap_start_failed),
                                Toast.LENGTH_SHORT).show();
                    }
                } else {
                    // stop snap service
                    if (!stopService(intentSnap)) {
                        Toast.makeText(
                                LogSettingSlogUICommonControl.this,
                                getText(R.string.toast_service_snap_end_failed),
                                Toast.LENGTH_SHORT).show();
                    }
                }
                SlogAction.setAlwaysRun(SlogAction.SERVICESNAP,
                        chkSnap.isChecked());
                break;

            case R.id.chk_general_autoclear:
                SlogAction.SetState (SlogAction.CLEARLOGAUTOKEY, chkClearLogAuto.isChecked(), true);
                break;

            case R.id.rdo_general_nand:
                Toast.makeText(
                        LogSettingSlogUICommonControl.this,
                        getText(R.string.toast_freespace_nand)
                                + String.valueOf(SlogAction
                                        .GetFreeSpace(mMediaContainer, SlogAction.STORAGENAND))
                                + "MB", Toast.LENGTH_SHORT).show();
                SlogAction.SetState(SlogAction.STORAGEKEY,
                        rdoSDCard.isChecked(), true);
                btnDump.setEnabled(false);
                chkBlueTooth.setEnabled(false);
                chkTCP.setEnabled(false);
                break;
            case R.id.rdo_general_sdcard:
                if (SlogAction.IsHaveSDCard()) {
                    Toast.makeText(
                            LogSettingSlogUICommonControl.this,
                            getText(R.string.toast_freespace_sdcard)
                                    + String.valueOf(SlogAction
                                            .GetFreeSpace(mMediaContainer, SlogAction.STORAGESDCARD))
                                    + "MB", Toast.LENGTH_SHORT).show();
                }
                SlogAction.SetState(SlogAction.STORAGEKEY,
                        rdoSDCard.isChecked(), true);
                btnDump.setEnabled(true && SlogAction.IsHaveSDCard());
                chkBlueTooth.setEnabled(true);
                chkTCP.setEnabled(true);
                break;

            case R.id.btn_general_clearall:
                clearLog();
                break;

            case R.id.btn_general_dump:
                dumpLog();
                break;
            }

        }
    }

    void dumpLog() {

        final EditText edtDump = new EditText(
                LogSettingSlogUICommonControl.this);
        if (edtDump == null) {
            return ;
        }
        edtDump.setSingleLine(true);
        new AlertDialog.Builder(LogSettingSlogUICommonControl.this)
                //
                .setIcon(android.R.drawable.ic_dialog_alert)
                .setTitle(R.string.alert_dump_title)
                .setView(edtDump)
                .setPositiveButton(R.string.alert_dump_dialog_ok,
                        new DialogInterface.OnClickListener() {
                            public void onClick(DialogInterface dialog,
                                    int whichButton) {
                                if (null == edtDump.getText()) {
                                   return;
                                }
                                String fileName = edtDump.getText().toString();
                                java.util.regex.Pattern pattern = java.util.regex.Pattern.compile("[0-9a-zA-Z]*");
                                if ((pattern.matcher(fileName).matches() && !"".equals(fileName)) 
                                        && fileName.length() <= 40) {
                                    SlogAction.dump(edtDump.getText()
                                        .toString());
                                } else {
                                    Toast.makeText(LogSettingSlogUICommonControl.this
                                                , fileName.length() > 40 ?
                                                getText(R.string.toast_dump_input_limited)
                                                : getText(R.string.toast_dump_filename_error)
                                                , Toast.LENGTH_LONG)
                                            .show();
                                }
                                    /* User clicked OK so do some stuff */
                        }
                })
                .setNegativeButton(R.string.alert_dump_dialog_cancel,
                    new DialogInterface.OnClickListener() {
                            public void onClick(DialogInterface dialog,
                                    int whichButton) {

                                /* User clicked cancel so do some stuff */
                            }
                        }).create().show();
        }

        void clearLog() {

            new AlertDialog.Builder(LogSettingSlogUICommonControl.this)
                    .setIcon(android.R.drawable.ic_dialog_alert)
                    .setTitle(R.string.alert_clear_title)
                    .setMessage(R.string.alert_clear_string)
                    .setPositiveButton(R.string.alert_clear_dialog_ok,
                            new DialogInterface.OnClickListener() {
                                public void onClick(DialogInterface dialog,
                                        int whichButton) {
                                    SlogAction.ClearLog();
                                    /* User clicked OK so do some stuff */
                                }
                            })
                    .setNegativeButton(R.string.alert_clear_dialog_cancel, null)
                    .create().show();

        }

    @Override
    protected Dialog onCreateDialog(int id) {
        return super.onCreateDialog(id);
    }

    void requestDumpLog() {
        new AlertDialog.Builder(LogSettingSlogUICommonControl.this)
                .setIcon(android.R.drawable.ic_dialog_alert)
                .setTitle(R.string.alert_request_dump_title)
                .setMessage(R.string.alert_request_dump_prompt)
                .setPositiveButton(R.string.alert_dump_dialog_ok,
                        new DialogInterface.OnClickListener() {
                            public void onClick(DialogInterface dialog,
                                    int whichButton) {
                                if (rdoSDCard.isChecked()) {
                                    dumpLog();
                                }
                            }
                        })
                .setNegativeButton(R.string.alert_dump_dialog_cancel, null)
                .create().show();
    }

    // Resolve dialog missing when orientation start.
    @Override
    public void onConfigurationChanged(Configuration newConfig) {
        super.onConfigurationChanged(newConfig);
    }

    @Override
    public void onSlogConfigChanged(){
        syncState();
    }

}
