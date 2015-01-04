#ifndef MENUTITLE

#define __MAKE_MENUTITLE_ENUM__
#define MENUTITLE(num,id ,title, func)	A_PHONE_##title,
enum {
#endif
	MENUTITLE(0,1,TEST_LCD, test_lcd_start)
	MENUTITLE(1,2,TEST_TP, test_tp_start)
	MENUTITLE(2,5,TEST_VIBRATOR, test_vibrator_start)
	MENUTITLE(3,6,TEST_BACKLIGHT, test_backlight_start)
	MENUTITLE(4,4,TEST_KEY, test_key_start)
	MENUTITLE(5,7,TEST_FCAMERA, test_fcamera_start)
	MENUTITLE(6,8,TEST_BCAMERA, test_bcamera_start)
	MENUTITLE(7,9,TEST_FLASH, test_flash_start)
	MENUTITLE(8,10,TEST_MAINLOOP, test_mainloopback_start)
	MENUTITLE(9,11,TEST_ASSISLOOP, test_assisloopback_start)
	MENUTITLE(10,13,TEST_RECEIVER, test_receiver_start)
	MENUTITLE(11,12,TEST_SPEAKER, test_speaker_start)
	MENUTITLE(12,17,TEST_CHARGE, test_charge_start)
	MENUTITLE(13,15,TEST_SDCARD, test_sdcard_start)//+++++++
	MENUTITLE(14,16,TEST_SIMCARD, test_sim_start)//+++++++
	MENUTITLE(15,14,TEST_HEADSET, test_headset_start)
	MENUTITLE(16,19,TEST_FM, test_fm_start)
	MENUTITLE(17,39,TEST_GSENSOR, test_gsensor_start)
	MENUTITLE(18,36,TEST_LSENSOR, test_lsensor_start)
	//MENUTITLE(19,33,TEST_MSENSOR, test_msensor_start)
	MENUTITLE(19,22,TEST_BT, test_bt_start)//+++++++++
	MENUTITLE(20,23,TEST_WIFI, test_wifi_start) //+++++++++
	MENUTITLE(21,24,TEST_GPS, test_gps_start)//++++++++++
	MENUTITLE(22,27,TEST_TEL, test_tel_start)//++++++++++
	MENUTITLE(23,26,TEST_OTG, test_otg_start)//++++++++++
	MENUTITLE(24,29,CALI_INFO, test_cali_info)
	MENUTITLE(25,30,VERSION, test_version_show)
	//MENUTITLE(22,0x400000,otg, test_otg_start)    OTG 0X400000
	//MENUTITLE(23,0x800000,dial, test_dial_start)    dial   0X800000

#ifdef __MAKE_MENUTITLE_ENUM__
	K_MENU_AUTO_TEST_CNT,
};
#undef __MAKE_MENUTITLE_ENUM__
#undef MENUTITLE
#endif

