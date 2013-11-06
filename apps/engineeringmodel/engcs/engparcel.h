

#ifndef __ENG_PARCEL_H__
#define __ENG_PARCEL_H__

typedef struct eng_at_response_t {
	char* content;
	int content_len;
	int item_num;
}eng_at_response;
 
typedef struct eng_at_sptest_t {
	int type;
	int value;
}eng_at_sptest;


typedef struct eng_at_sppsrate_t {
	int type;
	int ul;
	int dl;
}eng_at_sppsrate;


typedef struct eng_at_spfreq_t {
	int param_num;
	int operation;
	int index;
	int freq;
	int cell_id1;
	int cell_id2;
	int cell_id3;
}eng_at_spfreq;

typedef struct eng_at_spaute_t{
	int enable;
	int delayms;
	int dev;
	int volume;
}eng_at_spaute;

typedef struct eng_at_spdgcnum_t{
	int num;
}eng_at_spdgcnum;

typedef struct eng_at_spdgcinfo_t{
	int index;
	int band;
	int arfcn;
	int bsic;
}eng_at_spdgcinfo;

typedef struct eng_at_grrtdncell_t{
	int param_num;
	int index;
	int arfcn;
	int cpi1;
	int cpi2;
	int cpi3;
	int cpi4;
}eng_at_grrtdncell;
 
typedef struct eng_at_pccotdcell_t{
	int arfcn;
	int cell_id;
}eng_at_pccotdcell;

typedef struct eng_at_l1param_t{
	int param_num;
	int index;
	int value;
}eng_at_l1param;

typedef struct eng_at_trrparam_t{
	int param_num;
	int index;
	int value;
}eng_at_trrparam;

typedef struct eng_at_rrdmparam_t{
	int param_num;
	int index;
	int value;
}eng_at_rrdmparam;

typedef struct eng_at_smtimer_t{
	int param_num;
	int value;
}eng_at_smtimer;

typedef struct eng_at_ttrdcfeparam_t{
	int param_num;
	int index;
	int value;
}eng_at_ttrdcfeparam;

typedef struct eng_at_sdataconf_t{
	int param_num;
	int id;
	char type[16];
	char ip[64];
	int server_port;
	int self_port;
}eng_at_sdataconf;

typedef struct eng_at_mbau_t{
	int param_num;
	char rend[33];
	char autn[33];
}eng_at_mbau;

typedef struct eng_at_nohandle_t{
	char cmd[1024];
}eng_at_nohandle;

typedef struct eng_at_sysinfo_t{
	int data1;
	int data2;
	int data3;
	int data4;
}eng_at_sysinfo;

typedef struct eng_at_pdpactive_t{
	int param_num;
	int param1;
	int param2;
}eng_at_pdpactive;


typedef struct eng_at_spgresdata_t{
	int param_num;
	int param1;
	int param2;
	char param3[256];
}eng_at_spgresdata;

typedef struct eng_at_camm_t{
	int param_num;
	char param1[256];
	char param2[256];
}eng_at_camm;

typedef struct eng_at_cops_t{
	int param_num;
	int param1;
	int param2;
	char param3[256];
	int param4;
}eng_at_cops;

typedef struct eng_at_crsm_t{
	int param_num;
	int whatHandler;
	int fileID;
	int p1,p2,p3;
	char data[128];
	char others[16];
}eng_at_crsm;

typedef struct eng_at_sgmr_t{
	int param_num;
	int dual_sys;
	int op;
	int type;
	char str[256];
}eng_at_sgmr;

void eng_init_array(void);
void eng_clear_array(void);
void eng_push_array(char *data, unsigned char len);
void eng_push_finish(void);
char* eng_pop_array(unsigned char* len);
void  eng_ResponseOneInt(char *data, int *value);
void eng_ResponseCurrentBand(char *data, int length, int *band);
void eng_ResponseGetArmLog(char *data, int length, int *on_off);
void eng_ResponseGetAutoAnswer(char *data, int length, int *on_off) ;
int eng_ResponseGetSpPsrate(eng_at_response* content, char *out);
int eng_ResponseSPTest(eng_at_response* content, char *out) ;
int eng_ResponseSPFRQ(eng_at_response* content, char *out);
int eng_ResponseGetSPDGCNUM(eng_at_response* content, char *out);
int eng_ResponseGetSPDGCINFO(eng_at_response* content, char *out);
int eng_ResponseGetSPL1(eng_at_response* content, char *out) ;
int eng_ResponseSPID(eng_at_response* content, char *out) ;
int eng_ResponseL1PARAM(eng_at_response* content, char *out);
int eng_ResponseTRRPARAM(eng_at_response* content, char *out);
int eng_ResponseTDMEASSWTH(eng_at_response* content, char *out);
int eng_ResponseGetTDMEASSWTH(eng_at_response* content, char *out) ;
int eng_ResponseRRDMPARAM(eng_at_response* content, char *out);
int eng_ResponseSMTIMER(eng_at_response* content, char *out);
int eng_ResponseTRRDCFEPARAM(eng_at_response* content, char *out);
int eng_ResponseCIMI(eng_at_response* content, char *out);
int eng_ResponseMBCELLID(eng_at_response* content, char *out);
int eng_ResponseMBAU(eng_at_response* content, char *out);
int eng_ResponseEUICC(eng_at_response* content, char *out);
int eng_ResponseCGREG(eng_at_response* content, char *out);
void eng_ResponseCurrentAttach(char *data, int length, int *attach);
int eng_ResponseSYSINFO(eng_at_response* content, char *out);
int eng_ResponseHVER(eng_at_response* content, char *out);
int eng_ResponseGetSYSCONFIG(eng_at_response* content, char *out);
int eng_ResponseSPVER(eng_at_response* content, char *out);
int eng_ResponseSADC(eng_at_response* content, char *out);
int eng_ResponseCGMR(eng_at_response* content, char *out);
int eng_ResponseGETUPLMN(eng_at_response* content, char *out);
int eng_ResponseGETUPLMNLEN(eng_at_response* content, char *out);
int eng_ResponseSETUPLMN(eng_at_response* content, char *out);
int eng_ResponseCCED(eng_at_response* content, char *out);
int eng_ResponsePLMN(eng_at_response* content, char *out);
char* eng_pop_large_string();
void eng_push_large_string(char *data, unsigned char len);

#endif

