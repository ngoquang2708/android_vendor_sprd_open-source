/*
 * File:         eng_pcclient.c
 * Based on:
 * Author:       Yunlong Wang <Yunlong.Wang@spreadtrum.com>
 *
 * Created:	  2011-03-16
 * Description:  create pc client in android for engneer mode
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <linux/delay.h>
#include <termios.h>
#include <errno.h>
#include <semaphore.h>

#include "engopt.h"
#include "engclient.h"
#include "engparcel.h"
#include "engat.h"
#include "eng_attok.h"
#include "cutils/sockets.h"
#include "cutils/properties.h"

typedef struct eng_fdtype_str{
	int fd;
	int type;
}eng_fdtype_t;

static sem_t                thread_sem_lock;


#define NUM_ELEMS(x) (sizeof(x)/sizeof(x[0]))
static pthread_t tid_dispatch;
static int soc_fd;
static int modem_fd1;
static int modem_fd2;
static int s_readCount = 0;
static char s_ATBuffer[ENG_BUF_LEN];
static char s_ATDictBuf[ENG_BUF_LEN];
static char *s_ATBufferCur = s_ATBuffer;
static int s_started=0;
static int cmd_type;
static char at_cmd_req[ENG_AT_CMD_MAX_LEN];
static eng_at_response response;


/*at command param struct*/
static eng_at_sppsrate at_sppsrate;
static eng_at_sptest    at_sptest;
static eng_at_spfreq    at_spfreq;
static eng_at_spaute    at_spaute;
static eng_at_spdgcnum at_spdgcnum;
static eng_at_spdgcinfo at_spdgcinfo;
static eng_at_grrtdncell at_grrtdncell;
static eng_at_pccotdcell at_pccotdcell;
static eng_at_l1param   at_l1param;
static eng_at_sdataconf at_sdataconf;
static eng_at_trrparam at_trrparam;
static eng_at_rrdmparam at_rrdmparam;
static eng_at_ttrdcfeparam at_ttrdcfeparam;
static eng_at_smtimer at_smtimer;
static eng_at_mbau     at_mbau;
static eng_at_nohandle	at_nohandle;
static eng_at_sysinfo   at_sysinfo;
static eng_at_pdpactive   at_pdpactive;
static eng_at_spgresdata at_spgresdata;
static eng_at_camm at_camm;
static eng_at_cops at_cops;
static eng_at_crsm at_crsm;
static int at_oneint;
static eng_at_sptest    at_cced;
static eng_at_sgmr	      at_sgmr;
/*
 * Returns a pointer to the end of the next line
 * special-cases the "> " SMS prompt
 *
 * returns NULL if there is no complete line
 */
static char * findNextEOL(char *cur)
{
    if (cur[0] == '>' && cur[1] == ' ' && cur[2] == '\0') {
        /* SMS prompt character...not \r terminated */
        return cur+2;
    }

    // Find next newline
    while (*cur != '\0' && *cur != '\r' && *cur != '\n') cur++;

    return *cur == '\0' ? NULL : cur;
}


/** returns 1 if line starts with prefix, 0 if it does not */
static int strStartsWith(const char *line, const char *prefix)
{
    for ( ; *line != '\0' && *prefix != '\0' ; line++, prefix++) {
        if (*line != *prefix) {
            return 0;
        }
    }

    return *prefix == '\0';
}


/**
 * returns 1 if line is a final response indicating error
 * See 27.007 annex B
 * WARNING: NO CARRIER and others are sometimes unsolicited
 */
static const char * s_finalResponsesError[] = {
    "ERROR",
    "+CMS ERROR:",
    "+CME ERROR:",
    "NO CARRIER", /* sometimes! */
    "NO ANSWER",
    "NO DIALTONE",
};
static int isFinalResponseError(const char *line)
{
    size_t i;

    for (i = 0 ; i < NUM_ELEMS(s_finalResponsesError) ; i++) {
        if (strStartsWith(line, s_finalResponsesError[i])) {
            return 1;
        }
    }

    return 0;
}

/**
 * returns 1 if line is a final response indicating success
 * See 27.007 annex B
 * WARNING: NO CARRIER and others are sometimes unsolicited
 */
static const char * s_finalResponsesSuccess[] = {
    "OK",
    "CONNECT",       /* some stacks start up data on another channel */
};

static int isFinalResponseSuccess(const char *line)
{
    size_t i;

    for (i = 0 ; i < NUM_ELEMS(s_finalResponsesSuccess) ; i++) {
        if (strStartsWith(line, s_finalResponsesSuccess[i])) {
            return 1;
        }
    }

    return 0;
}


/**
 * returns 1 if line is a final response, either  error or success
 * See 27.007 annex B
 * WARNING: NO CARRIER and others are sometimes unsolicited
 */
static const char * s_finalResponse[] = {
	"OK",
	"ERROR",
};

static int isFinalResponse(const char *line)
{
    size_t i;

    for (i = 0 ; i < NUM_ELEMS(s_finalResponse) ; i++) {
        if (strStartsWith(line, s_finalResponse[i])) {
            return 1;
        }
    }

    return 0;
}

static const char * s_AllResponse[] = {
    "OK",
    "CONNECT",       /* some stacks start up data on another channel */
    "ERROR",
    "+CMS ERROR:",
    "+CME ERROR:",
    "NO CARRIER", /* sometimes! */
    "NO ANSWER",
    "NO DIALTONE",
    ">",
};

// find the last occurrence of needle in haystack
static char * rstrstr(const char *haystack, const char *needle)
{
    if (*needle == '\0')
        return (char *) haystack;

    char *result = NULL;
    for (;;) {
        char *p = strstr(haystack, needle);
        if (p == NULL)
            break;
        result = p;
        haystack = p + 1;
    }

    return result;
}


//check at response end through at-nohandle method
static int isAtNoHandleEnd(const char *data)
{
    size_t i,ret1, ret2, ret=0;
    size_t length;
    char *ptr;

    length = strlen(data);
    ENG_LOG("%s: data=%s",__func__, data);
    for(i=0;  i<length; i++) {
        ENG_LOG("%s: 0x%x", __func__, data[i]);
    }
    for (i = 0 ; i < NUM_ELEMS(s_AllResponse) ; i++) {
        if ((ptr=rstrstr(data, s_AllResponse[i]))!=NULL) {
            ret1=0;
            ret2=0;
            ENG_LOG("%s: check %s",__func__, s_AllResponse[i]);
            //check start \r\n
            /*ENG_LOG("%s: ptr=0x%x, data=0x%x",__func__, ptr, data);*/
            if((ptr-2) >= data) {
                ENG_LOG("%s: start two bytes[%x][%x]",__func__, *(ptr-2),*(ptr-1));
                if(((*(ptr-2)==0x0a)&&(*(ptr-1)==0x0d))||
                   ((*(ptr-2)==0x0d)&&(*(ptr-1)==0x0a))) {
                    ret1=1;
                }
            }
            //check end \r\n
            ENG_LOG("%s: end two bytes[%x][%x]",__func__, data[length-2],data[length-1]);
            if(((data[length-1]==0x0a)&&(data[length-2]==0x0d))||
               ((data[length-1]==0x0a)&&(data[length-2]==0x20))||
               ((data[length-1]==0x0d)&&(data[length-2]==0x0a))){
                ret2 = 1;
            }
            ENG_LOG("%s: ret1=%d; ret2=%d",__func__, ret1, ret2);
            if(ret1==1 && ret2==1) {
                ret = 1;
                break;
            }
        }
    }
    ENG_LOG("%s: ret=%d",__func__, ret);
    return ret;
}

static int eng_specialAT(int cmd)
{
	int ret = 0;
	
	if (cmd==ENG_AT_SPVER){
		ENG_LOG("%s: cmd %d is specail",__func__, ENG_AT_SPVER);
		ret = 1;
	}

	return ret;
}

static int eng_isLargeInfoAt(int cmd) {
        int ret = 0;
	
	if (cmd == ENG_AT_L1MON || cmd == ENG_AT_GET_ASSERT_MODE || cmd == ENG_AT_SGMR){
		ret = 1;
	}

	return ret;
}
/*******************************************************************************
* Function    :  eng_readline
* Description :  seperate the modem return string into line, referece to rild code
* Parameters  :  none
* Return      :    line string
*******************************************************************************/
static char *eng_readline(int modemfd)
{
    int count;

    char *p_read = NULL;
    char *p_eol = NULL;
    char *ret;

    /* this is a little odd. I use *s_ATBufferCur == 0 to
     * mean "buffer consumed completely". If it points to a character, than
     * the buffer continues until a \0
     */
	// ENG_LOG("eng_readline s_ATBufferCur=%s\n", s_ATBufferCur);
	
    if (*s_ATBufferCur == '\0') {
        /* empty buffer */
        s_ATBufferCur = s_ATBuffer;
        *s_ATBufferCur = '\0';
        p_read = s_ATBuffer;
    } else {   /* *s_ATBufferCur != '\0' */
        /* there's data in the buffer from the last read */

        // skip over leading newlines
        while (*s_ATBufferCur == '\r' || *s_ATBufferCur == '\n')
            s_ATBufferCur++;

        p_eol = findNextEOL(s_ATBufferCur);

        if (p_eol == NULL) {
            /* a partial line. move it up and prepare to read more */
            size_t len;

            len = strlen(s_ATBufferCur);

            memmove(s_ATBuffer, s_ATBufferCur, len + 1);
            p_read = s_ATBuffer + len;
            s_ATBufferCur = s_ATBuffer;
        }
        /* Otherwise, (p_eol !- NULL) there is a complete line  */
        /* that will be returned the while () loop below        */
    }


    while (p_eol == NULL) {
        if (0 == ENG_BUF_LEN - (p_read - s_ATBuffer)) {
            /* ditch buffer and start over again */
            s_ATBufferCur = s_ATBuffer;
            *s_ATBufferCur = '\0';
            p_read = s_ATBuffer;
        }
        do {
            count = read(modemfd, p_read,
                            ENG_BUF_LEN - (p_read - s_ATBuffer));
	   	  ENG_LOG("eng_readline[%d]: %s\n", count, p_read);
        } while (count < 0 && errno == EINTR);
	 
        if (count > 0) {
            p_read[count] = '\0';
            // skip over leading newlines
            while (*s_ATBufferCur == '\r' || *s_ATBufferCur == '\n')
                s_ATBufferCur++;

            p_eol = findNextEOL(s_ATBufferCur);
            p_read += count;
        } else if (count <= 0) {
            return NULL;
        }
    }

    /* a full line in the buffer. Place a \0 over the \r and return */
    ret = s_ATBufferCur;
    *p_eol = '\0';
    s_ATBufferCur = p_eol + 1; /* this will always be <= p_read,    */
                              /* and there will be a \0 at *p_read */
    return ret;
}



