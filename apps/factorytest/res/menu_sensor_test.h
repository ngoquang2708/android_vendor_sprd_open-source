#ifndef MENUTITLE


#define __MAKE_MENUTITLE_ENUM__
#define MENUTITLE(num,id,title, func)	K_PHONE_##title,
enum {
#endif

	MENUTITLE(17,39,TEST_GSENSOR, test_gsensor_start)
	MENUTITLE(18,36,TEST_LSENSOR, test_lsensor_start)
	//MENUTITLE(19,33,TEST_MSENSOR, test_msensor_start)

#ifdef __MAKE_MENUTITLE_ENUM__

	K_MENU_SENSOR_CNT,
};

#undef __MAKE_MENUTITLE_ENUM__
#undef MENUTITLE
#endif

