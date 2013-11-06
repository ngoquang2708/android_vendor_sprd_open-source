package com.spreadtrum.android.eng;



import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.nio.charset.Charset;
import java.util.ArrayList;
import java.util.List;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.DialogInterface;
import android.content.Intent;
import android.os.Bundle;
import android.os.Debug;
import android.os.Handler;
import android.os.Looper;
import android.os.Message;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.AdapterView;
import android.widget.AdapterView.OnItemClickListener;
import android.widget.ArrayAdapter;
import android.widget.EditText;
import android.widget.ListView;
import android.widget.Toast;

import com.android.internal.telephony.PhoneFactory;


public class uplmnsettings extends Activity {
    private static final boolean DEBUG = Debug.isDebug();
	private static final String LOG_TAG = "uplmnsettings";
	private static final boolean DBUG = false;
	private ListView listView = null;
	private EditText  mEditText01, mEditText02, mEditText03;
	private int sockid = 0;
	private engfetch mEf;
	private EventHandler mHandler;
	private String str;
	private boolean bNeedSet = true;
	private static final int LEN_UNIT = 10;
	private String[] showUPLMN = null ;
	private String[] originalUPLMN = null ;
	private String[] strUorG = null ;
	private byte[][] part = null;

	private byte[] setByte = new byte[6];
	private List<String> data = new ArrayList<String>();
	private int at_read_lenth = 0;
	private int lenth_get = 0;

