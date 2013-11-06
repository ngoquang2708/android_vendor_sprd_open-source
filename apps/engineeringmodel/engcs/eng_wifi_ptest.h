#ifndef SPRD_WIFI_PTEST_H__
#define SPRD_WIFI_PTEST_H__
/*
 * =====================================================================================
 *
 *       Filename:  eng_wifi_ptest.h
 *
 *    Description:  spreadtrum wifi production test
 *
 *        Version:  1.0
 *        Created:  10/06/2011 02:05:26 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Binary Yang (Binary), Binary.Yang@spreadtrum.com.cn
 *        Company:  © Copyright 2010 Spreadtrum Communications Inc.
 *
 * =====================================================================================
 */

#include	<stdint.h>
#include	<stddef.h>
#include	<sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif	
	
typedef intptr_t CsrIntptr;     /* Signed integer large enough to hold any pointer (ISO/IEC 9899:1999 7.18.1.4) */

/* Unsigned fixed width types */
typedef uint8_t CsrUint8;
typedef uint16_t CsrUint16;
typedef uint32_t CsrUint32;

/* Signed fixed width types */
typedef int8_t CsrInt8;
typedef int16_t CsrInt16;
typedef int32_t CsrInt32;

/* Boolean */
typedef CsrUint8 CsrBool;


/* MAC address */
typedef struct
{
    CsrUint8 a[6];
} CsrWifiMacAddress;

typedef CsrUint8 CsrWifiPtestPreamble;


typedef struct ptest_cmd{
	CsrUint16                       type;    
	CsrUint16                       band;             
        CsrUint16                       channel;          
        CsrUint16                       sFactor;  
	union{	
		struct {
        		CsrUint16                       frequency;        
        		CsrInt16                        frequencyOffset;  
        		CsrUint16                       amplitude;  
		}ptest_cw;
		struct {
        		CsrUint16                       rate;             
        		CsrUint16                       powerLevel;       
        		CsrUint16                       length;           
        		CsrBool                         enable11bCsTestMode;
        		CsrUint32                       interval;         
        		CsrWifiMacAddress               destMacAddr;      
        		CsrWifiPtestPreamble            preamble;         
		}ptest_tx;
		struct {
        		CsrUint16                       frequency;        
        		CsrBool                         filteringEnable;  
		}ptest_rx;
	};
}PTEST_CMD_T;


typedef struct ptest_res{
    CsrUint16                       type;
    CsrUint8                        result;           
    CsrUint32                       goodFrames[28];   
    CsrUint32                       total;            
    CsrUint32                       totalGood;        
    CsrInt32                        interval;         
    CsrInt32                        freqErrAv;        
    CsrInt32                        rssiAv;           
    CsrInt32                        snrAv;            
}PTEST_RES_T;


typedef enum{
	WIFI_PTEST_INIT,
	WIFI_PTEST_CW,
	WIFI_PTEST_TX,
	WIFI_PTEST_RX,
	WIFI_PTEST_DEINIT,
}WIFI_PTEST_CMD_E;

PTEST_RES_T *wifi_ptest_init(PTEST_CMD_T *ptest);
PTEST_RES_T *wifi_ptest_cw(PTEST_CMD_T *ptest);
PTEST_RES_T *wifi_ptest_tx(PTEST_CMD_T *ptest);
PTEST_RES_T *wifi_ptest_rx(PTEST_CMD_T *ptest);
PTEST_RES_T *wifi_ptest_deinit(PTEST_CMD_T *ptest);
void set_value(int val);
int bt_ptest_start(void);
int bt_ptest_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* SPRD_WIFI_PTEST_H__ */



		


