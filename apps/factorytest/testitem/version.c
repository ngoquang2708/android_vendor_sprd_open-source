#include "testitem.h"
#include <stdio.h>


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

	// get kernel version
	fd = open(PATH_LINUX_VER, O_RDONLY);
	if(fd < 0){
		LOGD("open %s fail!", PATH_LINUX_VER);
	} else {
		memset(kernelver, 0, sizeof(kernelver));
		read(fd, kernelver, sizeof(kernelver));
		cur_row = ui_show_text(cur_row+1, 0, kernelver);
	}


	modemver = test_modem_get_ver();
	cur_row = ui_show_text(cur_row+1, 0, modemver);

	gr_flip();

	while(ui_handle_button(NULL,NULL)!=RL_FAIL);

	return ret;
}

char phrase[300];
unsigned char phrash_valid=0;

static int checkPhaseCheck(void)
{
	if ((phrase[0] == '9' || phrase[0] == '5')
                && phrase[1] == '0'
                && phrase[2] == 'P'
                && phrase[3] == 'S')
    {
			phrash_valid=1;
			return 1;
    }

		LOGD("mmitest out of checkPhaseCheck");
		phrash_valid=0;
        return 0;	
}


int  isAscii(unsigned char b) 
{
    if (b >= 0 && b <= 127)
    {
        return 1;
    }
    return 0;
}



 static char * getSn1(void) {
		char string[32];
		if ( phrash_valid== 0) {
            strcpy(string,TEXT_INVALIDSN1);
			LOGD("mmitest out of sn11");
			return string;
        }
        if (!isAscii(phrase[SN1_START_INDEX])) {
            strcpy(string,TEXT_INVALIDSN1);
			LOGD("mmitest out of sn12");
			return string;
        }

        memcpy(string, phrase+SN1_START_INDEX, SP09_MAX_SN_LEN);
		LOGD("mmitest out of sn13");
        return string;
    }

static char * getSn2(void) {
		char string[32];
	   if (phrash_valid == 0) {
		   strcpy(string,TEXT_INVALIDSN2);
		   LOGD("mmitest out of sn21");
		   return string;
	   }
	   if (!isAscii(phrase[SN2_START_INDEX])) {
		   strcpy(string,TEXT_INVALIDSN2);
		   LOGD("mmitest out of sn22");
		   return string;
	   }

	   memcpy(string, phrase+SN2_START_INDEX, SP09_MAX_SN_LEN);
	   LOGD("mmitest out of sn23");
	   return string;
   }


static int isStationTest(int station) {
        unsigned char flag = 1;
        if (station < 8) {
            return (0 == ((flag << station) & phrase[TESTFLAG_START_INDEX]));
        } else if (station >= 8 && station < 16) {
            return (0 == ((flag << (station - 8)) & phrase[TESTFLAG_START_INDEX + 1]));
        }
        return 0;
    }

 static int  isStationPass(int station) {
        unsigned char flag = 1;
        if (station < 8) {
            return (0 == ((flag << station) & phrase[RESULT_START_INDEX]));
        } else if (station >= 8 && station < 16) {
            return (0 == ((flag << (station - 8)) & phrase[RESULT_START_INDEX + 1]));
        }
        return 0;
   }


