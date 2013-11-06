
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "engopt.h"
#include "engclient.h"
#include "engat.h"
#include "engparcel.h"
#include "eng_attok.h"

#define  ENG_PARCEL_DEBUG 1


struct send_array_t {
	unsigned char  index;
	unsigned char  item_num;
	char array[SEND_ARRAY_MAX_LEN];
};

static struct send_array_t  send_array;

static int largeIndex = 0;
static char largeString[2048];

/*
 * Send Data Struct:
 * 
 * |data length |item number | item one length | item content | item two length | item content | ------
 * |unsigned char |unsigned char |unsigned char  |      content   |unsigned char |    content   | -------
 * 
*/

int array_length_pos = 0;
int array_index_num_pos = 0;

void eng_init_array(void) 
{
	memset(&send_array, 0, sizeof(send_array));
	send_array.index = SEND_DATA_START_POS;
	send_array.item_num = 0;
	
    largeIndex = 0;
    memset(&largeString, 0, sizeof(largeString));
}
 
void eng_clear_array(void)
{
	memset(&send_array, 0, sizeof(send_array));
	send_array.index = SEND_DATA_START_POS;
	send_array.item_num = 0;

    largeIndex = 0;
    memset(&largeString, 0, sizeof(largeString));
}


/*******************************************************************************
* Function    :  eng_push_array
* Description :  put data into send_array, upate the pointer
* Parameters  : data, len
* Return      :    none
*******************************************************************************/
void eng_push_array(char *data, unsigned char len) 
{
	unsigned char i;
	unsigned char index;
	char *start_ptr;

#if ENG_PARCEL_DEBUG
	ENG_LOG("%s: push data=%s, length=%d\n", __FUNCTION__, data, len);
#endif

	index = send_array.index;
	start_ptr = &send_array.array[index];

	//item length
	*start_ptr = len;
	start_ptr ++;

	//item content
	memcpy(start_ptr, data, len);	

	//update send_array index
	 send_array.index += len + SEND_DATA_ITEM_LENGTH_BYTES;	

	//add data item number
	send_array.item_num++;

#if ENG_PARCEL_DEBUG
	printf("%s: array content:\n", __FUNCTION__);
	for(i=0; i<send_array.index; i++)
		printf("0x%x,", send_array.array[i]);
	printf("\n");
	printf("%s: array length [%d]\n", __FUNCTION__, send_array.index);
#endif
}
void eng_push_large_string(char *data, unsigned char len) 
{
    ENG_LOG("%s: push data=%s, length=%d\n", __FUNCTION__, data, len);

    memcpy(&largeString[largeIndex],data,len);
    largeIndex += len;
    largeString[largeIndex++] = '\n';
}

char* eng_pop_large_string() 
{
    ENG_LOG("%s: largeString is %s", __FUNCTION__, largeString);
    return largeString;
}

/*******************************************************************************
* Function    :  eng_push_finish
* Description :  write data length into array first byte, and item number into array second byte
* Parameters  : data, len
* Return      :    none
*******************************************************************************/
void eng_push_finish(void) 
{
	send_array.array[0] = send_array.index;
	send_array.array[1]=send_array.item_num;
}


/*******************************************************************************
* Function    :  eng_pop_array
* Description :  get data to be sent
* Parameters  : data, len
* Return      :    none
*******************************************************************************/
char* eng_pop_array(unsigned char* len) 
{
	*len = send_array.array[0];
	
#if ENG_PARCEL_DEBUG
	unsigned char i;
	printf("%s:length=%d\n", __FUNCTION__, *len);
	for(i=0; i<*len; i++) {
		printf("0x%x,", send_array.array[i]);
	}
	printf("\n");
#endif

       return send_array.array;
}

void  eng_ResponseOneInt(char *data, int *value)
{
	int err;
	
	err=at_tok_start(&data);
	if(err<0)
		goto err;
	err=at_tok_nextint(&data, value);
	if(err<0)
		goto err;

	return;

err:
	ENG_LOG("%s: Fail!\n",__FUNCTION__);
	
}


void eng_ResponseCurrentBand(char *data, int length, int *band) 
{
	eng_ResponseOneInt(data,band);
	ENG_LOG("%s: band=%d\n", __FUNCTION__, *band);

	return;
}

void eng_ResponseGetArmLog(char *data, int length, int *on_off) 
{
	eng_ResponseOneInt(data, on_off);
	ENG_LOG("%s: on_off=%d\n", __FUNCTION__, *on_off);

	return;
}

