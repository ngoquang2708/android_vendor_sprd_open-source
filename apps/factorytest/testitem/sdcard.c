#include "testitem.h"
#include <sys/vfs.h>


static int sdcard_rw(void)
{
	int fd;
	int ret = -1;
	unsigned char w_buf[RW_LEN];
	unsigned char r_buf[RW_LEN];
	int i = 0;

	for(i = 0; i < RW_LEN; i++) {
		w_buf[i] = 0xff & i;
	}

	fd = open(SPRD_SD_TESTFILE, O_CREAT|O_RDWR, 0666);
	if(fd < 0){
		LOGD("[%s]: create %s fail",__func__, SPRD_SD_TESTFILE);
		goto RW_END;
	}

	if(write(fd, w_buf, RW_LEN) != RW_LEN){
		LOGD("[%s]: write data error",	__FUNCTION__);
		goto RW_END;
	}

	lseek(fd, 0, SEEK_SET);
	memset(r_buf, 0, sizeof(r_buf));

	read(fd, r_buf, RW_LEN);
	if(memcmp(r_buf, w_buf, RW_LEN) != 0) {
		LOGD("[%s]: read data error", __FUNCTION__);
		goto RW_END;
	}

	ret = 0;
RW_END:
	if(fd > 0) close(fd);
	return ret;
}

int test_sdcard_pretest(void)
{
	int fd;
	int ret;
	system(SPRD_MOUNT_DEV);
	fd = open(SPRD_SD_DEV, O_RDWR);
	if(fd < 0) {
		ret= RL_FAIL;
	} else {
		close(fd);
		ret= RL_PASS;
	}

	save_result(CASE_TEST_SDCARD,ret);
	return ret;
}

int check_file_exit(void)
{
	int ret;
	int fd;
	system(SPRD_MOUNT_DEV);
	//open(SPRD_SD_DEV, O_RDWR);
	//fd=open(SPRD_SD_FMTESTFILE, O_CREAT,0666);
	if(access(SPRD_SD_FMTESTFILE,F_OK)!=-1)
		ret=1;
	else
		ret=0;
	close(fd);
	system(SPRD_UNMOUNT_DEV);
	return ret;
}

int sdcard_read_fm(int *rd)
{
	int fd;
	int ret;
	system(SPRD_MOUNT_DEV);
	fd = open(SPRD_SD_DEV, O_RDWR);
	if(fd < 0)
		{
			return -1;
		}
	LOGD("mmitest opensdcard is ok");
	fd = open(SPRD_SD_FMTESTFILE, O_CREAT|O_RDWR,0666);
	if(fd < 0)
		{
				return -1;
		}
	LOGD("mmitest opensdcard file is ok");
	lseek(fd, 0, SEEK_SET);
	ret=read(fd, rd, sizeof(int));
	LOGD("mmitest read file is=%d",*rd);
	close(fd);
	system(SPRD_UNMOUNT_DEV);
	return ret;
}

int sdcard_write_fm(int *freq)
{
	int fd;
	system(SPRD_MOUNT_DEV);
	fd = open(SPRD_SD_DEV, O_RDWR);
	if(fd < 0)
		{
			return -1;
		}
    LOGD("mmitest opensdcard is ok");
	fd = open(SPRD_SD_FMTESTFILE, O_CREAT|O_RDWR, 0666);
	if(fd < 0)
		{
			return -1;
		}
	LOGD("mmitest opensdcard file is ok");
	LOGD("mmitest write size=%d",write(fd, freq, sizeof(int)));
	if(write(fd, freq, sizeof(int)) != sizeof(int))
		{
			return -1;
		}
	LOGD("mmitest write file is ok");
	close(fd);
	system(SPRD_UNMOUNT_DEV);
	return 0;
}


int test_sdcard_start(void)
{
	struct statfs fs;
	int fd;
	int ret = RL_FAIL; //fail
	int cur_row = 2;
	char temp[64];
	ui_fill_locked();
	ui_show_title(MENU_TEST_SDCARD);
	ui_set_color(CL_WHITE);
	cur_row = ui_show_text(cur_row, 0, TEXT_SD_START);
	gr_flip();
	system(SPRD_MOUNT_DEV);
	fd = open(SPRD_SD_DEV, O_RDWR);
	if(fd < 0) {
		ui_set_color(CL_RED);
		cur_row = ui_show_text(cur_row, 0, TEXT_SD_OPEN_FAIL);
		gr_flip();
		goto TEST_END;
	} else {
		ui_set_color(CL_GREEN);
		cur_row = ui_show_text(cur_row, 0, TEXT_SD_OPEN_OK);
		gr_flip();
	}

	if(statfs(SPRD_SDCARD_PATH, &fs) < 0) {
		ui_set_color(CL_RED);
		cur_row = ui_show_text(cur_row, 0, TEXT_SD_STATE_FAIL);
		gr_flip();
		goto TEST_END;
	} else {
		ui_set_color(CL_GREEN);
		cur_row = ui_show_text(cur_row, 0, TEXT_SD_STATE_OK);
		sprintf(temp, "%d MB", (fs.f_blocks>>1));
		cur_row = ui_show_text(cur_row, 0, temp);
		gr_flip();
	}

	if(sdcard_rw() < 0) {
		ui_set_color(CL_RED);
		cur_row = ui_show_text(cur_row, 0, TEXT_SD_RW_FAIL);
		gr_flip();
		goto TEST_END;
	} else {
		ui_set_color(CL_GREEN);
		cur_row = ui_show_text(cur_row, 0, TEXT_SD_RW_OK);
		gr_flip();
	}

	ret = RL_PASS;
TEST_END:
	system(SPRD_UNMOUNT_DEV);
	if(ret == RL_PASS) {
		ui_set_color(CL_GREEN);
		cur_row = ui_show_text(cur_row, 0, TEXT_TEST_PASS);
	} else {
		ui_set_color(CL_RED);
		cur_row = ui_show_text(cur_row, 0, TEXT_TEST_FAIL);
	}
	gr_flip();
	sleep(1);

	save_result(CASE_TEST_SDCARD,ret);
	return ret;
}
