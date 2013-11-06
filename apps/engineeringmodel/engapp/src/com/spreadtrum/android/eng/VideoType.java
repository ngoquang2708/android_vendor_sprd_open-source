package com.spreadtrum.android.eng;

import android.app.Activity;
import android.os.Bundle;
import android.util.Log;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.RadioGroup.OnCheckedChangeListener;
import android.widget.Toast;

public class VideoType extends Activity{

	private static final String TAG = "VideoType";
	private static final String KEY = "debug.videophone.videotype";

	RadioGroup mGroup;
	RadioButton mBtn1,mBtn2,mBtn3,mBtn4;
	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		setContentView(R.layout.videotype);
		mGroup = (RadioGroup)findViewById(R.id.group);
		mGroup.setOnCheckedChangeListener(mListener);
		mBtn1 = (RadioButton)findViewById(R.id.radio1);
		mBtn2 = (RadioButton)findViewById(R.id.radio2);
		mBtn3 = (RadioButton)findViewById(R.id.radio3);
		mBtn4 = (RadioButton)findViewById(R.id.radio4);

		mBtn1.setText("H263 Prefer");
		mBtn2.setText("MPEG4 Prefer");
		mBtn3.setText("H263 Only");
		mBtn4.setText("MPEG4 Only");

		mGroup.setOnCheckedChangeListener(mListener);
		initView();
	}

	private void initView(){
//		System.getProperty(KEY, "1");
		int values = getValues();
		if(values == -1){
			return;
		}
		switch (values) {
		case 1:
			mBtn1.setChecked(true);
			break;
		case 2:
			mBtn2.setChecked(true);
			break;
		case 3:
			mBtn3.setChecked(true);
			break;
		case 4:
			mBtn4.setChecked(true);
			break;
		default:
			mBtn1.setChecked(true);
			break;
		}
	}

    private OnCheckedChangeListener mListener = new OnCheckedChangeListener() {
        @Override
        public void onCheckedChanged(RadioGroup group, int checkedId) {
            switch (checkedId) {
                /* Add 20130228 Spreadst of 130830 add toast start */
                case R.id.radio1:
                    if (System.getProperty(KEY) != null) {
                        Toast.makeText(VideoType.this, "select H263 Prefer successful",
                                Toast.LENGTH_SHORT).show();
                    }
                    setValues(1);
                    break;
                case R.id.radio2:
                    Toast.makeText(VideoType.this, "select MPEG4 Prefer successful",
                            Toast.LENGTH_SHORT).show();
                    setValues(2);
                    break;
                case R.id.radio3:
                    Toast.makeText(VideoType.this, "select H263 Only successful",
                            Toast.LENGTH_SHORT).show();
                    setValues(3);
                    break;
                case R.id.radio4:
                    Toast.makeText(VideoType.this, "select MPEG4 Only successful",
                            Toast.LENGTH_SHORT).show();
                    setValues(4);
                    break;
            }
            /* Add 20130228 Spreadst of 130830 add toast end */
        }
    };

	private void setValues(int value){
		String v = String.valueOf(value);
		System.setProperty(KEY, v);
	}

	private int getValues(){
		int value = -1;
		String v = System.getProperty(KEY, "1");
		try{
			value = Integer.valueOf(v);
		}catch (Exception e) {
			Log.e(TAG, "Pase " + v + " to Integer Error !");
		}
		return value;
	}
}