char* eng_atCommandRequest(int cmd,  void *param) 
{
	memset(at_cmd_req, 0, ENG_AT_CMD_MAX_LEN);
	
	switch (cmd) {
		case ENG_AT_REQUEST_MODEM_VERSION:
			sprintf(at_cmd_req, "AT+CGMM");
			break;
		case ENG_AT_REQUEST_IMEI:
			sprintf(at_cmd_req, "AT+CGSN");
			break;
			
		// band selection	
		case ENG_AT_SELECT_BAND:
			{
				int *band;
				band = (int *)param;
				sprintf(at_cmd_req, "AT+SBAND=%d",*band);
			}
			break;
		case ENG_AT_CURRENT_BAND:
			sprintf(at_cmd_req, "AT+SBAND?");
			break;

		// arm log
		case ENG_AT_SETARMLOG:
			{
				int *on_off;
				on_off = (int *)param;
				sprintf(at_cmd_req, "AT+ARMLOG=%d",*on_off);
			}
			break;
		case ENG_AT_GETARMLOG:
			sprintf(at_cmd_req, "AT+ARMLOG?");
			break;

		// DSP log
		case ENG_AT_SETDSPLOG:
			{
				int *mode;
				mode = (int *)param;
				sprintf(at_cmd_req, "AT+SPDSPOP=%d",*mode);
			}
			break;
		case ENG_AT_GETDSPLOG:
			sprintf(at_cmd_req, "AT+SPDSPOP?");
			break;

		//auto answer
		case ENG_AT_SETAUTOANSWER:
			{
				int *on_off;
				on_off = (int *)param;
				sprintf(at_cmd_req, "AT+SPAUTO=%d",*on_off);
			}
			break;
		case ENG_AT_GETAUTOANSWER:
			sprintf(at_cmd_req, "AT+SPAUTO?");
			break;

		//sp psrate
		case ENG_AT_SETSPPSRATE:
			{
				eng_at_sppsrate *at_sppsrate;
				at_sppsrate = (eng_at_sppsrate *)param;
				sprintf(at_cmd_req, "AT+SPPSRATE=%d,%d,%d",at_sppsrate->type, at_sppsrate->ul, at_sppsrate->dl);
			}
			break;
		case ENG_AT_GETSPPSRATE:
			sprintf(at_cmd_req, "AT+SPPSRATE?");
			break;

		//sp test
		case ENG_AT_SETSPTEST:
			{
				eng_at_sptest *at_sptest;
				at_sptest = (eng_at_sptest *)param;
				sprintf(at_cmd_req, "AT+SPTEST=%d,%d",at_sptest->type, at_sptest->value);
			}
			break;
			
		case ENG_AT_GETSPTEST:
			sprintf(at_cmd_req, "AT+SPTEST?");
			break;

		//spid get UE Identity
		case ENG_AT_SPID:
			sprintf(at_cmd_req, "AT+SPID");
			break;

		//sp frequence
		case ENG_AT_SETSPFRQ:
			{
				eng_at_spfreq *at_spfreq;
				at_spfreq = (eng_at_spfreq *)param;
				if(at_spfreq->param_num==3)
					sprintf(at_cmd_req, "AT+SPFRQ=%d,%d,%d", \
						at_spfreq->operation, at_spfreq->index, at_spfreq->freq);
				else
					sprintf(at_cmd_req, "AT+SPFRQ=%d,%d,%d,%d,%d,%d", \
						at_spfreq->operation, at_spfreq->index, at_spfreq->freq, at_spfreq->cell_id1,at_spfreq->cell_id2,at_spfreq->cell_id3);
			}
			break;

		//get sp frequence
		case ENG_AT_GETSPFRQ:
			sprintf(at_cmd_req,"AT+SPFRQ?");
			break;

		//set loopback mode
		case ENG_AT_SPAUTE:
			{
				eng_at_spaute *at_spaute;
				at_spaute = (eng_at_spaute *)param;
				sprintf(at_cmd_req, "AT+SPAUTE=%d,%d,%d,%d", \
					at_spaute->enable, at_spaute->delayms, at_spaute->dev, at_spaute->volume);
					
			}
			break;

		//set dummy gsm cell
		case ENG_AT_SETSPDGCNUM:
			{
				eng_at_spdgcnum *at_spdgcnum;
				at_spdgcnum = (eng_at_spdgcnum *)param;
				sprintf(at_cmd_req, "AT+SPDGCNUM=%d",at_spdgcnum->num);
			}
			break;
		
		case ENG_AT_GETSPDGCNUM:
			sprintf(at_cmd_req, "AT+SPDGCNUM?");
			break;

		//sp dgc info
		case ENG_AT_SETSPDGCINFO:
			{
				eng_at_spdgcinfo *at_spdgcinfo;
				at_spdgcinfo = (eng_at_spdgcinfo *)param;
				sprintf(at_cmd_req, "AT+SPDGCINFO=%d,%d,%d,%d", \
					at_spdgcinfo->index, at_spdgcinfo->band, at_spdgcinfo->arfcn, at_spdgcinfo->bsic);
			}
			break;

		case ENG_AT_GETSPDGCINFO:
			sprintf(at_cmd_req, "AT+SPDGCINFO?");
			break;

		//td ncell
		case ENG_AT_GRRTDNCELL:
			{
				eng_at_grrtdncell *at_grrtdncell;
				at_grrtdncell = (eng_at_grrtdncell *)param;
				
				if(param == NULL) { //clean
					sprintf(at_cmd_req, "AT+GRRTDNCELL");
				} else {
					printf("at_grrtdncell->param_num=%d\n",at_grrtdncell->param_num);
					switch (at_grrtdncell->param_num) { //diffrent param number cpi
						case 3:
							sprintf(at_cmd_req, "AT+GRRTDNCELL=%d,%d,%d",at_grrtdncell->index,at_grrtdncell->arfcn,at_grrtdncell->cpi1);
							break;
						case 4:
							sprintf(at_cmd_req, "AT+GRRTDNCELL=%d,%d,%d,%d",at_grrtdncell->index,at_grrtdncell->arfcn,at_grrtdncell->cpi1, at_grrtdncell->cpi2);
							break;
						case 5:
							sprintf(at_cmd_req, "AT+GRRTDNCELL=%d,%d,%d,%d,%d",at_grrtdncell->index,at_grrtdncell->arfcn,at_grrtdncell->cpi1,at_grrtdncell->cpi2,at_grrtdncell->cpi3);
							break;
						case 6:
							sprintf(at_cmd_req, "AT+GRRTDNCELL=%d,%d,%d,%d,%d,%d",at_grrtdncell->index,at_grrtdncell->arfcn,at_grrtdncell->cpi1,at_grrtdncell->cpi2,at_grrtdncell->cpi3,at_grrtdncell->cpi4);
							break;
						default:
							sprintf(at_cmd_req,"AT+GRRTDNCELL");
					}
				}
			}
			break;
		//l1ttr switch
		case ENG_AT_SPL1ITRRSWITCH:
			{
				int *on_off;
				on_off = (int *)param;
				sprintf(at_cmd_req, "AT+SPL1ITRRSWITCH=%d",*on_off);
			}		
			break;

		case ENG_AT_GETSPL1ITRRSWITCH:
			sprintf(at_cmd_req, "AT+SPL1ITRRSWITCH?");
			break;

		//pcco td cell
		case ENG_AT_PCCOTDCELL:
			{
				eng_at_pccotdcell *at_pccotdcell;
				at_pccotdcell = (eng_at_pccotdcell *)param;
				sprintf(at_cmd_req, "AT+PCCOTDCELL=%d,%d", \
					at_pccotdcell->arfcn,at_pccotdcell->cell_id);
			}
			break;

		//sdata conf
		case ENG_AT_SDATACONF:
			{
				eng_at_sdataconf *at_sdataconf;
				at_sdataconf = (eng_at_sdataconf *)param;
				if(at_sdataconf->param_num == 5) {
					sprintf(at_cmd_req, "AT+SDATACONF=%d,\"%s\",\"%s\",%d, %d", \
						at_sdataconf->id, at_sdataconf->type, at_sdataconf->ip, at_sdataconf->server_port, at_sdataconf->self_port);
				} else {
					sprintf(at_cmd_req, "AT+SDATACONF=%d,\"%s\",\"%s\",%d", \
						at_sdataconf->id, at_sdataconf->type, at_sdataconf->ip, at_sdataconf->server_port);
				}
			}
			break;

		//l1param
		case ENG_AT_L1PARAM:
			{
				eng_at_l1param *at_l1param;
				at_l1param = (eng_at_l1param *)param;
				if(at_l1param->param_num == 2)
					sprintf(at_cmd_req, "AT+L1PARAM=%d,%d", at_l1param->index,at_l1param->value);
				else
					sprintf(at_cmd_req, "AT+L1PARAM=%d", at_l1param->index);
			}
			break;

		//ttrparam
		case ENG_AT_TRRPARAM:
			{
				eng_at_trrparam *at_trrparam;
				at_trrparam = (eng_at_trrparam *)param;
				if(at_trrparam->param_num == 2)
					sprintf(at_cmd_req, "AT+TRRPARAM=%d,%d", at_trrparam->index,at_trrparam->value);
				else
					sprintf(at_cmd_req, "AT+TRRPARAM=%d", at_trrparam->index);
			}	
			break;

		//td measswth
		case ENG_AT_SETTDMEASSWTH:
			{
				int *type;
				type = (int *)param;
				sprintf(at_cmd_req, "AT+TDMEASSWTH=%d",*type);
			}
			break;

		case ENG_AT_GETTDMEASSWTH:
			sprintf(at_cmd_req, "AT+TDMEASSWTH?");
			break;

		//rrdm param
		case ENG_AT_RRDMPARAM:
			{
				eng_at_rrdmparam *at_rrdmparam;
				at_rrdmparam = (eng_at_rrdmparam *)param;
				if(at_rrdmparam->param_num == 2)
					sprintf(at_cmd_req, "AT+RRDMPARAM=%d,%d", at_rrdmparam->index,at_rrdmparam->value);
				else
					sprintf(at_cmd_req, "AT+RRDMPARAM=%d", at_rrdmparam->index);
			}
			break;

		//reset param
		case ENG_AT_DMPARAMRESET:
			sprintf(at_cmd_req, "AT+DMPARAMRESET");
			break;

		//set timer
		case ENG_AT_SMTIMER:
			{
				eng_at_smtimer *at_smtimer;
				at_smtimer = (eng_at_smtimer*)param;
				if(at_smtimer->param_num== 1)
					sprintf(at_cmd_req, "AT+SMTIMER=%d", at_smtimer->value);
				else
					sprintf(at_cmd_req, "AT+SMTIMER");
			}
			break;

		//trr dcfe param
		case ENG_AT_TRRDCFEPARAM:
			{
				eng_at_ttrdcfeparam *at_ttrdcfeparam;
				at_ttrdcfeparam = (eng_at_ttrdcfeparam*)param;
				switch(at_ttrdcfeparam->param_num) {
					case 1:
						sprintf(at_cmd_req,"AT+TRRDCFEPARAM=%d",at_ttrdcfeparam->index);
						break;
					case 2:
						sprintf(at_cmd_req,"AT+TRRDCFEPARAM=%d,%d",at_ttrdcfeparam->index, at_ttrdcfeparam->value);
						break;
				}
			}
			break;
		//cimi
		case ENG_AT_CIMI:
			sprintf(at_cmd_req, "AT+CIMI");
			break;

		//cell id
		case ENG_AT_MBCELLID:
			{
				int *type;
				type = (int *)param;
				sprintf(at_cmd_req, "AT^MBCELLID=%d",*type);
			}			
			break;

		case ENG_AT_MBAU:
			{
				eng_at_mbau *at_mbau;
				at_mbau = (eng_at_mbau *)param;
				if(at_mbau->param_num == 2)
					sprintf(at_cmd_req, "AT^MBAU=\"%s\",\"%s\" ", at_mbau->rend,at_mbau->autn);
				else
					sprintf(at_cmd_req, "AT^MBAU=\"%s\" ", at_mbau->rend);
			}
			break;

		//EUICC
		case ENG_AT_EUICC:
			sprintf(at_cmd_req, "AT+EUICC?");
			break;

		//CGREG
		case ENG_AT_CGREG:
			sprintf(at_cmd_req, "AT+CGREG?");
			break;

		case ENG_AT_NOHANDLE_CMD:
			{
				eng_at_nohandle *at_nohandle;
				at_nohandle = (eng_at_nohandle *)param;
				sprintf(at_cmd_req, "%s", at_nohandle->cmd);
			}
			break;

		case ENG_AT_SYSINFO:
			sprintf(at_cmd_req, "AT^SYSINFO");
			break;

		case ENG_AT_HVER:
			sprintf(at_cmd_req, "AT^HVER");
			break;

		case ENG_AT_GETSYSCONFIG:
			sprintf(at_cmd_req, "AT^SYSCONFIG?");
			break;

		case ENG_AT_SETSYSCONFIG:
			{
				eng_at_sysinfo *at_sysinfo;
				at_sysinfo = (eng_at_sysinfo *)param;
				sprintf(at_cmd_req, "AT^SYSCONFIG=%d,%d,%d,%d",\
					at_sysinfo->data1, at_sysinfo->data2, at_sysinfo->data3, at_sysinfo->data4);
			}
			break;

		case ENG_AT_SPVER:
			{
				int *value;
				value = (int *)param;
				sprintf(at_cmd_req, "AT+SPVER=%d",*value);
			}
			break;
			
		case ENG_AT_CAOC:
			sprintf(at_cmd_req, "AT+CAOC=2");
			break;
		case ENG_AT_CAOCD:
			sprintf(at_cmd_req, "AT+CAOC=1");
			break;
		case ENG_AT_CAOCQ:
			sprintf(at_cmd_req, "AT+CAOC=0");
			break;
			
		case ENG_AT_CGSMS:
			{
				int *value;
				value = (int *)param;
				sprintf(at_cmd_req, "AT+CGSMS=%d",*value);
			}
			break;
			
		case ENG_AT_GETUPLMN:
			{
				int *value;
				value = (int *)param;
				sprintf(at_cmd_req, "AT+CRSM=176,28512,0,0,%d,0,\"3F007FFF\"", *value);
			}
			break;

		case ENG_AT_GETUPLMNLEN:
			sprintf(at_cmd_req, "AT+CRSM=192,28512,0,0,15,0,\"3F007FFF\"");
			break;
			
		case ENG_AT_AUTOATTACH:
			sprintf(at_cmd_req,"AT+SAUTOATT?");
			break;
			
		case ENG_AT_SETAUTOATTACH:
			{
				int *on_off;
				on_off = (int *)param;
				sprintf(at_cmd_req, "AT+SAUTOATT=%d",*on_off);
			}
			break;	
			
		case ENG_AT_PDPACTIVE:
			{
				eng_at_pdpactive *at_pdpactive;
				at_pdpactive = (eng_at_pdpactive*)param;
				if(at_pdpactive->param_num == 2)
				sprintf(at_cmd_req, "AT+CGACT=%d,%d",at_pdpactive->param1, at_pdpactive->param2);
			}
			break;
			
		case ENG_AT_GETPDPACT:
			{
				sprintf(at_cmd_req, "AT+CGACT?");
			}
			break;		
				
		case ENG_AT_EXIT:
			sprintf(at_cmd_req,"AT+ENGEXIT");
			break;

		case ENG_AT_SGPRSDATA:
			{
				eng_at_spgresdata *at_spgresdata;
				at_spgresdata = (eng_at_spgresdata *)param;
				if(at_spgresdata->param_num == 1)
					sprintf(at_cmd_req, "AT+SGPRSDATA=%d", at_spgresdata->param1);
				else
					sprintf(at_cmd_req, "AT+SGPRSDATA=%d, %d, \"%s\" ", \
					 at_spgresdata->param1, at_spgresdata->param2,  at_spgresdata->param3);
			}
			break;
			
		case ENG_AT_CAMM:
			{
				eng_at_camm *at_camm;
				at_camm = (eng_at_camm *)param;
				if(at_camm->param_num == 2)
					sprintf(at_cmd_req, "AT+CAMM=\"%s\", \"%s\" ", \
					 at_camm->param1, at_camm->param2);
			}
			break;	
			
		case ENG_AT_SETCOPS:
			{
				eng_at_cops *at_cops;
				at_cops = (eng_at_cops *)param;
				if(at_cops->param_num == 4)
					sprintf(at_cmd_req, "AT+COPS=%d, %d, \"%s\", %d", \
					 at_cops->param1, at_cops->param2,  at_cops->param3, at_cops->param4);
			}
			break;
		case ENG_AT_SETUPLMN:
			{
				eng_at_crsm *at_crsm;
				at_crsm = (eng_at_crsm *)param;
				if(at_crsm->param_num == 7)
					sprintf(at_cmd_req, "AT+CRSM=%d,%d,%d,%d,%d,\"%s\",\"%s\"", \
					 at_crsm->whatHandler,
					 at_crsm->fileID,
					 at_crsm->p1,
					 at_crsm->p2,
					 at_crsm->p3,
					 at_crsm->data,
					 at_crsm->others);
			}
			break;
			
		case ENG_AT_SADC:
			sprintf(at_cmd_req, "AT+SADC");
			break;
			
		case ENG_AT_CFUN:
			{
				int *on_off;
				on_off = (int *)param;
				sprintf(at_cmd_req, "AT+CFUN=%d",*on_off);
			}
			break;

		case ENG_AT_CGMR:
			sprintf(at_cmd_req, "AT+CGMR");
			break;

		case ENG_AT_SETCAPLOG:
			{
				int *on_off;
				on_off = (int *)param;
				sprintf(at_cmd_req, "AT+SPCAPLOG=%d",*on_off);
			}
			break;
		case ENG_AT_CCED: //net info of sim
			{
				ENG_AT_LOG("cmd = AT+CCED");
				eng_at_sptest *at_cced;
				at_cced = (eng_at_sptest *)param;
				sprintf(at_cmd_req, "AT+CCED=%d,%d",at_cced->type, at_cced->value);
			}
			break;
		case ENG_AT_L1MON:
			ENG_AT_LOG("cmd = AT+SL1MON");
			sprintf(at_cmd_req, "AT+SL1MON");
			break;
		case ENG_AT_SFPL:
			sprintf(at_cmd_req, "AT+SFPL");
			break;

		case ENG_AT_SEPL:
			sprintf(at_cmd_req, "AT+SEPL");
			break;
		case 	ENG_AT_SPENGMD_QUERY:
			sprintf(at_cmd_req, "AT+SPENGMD=0,10,2");
			break;
		case	ENG_AT_SPENGMD_OPEN:
			sprintf(at_cmd_req, "AT+SPENGMD=1,10,2,3");
			break;
		case	ENG_AT_SPENGMD_CLOSE:
			sprintf(at_cmd_req, "AT+SPENGMD=1,10,2,1");			
			break;

        case ENG_AT_GET_ASSERT_MODE:
			sprintf(at_cmd_req, "AT+SDRMOD?");
            break;

        case ENG_AT_SET_ASSERT_MODE:
        {
            int *mode;
            mode = (int *)param;
            sprintf(at_cmd_req, "AT+SDRMOD=%d",*mode);                        
        }
        break;

        case ENG_AT_SET_MANUAL_ASSERT:
        	sprintf(at_cmd_req, "AT+SPATASSERT=1");
        break;

		case ENG_AT_SGMR:
			{
				eng_at_sgmr *at_sgmr;
				at_sgmr = (eng_at_sgmr *)param;
				if(at_sgmr->param_num == 4)//write-operation
				sprintf(at_cmd_req, "AT+SGMR=%d,%d,%d,\"%s\"",at_sgmr->dual_sys, at_sgmr->op, at_sgmr->type, at_sgmr->str);
				else if(at_sgmr->param_num == 3)
				sprintf(at_cmd_req, "AT+SGMR=%d,%d,%d",at_sgmr->dual_sys, at_sgmr->op, at_sgmr->type);
			}
			break;
		case ENG_AT_SSMP:
			sprintf(at_cmd_req, "AT+SSMP");
			break;
        case ENG_AT_SET_SPSIMRST: {
            int *times;
            times = (int *)param;
            sprintf(at_cmd_req, "AT+SPSIMRST=%d",*times);
            ALOGW("%s: index=%d, [%s]\n", __FUNCTION__, cmd, at_cmd_req);
        }
            break;
        case ENG_AT_QUERY_SPSIMRST: {
            sprintf(at_cmd_req, "AT+SPSIMRST?");
        }
            break;
		default:
			break;
	}

	ENG_LOG("%s: index=%d, cmd=%s\n", __FUNCTION__, cmd, at_cmd_req);
	return at_cmd_req;
}

