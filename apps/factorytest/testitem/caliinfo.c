#include "testitem.h"
#include <stdlib.h>
#include <ctype.h>
#include <string.h>


extern char* test_modem_get_caliinfo(void);





/********************************************************************
*  Function: my_strstr()
*********************************************************************/
char * my_strstr(char * ps,char *pd)
{
    char *pt = pd;
    int c = 0;
    while(*ps != '\0')
    {
        if(*ps == *pd)
        {
            while(*ps == *pd && *pd!='\0')
            {
                ps++;
                pd++;
                c++;
            }
        }else
        {
            ps++;
        }
        if(*pd == '\0')
        {
            return (ps - c);
        }
        c = 0;
        pd = pt;
    }
    return 0;
}


/********************************************************************
*  Function:str_replace()
*********************************************************************/
int str_replace(char *p_result,char* p_source,char* p_seach,char *p_repstr)
{
    int c = 0;
    int repstr_leng = 0;
    int searchstr_leng = 0;
    char *p1;
    char *presult = p_result;
    char *psource = p_source;
    char *prep = p_repstr;
    char *pseach = p_seach;
    int nLen = 0;
    repstr_leng = strlen(prep);
    searchstr_leng = strlen(pseach);
    do{
        p1 = my_strstr(psource,p_seach);
        if (p1 == 0)
        {
            strcpy(presult,psource);
            return c;
        }
        c++;
        nLen = p1 - psource;
        memcpy(presult, psource, nLen);
        memcpy(presult + nLen,p_repstr,repstr_leng);
        psource = p1 + searchstr_leng;
        presult = presult + nLen + repstr_leng;
    }while(p1);
    return c;
}


extern int text_rows;
int test_cali_info(void)
{
	int ret = 0;
	int i;
	int row = 2;
	char tmp[512][23];
	char tmp2[512][23];
	char* pcur;
	char* pos1;
	char* pos2;
	int len;
	int testlen;
	int row_num=0;
	int cali_size=0;
	unsigned char chang_page=0;
	unsigned char change=1;
	ui_fill_locked();
	ui_show_title(MENU_CALI_INFO);
	pcur = test_modem_get_caliinfo();
	len = strlen(pcur);
    cali_size=sizeof(tmp[0])/sizeof(tmp[0][0]);
	ui_set_color(CL_WHITE);
	while(len > 0) {
		pos1 = strchr(pcur, ':');
		if(pos1 == NULL) break;
		pos1++;
		pos2 = strstr(pos1, "BIT");
		if(pos2 == NULL) {
			strcpy(tmp, pos1);
			len = 0;
		} else {
			memcpy(tmp[row_num], pos1, (pos2-pos1));
			tmp[pos2-pos1][row_num] = '\0';
			len -= (pos2 - pcur);
			pcur = pos2;
		}
		testlen=str_replace(tmp2[row_num],tmp[row_num],"calibrated","cali");
		row_num++;
		//row = ui_show_text(row, 0, tmp2);
	}

	if(cali_size<=(text_rows-2))
	{
		for(i=0;i<cali_size;i++)
			row = ui_show_text(row, 0, tmp2[i]);
			gr_flip();
		while(ui_handle_button(NULL,NULL)!=RL_FAIL);
	}

	else
	{
		do{
			if(chang_page==RL_PASS)
			{
				change=!change;
				chang_page=RL_NA;
				LOGD("show result change=%d",change);
			}
			if(change==1)
			{
				ui_set_color(CL_SCREEN_BG);
	            gr_fill(0, 0, gr_fb_width(), gr_fb_height());
				ui_fill_locked();
				ui_show_title(MENU_CALI_INFO);
				row=2;
				for(i = 0; i < text_rows-2; i++){  
					row = ui_show_text(row, 0, tmp2[i]);
					gr_flip();
				}
			}

			else if(change==0)
			{
			row=2;
			ui_set_color(CL_SCREEN_BG);
        	gr_fill(0, 0, gr_fb_width(), gr_fb_height());
			ui_fill_locked();
			ui_show_title(MENU_CALI_INFO);
			for(i = 0; i < cali_size+2-text_rows; i++){
				row = ui_show_text(row, 0, tmp2[text_rows-2+i]);
				gr_flip();
			}
			}
		}while((chang_page=ui_handle_button(NULL,NULL))!=RL_FAIL);
	}

	LOGD("mmitest before button");
	return 0;
}