void eng_ResponseGetAutoAnswer(char *data, int length, int *on_off) 
{
	eng_ResponseOneInt(data, on_off);
	ENG_LOG("%s: on_off=%d\n", __FUNCTION__, *on_off);

	return;
}

int eng_ResponseGetSpPsrate(eng_at_response* content, char *out) 
{
	int i,item_len,err;
	int type, ul, dl;
	int n=content->content[SEND_DATA_START_POS-1];
	char item[128];
	char* item_ptr, *out_ptr;
	char* ptr = &content->content[SEND_DATA_START_POS];

	ENG_LOG("%s: item number=%d\n", __FUNCTION__, n);
	out_ptr = out;

	for(i=0; i<n; i++) {
		item_len=*ptr;

		ptr++;
		memset(item, 0, sizeof(item));
		snprintf(item, item_len+1, "%s", ptr);
		ptr += item_len;

		//parse int data in item string
		item_ptr = item;
		
		err=at_tok_start(&item_ptr);
		err=at_tok_nextint(&item_ptr, &type);
		err=at_tok_nextint(&item_ptr, &ul);
		err=at_tok_nextint(&item_ptr, &dl);
#if ENG_PARCEL_DEBUG
		printf("type=%d, ul=%d, dl=%d\n", type,ul,dl);
#endif
		//add info into out param
		sprintf(out_ptr, "%d,%d,%d,",type,ul,dl);
		out_ptr += strlen(out_ptr);
	}

	return strlen(out);
}

int eng_ResponseSPID(eng_at_response* content, char *out) 
{
	int i,item_len,err;
	int n=content->content[SEND_DATA_START_POS-1];
	char item[128];
	char data[64];
	char *item_ptr, *out_ptr, *data_ptr;
	char* ptr = &content->content[SEND_DATA_START_POS];

	ENG_LOG("%s: item number=%d\n", __FUNCTION__, n);
	out_ptr = out;

	for(i=0; i<n; i++) {
		item_len=*ptr;

		ptr++;
		memset(item, 0, sizeof(item));
		memset(data, 0, sizeof(data));
		snprintf(item, item_len+1, "%s", ptr);
		ptr += item_len;

		//parse int data in item string
		item_ptr = item;
		data_ptr = data;
		
		err=at_tok_start(&item_ptr);
		err=at_tok_nextstr(&item_ptr, &data_ptr);
#if ENG_PARCEL_DEBUG
		printf("data=%s\n", data_ptr);
#endif
		//add info into out param
		sprintf(out_ptr, "%s,",data_ptr);
		out_ptr += strlen(out_ptr);
	}

	return strlen(out);
}


int eng_ResponseSPTest(eng_at_response* content, char *out) 
{
	int i,item_len,err;
	int type, value;
	int n=content->content[SEND_DATA_START_POS-1];
	char item[128];
	char *item_ptr, *out_ptr;
	char* ptr = &content->content[SEND_DATA_START_POS];

	ENG_LOG("%s: item number=%d\n", __FUNCTION__, n);
	out_ptr = out;

	for(i=0; i<n; i++) {
		item_len=*ptr;

		ptr++;
		memset(item, 0, sizeof(item));
		snprintf(item, item_len+1, "%s", ptr);
		ptr += item_len;
		//parse int data in item string
		item_ptr = item;
		
		err=at_tok_start(&item_ptr);
		err=at_tok_nextint(&item_ptr, &type);
		err=at_tok_nextint(&item_ptr, &value);
#if ENG_PARCEL_DEBUG
		printf("type=%d, value=%d\n", type, value);
#endif
		//add info into out param
		sprintf(out_ptr, "%d,%d,",type, value);
		out_ptr += strlen(out_ptr);
	}

	return strlen(out);
}

