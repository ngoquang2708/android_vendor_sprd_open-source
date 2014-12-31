#include "testitem.h"



int test_version_show(void)
{
	char androidver[64];
	char tmpbuf[64];
	char sprdver[256];
	char kernelver[256];
	char* modemver;
	int fd;
	int cur_row = 3;
	int ret=0;

	ui_fill_locked();

	ui_show_title(MENU_TITLE_VERSION);
	ui_set_color(CL_WHITE);

	//get android version
	memset(androidver, 0, sizeof(androidver));
	memset(tmpbuf, 0, sizeof(tmpbuf));
	property_get(PROP_ANDROID_VER, tmpbuf, "");
	sprintf(androidver, "Android %s",tmpbuf);
	cur_row = ui_show_text(cur_row, 0, androidver);

	//get sprd version
	//memset(sprdver, 0, sizeof(sprdver));
	//property_get(PROP_SPRD_VER, sprdver, "");

	//cur_row = ui_show_text(cur_row+1, 0, sprdver);

	// get kernel version
	fd = open(PATH_LINUX_VER, O_RDONLY);
	if(fd < 0){
		LOGD("open %s fail!", PATH_LINUX_VER);
	} else {
		memset(kernelver, 0, sizeof(kernelver));		
		read(fd, kernelver, sizeof(kernelver));
		cur_row = ui_show_text(cur_row+1, 0, kernelver);
	}

	// get modem version
	modemver = test_modem_get_ver();
	cur_row = ui_show_text(cur_row+1, 0, modemver);

	//update
	gr_flip();

	//eng_draw_handle_softkey(0);
	ret=ui_handle_button(NULL,NULL);
	//ui_wait_key(NULL);

	return ret;
}