eng_at_response* eng_atCommandResponse(int cmd, char* data, int data_len)
{
	int respose_data_len;
	char respose_data[256];
	memset(&response, 0, sizeof(response));

	ENG_LOG("%s: cmd=%d\n", __FUNCTION__, cmd);

	if(isFinalResponse(data)) { //response OK/ERROR
	 	ENG_LOG("%s: Response %s\n",__FUNCTION__, data);
		response.content= data;
		response.content_len= data_len;
		return &response;
	} 
	
	switch(cmd) {
		case ENG_AT_REQUEST_MODEM_VERSION:
			response.content= &data[SEND_DATA_START_POS+SEND_DATA_ITEM_LENGTH_BYTES];
			response.content_len= data[SEND_DATA_START_POS+SEND_DATA_ITEM_LENGTH_BYTES-1];
			break;
			
		case ENG_AT_REQUEST_IMEI:
			response.content= &data[SEND_DATA_START_POS+SEND_DATA_ITEM_LENGTH_BYTES];
			response.content_len= data[SEND_DATA_START_POS+SEND_DATA_ITEM_LENGTH_BYTES-1];
			break;

		case ENG_AT_CURRENT_BAND:
			{
				int band;
				response.content=&data[SEND_DATA_START_POS+SEND_DATA_ITEM_LENGTH_BYTES];
				response.content_len = data[SEND_DATA_START_POS+SEND_DATA_ITEM_LENGTH_BYTES-1];
				eng_ResponseCurrentBand(response.content, response.content_len, &band);
				memset(response.content, 0, sizeof(response.content));
				sprintf((char*)response.content, "%d", band);
				response.content_len=strlen(response.content);
			}
			break;	
			
		case ENG_AT_GETARMLOG:
			{
				int on_off;
				response.content=&data[SEND_DATA_START_POS+SEND_DATA_ITEM_LENGTH_BYTES];
				response.content_len = data[SEND_DATA_START_POS+SEND_DATA_ITEM_LENGTH_BYTES-1];
				eng_ResponseGetArmLog(response.content, response.content_len, &on_off);
				memset(response.content, 0, sizeof(response.content));
				sprintf((char*)response.content, "%d", on_off);
				response.content_len=strlen(response.content);
			}
			break;	

		case ENG_AT_GETDSPLOG:
			{
				int on_off;
				response.content=&data[SEND_DATA_START_POS+SEND_DATA_ITEM_LENGTH_BYTES];
				response.content_len = data[SEND_DATA_START_POS+SEND_DATA_ITEM_LENGTH_BYTES-1];
				eng_ResponseGetArmLog(response.content, response.content_len, &on_off);
				memset(response.content, 0, sizeof(response.content));
				sprintf((char*)response.content, "%d", on_off);
				response.content_len=strlen(response.content);
			}
			break;

		case ENG_AT_GETAUTOANSWER:
			{
				int on_off;
				response.content=&data[SEND_DATA_START_POS+SEND_DATA_ITEM_LENGTH_BYTES];
				response.content_len = data[SEND_DATA_START_POS+SEND_DATA_ITEM_LENGTH_BYTES-1];
				eng_ResponseGetAutoAnswer(response.content, response.content_len, &on_off);
				memset(response.content, 0, sizeof(response.content));
				sprintf((char*)response.content, "%d", on_off);
				response.content_len=strlen(response.content);
			}
			break;	

		case ENG_AT_GETSPPSRATE:
			{
				memset(respose_data, 0, sizeof(respose_data));
				response.content=data;
				respose_data_len=eng_ResponseGetSpPsrate(&response, respose_data);

				//setup send data
				memset(response.content, 0, sizeof(response.content));
				memcpy(response.content, respose_data, sizeof(respose_data));
				response.content_len = respose_data_len;
				ENG_LOG("%s: AT+SPPSRATE?: %s ; length=%d\n", __FUNCTION__, response.content,response.content_len);
			}	
			break;

		case ENG_AT_GETSPTEST:
			{
				memset(respose_data, 0, sizeof(respose_data));
				response.content=data;
				respose_data_len=eng_ResponseSPTest(&response, respose_data);

				//setup send data
				memset(response.content, 0, sizeof(response.content));
				memcpy(response.content, respose_data, sizeof(respose_data));
				response.content_len = respose_data_len;
				ENG_LOG("%s: AT+SPTEST?: %s ; length=%d\n", __FUNCTION__, response.content,response.content_len);
			}
			break;
			
		//spid get UE Identity
		case ENG_AT_SPID:	
			{
				memset(respose_data, 0, sizeof(respose_data));
				response.content=data;
				respose_data_len=eng_ResponseSPID(&response, respose_data);

				//setup send data
				memset(response.content, 0, sizeof(response.content));
				memcpy(response.content, respose_data, sizeof(respose_data));
				response.content_len = respose_data_len; 
				ENG_LOG("%s: AT+SPID?: %s ; length=%d\n", __FUNCTION__, response.content,response.content_len);
			}		
			break;

		case ENG_AT_GETSPFRQ:
			{
				memset(respose_data, 0, sizeof(respose_data));
				response.content=data;
				respose_data_len=eng_ResponseSPFRQ(&response, respose_data);

				//setup send data
				memset(response.content, 0, sizeof(response.content));
				memcpy(response.content, respose_data, sizeof(respose_data));
				response.content_len = respose_data_len; 
				ENG_LOG("%s: AT+SPFRQ?: %s ; length=%d\n", __FUNCTION__, response.content,response.content_len);
			}		
			break;

		case ENG_AT_GETSPDGCNUM:
			{
				memset(respose_data, 0, sizeof(respose_data));
				response.content=data;
				respose_data_len=eng_ResponseGetSPDGCNUM(&response, respose_data);

				//setup send data
				memset(response.content, 0, sizeof(response.content));
				memcpy(response.content, respose_data, sizeof(respose_data));
				response.content_len = respose_data_len; 
				ENG_LOG("%s: AT+SPDGCNUM?: %s ; length=%d\n", __FUNCTION__, response.content,response.content_len);
			}
			break;

		case ENG_AT_GETSPDGCINFO:
			{
				memset(respose_data, 0, sizeof(respose_data));
				response.content=data;
				respose_data_len=eng_ResponseGetSPDGCINFO(&response, respose_data);

				//setup send data
				memset(response.content, 0, sizeof(response.content));
				memcpy(response.content, respose_data, sizeof(respose_data));
				response.content_len = respose_data_len; 
				ENG_LOG("%s: AT+SPDGCINFO?: %s ; length=%d\n", __FUNCTION__, response.content,response.content_len);
			}
			break;

		case ENG_AT_GETSPL1ITRRSWITCH:
			{
				memset(respose_data, 0, sizeof(respose_data));
				response.content=data;
				respose_data_len=eng_ResponseGetSPL1(&response, respose_data);

				//setup send data
				memset(response.content, 0, sizeof(response.content));
				memcpy(response.content, respose_data, sizeof(respose_data));
				response.content_len = respose_data_len; 
				ENG_LOG("%s: AT+SPL1ITRRSWITCH?: %s ; length=%d\n", __FUNCTION__, response.content,response.content_len);
			}
			break;

		case ENG_AT_L1PARAM:
			{
				memset(respose_data, 0, sizeof(respose_data));
				response.content=data;
				respose_data_len=eng_ResponseL1PARAM(&response, respose_data);

				//setup send data
				memset(response.content, 0, sizeof(response.content));
				memcpy(response.content, respose_data, sizeof(respose_data));
				response.content_len = respose_data_len; 
				ENG_LOG("%s: +L1PARAM: %s ; length=%d\n", __FUNCTION__, response.content,response.content_len);
			}	
			break;

		case ENG_AT_TRRPARAM:
			{
				memset(respose_data, 0, sizeof(respose_data));
				response.content=data;
				respose_data_len=eng_ResponseTRRPARAM(&response, respose_data);

				//setup send data
				memset(response.content, 0, sizeof(response.content));
				memcpy(response.content, respose_data, sizeof(respose_data));
				response.content_len = respose_data_len; 
				ENG_LOG("%s: +TRRPARAM: %s ; length=%d\n", __FUNCTION__, response.content,response.content_len);
			}	
			break;

		case ENG_AT_SETTDMEASSWTH:
			{
				memset(respose_data, 0, sizeof(respose_data));
				response.content=data;
				respose_data_len=eng_ResponseTDMEASSWTH(&response, respose_data);

				//setup send data
				memset(response.content, 0, sizeof(response.content));
				memcpy(response.content, respose_data, sizeof(respose_data));
				response.content_len = respose_data_len; 
				ENG_LOG("%s: +TDMEASSWTH: %s ; length=%d\n", __FUNCTION__, response.content,response.content_len);
			}
			break;

		case ENG_AT_GETTDMEASSWTH:
			{
				memset(respose_data, 0, sizeof(respose_data));
				response.content=data;
				respose_data_len=eng_ResponseGetTDMEASSWTH(&response, respose_data);

				//setup send data
				memset(response.content, 0, sizeof(response.content));
				memcpy(response.content, respose_data, sizeof(respose_data));
				response.content_len = respose_data_len; 
				ENG_LOG("%s: AT+TDMEASSWTH?: %s ; length=%d\n", __FUNCTION__, response.content,response.content_len);
			}
			break;

		case ENG_AT_RRDMPARAM:
			{
				memset(respose_data, 0, sizeof(respose_data));
				response.content=data;
				respose_data_len=eng_ResponseRRDMPARAM(&response, respose_data);

				//setup send data
				memset(response.content, 0, sizeof(response.content));
				memcpy(response.content, respose_data, sizeof(respose_data));
				response.content_len = respose_data_len; 
				ENG_LOG("%s: +RRDMPARAM: %s ; length=%d\n", __FUNCTION__, response.content,response.content_len);
			}
			break;

		case ENG_AT_SMTIMER:
			{
				memset(respose_data, 0, sizeof(respose_data));
				response.content=data;
				respose_data_len=eng_ResponseSMTIMER(&response, respose_data);

				//setup send data
				memset(response.content, 0, sizeof(response.content));
				memcpy(response.content, respose_data, sizeof(respose_data));
				response.content_len = respose_data_len; 
				ENG_LOG("%s: +SMTIMER: %s ; length=%d\n", __FUNCTION__, response.content,response.content_len);
			}		
			break;

		case ENG_AT_TRRDCFEPARAM:
			{
				memset(respose_data, 0, sizeof(respose_data));
				response.content=data;
				respose_data_len=eng_ResponseTRRDCFEPARAM(&response, respose_data);

				//setup send data
				memset(response.content, 0, sizeof(response.content));
				memcpy(response.content, respose_data, sizeof(respose_data));
				response.content_len = respose_data_len; 
				ENG_LOG("%s: +TRRDCFEPARAM: %s ; length=%d\n", __FUNCTION__, response.content,response.content_len);
			}
			break;
			
		case ENG_AT_CIMI:
			response.content= &data[SEND_DATA_START_POS+SEND_DATA_ITEM_LENGTH_BYTES];
			response.content_len= data[SEND_DATA_START_POS+SEND_DATA_ITEM_LENGTH_BYTES-1];
			break;
			
		case ENG_AT_MBCELLID:
			{
				memset(respose_data, 0, sizeof(respose_data));
				response.content=data;
				respose_data_len=eng_ResponseMBCELLID(&response, respose_data);

				//setup send data
				memset(response.content, 0, sizeof(response.content));
				memcpy(response.content, respose_data, sizeof(respose_data));
				response.content_len = respose_data_len; 
				ENG_LOG("%s: ^MBCELLID: %s ; length=%d\n", __FUNCTION__, response.content,response.content_len);
			}
			break;
			
		case ENG_AT_MBAU:
			{
				memset(respose_data, 0, sizeof(respose_data));
				response.content=data;
				respose_data_len=eng_ResponseMBAU(&response, respose_data);

				//setup send data
				memset(response.content, 0, sizeof(response.content));
				memcpy(response.content, respose_data, sizeof(respose_data));
				response.content_len = respose_data_len; 
				ENG_LOG("%s: ^MBAU: %s ; length=%d\n", __FUNCTION__, response.content,response.content_len);
			}
			break;

		case ENG_AT_EUICC:
			{
				memset(respose_data, 0, sizeof(respose_data));
				response.content=data;
				respose_data_len=eng_ResponseEUICC(&response, respose_data);

				//setup send data
				memset(response.content, 0, sizeof(response.content));
				memcpy(response.content, respose_data, sizeof(respose_data));
				response.content_len = respose_data_len; 
				ENG_LOG("%s: +EUICC: %s ; length=%d\n", __FUNCTION__, response.content,response.content_len);
			}
			break;

		case ENG_AT_CGREG:
			{
				memset(respose_data, 0, sizeof(respose_data));
				response.content=data;
				respose_data_len=eng_ResponseCGREG(&response, respose_data);

				//setup send data
				memset(response.content, 0, sizeof(response.content));
				memcpy(response.content, respose_data, sizeof(respose_data));
				response.content_len = respose_data_len;
				ENG_LOG("%s: +CGREG: %s ; length=%d\n", __FUNCTION__, response.content,response.content_len);
			}
			break;

		case ENG_AT_NOHANDLE_CMD:
			{ //no handle
				ENG_LOG("%s:No Handle CMD Response %s\n",__FUNCTION__, data);
				response.content= data;
				response.content_len= data_len;
			}
			break;

		case ENG_AT_SYSINFO:
			{
				memset(respose_data, 0, sizeof(respose_data));
				response.content=data;
				respose_data_len=eng_ResponseSYSINFO(&response, respose_data);

				//setup send data
				memset(response.content, 0, sizeof(response.content));
				memcpy(response.content, respose_data, sizeof(respose_data));
				response.content_len = respose_data_len;
				ENG_LOG("%s: ^SYSINFO: %s ; length=%d\n", __FUNCTION__, response.content,response.content_len);
			}
			break;

		case ENG_AT_HVER:
			{
				memset(respose_data, 0, sizeof(respose_data));
				response.content=data;
				respose_data_len=eng_ResponseHVER(&response, respose_data);

				//setup send data
				memset(response.content, 0, sizeof(response.content));
				memcpy(response.content, respose_data, sizeof(respose_data));
				response.content_len = respose_data_len;
				ENG_LOG("%s: ^HVER: %s ; length=%d\n", __FUNCTION__, response.content,response.content_len);
			}
			break;


		case ENG_AT_GETSYSCONFIG:
			{
				memset(respose_data, 0, sizeof(respose_data));
				response.content=data;
				respose_data_len=eng_ResponseGetSYSCONFIG(&response, respose_data);

				//setup send data
				memset(response.content, 0, sizeof(response.content));
				memcpy(response.content, respose_data, sizeof(respose_data));
				response.content_len = respose_data_len;
				ENG_LOG("%s: ^GETSYSCONFIG: %s ; length=%d\n", __FUNCTION__, response.content,response.content_len);
			}
			break;

		case ENG_AT_SPVER:
			{
				memset(respose_data, 0, sizeof(respose_data));
				response.content=data;
				respose_data_len=eng_ResponseSPVER(&response, respose_data);

				//setup send data
				memset(response.content, 0, sizeof(response.content));
				memcpy(response.content, respose_data, sizeof(respose_data));
				response.content_len = respose_data_len;
				ENG_LOG("%s: SPVER: %s ; length=%d\n", __FUNCTION__, response.content,response.content_len);
			}
			break;
			
		case ENG_AT_AUTOATTACH:
			{
				int attach;
				response.content=&data[SEND_DATA_START_POS+SEND_DATA_ITEM_LENGTH_BYTES];
				response.content_len = data[SEND_DATA_START_POS+SEND_DATA_ITEM_LENGTH_BYTES-1];
				eng_ResponseCurrentAttach(response.content, response.content_len, &attach);
				memset(response.content, 0, sizeof(response.content));
				sprintf((char*)response.content, "%d", attach);
				response.content_len=strlen(response.content);
				ENG_LOG("%s: AT+SAUTOATT: %s ; length=%d\n", __FUNCTION__, response.content,response.content_len);
			}	
			break;
		case ENG_AT_GETPDPACT:
			{
				response.content= &data[SEND_DATA_START_POS+SEND_DATA_ITEM_LENGTH_BYTES];
				response.content_len= data[SEND_DATA_START_POS+SEND_DATA_ITEM_LENGTH_BYTES-1];
				ENG_LOG("%s: AT+CGACT?: %s ; length=%d\n", __FUNCTION__, response.content,response.content_len);
			}		
			break;
		case ENG_AT_GETUPLMN:
			{
				memset(respose_data, 0, sizeof(respose_data));
				response.content=data;
				respose_data_len=eng_ResponseGETUPLMN(&response, respose_data);

				//setup send data
				memset(response.content, 0, sizeof(response.content));
				memcpy(response.content, respose_data, sizeof(respose_data));
				response.content_len = respose_data_len;
				ENG_LOG("%s: +GETUPLMN: %s ; length=%d\n", __FUNCTION__, response.content,response.content_len);
			}			
			break;
		case ENG_AT_GETUPLMNLEN:
			{
				memset(respose_data, 0, sizeof(respose_data));
				response.content=data;
				respose_data_len=eng_ResponseGETUPLMNLEN(&response, respose_data);

				//setup send data
				memset(response.content, 0, sizeof(response.content));
				memcpy(response.content, respose_data, sizeof(respose_data));
				response.content_len = respose_data_len;
				ENG_LOG("%s: +GETUPLMNLEN: %s ; length=%d\n", __FUNCTION__, response.content,response.content_len);
			}
			break;
		case ENG_AT_SETUPLMN:
			{
				memset(respose_data, 0, sizeof(respose_data));
				response.content=data;
				respose_data_len=eng_ResponseSETUPLMN(&response, respose_data);

				//setup send data
				memset(response.content, 0, sizeof(response.content));
				memcpy(response.content, respose_data, sizeof(respose_data));
				response.content_len = respose_data_len;
				ENG_LOG("%s: +SETUPLMN: %s ; length=%d\n", __FUNCTION__, response.content,response.content_len);
			}
			break;
		case ENG_AT_SADC:
			{
				memset(respose_data, 0, sizeof(respose_data));
				response.content=data;
				respose_data_len=eng_ResponseSADC(&response, respose_data);

				//setup send data
				memset(response.content, 0, sizeof(response.content));
				memcpy(response.content, respose_data, sizeof(respose_data));
				response.content_len = respose_data_len;
				ENG_LOG("%s: SADC: %s ; length=%d\n", __FUNCTION__, response.content,response.content_len);
			}
			break;	

		case ENG_AT_CGMR:
			{
				memset(respose_data, 0, sizeof(respose_data));
				response.content=data;
				respose_data_len=eng_ResponseCGMR(&response, respose_data);

				//setup send data
				memset(response.content, 0, sizeof(response.content));
				memcpy(response.content, respose_data, sizeof(respose_data));
				response.content_len = respose_data_len;
				ENG_LOG("%s: CGMR: %s ; length=%d\n", __FUNCTION__, response.content,response.content_len);
			}
			break;

		case ENG_AT_CCED:
			{
				memset(respose_data, 0, sizeof(respose_data));
				response.content=data;
				respose_data_len=eng_ResponseCCED(&response, respose_data);
				//setup send data
				memset(response.content, 0, sizeof(response.content));
				
				if ( 0 == respose_data_len || 1 == respose_data_len ) {
					respose_data_len = sizeof("No Network");
					memcpy(response.content,"No Network",respose_data_len);
				}else{
					memcpy(response.content, respose_data, sizeof(respose_data));
				}

				response.content_len = respose_data_len;
				ENG_LOG("%s: +CCED: %s ; length=%d\n", __FUNCTION__, response.content,response.content_len);
			}
			break;
        case ENG_AT_L1MON:
			response.content= data;
			response.content_len= strlen(data);
			break;

		case ENG_AT_SFPL:
            {
				memset(respose_data, 0, sizeof(respose_data));
				response.content=data;
				respose_data_len=eng_ResponsePLMN(&response, respose_data);
				//setup send data
				memset(response.content, 0, sizeof(response.content));
				memcpy(response.content, respose_data, sizeof(respose_data));
				response.content_len = respose_data_len;
				ENG_LOG("%s: AT+SFPL: %s ; length=%d\n", __FUNCTION__, response.content,response.content_len);
            }
            break;

        case ENG_AT_SEPL:
            {
				memset(respose_data, 0, sizeof(respose_data));
				response.content=data;
				respose_data_len=eng_ResponsePLMN(&response, respose_data);
				//setup send data
				memset(response.content, 0, sizeof(response.content));
				memcpy(response.content, respose_data, sizeof(respose_data));
				response.content_len = respose_data_len;
				ENG_LOG("%s: AT+SEPL: %s ; length=%d\n", __FUNCTION__, response.content,response.content_len);
            }
            break;
         
		 case ENG_AT_GET_ASSERT_MODE:
            response.content= data;
            response.content_len= strlen(data);
                        break;

		case ENG_AT_SGMR:
				response.content= data;
				response.content_len= strlen(data);
			break;

        case ENG_AT_QUERY_SPSIMRST:
            {
                int times;
                response.content=&data[SEND_DATA_START_POS+SEND_DATA_ITEM_LENGTH_BYTES];
                response.content_len = data[SEND_DATA_START_POS+SEND_DATA_ITEM_LENGTH_BYTES-1];
                response.content_len=strlen(response.content);
                ENG_LOG("%s: AT+SPSIMRST: %s ; length=%d\n", __FUNCTION__, response.content,response.content_len);
            }
            break;
		
		default:
			break;

	}

	return &response;
}