int eng_ResponseSPFRQ(eng_at_response* content, char *out)
{
	int i,item_len,err;
	int freq, cell_id1,cell_id2,cell_id3;
	int n=content->content[SEND_DATA_START_POS-1];
	char item[128];
	char *item_ptr, *out_ptr;
	char* ptr = &content->content[SEND_DATA_START_POS];

	ENG_LOG("%s: item number=%d\n", __FUNCTION__, n);
	out_ptr = out;

	for(i=0; i<n; i++) {
		item_len=*ptr;

		ptr++;
		memset(item, 0, sizeof(item));
		snprintf(item, item_len+1, "%s", ptr);
		ptr += item_len;
		//parse int data in item string
		item_ptr = item;
		
		err=at_tok_start(&item_ptr);
		err=at_tok_nextint(&item_ptr, &freq);
		err=at_tok_nextint(&item_ptr, &cell_id1);
		err=at_tok_nextint(&item_ptr, &cell_id2);
		err=at_tok_nextint(&item_ptr, &cell_id3);
#if ENG_PARCEL_DEBUG
		printf("freq=%d, cell_id1=%d, cell_id2=%d, cell_id3=%d\n", freq, cell_id2, cell_id2,cell_id3);
#endif
		//add info into out param
		sprintf(out_ptr, "%d,%d,%d,%d,", freq, cell_id2, cell_id2,cell_id3);
		out_ptr += strlen(out_ptr);
	}

	return strlen(out);
}

int eng_ResponseGetSPDGCNUM(eng_at_response* content, char *out)
{
	int i,item_len,err;
	int num;
	int n=content->content[SEND_DATA_START_POS-1];
	char item[128];
	char *item_ptr, *out_ptr;
	char* ptr = &content->content[SEND_DATA_START_POS];

	ENG_LOG("%s: item number=%d\n", __FUNCTION__, n);
	out_ptr = out;

	for(i=0; i<n; i++) {
		item_len=*ptr;

		ptr++;
		memset(item, 0, sizeof(item));
		snprintf(item, item_len+1, "%s", ptr);
		ptr += item_len;
		//parse int data in item string
		item_ptr = item;
		
		err=at_tok_nextint(&item_ptr, &num);
#if ENG_PARCEL_DEBUG
		printf("num=%d\n", num);
#endif
		//add info into out param
		sprintf(out_ptr, "%d", num);
		out_ptr += strlen(out_ptr);
	}

	return strlen(out);
}

int eng_ResponseGetSPDGCINFO(eng_at_response* content, char *out)
{
	int i,item_len,err;
	int index, arfcn, bsic;
	int n=content->content[SEND_DATA_START_POS-1];
	char item[128];
	char *item_ptr, *out_ptr;
	char* ptr = &content->content[SEND_DATA_START_POS];

	ENG_LOG("%s: item number=%d\n", __FUNCTION__, n);
	out_ptr = out;

	for(i=0; i<n; i++) {
		item_len=*ptr;

		ptr++;
		memset(item, 0, sizeof(item));
		snprintf(item, item_len+1, "%s", ptr);
		ptr += item_len;
		//parse int data in item string
		item_ptr = item;
		
		err=at_tok_nextint(&item_ptr, &index);
		err=at_tok_nextint(&item_ptr, &arfcn);
		err=at_tok_nextint(&item_ptr, &bsic);
#if ENG_PARCEL_DEBUG
		printf("index=%d, arfcn=%d, bsic=%d\n", index, arfcn, bsic);
#endif
		//add info into out param
		sprintf(out_ptr, "%d,%d,%d", index, arfcn, bsic);
		out_ptr += strlen(out_ptr);
	}

	return strlen(out);
}

int eng_ResponseGetSPL1(eng_at_response* content, char *out) 
{

	int on_off;
	char *data;
	data=&content->content[SEND_DATA_START_POS+SEND_DATA_ITEM_LENGTH_BYTES];

	eng_ResponseOneInt(data, &on_off);

	sprintf(out, "%d", on_off);
			
	return strlen(out);
}

int eng_ResponseIntString(char *data, char *out)
{
	int index, err;
	char tmp[16];
	char  *ptr; 
	

	ptr = tmp;
	err=at_tok_start(&data);
	err=at_tok_nextint(&data, &index);
	err=at_tok_nextstr(&data, &ptr);

	ENG_LOG("index=%d,ptr=%s\n", index, ptr);

	sprintf(out, "%d, %s", index, ptr);
			
	return strlen(out);
}

int eng_ResponseString(char *data, char *out)
{
	int index, err;
	char tmp[16];
	char  *ptr; 
	

	ptr = tmp;
	err=at_tok_start(&data);
	err=at_tok_nextstr(&data, &ptr);

	ENG_LOG("ptr=%s\n",ptr);

	sprintf(out, "%s", ptr);
			
	return strlen(out);
}

int eng_ResponseL1PARAM(eng_at_response* content, char *out)
{
	char *data;
	data=&content->content[SEND_DATA_START_POS+SEND_DATA_ITEM_LENGTH_BYTES];

	return eng_ResponseIntString(data, out);
}

