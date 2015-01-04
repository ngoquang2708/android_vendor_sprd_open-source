#ifndef MENUTITLE


#define __MAKE_MENUTITLE_ENUM__
#define MENUTITLE(num,id,title, func)	K_PHONE_##title,
enum {
#endif

	MENUTITLE(10,13,TEST_RECEIVER, test_receiver_start)
	MENUTITLE(11,12,TEST_SPEAKER, test_speaker_start)

#ifdef __MAKE_MENUTITLE_ENUM__

	K_MENU_VOICE_CNT,
};

#undef __MAKE_MENUTITLE_ENUM__
#undef MENUTITLE
#endif

