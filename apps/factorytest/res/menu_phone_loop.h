#ifndef MENUTITLE


#define __MAKE_MENUTITLE_ENUM__
#define MENUTITLE(num,id,title, func)	K_PHONE_##title,
enum {
#endif

	MENUTITLE(8,10,TEST_MAINLOOP, test_mainloopback_start)
	MENUTITLE(9,11,TEST_ASSISLOOP, test_assisloopback_start)

#ifdef __MAKE_MENUTITLE_ENUM__

	K_MENU_LOOP_CNT,
};

#undef __MAKE_MENUTITLE_ENUM__
#undef MENUTITLE
#endif

