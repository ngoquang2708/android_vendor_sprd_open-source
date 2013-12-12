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

/******************************************************************************
 *
 *  Filename:      bt_vendor_sprd.c
 *
 *  Description:   SPRD vendor specific library implementation
 *
 ******************************************************************************/

#define LOG_TAG "bt_vendor"

#include <utils/Log.h>
#include <fcntl.h>
#include <termios.h>
#include "bt_vendor_sprd.h"
#include "userial_vendor.h"
/******************************************************************************
**  Externs
******************************************************************************/
extern int hw_config(int nState);

extern int is_hw_ready();

/******************************************************************************
**  Variables
******************************************************************************/
int s_bt_fd = -1;
bt_hci_transport_device_type bt_hci_transport_device;

bt_vendor_callbacks_t *bt_vendor_cbacks = NULL;
uint8_t vnd_local_bd_addr[6]={0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

#if (HW_NEED_END_WITH_HCI_RESET == TRUE)
void hw_epilog_process(void);
#endif


/******************************************************************************
**  Local type definitions
******************************************************************************/


/******************************************************************************
**  Functions
******************************************************************************/
int sprd_config_init(int fd, char *bdaddr, struct termios *ti);
/*****************************************************************************
**
**   BLUETOOTH VENDOR INTERFACE LIBRARY FUNCTIONS
**
*****************************************************************************/

static int init(const bt_vendor_callbacks_t* p_cb, unsigned char *local_bdaddr)
{
    ALOGI("bt-vendor : init");

    if (p_cb == NULL)
    {
        ALOGE("init failed with no user callbacks!");
        return -1;
    }

    //userial_vendor_init();
    //upio_init();

    //vnd_load_conf(VENDOR_LIB_CONF_FILE);

    /* store reference to user callbacks */
    bt_vendor_cbacks = (bt_vendor_callbacks_t *) p_cb;

    /* This is handed over from the stack */
    memcpy(vnd_local_bd_addr, local_bdaddr, 6);

    return 0;
}


/** Requested operations */
static int op(bt_vendor_opcode_t opcode, void *param)
{
    int retval = 0;
    int nCnt = 0;
    int nState = -1;

    ALOGI("bt-vendor : op for %d", opcode);

    switch(opcode)
    {
        case BT_VND_OP_POWER_CTRL:
            {


		 ALOGI("bt-vendor : BT_VND_OP_POWER_CTRL");		
		  #if 0
		  nState = *(int *) param;
                retval = hw_config(nState);
                if(nState == BT_VND_PWR_ON
                   && retval == 0
                   && is_hw_ready() == TRUE)
                {
                    retval = 0;
                }
		  #endif			
            }
            break;

        case BT_VND_OP_FW_CFG:
            {
			ALOGI("bt-vendor : BT_VND_OP_FW_CFG");

			sprd_config_init(s_bt_fd,NULL,NULL);
			ALOGI("bt-vendor : eee");
                // call hciattach to initalize the stack
                if(bt_vendor_cbacks){
                   ALOGI("Bluetooth Firmware and smd is initialized");
                   bt_vendor_cbacks->fwcfg_cb(BT_VND_OP_RESULT_SUCCESS);
                }
                else{
                   ALOGI("Error : hci, smd initialization Error");
                   bt_vendor_cbacks->fwcfg_cb(BT_VND_OP_RESULT_FAIL);
                }
            }
            break;

        case BT_VND_OP_SCO_CFG:
            {
		ALOGI("bt-vendor : BT_VND_OP_SCO_CFG");						
                bt_vendor_cbacks->scocfg_cb(BT_VND_OP_RESULT_SUCCESS); //dummy
            }
            break;

        case BT_VND_OP_USERIAL_OPEN:
            {
		  int idx;
		  ALOGI("bt-vendor : BT_VND_OP_USERIAL_OPEN");
                if(bt_hci_init_transport(&s_bt_fd) != -1)
		  {
			int (*fd_array)[] = (int (*) []) param;
	
			for (idx=0; idx < CH_MAX; idx++)
                   {
                   		(*fd_array)[idx] = s_bt_fd;
			}
			ALOGI("bt-vendor : BT_VND_OP_USERIAL_OPEN ok");
			retval = 1;
		  }
                else 
		 {
		 	ALOGI("bt-vendor : BT_VND_OP_USERIAL_OPEN failed");
                    retval = -1;
                }
            }
            break;

        case BT_VND_OP_USERIAL_CLOSE:
            {
		   ALOGI("bt-vendor : BT_VND_OP_USERIAL_CLOSE");								
                 bt_hci_deinit_transport(s_bt_fd);
            }
            break;

        case BT_VND_OP_GET_LPM_IDLE_TIMEOUT:
            break;

        case BT_VND_OP_LPM_SET_MODE:
            {
                ALOGI("bt-vendor : BT_VND_OP_LPM_SET_MODE");				
                bt_vendor_cbacks->lpm_cb(BT_VND_OP_RESULT_SUCCESS); //dummy
            }
            break;

        case BT_VND_OP_LPM_WAKE_SET_STATE:
            ALOGI("bt-vendor : BT_VND_OP_LPM_WAKE_SET_STATE");			
            break;
        case BT_VND_OP_EPILOG:
            {
#if (HW_NEED_END_WITH_HCI_RESET == FALSE)
                if (bt_vendor_cbacks)
                {
                    bt_vendor_cbacks->epilog_cb(BT_VND_OP_RESULT_SUCCESS);
                }
#else
                hw_epilog_process();
#endif
            }
            break;
    }

    return retval;
}

/** Closes the interface */
static void cleanup( void )
{
    ALOGI("cleanup");

    //upio_cleanup();

    bt_vendor_cbacks = NULL;
}

// Entry point of DLib
const bt_vendor_interface_t BLUETOOTH_VENDOR_LIB_INTERFACE = {
    sizeof(bt_vendor_interface_t),
    init,
    op,
    cleanup
};

#define MAX_BT_TMP_PSKEY_FILE_LEN 2048

typedef unsigned int   UWORD32;
typedef unsigned short UWORD16;
typedef unsigned char  UWORD8;

#define down_bt_is_space(c)	(((c) == '\n') || ((c) == ',') || ((c) == '\r') || ((c) == ' ') || ((c) == '{') || ((c) == '}'))
#define down_bt_is_comma(c)	(((c) == ','))
#define down_bt_is_endc(c)	(((c) == '}')) // indicate end of data

/* Macros to swap byte order */
#define SWAP_BYTE_ORDER_WORD(val) ((((val) & 0x000000FF) << 24) + \
                                   (((val) & 0x0000FF00) << 8)  + \
                                   (((val) & 0x00FF0000) >> 8)   + \
                                   (((val) & 0xFF000000) >> 24))
