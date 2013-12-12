/*
 * Copyright 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef BT_VENDOR_SPRD_H
#define BT_VENDOR_SPRD_H

#include "bt_vendor_lib.h"
#include "vnd_buildcfg.h"
#include "userial_vendor.h"
#include "utils.h"

#ifndef FALSE
#define FALSE  0
#endif

#ifndef TRUE
#define TRUE   (!FALSE)
#endif

// File discriptor using Transport
extern int fd;

extern bt_hci_transport_device_type bt_hci_transport_device;

extern bt_vendor_callbacks_t *bt_vendor_cbacks;
/* HW_NEED_END_WITH_HCI_RESET

    code implementation of sending a HCI_RESET command during the epilog
    process. It calls back to the callers after command complete of HCI_RESET
    is received.

    Default TRUE .
*/
#ifndef HW_NEED_END_WITH_HCI_RESET
#define HW_NEED_END_WITH_HCI_RESET TRUE
#endif

#define HCI_RESET  0x0C03
#define HCI_CMD_PREAMBLE_SIZE 3
#define HCI_EVT_CMD_CMPL_STATUS_RET_BYTE   5
#define HCI_EVT_CMD_CMPL_OPCODE        3

#define NUM_OF_DEVS 1


 #define MAC_ERROR    "FF:FF:FF:FF:FF:FF"  
 #define BT_MAC_FILE  "/data/btmac.txt"  
 #define GET_BTMAC_ATCMD "AT+SNVM=0,401"  
 #define GET_BTPSKEY_ATCMD "AT+SNVM=0,415"  
 #define SET_BTMAC_ATCMD  "AT+SNVM=1,401"  
 #define BT_RAND_MAC_LENGTH   17  
   
 // used to store BT pskey structure and default values  
 #define BT_PSKEY_STRUCT_FILE "/system/lib/modules/pskey_bt.txt"  
 #define BT_PSKEY_FILE  "/system/lib/modules/pskey_bt.txt"  
  
 typedef unsigned char uint8;  
 typedef unsigned int uint32;  
 typedef unsigned short uint16;  
 #define BT_ADDRESS_SIZE    6  




// add by longting.zhao for pskey NV
// pskey file structure
typedef struct SPRD_BT_PSKEY_INFO_T{
    uint8_t	pskey_cmd;//add h4 cmd 5 means pskey cmd
    uint8_t   g_dbg_source_sink_syn_test_data;
    uint8_t   g_sys_sleep_in_standby_supported;
    uint8_t   g_sys_sleep_master_supported;
    uint8_t   g_sys_sleep_slave_supported;
    uint32_t  default_ahb_clk;
    uint32_t  device_class;
    uint32_t  win_ext;
    uint32_t  g_aGainValue[6];
    uint32_t  g_aPowerValue[5];
    uint8_t   feature_set[16];
    uint8_t   device_addr[6];
    uint8_t  g_sys_sco_transmit_mode; //0: DMA 1: UART 2:SHAREMEM
    uint8_t  g_sys_uart0_communication_supported; //true use uart0, otherwise use uart1 for debug
    uint8_t edr_tx_edr_delay;
    uint8_t edr_rx_edr_delay;
    uint32_t g_PrintLevel;
    uint16_t uart_rx_watermark;
    uint16_t uart_flow_control_thld;
    uint32_t comp_id;
    uint32_t  reserved[10];
}BT_PSKEY_CONFIG_T;

#endif /* BT_VENDOR_SPRD_H */

