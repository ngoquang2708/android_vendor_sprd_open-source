package com.spreadtrum.android.eng;

import android.os.Bundle;
import android.app.ListActivity;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.ListView;
import android.widget.AdapterView;
import android.widget.Toast;
import android.widget.TextView;
import android.widget.AdapterView.OnItemClickListener;
import android.content.Intent;

/**
 * Using a LogTextBox to display a scrollable text area
 * to which text is appended.
 *
 */
public class phonetest extends ListActivity {
	
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        
//        setContentView(R.layout.phonetest);    
        setListAdapter(new ArrayAdapter<String>(this, R.layout.list_item, pts));

        ListView lv = getListView();
        lv.setTextFilterEnabled(true);

        lv.setOnItemClickListener(new OnItemClickListener() {
          public void onItemClick(AdapterView<?> parent, View view,
              int position, long id) {
        	switch (position)
        	{
        	case 0:
        		break;
        	case 1:
        		break;
        	case 2:
			Intent wifintent = new Intent(getApplicationContext(), wifitest.class);
			startActivity(wifintent);
        		break;
        	default:
        		break;
        	}
            // When clicked, show a toast with the TextView text
	    if(view instanceof TextView){
            Toast.makeText(getApplicationContext(), ((TextView) view).getText(),
                Toast.LENGTH_SHORT).show();
	     }
          }
        
        });        
    }
    
    static final String[] pts = new String[] {
    	"Full phone test", 
    	"View phone test result", 
    	"Item test"     
    };	
}