/*******************************************************************************
* Function    :  eng_write2server
* Description :  send data to server
* Parameters  :  data & length
* Return      :    none
*******************************************************************************/
void eng_write2server(int fd, char* data, int len) 
{
	int counter,tmplen;
	char tmp[ENG_BUF_LEN];
	counter=0;

	memset(tmp, 0, ENG_BUF_LEN);
	sprintf(tmp, "%d;%s",fd,data);
	tmplen = strlen(tmp);
	ENG_LOG("%s: write data from modem to server fd=%d;data=%s;len=%d;tmp=%s;tmplen=%d\n", \
		__FUNCTION__,fd, data, len, tmp, tmplen);

	/* Client -> Server*/
	while(eng_write(soc_fd, tmp, tmplen)!=tmplen) {
		counter++;
		if(counter>=3) {
			ENG_LOG("%s: write data to %s failed, retry %d times\n", __FUNCTION__, ENG_PC_DEV, counter);
			break;
		}
	}
	
}

void * eng_parsecmd(int* cmd, char *data, int len)
{
	int err;
	int type;
	int param_num;
	
	err = at_tok_nextint(&data, &type);
	err = at_tok_nextint(&data, &param_num);
	*cmd = type;

	ENG_LOG("%s: cmd=%d\n", __FUNCTION__, *cmd);

	switch(type) {

		case ENG_AT_SELECT_BAND:
			err = at_tok_nextint(&data, &at_oneint);
			ENG_LOG("%s: SET SELECT BAND: %d\n", __FUNCTION__, at_oneint);
			return &at_oneint;

		case ENG_AT_SET_ASSERT_MODE:
			err = at_tok_nextint(&data, &at_oneint);
			ENG_LOG("%s: SET SELECT BAND: %d\n", __FUNCTION__, at_oneint);
			return &at_oneint;
		case ENG_AT_SETARMLOG:
		case ENG_AT_SETDSPLOG:
			err = at_tok_nextint(&data, &at_oneint);
			ENG_LOG("%s: SET ARMLOG: %d\n", __FUNCTION__, at_oneint);
			return &at_oneint;

		case ENG_AT_SETAUTOANSWER:
			err = at_tok_nextint(&data, &at_oneint);
			ENG_LOG("%s: SET AUTO ANSWER: %d\n", __FUNCTION__, at_oneint);
			return &at_oneint;
		
		case ENG_AT_SETSPPSRATE:
			memset(&at_sppsrate, 0, sizeof(at_sppsrate));
			err = at_tok_nextint(&data, &at_sppsrate.type);
			err = at_tok_nextint(&data, &at_sppsrate.ul);
			err = at_tok_nextint(&data, &at_sppsrate.dl);
			ENG_LOG("%s: SET SP PSRATE: %d, %d, %d\n",__FUNCTION__,  at_sppsrate.type,at_sppsrate.ul,at_sppsrate.dl);
			return &at_sppsrate;

		case ENG_AT_SETSPTEST:
			memset(&at_sptest, 0, sizeof(at_sptest));
			err = at_tok_nextint(&data, &at_sptest.type);
			err = at_tok_nextint(&data, &at_sptest.value);
			ENG_LOG("%s: SET SP TEST: %d, %d\n", __FUNCTION__, at_sptest.type, at_sptest.value);
			return &at_sptest;

		case ENG_AT_SETSPFRQ:
			memset(&at_spfreq, 0, sizeof(at_spfreq));
			at_spfreq.param_num = param_num;
			if(param_num == 3) {
				err = at_tok_nextint(&data, &at_spfreq.operation);
				err = at_tok_nextint(&data, &at_spfreq.index);
				err = at_tok_nextint(&data, &at_spfreq.freq);
				ENG_LOG("%s: SET SP TEST: %d, %d, %d\n", __FUNCTION__,at_spfreq.operation,at_spfreq.index, at_spfreq.freq);
			} else {
				err = at_tok_nextint(&data, &at_spfreq.operation);
				err = at_tok_nextint(&data, &at_spfreq.index);
				err = at_tok_nextint(&data, &at_spfreq.freq);
				err = at_tok_nextint(&data, &at_spfreq.cell_id1);
				err = at_tok_nextint(&data, &at_spfreq.cell_id2);
				err = at_tok_nextint(&data, &at_spfreq.cell_id3);
				ENG_LOG("%s: SET SP TEST: %d, %d, %d, %d, %d,%d\n", __FUNCTION__, \
					at_spfreq.operation,at_spfreq.index, at_spfreq.freq,at_spfreq.cell_id1,at_spfreq.cell_id2,at_spfreq.cell_id3);

			}
			return &at_spfreq;

		case ENG_AT_SPAUTE:
			memset(&at_spaute, 0, sizeof(at_spaute));
			err = at_tok_nextint(&data, &at_spaute.enable);
			err = at_tok_nextint(&data, &at_spaute.delayms);
			err = at_tok_nextint(&data, &at_spaute.dev);
			err = at_tok_nextint(&data, &at_spaute.volume);
			ENG_LOG("%s: SET SP AUTE: %d,%d,%d,%d\n", __FUNCTION__, at_spaute.enable, at_spaute.delayms,at_spaute.dev, at_spaute.volume);
			return &at_spaute;
			
		case ENG_AT_SETSPDGCNUM:
			memset(&at_spdgcnum, 0, sizeof(at_spdgcnum));
			err = at_tok_nextint(&data, &at_spdgcnum.num);
			ENG_LOG("%s: SET SP SPDGCNUM: %d\n", __FUNCTION__, at_spdgcnum.num);
			return &at_spdgcnum;

		case ENG_AT_SETSPDGCINFO:
			memset(&at_spdgcinfo, 0, sizeof(at_spdgcinfo));
			err = at_tok_nextint(&data, &at_spdgcinfo.index);
			err = at_tok_nextint(&data, &at_spdgcinfo.band);
			err = at_tok_nextint(&data, &at_spdgcinfo.arfcn);
			err = at_tok_nextint(&data, &at_spdgcinfo.bsic);
			ENG_LOG("%s: SET SP SETSPDGCINFO: %d,%d,%d,%d\n",\
				__FUNCTION__, at_spdgcinfo.index, at_spdgcinfo.band, at_spdgcinfo.arfcn, at_spdgcinfo.bsic);
			return &at_spdgcinfo;

		case ENG_AT_GRRTDNCELL:
			{
				int is_param=0;
				memset(&at_grrtdncell, 0, sizeof(at_grrtdncell));

				//there are params in this cmd
				if(param_num >= 3) {
					err = at_tok_nextint(&data, &at_grrtdncell.index);
					err = at_tok_nextint(&data, &at_grrtdncell.arfcn);
					err = at_tok_nextint(&data, &at_grrtdncell.cpi1);
					is_param=1;
					at_grrtdncell.param_num=3;
				}
				//more params
				if(param_num >= 4) {
					err = at_tok_nextint(&data, &at_grrtdncell.cpi2);	
					at_grrtdncell.param_num=4;
				}
				if(param_num >= 5) {
					err = at_tok_nextint(&data, &at_grrtdncell.cpi3);
					at_grrtdncell.param_num = 5;
				}
				if(param_num >= 6) {
					err = at_tok_nextint(&data, &at_grrtdncell.cpi4);
					at_grrtdncell.param_num = 6;
				}

				if(is_param==0) //no param, used to clean
					return NULL;
				else 
					return &at_grrtdncell;
			}

		case ENG_AT_SPL1ITRRSWITCH:
			err = at_tok_nextint(&data, &at_oneint);
			ENG_LOG("%s: SET SPL1ITRRSWITCH: %d\n", __FUNCTION__, at_oneint);
			return &at_oneint;

		case ENG_AT_PCCOTDCELL:
			memset(&at_pccotdcell, 0, sizeof(at_pccotdcell));
			err = at_tok_nextint(&data, &at_pccotdcell.arfcn);
			err = at_tok_nextint(&data, &at_pccotdcell.cell_id);
			ENG_LOG("%s: SET PCCOTDCELL: %d,%d\n",\
				__FUNCTION__,at_pccotdcell.arfcn, at_pccotdcell.cell_id);
			return &at_pccotdcell;

		case ENG_AT_SDATACONF:
			{
				char type[16]; 
				char ip[64];
				char *ptr_type, *ptr_ip;
				ptr_type = type;
				ptr_ip = ip;
				
				memset(&at_sdataconf, 0, sizeof(at_sdataconf));
				at_sdataconf.param_num = param_num;
				
				err = at_tok_nextint(&data,&at_sdataconf.id);
				err = at_tok_nextstr(&data, &ptr_type);
				memcpy(at_sdataconf.type, ptr_type, sizeof(type));
				err = at_tok_nextstr(&data, &ptr_ip);
				memcpy(at_sdataconf.ip, ptr_ip, sizeof(ip));
				err = at_tok_nextint(&data, &at_sdataconf.server_port);

				if(param_num == 5) {
					err = at_tok_nextint(&data, &at_sdataconf.self_port);
					ENG_LOG("%s: SET SDATACONF: %d, %s, %s, %d, %d\n", \
						__FUNCTION__, at_sdataconf.id, at_sdataconf.type, at_sdataconf.ip, at_sdataconf.server_port, at_sdataconf.self_port);

				} else {
					ENG_LOG("%s: SET SDATACONF: %d, %s, %s, %d\n", \
						__FUNCTION__, at_sdataconf.id, at_sdataconf.type, at_sdataconf.ip, at_sdataconf.server_port);
				}
				return&at_sdataconf;
			}
				
		
		case ENG_AT_L1PARAM:
			memset(&at_l1param, 0, sizeof(at_l1param));
			at_l1param.param_num = param_num; 
			if(param_num == 2) {
				err = at_tok_nextint(&data, &at_l1param.index);
				err = at_tok_nextint(&data, &at_l1param.value);
				ENG_LOG("%s: SET L1PARAM: %d, %d\n", __FUNCTION__, at_l1param.index, at_l1param.value);
			} else {
				err = at_tok_nextint(&data, &at_l1param.index);
				ENG_LOG("%s: GET L1PARAM: %d\n", __FUNCTION__, at_l1param.index);
			}
			return &at_l1param;

		case ENG_AT_TRRPARAM:
			memset(&at_trrparam, 0, sizeof(at_trrparam));
			at_trrparam.param_num = param_num;
			if(param_num == 2){
				err = at_tok_nextint(&data, &at_trrparam.index);
				err = at_tok_nextint(&data, &at_trrparam.value);
				ENG_LOG("%s: SET TRRPARAM: %d, %d\n", __FUNCTION__, at_trrparam.index, at_trrparam.value);
			} else {
				err = at_tok_nextint(&data, &at_trrparam.index);
				ENG_LOG("%s: GET TRRPARAM: %d\n", __FUNCTION__, at_trrparam.index);
			}
			return &at_trrparam;

		case ENG_AT_SETTDMEASSWTH:
			err = at_tok_nextint(&data, &at_oneint);
			ENG_LOG("%s: SET TDMEASSWTH: %d\n", __FUNCTION__, at_oneint);
			return &at_oneint;

		case ENG_AT_RRDMPARAM:
			memset(&at_rrdmparam, 0, sizeof(at_rrdmparam));
			at_rrdmparam.param_num = param_num;
			if(param_num == 2){
				err = at_tok_nextint(&data, &at_rrdmparam.index);
				err = at_tok_nextint(&data, &at_rrdmparam.value);
				ENG_LOG("%s: SET RRDMPARAM: %d, %d\n", __FUNCTION__, at_rrdmparam.index, at_rrdmparam.value);
			} else {
				err = at_tok_nextint(&data, &at_rrdmparam.index);
				ENG_LOG("%s: GET RRDMPARAM: %d\n", __FUNCTION__, at_rrdmparam.index);
			}
			return &at_rrdmparam;	

		case ENG_AT_SMTIMER:
			memset(&at_smtimer, 0, sizeof(at_smtimer));
			at_smtimer.param_num = param_num;
			if(param_num==1) {
				err = at_tok_nextint(&data, &at_smtimer.value);
				ENG_LOG("%s: SET SMTIMER: %d\n", __FUNCTION__, at_smtimer.value);
			} else {
				ENG_LOG("%s: SET GMTIMER\n", __FUNCTION__);
			}
			
			return &at_smtimer;

		case ENG_AT_TRRDCFEPARAM:
			memset(&at_ttrdcfeparam, 0, sizeof(at_ttrdcfeparam));
			at_ttrdcfeparam.param_num = param_num;
			switch(param_num) {
				case 0:
					break;
				case 1:
					err = at_tok_nextint(&data, &at_ttrdcfeparam.index);
					ENG_LOG("%s: SET RRDCFEPARAM: %d\n", __FUNCTION__, at_ttrdcfeparam.index);
					break;
				case 2:
					err = at_tok_nextint(&data, &at_ttrdcfeparam.index);
					err = at_tok_nextint(&data, &at_ttrdcfeparam.value);
					ENG_LOG("%s: SET RRDCFEPARAM: %d, %d\n", __FUNCTION__, at_ttrdcfeparam.index, at_ttrdcfeparam.value);
					break;
				default:
					break;
			}
			return &at_ttrdcfeparam;
			
		case ENG_AT_MBCELLID:
			err = at_tok_nextint(&data, &at_oneint);
			ENG_LOG("%s: MBCELLID: %d\n", __FUNCTION__, at_oneint);
			return &at_oneint;

		case ENG_AT_MBAU:
			{
				char *rend_ptr, *autn_ptr;
				
				memset(&at_mbau, 0, sizeof(at_mbau));
				at_mbau.param_num = param_num;
				rend_ptr = at_mbau.rend;
				autn_ptr = at_mbau.autn;
				switch(param_num) {
					case 1:
						err = at_tok_nextstr(&data, &rend_ptr);
						memcpy(at_mbau.rend, rend_ptr, sizeof(at_mbau.rend));
						ENG_LOG("%s: MBAU: %s\n", __FUNCTION__, at_mbau.rend);
						break;
					case 2:
						err = at_tok_nextstr(&data, &rend_ptr);
						memcpy(at_mbau.rend, rend_ptr, sizeof(at_mbau.rend));
						err = at_tok_nextstr(&data, &autn_ptr);
						memcpy(at_mbau.autn, autn_ptr, sizeof(at_mbau.autn));
						ENG_LOG("%s: MBAU: %s,%s\n", __FUNCTION__, at_mbau.rend,at_mbau.autn);
						break;
				}
				return &at_mbau;
			}
		case ENG_AT_NOHANDLE_CMD:
			{
				memset(&at_nohandle, 0, sizeof(at_nohandle));
				memcpy(at_nohandle.cmd, data, sizeof(at_nohandle.cmd));
				ENG_LOG("ENG_AT_NOHANDLE_CMD: %s\n",at_nohandle.cmd);
				return &at_nohandle;
			}
		case ENG_AT_CGSMS:
			err = at_tok_nextint(&data, &at_oneint);
			ENG_LOG("%s: SET SMS SER: %d\n", __FUNCTION__, at_oneint);
			return &at_oneint;
						
		case ENG_AT_SETAUTOATTACH:
			err = at_tok_nextint(&data, &at_oneint);
			ENG_LOG("%s: SET AUTO ATTACH AT POWER ON: %d\n", __FUNCTION__, at_oneint);
			return &at_oneint;
						
		case ENG_AT_PDPACTIVE:
			{
				memset(&at_pdpactive, 0, sizeof(at_pdpactive));
				at_pdpactive.param_num = param_num;
				if(2== param_num)
				{
				err = at_tok_nextint(&data, &at_pdpactive.param1);
				err = at_tok_nextint(&data, &at_pdpactive.param2);
				}
				ENG_LOG("%s: ENG_AT_PDPACTIVE: %d,%d\n", __FUNCTION__, at_pdpactive.param1,at_pdpactive.param2);
				return &at_pdpactive;
			}
			
		case ENG_AT_SGPRSDATA:
			{
				char param3[256];
				char *param3_ptr = param3;
				memset(&at_spgresdata, 0, sizeof(at_spgresdata));
				at_spgresdata.param_num = param_num;
				switch (param_num){
					case 1:
						err = at_tok_nextint(&data, &at_spgresdata.param1);
						ENG_LOG("%s: SET GPRSDATA: %d\n", __FUNCTION__, at_spgresdata.param1);
						break;
					case 3:
						err = at_tok_nextint(&data, &at_spgresdata.param1);
						err = at_tok_nextint(&data, &at_spgresdata.param2);
						err = at_tok_nextstr(&data, &param3_ptr);
						memcpy(at_spgresdata.param3, param3_ptr, sizeof(at_spgresdata.param3));
						ENG_LOG("%s: SET GPRSDATA: %d,%d,%s\n", __FUNCTION__, \
							at_spgresdata.param1, at_spgresdata.param2, at_spgresdata.param3);
						break;
				}

				return &at_spgresdata;
			}
		case ENG_AT_CAMM:
			{
				char param1[256];
				char param2[256];
				char *param1_ptr = param1;
				char *param2_ptr = param2;
				memset(&at_camm, 0, sizeof(at_camm));
				at_camm.param_num = param_num;
				if (2 == param_num)
				{
					err = at_tok_nextstr(&data, &param1_ptr);
					err = at_tok_nextstr(&data, &param2_ptr);
					memcpy(at_camm.param1, param1_ptr, sizeof(at_camm.param1));
					memcpy(at_camm.param2, param2_ptr, sizeof(at_camm.param2));
					ENG_LOG("%s: SET CAMM: %s,%s\n", __FUNCTION__, \
						at_camm.param1, at_camm.param2);
				}
				return &at_camm;
			}
		case ENG_AT_GETUPLMN:
			{
				err = at_tok_nextint(&data, &at_oneint);
				ENG_LOG("%s: SET SMS SER: %d\n", __FUNCTION__, at_oneint);
				return &at_oneint;
			}
		case ENG_AT_SETUPLMN:
			{
				char param6[128];
				char param7[16];
				char *param6_ptr = param6;
				char *param7_ptr = param7;
				memset(&at_crsm, 0, sizeof(at_crsm));
				at_crsm.param_num = param_num;
				if (7 == param_num){
					err = at_tok_nextint(&data, &at_crsm.whatHandler);
					err = at_tok_nextint(&data, &at_crsm.fileID);
					err = at_tok_nextint(&data, &at_crsm.p1);
					err = at_tok_nextint(&data, &at_crsm.p2);
					err = at_tok_nextint(&data, &at_crsm.p3);
					err = at_tok_nextstr(&data, &param6_ptr);
					err = at_tok_nextstr(&data, &param7_ptr);
					memcpy(at_crsm.data, param6_ptr, sizeof(at_crsm.data));
					memcpy(at_crsm.others, param7_ptr, sizeof(at_crsm.others));
					ENG_LOG("%s: SET UPLMN: %d,%d,%d,%d,%d,%s,%s\n", __FUNCTION__, \
						at_crsm.whatHandler,
						at_crsm.fileID,
						at_crsm.p1,
						at_crsm.p2,
						at_crsm.p3,
						at_crsm.data,
						at_crsm.others);
				}
				return &at_crsm;
			}	
		case ENG_AT_SETCOPS:
			{
				char param3[256];
				char *param3_ptr = param3;
				memset(&at_cops, 0, sizeof(at_cops));
				at_cops.param_num = param_num;
				if (4 == param_num){
					err = at_tok_nextint(&data, &at_cops.param1);
					err = at_tok_nextint(&data, &at_cops.param2);
					err = at_tok_nextstr(&data, &param3_ptr);
					err = at_tok_nextint(&data, &at_cops.param4);
					memcpy(at_cops.param3, param3_ptr, sizeof(at_cops.param3));
					ENG_LOG("%s: SET COPS: %d,%d,%s,%d\n", __FUNCTION__, \
						at_cops.param1, at_cops.param2, at_cops.param3, at_cops.param4);
				}
				return &at_cops;
			}	
		
		
		case ENG_AT_SETSYSCONFIG:
			{
				memset(&at_sysinfo, 0, sizeof(at_sysinfo));
				err = at_tok_nextint(&data, &at_sysinfo.data1);
				err = at_tok_nextint(&data, &at_sysinfo.data2);
				err = at_tok_nextint(&data, &at_sysinfo.data3);
				err = at_tok_nextint(&data, &at_sysinfo.data4);	
				return &at_sysinfo;
			}

		case ENG_AT_SPVER:
			err = at_tok_nextint(&data, &at_oneint);
			ENG_LOG("%s: AT SPVER: %d\n", __FUNCTION__, at_oneint);
			return &at_oneint;

		case ENG_AT_CFUN:
			err = at_tok_nextint(&data, &at_oneint);
			ENG_LOG("%s: SET CFUN: %d\n", __FUNCTION__, at_oneint);
			return &at_oneint;

		case ENG_AT_SETCAPLOG:
			err = at_tok_nextint(&data, &at_oneint);
			ENG_LOG("%s: SET SPCAPLOG: %d\n", __FUNCTION__, at_oneint);
			return &at_oneint;

		case ENG_AT_CCED:
			memset(&at_cced, 0, sizeof(at_cced));
			err = at_tok_nextint(&data, &at_cced.type);
			err = at_tok_nextint(&data, &at_cced.value);
			ENG_LOG("%s: SET CCED: %d, %d\n", __FUNCTION__, at_cced.type, at_cced.value);
			return &at_cced;
		case ENG_AT_SGMR:
			{
				char param3[256];
				char *param3_ptr = param3;
				memset(&at_sgmr, 0, sizeof(at_sgmr));
				at_sgmr.param_num = param_num;
				switch (param_num){
					case 3:
						err = at_tok_nextint(&data, &at_sgmr.dual_sys);
						err = at_tok_nextint(&data, &at_sgmr.op);
						err = at_tok_nextint(&data, &at_sgmr.type);
						ENG_LOG("%s: READ SGMR: %d,%d,%d\n", __FUNCTION__, at_sgmr.dual_sys,at_sgmr.op,at_sgmr.type);
						break;
					case 4:
						err = at_tok_nextint(&data, &at_sgmr.dual_sys);
						err = at_tok_nextint(&data, &at_sgmr.op);
						err = at_tok_nextint(&data, &at_sgmr.type);
						err = at_tok_nextstr(&data, &param3_ptr);
						memcpy(at_sgmr.str, param3_ptr, sizeof(at_sgmr.str));
						ENG_LOG("%s: WIRTE SGMR: %d,%d,%d,%s\n", __FUNCTION__, \
							at_sgmr.dual_sys, at_sgmr.op, at_sgmr.type, at_sgmr.str);
						break;
				}

				return &at_sgmr;
			}
        case ENG_AT_SET_SPSIMRST:
            err = at_tok_nextint(&data, &at_oneint);
            ENG_LOG("%s: ENG AT SET SPSIMRST: %d\n", __FUNCTION__, at_oneint);
            return &at_oneint;
		default:
			return NULL;
			
	}
	
}

