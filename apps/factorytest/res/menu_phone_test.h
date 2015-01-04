#ifndef MENUTITLE

#define __MAKE_MENUTITLE_ENUM__
#define MENUTITLE(num ,id ,title, func)	K_PHONE_##title,
enum {
#endif
	MENUTITLE(0,1,TEST_LCD, test_lcd_start)
	MENUTITLE(1,2,TEST_TP, test_tp_start)
	MENUTITLE(0,0,TEST_VB_BL, show_phone_vb_bl_menu)//bcamera and flash
	MENUTITLE(4,4,TEST_KEY, test_key_start)
	MENUTITLE(5,7,TEST_FCAMERA, test_fcamera_start)
	MENUTITLE(0,0,TEST_BCAMERA_FLASH, show_phone_bcamera_test_menu)//bcamera and flash
	MENUTITLE(0,0,TEST_PHONE_LOOPBACK, show_phone_loop_test_menu)
	MENUTITLE(0,0,TEST_VOICE, show_phone_voice_test_menu)
	MENUTITLE(12,17,TEST_CHARGE, test_charge_start)
	MENUTITLE(13,15,TEST_SDCARD, test_sdcard_start)//+++++++
	MENUTITLE(14,16,TEST_SIMCARD, test_sim_start)//+++++++
	MENUTITLE(15,14,TEST_HEADSET, test_headset_start)
	MENUTITLE(16,19,TEST_FM, test_fm_start)
	MENUTITLE(0,0,TEST_SENSOR, show_sensor_test_menu)
	//MENUTITLE(20,22,TEST_BT, test_bt_start)//+++++++++
	//MENUTITLE(21,23,TEST_WIFI, test_wifi_start) //+++++++++
	//MENUTITLE(22,24,TEST_GPS, test_gps_start)//++++++++++
	MENUTITLE(0,0,TEST_BTWIFIGPS, show_phone_btwifigps_test_menu)
	MENUTITLE(22,27,TEST_TEL, test_tel_start)//++++++++++
	MENUTITLE(23,26,TEST_OTG, test_otg_start)//++++++++++
	MENUTITLE(24,29,CALI_INFO, test_cali_info)
	MENUTITLE(25,30,VERSION, test_version_show)
	//OTG
	//DIAL

#ifdef __MAKE_MENUTITLE_ENUM__
	K_MENU_PHONE_TEST_CNT,
};
#undef __MAKE_MENUTITLE_ENUM__
#undef MENUTITLE
#endif
