#ifndef MENUTITLE


#define __MAKE_MENUTITLE_ENUM__
#define MENUTITLE(num,id,title, func)	K_PHONE_##title,
enum {
#endif

	MENUTITLE(6,8,TEST_BCAMERA, test_bcamera_start)
	MENUTITLE(7,9,TEST_FLASH, test_flash_start)

#ifdef __MAKE_MENUTITLE_ENUM__

	K_MENU_BCAMERA_CNT,
};

#undef __MAKE_MENUTITLE_ENUM__
#undef MENUTITLE
#endif