#define INLINE static __inline

#ifndef LITTLE_ENDIAN
#define LITTLE_ENDIAN
#endif

INLINE UWORD32 convert_to_le(UWORD32 val)
{
#ifdef BIG_ENDIAN
    return SWAP_BYTE_ORDER_WORD(val);
#endif /* BIG_ENDIAN */

#ifdef LITTLE_ENDIAN
    return val;
#endif /* LITTLE_ENDIAN */
}

// pskey file structure default value
BT_PSKEY_CONFIG_T bt_para_setting={
5,
0,
0,
0,
0,
0x18cba80,
0x001f00,
0x1e,
{0x7a00,0x7600,0x7200,0x5200,0x2300,0x0300},
{0XCe418CFE,
 0Xd0418CFE,0Xd2438CFE,
 0Xd4438CFE,0xD6438CFE},
{0xFF, 0xFF, 0x8D, 0xFE, 0x9B, 0xFF, 0x79, 0x83,
  0xFF, 0xA7, 0xFF, 0x7F, 0x00, 0xE0, 0xF7, 0x3E},
{0x11, 0xFF, 0x0, 0x22, 0x2D, 0xAE},
0,
1,
5,
0x0e,
0xFFFFFFFF,
0x30,
0x3f,
0,
{0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000}
};



INLINE void hex_dump(UWORD8 *info, UWORD8 *str, UWORD32 len)
{
    if(str == NULL || len == 0)
        return;

    if(1)
    {
		UWORD32  i = 0;
		ALOGI("dump %s, len: %d; data:\n",info,len);
		for(i = 0; i<len; i++)
		{
			if(((UWORD8 *)str+i)==NULL)
				break;
			ALOGI("%02x ",*((UWORD8 *)str+i));
			if((i+1)%16 == 0)
				ALOGI("\n");
		}
		ALOGI("\n");
	}
}

static char *down_bt_strstrip(char *s)
{
    size_t size;

    size = strlen(s);
    if (!size)
        return s;

    while (*s && down_bt_is_space(*s))
        s++;

    return s;
}