int eng_ResponseTRRPARAM(eng_at_response* content, char *out)
{
	char *data;
	data=&content->content[SEND_DATA_START_POS+SEND_DATA_ITEM_LENGTH_BYTES];

	return eng_ResponseIntString(data, out);
}

int eng_ResponseRRDMPARAM(eng_at_response* content, char *out)
{
	char *data;
	data=&content->content[SEND_DATA_START_POS+SEND_DATA_ITEM_LENGTH_BYTES];

	return eng_ResponseIntString(data, out);
}

int eng_ResponseTDMEASSWTH(eng_at_response* content, char *out)
{
	char *data;
	data=&content->content[SEND_DATA_START_POS+SEND_DATA_ITEM_LENGTH_BYTES];

	return eng_ResponseString(data, out);
}

int eng_ResponseGetTDMEASSWTH(eng_at_response* content, char *out) 
{

	int type;
	char *data;
	data=&content->content[SEND_DATA_START_POS+SEND_DATA_ITEM_LENGTH_BYTES];

	eng_ResponseOneInt(data, &type);

	sprintf(out, "%d", type);
			
	return strlen(out);
}

int eng_ResponseSMTIMER(eng_at_response* content, char *out)
{
	char *data;
	data=&content->content[SEND_DATA_START_POS+SEND_DATA_ITEM_LENGTH_BYTES];

	return eng_ResponseString(data, out);
}

int eng_ResponseTRRDCFEPARAM(eng_at_response* content, char *out)
{
	char *data;
	data=&content->content[SEND_DATA_START_POS+SEND_DATA_ITEM_LENGTH_BYTES];

	return eng_ResponseIntString(data, out);
}


int eng_ResponseMBCELLID(eng_at_response* content, char *out)
{
	int err;
	char *data;
	char *ptr1,*ptr2, *ptr3, *ptr4;
	char mcc[16]={0};
	char mnc[16]={0};
	char lac[16]={0};
	char ci[16]={0};
	
	data=&content->content[SEND_DATA_START_POS+SEND_DATA_ITEM_LENGTH_BYTES];

	
	err=at_tok_start(&data);

	if(strlen(data)>1) {
		ptr1=mcc;
		err=at_tok_nextstr(&data, &ptr1);
		ptr2=mnc;
		err = at_tok_nextstr(&data, &ptr2);
		ptr3=lac;
		err = at_tok_nextstr(&data, &ptr3);
		ptr4 = ci;
		err = at_tok_nextstr(&data, &ptr4);
		sprintf(out, "%s,%s,%s,%s", ptr1, ptr2, ptr3,ptr4);
	} else {
		sprintf(out," ");
	}

	return  strlen(out);
}

int eng_ResponseMBAU(eng_at_response* content, char *out)
{
	int i,item_len,err;
	int data1,data2, data3;
	int n=content->content[SEND_DATA_START_POS-1];
	char item[128];
	char *item_ptr, *out_ptr;
	char* ptr = &content->content[SEND_DATA_START_POS];

	out_ptr = out;

	item_len=*ptr;

	ptr++;
	memset(item, 0, sizeof(item));
	snprintf(item, item_len+1, "%s", ptr);

	//parse int data in item string
	item_ptr = item;
	
	err=at_tok_start(&item_ptr);

	//add info into out param
	sprintf(out_ptr, "%s",item_ptr);
	out_ptr += strlen(out_ptr);


	return strlen(out);
}

int eng_ResponseEUICC(eng_at_response* content, char *out)
{
	int i,item_len,err;
	int data1,data2, data3;
	int n=content->content[SEND_DATA_START_POS-1];
	char item[128];
	char *item_ptr, *out_ptr;
	char* ptr = &content->content[SEND_DATA_START_POS];

	out_ptr = out;

	item_len=*ptr;

	ptr++;
	memset(item, 0, sizeof(item));
	snprintf(item, item_len+1, "%s", ptr);

	//parse int data in item string
	item_ptr = item;

	err=at_tok_start(&item_ptr);
	err=at_tok_nextint(&item_ptr, &data1);
	err=at_tok_nextint(&item_ptr, &data2);
	err=at_tok_nextint(&item_ptr, &data3);

	//add info into out param
	sprintf(out_ptr, "%d,%d,%d",data1,data2,data3);
	out_ptr += strlen(out_ptr);


	return strlen(out);
}