static int get_send_fd(char *inbuf, int *len, char *outbuf, eng_fdtype_t *fdtype)
{
	char strtmp[8];
	char *ptr, *ptr1,*ptr2;
	ENG_LOG("%s: inbuf=%s",__func__, inbuf);

	ptr = strchr(inbuf, ';');

	memset(strtmp, 0, sizeof(strtmp));
	*ptr='\0';
	sprintf(strtmp, "%s", inbuf);	
	*len = atoi(strtmp);
	ptr++;

	ptr1 = strchr(ptr, ';');
	memset(strtmp, 0, sizeof(strtmp));
	*ptr1 = '\0';
	sprintf(strtmp, "%s",ptr);
	fdtype->fd = atoi(strtmp);
	ptr1++;

	ptr2 = strchr(ptr1, ';');
	memset(strtmp, 0, sizeof(strtmp));
	*ptr2='\0';
	sprintf(strtmp, "%s", ptr1);
	fdtype->type = atoi(strtmp);
	//sprintf(outbuf, "%s",ptr2+1);
	memcpy(outbuf,ptr2+1,*len - (ptr2-inbuf-1));

	ENG_LOG("%s: outbuf=%s; len =%d,fd=%d, type=%d",__func__, outbuf, *len,fdtype->fd, fdtype->type);

	return 0;
	
}

