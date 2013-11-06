package com.spreadtrum.android.eng;

import android.os.Bundle;
import android.app.Activity;
import android.widget.TextView;
import org.apache.http.util.EncodingUtils;
import java.io.InputStream;
import java.io.FileReader;
import android.util.Log;

public class EngPatchRecordActivity extends Activity {
    final private static String TAG = "EngPatchRecordActivity";
    private static final int LOAD_STRING_MAX_LENTH = 10240;
    
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.prefs_patchrecord);     
        String text = "";
		
        try{
		        int count;
		        char[] buf = new char[LOAD_STRING_MAX_LENTH+1];
		        FileReader rd = new FileReader("/system/patch/patch.record");
		        count = rd.read(buf, 0, LOAD_STRING_MAX_LENTH);
		        buf[count] = '\n';
		        text = new String(buf).substring(0, count - 1);

		        rd.close();

		        Log.i(TAG, "patch.record  count = "+count);
        }
        catch(Exception e){
		        Log.e(TAG,"patch.record, file reader error:" + e.toString());
		    }
		    
		    Log.i(TAG, "findViewById ");
		               
           // Finally stick the string into the text view. 
		    TextView tv = (TextView) findViewById(R.id.patch_record_textView); 
		    tv.setText(text); 


    }
}
