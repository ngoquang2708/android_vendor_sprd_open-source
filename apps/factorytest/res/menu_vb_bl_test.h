#ifndef MENUTITLE


#define __MAKE_MENUTITLE_ENUM__
#define MENUTITLE(num,id,title, func)	K_PHONE_##title,
enum {
#endif

	MENUTITLE(2,5,TEST_VIBRATOR, test_vibrator_start)
	MENUTITLE(3,6,TEST_BACKLIGHT, test_backlight_start)

#ifdef __MAKE_MENUTITLE_ENUM__

	K_MENU_VB_BL_CNT,
};

#undef __MAKE_MENUTITLE_ENUM__
#undef MENUTITLE
#endif