static int eng_modem2server(int status, eng_fdtype_t *fdtype)
{
	int n, counter, tmp, i=0;
	int ret=-1, modemfd;
	fd_set readfds;
	char *line=NULL;
	char *send_data=NULL;
	char *ptr=NULL;
	unsigned char send_len;
	eng_at_response *at_response;
	eng_at_response at_response_tmp;
	struct timeval timeout;	

    /* Add for Bug99799 Start */
    if(status == -2) {
        ALOGD("%s: Fatal status Error,do nothing\n",__FUNCTION__);
        return ret;
    }
    /* Add for Bug99799 End   */
	if(status < 0) {
		ALOGD("%s: Get status Error\n",__FUNCTION__);
		eng_write2server(fdtype->fd, "ERROR", strlen("ERROR"));
		return ret;
	}
	if(fdtype->type==1) { //SIM2
		ENG_AT_LOG("%s: use SIM2\n",__func__);
		modemfd = modem_fd2;
	} else {	//SIM1
		ENG_AT_LOG("%s: use SIM1\n",__func__);
		modemfd = modem_fd1;
	}
	
	timeout.tv_sec=300;
	timeout.tv_usec=0;

	FD_ZERO(&readfds);
	FD_SET(modemfd, &readfds);

	ENG_LOG("%s: Waiting modem response ..., fd=%d",__FUNCTION__, fdtype->fd);
	n = select(modemfd+1, &readfds, NULL, NULL, &timeout);
	ENG_LOG("%s: Receive Modem Response",__FUNCTION__);
	
	if(n>0) { //modem response
		if(ENG_AT_NOHANDLE_CMD != cmd_type) {
			while(1) {
				line = eng_readline(modemfd);
				if (line == NULL) {
					if(eng_specialAT(cmd_type) == 1) {//AT end without OK/ERROR
						ENG_LOG("%s: get special CMD %d response",__func__, cmd_type);
						send_data=eng_pop_array(&send_len);
						ENG_LOG("%s: Response Read End\n", __FUNCTION__);
						break;
					}
					ENG_LOG("%s: CONTINUE\n",__FUNCTION__);
					continue;
				}
				if(isFinalResponseSuccess(line)) {//reponse finish
					eng_push_finish();
					if(eng_isLargeInfoAt(cmd_type) == 1) {

                        send_data = eng_pop_large_string();

                    }else {
					    send_data=eng_pop_array(&send_len);
					}
					if(send_data[1]==0)  {//no content, return OK
						send_len = strlen("OK");
						sprintf(send_data, "OK");
					}	
					ENG_LOG("%s: OK END\n",__FUNCTION__);
					break;
				}
				else if(isFinalResponseError(line)) {
					send_data=eng_pop_array(&send_len);
					send_len = strlen("ERROR");
					sprintf(send_data, "ERROR");
					ENG_LOG("%s: ERROR END\n",__FUNCTION__);
					break;
				}
				else { //push data into an array, for multi lines resposne, to build a serial data flow
                    ENG_LOG("%s: PUSH DATA\n",__FUNCTION__);
					if(eng_isLargeInfoAt(cmd_type) == 1) {
                        eng_push_large_string(line,strlen(line));
                    }else {
                        eng_push_array(line, strlen(line));
                    }
				}
			}

			//parse at response
			at_response=eng_atCommandResponse(cmd_type, send_data, send_len);
		} else {
			memset(&at_response_tmp, 0, sizeof(at_response_tmp));
			at_response = &at_response_tmp;
			memset(s_ATDictBuf, 0, ENG_BUF_LEN);
			ptr = s_ATDictBuf;
			counter=0;
			do {
				tmp= read(modemfd, ptr, ENG_BUF_LEN);
				ENG_LOG("%s [ptr=%s] tmp=[%d]",__func__, ptr, tmp);
				if(tmp < 0) {
					usleep(1000);
					ENG_LOG("%s: ENG_AT_NOHANDLE_CMD continue",__func__);
					continue;
				} else {
					counter += tmp;
					ptr += counter;
					ENG_LOG("%s: counter=%d",__func__, counter);
				}
			}while(isAtNoHandleEnd(s_ATDictBuf)==0);

			ENG_LOG("%s: s_ATDictBuf[%d]=%s",__FUNCTION__, counter, s_ATDictBuf);
			at_response->content = s_ATDictBuf;
			at_response->content_len = counter;
		}
		
		ENG_LOG("%s: write data from service to app  content=%s; content_len=%d\n", __FUNCTION__, at_response->content,at_response->content_len);
		ENG_AT_LOG("AT < %s\n", at_response->content);				
		//send at response
		eng_write2server(fdtype->fd, at_response->content, at_response->content_len);
		eng_clear_array();

		ret = 0;
	} else{
		ENG_LOG("%s: Get response error n=%d",__FUNCTION__, n);
		ENG_AT_LOG("AT < ERROR(timeout)");
		eng_write2server(fdtype->fd, "ERROR", strlen("ERROR"));
	}

	return ret;
}