static void getTestsAndResult(void ) {

		int i;
		int flag = 1;
		char testResult[64];
		char testname[32];


		if (phrash_valid == 0) {
            strcpy(testResult,TEXT_PHASE_NOTTEST);
			LOGD("mmitest out of Result1");
			ui_show_text(15, 0, testResult);
			return;
        }

        if (!isAscii(phrase[STATION_START_INDEX])) {
            strcpy(testResult,TEXT_PHASE_NOTTEST);
			LOGD("mmitest out of Result2");
			ui_show_text(15, 0, testResult);
			return;
        }

        for (i = 0; i < SP09_MAX_STATION_NUM; i++) {
            if (0 == phrase[STATION_START_INDEX + i * SP09_MAX_STATION_NAME_LEN]) {
                break;
            }
			memset(testname,0,sizeof(testname));
			memset(testResult,0,sizeof(testResult));
            memcpy(testname, phrase+STATION_START_INDEX + i * SP09_MAX_STATION_NAME_LEN,
                    SP09_MAX_STATION_NAME_LEN);
            if (!isStationTest(i)) {
                sprintf(testResult,"%s %s\n",testname,TEXT_PHASE_NOTTEST);
            } else if (isStationPass(i)) {
                sprintf(testResult,"%s %s\n",testname,TEXT_PHASE_PASS);
            } else {
                sprintf(testResult,"%s %s\n",testname,TEXT_PHASE_FAILED);
            }
            flag = flag << 1;
			ui_show_text(15+i, 0, testResult);
        }
		return;
    }





extern char imei_buf1[128];
extern char imei_buf2[128];

extern char wifi_addre[64];


int test_phone_info_show(void)
{
	char info[256];
	char bt_addr[18];
	char *sn1;
	char *sn2;
	char *phase_check;
	char *phase_result;
	FILE *fd;
	int i;

	ui_fill_locked();

	ui_show_title(MENU_PHONE_INFO_TEST);
	ui_set_color(CL_WHITE);
	memset(info, 0, sizeof(info));
	memset(bt_addr, 0, sizeof(bt_addr));
	memset(phrase, 0, sizeof(phrase));

	if(read_local_bt_mac(BT_MAC_FILE_PATH,bt_addr)== 1) {
        LOGD("Local MAC address:%s",bt_addr);
    } else {
        create_mac_rand((char*)bt_addr);
        LOGD("Randomly generated MAC address:%s",bt_addr);
    }

	//property_get("ro.serialno",snsn,"");
	//LOGD("mmitest snsn=%s\n",snsn);




	fd=fopen("/dev/block/platform/sprd-sdhci.3/by-name/miscdata","r");
	fread(phrase,sizeof(char),sizeof(phrase),fd);
	fclose(fd);


	if(checkPhaseCheck()==1)
		phase_check=TEXT_VALID;
	else
		phase_check=TEXT_INVALID;

	LOGD("mmitest phase_check=%s",phase_check);
	sn1=getSn1();
	LOGD("mmitest sn1=%s",sn1);
	sn2=getSn2();
	LOGD("mmitest sn2=%s",sn2);
	if(strlen(sn2)<10)
	sn2=TEXT_INVALIDSN2;

	ui_set_color(CL_WHITE);
	ui_show_text(2, 0, TEXT_BT_MAC);
	ui_set_color(CL_GREEN);
	ui_show_text(3, 0, bt_addr);

	ui_set_color(CL_WHITE);
	ui_show_text(4, 0, TEXT_SN);
	ui_set_color(CL_GREEN);
	ui_show_text(5, 0, sn1);
	ui_show_text(6, 0, sn2);

	ui_set_color(CL_WHITE);
	ui_show_text(7, 0, TEXT_IMEI);
	ui_set_color(CL_GREEN);
	ui_show_text(8, 0, imei_buf1);
	ui_show_text(9, 0, imei_buf2);

	ui_set_color(CL_WHITE);
	ui_show_text(10, 0, TEXT_WIFI_ADDR);
	ui_set_color(CL_GREEN);
	ui_show_text(11, 0, wifi_addre);

	ui_set_color(CL_WHITE);
	ui_show_text(12, 0, TEXT_PHASE_CHECK);
	ui_set_color(CL_GREEN);
	ui_show_text(13, 0, phase_check);

	ui_set_color(CL_WHITE);
	ui_show_text(14, 0, TEXT_PHASE_CHECK_RESULT);
	ui_set_color(CL_GREEN);
	getTestsAndResult();

    gr_flip();


	while(ui_handle_button(NULL,NULL)!=RL_FAIL);

	return 0;
}