// find first comma(char ',') then return the next comma'next space ptr.
static char *down_bt_skip_comma(char *s)
{
    size_t size;

    size = strlen(s);
    if (!size)
        return s;

    while(*s)
    {
        if(down_bt_is_comma(*s))
        {
            return ++s;
        }
        if(down_bt_is_endc(*s))
        {
            ALOGI("end of buff, str=%s\n",s);
            return s;
        }
        s++;
    }

    return s;
}


static int count_g_u32 = 0;
static int count_g_u16 = 0;
static int count_g_u8 = 0;

//#define get_one_item(buff)  (simple_strtoul(buff, NULL, 0))
#define get_one_item(buff)  (strtoul(buff, NULL, 16))

static int get_one_digit(UWORD8 *ptr, UWORD8 *data)
{
    int count = 0;

    //hex_dump("get_one_digit 10 ptr",ptr,10);
    if(*ptr=='0' && (*(ptr+1)=='x' || *(ptr+1)=='X'))
    {
        memcpy(data, "0x", 2);
        ptr += 2;
        data += 2;

        while(1)
        {
            if((*ptr >= '0' && *ptr <= '9') || (*ptr >= 'a' && *ptr <= 'f') || (*ptr >= 'A' && *ptr <= 'F'))
            {
                *data = *ptr;
                ALOGI("char:%c, %c\n", *ptr, *data);
                data++;
                ptr++;
                count++;
            }
            else
            {
                break;
            }
        }
    }
    else
    {
        while(1)
        {
            if(*ptr >= '0' && *ptr <= '9')
            {
                *data = *ptr;
                ALOGI("char:%c, %c\n", *ptr, *data);
                data++;
                ptr++;
                count++;
            }
            else
            {
                break;
            }
        }
    }

    return count;
}

static UWORD32 get_next_u32(UWORD8 **p_buff)
{
    UWORD32  val_u32  = 0;
    UWORD8 *ptr = 0;
    UWORD8 data[10];
    UWORD32 data_len = 0;
    ++count_g_u32;

    if(p_buff == NULL || *p_buff == NULL)
    {
        if(p_buff)
            ALOGI("%s: Error occur! *p_buff == null!\n",__FUNCTION__);
        else
            ALOGI("%s: Error occur! p_buff == null!\n",__FUNCTION__);
        return 0;
    }
    ptr = *p_buff;

    ptr = down_bt_strstrip(ptr);

    memset(data,'\0',sizeof(data));
    data_len = get_one_digit(ptr, data);


    ALOGI("get_one_item(data) = 0x%x\n",(UWORD32)get_one_item(data));
    val_u32 = get_one_item(data);


    ALOGI("%s: here:%x\n",__FUNCTION__,val_u32);

    ptr = down_bt_skip_comma(ptr);
    *p_buff = ptr;

    return val_u32;
}

static UWORD16 get_next_u16(UWORD8 **p_buff)
{
    UWORD32  val_u16  = 0;
    UWORD8 *ptr = 0;
    UWORD8 data[10];
    UWORD32 data_len = 0;
    ++count_g_u16;

    if(p_buff == NULL || *p_buff == NULL)
    {
        if(p_buff)
            ALOGI("%s: Error occur! *p_buff == null!\n",__FUNCTION__);
        else
            ALOGI("%s: Error occur! p_buff == null!\n",__FUNCTION__);
        return 0;
    }
    ptr = *p_buff;

    ptr = down_bt_strstrip(ptr);

    //ALOGI("before parser:%s;\n",ptr);

    memset(data,'\0',sizeof(data));
    data_len = get_one_digit(ptr, data);

    //hex_dump("get_one_digit ok", data, data_len);
/*
    if(!data_len)
    {
        ALOGI("get_one_digit error! ptr=%s\n",ptr);
        BUG_ON(1);
    }
    */

    ALOGI("get_one_item(data) = 0x%x\n",(UWORD16)get_one_item(data));
    val_u16 = get_one_item(data);

    ALOGI("%s: here:%x\n",__FUNCTION__,val_u16);

    ptr = down_bt_skip_comma(ptr);
    *p_buff = ptr;
    //ALOGI("after parser:%s;\n",ptr);

    return val_u16;
}


