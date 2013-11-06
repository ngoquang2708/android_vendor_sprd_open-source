#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include "engopt.h"


#define NUM_ELEMS(x) (sizeof(x)/sizeof(x[0]))

static const char * s_AllResponse[] = {
    "OK",
    "CONNECT",       /* some stacks start up data on another channel */
    "ERROR",
    "+CMS ERROR:",
    "+CME ERROR:",
    "NO CARRIER", /* sometimes! */
    "NO ANSWER",
    "NO DIALTONE",
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
int isAtNoHandleEnd(const char *data)
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
			ENG_LOG("%s: ptr=0x%x, data=0x%x",__func__, ptr, data);
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


