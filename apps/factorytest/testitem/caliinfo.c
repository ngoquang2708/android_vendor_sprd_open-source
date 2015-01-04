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



int test_cali_info(void)
{
	int ret = 0;
	int row = 2;
	char tmp[512];
	char tmp2[512];
	char* pcur;
	char* pos1;
	char* pos2;
	int len;
	int testlen;
	ui_fill_locked();
	ui_show_title(MENU_CALI_INFO);
	pcur = test_modem_get_caliinfo();
	len = strlen(pcur);
    //LOGD("mmitest %s\n",pcur);
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
			memcpy(tmp, pos1, (pos2-pos1));
			tmp[pos2-pos1] = '\0';
			len -= (pos2 - pcur);
			pcur = pos2;
		}
		testlen=str_replace(tmp2,tmp,"calibrated","cali");
		//LOGD("mmitest<%s> %d", tmp2,testlen);
		row = ui_show_text(row, 0, tmp2);
	}
	gr_flip();
	//ui_wait_key(NULL);
	LOGD("mmitest before button");
	ret=ui_handle_button(NULL,NULL);
	return ret;
}