int eng_ResponseCGREG(eng_at_response* content, char *out)
{
	int i,item_len,err;
	int int1,int2, int3;
	char string1[32]={0};
	char string2[32]={0};
	char item[128];
	char *item_ptr, *out_ptr, *string1_ptr, *string2_ptr;
	
	int n=content->content[SEND_DATA_START_POS-1];
	char* ptr = &content->content[SEND_DATA_START_POS];
	
	item_len=*ptr;
	out_ptr = out;

	//data in item string
	ptr++;
	memset(item, 0, sizeof(item));
	snprintf(item, item_len+1, "%s", ptr);
	item_ptr = item;

	//parse data
	err=at_tok_start(&item_ptr);

	//two int data
	err=at_tok_nextint(&item_ptr, &int1);
	err=at_tok_nextint(&item_ptr, &int2);

	//two string data
	string1_ptr=string1;
	err=at_tok_nextstr(&item_ptr, &string1_ptr);
	string2_ptr=string2;
	err=at_tok_nextstr(&item_ptr, &string2_ptr);

	//one int
	err=at_tok_nextint(&item_ptr, &int3);
	
	//add info into out param
	sprintf(out_ptr, "%d,%d,%s,%s,%d",int1,int2,string1_ptr,string2_ptr,int3);
	out_ptr += strlen(out_ptr);


	return strlen(out);
}

int eng_ResponseSYSINFO(eng_at_response* content, char *out)
{
	int i,item_len,err;
	int int1,int2, int3, int4, int5, int6, int7;
	char item[128];
	char *item_ptr, *out_ptr;
	
	int n=content->content[SEND_DATA_START_POS-1];
	char* ptr = &content->content[SEND_DATA_START_POS];
	
	item_len=*ptr;
	out_ptr = out;

	//data in item string
	ptr++;
	memset(item, 0, sizeof(item));
	snprintf(item, item_len+1, "%s", ptr);
	item_ptr = item;

	//parse data
	err=at_tok_start(&item_ptr);

	//int data
	err=at_tok_nextint(&item_ptr, &int1);
	err=at_tok_nextint(&item_ptr, &int2);
	err=at_tok_nextint(&item_ptr, &int3);
	err=at_tok_nextint(&item_ptr, &int4);
	err=at_tok_nextint(&item_ptr, &int5);
	err=at_tok_nextint(&item_ptr, &int6);
	err=at_tok_nextint(&item_ptr, &int7);
	
	//add info into out param
	sprintf(out_ptr, "%d,%d,%d,%d,%d,%d,%d",int1,int2,int3,int4,int5,int6,int7);
	out_ptr += strlen(out_ptr);


	return strlen(out);
}

int eng_ResponseHVER(eng_at_response* content, char *out)
{
	int i,item_len,err;
	int int1,int2, int3;
	char string1[32]={0};
	char item[128];
	char *item_ptr, *out_ptr, *string1_ptr, *string2_ptr;
	
	int n=content->content[SEND_DATA_START_POS-1];
	char* ptr = &content->content[SEND_DATA_START_POS];
	
	item_len=*ptr;
	out_ptr = out;

	//data in item string
	ptr++;
	memset(item, 0, sizeof(item));
	snprintf(item, item_len+1, "%s", ptr);
	item_ptr = item;

	//parse data
	err=at_tok_start(&item_ptr);

	//string data
	string1_ptr=string1;
	err=at_tok_nextstr(&item_ptr, &string1_ptr);

	
	//add info into out param
	sprintf(out_ptr, "%s",string1_ptr);
	out_ptr += strlen(out_ptr);


	return strlen(out);
}


int eng_ResponseGetSYSCONFIG(eng_at_response* content, char *out)
{
	int i,item_len,err;
	int int1,int2, int3, int4;
	char item[128];
	char *item_ptr, *out_ptr;
	
	int n=content->content[SEND_DATA_START_POS-1];
	char* ptr = &content->content[SEND_DATA_START_POS];
	
	item_len=*ptr;
	out_ptr = out;

	//data in item string
	ptr++;
	memset(item, 0, sizeof(item));
	snprintf(item, item_len+1, "%s", ptr);
	item_ptr = item;

	//parse data
	err=at_tok_start(&item_ptr);

	//int data
	err=at_tok_nextint(&item_ptr, &int1);
	err=at_tok_nextint(&item_ptr, &int2);
	err=at_tok_nextint(&item_ptr, &int3);
	err=at_tok_nextint(&item_ptr, &int4);
	
	//add info into out param
	sprintf(out_ptr, "%d,%d,%d,%d",int1,int2,int3,int4);
	out_ptr += strlen(out_ptr);


	return strlen(out);
}