static UWORD8 get_next_u8(UWORD8 **p_buff)
{
    UWORD32  val_u8  = 0;
    UWORD8 *ptr = 0;
    UWORD8 data[10];
    UWORD32 data_len = 0;
    ++count_g_u8;

    if(p_buff == NULL || *p_buff == NULL)
    {
        if(p_buff)
            ALOGI("%s: Error occur! *p_buff == null!\n",__FUNCTION__);
        else
            ALOGI("%s: Error occur! p_buff == null!\n",__FUNCTION__);
        return 0;
    }
    ptr = *p_buff;

    ptr = down_bt_strstrip(ptr);

    //ALOGI("before parser:%s;\n",ptr);

    memset(data,'\0',sizeof(data));
    data_len = get_one_digit(ptr, data);

    //hex_dump("get_one_digit ok", data, data_len);
    /*
    if(!data_len)
    {
        ALOGI("get_one_digit error! ptr=%s\n",ptr);
        BUG_ON(1);
    }
    */

    ALOGI("get_one_item(data) = 0x%x\n",(UWORD8)get_one_item(data));
    val_u8 = get_one_item(data);

    ALOGI("%s: here:%x\n",__FUNCTION__,val_u8);

    ptr = down_bt_skip_comma(ptr);
    *p_buff = ptr;
    //ALOGI("after parser:%s;\n",ptr);

    return val_u8;
}

int parser_pskey_info(UWORD8 *buff, BT_PSKEY_CONFIG_T *p_params)
{
    UWORD8 *tmp_buff = buff;
    int i = 0;
    ALOGI("%s",__FUNCTION__);
    tmp_buff = strstr(tmp_buff, "}");
    //ALOGI("read 1 this: %s", tmp_buff);
    tmp_buff++;
    tmp_buff = strstr(tmp_buff, "{");
    //ALOGI("read 2 this: %s", tmp_buff);
    tmp_buff++;

    p_params->pskey_cmd = get_next_u8(&tmp_buff);
    p_params->g_dbg_source_sink_syn_test_data = get_next_u8(&tmp_buff);
    p_params->g_sys_sleep_in_standby_supported = get_next_u8(&tmp_buff);
    p_params->g_sys_sleep_master_supported = get_next_u8(&tmp_buff);
    p_params->g_sys_sleep_slave_supported = get_next_u8(&tmp_buff);

    p_params->default_ahb_clk = get_next_u32(&tmp_buff);
    p_params->device_class = get_next_u32(&tmp_buff);
    p_params->win_ext = get_next_u32(&tmp_buff);

    for(i=0; i<6; i++)
    {
        p_params->g_aGainValue[i] = get_next_u32(&tmp_buff);
    }

    for(i=0; i<5; i++)
    {
        p_params->g_aPowerValue[i] = get_next_u32(&tmp_buff);
    }

    for(i=0; i<16; i++)
    {
        p_params->feature_set[i] = get_next_u8(&tmp_buff);
    }

    for(i=0; i<6; i++)
    {
        p_params->device_addr[i] = get_next_u8(&tmp_buff);
    }

    p_params->g_sys_sco_transmit_mode = get_next_u8(&tmp_buff);
    p_params->g_sys_uart0_communication_supported = get_next_u8(&tmp_buff);
    p_params->edr_tx_edr_delay = get_next_u8(&tmp_buff);
    p_params->edr_rx_edr_delay = get_next_u8(&tmp_buff);

    p_params->g_PrintLevel = get_next_u32(&tmp_buff);

    p_params->uart_rx_watermark = get_next_u16(&tmp_buff);
    p_params->uart_flow_control_thld = get_next_u16(&tmp_buff);
    p_params->comp_id = get_next_u32(&tmp_buff);

    for(i=0; i<10; i++)
    {
        p_params->reserved[i] = get_next_u32(&tmp_buff);
    }

    ALOGI("leave out tmp_buff=%s\n", tmp_buff);
    //hex_dump("p_params",(void *)p_params,sizeof(BT_PSKEY_CONFIG_T));

    return 0;
}


