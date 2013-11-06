package com.spreadtrum.android.eng;

import android.app.ListActivity;
import android.content.Intent;
import android.os.Bundle;
import android.os.Debug;
import android.util.Log;
import android.view.View;
import android.widget.AdapterView;
import android.widget.AdapterView.OnItemClickListener;
import android.widget.ArrayAdapter;
import android.widget.ListView;
import android.widget.TextView;
import android.widget.Toast;

/**
 * Using a LogTextBox to display a scrollable text area
 * to which text is appended.
 *
 */
public class phoneinfo extends ListActivity {

    private static final String LOG_TAG = "engphoneinfo";
    private static final boolean DEBUG = Debug.isDebug();
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        
//        setContentView(R.layout.phonetest);    
        setListAdapter(new ArrayAdapter<String>(this, R.layout.list_item, pts));
        //setListAdapter(new ArrayAdapter<String>(this, R.layout.simple_list_item_1, pts));        

        ListView lv = getListView();
        lv.setTextFilterEnabled(true);

        lv.setOnItemClickListener(new OnItemClickListener() {
          public void onItemClick(AdapterView<?> parent, View view,
              int position, long id) {
          if(DEBUG) Log.d(LOG_TAG, "phoneinfo: p=" + position +"id="+ id);              
          	switch (position)
        	{
        	case 0:
        		break;
        	case 1:
        		break;
        	case 2:
        		Intent infointent = new Intent(getApplicationContext(), netinfo.class);
        		startActivity(infointent);
        		break;
        	case 3:
        		break;
        	case 4:
        		break;
        	case 5:
        		break;
        	default:
        		break;
        	}        	  
            // When clicked, show a toast with the TextView text
            Toast.makeText(getApplicationContext(), ((TextView) view).getText(),
                Toast.LENGTH_SHORT).show();
          }
        
        });        
    }
    
    static final String[] pts = new String[] {
    		"Version Info",
    		"Third Party Version Info",
    		"Net Info",
    		"Phone Info",
    		"Adc Calibration Info",
    		"Restore Info",
    		"Para Set",
    		"App Set",
    		"Layer1 Monitor",
    		"IQ Modem"    		
    };
    
}