int eng_ResponseSPVER(eng_at_response* content, char *out)
{
	int i,item_len,err;
	int int1,int2, int3;
	char string1[32]={0};
	char item[128];
	char *item_ptr, *out_ptr, *string1_ptr, *string2_ptr;
	
	int n=content->content[SEND_DATA_START_POS-1];
	char* ptr = &content->content[SEND_DATA_START_POS];
	
	item_len=*ptr;
	out_ptr = out;

	//data in item string
	ptr++;
	memset(item, 0, sizeof(item));
	snprintf(item, item_len+1, "%s", ptr);
	item_ptr = item;

	//parse data
	err=at_tok_start(&item_ptr);

	//string data
	string1_ptr=string1;
	err=at_tok_nextstr(&item_ptr, &string1_ptr);

	
	//add info into out param
	sprintf(out_ptr, "%s",string1_ptr);
	out_ptr += strlen(out_ptr);


	return strlen(out);
}

void eng_ResponseCurrentAttach(char *data, int length, int *attach) 
{
	eng_ResponseOneInt(data,attach);
	ENG_LOG("%s: attach=%d\n", __FUNCTION__, *attach);

	return;
}
int eng_ResponseSETUPLMN(eng_at_response* content, char *out)
{
	int i,item_len,err;
	int int1,int2;
	char item[256];
	char *item_ptr, *out_ptr;
	
	int n=content->content[SEND_DATA_START_POS-1];
	char* ptr = &content->content[SEND_DATA_START_POS];
	
	item_len=*ptr;
	out_ptr = out;

	//data in item string
	ptr++;
	memset(item, 0, sizeof(item));
	snprintf(item, item_len+1, "%s",ptr);
	item_ptr = item;

	//parse data
	err=at_tok_start(&item_ptr);

	//two int data
	err=at_tok_nextint(&item_ptr, &int1);
	err=at_tok_nextint(&item_ptr, &int2);

	if(int1==144){
	    sprintf(out_ptr, "%d",int1);
	}
	else{	
	    sprintf(out_ptr, "%d,%d",int1,int2);
		}
	out_ptr += strlen(out_ptr);

	return strlen(out);
}
int eng_ResponseGETUPLMN(eng_at_response* content, char *out)
{
	int i,item_len,err;
	int int1,int2;//, int3;
	char string1[128]={0};
	char item[256];
	char *item_ptr, *out_ptr, *string1_ptr;
	
	int n=content->content[SEND_DATA_START_POS-1];
	char* ptr = &content->content[SEND_DATA_START_POS];
	
	item_len=*ptr;
	out_ptr = out;

	//data in item string
	ptr++;
	memset(item, 0, sizeof(item));
	snprintf(item, item_len+1, "%s", ptr);
	item_ptr = item;

	//parse data
	err=at_tok_start(&item_ptr);

	//two int data
	err=at_tok_nextint(&item_ptr, &int1);
	err=at_tok_nextint(&item_ptr, &int2);

	//string data
	string1_ptr=string1;
	err=at_tok_nextstr(&item_ptr, &string1_ptr);

	//one int
	//err=at_tok_nextint(&item_ptr, &int3);
	
	//add info into out param
	//sprintf(out_ptr, "%d,%d,%s,%d",int1,int2,string1_ptr,int3);
	if(int1==144){
	    sprintf(out_ptr, "%s",string1_ptr);
	}
	else{	
	    sprintf(out_ptr, "%d",int1);
		}
	out_ptr += strlen(out_ptr);


	return strlen(out);
}
int eng_ResponseGETUPLMNLEN(eng_at_response* content, char *out)
{
	int i,item_len,err;
	int int1,int2;//, int3;
	char string1[128]={0};
	char item[256];
	char *item_ptr, *out_ptr, *string1_ptr;

	int n=content->content[SEND_DATA_START_POS-1];
	char* ptr = &content->content[SEND_DATA_START_POS];

	item_len=*ptr;
	out_ptr = out;

	//data in item string
	ptr++;
	memset(item, 0, sizeof(item));
	snprintf(item, item_len+1, "%s",ptr);
	item_ptr = item;

	//parse data
	err=at_tok_start(&item_ptr);

	//two int data
	err=at_tok_nextint(&item_ptr, &int1);
	err=at_tok_nextint(&item_ptr, &int2);

	//string data
	string1_ptr=string1;
	err=at_tok_nextstr(&item_ptr, &string1_ptr);

	if(int1==144){
	    sprintf(out_ptr, "%s",string1_ptr);
	}else{
	    sprintf(out_ptr, "%d",int1);
	}
	out_ptr += strlen(out_ptr);


	return strlen(out);
}
int eng_ResponseSADC(eng_at_response* content, char *out)
{
	int i,item_len,err;
	int int1,int2, int3, int4;
	char item[128];
	char *item_ptr, *out_ptr;
	
	int n=content->content[SEND_DATA_START_POS-1];
	char* ptr = &content->content[SEND_DATA_START_POS];

	
	item_len=*ptr;
	out_ptr = out;

	//data in item string
	ptr++;
	memset(item, 0, sizeof(item));
	snprintf(item, item_len+1, "%s", ptr);
	item_ptr = item;

	ENG_LOG("%s: ptr=%s",__func__, item_ptr);

	//parse data
	err=at_tok_start(&item_ptr);
	//int data
	err=at_tok_nexthexint(&item_ptr, &int1);
	err=at_tok_nexthexint(&item_ptr, &int2);
	err=at_tok_nexthexint(&item_ptr, &int3);
	err=at_tok_nexthexint(&item_ptr, &int4);
	
	//add info into out param
	sprintf(out_ptr, "0x%x,0x%x,0x%x,0x%x",int1,int2,int3,int4);
	out_ptr += strlen(out_ptr);


	return strlen(out);
}

