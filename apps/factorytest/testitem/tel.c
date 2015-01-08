#include "testitem.h"


int test_tel_start(void)
{
	int cur_row=2;
	int ret,fd;
	int pos;
    char modem_call1[256];
	char modem_call2[256];
	static int onetime=0;

	ui_fill_locked();
	ui_show_title(MENU_TEST_TEL);
	ui_set_color(CL_GREEN);
	cur_row = ui_show_text(cur_row, 0, TEL_TEST_START);
	cur_row = ui_show_text(cur_row, 0, TEL_TEST_TIPS);
	gr_flip();

	fd=open(TEL_DEVICE_PATH,O_RDWR);
    LOGD("mmitest tel test %s",TEL_DEVICE_PATH);
	if(fd<0)
	{
		LOGD("mmitest tel test is faild");
		save_result(CASE_TEST_TEL,RL_FAIL);
		return RL_FAIL;
	}

	if(onetime==0)
		{
			onetime=1;

			tel_send_at(fd,"ATH",NULL,0, 0);
		}

	tel_send_at(fd,"AT+SFUN=2",NULL,0, 0);
    tel_send_at(fd,"AT+SFUN=4",NULL,0, 0);
    pos=tel_send_at(fd, "ATD112;", NULL,NULL, 0);
	cur_row = ui_show_text(cur_row, 0, TEL_DIAL_OVER);

	gr_flip();

	ret = ui_handle_button(NULL, NULL);
	tel_send_at(fd,"ATH",NULL,0, 0);
	close(fd);

	save_result(CASE_TEST_TEL,ret);
	return ret;
}
