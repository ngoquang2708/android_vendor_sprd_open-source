#ifndef MENUTITLE


#define __MAKE_MENUTITLE_ENUM__
#define MENUTITLE(num,id,title, func)	K_PHONE_##title,
enum {
#endif

	MENUTITLE(19,22,TEST_BT, test_bt_start)//+++++++++
	MENUTITLE(20,23,TEST_WIFI, test_wifi_start) //+++++++++
	MENUTITLE(21,24,TEST_GPS, test_gps_start)//++++++++++

#ifdef __MAKE_MENUTITLE_ENUM__

	K_MENU_BTWIFIGPS_CNT,
};

#undef __MAKE_MENUTITLE_ENUM__
#undef MENUTITLE
#endif

