package com.spreadtrum.android.eng;

import java.io.BufferedInputStream;
import java.io.BufferedReader;
import java.io.DataOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStreamReader;

import android.content.ContentValues;
import android.content.Context;
import android.database.Cursor;
import android.database.sqlite.SQLiteDatabase;
import android.database.sqlite.SQLiteOpenHelper;
import android.os.Debug;
import android.provider.BaseColumns;
import android.util.Log;



//import dalvik.system.VMRuntime;

public class EngSqlite {
    private static final boolean DEBUG = Debug.isDebug();
    private static final String TAG = "EngSqlite";
    private Context mContext;
    private SQLiteDatabase mSqLiteDatabase = null;

    private static final String ENG_ENGTEST_DB = "/productinfo/engtest.db";
    private static final String ENG_STRING2INT_TABLE = "str2int";
    private static final String ENG_STRING2INT_NAME = "name";
    private static final String ENG_STRING2INT_VALUE = "value";
    private static final String ENG_GROUPID_VALUE = "groupid";
    private static final int  ENG_ENGTEST_VERSION = 1;

    private static EngSqlite mEngSqlite;
    public static synchronized EngSqlite getInstance(Context context){
    	if(mEngSqlite == null){
    		mEngSqlite = new EngSqlite(context);
    	}
    	return mEngSqlite;
    }
    private  EngSqlite(Context context){
        mContext = context;
        File file = new File(ENG_ENGTEST_DB);
            Process p = null;
            DataOutputStream os = null;
            try {
                p = Runtime.getRuntime().exec("chmod 774 productinfo");
                os = new DataOutputStream(p.getOutputStream());
                BufferedInputStream err = new BufferedInputStream(p.getErrorStream());
                BufferedReader br = new BufferedReader(new InputStreamReader(err));
                if(DEBUG) Log.d("Vtools","os= "+br.readLine());
                Runtime.getRuntime().exec("chmod 774 "+file.getAbsolutePath());
                int status = p.waitFor();
		Log.d(TAG, "process :"+status+"has finished");
            } catch (IOException e) {
                // TODO Auto-generated catch block
                e.printStackTrace();
            } catch (InterruptedException e) {
                // TODO Auto-generated catch block
                e.printStackTrace();
            }finally{
                if (os != null) {
                    try {
                        os.close();
               //         p.destroy();
                    } catch (IOException e) {
                        // TODO Auto-generated catch block
                        e.printStackTrace();
                    }
                }
            }
        EngineeringModeDatabaseHelper databaseHelper = new EngineeringModeDatabaseHelper(mContext);
        mSqLiteDatabase = databaseHelper.getWritableDatabase();
    }

    //the follow method is add for factory mode
    public int  queryFactoryModeDate(String name){
        int ret = 0;
        try {
            Cursor c = mSqLiteDatabase.query(ENG_STRING2INT_TABLE,
                    new String[]{ENG_STRING2INT_NAME,ENG_STRING2INT_VALUE},
                    ENG_STRING2INT_NAME+"= \'"+name+"\'",
                    null, null, null, null);
            if (c != null) {
            	 c.moveToFirst();
            	 ret = c.getInt(1);
                 c.close();
            }
        } catch (Exception e) {
            return ret;
        }
        return ret;
    }

    //jude the name is exited or not
    public boolean queryData(String name){
        try {
            Cursor c = mSqLiteDatabase.query(ENG_STRING2INT_TABLE,
                    new String[]{ENG_STRING2INT_NAME,ENG_STRING2INT_VALUE},
                    ENG_STRING2INT_NAME+"= \'"+name+"\'",
                    null, null, null, null);
            if (c != null) {
                if (c.getCount() > 0) {
                	c.close();
                    return true;
                }
            	c.close();
            }
        } catch (Exception e) {
            return false;
        }
        return false;
    }    

    private void insertFactoryModeData(String name,int  value){
        ContentValues cv = new ContentValues();
        cv.put(ENG_STRING2INT_NAME,name);
        cv.put(ENG_STRING2INT_VALUE,value);

        long returnValue= mSqLiteDatabase.insert(ENG_STRING2INT_TABLE, null, cv);
        if(DEBUG) Log.d(TAG, "returnValue" + returnValue);
        if (returnValue == -1) {
             Log.e(TAG, "insert DB error!");
        }
    }

    private void updateFactoryModeData(String name,int value){
        ContentValues cv = new ContentValues();
        cv.put(ENG_STRING2INT_NAME,name);
        cv.put(ENG_STRING2INT_VALUE,value);
        mSqLiteDatabase.update(ENG_STRING2INT_TABLE, cv,
                ENG_STRING2INT_NAME+"= \'"+name+"\'", null);

    }

    public void updataFactoryModeDB(String name,int value) {
    	if (queryData(name)) {
    		updateFactoryModeData(name,value);
        }else {
        	insertFactoryModeData(name, value);
        }
    }

    public void release() {
        if(mSqLiteDatabase != null) {
            mSqLiteDatabase.close();
            mSqLiteDatabase = null;
        }
        if(mEngSqlite != null) {
            mEngSqlite = null;
        }
    }

    private static class EngineeringModeDatabaseHelper extends SQLiteOpenHelper{

		public EngineeringModeDatabaseHelper(Context context) {
			super(context, ENG_ENGTEST_DB, null, ENG_ENGTEST_VERSION);
			// TODO Auto-generated constructor stub
		}

		@Override
		public void onCreate(SQLiteDatabase db) {
			// TODO Auto-generated method stub
            //db.execSQL("DROP TABLE IF EXISTS " + ENG_STRING2INT_TABLE + ";");
			//db.execSQL("CREATE TABLE " + ENG_STRING2INT_TABLE + " (" + BaseColumns._ID
	        //        + " INTEGER PRIMARY KEY AUTOINCREMENT," + ENG_GROUPID_VALUE + " INTEGER NOT NULL DEFAULT 0,"
	        //        + ENG_STRING2INT_NAME + " TEXT," + ENG_STRING2INT_VALUE + " INTEGER NOT NULL DEFAULT 0" + ");");
			db.execSQL("CREATE TABLE IF NOT EXISTS " + ENG_STRING2INT_TABLE + " (" + BaseColumns._ID
	                + " INTEGER PRIMARY KEY AUTOINCREMENT," + ENG_GROUPID_VALUE + " INTEGER NOT NULL DEFAULT 0,"
	                + ENG_STRING2INT_NAME + " TEXT," + ENG_STRING2INT_VALUE + " INTEGER NOT NULL DEFAULT 0" + ");");
		}

		@Override
		public void onUpgrade(SQLiteDatabase db, int oldVersion, int newVersion) {
			// TODO Auto-generated method stub
			if(newVersion > oldVersion){
				db.execSQL("DROP TABLE IF EXISTS " + ENG_STRING2INT_TABLE + ";");
				onCreate(db);
			}
		}

    }

}
