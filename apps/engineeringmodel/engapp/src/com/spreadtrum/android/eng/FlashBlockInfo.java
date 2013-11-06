/**
 * add by zhaoty 12-01-16 for flash block information
 */
package com.spreadtrum.android.eng;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.IOException;

import android.app.Activity;
import android.os.Bundle;
import android.util.Log;
import android.widget.TextView;

public class FlashBlockInfo extends Activity {

	private static final String LOG_TAG = "FlashBlockInfo";
	private TextView mFlashblock;
	private final String MTD_PATH = "/proc/mtd";
	/** Called when the activity is first created. */
	@Override
	public void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		setContentView(R.layout.flashblockinfo);

		mFlashblock = (TextView) findViewById(R.id.flashblock);
	}

	@Override
	protected void onResume() {
		super.onResume();
		File file = new File(MTD_PATH);
        /*
         * Add 20130609 spreadst of 157181 no information in flashblockinfo
         * start
         */
        if (!file.exists() || !file.canRead()) {
            mFlashblock.setText("the file /proc/mtd don't exist or can't read!");
            return;
        }
        /* Add 20130609 spreadst of 157181 no information in flashblockinfo end */
        BufferedReader reader = null;
        try {
        	reader = new BufferedReader(new FileReader(file));
        	String tmpString = null;
        	String tmpRead = null;
        	int line = 1;
        	while((tmpRead = reader.readLine())!= null){
        		if(line == 2){
        			tmpString += "\n" + tmpRead;
        			break;
        		}else{
        			tmpString = tmpRead;
        		}
    			tmpRead = null;
        		line ++;
        	}
        	reader.close();
        	reader = null;
        	mFlashblock.setText(tmpString);
        } catch (IOException e) {
            Log.e(LOG_TAG,"Read file failed.");
        }finally{
            if(reader != null){
                try {
                	reader.close();
	            } catch (IOException e) {
					Log.e(LOG_TAG,"Read file failed.");
				}
             }
        }
	}
    
}