int get_pskey_from_file(BT_PSKEY_CONFIG_T *pskey_file)
{
    int bt_flag=0;
    char* bt_ptr = NULL;
    int bt_pskey_len = 0;
    unsigned char *tmp_buff = NULL;
    int ret = 0;
    int fd  = 0;
    int sz  = 0;

    ALOGI("%s",__FUNCTION__);

    tmp_buff = (unsigned char *)malloc(MAX_BT_TMP_PSKEY_FILE_LEN+1);
	if(!tmp_buff)
	{
	    ALOGI("%s: unable to alloc tmp_buff! \n", __FUNCTION__);
        return -1;
    }

    memset(tmp_buff, 0x0, MAX_BT_TMP_PSKEY_FILE_LEN+1);

    // read pskey_bt.txt from file
    fd = open(BT_PSKEY_STRUCT_FILE, O_RDONLY, 0644);
    ALOGI("open bt pskey structure file fd=%d", fd);
	if(fd > 0) {
		sz = read(fd, tmp_buff, MAX_BT_TMP_PSKEY_FILE_LEN);
        ALOGI("buf from pskey file:%d", sz);
        //ALOGI("buf = %s", tmp_buff);
		close(fd);
	}
    else {
        ALOGI("open BT_PSKEY_STRUCT_FILE fail");
        return -1;
    }

    // parser file struct
    ret = parser_pskey_info(tmp_buff, pskey_file);
    if(ret < 0)
    {
        ALOGI("parse pskey info fail");
        goto parse_pskey_error;
    }

    ALOGI("parse pskey success, tmp_buff=%p", tmp_buff);

    // free tmp_buff
    if(tmp_buff)
        free(tmp_buff);
    tmp_buff = NULL;

    return 0;

parse_pskey_error:
    if(tmp_buff){
        free(tmp_buff);
        tmp_buff = NULL;
    }

    return -1;
}

//******************create bt addr***********************
static void mac_rand(char *btmac)
{
	int fd,i, j, k;
	char buf[80];
	char *ptr;
	int size = 0,counter = 80;
	unsigned int randseed;

	ALOGI("mac_rand");

	memset(buf, 0, sizeof(buf));

	if(access(BT_MAC_FILE, F_OK) == 0) {
		ALOGI("%s: %s exists",__FUNCTION__, BT_MAC_FILE);
		fd = open(BT_MAC_FILE, O_RDWR);
		if(fd>=0) {
			size = read(fd, buf, sizeof(buf));
			ALOGI("%s: read %s %s, size=%d",__FUNCTION__, BT_MAC_FILE, buf, size);
			if(size == BT_RAND_MAC_LENGTH){
				ALOGI("bt mac already exists, no need to random it");
				strcpy(btmac, buf);
				close(fd);
				ALOGI("%s: read btmac=%s",__FUNCTION__, btmac);
				return;
			}
			close(fd);
		}
	}
      ALOGI("%s: there is no bt mac, random it", __FUNCTION__);
	k=0;
	for(i=0; i<counter; i++)
		k += buf[i];

	//rand seed
	randseed = (unsigned int) time(NULL) + k*fd*counter + buf[counter-2];
	ALOGI("%s: randseed=%d",__FUNCTION__, randseed);
	srand(randseed);

	//FOR BT
	i=rand(); j=rand();
	ALOGI("%s:  rand i=0x%x, j=0x%x",__FUNCTION__, i,j);
	sprintf(btmac, "00:%02x:%02x:%02x:%02x:%02x", \
							(unsigned char)((i>>8)&0xFF), \
							(unsigned char)((i>>16)&0xFF), \
							(unsigned char)((j)&0xFF), \
							(unsigned char)((j>>8)&0xFF), \
							(unsigned char)((j>>16)&0xFF));
}


static void write_btmac2file(char *btmac)
{
	int fd;
	fd = open(BT_MAC_FILE, O_CREAT|O_RDWR|O_TRUNC);
	if(fd > 0) {
		chmod(BT_MAC_FILE,0666);
		write(fd, btmac, strlen(btmac));
		close(fd);
	}
}
uint8 ConvertHexToBin(
			uint8        *hex_ptr,     // in: the hexadecimal format string
			uint16       length,       // in: the length of hexadecimal string
			uint8        *bin_ptr      // out: pointer to the binary format string
			){
    uint8        *dest_ptr = bin_ptr;
    uint32        i = 0;
    uint8        ch;

    for(i=0; i<length; i+=2){
		    // the bit 8,7,6,5
				ch = hex_ptr[i];
				// digital 0 - 9
				if (ch >= '0' && ch <= '9')
				    *dest_ptr =(uint8)((ch - '0') << 4);
				// a - f
				else if (ch >= 'a' && ch <= 'f')
				    *dest_ptr = (uint8)((ch - 'a' + 10) << 4);
				// A - F
				else if (ch >= 'A' && ch <= 'F')
				    *dest_ptr = (uint8)((ch -'A' + 10) << 4);
				else{
				    return 0;
				}

				// the bit 1,2,3,4
				ch = hex_ptr[i+1];
				// digtial 0 - 9
				if (ch >= '0' && ch <= '9')
				    *dest_ptr |= (uint8)(ch - '0');
				// a - f
				else if (ch >= 'a' && ch <= 'f')
				    *dest_ptr |= (uint8)(ch - 'a' + 10);
				// A - F
				else if (ch >= 'A' && ch <= 'F')
				    *dest_ptr |= (uint8)(ch -'A' + 10);
				else{
			            return 0;
				}

				dest_ptr++;
	  }

    return 1;
}
//******************create bt addr end***********************