	private int[] order = null;
	private int uplmn_list_num = 0;

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		setContentView(R.layout.uplmnlist);
		initialPara();
		listView = (ListView)findViewById(R.id.ListView01);
		listView.setAdapter(new ArrayAdapter<String>(this, android.R.layout.simple_list_item_1, data));
		listView.setOnItemClickListener(new OnItemClickListener(){
			public void onItemClick(AdapterView<?> arg0, View arg1, int position,
					long id) {
				showEditDialog(position);
				
			}});
		
	}

	private void initialPara() {
	    int phoneId = PhoneFactory.DEFAULT_PHONE_ID;
	    Intent intent = getIntent();
	    if(intent != null){
	        phoneId = intent.getIntExtra("sub_id", phoneId);
	    }
		mEf = new engfetch();
		sockid = mEf.engopen(phoneId);
		Looper looper;
		looper = Looper.myLooper();
		mHandler = new EventHandler(looper);
		mHandler.removeMessages(0);
		Message m = mHandler.obtainMessage(engconstents.ENG_AT_GETUPLMNLEN, 0, 0, 0);
		mHandler.sendMessage(m);
	}

	private void showEditDialog(int pos) {
            LayoutInflater factory = LayoutInflater.from(uplmnsettings.this);
            final View view = factory.inflate(R.layout.uplmn_edit, null);
            mEditText01 = (EditText)view.findViewById(R.id.index_value);
            mEditText02 = (EditText)view.findViewById(R.id.id_value);
            mEditText03 = (EditText)view.findViewById(R.id.type_value);
            mEditText01.setText(""+order[pos]);
            mEditText02.setText(showUPLMN[order[pos]].replaceAll("F", ""));
            mEditText03.setText(""+getvalueofUTRANorGSM(order[pos]));
            AlertDialog dialog = new AlertDialog.Builder(uplmnsettings.this)
		.setTitle("UPLMN set")
                .setView(view)
                .setPositiveButton(android.R.string.ok, new DialogInterface.OnClickListener() {
			public void onClick(DialogInterface dialog, int which) {
				// TODO Auto-generated method stub
				final String editIndex = mEditText01.getText().toString();
				final String editId = mEditText02.getText().toString();
				final String editTag = mEditText03.getText().toString();
				if(checkInputParametersIsWrong(editIndex, editId, editTag)){
					if(DEBUG) Log.d(LOG_TAG,"bNeedSet is set to false");
					bNeedSet = false;
				}else{
				changeDataFromEdit(       Integer.parseInt(mEditText01.getText().toString()),
									editId,
									Integer.parseInt(mEditText03.getText().toString())
								);
				}
				if(bNeedSet){
					if(DEBUG) Log.d(LOG_TAG,"ENG_AT_SETUPLMN");
					Message m = mHandler.obtainMessage(engconstents.ENG_AT_SETUPLMN, 0, 0, 0);
					mHandler.sendMessage(m);
				}
			}
		})
                .setNegativeButton(android.R.string.cancel, null)
                .create();
            dialog.show();
	}

	//Functions for uplmn set
	private String getPacketData(){
		String str = "";
		for(int i=0;i<lenth_get/5;i++){
			str = str + originalUPLMN[i];
		}
		if(DBUG){
			if(DEBUG) Log.d(LOG_TAG, "getPacketData---str="+str);
		}
		return str;
	}
	private boolean checkInputParametersIsWrong(String str01, String str02, String str03){
		boolean IsWrong = false;
		if(str01.length()==0){
			IsWrong = true;
			DisplayToast(getString(R.string.index_error_uplmn));
			return IsWrong;
		}
		if(str03.length()==0){
			IsWrong = true;
			DisplayToast(getString(R.string.type_is_emplty_error_uplmn));
			return IsWrong;
		}
		if(str02.length() < 5){
			IsWrong = true;
			DisplayToast(getString(R.string.number_too_short_uplmn));
			return IsWrong;
		}
		if(Integer.parseInt(str03) > 1 ||Integer.parseInt(str03) < 0){
			IsWrong = true;
			DisplayToast(getString(R.string.type_is_wrong_uplmn));
			return IsWrong;
		}
		return IsWrong;

	}
	private void changeDataFromEdit(int index, String strId, int tag){
		if(strId.length() == 5){
			String result = strId +"F";
			String compare = "";
			byte[] byteId05 = result.getBytes();
			for(int i=0; i<byteId05.length; i++){
				setByte[i] = byteId05[transferSpecialIntPlus( i, 0)];
			}

			if(tag == 0){
				compare = new String(setByte)+"8000";
			}
			else if(tag ==1){
				compare = new String(setByte)+"0080";
			}
			if(originalUPLMN[index].equals(compare))	{
				bNeedSet = false;
			}else{
				bNeedSet = true;
				originalUPLMN[index] = compare;
			}
			if(DBUG){
				if(DEBUG) Log.d(LOG_TAG, "changeDataFromEdit 555originalUPLMN["+index+"]"+originalUPLMN[index]);
			}
		}
		else if(strId.length() == 6){
			String compare01 = "";
			byte[] byteId06 = strId.getBytes();
			for(int i=0; i<byteId06.length; i++){
				setByte[i] = byteId06[transferSpecialIntPlus( i, 0)];
			}

			if(tag == 0){
				compare01 = new String(setByte)+"8000";
			}
			else if(tag ==1){
				compare01 = new String(setByte)+"0080";
			}
			if(originalUPLMN[index].equals(compare01))	{
				bNeedSet = false;
			}else{
				bNeedSet = true;
				originalUPLMN[index] = compare01;
			}
			if(DBUG){
				if(DEBUG) Log.d(LOG_TAG, "changeDataFromEdit 666originalUPLMN["+index+"]"+originalUPLMN[index]);
			}
		}
	}

	private void DisplayToast(String str) {
		Toast mToast = Toast.makeText(this, str, Toast.LENGTH_SHORT);
        /*Delete 20130225 Spreadst of 108373 the toast's location is too high start*/
        //mToast.setGravity(Gravity.TOP, 0, 100);
        /*Delete 20130225 Spreadst of 108373 the toast's location is too high end  */
		mToast.show();
	}
	//end set

	// Functions for uplmn list display
	private int getvalueofUTRANorGSM(int index){
			int value = 0;
			if(strUorG[index] == "G"){
				value = 1;
			}
			return value;
	}

	private void handleUTRANorGSM(byte [] input) {
		for(int i = 0; i < uplmn_list_num; i++){
			if(input[10*i+6] == (byte)0x38){
				strUorG[i] = "U";
			}else if(input[10*i+8] == (byte)0x38){
				strUorG[i] = "G";
			}else{
				strUorG[i] = "NULL";
			}
		}
	}
	private int transferSpecialInt(int i, int offset){
		switch(i){
		case 0:
			return 1+offset;
		case 1:
			return 0+offset;
		case 2:
			return 3+offset;
		case 3:
			return 5+offset;
		case 4:
			return 4+offset;
		case 5:
			return 2+offset;
		default:
			return 0+offset;
		}
	}
	private int transferSpecialIntPlus(int i, int offset){
		switch(i){
		case 0:
			return 1+offset;
		case 1:
			return 0+offset;
		case 2:
			return 5+offset;
		case 3:
			return 2+offset;
		case 4:
			return 4+offset;
		case 5:
			return 3+offset;
		default:
			return 0+offset;
		}
	}
	private void handleShowStrUPLMN(byte [] input){

		for(int j=0;j<uplmn_list_num;j++){
			for(int i=0; i<6; i++){
				part[j][i] = input[transferSpecialInt( i, j*10)];
			}
			showUPLMN[j] = new String(part[j]);
		}
	}
	//end display
	private class EventHandler extends Handler
	{
		public EventHandler(Looper looper) {
		    super(looper);
		}

		@Override
		public void handleMessage(Message msg) {
			int m = 0;
			ByteArrayOutputStream outputBuffer = new ByteArrayOutputStream();
			DataOutputStream outputBufferStream = new DataOutputStream(outputBuffer);
			if(msg.what == engconstents.ENG_AT_GETUPLMN){
    /*Modify 20130205 Spreadst of 125480 change the method of creating cmd start*/
                //str=String.format("%d,%d,%d", msg.what, 1, msg.arg1);
                str=new StringBuilder().append(msg.what).append(",").append(1)
                    .append(",").append(msg.arg1).toString();
    /*Modify 20130205 Spreadst of 125480 change the method of creating cmd end*/
				try {
				outputBufferStream.writeBytes(str);
				} catch (IOException e) {
				Log.e(LOG_TAG, "writebytes error");
				return;
				}

				mEf.engwrite(sockid,outputBuffer.toByteArray(),outputBuffer.toByteArray().length);
				int dataSize = 512;//160 used for 16 uplmn
				byte[] inputBytes = new byte[dataSize];
				int showlen= mEf.engread(sockid,inputBytes,dataSize);
				lenth_get = showlen/10*5;
				if(showlen <= 3){
					DisplayToast(getString(R.string.no_sim_card_prompt));
					listView.setVisibility(View.GONE);
					finish();
					return;
				}

				for(int i=0; i<uplmn_list_num; i++){
					originalUPLMN[i] = new String(inputBytes,i*10,LEN_UNIT,Charset.defaultCharset());
					if(DEBUG) Log.d(LOG_TAG, "strUPLMN["+i+"] "+originalUPLMN[i] );
				}
				handleUTRANorGSM(inputBytes);
				handleShowStrUPLMN(inputBytes);

				for(int n=0;n<showlen/10;n++){
					if(strUorG[n].equals("G")||strUorG[n].equals("U")){
						data.add(showUPLMN[n].replaceAll("F", "") +": "+ strUorG[n] );
						order[m++] = n;
					}
				}
		}
		else if(msg.what == engconstents.ENG_AT_GETUPLMNLEN){
    /*Modify 20130205 Spreadst of 125480 change the method of creating cmd start*/
            //str=String.format("%d,%d", msg.what,0);
            str=new StringBuilder().append(msg.what).append(",").append(0).toString();
    /*Modify 20130205 Spreadst of 125480 change the method of creating cmd end*/
			try {
				outputBufferStream.writeBytes(str);
			} catch (IOException e) {
				Log.e(LOG_TAG, "writebytes error");
			   return;
			}

			mEf.engwrite(sockid,outputBuffer.toByteArray(),outputBuffer.toByteArray().length);
			int dataSize = 512;//for length
			byte[] inputBytes = new byte[dataSize];
			int showlen= mEf.engread(sockid,inputBytes,dataSize);
			String setResult =new String(inputBytes,0,showlen,Charset.defaultCharset());
			if(showlen <= 3)
			{
				DisplayToast(getString(R.string.no_sim_card_prompt));
				listView.setVisibility(View.GONE);
				finish();
				return;
			}

			String lenString = getLenStringFromResponse(setResult);

			at_read_lenth = Integer.parseInt(lenString, 16);
			if(at_read_lenth>=250){
				at_read_lenth = 100;
			}
			uplmn_list_num = at_read_lenth/5;

			setAllParameters(uplmn_list_num);
			if(DBUG){
			if(DEBUG) Log.d(LOG_TAG, "showlen== "+showlen );
			if(DEBUG) Log.d(LOG_TAG, "setResult== "+setResult );
			if(DEBUG) Log.d(LOG_TAG, "lenString== "+lenString );
			if(DEBUG) Log.d(LOG_TAG, "at_read_lenth== "+at_read_lenth );
			}

			sendMessageToGetUPLMNList(at_read_lenth);


		}
			else if(msg.what == engconstents.ENG_AT_SETUPLMN){
    /*Modify 20130205 Spreadst of 125480 change the method of creating cmd start*/
//              str=String.format("%d,%d,%d,%d,%d,%d,%d,%s,%s",
//                      msg.what,
//                      7,
//                      214,
//                      28512,
//                      0,
//                      0,
//                      lenth_get< at_read_lenth? lenth_get:at_read_lenth,
//                      getPacketData(),
//                      "3F007FFF"
//                      );
                str=new StringBuilder().append(msg.what).append(",").append(7).append(",")
                    .append(214).append(",").append(28512).append(",").append(0).append(",")
                    .append(0).append(",").append(lenth_get< at_read_lenth? lenth_get:at_read_lenth)
                    .append(",").append(getPacketData()).append(",").append("3F007FFF").toString();
    /*Modify 20130205 Spreadst of 125480 change the method of creating cmd end*/
				try {
					outputBufferStream.writeBytes(str);
				} catch (IOException e) {
					Log.e(LOG_TAG, "writebytes error");
				   return;
				}
				mEf.engwrite(sockid,outputBuffer.toByteArray(),outputBuffer.toByteArray().length);
				int dataSize01 = 64;
				byte[] inputBytes01 = new byte[dataSize01];
				int showlen01= mEf.engread(sockid,inputBytes01,dataSize01);

				String setResult =new String(inputBytes01,0,showlen01,Charset.defaultCharset());
				if(setResult.equals("144")){
					finish();
				}else{
					DisplayToast(getString(R.string.fail_uplmn));
				}

			}
		}

		private void setAllParameters(int len) {
			// TODO Auto-generated method stub
			showUPLMN = new String[len] ;
			originalUPLMN = new String[len] ;
			strUorG = new String[len] ;
			part = new byte[len][];
			order = new int[len];
			for(int i=0;i<len;i++){
				 part[i] = new byte[6];
			}
		}

		private void sendMessageToGetUPLMNList(int length) {
			mHandler.removeMessages(0);
			Message m = mHandler.obtainMessage(engconstents.ENG_AT_GETUPLMN, length, 0, 0);
			mHandler.sendMessage(m);
		}

		private String getLenStringFromResponse(String str) {
			int total_len = str.length();
			int offset = 4;
			int index = 0;
			String strBody = str.substring(4, total_len);
			String result ="0";
			while(index < strBody.length()){
				int len = transferStringToInt(strBody.substring(index+2, index+4));
				if((strBody.substring(index, index+2)).equals("80")){
				result = strBody.substring(index+offset, index+offset+2*len);
				break;
				}
				index = index+len*2+offset;
			}
			return result;
		}

		private int transferStringToInt(String str) {
			int num = Integer.parseInt(str,16);
			return num;
		}
	}

}
