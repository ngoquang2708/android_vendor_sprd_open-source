#ifndef _ENG_HARDWARE_TEST_H
#define _ENG_HARDWARE_TEST_H

#ifdef __cplusplus
extern "C" {
#endif

#include	"eut_opt.h"

#define SOCKET_BUF_LEN	1024
#define OPEN_WIFI   1
#define CLOSE_WIFI  0
#define OPEN_BT   1
#define CLOSE_BT  0

#define TEST_OK	"test_ok"
#define TEST_ERROR	"test_err"

#define TYPE_OFFSET 1
#define CMD_OFFSET 7

#define BROADCOM_WIFI	1
#define BROADCOM_BT		2
#define CLOSE_SOCKET    3

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



#ifdef __cplusplus
}
#endif

#endif