int sprd_config_init(int fd, char *bdaddr, struct termios *ti)
{
	int i,psk_fd,fd_btaddr,ret = 0,r,size=0,read_btmac=0;
	unsigned char resp[30];
	BT_PSKEY_CONFIG_T bt_para_tmp;
	char bt_mac[30] = {0};
	char bt_mac_tmp[20] = {0};
	uint8_t bt_mac_bin[32]     = {0};

	ALOGI("init_sprd_config in \n");
/*
	mac_rand(bt_mac);
	ALOGI("bt random mac=%s",bt_mac);
	printf("bt_mac=%s\n",bt_mac);
	write_btmac2file(bt_mac);

*/
	if(access(BT_MAC_FILE, F_OK) == 0) {
		ALOGI("%s: %s exists",__FUNCTION__, BT_MAC_FILE);
		fd_btaddr = open(BT_MAC_FILE, O_RDWR);
		if(fd_btaddr>=0) {
			size = read(fd_btaddr, bt_mac, sizeof(bt_mac));
			ALOGI("%s: read %s %s, size=%d",__FUNCTION__, BT_MAC_FILE, bt_mac, size);
			if(size == BT_RAND_MAC_LENGTH){
						ALOGI("bt mac already exists, no need to random it");
						fprintf(stderr, "read btmac ok \n");
						read_btmac=1;
			}
			close(fd_btaddr);
		}
		fprintf(stderr, "read bt_addr_read end\n");
		for(i=0; i<6; i++){
				bt_mac_tmp[i*2] = bt_mac[3*(5-i)];
				bt_mac_tmp[i*2+1] = bt_mac[3*(5-i)+1];
		}
		ALOGI("====bt_mac_tmp=%s", bt_mac_tmp);
		printf("====bt_mac_tmp=%s\n", bt_mac_tmp);
		ConvertHexToBin(bt_mac_tmp, strlen(bt_mac_tmp), bt_mac_bin);
	}else{
		fprintf(stderr, "btmac.txt not exsit!\n");
		read_btmac=0;
	}

	/* Reset the BT Chip */

	memset(resp, 0, sizeof(resp));

	ret = get_pskey_from_file(&bt_para_tmp);
       if(ret != 0){
			ALOGI("get_pskey_from_file faill \n");
			/* Send command from hciattach*/
			if(read_btmac == 1){
				memcpy(bt_para_setting.device_addr, bt_mac_bin, sizeof(bt_para_setting.device_addr));
			}
			if (write(s_bt_fd, (char *)&bt_para_setting, sizeof(BT_PSKEY_CONFIG_T)) != sizeof(BT_PSKEY_CONFIG_T)) {
				ALOGI("Failed to write reset command\n");
				return -1;
			}
        }else{
			ALOGI("get_pskey_from_file ok \n");
			/* Send command from pskey_bt.txt*/
			if(read_btmac == 1){
				memcpy(bt_para_tmp.device_addr, bt_mac_bin, sizeof(bt_para_tmp.device_addr));
			}
			if (write(s_bt_fd, (char *)&bt_para_tmp, sizeof(BT_PSKEY_CONFIG_T)) != sizeof(BT_PSKEY_CONFIG_T)) {
				ALOGI("Failed to write reset command111\n");
				return -1;
			}else{
				ALOGI("get_pskey_from_file down ok\n");
			}
        }
		ALOGI("sprd_config_init ok 111 \n");

	while (1) {
		r = read(s_bt_fd, resp, 1);
		if (r <= 0)
			return -1;
		if (resp[0] == 0x05){
			ALOGI("read response ok \n");
			break;
		}
	}

       ALOGI("sprd_config_init ok \n");

	return 0;
}