/*
 *communication between server and modem
 */
static int eng_servermodem_comm(eng_fdtype_t *fdtype)
{
	int n,data_len,cmd_len,length, modemfd;
	char tmp[ENG_BUF_LEN];
	fd_set readfds;
	char *cmd;
	char *data_ptr;
	void *param;
	char* words;
	int words_len;
	int ret = 0;
	struct eng_buf_struct data;

	/* Server-> Client */
	FD_ZERO(&readfds);
	FD_SET(soc_fd, &readfds);
	ENG_LOG("%s: Waiting command from Server ...... soc_fd=0x%x\n", __FUNCTION__, soc_fd);

	n = select(soc_fd+1, &readfds, NULL, NULL, NULL);
	ENG_LOG("%s: Get Command n=0x%x", __FUNCTION__, n);
	if (n<= 0){
		ALOGE("eng_thread_server2modem exit n=0x%x",n);
		/* Mod for Bug99799 Start */
		//return -1;
		return -2;
		/* Mod for Bug99799 End   */
	}

	//read data from socket
	memset(data.buf, 0, ENG_BUF_LEN);
	memset(tmp, 0, ENG_BUF_LEN);

	length= eng_read(soc_fd, tmp, ENG_BUF_LEN);

	if(length<=0) {
		ALOGE("%s: no data receive read from server\n", __FUNCTION__);
		/* Mod for Bug99799 Start */
		//return -1;
		return -2;
		/* Mod for Bug99799 End   */
	}

	ENG_LOG("%s: tmp=%s, length=%d\n", __FUNCTION__, tmp, length);

	length = 0;
	words = tmp;

	do {
		get_send_fd(words,&words_len, data.buf, fdtype);

		/* Client -> Modem*/
		data_ptr = data.buf;
		data.buf_len = strlen(data.buf);

		ENG_LOG("%s: data.buf=%s, data.buf_len=%d\n", __FUNCTION__, data.buf, data.buf_len);

		param = eng_parsecmd(&cmd_type, data_ptr, data.buf_len);
		cmd=eng_atCommandRequest(cmd_type, param);
		ENG_LOG("%s: write data[%s] from server to modem \n",__FUNCTION__, cmd);

		//write at command
		if(fdtype->type==1) { //SIM2
			ENG_AT_LOG("%s: use SIM2\n",__func__);
			modemfd = modem_fd2;
		} else {	//SIM1
			ENG_AT_LOG("%s: use SIM1\n",__func__);
			modemfd = modem_fd1;
		}
		ENG_AT_LOG("AT > %s",cmd);
		cmd_len = strlen(cmd);
		if(cmd_len>0){
			n=write(modemfd, cmd, cmd_len);
			n=write(modemfd, "\r", 1);
			ENG_LOG("%s: Write Request to Modem Success", __func__);
		} else {
			ALOGE("%s: No cmd to write",__func__);
			ret = -1;
		}

		eng_modem2server(ret, fdtype);

		length += words_len;
		if ( length >=  ENG_BUF_LEN )
		{
			break;
		}
		words += length;

	}while(words[0]!='\0');

	return 0;

}