int eng_ResponseCGMR(eng_at_response* content, char *out)
{
	int i,item_len,err;
	int index, arfcn, bsic;
	int n=content->content[SEND_DATA_START_POS-1];
	char item[128];
	char *item_ptr, *out_ptr;
	char* ptr = &content->content[SEND_DATA_START_POS];

	ENG_LOG("%s: item number=%d\n", __FUNCTION__, n);
	out_ptr = out;

	for(i=0; i<n; i++) {
		item_len=*ptr;

		ptr++;
		memset(item, 0, sizeof(item));
		snprintf(item, item_len+1, "%s", ptr);
		ptr += item_len;
		//parse int data in item string
		item_ptr = item;
		ENG_LOG("%s: %s",__func__, item_ptr);
		
		//add info into out param
		sprintf(out_ptr, "%s,", item_ptr);
		out_ptr += strlen(out_ptr);
	}

	return strlen(out);
}

int eng_ResponseCCED(eng_at_response* content, char *out)
{
	int i,item_len,err;
	int data1,data2, data3;
	int n=content->content[SEND_DATA_START_POS-1];
	char item[256];
	char *item_ptr, *out_ptr;
	char* ptr = &content->content[SEND_DATA_START_POS];

	out_ptr = out;

	item_len=*ptr;

	ptr++;
	memset(item, 0, sizeof(item));
	snprintf(item, item_len+1, "%s", ptr);

	//parse int data in item string
	item_ptr = item;

	err=at_tok_start(&item_ptr);

	//add info into out param
	sprintf(out_ptr, "%s",item_ptr);
	out_ptr += strlen(out_ptr);


	return strlen(out);
}

int eng_ResponsePLMN(eng_at_response* content, char *out)
{
	int i,item_len,err;
	int data1,data2, data3;
	int n=content->content[SEND_DATA_START_POS-1];
	char item[256];
	char *item_ptr, *out_ptr;
	char* ptr = &content->content[SEND_DATA_START_POS];

	ENG_LOG("%s: item number=%d\n", __FUNCTION__, n);
	out_ptr = out;

	for(i=0; i<n; i++) {
		item_len=*ptr;

		ptr++;
		memset(item, 0, sizeof(item));
		snprintf(item, item_len+1, "%s",ptr);
		ptr += item_len;
		//parse int data in item string
		item_ptr = item;
		ENG_LOG("%s: %s",__func__, item_ptr);
		
		//add info into out param
		sprintf(out_ptr, "%s \r\n", item_ptr);

        ENG_LOG("%s: out_ptr=%s \n", __FUNCTION__, out_ptr);
		out_ptr += strlen(out_ptr);
	}

	return strlen(out);
}
