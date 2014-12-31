#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include "minui.h"
#include "common.h"

#include "resource.h"
#include "testitem.h"
#include "eng_sqlite.h"


typedef struct {
    unsigned char num;
	unsigned int id;
	char* title;
	int (*func)(void);
}menu_info;



typedef struct {
	unsigned int id;
	char* name;
	int value;
}result_info;


typedef struct {
	char* name;
	unsigned int id;
	char yes_no;
	char status;
}new_result_info;




typedef struct {
	unsigned char num;
	unsigned int rid;
	int id;
	int (*func)(void);
}multi_test_info;



static int show_phone_loop_test_menu(void);
static int show_pcba_phone_loop_test_menu(void);

static int show_sensor_test_menu(void);
static int show_pcba_sensor_test_menu(void);


static int show_pcba_btswifigps_test_menu(void);
static int show_phone_btwifigps_test_menu(void);



static int show_phone_voice_test_menu(void);
static int show_pcba_voice_test_menu(void);



static int show_pcba_bcamera_test_menu(void);
static int show_phone_bcamera_test_menu(void);

static int show_phone_test_menu(void);
static int show_pcba_test_menu(void);


static int show_pcba_vb_bl_menu(void);
static int show_phone_vb_bl_menu(void);



static int auto_all_test(void);
static int PCBA_auto_all_test(void);//+++++++++++++++
static int test_result_mkdir(void);//++++++++++++++++++++
static void write_txt(char * pathname ,int n);//++++++++++


static int show_pcba_test_result(void);
static int show_phone_test_result(void);

static int phone_shutdown(void);
static pthread_t bt_wifi_init_thread;
static pthread_t gps_init_thread;
extern void temp_set_visible(int is_show);