static void *eng_servermodem_exchange(void *_param)
{
	int ret;
	eng_fdtype_t fdtype;

	ENG_LOG("%s: Run",__FUNCTION__);
	for( ; ; ){

		// data transfer between  server and  modem
		eng_servermodem_comm(&fdtype);

	}

	return NULL;
}


/*******************************************************************************
* Function    :  eng_modemclient_handshake
* Description :  client register to server
* Parameters  :  fd: socket fd
* Return      :    none
*******************************************************************************/
static int eng_modemclient_handshake( int fd)
{
       struct eng_buf_struct data;

       memset (&data,0,sizeof(struct eng_buf_struct));

  	strcpy((char*)data.buf, ENG_MODEM);
	eng_write(fd, data.buf, strlen(ENG_MODEM));
	
	memset(data.buf,0,ENG_BUF_LEN*sizeof(unsigned char));
	eng_read(fd,data.buf, ENG_BUF_LEN);
	if ( strncmp((const char*)data.buf,ENG_WELCOME,strlen(ENG_WELCOME)) == 0){
		return 0;
	}
	ALOGD("%s: handshake error", __FUNCTION__);
	return -1;
}

int eng_mcinit(char *name)
{
	int ret, n=-1, counter=0;
	char simtype[PROP_VALUE_MAX];
	char modem_dev[20];
	char modem_dev2[20];
	modem_fd1 = -1;
	modem_fd2 = -1;

	memset(simtype, 0, sizeof(simtype));
	property_get(ENG_SIMTYPE, simtype, "");
	n = atoi(simtype);
	ALOGD("%s: %s is %s, n=%d\n",__func__, ENG_SIMTYPE, simtype,n);

	if (strcmp(name,"engw") == 0){
		sprintf(modem_dev,"%s",ENG_MODEM_DEVW);
		sprintf(modem_dev2,"%s",ENG_MODEM_DEVW2);
	} else {
		sprintf(modem_dev,"%s",ENG_MODEM_DEVT);
		sprintf(modem_dev2,"%s",ENG_MODEM_DEVT2);
	}

	//open modem-client device file
	while((modem_fd1=open(modem_dev, O_RDWR|O_NONBLOCK)) < 0){
		ENG_LOG("%s: open %s failed!, error[%d][%s]\n",\
			__func__,modem_dev, errno, strerror(errno));
		usleep(500*1000);
	}

	ALOGD("%s: open %s OK",__func__, modem_dev);

	if(n==2) {
		while((modem_fd2=open(modem_dev2, O_RDWR|O_NONBLOCK)) < 0){
			ENG_LOG("%s: open %s failed!, error[%d][%s]\n",\
				__func__,modem_dev2, errno, strerror(errno));
			usleep(500*1000);
		}

		ALOGD("%s: open %s OK",__func__, modem_dev2);
	}

	eng_init_array();
	//connect to server
	while((soc_fd = eng_client(name, SOCK_STREAM))<0) {
		ALOGD("%s: eng_client failed!, error[%d][%s]\n",\
			__func__, errno, strerror(errno));
		usleep(1000);
	}

	ENG_LOG("%s: soc_fd=%d",__func__, soc_fd);
	if (soc_fd < 0) {
	    ENG_LOG("%s: opening  %s server_socket failed\n", __func__,name);
	    close(modem_fd1);
		close(modem_fd2);
	    return -1;
	}
	
	//confirm the connection
	while(eng_modemclient_handshake(soc_fd)!=0) {
		counter++;
		if(counter>=3) {
			ENG_LOG("%s: handshake with server failed, retry %d times\n",__FILE__, counter);
			eng_close(soc_fd);
			close(modem_fd1);
			close(modem_fd2);
			return -1;
		}
	}

	//eng_thread_servermodem();
	eng_servermodem_exchange(NULL);

	return 0;
}

int eng_mcclose(void)
{	
	int ret = 0;
	
	return ret;
}



/*******************************************************************************
* Function    :  eng_monitor_handshake
* Description :  client register to server
* Parameters  :  fd: socket fd
* Return      :    none
*******************************************************************************/
static int eng_monitor_handshake( int fd)
{
	struct eng_buf_struct data;
	
	memset(data.buf,0,ENG_BUF_LEN*sizeof(unsigned char));
	strcpy((char*)data.buf, ENG_MONITOR);
	eng_write(fd, data.buf, strlen(ENG_MONITOR));
	
	memset(data.buf,0,ENG_BUF_LEN*sizeof(unsigned char));
	eng_read(fd,data.buf, ENG_BUF_LEN);
	if ( strncmp((const char*)data.buf,ENG_WELCOME,strlen(ENG_WELCOME)) == 0){
		return 0;
	}
	ALOGD("%s: handshake error read=%s", __FUNCTION__,data.buf);
	return -1;
}

int eng_monitor_open(char *name)
{
	int counter=0;
	int soc_fd;
	int err=0;
	
	//connect to server
	soc_fd = eng_client(name, SOCK_STREAM);

	while(soc_fd < 0) {
		ENG_LOG ("%s: open %s server socket failed\n", __FUNCTION__,name);
		ENG_LOG("%s: soc_fd=%d",__func__, soc_fd);
		usleep(50*1000);
	    soc_fd = eng_client(name, SOCK_STREAM);
	}
	ENG_LOG("%s: soc_fd=%d",__func__, soc_fd);

	//confirm the connection
	while(eng_monitor_handshake(soc_fd)!=0) {
		counter++;
		if(counter>=3) {
			//ENG_LOG("%s: handshake with server failed, retry %d times\n",__FUNCTION__, counter);
			err=-1;
			eng_close(soc_fd);
		}
	}

	return soc_fd;
}

void *eng_monitor_func(void *x)
{
	int fd,n,readlen;
	fd_set readfds;
	unsigned char mbuf[ENG_BUF_LEN] = {0};

	fd = eng_monitor_open((char*)x);
	FD_ZERO(&readfds);
	FD_SET(fd, &readfds);
	sem_post(&thread_sem_lock);
	for ( ;;){
		n = select(fd+1, &readfds, NULL, NULL, NULL);

		readlen=eng_read(fd, mbuf, ENG_BUF_LEN);

		if ( 0 == readlen ){
			ENG_LOG("eng_monitor_thread  break");
			eng_close(fd);
			break;
		}

		//ENG_LOG("eng_monitor_thread  mbuf=%s,n=%d\n",mbuf,n);
		
		eng_write(fd,mbuf,ENG_BUF_LEN);
	}

	return NULL;
	
}

static int eng_monitor_thread(char *name)
{
	pthread_attr_t attr;
	int ret;
	
	pthread_attr_init (&attr);
	ret = pthread_create(&tid_dispatch, &attr, eng_monitor_func, name);
	return ret;

}

int main (int argc, char** argv)
{
	char name[10];
	int opt;
	int type;
#if 0
	umask(0);

	/*
	* Become a session leader to lose controlling TTY.
	*/
	if ((pid = fork()) < 0)
		ENG_LOG("%s: engclient can't fork", __FILE__);
	else if (pid != 0) /* parent */
		exit(0);
	setsid();

	if (chdir("/") < 0){
		ENG_LOG("%s: engclient can't change directory to /", __FILE__);	
	}
#endif
	ENG_LOG("eng_modemclient");

	while ( -1 != (opt = getopt(argc, argv, "t:"))) {
		switch (opt) {
			case 't':
				memset(name,0,10);
				type = atoi(optarg);
				if (type){
					strcpy(name,"engtd");
				} else {
					strcpy(name,"engw");
				}
				break;
			default:
				exit(EXIT_FAILURE);
		}
	}
	ENG_LOG("%s name=%s",__func__,name);

	sem_init(&thread_sem_lock, 0, 0);

	eng_monitor_thread(name);

	sem_wait(&thread_sem_lock);

	eng_mcinit(name);

	return 0;
	
}