menu_info menu_root[] = {
#define MENUTITLE(num,id,title, func) \
	[ K_ROOT_##title ] = {num,id,MENU_##title, func, },
#include "./res/menu_root.h"
#undef MENUTITLE
    [K_MENU_ROOT_CNT]={0,0,MEUN_FACTORY_RESET, phone_shutdown,},
};


menu_info menu_phone_test[] = {
#define MENUTITLE(num,id,title, func) \
	[ K_PHONE_##title ] = {num,id,MENU_##title, func, },
#include "./res/menu_phone_test.h"
#undef MENUTITLE
	[K_MENU_PHONE_TEST_CNT] = {0,0,MENU_BACK, 0,},
};


menu_info menu_pcba_test[] = {
#define MENUTITLE(num,id,title, func) \
	[ P_PHONE_##title ] = {num,id,MENU_##title, func, },
#include "./res/menu_pcba_test.h"
#undef MENUTITLE
	[P_MENU_PHONE_TEST_CNT] = {0,0,MENU_BACK, 0,},
};



menu_info menu_auto_test[] = {
#define MENUTITLE(num,id,title, func) \
	[ A_PHONE_##title ] = {num,id,MENU_##title, func, },
#include "./res/menu_auto_test.h"
#undef MENUTITLE
	[K_MENU_AUTO_TEST_CNT] = {0,0,MENU_BACK, 0,},
};



menu_info menu_phone_loopmenu[] = {
#define MENUTITLE(num,id,title, func) \
	[ K_PHONE_##title ] = {num,id,MENU_##title, func, },
#include "./res/menu_phone_loop.h"
#undef MENUTITLE
	[K_MENU_LOOP_CNT] = {0,0,MENU_BACK, 0,},
};


menu_info menu_sensor_testmenu[] = {
#define MENUTITLE(num,id,title, func) \
	[ K_PHONE_##title ] = {num,id,MENU_##title, func, },
#include "./res/menu_sensor_test.h"
#undef MENUTITLE
	[K_MENU_SENSOR_CNT] = {0,0,MENU_BACK, 0,},
};


menu_info menu_btwifigps_testmenu[] = {
#define MENUTITLE(num,id,title, func) \
	[ K_PHONE_##title ] = {num,id,MENU_##title, func, },
#include "./res/menu_bt_wifi_gps_test.h"
#undef MENUTITLE
	[K_MENU_BTWIFIGPS_CNT] = {0,0,MENU_BACK, 0,},
};




menu_info menu_voice_testmenu[] = {
#define MENUTITLE(num,id,title, func) \
	[ K_PHONE_##title ] = {num,id,MENU_##title, func, },
#include "./res/menu_voice_test.h"
#undef MENUTITLE
	[K_MENU_VOICE_CNT] = {0,0,MENU_BACK, 0,},
};

menu_info menu_vb_bl_testmenu[] = {
#define MENUTITLE(num,id,title, func) \
	[ K_PHONE_##title ] = {num,id,MENU_##title, func, },
#include "./res/menu_vb_bl_test.h"
#undef MENUTITLE
	[K_MENU_VB_BL_CNT] = {0,0,MENU_BACK, 0,},
};


menu_info menu_bc_fl_testmenu[] = {
#define MENUTITLE(num,id,title, func) \
	[ K_PHONE_##title ] = {num,id,MENU_##title, func, },
#include "./res/menu_bc_fl_test.h"
#undef MENUTITLE
	[K_MENU_BCAMERA_CNT] = {0,0,MENU_BACK, 0,},
};





result_info test_result_phone[] = {
#define MENUTITLE(num,id,title, func) \
	[ A_PHONE_##title ] = {id , #title, RL_NA},
#include "./res/menu_auto_test.h"
#undef MENUTITLE

};


result_info test_result_pcba[] = {
#define MENUTITLE(num,id,title, func) \
	[ A_PHONE_##title ] = {id , #title, RL_NA},
#include "./res/menu_auto_test.h"
#undef MENUTITLE

};



new_result_info test_result_txt[] = {
#define MENUTITLE(num,id,title, func) \
	[ A_PHONE_##title ] = {   #title, id,0,RL_NA},
#include "./res/menu_auto_test.h"
#undef MENUTITLE
};


/**********************************************************************/

char sdcard_fm_state;
static int fm_freq=875;



static void write_txt(char * pathname ,int n)
{
	int i;
	int fd;
	int ret;
	char tmp[64];
	char wholetmp[2048];
	memset(wholetmp, 0, sizeof(wholetmp));
	memset(tmp, 0, sizeof(tmp));
	//LOGD("mmitest I am here2");
	fd=open(pathname,O_RDWR);
	if(fd < 0)
	{
		LOGD("[%s]: mmitest open %s fail", __FUNCTION__, pathname);
		//return -1;
	}
	for(i=0;i<n;i++)
	{
        sprintf(tmp, "name=%s: id=%d  yes_no=%d  status=%d \n", test_result_txt[i].name,test_result_txt[i].id,test_result_txt[i].yes_no,test_result_txt[i].status);
        strcat(wholetmp,tmp);
	}
	write(fd,wholetmp,strlen(wholetmp));
	close(fd);
	LOGD("mmitest write over");

	//return 0;
}



static int
show_root_menu(void)
{
	int chosen_item = -1;
	char *pp=NULL;
	int i = 0;
	const char* items[K_MENU_ROOT_CNT+2];
	int menu_cnt = K_MENU_ROOT_CNT+1;
	menu_info* pmenu = menu_root;
	temp_set_visible(1);

	for(i = 0; i < menu_cnt; i++) {
		items[i] = pmenu[i].title;
	}
	items[menu_cnt] = NULL;

	while(1) {
		chosen_item = ui_show_menu(MENU_TITLE_ROOT, items, 1, chosen_item);
		LOGD("[%s] chosen_item = %d\n", __FUNCTION__, chosen_item);
		if(chosen_item >= 0 && chosen_item < menu_cnt) {
			if(pmenu[chosen_item].func != NULL) {
				LOGD("mmitest [%s] select menu = <%s>\n", __FUNCTION__, pmenu[chosen_item].title);
				pmenu[chosen_item].func();
			}
		}
    }

	return 0;
}



static int show_pcba_btswifigps_test_menu(void)
{
		int chosen_item = -1; 
		int i = 0;
		char* items[K_MENU_BTWIFIGPS_CNT+2];
		int menu_cnt = K_MENU_BTWIFIGPS_CNT+1;
		int result = 0;
		menu_info* pmenu = menu_btwifigps_testmenu;
		for(i = 0; i < menu_cnt; i++) {
			items[i] = pmenu[i].title;
		}
		items[menu_cnt] = NULL;
		while(1) {
			chosen_item = ui_show_menu(MENU_TEST_BTWIFIGPS, items, 0, chosen_item);
			LOGD("mmitest [%s] chosen_item = %d\n", __FUNCTION__, chosen_item);
			if(chosen_item >= 0 && chosen_item < menu_cnt) {
				LOGD("mmitest [%s] select menu = <%s>\n", __FUNCTION__, pmenu[chosen_item].title);
				if(chosen_item >= K_MENU_BTWIFIGPS_CNT) {
					return 0;
				}
				if(pmenu[chosen_item].func != NULL) {
					result = pmenu[chosen_item].func();
					//LOGD("mmitest result=%d id=0x%08x\n", result,pmenu[chosen_item].id);
					if(result == RL_FAIL || result == RL_PASS || result == RL_NS)
					{
						test_result_pcba[pmenu[chosen_item].num].value = result;
						//LOGD("mmitest before sql");
						//eng_sql_factorytest_set(test_result[pmenu[chosen_item].num].name, result, pmenu[chosen_item].num);
						//LOGD("mmitest after sql");
						test_result_txt[pmenu[chosen_item].num].status= result;
						test_result_txt[pmenu[chosen_item].num].id=pmenu[chosen_item].id;
						test_result_txt[pmenu[chosen_item].num].yes_no=1;
						//LOGD("mmitest i am here1");
						LOGD("mmitest phoneloop%s 0x%08x %d %d",test_result_txt[pmenu[chosen_item].num].name,test_result_txt[pmenu[chosen_item].num].id,test_result_txt[pmenu[chosen_item].num].yes_no,test_result_txt[pmenu[chosen_item].num].status);
						LOGD("mmitest before write");
						write_txt(PCBATXTPATH,K_MENU_AUTO_TEST_CNT);
                    }
				}
			}
			else if (chosen_item < 0)
			{
				return 0;
			}
        }
        return 0;
}


static int show_phone_btwifigps_test_menu(void)
{
		int chosen_item = -1; 
		int i = 0;
		char* items[K_MENU_BTWIFIGPS_CNT+2];
		int menu_cnt = K_MENU_BTWIFIGPS_CNT+1;
		int result = 0;
		menu_info* pmenu = menu_btwifigps_testmenu;
		for(i = 0; i < menu_cnt; i++) {
			items[i] = pmenu[i].title;
		}
		items[menu_cnt] = NULL;
		while(1) {
			chosen_item = ui_show_menu(MENU_TEST_BTWIFIGPS, items, 0, chosen_item);
			LOGD("mmitest [%s] chosen_item = %d\n", __FUNCTION__, chosen_item);
			if(chosen_item >= 0 && chosen_item < menu_cnt) {
				LOGD("mmitest [%s] select menu = <%s>\n", __FUNCTION__, pmenu[chosen_item].title);
				if(chosen_item >= K_MENU_BTWIFIGPS_CNT) {
					return 0;
				}
				if(pmenu[chosen_item].func != NULL) {
					result = pmenu[chosen_item].func();
					//LOGD("mmitest result=%d id=0x%08x\n", result,pmenu[chosen_item].id);
					if(result == RL_FAIL || result == RL_PASS || result == RL_NS)
					{
						test_result_phone[pmenu[chosen_item].num].value = result;
						//LOGD("mmitest before sql");
						//eng_sql_factorytest_set(test_result[pmenu[chosen_item].num].name, result, pmenu[chosen_item].num);
						//LOGD("mmitest after sql");
						test_result_txt[pmenu[chosen_item].num].status= result;
						test_result_txt[pmenu[chosen_item].num].id=pmenu[chosen_item].id;
						test_result_txt[pmenu[chosen_item].num].yes_no=1;
						//LOGD("mmitest i am here1");
						LOGD("mmitest phoneloop%s 0x%08x %d %d",test_result_txt[pmenu[chosen_item].num].name,test_result_txt[pmenu[chosen_item].num].id,test_result_txt[pmenu[chosen_item].num].yes_no,test_result_txt[pmenu[chosen_item].num].status);
						LOGD("mmitest before write");
						write_txt(PHONETXTPATH,K_MENU_AUTO_TEST_CNT);
					}
				}
			}
			else if (chosen_item < 0)
			{
				return 0;
			}
		}
        return 0;
}




static int show_pcba_bcamera_test_menu(void)
{
		int chosen_item = -1;
		int i = 0;
		char* items[K_MENU_BCAMERA_CNT+2];
		int menu_cnt = K_MENU_BCAMERA_CNT+1;
		int result = 0;
		menu_info* pmenu = menu_bc_fl_testmenu;
		for(i = 0; i < menu_cnt; i++) {
			items[i] = pmenu[i].title;
		}
		items[menu_cnt] = NULL;
		while(1) {
			chosen_item = ui_show_menu(MENU_TEST_BCAMERA_FLASH, items, 0, chosen_item);
			LOGD("mmitest [%s] chosen_item = %d\n", __FUNCTION__, chosen_item);
			if(chosen_item >= 0 && chosen_item < menu_cnt) {
				LOGD("mmitest [%s] select menu = <%s>\n", __FUNCTION__, pmenu[chosen_item].title);
				if(chosen_item >= K_MENU_BCAMERA_CNT) {
					return 0;
				}
				if(pmenu[chosen_item].func != NULL) {
					result = pmenu[chosen_item].func();
					//LOGD("mmitest result=%d id=0x%08x\n", result,pmenu[chosen_item].id);
					if(result == RL_FAIL || result == RL_PASS || result == RL_NS)
					{
						test_result_pcba[pmenu[chosen_item].num].value = result;
						//LOGD("mmitest before sql");
						//eng_sql_factorytest_set(test_result[pmenu[chosen_item].num].name, result, pmenu[chosen_item].num);
						//LOGD("mmitest after sql");
						test_result_txt[pmenu[chosen_item].num].status= result;
						test_result_txt[pmenu[chosen_item].num].id=pmenu[chosen_item].id;
						test_result_txt[pmenu[chosen_item].num].yes_no=1;
						//LOGD("mmitest i am here1");
						LOGD("mmitest phoneloop%s 0x%08x %d %d",test_result_txt[pmenu[chosen_item].num].name,test_result_txt[pmenu[chosen_item].num].id,test_result_txt[pmenu[chosen_item].num].yes_no,test_result_txt[pmenu[chosen_item].num].status);
						LOGD("mmitest before write");
						write_txt(PCBATXTPATH,K_MENU_AUTO_TEST_CNT);
					}
				}
			}
			else if (chosen_item < 0)
			{
				return 0;
			}
		}
        return 0;
}




static int show_phone_bcamera_test_menu(void)
{
		int chosen_item = -1; 
		int i = 0;
		char* items[K_MENU_BCAMERA_CNT+2];
		int menu_cnt = K_MENU_BCAMERA_CNT+1;
		int result = 0;
		menu_info* pmenu = menu_bc_fl_testmenu;
		for(i = 0; i < menu_cnt; i++) {
			items[i] = pmenu[i].title;
		}
		items[menu_cnt] = NULL;
		while(1) {
			chosen_item = ui_show_menu(MENU_TEST_BCAMERA_FLASH, items, 0, chosen_item);
			LOGD("mmitest [%s] chosen_item = %d\n", __FUNCTION__, chosen_item);
			if(chosen_item >= 0 && chosen_item < menu_cnt) {
				LOGD("mmitest [%s] select menu = <%s>\n", __FUNCTION__, pmenu[chosen_item].title);
				if(chosen_item >= K_MENU_BCAMERA_CNT) {
					return 0;
				}
				if(pmenu[chosen_item].func != NULL) {
					result = pmenu[chosen_item].func();
					//LOGD("mmitest result=%d id=0x%08x\n", result,pmenu[chosen_item].id);
					if(result == RL_FAIL || result == RL_PASS || result == RL_NS)
					{
						test_result_phone[pmenu[chosen_item].num].value = result;
						//LOGD("mmitest before sql");
						//eng_sql_factorytest_set(test_result[pmenu[chosen_item].num].name, result, pmenu[chosen_item].num);
						//LOGD("mmitest after sql");
						test_result_txt[pmenu[chosen_item].num].status= result;
						test_result_txt[pmenu[chosen_item].num].id=pmenu[chosen_item].id;
						test_result_txt[pmenu[chosen_item].num].yes_no=1;
						//LOGD("mmitest i am here1");
						LOGD("mmitest phoneloop%s 0x%08x %d %d",test_result_txt[pmenu[chosen_item].num].name,test_result_txt[pmenu[chosen_item].num].id,test_result_txt[pmenu[chosen_item].num].yes_no,test_result_txt[pmenu[chosen_item].num].status);
						LOGD("mmitest before write");
						write_txt(PHONETXTPATH,K_MENU_AUTO_TEST_CNT);
					}
				}
			}
			else if (chosen_item < 0)
			{
				return 0;
			}
		}
		return 0;
}



static int show_pcba_vb_bl_menu(void)
{
	int chosen_item = -1;
	int i = 0;
	char* items[K_MENU_VB_BL_CNT+2];
	int menu_cnt = K_MENU_VB_BL_CNT+1;
	int result = 0;
	menu_info* pmenu = menu_vb_bl_testmenu;

	for(i = 0; i < menu_cnt; i++) {
		items[i] = pmenu[i].title;
	}
	items[menu_cnt] = NULL;

	while(1) {
		chosen_item = ui_show_menu(MENU_TEST_VB_BL, items, 0, chosen_item);
		LOGD("mmitest [%s] chosen_item = %d\n", __FUNCTION__, chosen_item);
		if(chosen_item >= 0 && chosen_item < menu_cnt) {
			LOGD("mmitest [%s] select menu = <%s>\n", __FUNCTION__, pmenu[chosen_item].title);
			if(chosen_item >= K_MENU_VB_BL_CNT) {
				return 0;
			}
			if(pmenu[chosen_item].func != NULL) {
				result = pmenu[chosen_item].func();
				//LOGD("mmitest result=%d id=0x%08x\n", result,pmenu[chosen_item].id);
				if(result == RL_FAIL || result == RL_PASS || result == RL_NS)
				{
					test_result_pcba[pmenu[chosen_item].num].value = result;
					//LOGD("mmitest before sql");
					//eng_sql_factorytest_set(test_result[pmenu[chosen_item].num].name, result, pmenu[chosen_item].num);
					//LOGD("mmitest after sql");
					test_result_txt[pmenu[chosen_item].num].status= result;
					test_result_txt[pmenu[chosen_item].num].id=pmenu[chosen_item].id;
					test_result_txt[pmenu[chosen_item].num].yes_no=1;
					//LOGD("mmitest i am here1");
					LOGD("mmitest phoneloop%s 0x%08x %d %d",test_result_txt[pmenu[chosen_item].num].name,test_result_txt[pmenu[chosen_item].num].id,test_result_txt[pmenu[chosen_item].num].yes_no,test_result_txt[pmenu[chosen_item].num].status);

					LOGD("mmitest before write");
					write_txt(PCBATXTPATH,K_MENU_AUTO_TEST_CNT);
				}
			}
		}
		else if (chosen_item < 0)
		{
			return 0;
		}
    }
	return 0;
}




static int show_phone_vb_bl_menu(void)
{
	int chosen_item = -1;
	int i = 0;
	char* items[K_MENU_VB_BL_CNT+2];
	int menu_cnt = K_MENU_VB_BL_CNT+1;
	int result = 0;
	menu_info* pmenu = menu_vb_bl_testmenu;

	for(i = 0; i < menu_cnt; i++) {
		items[i] = pmenu[i].title;
	}
	items[menu_cnt] = NULL;

	while(1) {
		chosen_item = ui_show_menu(MENU_TEST_VB_BL, items, 0, chosen_item);
		LOGD("mmitest [%s] chosen_item = %d\n", __FUNCTION__, chosen_item);
		if(chosen_item >= 0 && chosen_item < menu_cnt) {
			LOGD("mmitest [%s] select menu = <%s>\n", __FUNCTION__, pmenu[chosen_item].title);
			if(chosen_item >= K_MENU_VB_BL_CNT) {
				return 0;
			}
			if(pmenu[chosen_item].func != NULL) {
				result = pmenu[chosen_item].func();
				//LOGD("mmitest result=%d id=0x%08x\n", result,pmenu[chosen_item].id);
				if(result == RL_FAIL || result == RL_PASS || result == RL_NS)
				{
					test_result_phone[pmenu[chosen_item].num].value = result;
					//LOGD("mmitest before sql");
					//eng_sql_factorytest_set(test_result[pmenu[chosen_item].num].name, result, pmenu[chosen_item].num);
					//LOGD("mmitest after sql");
					test_result_txt[pmenu[chosen_item].num].status= result;
					test_result_txt[pmenu[chosen_item].num].id=pmenu[chosen_item].id;
					test_result_txt[pmenu[chosen_item].num].yes_no=1;
					//LOGD("mmitest i am here1");
					LOGD("mmitest phoneloop%s 0x%08x %d %d",test_result_txt[pmenu[chosen_item].num].name,test_result_txt[pmenu[chosen_item].num].id,test_result_txt[pmenu[chosen_item].num].yes_no,test_result_txt[pmenu[chosen_item].num].status);

					LOGD("mmitest before write");
					write_txt(PHONETXTPATH,K_MENU_AUTO_TEST_CNT);
				}
			}
		}
		else if (chosen_item < 0)
		{
			return 0;
		}
    }
	return 0;
}






static int show_pcba_voice_test_menu(void)
{
	int chosen_item = -1;
	int i = 0;
	char* items[K_MENU_VOICE_CNT+2];
	int menu_cnt = K_MENU_VOICE_CNT+1;
	int result = 0;
	menu_info* pmenu = menu_voice_testmenu;

	for(i = 0; i < menu_cnt; i++) {
		items[i] = pmenu[i].title;
	}
	items[menu_cnt] = NULL;

	while(1) {
		chosen_item = ui_show_menu(MENU_TEST_VOICE, items, 0, chosen_item);
		LOGD("mmitest [%s] chosen_item = %d\n", __FUNCTION__, chosen_item);
		if(chosen_item >= 0 && chosen_item < menu_cnt) {
			LOGD("mmitest [%s] select menu = <%s>\n", __FUNCTION__, pmenu[chosen_item].title);
			if(chosen_item >= K_MENU_VOICE_CNT) {
				return 0;
			}
			if(pmenu[chosen_item].func != NULL) {
				result = pmenu[chosen_item].func();
				//LOGD("mmitest result=%d id=0x%08x\n", result,pmenu[chosen_item].id);
				if(result == RL_FAIL || result == RL_PASS || result == RL_NS)
				{
					test_result_pcba[pmenu[chosen_item].num].value = result;
					//LOGD("mmitest before sql");
					//eng_sql_factorytest_set(test_result[pmenu[chosen_item].num].name, result, pmenu[chosen_item].num);
					//LOGD("mmitest after sql");
					test_result_txt[pmenu[chosen_item].num].status= result;
					test_result_txt[pmenu[chosen_item].num].id=pmenu[chosen_item].id;
					test_result_txt[pmenu[chosen_item].num].yes_no=1;
					//LOGD("mmitest i am here1");
					LOGD("mmitest phoneloop%s 0x%08x %d %d",test_result_txt[pmenu[chosen_item].num].name,test_result_txt[pmenu[chosen_item].num].id,test_result_txt[pmenu[chosen_item].num].yes_no,test_result_txt[pmenu[chosen_item].num].status);

					LOGD("mmitest before write");
					write_txt(PCBATXTPATH,K_MENU_AUTO_TEST_CNT);
				}
            }
		}
		else if (chosen_item < 0)
		{
			return 0;
		}
    }
	return 0;
}




static int show_phone_voice_test_menu(void)
{
	int chosen_item = -1;
	int i = 0;
	char* items[K_MENU_VOICE_CNT+2];
	int menu_cnt = K_MENU_VOICE_CNT+1;
	int result = 0;
	menu_info* pmenu = menu_voice_testmenu;

	for(i = 0; i < menu_cnt; i++) {
		items[i] = pmenu[i].title;
	}
	items[menu_cnt] = NULL;

	while(1) {
		chosen_item = ui_show_menu(MENU_TEST_VOICE, items, 0, chosen_item);
		LOGD("mmitest [%s] chosen_item = %d\n", __FUNCTION__, chosen_item);
		if(chosen_item >= 0 && chosen_item < menu_cnt) {
			LOGD("mmitest [%s] select menu = <%s>\n", __FUNCTION__, pmenu[chosen_item].title);
			if(chosen_item >= K_MENU_VOICE_CNT) {
				return 0;
			}
			if(pmenu[chosen_item].func != NULL) {
				result = pmenu[chosen_item].func();
				//LOGD("mmitest result=%d id=0x%08x\n", result,pmenu[chosen_item].id);
				if(result == RL_FAIL || result == RL_PASS || result == RL_NS)
				{
					test_result_phone[pmenu[chosen_item].num].value = result;
					//LOGD("mmitest before sql");
					//eng_sql_factorytest_set(test_result[pmenu[chosen_item].num].name, result, pmenu[chosen_item].num);
					//LOGD("mmitest after sql");
					test_result_txt[pmenu[chosen_item].num].status= result;
					test_result_txt[pmenu[chosen_item].num].id=pmenu[chosen_item].id;
					test_result_txt[pmenu[chosen_item].num].yes_no=1;
					//LOGD("mmitest i am here1");
					LOGD("mmitest phoneloop%s 0x%08x %d %d",test_result_txt[pmenu[chosen_item].num].name,test_result_txt[pmenu[chosen_item].num].id,test_result_txt[pmenu[chosen_item].num].yes_no,test_result_txt[pmenu[chosen_item].num].status);

					LOGD("mmitest before write");
					write_txt(PHONETXTPATH,K_MENU_AUTO_TEST_CNT);
				}
			}
		}
		else if (chosen_item < 0)
		{
			return 0;
		}
    }
	return 0;
}



static int show_phone_loop_test_menu(void)
{
	int chosen_item = -1;
	int i = 0;
	char* items[K_MENU_LOOP_CNT+2];
	int menu_cnt = K_MENU_LOOP_CNT+1;
	int result = 0;
	menu_info* pmenu = menu_phone_loopmenu;

	for(i = 0; i < menu_cnt; i++) {
		items[i] = pmenu[i].title;
	}
	items[menu_cnt] = NULL;

	while(1) {
		chosen_item = ui_show_menu(MENU_TEST_PHONE_LOOPBACK, items, 0, chosen_item);
		LOGD("mmitest [%s] chosen_item = %d\n", __FUNCTION__, chosen_item);
		if(chosen_item >= 0 && chosen_item < menu_cnt) {
			LOGD("mmitest [%s] select menu = <%s>\n", __FUNCTION__, pmenu[chosen_item].title);
			if(chosen_item >= K_MENU_LOOP_CNT) {
				return 0;
			}
			if(pmenu[chosen_item].func != NULL) {
				result = pmenu[chosen_item].func();
				//LOGD("mmitest result=%d id=0x%08x\n", result,pmenu[chosen_item].id);
				if(result == RL_FAIL || result == RL_PASS || result == RL_NS)
				{
					test_result_phone[pmenu[chosen_item].num].value = result;
					//LOGD("mmitest before sql");
					//eng_sql_factorytest_set(test_result[pmenu[chosen_item].num].name, result, pmenu[chosen_item].num);
					//LOGD("mmitest after sql");
					test_result_txt[pmenu[chosen_item].num].status= result;
					test_result_txt[pmenu[chosen_item].num].id=pmenu[chosen_item].id;
					test_result_txt[pmenu[chosen_item].num].yes_no=1;
					//LOGD("mmitest i am here1");
					LOGD("mmitest phoneloop%s 0x%08x %d %d",test_result_txt[pmenu[chosen_item].num].name,test_result_txt[pmenu[chosen_item].num].id,test_result_txt[pmenu[chosen_item].num].yes_no,test_result_txt[pmenu[chosen_item].num].status);

					LOGD("mmitest before write");
					write_txt(PHONETXTPATH,K_MENU_AUTO_TEST_CNT);
				}
			}
		}
		else if (chosen_item < 0)
		{
			return 0;
		}
    }
	return 0;
}




static int show_pcba_phone_loop_test_menu(void)
{
	int chosen_item = -1;
	int i = 0;
	char* items[K_MENU_LOOP_CNT+2];
	int menu_cnt = K_MENU_LOOP_CNT+1;
	int result = 0;
	menu_info* pmenu = menu_phone_loopmenu;

	for(i = 0; i < menu_cnt; i++) {
		items[i] = pmenu[i].title;
	}
	items[menu_cnt] = NULL;

	while(1) {
		chosen_item = ui_show_menu(MENU_TEST_PHONE_LOOPBACK, items, 0, chosen_item);
		LOGD("mmitest [%s] chosen_item = %d\n", __FUNCTION__, chosen_item);
		if(chosen_item >= 0 && chosen_item < menu_cnt) {
			LOGD("mmitest [%s] select menu = <%s>\n", __FUNCTION__, pmenu[chosen_item].title);
			if(chosen_item >= K_MENU_LOOP_CNT) {
				return 0;
			}
			if(pmenu[chosen_item].func != NULL) {
				result = pmenu[chosen_item].func();
				LOGD("mmitest result=%d id=0x%08x\n", result,pmenu[chosen_item].id);
				if(result == RL_FAIL || result == RL_PASS || result == RL_NS)
				{
					test_result_pcba[pmenu[chosen_item].num].value = result;
					//LOGD("mmitest before sql");
					//eng_sql_factorytest_set(test_result[pmenu[chosen_item].num].name, result, pmenu[chosen_item].num);
					//LOGD("mmitest after sql");
					test_result_txt[pmenu[chosen_item].num].status= result;
					test_result_txt[pmenu[chosen_item].num].id=pmenu[chosen_item].id;
					test_result_txt[pmenu[chosen_item].num].yes_no=1;
					//LOGD("mmitest i am here1");
					LOGD("mmitest phoneloop%s 0x%08x %d %d",test_result_txt[pmenu[chosen_item].num].name,test_result_txt[pmenu[chosen_item].num].id,test_result_txt[pmenu[chosen_item].num].yes_no,test_result_txt[pmenu[chosen_item].num].status);

					LOGD("mmitest before write");
					write_txt(PCBATXTPATH,K_MENU_AUTO_TEST_CNT);
				}
			}
		}
		else if (chosen_item < 0)
		{
			return 0;
		}
    }
	return 0;
}



static int show_sensor_test_menu(void)
{
	int chosen_item = -1;
	int i = 0;
	char* items[K_MENU_SENSOR_CNT+2];
	int menu_cnt = K_MENU_SENSOR_CNT+1;
	int result = 0;
	menu_info* pmenu = menu_sensor_testmenu;

	for(i = 0; i < menu_cnt; i++) {
		items[i] = pmenu[i].title;
	}
	items[menu_cnt] = NULL;

	while(1) {
		chosen_item = ui_show_menu(MENU_TEST_SENSOR, items, 0, chosen_item);
		LOGD("mmitest [%s] chosen_item = %d\n", __FUNCTION__, chosen_item);
		if(chosen_item >= 0 && chosen_item < menu_cnt) {
			LOGD("mmitest [%s] select menu = <%s>\n", __FUNCTION__, pmenu[chosen_item].title);
			if(chosen_item >= K_MENU_SENSOR_CNT) {
				return 0;
			}
			if(pmenu[chosen_item].func != NULL) {
				result = pmenu[chosen_item].func();
				//LOGD("mmitest num=%d result=%d id=0x%08x\n", pmenu[chosen_item].num, result,pmenu[chosen_item].id);
				if(result == RL_FAIL || result == RL_PASS || result == RL_NS)
				{

					test_result_phone[pmenu[chosen_item].num].value = result;
					LOGD("mmitest before sql");
					//eng_sql_factorytest_set(test_result[pmenu[chosen_item].num].name, result, pmenu[chosen_item].num);
					LOGD("mmitest after sql");

					test_result_txt[pmenu[chosen_item].num].status= result;
					test_result_txt[pmenu[chosen_item].num].id=pmenu[chosen_item].id;
					test_result_txt[pmenu[chosen_item].num].yes_no=1;
					LOGD("mmitest sensortest%s 0x%08x %d %d",test_result_txt[pmenu[chosen_item].num].name,test_result_txt[pmenu[chosen_item].num].id,test_result_txt[pmenu[chosen_item].num].yes_no,test_result_txt[pmenu[chosen_item].num].status);
					LOGD("mmitest before write");
					write_txt(PHONETXTPATH,K_MENU_AUTO_TEST_CNT);
				}
			}
		}
		else if (chosen_item < 0)
		{
			return 0;
		}
    }
	return 0;
}


static int show_pcba_sensor_test_menu(void)
{
	int chosen_item = -1;
	int i = 0;
	char* items[K_MENU_SENSOR_CNT+2];
	int menu_cnt = K_MENU_SENSOR_CNT+1;
	int result = 0;
	menu_info* pmenu = menu_sensor_testmenu;

	for(i = 0; i < menu_cnt; i++) {
		items[i] = pmenu[i].title;
	}
	items[menu_cnt] = NULL;

	while(1) {
		chosen_item = ui_show_menu(MENU_TEST_SENSOR, items, 0, chosen_item);
		LOGD("mmitest [%s] chosen_item = %d\n", __FUNCTION__, chosen_item);
		if(chosen_item >= 0 && chosen_item < menu_cnt) {
			LOGD("mmitest [%s] select menu = <%s>\n", __FUNCTION__, pmenu[chosen_item].title);
			if(chosen_item >= K_MENU_SENSOR_CNT) {
				return 0;
			}
			if(pmenu[chosen_item].func != NULL) {
				result = pmenu[chosen_item].func();
				//LOGD("mmitest num=%d result=%d id=0x%08x\n", pmenu[chosen_item].num, result,pmenu[chosen_item].id);
				if(result == RL_FAIL || result == RL_PASS || result == RL_NS)
				{

					test_result_pcba[pmenu[chosen_item].num].value = result;
					LOGD("mmitest before sql");
					//eng_sql_factorytest_set(test_result[pmenu[chosen_item].num].name, result, pmenu[chosen_item].num);
					LOGD("mmitest after sql");

					test_result_txt[pmenu[chosen_item].num].status= result;
					test_result_txt[pmenu[chosen_item].num].id=pmenu[chosen_item].id;
					test_result_txt[pmenu[chosen_item].num].yes_no=1;
					LOGD("mmitest sensortest%s 0x%08x %d %d",test_result_txt[pmenu[chosen_item].num].name,test_result_txt[pmenu[chosen_item].num].id,test_result_txt[pmenu[chosen_item].num].yes_no,test_result_txt[pmenu[chosen_item].num].status);
					LOGD("mmitest before write");
					write_txt(PCBATXTPATH,K_MENU_AUTO_TEST_CNT);
				}
			}
		}
		else if (chosen_item < 0)
		{
			return 0;
		}
    }
	return 0;
}



static int show_phone_test_menu(void)
{
	int chosen_item = -1;
	int i = 0;
	char* items[K_MENU_PHONE_TEST_CNT+2];
	int menu_cnt = K_MENU_PHONE_TEST_CNT+1;
	int result = 0;
	menu_info* pmenu = menu_phone_test;
	//temp_set_visible(1);

	for(i = 0; i < menu_cnt; i++) {
		items[i] = pmenu[i].title;
	}
	items[menu_cnt] = NULL;

	while(1) {
		LOGD("mmitest back to main");
		chosen_item = ui_show_menu(MENU_TITLE_PHONETEST, items, 0, chosen_item);
		LOGD("mmitest [%s] chosen_item = %d\n", __FUNCTION__, chosen_item);
		if(chosen_item >= 0 && chosen_item < menu_cnt) {
			LOGD("mmitest [%s] select menu = <%s>\n", __FUNCTION__, pmenu[chosen_item].title);
			if(chosen_item >= K_MENU_PHONE_TEST_CNT) {
				return 0;
			}
			if(pmenu[chosen_item].func != NULL) {
				result = pmenu[chosen_item].func();
				LOGD("mmitest result=%d id=0x%08x\n", result,pmenu[chosen_item].id);
				if(result == RL_FAIL || result == RL_PASS || result == RL_NS)
				{

					test_result_phone[pmenu[chosen_item].num].value = result;
					//LOGD("mmitest before sql");
					//eng_sql_factorytest_set(test_result[pmenu[chosen_item].num].name, result, pmenu[chosen_item].num);
					//LOGD("mmitest after sql");

					test_result_txt[pmenu[chosen_item].num].status= result;
					test_result_txt[pmenu[chosen_item].num].id=pmenu[chosen_item].id;
					test_result_txt[pmenu[chosen_item].num].yes_no=1;
					LOGD("mmitest phonetest%s 0x%08x %d %d",test_result_txt[pmenu[chosen_item].num].name,test_result_txt[pmenu[chosen_item].num].id,test_result_txt[pmenu[chosen_item].num].yes_no,test_result_txt[pmenu[chosen_item].num].status);
					LOGD("mmitest before write");

					write_txt(PHONETXTPATH,K_MENU_AUTO_TEST_CNT);	
				}
			}
		//write_txt(PHONEpath,K_MENU_PHONE_TEST_CNT);	
		}
		else if (chosen_item < 0)
		{
			return 0;
		}
    }
	return 0;
} 



static int show_pcba_test_menu(void)
{
	int chosen_item = -1;
	int i = 0;
	char* items[P_MENU_PHONE_TEST_CNT+2];
	int menu_cnt = P_MENU_PHONE_TEST_CNT+1;
	int result = 0;
	unsigned char txt_flag=1;
	menu_info* pmenu = menu_pcba_test;
	int ret;
	//temp_set_visible(1);

	for(i = 0; i < menu_cnt; i++) {
		items[i] = pmenu[i].title;
	}
	items[menu_cnt] = NULL;

	while(1) {
		chosen_item = ui_show_menu(MENU_TITLE_PHONETEST, items, 0, chosen_item);
		LOGD("mmitest [%s] chosen_item = %d\n", __FUNCTION__, chosen_item);
		if(chosen_item >= 0 && chosen_item < menu_cnt) {
			LOGD("mmitest [%s] select menu = <%s>\n", __FUNCTION__, pmenu[chosen_item].title);
			if(chosen_item >= P_MENU_PHONE_TEST_CNT) {
				return 0;
			}
			if(pmenu[chosen_item].func != NULL) {
				result = pmenu[chosen_item].func();
				LOGD("mmitest result=%d id=0x%08x\n", result,pmenu[chosen_item].id);
				if(result == RL_FAIL || result == RL_PASS || result == RL_NS)
				{
					//system("mkdir /data/misc/wifi");
					test_result_pcba[pmenu[chosen_item].num].value = result;
					//LOGD("mmitest before sql");
					//eng_sql_factorytest_set(test_result[pmenu[chosen_item].num].name, result, pmenu[chosen_item].num);
					//LOGD("mmitest after sql");
					test_result_txt[pmenu[chosen_item].num].status= result;
					test_result_txt[pmenu[chosen_item].num].id=pmenu[chosen_item].id;
					test_result_txt[pmenu[chosen_item].num].yes_no=1;
					LOGD("mmitest pcbatest%s 0x%08x %d %d",test_result_txt[pmenu[chosen_item].num].name,test_result_txt[pmenu[chosen_item].num].id,test_result_txt[pmenu[chosen_item].num].yes_no,test_result_txt[pmenu[chosen_item].num].status);
					LOGD("mmitest before write");
					write_txt(PCBATXTPATH,K_MENU_AUTO_TEST_CNT);
				}
			}
			//LOGD("leon%s","i am here");
		//write_txt(PCBApath,K_MENU_PHONE_TEST_CNT);
		}
		else if (chosen_item < 0)
		{
			return 0;
		}
    }
	return 0;
}



#define MULTI_TEST_CNT	5
multi_test_info multi_test_item[MULTI_TEST_CNT] = {
	{13,15,A_PHONE_TEST_SDCARD, test_sdcard_pretest},
	{14,16,A_PHONE_TEST_SIMCARD, test_sim_pretest},
	{20,23,A_PHONE_TEST_WIFI, test_wifi_pretest},
	{19,22,A_PHONE_TEST_BT, test_bt_pretest},
	{21,24,A_PHONE_TEST_GPS, test_gps_pretest}
};

static void show_multi_phone_test_result(void)
{
	int ret = RL_NA;
	int row = 3;
	char tmp[128];
	char* rl_str;
	int i;
	multi_test_info* ptest = multi_test_item;
	menu_info* pmenu = menu_auto_test;
	ui_fill_locked();
	ui_show_title(MENU_MULTI_TEST);
	gr_flip();

	for(i = 0; i < MULTI_TEST_CNT; i++) {
		ret = ptest[i].func();
		if(ret == RL_FAIL || ret == RL_PASS || ret == RL_NS)
		{

			test_result_phone[ptest[i].num].value = ret;
			LOGD("mmitest before sql");
			//eng_sql_factorytest_set(test_result[ptest[i].num].name, ret, ptest[i].num);
			LOGD("mmitest after sql");
			test_result_txt[ptest[i].num].status=ret;
			test_result_txt[ptest[i].num].id=ptest[i].rid;
			test_result_txt[ptest[i].num].yes_no=1;
			LOGD("mmitest multitest%s 0x%08x %d %d",test_result_txt[ptest[i].num].name,test_result_txt[ptest[i].num].id,test_result_txt[ptest[i].num].yes_no,test_result_txt[ptest[i].num].status);

		}

		if(ret == RL_PASS) {
			ui_set_color(CL_GREEN);
			rl_str = TEXT_PASS;
		} else {
			ui_set_color(CL_RED);
			rl_str = TEXT_FAIL;
		}
		memset(tmp, 0, sizeof(tmp));
		sprintf(tmp, "%s: %s", (pmenu[ptest[i].num].title+1), rl_str);
		row = ui_show_text(row, 0, tmp);
		gr_flip();
	}

	sleep(1);
}



static void show_multi_pcba_test_result(void)
{
	int ret = RL_NA;
	int row = 3;
	char tmp[128];
	char* rl_str;
	int i;
	multi_test_info* ptest = multi_test_item;
	menu_info* pmenu = menu_auto_test;
	ui_fill_locked();
	ui_show_title(MENU_MULTI_TEST);
	gr_flip();

	for(i = 0; i < MULTI_TEST_CNT; i++) {
		ret = ptest[i].func();
		if(ret == RL_FAIL || ret == RL_PASS || ret == RL_NS)
		{

			test_result_pcba[ptest[i].num].value = ret;
			LOGD("mmitest before sql");
			//eng_sql_factorytest_set(test_result[ptest[i].num].name, ret, ptest[i].num);
			LOGD("mmitest after sql");
			test_result_txt[ptest[i].num].status=ret;
			test_result_txt[ptest[i].num].id=ptest[i].rid;
			test_result_txt[ptest[i].num].yes_no=1;
			LOGD("mmitest multitest%s 0x%08x %d %d",test_result_txt[ptest[i].num].name,test_result_txt[ptest[i].num].id,test_result_txt[ptest[i].num].yes_no,test_result_txt[ptest[i].num].status);

		}
		if(ret == RL_PASS) {
			ui_set_color(CL_GREEN);
			rl_str = TEXT_PASS;
		} else {
			ui_set_color(CL_RED);
			rl_str = TEXT_FAIL;
		}
		memset(tmp, 0, sizeof(tmp));
		sprintf(tmp, "%s: %s", (pmenu[ptest[i].num].title+1), rl_str);
		row = ui_show_text(row, 0, tmp);
		gr_flip();
	}

	sleep(1);
}



static int auto_all_test(void)
{
	int i = 0;
	int j = 0;
	int result = 0;
	char* rl_str;
	menu_info* pmenu = menu_auto_test;
	for(i = 0; i < K_MENU_AUTO_TEST_CNT; i++){
		for(j = 0; j < MULTI_TEST_CNT; j++) {
			if(i == multi_test_item[j].num) {
				LOGD("mmitest break, id=%d", i);
				break;
			}
		}
		if(j < MULTI_TEST_CNT) {
			continue;
		}
		LOGD("mmitest Do, id=%d", i);
		if(pmenu[i].func != NULL) {
			result = pmenu[i].func();
			//LOGD("[%s] result=%d, value=%d\n", __FUNCTION__, result, test_result[i].value);
			if(result == RL_FAIL || result == RL_PASS || result == RL_NS)
			{
				test_result_phone[pmenu[i].num].value = result;
				LOGD("mmitest before sql");
				//eng_sql_factorytest_set(test_result[pmenu[i].num].name, result, pmenu[i].num);
				LOGD("mmitest after sql");
				test_result_txt[pmenu[i].num].status= result;
				test_result_txt[pmenu[i].num].id=pmenu[i].id;
				test_result_txt[pmenu[i].num].yes_no=1;
				LOGD("mmitest aphone%s 0x%08x %d %d",test_result_txt[pmenu[i].num].name,test_result_txt[pmenu[i].num].id,test_result_txt[pmenu[i].num].yes_no,test_result_txt[pmenu[i].num].status);

			}
		}
	}
	show_multi_phone_test_result();
	LOGD("mmitest before write");
	write_txt(PHONETXTPATH,K_MENU_AUTO_TEST_CNT);
	return 0;
}


static int PCBA_auto_all_test(void)
{
	int i = 0;
	int j = 0;
	int k = 0;
	int result = 0;
	char* rl_str;
	menu_info* pmenu = menu_auto_test;
	for(i = 1; i < K_MENU_AUTO_TEST_CNT; i++){
		for(j = 0; j < MULTI_TEST_CNT; j++) {
			if(i == multi_test_item[j].num) {
				LOGD("mmitest break, id=%d", i);
				break;
			}
		}
		if(j < MULTI_TEST_CNT) {
			continue;
		}
		if(pmenu[i].func != NULL) {
			LOGD("mmitest Do id=%d", i);
			result = pmenu[i].func();
			//LOGD("mmitest[%s] result=%d, value=%d\n", __FUNCTION__, result, test_result[i].value);
			if( result == RL_FAIL || result == RL_PASS || result == RL_NS) 
			{
				test_result_pcba[pmenu[i].num].value = result;
				LOGD("mmitest before sql");
				//eng_sql_factorytest_set(test_result[pmenu[i].num].name, result, pmenu[i].num);
				LOGD("mmitest after sql");
				test_result_txt[pmenu[i].num].status= result;
				test_result_txt[pmenu[i].num].id=pmenu[i].id;
				test_result_txt[pmenu[i].num].yes_no=1;
				LOGD("mmitest apcba %s 0x%08x %d %d",test_result_txt[pmenu[i].num].name,test_result_txt[pmenu[i].num].id,test_result_txt[pmenu[i].num].yes_no,test_result_txt[pmenu[i].num].status);
			}
		}

	}
	show_multi_pcba_test_result();
	LOGD("mmitest before write");
	write_txt(PCBATXTPATH,K_MENU_AUTO_TEST_CNT);
	return 0;
}





static void test_init(void)
{
	int i = 0;
	int value = -1;
	/*************BT****************/

	system("mkdir /etc/bluetooth");
	system("chmod 0777 /etc/bluetooth");
	system("cp /system/etc/bluetooth/auto_pair_devlist.conf /etc/bluetooth/");
    system("cp /system/etc/bluetooth/bt_did.conf /etc/bluetooth/");
	system("cp /system/etc/bluetooth/bt_stack.conf /etc/bluetooth/");
	system("cp /system/etc/bluetooth/bt_vendor.conf /etc/bluetooth/");
	/****************************/
	ui_init();
	ui_set_background(BACKGROUND_ICON_NONE);
	if(0 != eng_sqlite_create()) {
		LOGD("mmitest [%s]: sqilte create failed!", __func__);
	}
	LOGD("=== show test result ===\n");
	for(i = 0; i < K_MENU_AUTO_TEST_CNT; i++){
		//value = eng_sql_factorytest_get(test_result[i].name);
		/*if(value>=RL_NA && value<=RL_PASS )
		{
			test_result[i].value = value;
			//test_result_txt[i].status=value;
		} else
		{*/
			//eng_sql_factorytest_set(test_result[i].name, RL_NA, i);
			test_result_pcba[i].value = RL_NA;
		    test_result_phone[i].value=RL_NA;
			//test_result_txt[i].status= RL_NA;
		//}
		/*********ID init******************/
		//test_result_txt[i].id=i;
		//test_result_txt[i].yes_no=1;
		//LOGD("<%d>-%s,%d\n", i, test_result[i].name, test_result[i].value);
		//LOGD("writetest<%d>-%s,%d\n", i, test_result_txt[i].name, test_result_txt[i].status);
	}
}

static int show_phone_test_result(void)
{
	int row = 2;
	int i = 0;
	char tmp[128];
	char* rl_str;
	char* rl_str1;
	char* rl_str2;
	//char* ptrpass=TEXT_PASS;
	char* ptrfail=TEXT_FAIL;
	char* ptrna=TEXT_NA;
	menu_info* pmenu = menu_auto_test;
	ui_fill_locked();
	ui_show_title(TEST_REPORT);
	for(i = 0; i < K_MENU_AUTO_TEST_CNT; i++){
		LOGD("mmitest <%d>-%s,%d\n", i, test_result_phone[i].name, test_result_phone[i].value);


		if(i>=2&&i<=3)
			{
				if(i==2)
				{
					switch(test_result_phone[i].value)
						{
							case RL_NA:
								//ui_set_color(CL_WHITE);
								rl_str = TEXT_NA;
								break;
							case RL_FAIL:
								//ui_set_color(CL_RED);
								rl_str = TEXT_FAIL;
								break;
							case RL_PASS:
								//ui_set_color(CL_GREEN);
								rl_str = TEXT_PASS;
								break;
							default:
								//ui_set_color(CL_WHITE);
								rl_str = TEXT_NA;
								break;
						}
				}
				if(i==3)
					{
						switch(test_result_phone[i].value)
							{
								case RL_NA:
									//ui_set_color(CL_WHITE);
									rl_str1 = TEXT_NA;
									break;
								case RL_FAIL:
									//ui_set_color(CL_RED);
									rl_str1 = TEXT_FAIL;
									break;
								case RL_PASS:
									//ui_set_color(CL_GREEN);
									rl_str1 = TEXT_PASS;
									break;
								default:
									//ui_set_color(CL_WHITE);
									rl_str1 = TEXT_NA;
									break;
							}
					if((strcmp(rl_str1, ptrfail) == 0)||(strcmp(rl_str, ptrfail) == 0))
						{
							rl_str2=TEXT_FAIL;
							ui_set_color(CL_RED);
							LOGD("mmitest %s\n",rl_str2);
						}
					else if((strcmp(rl_str1, ptrna) == 0)||(strcmp(rl_str, ptrna) == 0))
						{
							rl_str2=TEXT_NA;
							ui_set_color(CL_WHITE);
							LOGD("mmitest %s\n",rl_str2);
						}
					else
						{
							rl_str2=TEXT_PASS;
							ui_set_color(CL_GREEN);
							LOGD("mmitest %s\n",rl_str2);
						}
					//LOGD("mmitest%s\n",rl_str2);
					memset(tmp, 0, sizeof(tmp));
					sprintf(tmp, "%s:%s",MENU_TEST_VB_BL_RESULT, rl_str2);
					row = ui_show_text(row, 0, tmp);
			}
		}
		else if(i>=6&&i<=7)
			{
				if(i==6)
				{
					switch(test_result_phone[i].value)
						{
							case RL_NA:
								//ui_set_color(CL_WHITE);
								rl_str = TEXT_NA;
								break;
							case RL_FAIL:
								//ui_set_color(CL_RED);
								rl_str = TEXT_FAIL;
								break;
							case RL_PASS:
								//ui_set_color(CL_GREEN);
								rl_str = TEXT_PASS;
								break;
							default:
								//ui_set_color(CL_WHITE);
								rl_str = TEXT_NA;
								break;
						}
				}
				if(i==7)
					{
						switch(test_result_phone[i].value)
							{
								case RL_NA:
								//	ui_set_color(CL_WHITE);
									rl_str1 = TEXT_NA;
									break;
								case RL_FAIL:
								//	ui_set_color(CL_RED);
									rl_str1 = TEXT_FAIL;
									break;
								case RL_PASS:
								//	ui_set_color(CL_GREEN);
									rl_str1 = TEXT_PASS;
									break;
								case RL_NS:
								//	ui_set_color(CL_GREEN);
									rl_str1 = TEXT_NS;
									break;
								default:
								//	ui_set_color(CL_WHITE);
									rl_str1 = TEXT_NA;
									break;
							}
					if((strcmp(rl_str1, ptrfail) == 0)||(strcmp(rl_str, ptrfail) == 0))
						{
							ui_set_color(CL_RED);
							rl_str2=TEXT_FAIL;
						}
					else if((strcmp(rl_str1, ptrna) == 0)||(strcmp(rl_str, ptrna) == 0))
						{
							ui_set_color(CL_WHITE);
							rl_str2=TEXT_NA;
						}
					else
						{
							ui_set_color(CL_GREEN);
							rl_str2=TEXT_PASS;
						}
					memset(tmp, 0, sizeof(tmp));
					sprintf(tmp, "%s:C:%sF:%s",MENU_TEST_BCAMERA_RESULT, rl_str,rl_str1);
					row = ui_show_text(row, 0, tmp);
			}
		}
		else if(i>=8&&i<=9)
			{
				if(i==8)
				{
					switch(test_result_phone[i].value)
						{
							case RL_NA:
								//ui_set_color(CL_WHITE);
								rl_str = TEXT_NA;
								break;
							case RL_FAIL:
								//ui_set_color(CL_RED);
								rl_str = TEXT_FAIL;
								break;
							case RL_PASS:
								//ui_set_color(CL_GREEN);
								rl_str = TEXT_PASS;
								break;
							default:
								//ui_set_color(CL_WHITE);
								rl_str = TEXT_NA;
								break;
						}
				}
				if(i==9)
					{
						switch(test_result_phone[i].value)
							{
								case RL_NA:
									//ui_set_color(CL_WHITE);
									rl_str1 = TEXT_NA;
									break;
								case RL_FAIL:
									//ui_set_color(CL_RED);
									rl_str1 = TEXT_FAIL;
									break;
								case RL_PASS:
									//ui_set_color(CL_GREEN);
									rl_str1 = TEXT_PASS;
									break;
								default:
									//ui_set_color(CL_WHITE);
									rl_str1 = TEXT_NA;
									break;
							}
					if((strcmp(rl_str1, ptrfail) == 0)||(strcmp(rl_str, ptrfail) == 0))
						ui_set_color(CL_RED);
					else if((strcmp(rl_str1, ptrna) == 0)||(strcmp(rl_str, ptrna) == 0))
						ui_set_color(CL_WHITE);
					else
						ui_set_color(CL_GREEN);
					memset(tmp, 0, sizeof(tmp));
					sprintf(tmp, "%s: M:%s A:%s",MENU_TEST_PHONE_LOOPRESULT, rl_str,rl_str1);
					row = ui_show_text(row, 0, tmp);
			}
		}


		else if(i>=10&&i<=11)
			{ 
				if(i==10)
				{
					switch(test_result_phone[i].value)
						{
							case RL_NA:
								//ui_set_color(CL_WHITE);
								rl_str = TEXT_NA;
								break;
							case RL_FAIL:
								//ui_set_color(CL_RED);
								rl_str = TEXT_FAIL;
								break;
							case RL_PASS:
								//ui_set_color(CL_GREEN);
								rl_str = TEXT_PASS;
								break;
							default:
								//ui_set_color(CL_WHITE);
								rl_str = TEXT_NA;
								break;
						}
				}
				if(i==11)
					{
						switch(test_result_phone[i].value)
							{
								case RL_NA:
									//ui_set_color(CL_WHITE);
									rl_str1 = TEXT_NA;
									break;
								case RL_FAIL:
									//ui_set_color(CL_RED);
									rl_str1 = TEXT_FAIL;
									break;
								case RL_PASS:
									//ui_set_color(CL_GREEN);
									rl_str1 = TEXT_PASS;
									break;
								default:
									//ui_set_color(CL_WHITE);
									rl_str1 = TEXT_NA;
									break;
							}
					if((strcmp(rl_str1, ptrfail) == 0)||(strcmp(rl_str, ptrfail) == 0))
						ui_set_color(CL_RED);
					else if((strcmp(rl_str1, ptrna) == 0)||(strcmp(rl_str, ptrna) == 0))
						ui_set_color(CL_WHITE);
					else
						ui_set_color(CL_GREEN);
					memset(tmp, 0, sizeof(tmp));
					sprintf(tmp, "%s: R:%s S:%s",MENU_TEST_VOICE_RESULT, rl_str,rl_str1);
					row = ui_show_text(row, 0, tmp);
			}
		}

		else if(i>=17&&i<=18)
			{
				if(i==17)
				{
					switch(test_result_phone[i].value)
						{
							case RL_NA:
								//ui_set_color(CL_WHITE);
								rl_str = TEXT_NA;
								break;
							case RL_FAIL:
								//ui_set_color(CL_RED);
								rl_str = TEXT_FAIL;
								break;
							case RL_PASS:
								//ui_set_color(CL_GREEN);
								rl_str = TEXT_PASS;
								break;
							default:
								//ui_set_color(CL_WHITE);
								rl_str = TEXT_NA;
								break;
						}
				}
				if(i==18)
					{
						switch(test_result_phone[i].value)
							{
								case RL_NA:
									//ui_set_color(CL_WHITE);
									rl_str1 = TEXT_NA;
									break;
								case RL_FAIL:
									//ui_set_color(CL_RED);
									rl_str1 = TEXT_FAIL;
									break;
								case RL_PASS:
									//ui_set_color(CL_GREEN);
									rl_str1 = TEXT_PASS;
									break;
								default:
									//ui_set_color(CL_WHITE);
									rl_str1 = TEXT_NA;
									break;
							}



					if((strcmp(rl_str, ptrfail) == 0)||(strcmp(rl_str1, ptrfail) == 0))
						ui_set_color(CL_RED);
					else if((strcmp(rl_str, ptrna) == 0)||(strcmp(rl_str1, ptrna) == 0))
						ui_set_color(CL_WHITE);
					else
						ui_set_color(CL_GREEN);
					 memset(tmp, 0, sizeof(tmp));
					 sprintf(tmp, "%s:G:%sL:%s",MENU_TEST_SENSOR_RESULT, rl_str,rl_str1);
					 row = ui_show_text(row, 0, tmp);
			}

		}

		else{
		switch(test_result_phone[i].value) {
			case RL_NA:
				ui_set_color(CL_WHITE);
				rl_str = TEXT_NA;
				break;
			case RL_FAIL:
				ui_set_color(CL_RED);
				rl_str = TEXT_FAIL;
				break;
			case RL_PASS:
				ui_set_color(CL_GREEN);
				rl_str = TEXT_PASS;
				break;
			case RL_NS:
				ui_set_color(CL_BLUE);
				rl_str = TEXT_NS;
				break;
			default:
				ui_set_color(CL_WHITE);
				rl_str = TEXT_NA;
				break;
		}
		memset(tmp, 0, sizeof(tmp));
		sprintf(tmp, "%s:%s", (pmenu[i].title+2), rl_str);
		row = ui_show_text(row, 0, tmp);
	}
}
	gr_flip();
	ui_handle_button(NULL,NULL);
	return 0;
}



static int show_pcba_test_result(void)
{
	int row = 2;
	int i = 0;
	char tmp[128];
	char* rl_str;
	char* rl_str1;
	char* rl_str2;
	//char* ptrpass=TEXT_PASS;
	char* ptrfail=TEXT_FAIL;
	char* ptrna=TEXT_NA;
	menu_info* pmenu = menu_auto_test;
	ui_fill_locked();
	ui_show_title(TEST_REPORT);
	for(i = 0; i < K_MENU_AUTO_TEST_CNT; i++){
		LOGD("mmitest <%d>-%s,%d\n", i, test_result_pcba[i].name, test_result_pcba[i].value);


		if(i>=2&&i<=3)
			{
				if(i==2)
				{
					switch(test_result_pcba[i].value)
						{
							case RL_NA:
								//ui_set_color(CL_WHITE);
								rl_str = TEXT_NA;
								break;
							case RL_FAIL:
								//ui_set_color(CL_RED);
								rl_str = TEXT_FAIL;
								break;
							case RL_PASS:
								//ui_set_color(CL_GREEN);
								rl_str = TEXT_PASS;
								break;
							default:
								//ui_set_color(CL_WHITE);
								rl_str = TEXT_NA;
								break;
						}
				}
				if(i==3)
					{
						switch(test_result_pcba[i].value)
							{
								case RL_NA:
									//ui_set_color(CL_WHITE);
									rl_str1 = TEXT_NA;
									break;
								case RL_FAIL:
									//ui_set_color(CL_RED);
									rl_str1 = TEXT_FAIL;
									break;
								case RL_PASS:
									//ui_set_color(CL_GREEN);
									rl_str1 = TEXT_PASS;
									break;
								default:
									//ui_set_color(CL_WHITE);
									rl_str1 = TEXT_NA;
									break;
							}
					if((strcmp(rl_str1, ptrfail) == 0)||(strcmp(rl_str, ptrfail) == 0))
						{
							rl_str2=TEXT_FAIL;
							ui_set_color(CL_RED);
							LOGD("mmitest %s\n",rl_str2);
						}
					else if((strcmp(rl_str1, ptrna) == 0)||(strcmp(rl_str, ptrna) == 0))
						{
							rl_str2=TEXT_NA;
							ui_set_color(CL_WHITE);
							LOGD("mmitest %s\n",rl_str2);
						}
					else
						{
							rl_str2=TEXT_PASS;
							ui_set_color(CL_GREEN);
							LOGD("mmitest %s\n",rl_str2);
						}
					//LOGD("mmitest%s\n",rl_str2);
					memset(tmp, 0, sizeof(tmp));
					sprintf(tmp, "%s:%s",MENU_TEST_VB_BL_RESULT, rl_str2);
					row = ui_show_text(row, 0, tmp);
			}
		}
		else if(i>=6&&i<=7)
			{
				if(i==6)
				{
					switch(test_result_pcba[i].value)
						{
							case RL_NA:
								//ui_set_color(CL_WHITE);
								rl_str = TEXT_NA;
								break;
							case RL_FAIL:
								//ui_set_color(CL_RED);
								rl_str = TEXT_FAIL;
								break;
							case RL_PASS:
								//ui_set_color(CL_GREEN);
								rl_str = TEXT_PASS;
								break;
							default:
								//ui_set_color(CL_WHITE);
								rl_str = TEXT_NA;
								break;
						}
				}
				if(i==7)
					{
						switch(test_result_pcba[i].value)
							{
								case RL_NA:
								//	ui_set_color(CL_WHITE);
									rl_str1 = TEXT_NA;
									break;
								case RL_FAIL:
								//	ui_set_color(CL_RED);
									rl_str1 = TEXT_FAIL;
									break;
								case RL_PASS:
								//	ui_set_color(CL_GREEN);
									rl_str1 = TEXT_PASS;
									break;
								case RL_NS:
								//	ui_set_color(CL_GREEN);
									rl_str1 = TEXT_NS;
									break;
								default:
								//	ui_set_color(CL_WHITE);
									rl_str1 = TEXT_NA;
									break;
							}
					if((strcmp(rl_str1, ptrfail) == 0)||(strcmp(rl_str, ptrfail) == 0))
						{
							ui_set_color(CL_RED);
							rl_str2=TEXT_FAIL;
						}
					else if((strcmp(rl_str1, ptrna) == 0)||(strcmp(rl_str, ptrna) == 0))
						{
							ui_set_color(CL_WHITE);
							rl_str2=TEXT_NA;
						}
					else
						{
							ui_set_color(CL_GREEN);
							rl_str2=TEXT_PASS;
						}
					memset(tmp, 0, sizeof(tmp));
					sprintf(tmp, "%s:C:%sF:%s",MENU_TEST_BCAMERA_RESULT, rl_str,rl_str1);
					row = ui_show_text(row, 0, tmp);
			}
		}
		else if(i>=8&&i<=9)
			{
				if(i==8)
				{
					switch(test_result_pcba[i].value)
						{
							case RL_NA:
								//ui_set_color(CL_WHITE);
								rl_str = TEXT_NA;
								break;
							case RL_FAIL:
								//ui_set_color(CL_RED);
								rl_str = TEXT_FAIL;
								break;
							case RL_PASS:
								//ui_set_color(CL_GREEN);
								rl_str = TEXT_PASS;
								break;
							default:
								//ui_set_color(CL_WHITE);
								rl_str = TEXT_NA;
								break;
						}
				}
				if(i==9)
					{
						switch(test_result_pcba[i].value)
							{
								case RL_NA:
									//ui_set_color(CL_WHITE);
									rl_str1 = TEXT_NA;
									break;
								case RL_FAIL:
									//ui_set_color(CL_RED);
									rl_str1 = TEXT_FAIL;
									break;
								case RL_PASS:
									//ui_set_color(CL_GREEN);
									rl_str1 = TEXT_PASS;
									break;
								default:
									//ui_set_color(CL_WHITE);
									rl_str1 = TEXT_NA;
									break;
							}
					if((strcmp(rl_str1, ptrfail) == 0)||(strcmp(rl_str, ptrfail) == 0))
						ui_set_color(CL_RED);
					else if((strcmp(rl_str1, ptrna) == 0)||(strcmp(rl_str, ptrna) == 0))
						ui_set_color(CL_WHITE);
					else
						ui_set_color(CL_GREEN);
					memset(tmp, 0, sizeof(tmp));
					sprintf(tmp, "%s: M:%s A:%s",MENU_TEST_PHONE_LOOPRESULT, rl_str,rl_str1);
					row = ui_show_text(row, 0, tmp);
			}
		}


		else if(i>=10&&i<=11)
			{
				if(i==10)
				{
					switch(test_result_pcba[i].value)
						{
							case RL_NA:
								//ui_set_color(CL_WHITE);
								rl_str = TEXT_NA;
								break;
							case RL_FAIL:
								//ui_set_color(CL_RED);
								rl_str = TEXT_FAIL;
								break;
							case RL_PASS:
								//ui_set_color(CL_GREEN);
								rl_str = TEXT_PASS;
								break;
							default:
								//ui_set_color(CL_WHITE);
								rl_str = TEXT_NA;
								break;
						}
				}
				if(i==11)
					{
						switch(test_result_pcba[i].value)
							{
								case RL_NA:
									//ui_set_color(CL_WHITE);
									rl_str1 = TEXT_NA;
									break;
								case RL_FAIL:
									//ui_set_color(CL_RED);
									rl_str1 = TEXT_FAIL;
									break;
								case RL_PASS:
									//ui_set_color(CL_GREEN);
									rl_str1 = TEXT_PASS;
									break;
								default:
									//ui_set_color(CL_WHITE);
									rl_str1 = TEXT_NA;
									break;
							}
					if((strcmp(rl_str1, ptrfail) == 0)||(strcmp(rl_str, ptrfail) == 0))
						ui_set_color(CL_RED);
					else if((strcmp(rl_str1, ptrna) == 0)||(strcmp(rl_str, ptrna) == 0))
						ui_set_color(CL_WHITE);
					else
						ui_set_color(CL_GREEN);
					memset(tmp, 0, sizeof(tmp));
					sprintf(tmp, "%s: R:%s S:%s",MENU_TEST_VOICE_RESULT, rl_str,rl_str1);
					row = ui_show_text(row, 0, tmp);
			}
		}

		else if(i>=17&&i<=18)
			{
				if(i==17)
				{
					switch(test_result_pcba[i].value)
						{
							case RL_NA:
								//ui_set_color(CL_WHITE);
								rl_str = TEXT_NA;
								break;
							case RL_FAIL:
								//ui_set_color(CL_RED);
								rl_str = TEXT_FAIL;
								break;
							case RL_PASS:
								//ui_set_color(CL_GREEN);
								rl_str = TEXT_PASS;
								break;
							default:
								//ui_set_color(CL_WHITE);
								rl_str = TEXT_NA;
								break;
						}
				}
				if(i==18)
					{
						switch(test_result_pcba[i].value)
							{
								case RL_NA:
									//ui_set_color(CL_WHITE);
									rl_str1 = TEXT_NA;
									break;
								case RL_FAIL:
									//ui_set_color(CL_RED);
									rl_str1 = TEXT_FAIL;
									break;
								case RL_PASS:
									//ui_set_color(CL_GREEN);
									rl_str1 = TEXT_PASS;
									break;
								default:
									//ui_set_color(CL_WHITE);
									rl_str1 = TEXT_NA;
									break;
							}


					if((strcmp(rl_str, ptrfail) == 0)||(strcmp(rl_str1, ptrfail) == 0))
						ui_set_color(CL_RED);
					else if((strcmp(rl_str, ptrna) == 0)||(strcmp(rl_str1, ptrna) == 0))
						ui_set_color(CL_WHITE);
					else
						ui_set_color(CL_GREEN);
					 memset(tmp, 0, sizeof(tmp));
					 sprintf(tmp, "%s:G:%sL:%s",MENU_TEST_SENSOR_RESULT, rl_str,rl_str1);
					 row = ui_show_text(row, 0, tmp);
			}

		}

		else{
		switch(test_result_pcba[i].value) {
			case RL_NA:
				ui_set_color(CL_WHITE);
				rl_str = TEXT_NA;
				break;
			case RL_FAIL:
				ui_set_color(CL_RED);
				rl_str = TEXT_FAIL;
				break;
			case RL_PASS:
				ui_set_color(CL_GREEN);
				rl_str = TEXT_PASS;
				break;
			case RL_NS:
				ui_set_color(CL_BLUE);
				rl_str = TEXT_NS;
				break;
			default:
				ui_set_color(CL_WHITE);
				rl_str = TEXT_NA;
				break;
		}
		memset(tmp, 0, sizeof(tmp));
		sprintf(tmp, "%s:%s", (pmenu[i].title+2), rl_str);
		row = ui_show_text(row, 0, tmp);
	}
}
	gr_flip();
	ui_handle_button(NULL,NULL);
	return 0;
}



static int phone_shutdown(void)
{
	sync();
	reboot(LINUX_REBOOT_CMD_POWER_OFF);
	return 0;
}

void eng_bt_wifi_start(void)
{
    LOGD("==== eng_bt_wifi_start ====\n");
	eng_wifi_scan_start();
	eng_bt_scan_start();
    LOGD("==== eng_bt_wifi_end ====\n");
}

int test_bt_wifi_init(void)
{
    pthread_create(&bt_wifi_init_thread, NULL, (void *)eng_bt_wifi_start, NULL);
    return 0;
}

void test_gps_open(void)
{
        int ret;
        ret = gpsOpen();
        if( ret < 0)
        {
                LOGD("%s gps open error = %d \n", __FUNCTION__,ret);
        }
        LOGD("%s gps open success \n", __FUNCTION__);
}
int test_gps_init(void)
{
	pthread_create(&gps_init_thread, NULL, (void *)test_gps_open, NULL);
	return 0;
}

int test_tel_init(void)
{
	int fd;
	fd=open(TEL_DEVICE_PATH,O_RDWR);

	if(fd<0)
	{
		LOGD("mmitest tel test is faild");
		return RL_FAIL;
	}

	tel_send_at(fd,"AT+SFUN=2",NULL,0, 0);

    tel_send_at(fd,"AT+SFUN=4",NULL,0, 0);

	return 0;
}

static void test_item_init(void)
{
	test_modem_init();//SIM
	test_bt_wifi_init();//BT WIFI
	test_gps_init();//GPS
	//test_tel_init();//telephone
}

//unsigned char test_buff[]="hello world";
static int test_result_mkdir(void)
{
	system("mkdir /productinfo/");
	system("touch  /productinfo/wholephonetest.txt");
	system("touch  /productinfo/PCBAtest.txt");
	return 0;
}




void sdcard_fm_init(void)
{   int empty;

	empty=check_file_exit();

	LOGD("mmitest empty=%d\n",empty);
	if(empty==0)
		sdcard_fm_state=sdcard_write_fm(&fm_freq);
	else
		sdcard_fm_state=0;
}


int main(int argc, char **argv)
{

	//while(1)  {sleep(5);LOGD("sleep Zzz\n");}
	LOGD("==== factory test start ====\n");//}
	char *pp;

	system(SPRD_TS_MODULE);
	test_init();
	test_item_init();
	test_result_mkdir();//+++++++++++++++
	sdcard_fm_init();
	show_root_menu();

	//sleep(10);//delay for start phone

	return 1;
}



