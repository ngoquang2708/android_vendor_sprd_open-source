/*
* Copyright (C) 2012 The Android Open Source Project
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

#include <utils/Log.h>
#include <fcntl.h>              /* low-level i/o */
#include <errno.h>
#include <sys/ioctl.h>

#include "sensor_cfg.h"
#include "sensor_drv_u.h"
#include "cmr_msg.h"
#include "isp_cali_interface.h"
#include "cmr_set.h"

#define SENSOR_ONE_I2C                    1
#define SENSOR_ZERO_I2C                   0
#define SENSOR_16_BITS_I2C                2
#define SENSOR_CHECK_STATUS_INTERVAL      50000

#define SENSOR_LOW_SIXTEEN_BIT            0xffff

#ifndef SCI_TRUE
#define SCI_TRUE                          1
#define SCI_FALSE                         0
#endif

#define SIGN_0                            0x73
#define SIGN_1                            0x69
#define SIGN_2                            0x67
#define SIGN_3                            0x6e
#define SENSOR_MSG_QUEUE_SIZE             10

#define SENSOR_DRV_CHECK_ZERO(a)                                   \
	do {                                                        \
		if (PNULL == a) {                                    \
			CMR_LOGE("Sensor_driver_u, zero pointer \n"); \
			return SENSOR_CTX_ERROR;                      \
		}                                                    \
	} while(0)

#define SENSOR_DRV_CHECK_ZERO_VOID(a)                              \
	do {                                                        \
		if (PNULL == a) {                                    \
			CMR_LOGE("Sensor_driver_u, zero pointer \n"); \
			return;                       \
		}                                                    \
	} while(0)

enum SENSOR_EVT_TYPE {
	SENSOR_EVT_INIT = CMR_EVT_SENSOR_BASE,
	SENSOR_EVT_SET_MODE,
	SENSOR_EVT_STREAM_ON,
	SENSOR_EVT_STREAM_OFF,
	SENSOR_EVT_AF_INIT,
	SENSOR_EVT_DEINIT,
	SENSOR_EVT_SET_MODE_DONE
};

struct sensor_drv_context {
	BOOLEAN                             sensor_init;
	BOOLEAN                             sensor_identified;
	BOOLEAN                             sensor_param_saved;
	uint8_t                             sensor_index[SENSOR_ID_MAX];
	uint16_t                            i2c_addr;
	int                                 fd_sensor;         /*sensor device id, used when sensor dev alive*/
	cmr_evt_cb                          sensor_event_cb;
	pthread_mutex_t                     cb_mutex;
	uint32_t                            is_calibration;
	pthread_t                           monitor_thread;
	uint32_t                            monitor_exit;
	uint32_t                            stream_on;
	SENSOR_INFO_T                       *sensor_list_ptr[SENSOR_ID_MAX];
	SENSOR_INFO_T                       *sensor_info_ptr;
	SENSOR_EXP_INFO_T                   sensor_exp_info;
	SENSOR_TYPE_E                       sensor_type;
	SENSOR_MODE_E                       sensor_mode[SENSOR_ID_MAX];
	SENSOR_REGISTER_INFO_T              sensor_register_info;
	uint32_t                            flash_mode;
	int                                 is_main_sensor;
	int                                 is_register_sensor;
	EXIF_SPEC_PIC_TAKING_COND_T         default_exif;
	pthread_t                           sensor_thread;
	uint32_t                            queue_handle;
	sem_t                               sensor_sync_sem;
	sem_t                               st_on_sem;
	sem_t                               st_off_sem;
	sem_t                               st_af_sem;
	sem_t                               st_setmode_sem;
	uint32_t                            exit_flag;
};
/**---------------------------------------------------------------------------*
 **                         Local Variables                                   *
 **---------------------------------------------------------------------------*/

LOCAL char                             dev_name[50] = "/dev/sprd_sensor";
LOCAL char                             cali_file_dir[64] = "/data/";
LOCAL struct sensor_drv_context        *s_p_sensor_cxt = PNULL;
/*item0: index, item1: new_address, item2: original address, item3: length*/
static uint32_t                        s_lnc_addr_bakup[8][4];


/**---------------------------------------------------------------------------*
 **                         Local Functions                                   *
 **---------------------------------------------------------------------------*/
LOCAL uint32_t _Sensor_InitDefaultExifInfo(void);
LOCAL int _Sensor_WaitSync(void);
LOCAL int _Sensor_SyncDone(void);
LOCAL int _Sensor_SetMode(uint32_t mode);
LOCAL int _Sensor_StreamOn(void);
LOCAL int _Sensor_StreamOff(void);
LOCAL int _Sensor_CreateThread(void);
LOCAL int _Sensor_KillThread(void);
LOCAL void* _Sensor_ThreadProc(void* data);
LOCAL int   _Sensor_CreateMonitorThread(void);
LOCAL int   _Sensor_KillMonitorThread(void);
LOCAL int _Sensor_AutoFocusInit(void);
LOCAL int _Sensor_SetId(SENSOR_ID_E sensor_id);

static int xioctl(int fd, int request, void * arg) {
	int r;
	r = ioctl(fd, request, arg);
	return r;
}

/* This function is to set power down */
LOCAL int _Sensor_Device_PowerDown(BOOLEAN power_level)
{
	int ret = SENSOR_SUCCESS;
	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);

	ret = xioctl(s_p_sensor_cxt->fd_sensor, SENSOR_IO_PD, &power_level);

	if (0 != ret)
	{
		CMR_LOGE("_Sensor_Device_PowerDown failed,  power_level = %d, ret=%d \n", power_level, ret);
		ret = -1;
	}

	return ret;
}

LOCAL int _Sensor_Device_SetVoltageMonitor(uint32_t vdd_value)
{
	int ret = SENSOR_SUCCESS;
	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);

	ret = xioctl(s_p_sensor_cxt->fd_sensor, SENSOR_IO_SET_CAMMOT, &vdd_value);
	if (0 != ret)
	{
		CMR_LOGE("_Sensor_Device_SetVoltageMonitor failed,  vdd_value = %d, ret=%d \n",
			vdd_value, ret);
		ret = -1;
	}

	return ret;
}

LOCAL int _Sensor_Device_SetVoltageAVDD(uint32_t vdd_value)
{
	int ret = SENSOR_SUCCESS;
	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);

	ret = xioctl(s_p_sensor_cxt->fd_sensor, SENSOR_IO_SET_AVDD, &vdd_value);
	if (0 != ret)
	{
		CMR_LOGE("_Sensor_Device_SetVoltageAVDD failed,  vdd_value = %d, ret=%d \n", vdd_value, ret);
		ret = -1;
	}

	return ret;
}

LOCAL int _Sensor_Device_SetVoltageDVDD(uint32_t vdd_value)
{
	int ret = SENSOR_SUCCESS;
	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);

	ret = xioctl(s_p_sensor_cxt->fd_sensor, SENSOR_IO_SET_DVDD, &vdd_value);
	if (0 != ret)
	{
		CMR_LOGE("_Sensor_Device_SetVoltageDVDD failed,  vdd_value = %d, ret=%d \n", vdd_value, ret);
		ret = -1;
	}

	return ret;
}

LOCAL int _Sensor_Device_SetVoltageIOVDD(uint32_t vdd_value)
{
	int ret = SENSOR_SUCCESS;
	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);

	ret = xioctl(s_p_sensor_cxt->fd_sensor, SENSOR_IO_SET_IOVDD, &vdd_value);
	if (0 != ret)
	{
		CMR_LOGE("_Sensor_Device_SetVoltageIOVDD failed,  vdd_value = %d, ret=%d \n", vdd_value, ret);
		ret = -1;
	}

	return ret;
}

LOCAL int _Sensor_Device_read(uint8_t *buff, uint32_t size)
{
	int ret = SENSOR_SUCCESS;
	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);

	ret = read(s_p_sensor_cxt->fd_sensor, buff, size);

	return ret;
}

LOCAL int _Sensor_Device_Write(uint8_t *buff, uint32_t size)
{
	int ret = SENSOR_SUCCESS;
	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);

	ret = write(s_p_sensor_cxt->fd_sensor, buff, size);

	if(0 != ret)
	{
		CMR_LOGE("_Sensor_Device_Write failed,  buff[0] = %d, size=%d, ret=%d \n", buff[0], size, ret);
		ret = -1;
	}

	return ret;
}


LOCAL int _Sensor_Device_SetMCLK(uint32_t mclk)
{
	int ret = SENSOR_SUCCESS;
	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);

	ret = xioctl(s_p_sensor_cxt->fd_sensor, SENSOR_IO_SET_MCLK, &mclk);

	if (0 != ret)
	{
		CMR_LOGE("_Sensor_Device_SetMCLK failed,  mclk = %d, ret = %d  \n", mclk, ret);
		ret = -1;
	}

	return ret;
}

LOCAL int _Sensor_Device_Reset(uint32_t *reset_val)
{
	int ret = SENSOR_SUCCESS;
	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);

	CMR_LOGV("level %d, width %d",reset_val[0],reset_val[1]);

	ret = xioctl(s_p_sensor_cxt->fd_sensor, SENSOR_IO_RST, reset_val);
	if (ret) {
		ret = -1;
	}

	return ret;
}

LOCAL int _Sensor_Device_I2CInit(uint32_t senor_id)
{
	int ret = SENSOR_SUCCESS;
	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);

	CMR_LOGV("_Sensor_Device_I2CInit in");
	ret = xioctl(s_p_sensor_cxt->fd_sensor, SENSOR_IO_I2C_INIT, &senor_id);
	if (0 != ret)
	{
		CMR_LOGE("_Sensor_Device_I2CInit failed,  senor_id = %d, ret=%d", senor_id, ret);
		ret = -1;
	}
	CMR_LOGV("_Sensor_Device_I2CInit out");
	return ret;
}

LOCAL int _Sensor_Device_I2CDeInit(uint32_t senor_id)
{
	int ret = SENSOR_SUCCESS;
	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);

	CMR_LOGV("_Sensor_Device_I2CDeInit in \n");
	ret = xioctl(s_p_sensor_cxt->fd_sensor, SENSOR_IO_I2C_DEINIT, &senor_id);
	if (0 != ret)
	{
		CMR_LOGE("_Sensor_Device_I2CDeInit failed,  senor_id = %d, ret=%d \n", senor_id, ret);
		ret = -1;
	}
	CMR_LOGV("_Sensor_Device_I2CDeInit out \n");
	return ret;
}


LOCAL int _Sensor_Device_ResetLevel(uint32_t level)
{
	int ret = SENSOR_SUCCESS;
	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);

	ret = xioctl(s_p_sensor_cxt->fd_sensor, SENSOR_IO_RST_LEVEL, &level);
	if (0 != ret)
	{
		CMR_LOGE("_Sensor_Device_Reset failed,  level = %d, ret=%d \n", level, ret);
		ret = -1;
	}

	return ret;
}

LOCAL int _Sensor_Device_SetI2cAddr(uint16_t addr)
{
	int ret = SENSOR_SUCCESS;
	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);

	ret = xioctl(s_p_sensor_cxt->fd_sensor, SENSOR_IO_I2C_ADDR, &addr);
	if (0 != ret)
	{
		CMR_LOGE("_Sensor_Device_SetI2cAddr failed,  addr = 0x%x, ret=%d \n", addr, ret);
		ret = -1;
	}

	return ret;
}

LOCAL int _Sensor_Device_ReadReg(SENSOR_REG_BITS_T_PTR reg)
{
	int ret = SENSOR_SUCCESS;
	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);

	ret = xioctl(s_p_sensor_cxt->fd_sensor, SENSOR_IO_I2C_READ, reg);
	if (0 != ret)
	{
		CMR_LOGE("_Sensor_Device_ReadReg failed,  addr = 0x%x, value=%x, bit=%d, ret=%d \n", reg->reg_addr, reg->reg_value, reg->reg_bits, ret);
		ret = -1;
	}

	return ret;
}

LOCAL int _Sensor_Device_WriteReg(SENSOR_REG_BITS_T_PTR reg)
{
	int ret = SENSOR_SUCCESS;
	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);

	ret = xioctl(s_p_sensor_cxt->fd_sensor, SENSOR_IO_I2C_WRITE, reg);
	if (0 != ret)
	{
		CMR_LOGE("_Sensor_Device_WriteReg failed,  addr = 0x%x, value=%x, bit=%d, ret=%d \n", reg->reg_addr, reg->reg_value, reg->reg_bits, ret);
		ret = -1;
	}

	return ret;
}

LOCAL int _Sensor_Device_SetFlash(uint32_t flash_mode)
{
	int ret = SENSOR_SUCCESS;
	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);

	ret = xioctl(s_p_sensor_cxt->fd_sensor, SENSOR_IO_SET_FLASH, &flash_mode);
	if (0 != ret)
	{
		CMR_LOGE("_Sensor_Device_SetFlash failed,   flash_mode=%d \n",flash_mode);
		ret = -1;
	}

	return ret;
}

int _Sensor_Device_WriteRegTab(SENSOR_REG_TAB_PTR reg_tab)
{
	int ret = SENSOR_SUCCESS;
	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);

	ret = xioctl(s_p_sensor_cxt->fd_sensor, SENSOR_IO_I2C_WRITE_REGS, reg_tab);
	if (0 != ret)
	{
		CMR_LOGE("_Sensor_Device_SetRegTab failed,  ptr=%x, count=%d, bits=%d, burst=%d \n",
			(uint32_t)reg_tab->sensor_reg_tab_ptr, reg_tab->reg_count, reg_tab->reg_bits, reg_tab->burst_mode);

		ret = -1;
	}

	return ret;
}

LOCAL int _Sensor_Device_SetI2CClock(uint32_t clock)
{
	int ret = SENSOR_SUCCESS;
	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);

	ret = xioctl(s_p_sensor_cxt->fd_sensor, SENSOR_IO_SET_I2CCLOCK, &clock);
	if (0 != ret)
	{
		CMR_LOGE("_Sensor_Device_SetI2CClock failed,  clock = %d, ret=%d \n", clock, ret);
		ret = -1;
	}

	return ret;
}

LOCAL int Sensor_SetI2CClock(void)
{
	uint32_t freq;
	uint32_t clock;
	int ret;
	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);

	if (PNULL == s_p_sensor_cxt->sensor_info_ptr) {
		CMR_LOGE("Sensor_SetI2CClock: No sensor info \n");
		return -1;
	}

	freq = s_p_sensor_cxt->sensor_info_ptr->reg_addr_value_bits & SENSOR_I2C_CLOCK_MASK;

	switch(freq)
	{
		case SENSOR_I2C_FREQ_20:
			clock = 20000;
			break;

		case SENSOR_I2C_FREQ_50:
			clock = 50000;
			break;

		case SENSOR_I2C_FREQ_100:
			clock = 100000;
			break;

		case SENSOR_I2C_FREQ_200:
			clock = 200000;
			break;

		case SENSOR_I2C_FREQ_400:
			clock = 400000;
			break;

		default:
			clock = 100000;
			CMR_LOGV("Sensor_SetI2CClock: no valid freq, set clock to 100k \n");
			break;
	}

	CMR_LOGV("Sensor_SetI2CClock: clock = %d \n", clock);

	ret = _Sensor_Device_SetI2CClock(clock);

	return ret;
}

LOCAL int _Sensor_Device_I2CWrite(SENSOR_I2C_T_PTR i2c_tab)
{
	int ret = SENSOR_SUCCESS;
	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);

	ret = xioctl(s_p_sensor_cxt->fd_sensor, SENSOR_IO_I2C_WRITE_EXT, i2c_tab);
	if (0 != ret)
	{
		CMR_LOGE("_Sensor_Device_I2CWrite failed, slave_addr=0x%x, ptr=0x%x, count=%d\n",
			i2c_tab->slave_addr, (uint32_t)i2c_tab->i2c_data, i2c_tab->i2c_count);
		ret = -1;
	}

	return ret;
}

LOCAL int _Sensor_Device_GetFlashLevel(SENSOR_FLASH_LEVEL_T *level)
{
	int ret = SENSOR_SUCCESS;
	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);

	ret = xioctl(s_p_sensor_cxt->fd_sensor, SENSOR_IO_GET_FLASH_LEVEL, level);
	if (0 != ret)
	{
		CMR_LOGE("_Sensor_Device_GetFlashLevel failed, ret=%d \n",  ret);
		ret = -1;
	}

	return ret;
}

int Sensor_GetFlashLevel(SENSOR_FLASH_LEVEL_T *level)
{
	int ret = SENSOR_SUCCESS;

	ret = _Sensor_Device_GetFlashLevel(level);

	return ret;
}

int Sensor_WriteI2C(uint16_t slave_addr, uint8_t *cmd, uint16_t cmd_length)
{
	SENSOR_I2C_T i2c_tab;
	int ret = SENSOR_SUCCESS;

	i2c_tab.slave_addr 	= slave_addr;
	i2c_tab.i2c_data	= cmd;
	i2c_tab.i2c_count	= cmd_length;

	CMR_LOGV("Sensor_WriteI2C, slave_addr=0x%x, ptr=0x%x, count=%d\n",
		i2c_tab.slave_addr, (uint32_t)i2c_tab.i2c_data, i2c_tab.i2c_count);

	ret = _Sensor_Device_I2CWrite(&i2c_tab);

	return ret;
}

SENSOR_TYPE_E _Sensor_GetSensorType(void)
{
	if (PNULL == s_p_sensor_cxt) {
		CMR_LOGE("Sensor_driver_u, zero pointer \n");
		return SENSOR_TYPE_NONE;
	}
	return s_p_sensor_cxt->sensor_type;
}


void Sensor_Reset_EX(uint32_t power_down, uint32_t level)
{
	SENSOR_IOCTL_FUNC_PTR reset_func = 0;
	CMR_LOGV("Sensor_Reset_EX.\n");
	SENSOR_DRV_CHECK_ZERO_VOID(s_p_sensor_cxt);

	if (PNULL == s_p_sensor_cxt->sensor_info_ptr) {
		CMR_LOGE("Sensor_SetI2CClock: No sensor info \n");
		return;
	}

	reset_func = s_p_sensor_cxt->sensor_info_ptr->ioctl_func_tab_ptr->reset;
	Sensor_PowerDown(!power_down);
	if (NULL != reset_func) {
		reset_func(level);
	} else {
		Sensor_Reset(level);
	}
}

void Sensor_Reset(uint32_t level)
{
	int err = 0xff;
	uint32_t rst_val[2];
	SENSOR_IOCTL_FUNC_PTR reset_func;

	CMR_LOGV("in.\n");
	SENSOR_DRV_CHECK_ZERO_VOID(s_p_sensor_cxt);

	if (PNULL == s_p_sensor_cxt->sensor_info_ptr) {
		CMR_LOGE("Sensor_SetI2CClock: No sensor info \n");
		return;
	}

	reset_func = s_p_sensor_cxt->sensor_info_ptr->ioctl_func_tab_ptr->reset;

	if (PNULL != reset_func) {
		reset_func(level);
	} else {
		rst_val[0] = level;
		rst_val[1] = s_p_sensor_cxt->sensor_info_ptr->reset_pulse_width;
		if (rst_val[1] < SENSOR_RESET_PULSE_WIDTH_DEFAULT) {
			rst_val[1] = SENSOR_RESET_PULSE_WIDTH_DEFAULT;
		} else if (rst_val[1] > SENSOR_RESET_PULSE_WIDTH_MAX) {
			rst_val[1] = SENSOR_RESET_PULSE_WIDTH_MAX;
		}
		_Sensor_Device_Reset(rst_val);
	}
	CMR_LOGV("OK out.\n");
}


int Sensor_SetMCLK(uint32_t mclk)
{
	int ret;

	CMR_LOGV("Sensor_SetMCLK mclk = %d \n", mclk);

	ret = _Sensor_Device_SetMCLK(mclk);

	CMR_LOGV("Sensor_SetMCLK Done mclk = %d, ret = %d \n", mclk, ret );

	return ret;
}

int Sensor_SetVoltage(SENSOR_AVDD_VAL_E dvdd_val, SENSOR_AVDD_VAL_E avdd_val,
			SENSOR_AVDD_VAL_E iodd_val)
{

	int err = 0;

	err = _Sensor_Device_SetVoltageAVDD((uint32_t)avdd_val);
	if(SENSOR_SUCCESS != err)
		return err;

	err = _Sensor_Device_SetVoltageDVDD((uint32_t)dvdd_val);
	if(SENSOR_SUCCESS != err)
		return err;

	err = _Sensor_Device_SetVoltageIOVDD((uint32_t)iodd_val);
	if(SENSOR_SUCCESS != err)
		return err;

	CMR_LOGV("Sensor_SetVoltage avdd_val = %d,  dvdd_val=%d, iodd_val=%d \n", avdd_val, dvdd_val, iodd_val);

	return err;
}

int Sensor_SetAvddVoltage(SENSOR_AVDD_VAL_E vdd_val)
{
	int rtn  = SENSOR_SUCCESS;
	rtn = _Sensor_Device_SetVoltageAVDD((uint32_t)vdd_val);
	CMR_LOGV("Sensor_SetAvddVoltage vdd_val is %d, set result is =%d \n", vdd_val, rtn);
	return rtn;
}

int Sensor_SetDvddVoltage(SENSOR_AVDD_VAL_E vdd_val)
{
	int rtn  = SENSOR_SUCCESS;
	rtn = _Sensor_Device_SetVoltageDVDD((uint32_t)vdd_val);
	CMR_LOGV("Sensor_SetDvddVoltage vdd_val is %d, set result is =%d \n", vdd_val, rtn);
	return rtn;
}


int Sensor_SetIovddVoltage(SENSOR_AVDD_VAL_E vdd_val)
{
	int rtn  = SENSOR_SUCCESS;
	rtn = _Sensor_Device_SetVoltageIOVDD((uint32_t)vdd_val);
	CMR_LOGV("Sensor_SetIovddVoltage vdd_val is %d, set result is =%d \n", vdd_val, rtn);
	return rtn;
}

int Sensor_SetMonitorVoltage(SENSOR_AVDD_VAL_E vdd_val)
{
	int err = 0;

	err = _Sensor_Device_SetVoltageMonitor((uint32_t)vdd_val);
	CMR_LOGV("Sensor_SetMonitorVoltage vdd_val = %d \n", vdd_val);

	return err;
}

LOCAL void Sensor_PowerOn(BOOLEAN power_on)
{
	BOOLEAN power_down;
	SENSOR_AVDD_VAL_E dvdd_val;
	SENSOR_AVDD_VAL_E avdd_val;
	SENSOR_AVDD_VAL_E iovdd_val;
	SENSOR_IOCTL_FUNC_PTR power_func;
	uint32_t rst_lvl;
	SENSOR_DRV_CHECK_ZERO_VOID(s_p_sensor_cxt);

	if (PNULL == s_p_sensor_cxt->sensor_info_ptr) {
		CMR_LOGE("No sensor info!");
		return;
	}
	rst_lvl = s_p_sensor_cxt->sensor_info_ptr->reset_pulse_level;
	power_down = (BOOLEAN) s_p_sensor_cxt->sensor_info_ptr->power_down_level;
	dvdd_val = s_p_sensor_cxt->sensor_info_ptr->dvdd_val;
	avdd_val = s_p_sensor_cxt->sensor_info_ptr->avdd_val;
	iovdd_val = s_p_sensor_cxt->sensor_info_ptr->iovdd_val;
	power_func = s_p_sensor_cxt->sensor_info_ptr->ioctl_func_tab_ptr->power;

	CMR_LOGV("SENSOR: Sensor_PowerOn -> power_on = %d, power_down_level = %d, avdd_val = %d\n",
		power_on, power_down, avdd_val);

	if (PNULL != power_func) {
		power_func(power_on);
	} else {
		if (power_on) {
			Sensor_PowerDown(power_down);
			Sensor_SetVoltage(dvdd_val, avdd_val, iovdd_val);
			SENSOR_Sleep(10);
			Sensor_SetMCLK(SENSOR_DEFALUT_MCLK);
			SENSOR_Sleep(5);
			Sensor_PowerDown(!power_down);
			Sensor_Reset(rst_lvl);
		} else {
			Sensor_PowerDown(power_down);
			SENSOR_Sleep(20);
			Sensor_SetMCLK(SENSOR_DISABLE_MCLK);
			Sensor_SetVoltage(SENSOR_AVDD_CLOSED,
					  SENSOR_AVDD_CLOSED,
					  SENSOR_AVDD_CLOSED);
		}
	}
}


LOCAL void Sensor_PowerOn_Ex(uint32_t sensor_id)
{
	BOOLEAN power_down;
	SENSOR_AVDD_VAL_E dvdd_val;
	SENSOR_AVDD_VAL_E avdd_val;
	SENSOR_AVDD_VAL_E iovdd_val;
	SENSOR_IOCTL_FUNC_PTR power_func;
	uint32_t rst_lvl = 0;
	SENSOR_DRV_CHECK_ZERO_VOID(s_p_sensor_cxt);

	_Sensor_SetId((SENSOR_ID_E)sensor_id);

	s_p_sensor_cxt->sensor_info_ptr = s_p_sensor_cxt->sensor_list_ptr[sensor_id];

	power_down = (BOOLEAN) s_p_sensor_cxt->sensor_info_ptr->power_down_level;
	dvdd_val = s_p_sensor_cxt->sensor_info_ptr->dvdd_val;
	avdd_val = s_p_sensor_cxt->sensor_info_ptr->avdd_val;
	iovdd_val = s_p_sensor_cxt->sensor_info_ptr->iovdd_val;
	power_func = s_p_sensor_cxt->sensor_info_ptr->ioctl_func_tab_ptr->power;

	CMR_LOGV("SENSOR:  power_down_level = %d, avdd_val = %d\n", power_down, avdd_val);

	if (PNULL != power_func) {
		power_func(1);
	} else {
		Sensor_PowerDown(power_down);
		Sensor_SetVoltage(dvdd_val, avdd_val, iovdd_val);
		SENSOR_Sleep(10);
		Sensor_SetMCLK(SENSOR_DEFALUT_MCLK);
		SENSOR_Sleep(5);
	}
}

BOOLEAN Sensor_PowerDown(BOOLEAN power_level)
{
	SENSOR_IOCTL_FUNC_PTR entersleep_func = PNULL;

	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);
	if (PNULL == s_p_sensor_cxt->sensor_info_ptr) {
		CMR_LOGE("Sensor_SetI2CClock: No sensor info \n");
		return SENSOR_FAIL;
	}

	entersleep_func = s_p_sensor_cxt->sensor_info_ptr->ioctl_func_tab_ptr->enter_sleep;

	CMR_LOGV("SENSOR: Sensor_PowerDown -> main: power_down %d\n",
			power_level);

	if (entersleep_func) {
		entersleep_func(power_level);
		return SENSOR_SUCCESS;
	}

	if (-1 == _Sensor_Device_PowerDown(power_level))
		return SENSOR_FAIL;

	return SENSOR_SUCCESS;
}

BOOLEAN Sensor_SetResetLevel(BOOLEAN plus_level)
{
	if (-1 ==_Sensor_Device_ResetLevel((uint32_t)plus_level))
		return SENSOR_FAIL;

	return SENSOR_SUCCESS;
}

LOCAL void Sensor_SetExportInfo(SENSOR_EXP_INFO_T * exp_info_ptr)
{
	SENSOR_REG_TAB_INFO_T        *resolution_info_ptr = PNULL;
	SENSOR_TRIM_T_PTR            resolution_trim_ptr = PNULL;
	SENSOR_INFO_T                *sensor_info_ptr = PNULL;;
	SENSOR_VIDEO_INFO_T          *video_info_ptr = PNULL;
	uint32_t i = 0;

	CMR_LOGV("SENSOR: Sensor_SetExportInfo.\n");
	SENSOR_DRV_CHECK_ZERO_VOID(s_p_sensor_cxt);
	if (PNULL == s_p_sensor_cxt->sensor_info_ptr) {
		CMR_LOGE("SENSOR: sensor_info_ptr is null.\n");
		return;
	} else {
		sensor_info_ptr = s_p_sensor_cxt->sensor_info_ptr;
	}

	SENSOR_MEMSET(exp_info_ptr, 0x00, sizeof(SENSOR_EXP_INFO_T));
	exp_info_ptr->name= sensor_info_ptr->name;
	exp_info_ptr->image_format = sensor_info_ptr->image_format;
	exp_info_ptr->image_pattern = sensor_info_ptr->image_pattern;

	exp_info_ptr->pclk_polarity = (sensor_info_ptr->hw_signal_polarity & 0x01);  /*the high 3bit will be the phase(delay sel)*/
	exp_info_ptr->vsync_polarity =
		((sensor_info_ptr->hw_signal_polarity >> 2) & 0x1);
	exp_info_ptr->hsync_polarity =
		((sensor_info_ptr->hw_signal_polarity >> 4) & 0x1);
	exp_info_ptr->pclk_delay =
		((sensor_info_ptr->hw_signal_polarity >> 5) & 0x07);

	if (NULL!=sensor_info_ptr->raw_info_ptr) {
		exp_info_ptr->raw_info_ptr = (struct sensor_raw_info*)*sensor_info_ptr->raw_info_ptr;
	}

	exp_info_ptr->source_width_max = sensor_info_ptr->source_width_max;
	exp_info_ptr->source_height_max = sensor_info_ptr->source_height_max;

	exp_info_ptr->environment_mode = sensor_info_ptr->environment_mode;
	exp_info_ptr->image_effect = sensor_info_ptr->image_effect;
	exp_info_ptr->wb_mode = sensor_info_ptr->wb_mode;
	exp_info_ptr->step_count = sensor_info_ptr->step_count;

	exp_info_ptr->ext_info_ptr = sensor_info_ptr->ext_info_ptr;

	exp_info_ptr->preview_skip_num = sensor_info_ptr->preview_skip_num;
	exp_info_ptr->capture_skip_num = sensor_info_ptr->capture_skip_num;
	exp_info_ptr->preview_deci_num = sensor_info_ptr->preview_deci_num;
	exp_info_ptr->video_preview_deci_num =
	    sensor_info_ptr->video_preview_deci_num;

	exp_info_ptr->threshold_eb = sensor_info_ptr->threshold_eb;
	exp_info_ptr->threshold_mode = sensor_info_ptr->threshold_mode;
	exp_info_ptr->threshold_start = sensor_info_ptr->threshold_start;
	exp_info_ptr->threshold_end = sensor_info_ptr->threshold_end;

	exp_info_ptr->ioctl_func_ptr = sensor_info_ptr->ioctl_func_tab_ptr;
	if (PNULL != sensor_info_ptr->ioctl_func_tab_ptr->get_trim) {
		resolution_trim_ptr =
			(SENSOR_TRIM_T_PTR)sensor_info_ptr->ioctl_func_tab_ptr->get_trim(0x00);
	}
	for (i = SENSOR_MODE_COMMON_INIT; i < SENSOR_MODE_MAX; i++) {
		resolution_info_ptr =
		    &(sensor_info_ptr->resolution_tab_info_ptr[i]);

		if (SENSOR_IMAGE_FORMAT_JPEG == resolution_info_ptr->image_format) {
			exp_info_ptr->sensor_image_type = SENSOR_IMAGE_FORMAT_JPEG;
		}

		if ((PNULL != resolution_info_ptr->sensor_reg_tab_ptr)
		    || ((0x00 != resolution_info_ptr->width)
			&& (0x00 != resolution_info_ptr->width))) {
			exp_info_ptr->sensor_mode_info[i].mode = i;
			exp_info_ptr->sensor_mode_info[i].width =
			    resolution_info_ptr->width;
			exp_info_ptr->sensor_mode_info[i].height =
			    resolution_info_ptr->height;
			if ((PNULL != resolution_trim_ptr)
			    && (0x00 != resolution_trim_ptr[i].trim_width)
			    && (0x00 != resolution_trim_ptr[i].trim_height)) {
				exp_info_ptr->sensor_mode_info[i].trim_start_x =
				    resolution_trim_ptr[i].trim_start_x;
				exp_info_ptr->sensor_mode_info[i].trim_start_y =
				    resolution_trim_ptr[i].trim_start_y;
				exp_info_ptr->sensor_mode_info[i].trim_width =
				    resolution_trim_ptr[i].trim_width;
				exp_info_ptr->sensor_mode_info[i].trim_height =
				    resolution_trim_ptr[i].trim_height;
				exp_info_ptr->sensor_mode_info[i].line_time =
				    resolution_trim_ptr[i].line_time;
				exp_info_ptr->sensor_mode_info[i].pclk =
				    resolution_trim_ptr[i].pclk;
			} else {
				exp_info_ptr->sensor_mode_info[i].trim_start_x =
				    0x00;
				exp_info_ptr->sensor_mode_info[i].trim_start_y =
				    0x00;
				exp_info_ptr->sensor_mode_info[i].trim_width =
				    resolution_info_ptr->width;
				exp_info_ptr->sensor_mode_info[i].trim_height =
				    resolution_info_ptr->height;
			}

			if (SENSOR_IMAGE_FORMAT_MAX !=
			    sensor_info_ptr->image_format) {
				exp_info_ptr->sensor_mode_info[i].image_format =
				    sensor_info_ptr->image_format;
			} else {
				exp_info_ptr->sensor_mode_info[i].image_format =
				    resolution_info_ptr->image_format;
			}
		} else {
			exp_info_ptr->sensor_mode_info[i].mode =
			    SENSOR_MODE_MAX;
		}
		if (PNULL != sensor_info_ptr->video_tab_info_ptr) {
			video_info_ptr = &sensor_info_ptr->video_tab_info_ptr[i];
			if (PNULL != video_info_ptr) {
				memcpy((void*)&exp_info_ptr->sensor_video_info[i], (void*)video_info_ptr,sizeof(SENSOR_VIDEO_INFO_T));
			}
		}
	}
	exp_info_ptr->sensor_interface = sensor_info_ptr->sensor_interface;
	exp_info_ptr->change_setting_skip_num = sensor_info_ptr->change_setting_skip_num;
}

int32_t Sensor_WriteReg(uint16_t subaddr, uint16_t data)
{
	int32_t ret = -1;
	SENSOR_IOCTL_FUNC_PTR write_reg_func = PNULL;
	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);

	if (PNULL == s_p_sensor_cxt->sensor_info_ptr) {
		CMR_LOGE("SENSOR: sensor_info_ptr is null.\n");
		return 0xFF;
	}

	write_reg_func = s_p_sensor_cxt->sensor_info_ptr->ioctl_func_tab_ptr->write_reg;

	if (PNULL != write_reg_func) {
		if (SENSOR_OP_SUCCESS != write_reg_func((subaddr << S_BIT_4) + data)) {
			CMR_LOGV("SENSOR: IIC write : reg:0x%04x, val:0x%04x error\n",
					subaddr, data);
		}
	} else {

		SENSOR_REG_BITS_T reg;

		reg.reg_addr = subaddr;
		reg.reg_value = data;
		reg.reg_bits = s_p_sensor_cxt->sensor_info_ptr->reg_addr_value_bits;

		ret = _Sensor_Device_WriteReg(&reg);
	}

	return ret;
}

uint16_t Sensor_ReadReg(uint16_t reg_addr)
{

	uint32_t i = 0;
	uint16_t ret_val = 0xffff;
	int ret = -1;

	SENSOR_IOCTL_FUNC_PTR read_reg_func;

	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);
	if (PNULL == s_p_sensor_cxt->sensor_info_ptr) {
		CMR_LOGE("No sensor info!");
		return 0xFF;
	}

	read_reg_func = s_p_sensor_cxt->sensor_info_ptr->ioctl_func_tab_ptr->read_reg;

	if (PNULL != read_reg_func) {
		ret_val = (uint16_t)read_reg_func((uint32_t)(reg_addr & SENSOR_LOW_SIXTEEN_BIT));
	} else {
		SENSOR_REG_BITS_T reg;

		reg.reg_addr = reg_addr;
		reg.reg_bits = s_p_sensor_cxt->sensor_info_ptr->reg_addr_value_bits;

		ret = _Sensor_Device_ReadReg(&reg);
		if(SENSOR_SUCCESS == ret){
			ret_val = reg.reg_value;
		}

	}

	return ret_val;
}

int32_t Sensor_WriteReg_8bits(uint16_t reg_addr, uint8_t value)
{
	int32_t ret = -1;

	if (0xFFFF == reg_addr) {
		SENSOR_Sleep(value);
		CMR_LOGV("Sensor_WriteReg_8bits wait %d ms.\n", value);
		return 0;
	}

	SENSOR_REG_BITS_T reg;

	reg.reg_addr = reg_addr;
	reg.reg_value = value;
	reg.reg_bits = SENSOR_I2C_REG_8BIT | SENSOR_I2C_VAL_8BIT;

	ret = _Sensor_Device_WriteReg(&reg);

	return 0;
}

int32_t Sensor_ReadReg_8bits(uint8_t reg_addr, uint8_t * reg_val)
{

	int32_t ret = -1;

	SENSOR_REG_BITS_T reg;

	reg.reg_addr = reg_addr;
	reg.reg_bits = SENSOR_I2C_REG_8BIT | SENSOR_I2C_VAL_8BIT;

	ret = _Sensor_Device_ReadReg(&reg);
	if(SENSOR_SUCCESS == ret){
		*reg_val  = reg.reg_value;
	}

	return ret;
}

ERR_SENSOR_E Sensor_SendRegTabToSensor(SENSOR_REG_TAB_INFO_T *
				       sensor_reg_tab_info_ptr)
{
	uint32_t i;
	SENSOR_IOCTL_FUNC_PTR write_reg_func;
	uint16_t subaddr;
	uint16_t data;
	int32_t ret = -1;

	CMR_LOGV("SENSOR: Sensor_SendRegTabToSensor E.\n");

	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);
	if (PNULL == s_p_sensor_cxt->sensor_info_ptr) {
		CMR_LOGE("No sensor info!");
		return 0xFF;
	}

	write_reg_func = s_p_sensor_cxt->sensor_info_ptr->ioctl_func_tab_ptr->write_reg;

	if (PNULL != write_reg_func) {
		for (i = 0; i < sensor_reg_tab_info_ptr->reg_count; i++) {
			subaddr = sensor_reg_tab_info_ptr->sensor_reg_tab_ptr[i].reg_addr;
			data	= sensor_reg_tab_info_ptr->sensor_reg_tab_ptr[i].reg_value;
			if (SENSOR_OP_SUCCESS != write_reg_func((subaddr << S_BIT_4) + data))
				CMR_LOGV("SENSOR: IIC write : reg:0x%04x, val:0x%04x error\n", subaddr, data);
		}
	}else{
		SENSOR_REG_TAB_T regTab;
		regTab.reg_count = sensor_reg_tab_info_ptr->reg_count;
		regTab.reg_bits = s_p_sensor_cxt->sensor_info_ptr->reg_addr_value_bits;
		regTab.burst_mode = 0;
		regTab.sensor_reg_tab_ptr = sensor_reg_tab_info_ptr->sensor_reg_tab_ptr;

		ret = _Sensor_Device_WriteRegTab(&regTab);

	}

	CMR_LOGV("SENSOR: Sensor_SendRegValueToSensor -> reg_count = %d, is_main_sensor: %d.\n",
		sensor_reg_tab_info_ptr->reg_count, s_p_sensor_cxt->is_main_sensor);

	CMR_LOGV("SENSOR: Sensor_SendRegTabToSensor X.\n");

	return SENSOR_SUCCESS;
}

LOCAL void _Sensor_CleanInformation(void)
{
	SENSOR_REGISTER_INFO_T_PTR sensor_register_info_ptr = PNULL;

	SENSOR_DRV_CHECK_ZERO_VOID(s_p_sensor_cxt);
	sensor_register_info_ptr = &s_p_sensor_cxt->sensor_register_info;
	s_p_sensor_cxt->sensor_mode[SENSOR_MAIN] = SENSOR_MODE_MAX;
	s_p_sensor_cxt->sensor_mode[SENSOR_SUB] = SENSOR_MODE_MAX;
	s_p_sensor_cxt->sensor_info_ptr = PNULL;
	s_p_sensor_cxt->sensor_init = SENSOR_FALSE;
	s_p_sensor_cxt->sensor_index[SENSOR_MAIN] = 0xFF;
	s_p_sensor_cxt->sensor_index[SENSOR_SUB] = 0xFF;
	s_p_sensor_cxt->sensor_index[SENSOR_ATV] = 0xFF;
	SENSOR_MEMSET(&s_p_sensor_cxt->sensor_exp_info, 0x00, sizeof(SENSOR_EXP_INFO_T));
	sensor_register_info_ptr->cur_id = SENSOR_ID_MAX;
	return;
}

LOCAL int _Sensor_SetId(SENSOR_ID_E sensor_id)
{
	SENSOR_REGISTER_INFO_T_PTR sensor_register_info_ptr = PNULL;

	sensor_register_info_ptr = &s_p_sensor_cxt->sensor_register_info;
	sensor_register_info_ptr->cur_id = sensor_id;

	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);
	CMR_LOGV("_Sensor_SetId:sensor_id=%d,is_register_sensor=%d,is_main_sensor=%d \n",
		sensor_id, s_p_sensor_cxt->is_register_sensor, s_p_sensor_cxt->is_main_sensor);

	if (1 == s_p_sensor_cxt->is_register_sensor) {
		if ((SENSOR_MAIN == sensor_id) && (1 == s_p_sensor_cxt->is_main_sensor))
			return SENSOR_SUCCESS;
		if ((SENSOR_SUB == sensor_id) && (0 == s_p_sensor_cxt->is_main_sensor))
			return SENSOR_SUCCESS;
	}
	if ((SENSOR_MAIN == sensor_id) || (SENSOR_SUB == sensor_id)) {
		if (SENSOR_SUB == sensor_id) {
			if ((1 == s_p_sensor_cxt->is_register_sensor) && (1 == s_p_sensor_cxt->is_main_sensor)) {
				_Sensor_Device_I2CDeInit(SENSOR_MAIN);
			}
			s_p_sensor_cxt->is_main_sensor = 0;
		} else if (SENSOR_MAIN == sensor_id) {
			if ((1 == s_p_sensor_cxt->is_register_sensor) && (0 == s_p_sensor_cxt->is_main_sensor)) {
				_Sensor_Device_I2CDeInit(SENSOR_SUB);
			}
			s_p_sensor_cxt->is_main_sensor = 1;
		}
		s_p_sensor_cxt->is_register_sensor = 0;
		if (_Sensor_Device_I2CInit(sensor_id)) {
			if (SENSOR_MAIN == sensor_id) {
				s_p_sensor_cxt->is_main_sensor = 0;
			}
			CMR_LOGV("SENSOR: add I2C driver error\n");
			return SENSOR_FAIL;
		} else {
			CMR_LOGV("SENSOR: add I2C driver OK.\n");
			s_p_sensor_cxt->is_register_sensor = 1;
		}
	}

	return SENSOR_SUCCESS;
}

SENSOR_ID_E Sensor_GetCurId(void)
{
	SENSOR_REGISTER_INFO_T_PTR sensor_register_info_ptr = PNULL;

	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);
	sensor_register_info_ptr = &s_p_sensor_cxt->sensor_register_info;
	CMR_LOGV("Sensor_GetCurId,sensor_id =%d",
		sensor_register_info_ptr->cur_id);
	return (SENSOR_ID_E) sensor_register_info_ptr->cur_id;
}

uint32_t Sensor_SetCurId(SENSOR_ID_E sensor_id)
{
	CMR_LOGV("Sensor_SetCurId : %d.\n", sensor_id);
	if (sensor_id >= SENSOR_ID_MAX) {
		_Sensor_CleanInformation();
		return SENSOR_FAIL;
	}
	if (SENSOR_SUCCESS != _Sensor_SetId(sensor_id)) {
		CMR_LOGV("SENSOR: Fail to Sensor_SetCurId.\n");
		return SENSOR_FAIL;
	}
	return SENSOR_SUCCESS;
}

SENSOR_REGISTER_INFO_T_PTR Sensor_GetRegisterInfo(void)
{
	if (PNULL == s_p_sensor_cxt) {
		return PNULL;
	}
	return &s_p_sensor_cxt->sensor_register_info;
}

LOCAL void _Sensor_I2CInit(SENSOR_ID_E sensor_id)
{
	SENSOR_REGISTER_INFO_T_PTR sensor_register_info_ptr = PNULL;

	sensor_register_info_ptr = PNULL;
	SENSOR_INFO_T** sensor_info_tab_ptr = PNULL;
	SENSOR_INFO_T* sensor_info_ptr= PNULL;
	uint32_t i2c_clock = 100000;
	uint32_t set_i2c_clock = 0;

	SENSOR_DRV_CHECK_ZERO_VOID(s_p_sensor_cxt);
	sensor_register_info_ptr = &s_p_sensor_cxt->sensor_register_info;
	sensor_register_info_ptr->cur_id = sensor_id;

	if (0 == s_p_sensor_cxt->is_register_sensor) {
		if ((SENSOR_MAIN == sensor_id) || (SENSOR_SUB == sensor_id)) {

			if(_Sensor_Device_I2CInit(sensor_id)){
				CMR_LOGE("SENSOR: add I2C driver error\n");
				return;
			} else {
				CMR_LOGV("SENSOR: add I2C driver OK.\n");
				s_p_sensor_cxt->is_register_sensor = 1;
			}
		}
	} else {
		CMR_LOGV("Sensor: Init I2c %d ternimal! exits\n", sensor_id);
	}
	CMR_LOGV("_Sensor_I2CInit,sensor_id=%d,is_register_sensor=%d\n",
				sensor_id, s_p_sensor_cxt->is_register_sensor);
}

LOCAL int _Sensor_I2CDeInit(SENSOR_ID_E sensor_id)
{
	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);
	if (1 == s_p_sensor_cxt->is_register_sensor) {
		if ((SENSOR_MAIN == sensor_id) || (SENSOR_SUB == sensor_id)) {
			_Sensor_Device_I2CDeInit(sensor_id);
			s_p_sensor_cxt->is_register_sensor = 0;
			CMR_LOGV("SENSOR: delete  I2C  %d driver OK.\n", sensor_id);
		}
	} else {
		CMR_LOGV("SENSOR: delete  I2C  %d driver OK.\n", SENSOR_ID_MAX);
	}

	return SENSOR_SUCCESS;
}

LOCAL BOOLEAN _Sensor_Identify(SENSOR_ID_E sensor_id)
{
	uint32_t sensor_index = 0;
	SENSOR_INFO_T **sensor_info_tab_ptr = PNULL;
	uint32_t valid_tab_index_max = 0x00;
	SENSOR_INFO_T *sensor_info_ptr=PNULL;
	BOOLEAN retValue = SCI_FALSE;
	SENSOR_REGISTER_INFO_T_PTR sensor_register_info_ptr = PNULL;

	CMR_LOGV("SENSOR: sensor identifing %d", sensor_id);
	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);
	sensor_register_info_ptr = &s_p_sensor_cxt->sensor_register_info;

	//if already identified
	if (SCI_TRUE == sensor_register_info_ptr->is_register[sensor_id]) {
		CMR_LOGV("SENSOR: sensor identified");
		return SCI_TRUE;
	}
	if (s_p_sensor_cxt->sensor_identified && (SENSOR_ATV != sensor_id)) {
		sensor_index = s_p_sensor_cxt->sensor_index[sensor_id];
		CMR_LOGV("_Sensor_Identify:sensor_index=%d.\n",sensor_index);
		if (0xFF != sensor_index) {
			sensor_info_tab_ptr=(SENSOR_INFO_T**)Sensor_GetInforTab(sensor_id);
			_Sensor_I2CInit(sensor_id);
			sensor_info_ptr = sensor_info_tab_ptr[sensor_index];
			if (NULL == sensor_info_ptr) {
				CMR_LOGE("SENSOR: %d info of Sensor_Init table %d is null", sensor_index, (uint)sensor_id);
				_Sensor_I2CDeInit(sensor_id);
				goto IDENTIFY_SEARCH;
			}
			s_p_sensor_cxt->sensor_info_ptr = sensor_info_ptr;
			Sensor_PowerOn(SCI_TRUE);
			if (PNULL != sensor_info_ptr->ioctl_func_tab_ptr->identify) {
				s_p_sensor_cxt->i2c_addr = (s_p_sensor_cxt->sensor_info_ptr->salve_i2c_addr_w & 0xFF);
				_Sensor_Device_SetI2cAddr(s_p_sensor_cxt->i2c_addr);

				CMR_LOGV("SENSOR:identify  Sensor 01\n");
				if(SENSOR_SUCCESS==sensor_info_ptr->ioctl_func_tab_ptr->identify(SENSOR_ZERO_I2C)) {
					s_p_sensor_cxt->sensor_list_ptr[sensor_id] = sensor_info_ptr;
					sensor_register_info_ptr->is_register[sensor_id]=SCI_TRUE;
					sensor_register_info_ptr->img_sensor_num++;
					retValue = SCI_TRUE;
					CMR_LOGV("_Sensor_Identify:sensor_id :%d,img_sensor_num=%d\n",
								sensor_id, sensor_register_info_ptr->img_sensor_num);
				} else {
					Sensor_PowerOn(SCI_FALSE);
					_Sensor_I2CDeInit(sensor_id);
					CMR_LOGV("_Sensor_Identify:identify fail!.\n");
					goto IDENTIFY_SEARCH;
				}
			}
			Sensor_PowerOn(SCI_FALSE);
			_Sensor_I2CDeInit(sensor_id);
			return retValue;
		}
	}

IDENTIFY_SEARCH:
	CMR_LOGV("_Sensor_Identify:search.\n");
	sensor_info_tab_ptr=(SENSOR_INFO_T**)Sensor_GetInforTab(sensor_id);
	valid_tab_index_max=Sensor_GetInforTabLenght(sensor_id)-SENSOR_ONE_I2C;
	_Sensor_I2CInit(sensor_id);

	//search the sensor in the table
	for (sensor_index=0x00; sensor_index<valid_tab_index_max;sensor_index++) {
		sensor_info_ptr = sensor_info_tab_ptr[sensor_index];

		if (NULL==sensor_info_ptr) {
			CMR_LOGE("SENSOR: %d info of Sensor_Init table %d is null", sensor_index, (uint)sensor_id);
			continue ;
		}
		s_p_sensor_cxt->sensor_info_ptr = sensor_info_ptr;
		Sensor_PowerOn(SCI_TRUE);

		if(PNULL!=sensor_info_ptr->ioctl_func_tab_ptr->identify)
		{
			if (SENSOR_ATV != Sensor_GetCurId()) {
				s_p_sensor_cxt->i2c_addr = (s_p_sensor_cxt->sensor_info_ptr->salve_i2c_addr_w & 0xFF);
				_Sensor_Device_SetI2cAddr(s_p_sensor_cxt->i2c_addr);
			}
			CMR_LOGV("SENSOR:identify  Sensor 01\n");
			if (SENSOR_SUCCESS==sensor_info_ptr->ioctl_func_tab_ptr->identify(SENSOR_ZERO_I2C)) {
				s_p_sensor_cxt->sensor_list_ptr[sensor_id] = sensor_info_ptr;
				sensor_register_info_ptr->is_register[sensor_id] = SCI_TRUE;
				if (SENSOR_ATV != Sensor_GetCurId())
					s_p_sensor_cxt->sensor_index[sensor_id] = sensor_index;
				sensor_register_info_ptr->img_sensor_num++;
				Sensor_PowerOn(SCI_FALSE);
				retValue = SCI_TRUE;
				CMR_LOGV("_Sensor_Identify:sensor_id :%d,img_sensor_num=%d\n",
					sensor_id, sensor_register_info_ptr->img_sensor_num);
				break ;
			}
		}
		Sensor_PowerOn(SCI_FALSE);
	}
	_Sensor_I2CDeInit(sensor_id);
	if (SCI_TRUE == sensor_register_info_ptr->is_register[sensor_id]) {
		CMR_LOGV("SENSOR TYPE of %d indentify OK",(uint32_t)sensor_id);
		s_p_sensor_cxt->sensor_param_saved = SCI_TRUE;
	} else {
		CMR_LOGV("SENSOR TYPE of %d indentify FAILURE",(uint32_t)sensor_id);
	}

	return retValue;
}

LOCAL void _Sensor_SetStatus(SENSOR_ID_E sensor_id)
{
	uint32_t i = 0;
	uint32_t rst_lvl = 0;
	SENSOR_REGISTER_INFO_T_PTR sensor_register_info_ptr = PNULL;

	SENSOR_DRV_CHECK_ZERO_VOID(s_p_sensor_cxt);
	sensor_register_info_ptr = &s_p_sensor_cxt->sensor_register_info;

	/*pwdn all the sensor to avoid confilct as the sensor output*/
	CMR_LOGV("_Sensor_SetStatus: 1");
	for (i=0; i<=SENSOR_SUB; i++) {
		if (i == sensor_id) {
			continue;
		}
		if(SENSOR_TRUE == sensor_register_info_ptr->is_register[i]) {
			_Sensor_SetId(i);
			s_p_sensor_cxt->sensor_info_ptr = s_p_sensor_cxt->sensor_list_ptr[i];
			if (SENSOR_ATV != Sensor_GetCurId()) {
				s_p_sensor_cxt->i2c_addr = (s_p_sensor_cxt->sensor_info_ptr->salve_i2c_addr_w & 0xFF);
				_Sensor_Device_SetI2cAddr(s_p_sensor_cxt->i2c_addr);
			}
			Sensor_PowerDown((BOOLEAN)s_p_sensor_cxt->sensor_info_ptr->power_down_level);
			CMR_LOGV("SENSOR: Sensor_sleep of id %d",i);
		}
	}

	/*Give votage according the target sensor*/
	/*For dual sensor solution, the dual sensor should share all the power*/
	CMR_LOGV("_Sensor_SetStatus: 1_1");

	Sensor_PowerOn_Ex(sensor_id);

	CMR_LOGV("_Sensor_SetStatus: 2");

	_Sensor_SetId(sensor_id);
	s_p_sensor_cxt->sensor_info_ptr = s_p_sensor_cxt->sensor_list_ptr[sensor_id];
	CMR_LOGV("_Sensor_SetStatus: 3");
	//reset target sensor. and make normal.
	Sensor_SetExportInfo(&s_p_sensor_cxt->sensor_exp_info);
	CMR_LOGV("_Sensor_SetStatus: 4");
}

LOCAL int _Sensor_DeviceInit()
{
	int                      ret = 0;

	CMR_LOGV("To open sensor device.");

	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);
	if (-1 == s_p_sensor_cxt->fd_sensor) {
		s_p_sensor_cxt->fd_sensor = open(dev_name, O_RDWR, 0);
		if (-1 == s_p_sensor_cxt->fd_sensor) {
			CMR_LOGE("Failed to open sensor device.errno : %d", errno);
			fprintf(stderr, "Cannot open '%s': %d, %s\n", dev_name, errno,  strerror(errno));
		} else {
			CMR_LOGV("OK to open device.");
		}
	}

	ret = pthread_mutex_init(&s_p_sensor_cxt->cb_mutex, NULL);
	if (ret) {
		CMR_LOGV("Failed to init mutex : %d", errno);
	}

	ret = _Sensor_CreateThread();
	if (ret) {
		CMR_LOGV("Failed to create sensor thread");
		return ret;
	}
	ret = _Sensor_CreateMonitorThread();
	s_p_sensor_cxt->sensor_event_cb = NULL;

	return ret;
}

LOCAL int _Sensor_DeviceDeInit()
{
	int ret;
	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);
	if (-1 != s_p_sensor_cxt->fd_sensor) {
		ret = close(s_p_sensor_cxt->fd_sensor);
		s_p_sensor_cxt->fd_sensor = -1;
		CMR_LOGV("SENSOR: _Sensor_DeviceDeInit is done, ret = %d \n", ret);
	}
	_Sensor_KillMonitorThread();
	_Sensor_KillThread();

	return 0;
}


LOCAL uint32_t _Sensor_Register(SENSOR_ID_E sensor_id)
{
	uint32_t sensor_index = 0;
	SENSOR_INFO_T** sensor_info_tab_ptr = PNULL;
	uint32_t valid_tab_index_max = 0x00;
	SENSOR_INFO_T* sensor_info_ptr=PNULL;
	SENSOR_REGISTER_INFO_T_PTR sensor_register_info_ptr = PNULL;

	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);
	sensor_register_info_ptr = &s_p_sensor_cxt->sensor_register_info;

	CMR_LOGV("id is %d \n", sensor_id);
	//if already identified
	if (SCI_TRUE == sensor_register_info_ptr->is_register[sensor_id]) {
		CMR_LOGV("identified \n");
		return SENSOR_SUCCESS;
	}
	if (s_p_sensor_cxt->sensor_identified && (SENSOR_ATV != sensor_id)) {
		sensor_index = s_p_sensor_cxt->sensor_index[sensor_id];
		CMR_LOGV("sensor_index=%d.\n",sensor_index);
		if(0xFF != sensor_index) {
			valid_tab_index_max=Sensor_GetInforTabLenght(sensor_id)-SENSOR_ONE_I2C;
			if (sensor_index>=valid_tab_index_max) {
				CMR_LOGE("saved index is larger than sensor sum.\n");
				return SENSOR_FAIL;
			}

			sensor_info_tab_ptr=(SENSOR_INFO_T**)Sensor_GetInforTab(sensor_id);
			sensor_info_ptr = sensor_info_tab_ptr[sensor_index];
			if (NULL==sensor_info_ptr) {
				CMR_LOGE("index %d info of Sensor_Init table %d is null", sensor_index, (uint)sensor_id);
				return SENSOR_FAIL;
			}
			s_p_sensor_cxt->sensor_info_ptr = sensor_info_ptr;
			s_p_sensor_cxt->sensor_list_ptr[sensor_id] = sensor_info_ptr;
			sensor_register_info_ptr->is_register[sensor_id] = SCI_TRUE;
			sensor_register_info_ptr->img_sensor_num++;
		}
	}

	return SENSOR_SUCCESS;

}


#define SENSOR_PARAM_NUM  8
#define SENSOR_PARA "/data/misc/sensors/sensor.file"
void _Sensor_load_sensor_type(void)
{
	FILE 		*fp;
	uint8_t 	sensor_param[SENSOR_PARAM_NUM];
	uint32_t 	len = 0;

	memset(&sensor_param[0],0,SENSOR_PARAM_NUM);

	fp = fopen(SENSOR_PARA,"rb+");
	if(NULL == fp){
		fp = fopen(SENSOR_PARA,"wb+");
		if(NULL == fp){
			CMR_LOGE("_Sensor_load_sensor_type: file %s open error:%s\n",SENSOR_PARA,strerror(errno));
		}
		memset(&sensor_param[0],0xFF,SENSOR_PARAM_NUM);
	}else{
		len = fread(sensor_param, 1, SENSOR_PARAM_NUM, fp);
		CMR_LOGV("_Sensor_load_sensor_type:read sensor param len is %d \n",len);
		CMR_LOGV("_Sensor_load_sensor_type:read sensor param  is %x,%x,%x,%x,%x,%x,%x,%x \n",
			sensor_param[0], sensor_param[1], sensor_param[2], sensor_param[3],
			sensor_param[4], sensor_param[5], sensor_param[6], sensor_param[7]);
	}

	if(NULL != fp)
		fclose(fp);

	Sensor_SetMark(sensor_param);
}

void _Sensor_save_sensor_type(void)
{
	FILE                  *fp;
	uint8_t               is_saved;
	uint8_t               sensor_param[SENSOR_PARAM_NUM];

	memset(&sensor_param[0], 0, SENSOR_PARAM_NUM);
	Sensor_GetMark(sensor_param, &is_saved);

	if(is_saved){
		fp = fopen(SENSOR_PARA,"wb+");
		if(NULL == fp){
			CMR_LOGV("_Sensor_save_sensor_type: file %s open error:%s \n",SENSOR_PARA,strerror(errno));
		}else{
			fwrite(sensor_param, 1, SENSOR_PARAM_NUM, fp);
			fclose(fp);
		}
	}
}

int Sensor_set_calibration(uint32_t value)
{
	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);
	s_p_sensor_cxt->is_calibration = value;
	return SENSOR_SUCCESS;
}

static void _sensor_calil_lnc_param_recover(SENSOR_INFO_T *sensor_info_ptr)
{
	uint32_t i = 0;
	uint32_t index = 0;
	uint32_t length = 0;
	uint32_t addr = 0;
	struct sensor_raw_fix_info *raw_fix_info_ptr = PNULL;
	struct sensor_raw_info* raw_info_ptr = PNULL;
	raw_info_ptr = (struct sensor_raw_info*)(*(sensor_info_ptr->raw_info_ptr));

	SENSOR_DRV_CHECK_ZERO_VOID(s_p_sensor_cxt);

	if (PNULL != raw_info_ptr) {
		raw_fix_info_ptr = raw_info_ptr->fix_ptr;
		if (PNULL != raw_fix_info_ptr) {
			for (i = 0; i < 8; i++) {
				if (s_lnc_addr_bakup[i][1]) {

					free((void*)s_lnc_addr_bakup[i][1]);
					s_lnc_addr_bakup[i][1] = 0;
					index = s_lnc_addr_bakup[i][0];     /*index*/
					length = s_lnc_addr_bakup[i][3];    /*length*/
					addr = s_lnc_addr_bakup[i][2];      /*original address*/

					raw_fix_info_ptr->lnc.map[index][0].param_addr = (uint16_t*)addr;
					raw_fix_info_ptr->lnc.map[index][0].len = length;
					s_lnc_addr_bakup[i][0] = 0;
					s_lnc_addr_bakup[i][1] = 0;
					s_lnc_addr_bakup[i][2] = 0;
					s_lnc_addr_bakup[i][3] = 0;
				}
			}
		} else {
			for (i = 0; i < 8; i++) {
				if (s_lnc_addr_bakup[i][1]) {
					free((void*)s_lnc_addr_bakup[i][1]);
				}
			}
		memset((void*)&s_lnc_addr_bakup[0][0], 0x00, sizeof(s_lnc_addr_bakup));
		}
	}else {
		for (i = 0; i < 8; i++) {
			if (s_lnc_addr_bakup[i][1]) {
				free((void*)s_lnc_addr_bakup[i][1]);
			}
		}
		memset((void*)&s_lnc_addr_bakup[0][0], 0x00, sizeof(s_lnc_addr_bakup));
	}

	s_p_sensor_cxt->is_calibration = 0;

	CMR_LOGV("test: _sensor_calil_lnc_param_recover is_calibration: %d\n", s_p_sensor_cxt->is_calibration);

}

static int _sensor_cali_lnc_param_update(char *cfg_file_dir,SENSOR_INFO_T *sensor_info_ptr,SENSOR_ID_E sensor_id)
{
	const char *sensor_name = sensor_info_ptr->name;
	FILE *fp = PNULL;
	char file_name[80] = {0};
	char* file_name_ptr = 0;
	uint32_t str_len = 0;
	int file_pos = 0;
	uint32_t file_size = 0;
	char *data_ptr;
	int i,j;
	uint16_t *temp_buf_16 = PNULL;
	uint32_t width;
	uint32_t height;
	uint32_t index = 0;
	uint32_t rtn = SENSOR_SUCCESS;
	SENSOR_TRIM_T *trim_ptr = 0;
	struct sensor_raw_fix_info *raw_fix_info_ptr = PNULL;

	if(SENSOR_IMAGE_FORMAT_RAW != sensor_info_ptr->image_format){
		rtn = SENSOR_FAIL;
		goto cali_lnc_param_update_exit;
	}

	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);
	if (PNULL == s_p_sensor_cxt->sensor_info_ptr) {
		CMR_LOGE("No sensor info!");
		return -1;
	}

	str_len = sprintf(file_name, "%ssensor_%s",cfg_file_dir, sensor_name);
	file_name_ptr = (char*)&file_name[0] + str_len;

	/*LNC DATA Table*/
	temp_buf_16 = (uint16_t*)malloc(128*1024*2);
	if(!temp_buf_16){
		rtn = SENSOR_FAIL;
		goto cali_lnc_param_update_exit;
	}

	trim_ptr = (SENSOR_TRIM_T *)(s_p_sensor_cxt->sensor_info_ptr->ioctl_func_tab_ptr->get_trim(0));
	raw_fix_info_ptr = ((struct sensor_raw_info*)(*(sensor_info_ptr->raw_info_ptr)))->fix_ptr;
	i = 1;
	while(1) {
		height = trim_ptr[i].trim_height;
		width = trim_ptr[i].trim_width;
		if ((0 == height) || (0 == width)) {
			break;
		}

		sprintf(file_name_ptr, "_lnc_%d_%d_%d_rdm.dat", width, height, (i - 1));

		fp = fopen(file_name, "rb");
		if (0 == fp) {
			CMR_LOGV("_sensor_cali_param_update: does not find calibration file\n");
			i++;
			continue;
		}

		fseek(fp, 0L, SEEK_END);
		file_pos = ftell(fp);
		if (file_pos >= 0) {
			file_size = (uint32_t)file_pos;
		} else {
			fclose(fp);
			free(temp_buf_16);
			temp_buf_16 = NULL;
			CMR_LOGV("file pointers error!");
			rtn = SENSOR_FAIL;
			goto cali_lnc_param_update_exit;
		}
		fseek(fp, 0L, SEEK_SET);

		fread(temp_buf_16,1,file_size,fp);
		fclose(fp);

		if (file_size != raw_fix_info_ptr->lnc.map[i-1][0].len) {
			CMR_LOGV("_sensor_cali_param_update: file size dis-match, do not replace, w:%d, h:%d, ori: %d, now:%d/n",
				width, height, raw_fix_info_ptr->lnc.map[i-1][0].len, file_size);
		} else {
			if (s_lnc_addr_bakup[index][1]) {
				free((void*)s_lnc_addr_bakup[index][1]);
				s_lnc_addr_bakup[index][1] = 0;
			}
			s_lnc_addr_bakup[index][1] = (uint32_t)malloc(file_size);
			if (0 == s_lnc_addr_bakup[index][1]) {
				rtn = SENSOR_FAIL;
				CMR_LOGV("malloc failed i = %d\n", i);
				goto cali_lnc_param_update_exit;
			}
			memset((void*)s_lnc_addr_bakup[index][1], 0x00, file_size);

			s_lnc_addr_bakup[index][0] = i -1;
			s_lnc_addr_bakup[index][2] = (uint32_t)raw_fix_info_ptr->lnc.map[i-1][0].param_addr;	/*save the original address*/
			s_lnc_addr_bakup[index][3] = file_size;
			data_ptr = (char*)s_lnc_addr_bakup[index][1];
			raw_fix_info_ptr->lnc.map[i-1][0].param_addr = (uint16_t*)data_ptr;
			memcpy(data_ptr, temp_buf_16, file_size);
			index++;
			CMR_LOGV("_sensor_cali_param_update: replace finished/n");
		}

		i++;
	}

	if (temp_buf_16) {

		free((void*)temp_buf_16);
		temp_buf_16 = 0;

	}
	return rtn;

cali_lnc_param_update_exit:

	if (temp_buf_16) {

		free((void*)temp_buf_16);
		temp_buf_16 = 0;

	}

	_sensor_calil_lnc_param_recover(sensor_info_ptr);

	return rtn;
}


static int _sensor_cali_awb_param_update(char *cfg_file_dir,SENSOR_INFO_T *sensor_info_ptr,SENSOR_ID_E sensor_id)
{
	int rtn = 0;
	const char *sensor_name = sensor_info_ptr->name;
	FILE *fp = PNULL;
	char file_name[80] = {0};
	char buf[256] = {0x00};
	char* file_name_ptr = 0;
	uint32_t str_len = 0;
	int file_pos = 0;
	uint32_t file_size = 0;
	struct isp_bayer_ptn_stat_t *stat_ptr = PNULL;
	struct sensor_cali_info *cali_info_ptr = PNULL;
	struct sensor_raw_tune_info *raw_tune_info_ptr = PNULL;

	if(SENSOR_IMAGE_FORMAT_RAW != sensor_info_ptr->image_format){
		return SENSOR_FAIL;
	}
	raw_tune_info_ptr = (struct sensor_raw_tune_info*)(((struct sensor_raw_info*)(*(sensor_info_ptr->raw_info_ptr)))->tune_ptr);
	cali_info_ptr = (struct sensor_cali_info*)&(((struct sensor_raw_info*)(*(sensor_info_ptr->raw_info_ptr)))->cali_ptr->awb.cali_info);

	str_len = sprintf(file_name, "%ssensor_%s",cfg_file_dir, sensor_name);
	file_name_ptr = (char*)&file_name[0] + str_len;

	sprintf(file_name_ptr, "_awb_rdm.dat");

	CMR_LOGV("_sensor_cali_awb_param_update: %s\n", file_name);
	fp = fopen(file_name, "rb");
	if (0 == fp) {

		CMR_LOGV("_sensor_cali_awb_param_update: does not find calibration file\n");

		cali_info_ptr->r_sum = 1024;
		cali_info_ptr->b_sum = 1024;
		cali_info_ptr->gr_sum = 1024;
		cali_info_ptr->gb_sum = 1024;

		cali_info_ptr = (struct sensor_cali_info*)&(((struct sensor_raw_info*)(*(sensor_info_ptr->raw_info_ptr)))->cali_ptr->awb.golden_cali_info);
		cali_info_ptr->r_sum = 1024;
		cali_info_ptr->b_sum = 1024;
		cali_info_ptr->gr_sum = 1024;
		cali_info_ptr->gb_sum = 1024;
		rtn = SENSOR_SUCCESS;
		return rtn;

	} else {
		fseek(fp, 0L, SEEK_END);
		file_pos = ftell(fp);
		if (file_pos >= 0) {
			file_size = (uint32_t)file_pos;
		} else {
			fclose(fp);
			CMR_LOGV("file pointers error!");
			return SENSOR_FAIL;
		}
		fseek(fp, 0L, SEEK_SET);

		fread(buf,1,file_size,fp);
		fclose(fp);

		stat_ptr = (struct isp_bayer_ptn_stat_t*)&buf[0];
		cali_info_ptr->r_sum = stat_ptr->r_stat;
		cali_info_ptr->b_sum = stat_ptr->b_stat;
		cali_info_ptr->gr_sum = stat_ptr->gr_stat;
		cali_info_ptr->gb_sum = stat_ptr->gb_stat;

		rtn = SENSOR_SUCCESS;
	}

	memset (&file_name[0], 0x00, sizeof (file_name));
	memset (&buf[0], 0x00, sizeof (buf));
	str_len = sprintf(file_name, "%ssensor_%s",cfg_file_dir, sensor_name);
	file_name_ptr = (char*)&file_name[0] + str_len;

	sprintf(file_name_ptr, "_awb_gldn.dat");

	CMR_LOGV("_sensor_cali_awb_param_update: %s\n", file_name);
	cali_info_ptr = (struct sensor_cali_info*)&(((struct sensor_raw_info*)(*(sensor_info_ptr->raw_info_ptr)))->cali_ptr->awb.golden_cali_info);
	fp = fopen(file_name, "rb");
	if (0 == fp) {

		CMR_LOGV("_sensor_cali_awb_param_update: does not find calibration file\n");

		cali_info_ptr->r_sum = 1024;
		cali_info_ptr->b_sum = 1024;
		cali_info_ptr->gr_sum = 1024;
		cali_info_ptr->gb_sum = 1024;

		cali_info_ptr = (struct sensor_cali_info*)&(((struct sensor_raw_info*)(*(sensor_info_ptr->raw_info_ptr)))->cali_ptr->awb.cali_info);
		cali_info_ptr->r_sum = 1024;
		cali_info_ptr->b_sum = 1024;
		cali_info_ptr->gr_sum = 1024;
		cali_info_ptr->gb_sum = 1024;

		rtn = SENSOR_SUCCESS;
		return rtn;

	} else {
		fseek(fp, 0L, SEEK_END);
		file_pos = ftell(fp);
		if (file_pos >= 0) {
			file_size = (uint32_t)file_pos;
		} else {
			fclose(fp);
			CMR_LOGV("file pointers error!");
			return SENSOR_FAIL;
		}
		fseek(fp, 0L, SEEK_SET);

		fread(buf,1,file_size,fp);
		fclose(fp);

		stat_ptr = (struct isp_bayer_ptn_stat_t*)&buf[0];
		cali_info_ptr->r_sum = stat_ptr->r_stat;
		cali_info_ptr->b_sum = stat_ptr->b_stat;
		cali_info_ptr->gr_sum = stat_ptr->gr_stat;
		cali_info_ptr->gb_sum = stat_ptr->gb_stat;

		rtn = SENSOR_SUCCESS;
	}


	return rtn;
}

static int _sensor_cali_flashlight_param_update(char *cfg_file_dir,SENSOR_INFO_T *sensor_info_ptr, SENSOR_ID_E sensor_id)
{
	int rtn = 0;
	const char *sensor_name = sensor_info_ptr->name;
	FILE *fp = PNULL;
	char file_name[80] = {0};
	char buf[256] = {0x00};
	char* file_name_ptr = 0;
	uint32_t str_len = 0;
	int file_pos = 0;
	uint32_t file_size = 0;
	struct isp_bayer_ptn_stat_t *stat_ptr = PNULL;
	struct sensor_cali_info *cali_info_ptr = PNULL;
	struct sensor_raw_tune_info *raw_tune_info_ptr = PNULL;

	if(SENSOR_IMAGE_FORMAT_RAW != sensor_info_ptr->image_format){
		return SENSOR_FAIL;
	}
	raw_tune_info_ptr = (struct sensor_raw_tune_info*)(((struct sensor_raw_info*)(*(sensor_info_ptr->raw_info_ptr)))->tune_ptr);
	cali_info_ptr = (struct sensor_cali_info*)&(((struct sensor_raw_info*)(*(sensor_info_ptr->raw_info_ptr)))->cali_ptr->flashlight.cali_info);

	str_len = sprintf(file_name, "%ssensor_%s",cfg_file_dir, sensor_name);
	file_name_ptr = (char*)&file_name[0] + str_len;

	sprintf(file_name_ptr, "_flashlight_rdm.dat");

	CMR_LOGV("_sensor_cali_flashlight_param_update: %s\n", file_name);
	fp = fopen(file_name, "rb");
	if (0 == fp) {

		CMR_LOGV("_sensor_cali_flashlight_param_update: does not find calibration file\n");

		cali_info_ptr->r_sum = 1024;
		cali_info_ptr->b_sum = 1024;
		cali_info_ptr->gr_sum = 1024;
		cali_info_ptr->gb_sum = 1024;

		cali_info_ptr = (struct sensor_cali_info*)&(((struct sensor_raw_info*)(*(sensor_info_ptr->raw_info_ptr)))->cali_ptr->flashlight.golden_cali_info);
		cali_info_ptr->r_sum = 1024;
		cali_info_ptr->b_sum = 1024;
		cali_info_ptr->gr_sum = 1024;
		cali_info_ptr->gb_sum = 1024;

		rtn = SENSOR_SUCCESS;
		return rtn;

	} else {
		fseek(fp, 0L, SEEK_END);
		file_pos = ftell(fp);
		if (file_pos >= 0) {
			file_size = (uint32_t)file_pos;
		} else {
			fclose(fp);
			CMR_LOGV("file pointers error!");
			return SENSOR_FAIL;
		}
		fseek(fp, 0L, SEEK_SET);

		fread(buf,1,file_size,fp);
		fclose(fp);

		stat_ptr = (struct isp_bayer_ptn_stat_t*)&buf[0];
		cali_info_ptr->r_sum = stat_ptr->r_stat;
		cali_info_ptr->b_sum = stat_ptr->b_stat;
		cali_info_ptr->gr_sum = stat_ptr->gr_stat;
		cali_info_ptr->gb_sum = stat_ptr->gb_stat;

		rtn = SENSOR_SUCCESS;
	}

	memset (&file_name[0], 0x00, sizeof (file_name));
	memset (&buf[0], 0x00, sizeof (buf));
	str_len = sprintf(file_name, "%ssensor_%s",cfg_file_dir, sensor_name);
	file_name_ptr = (char*)&file_name[0] + str_len;

	sprintf(file_name_ptr, "_flashlight_gldn.dat");

	CMR_LOGV("_sensor_cali_flashlight_param_update: %s\n", file_name);
	cali_info_ptr = (struct sensor_cali_info*)&(((struct sensor_raw_info*)(*(sensor_info_ptr->raw_info_ptr)))->cali_ptr->flashlight.golden_cali_info);
	fp = fopen(file_name, "rb");
	if (0 == fp) {
		CMR_LOGV("_sensor_cali_flashlight_param_update: does not find calibration file\n");

		cali_info_ptr->r_sum = 1024;
		cali_info_ptr->b_sum = 1024;
		cali_info_ptr->gr_sum = 1024;
		cali_info_ptr->gb_sum = 1024;

		cali_info_ptr = (struct sensor_cali_info*)&(((struct sensor_raw_info*)(*(sensor_info_ptr->raw_info_ptr)))->cali_ptr->flashlight.cali_info);
		cali_info_ptr->r_sum = 1024;
		cali_info_ptr->b_sum = 1024;
		cali_info_ptr->gr_sum = 1024;
		cali_info_ptr->gb_sum = 1024;

		rtn = SENSOR_SUCCESS;
		return rtn;

	} else {
		fseek(fp, 0L, SEEK_END);

		file_pos = ftell(fp);
		if (file_pos >= 0) {
			file_size = (uint32_t)file_pos;
		} else {
			fclose(fp);
			CMR_LOGV("file pointers error!");
			return SENSOR_FAIL;
		}
		fseek(fp, 0L, SEEK_SET);

		fread(buf,1,file_size,fp);
		fclose(fp);

		stat_ptr = (struct isp_bayer_ptn_stat_t*)&buf[0];
		cali_info_ptr->r_sum = stat_ptr->r_stat;
		cali_info_ptr->b_sum = stat_ptr->b_stat;
		cali_info_ptr->gr_sum = stat_ptr->gr_stat;
		cali_info_ptr->gb_sum = stat_ptr->gb_stat;

		rtn = SENSOR_SUCCESS;
	}


	return rtn;
}

static int  _sensor_cali_load_param(char *cfg_file_dir,SENSOR_INFO_T *sensor_info_ptr,SENSOR_ID_E sensor_id)
{
	int rtn = 0;

	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);
	if (1 != s_p_sensor_cxt->is_calibration) {/*for normal*/

		rtn = _sensor_cali_lnc_param_update(cfg_file_dir,sensor_info_ptr, sensor_id);
		if (rtn) {

			return SENSOR_FAIL;
		}
		rtn = _sensor_cali_flashlight_param_update(cfg_file_dir,sensor_info_ptr, sensor_id);
		if (rtn) {
			return SENSOR_FAIL;
		}
		rtn = _sensor_cali_awb_param_update(cfg_file_dir,sensor_info_ptr, sensor_id);
		if (rtn) {
			return SENSOR_FAIL;
		}
	} else {/*for calibration*/

		struct sensor_cali_info *cali_info_ptr = PNULL;

		/*for awb calibration*/
		cali_info_ptr = (struct sensor_cali_info*)&(((struct sensor_raw_info*)(*(sensor_info_ptr->raw_info_ptr)))->cali_ptr->awb.cali_info);
		cali_info_ptr->r_sum = 1024;
		cali_info_ptr->b_sum = 1024;
		cali_info_ptr->gr_sum = 1024;
		cali_info_ptr->gb_sum = 1024;

		cali_info_ptr = (struct sensor_cali_info*)&(((struct sensor_raw_info*)(*(sensor_info_ptr->raw_info_ptr)))->cali_ptr->awb.golden_cali_info);
		cali_info_ptr->r_sum = 1024;
		cali_info_ptr->b_sum = 1024;
		cali_info_ptr->gr_sum = 1024;
		cali_info_ptr->gb_sum = 1024;

		/*for flash  calibration*/
		cali_info_ptr = (struct sensor_cali_info*)&(((struct sensor_raw_info*)(*(sensor_info_ptr->raw_info_ptr)))->cali_ptr->flashlight.cali_info);
		cali_info_ptr->r_sum = 1024;
		cali_info_ptr->b_sum = 1024;
		cali_info_ptr->gr_sum = 1024;
		cali_info_ptr->gb_sum = 1024;

		cali_info_ptr = (struct sensor_cali_info*)&(((struct sensor_raw_info*)(*(sensor_info_ptr->raw_info_ptr)))->cali_ptr->flashlight.golden_cali_info);
		cali_info_ptr->r_sum = 1024;
		cali_info_ptr->b_sum = 1024;
		cali_info_ptr->gr_sum = 1024;
		cali_info_ptr->gb_sum = 1024;
	}

	return SENSOR_SUCCESS;
}

int Sensor_Init(uint32_t sensor_id, uint32_t *sensor_num_ptr)
{
	int ret_val = SENSOR_FAIL;
	uint32_t sensor_num = 0;

	CMR_LOGV("0, start,id %d.",sensor_id);

	if (NULL != s_p_sensor_cxt) {
		CMR_LOGV("sensor close.");
		Sensor_Close();
	}

	s_p_sensor_cxt = (struct sensor_drv_context *)malloc(sizeof(struct sensor_drv_context));
	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);

	memset((void*)s_p_sensor_cxt, 0, sizeof(struct sensor_drv_context));
	s_p_sensor_cxt->fd_sensor = -1;
	s_p_sensor_cxt->i2c_addr = 0xff;

	{
		_Sensor_CleanInformation();
		_Sensor_InitDefaultExifInfo();
		_Sensor_load_sensor_type();

		if (_Sensor_DeviceInit()) {
			CMR_LOGV("_Sensor_DeviceInit error, return");
			ret_val = SENSOR_FAIL;
			goto init_exit;
		}
		if (SCI_TRUE == s_p_sensor_cxt->sensor_identified) {
			if (SENSOR_SUCCESS == _Sensor_Register(SENSOR_MAIN)) {
				sensor_num++;
			}
#ifndef CONFIG_DCAM_SENSOR_NO_FRONT_SUPPORT
			if (SENSOR_SUCCESS == _Sensor_Register(SENSOR_SUB)) {
				sensor_num++;
			}
#endif
			CMR_LOGV("1 is identify, register OK");

			ret_val = Sensor_Open(sensor_id);
			if (ret_val != SENSOR_SUCCESS ) {
				Sensor_PowerOn(SENSOR_FALSE);
			}

		}
		if (ret_val != SENSOR_SUCCESS ) {
			sensor_num = 0;
			CMR_LOGV("register sensor fail, start identify \n");
			if (_Sensor_Identify(SENSOR_MAIN))
				sensor_num++;
#ifndef CONFIG_DCAM_SENSOR_NO_FRONT_SUPPORT
			if (_Sensor_Identify(SENSOR_SUB))
				sensor_num++;
#endif
			ret_val = Sensor_Open(sensor_id);
		}
		s_p_sensor_cxt->sensor_identified = SCI_TRUE;
	}

	_Sensor_save_sensor_type();

	if (SENSOR_IMAGE_FORMAT_RAW == s_p_sensor_cxt->sensor_info_ptr->image_format) {
		if (SENSOR_SUCCESS == ret_val) {
			ret_val = _sensor_cali_load_param(cali_file_dir, s_p_sensor_cxt->sensor_info_ptr, sensor_id);
			if (ret_val) {
				CMR_LOGV("load cali data fail!! rtn:%d",ret_val);
				goto init_exit;
			}
		}
	}

	*sensor_num_ptr = sensor_num;
init_exit:
	if (SENSOR_SUCCESS != ret_val) {
		if (PNULL != s_p_sensor_cxt) {
			free(s_p_sensor_cxt);
			s_p_sensor_cxt = PNULL;
			CMR_LOGI("free s_p_sensor_cxt.");
		}
	}
	CMR_LOGV("2 init OK!");
	return ret_val;
}

BOOLEAN Sensor_IsInit(void)
{
	return s_p_sensor_cxt->sensor_init;
}

int Sensor_Open(uint32_t sensor_id)
{
	uint32_t ret_val = SENSOR_FAIL;
	SENSOR_REGISTER_INFO_T_PTR sensor_register_info_ptr = PNULL;

	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);
	sensor_register_info_ptr = &s_p_sensor_cxt->sensor_register_info;

	if (SENSOR_TRUE == sensor_register_info_ptr->is_register[sensor_id]) {
		CMR_LOGV("1, sensor register ok");
		_Sensor_SetStatus(sensor_id);
		CMR_LOGV("2, sensor set status");
		s_p_sensor_cxt->sensor_init = SENSOR_TRUE;

		if (SENSOR_ATV != Sensor_GetCurId()) {
			s_p_sensor_cxt->i2c_addr = (s_p_sensor_cxt->sensor_info_ptr->salve_i2c_addr_w & 0xFF);
			_Sensor_Device_SetI2cAddr(s_p_sensor_cxt->i2c_addr);
		}

		CMR_LOGV("3:sensor_id :%d,addr=0x%x", sensor_id, s_p_sensor_cxt->i2c_addr);
		Sensor_SetI2CClock();

		//confirm camera identify OK
		if (SENSOR_SUCCESS != s_p_sensor_cxt->sensor_info_ptr->ioctl_func_tab_ptr->identify(SENSOR_ZERO_I2C)) {
			sensor_register_info_ptr->is_register[sensor_id] = SENSOR_FALSE;
			_Sensor_I2CDeInit(sensor_id);
			CMR_LOGV("sensor identify not correct!!");
			return SENSOR_FAIL;
		}

		Sensor_SetExportInfo(&s_p_sensor_cxt->sensor_exp_info);

		ret_val = SENSOR_SUCCESS;
		if (SENSOR_SUCCESS != Sensor_SetMode(SENSOR_MODE_COMMON_INIT)) {
			CMR_LOGE("Sensor set init mode error!");
			_Sensor_I2CDeInit(sensor_id);
			ret_val = SENSOR_FAIL;
		}

		if ((NULL != s_p_sensor_cxt->sensor_info_ptr)
			&& (NULL != s_p_sensor_cxt->sensor_info_ptr->ioctl_func_tab_ptr)
			&& (PNULL != s_p_sensor_cxt->sensor_info_ptr->ioctl_func_tab_ptr->cfg_otp)) {
			s_p_sensor_cxt->sensor_info_ptr->ioctl_func_tab_ptr->cfg_otp(0);
		}

		CMR_LOGV("4 open success\n");
	} else {
		CMR_LOGE("Sensor not register, open fail, sensor_id = %d", sensor_id);
	}

	return ret_val;
}

int _Sensor_SetMode(uint32_t mode)
{
	int32_t rtn;
	uint32_t mclk;
	SENSOR_IOCTL_FUNC_PTR set_reg_tab_func = PNULL;

	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);
	if (PNULL == s_p_sensor_cxt->sensor_info_ptr) {
		CMR_LOGE("No sensor info!");
		return -1;
	}

	set_reg_tab_func = s_p_sensor_cxt->sensor_info_ptr->ioctl_func_tab_ptr->cus_func_1;

	CMR_LOGV("SENSOR: Sensor_SetMode -> mode = %d.\n", mode);
	if (SENSOR_FALSE == Sensor_IsInit()) {
		CMR_LOGV("SENSOR: Sensor_SetResolution -> sensor has not init");
		return SENSOR_OP_STATUS_ERR;
	}

	if (s_p_sensor_cxt->sensor_mode[Sensor_GetCurId()] == mode) {
		CMR_LOGV("SENSOR: The sensor mode as before");
		return SENSOR_SUCCESS;
	}

	if (PNULL != s_p_sensor_cxt->sensor_info_ptr->resolution_tab_info_ptr[mode].sensor_reg_tab_ptr) {
		mclk = s_p_sensor_cxt->sensor_info_ptr->resolution_tab_info_ptr[mode].xclk_to_sensor;
		Sensor_SetMCLK(mclk);
		s_p_sensor_cxt->sensor_exp_info.image_format = s_p_sensor_cxt->sensor_exp_info.sensor_mode_info[mode].image_format;

		if((SENSOR_MODE_COMMON_INIT == mode) && set_reg_tab_func){
			set_reg_tab_func(SENSOR_MODE_COMMON_INIT);
		}else{
			Sensor_SendRegTabToSensor(&s_p_sensor_cxt->sensor_info_ptr->resolution_tab_info_ptr[mode]);
		}
		s_p_sensor_cxt->sensor_mode[Sensor_GetCurId()] = mode;
	} else {
		if(set_reg_tab_func)
			set_reg_tab_func(0);
		CMR_LOGV("SENSOR: Sensor_SetResolution -> No this resolution information !!!");
	}
	return SENSOR_SUCCESS;
}

int Sensor_SetMode(uint32_t mode)
{
	int                      ret = 0;
	CMR_MSG_INIT(message);

	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);
	message.msg_type = SENSOR_EVT_SET_MODE;
	message.sub_msg_type = mode;
	ret = cmr_msg_post(s_p_sensor_cxt->queue_handle, &message);
	if (ret) {
		CMR_LOGE("Fail to send message");
	}

	return ret;
}

int Sensor_SetMode_WaitDone(void)
{
	int                      ret = 0;
	CMR_MSG_INIT(message);

	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);
	message.msg_type = SENSOR_EVT_SET_MODE_DONE;
	ret = cmr_msg_post(s_p_sensor_cxt->queue_handle, &message);
	if (ret) {
		CMR_LOGE("Fail to send message");
	}
	sem_wait(&s_p_sensor_cxt->st_setmode_sem);
	return ret;
}

int Sensor_GetMode(uint32_t *mode)
{
	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);
	if (SENSOR_FALSE == Sensor_IsInit()) {
		CMR_LOGV("sensor has not init");
		return SENSOR_OP_STATUS_ERR;
	}
	*mode = s_p_sensor_cxt->sensor_mode[Sensor_GetCurId()];
	return SENSOR_SUCCESS;
}

int _Sensor_StreamOn(void)
{
	int                    err = 0xff;
	uint32_t               param = 0;
	SENSOR_IOCTL_FUNC_PTR  stream_on_func;

	CMR_LOGV("Sensor_StreamOn");

	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);
	if (!Sensor_IsInit()) {
		CMR_LOGV("SENSOR: Sensor_StreamOn -> sensor has not been initialized");
		return SENSOR_FAIL;
	}

	if (PNULL == s_p_sensor_cxt->sensor_info_ptr) {
		CMR_LOGE("No sensor info!");
		return -1;
	}

	stream_on_func = s_p_sensor_cxt->sensor_info_ptr->ioctl_func_tab_ptr->stream_on;

	if (PNULL != stream_on_func) {
		err = stream_on_func(param);
	}

	if (0 == err) {
		s_p_sensor_cxt->stream_on = 1;
	}
	sem_post(&s_p_sensor_cxt->st_on_sem);
	return err;
}
int Sensor_StreamOn(void)
{
	int                      ret = 0;
	CMR_MSG_INIT(message);

	CMR_LOGE("Before");
	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);
	message.msg_type = SENSOR_EVT_STREAM_ON;
	ret = cmr_msg_post(s_p_sensor_cxt->queue_handle, &message);
	if (ret) {
		CMR_LOGE("Fail to send message");
	}

	sem_wait(&s_p_sensor_cxt->st_on_sem);
	CMR_LOGE("Ret");
	return ret;

}

int Sensor_AutoFocusInit(void)
{
	int                      ret = 0;
	CMR_MSG_INIT(message);

	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);
	message.msg_type = SENSOR_EVT_AF_INIT;
	ret = cmr_msg_post(s_p_sensor_cxt->queue_handle, &message);
	if (ret) {
		CMR_LOGE("Fail to send message");
	}
	sem_wait(&s_p_sensor_cxt->st_af_sem);

	return ret;}


int _Sensor_StreamOff(void)
{
	int                   err = 0xff;
	uint32_t               param = 0;
	SENSOR_IOCTL_FUNC_PTR stream_off_func;

	CMR_LOGV("Sensor_StreamOff");
	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);

	if (!Sensor_IsInit()) {
		CMR_LOGV("SENSOR: Sensor_StreamOn -> sensor has not been initialized");
		return SENSOR_FAIL;
	}

	if (PNULL == s_p_sensor_cxt->sensor_info_ptr) {
		CMR_LOGE("No sensor info!");
		return -1;
	}

	stream_off_func = s_p_sensor_cxt->sensor_info_ptr->ioctl_func_tab_ptr->stream_off;

	if (PNULL != stream_off_func) {
		err = stream_off_func(param);
	}
	s_p_sensor_cxt->stream_on = 0;
	sem_post(&s_p_sensor_cxt->st_off_sem);
	return err;
}
int Sensor_StreamOff(void)
{
	int                      ret = 0;
	CMR_MSG_INIT(message);

	CMR_LOGE("Before");
	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);
	message.msg_type = SENSOR_EVT_STREAM_OFF;
	ret = cmr_msg_post(s_p_sensor_cxt->queue_handle, &message);
	if (ret) {
		CMR_LOGE("Fail to send message");
	}

	sem_wait(&s_p_sensor_cxt->st_off_sem);
	CMR_LOGE("Ret");

	return ret;
}

uint32_t Sensor_Ioctl(uint32_t cmd, uint32_t arg)
{
	SENSOR_IOCTL_FUNC_PTR func_ptr;
	SENSOR_IOCTL_FUNC_TAB_T *func_tab_ptr;
	uint32_t temp;
	uint32_t ret_value = SENSOR_SUCCESS;

	if (SENSOR_IOCTL_GET_STATUS != cmd) {
		CMR_LOGV("SENSOR: Sensor_Ioctl -> cmd = %d, arg = %d.\n", cmd, arg);
	}
	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);
	if (!Sensor_IsInit()) {
		CMR_LOGV("SENSOR: Sensor_Ioctl -> sensor has not init.\n");
		return SENSOR_OP_STATUS_ERR;
	}

	if (SENSOR_IOCTL_CUS_FUNC_1 > cmd) {
		CMR_LOGV("SENSOR: Sensor_Ioctl - > can't access internal command !\n");
		return SENSOR_SUCCESS;
	}

	if (PNULL == s_p_sensor_cxt->sensor_info_ptr) {
		CMR_LOGE("No sensor info!");
		return -1;
	}

	func_tab_ptr = s_p_sensor_cxt->sensor_info_ptr->ioctl_func_tab_ptr;
	temp = *(uint32_t *) ((uint32_t) func_tab_ptr + cmd * S_BIT_2);
	func_ptr = (SENSOR_IOCTL_FUNC_PTR) temp;

	if (PNULL != func_ptr) {
		ret_value = func_ptr(arg);
	}
	return ret_value;
}

SENSOR_EXP_INFO_T *Sensor_GetInfo(void)
{
	if (PNULL == s_p_sensor_cxt) {
		CMR_LOGE("Sensor_driver_u, zero pointer \n");
		return PNULL;
	}
	if (!Sensor_IsInit()) {
		CMR_LOGV("SENSOR: Sensor_GetInfo -> sensor has not init");
		return PNULL;
	}

	CMR_LOGV("Sensor_GetInfo: info=%x \n", (uint32_t)&s_p_sensor_cxt->sensor_exp_info);
	return &s_p_sensor_cxt->sensor_exp_info;
}

ERR_SENSOR_E Sensor_Close(void)
{
	SENSOR_REGISTER_INFO_T_PTR sensor_register_info_ptr = PNULL;

	CMR_LOGV("SENSOR: Sensor_close");
	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);

	sensor_register_info_ptr = &s_p_sensor_cxt->sensor_register_info;

	if (1 == s_p_sensor_cxt->is_register_sensor) {
		if (1 == s_p_sensor_cxt->is_main_sensor) {
			_Sensor_Device_I2CDeInit(SENSOR_MAIN);
		} else {
			_Sensor_Device_I2CDeInit(SENSOR_SUB);
		}
		s_p_sensor_cxt->is_register_sensor = 0;
		s_p_sensor_cxt->is_main_sensor = 0;
	}

	if (SENSOR_TRUE == Sensor_IsInit()) {
		if (SENSOR_IMAGE_FORMAT_RAW == s_p_sensor_cxt->sensor_info_ptr->image_format) {
			if (0 == s_p_sensor_cxt->is_calibration) {
				_sensor_calil_lnc_param_recover(s_p_sensor_cxt->sensor_info_ptr);
			}
		}
		Sensor_PowerOn(SENSOR_FALSE);
		if (SENSOR_MAIN == Sensor_GetCurId()) {
			CMR_LOGV("SENSOR: Sensor_close 0.\n");
			if (SCI_TRUE == sensor_register_info_ptr->is_register[SENSOR_SUB]) {
				CMR_LOGV("SENSOR: Sensor_close 1.\n");
				_Sensor_SetId(SENSOR_SUB);
				s_p_sensor_cxt->sensor_info_ptr = s_p_sensor_cxt->sensor_list_ptr[SENSOR_SUB];
				Sensor_SetExportInfo(&s_p_sensor_cxt->sensor_exp_info);
				Sensor_PowerOn(SENSOR_FALSE);
				if (1 == s_p_sensor_cxt->is_register_sensor) {
					CMR_LOGV ("SENSOR: Sensor_close 2.\n");
					_Sensor_Device_I2CDeInit(SENSOR_SUB);
					s_p_sensor_cxt->is_register_sensor = 0;
					s_p_sensor_cxt->is_main_sensor = 0;
				}
			}
		} else if (SENSOR_SUB == Sensor_GetCurId()) {
			CMR_LOGV("SENSOR: Sensor_close 3.\n");
			if (SCI_TRUE ==  sensor_register_info_ptr->is_register[SENSOR_MAIN]) {
				CMR_LOGV("SENSOR: Sensor_close 4.\n");
				_Sensor_SetId(SENSOR_MAIN);
				s_p_sensor_cxt->sensor_info_ptr = s_p_sensor_cxt->sensor_list_ptr[SENSOR_MAIN];
				Sensor_SetExportInfo(&s_p_sensor_cxt->sensor_exp_info);
				Sensor_PowerOn(SENSOR_FALSE);
				if (1 == s_p_sensor_cxt->is_register_sensor) {
					CMR_LOGV ("SENSOR: Sensor_close 5.\n");
					_Sensor_Device_I2CDeInit(SENSOR_MAIN);
					s_p_sensor_cxt->is_register_sensor = 0;
					s_p_sensor_cxt->is_main_sensor = 0;
				}
			}
		} else if (SENSOR_ATV == Sensor_GetCurId()) {
			if (SCI_TRUE == sensor_register_info_ptr->is_register[SENSOR_MAIN]) {
				CMR_LOGV("SENSOR: Sensor_close 4.\n");
				_Sensor_SetId(SENSOR_MAIN);
				s_p_sensor_cxt->sensor_info_ptr = s_p_sensor_cxt->sensor_list_ptr[SENSOR_MAIN];
				Sensor_SetExportInfo(&s_p_sensor_cxt->sensor_exp_info);
				Sensor_PowerOn(SENSOR_FALSE);
				if (1 == s_p_sensor_cxt->is_register_sensor) {
					CMR_LOGV("SENSOR: Sensor_close 6.\n");
					_Sensor_Device_I2CDeInit(SENSOR_MAIN);
					s_p_sensor_cxt->is_register_sensor = 0;
					s_p_sensor_cxt->is_main_sensor = 0;
				}
			}
			if (SCI_TRUE == sensor_register_info_ptr->is_register[SENSOR_SUB]) {
				CMR_LOGV("SENSOR: Sensor_close 7.\n");
				_Sensor_SetId(SENSOR_SUB);
				s_p_sensor_cxt->sensor_info_ptr = s_p_sensor_cxt->sensor_list_ptr[SENSOR_SUB];
				Sensor_SetExportInfo(&s_p_sensor_cxt->sensor_exp_info);
				Sensor_PowerOn(SENSOR_FALSE);
				if (1 == s_p_sensor_cxt->is_register_sensor) {
					CMR_LOGV("SENSOR: Sensor_close 8.\n");
					_Sensor_Device_I2CDeInit(SENSOR_SUB);
					s_p_sensor_cxt->is_register_sensor = 0;
					s_p_sensor_cxt->is_main_sensor = 0;
				}
			}
		}
	}
	CMR_LOGV("SENSOR: Sensor_close 9.\n");

	_Sensor_DeviceDeInit();
	s_p_sensor_cxt->sensor_init = SENSOR_FALSE;
	s_p_sensor_cxt->sensor_mode[SENSOR_MAIN] = SENSOR_MODE_MAX;
	s_p_sensor_cxt->sensor_mode[SENSOR_SUB] = SENSOR_MODE_MAX;
	s_p_sensor_cxt->fd_sensor = -1;

	if (PNULL != s_p_sensor_cxt) {
		free(s_p_sensor_cxt);
		s_p_sensor_cxt = PNULL;
	}

	return SENSOR_SUCCESS;
}

uint32_t Sensor_SetSensorType(SENSOR_TYPE_E sensor_type)
{
	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);

	s_p_sensor_cxt->sensor_type = sensor_type;
	return SENSOR_SUCCESS;
}

ERR_SENSOR_E Sensor_SetTiming(SENSOR_MODE_E mode)
{
	SENSOR_REGISTER_INFO_T_PTR sensor_register_info_ptr = PNULL;
	uint32_t cur_id = 0;

	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);
	sensor_register_info_ptr = &s_p_sensor_cxt->sensor_register_info;
	cur_id = sensor_register_info_ptr->cur_id;

	CMR_LOGV("SENSOR: Sensor_SetTiming -> mode = %d,sensor_id=%d.\n", mode, cur_id);

	if (PNULL !=
		s_p_sensor_cxt->sensor_info_ptr->resolution_tab_info_ptr[mode].sensor_reg_tab_ptr) {
		/*send register value to sensor */
		Sensor_SendRegTabToSensor(&s_p_sensor_cxt->sensor_info_ptr->resolution_tab_info_ptr[mode]);
		s_p_sensor_cxt->sensor_mode[Sensor_GetCurId()] = mode;
	} else {
		CMR_LOGV("SENSOR: Sensor_SetResolution -> No this resolution information !!!");
	}
	return SENSOR_SUCCESS;
}

int Sensor_CheckTiming(SENSOR_MODE_E mode)
{
	SENSOR_REG_TAB_INFO_T *sensor_reg_tab_info_ptr = PNULL;
	uint32_t i = 0;
	uint16_t data = 0;
	uint32_t cur_id = 0;
	int ret = SENSOR_SUCCESS;
	SENSOR_REGISTER_INFO_T_PTR sensor_register_info_ptr = PNULL;

	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);
	if (PNULL == s_p_sensor_cxt->sensor_info_ptr) {
		CMR_LOGE("No sensor info!");
		return -1;
	}

	sensor_register_info_ptr = &s_p_sensor_cxt->sensor_register_info;
	sensor_reg_tab_info_ptr = &s_p_sensor_cxt->sensor_info_ptr->resolution_tab_info_ptr[mode];
	cur_id = sensor_register_info_ptr->cur_id;

	CMR_LOGV(" -> mode = %d,sensor_id=%d.\n", mode, cur_id);

	if (0 != cur_id)
		return 0;

	for (i = 0; i < sensor_reg_tab_info_ptr->reg_count; i++) {
		if ((0x4202 ==
			sensor_reg_tab_info_ptr->sensor_reg_tab_ptr[i].reg_addr)
			|| (SENSOR_WRITE_DELAY ==
			sensor_reg_tab_info_ptr->sensor_reg_tab_ptr[i].reg_addr))
			continue;
		data = Sensor_ReadReg(sensor_reg_tab_info_ptr->sensor_reg_tab_ptr[i].reg_addr);
		if (data != sensor_reg_tab_info_ptr->sensor_reg_tab_ptr[i].reg_value) {
			ret = -1;
			CMR_LOGV("report error!.\n");
			break;
		}
	}
	CMR_LOGV("return = %d.\n", ret);
	return ret;
}

uint32_t Sensor_SetFlash(uint32_t flash_mode)
{
	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);
	if (s_p_sensor_cxt->flash_mode == flash_mode)
		return 0;

	s_p_sensor_cxt->flash_mode = flash_mode;
	if ((FLASH_OPEN == flash_mode) || (FLASH_HIGH_LIGHT == flash_mode)) {
		Sensor_SetSensorExifInfo(SENSOR_EXIF_CTRL_FLASH, 1);
	} else {
		Sensor_SetSensorExifInfo(SENSOR_EXIF_CTRL_FLASH, 0);
	}
	_Sensor_Device_SetFlash(flash_mode);

	CMR_LOGV("Sensor_SetFlash:flash_mode=0x%x .\n", flash_mode);

	return 0;
}


LOCAL uint32_t _Sensor_InitDefaultExifInfo(void)
{
	EXIF_SPEC_PIC_TAKING_COND_T* exif_ptr = PNULL;
	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);
	exif_ptr = &s_p_sensor_cxt->default_exif;
	memset(&s_p_sensor_cxt->default_exif, 0, sizeof(EXIF_SPEC_PIC_TAKING_COND_T));

	CMR_LOGV("SENSOR: Sensor_InitDefaultExifInfo \n");

	exif_ptr->valid.FNumber=1;
	exif_ptr->FNumber.numerator=14;
	exif_ptr->FNumber.denominator=5;
	exif_ptr->valid.ExposureProgram=1;
	exif_ptr->ExposureProgram=0x04;
	exif_ptr->valid.ApertureValue=1;
	exif_ptr->ApertureValue.numerator=14;
	exif_ptr->ApertureValue.denominator=5;
	exif_ptr->valid.MaxApertureValue=1;
	exif_ptr->MaxApertureValue.numerator=14;
	exif_ptr->MaxApertureValue.denominator=5;
	exif_ptr->valid.FocalLength=1;
	exif_ptr->FocalLength.numerator=289;
	exif_ptr->FocalLength.denominator=100;
	exif_ptr->valid.FileSource=1;
	exif_ptr->FileSource=0x03;
	exif_ptr->valid.ExposureMode=1;
	exif_ptr->ExposureMode=0x00;
	exif_ptr->valid.WhiteBalance=1;
	exif_ptr->WhiteBalance=0x00;
	return SENSOR_SUCCESS;
}

uint32_t Sensor_SetSensorExifInfo(SENSOR_EXIF_CTRL_E cmd, uint32_t param)
{
	SENSOR_EXP_INFO_T_PTR sensor_info_ptr = Sensor_GetInfo();
	EXIF_SPEC_PIC_TAKING_COND_T *sensor_exif_info_ptr = PNULL;

	if (PNULL != sensor_info_ptr->ioctl_func_ptr->get_exif) {
		sensor_exif_info_ptr =
			(EXIF_SPEC_PIC_TAKING_COND_T *)sensor_info_ptr->ioctl_func_ptr->get_exif(0x00);
	} else {
		sensor_exif_info_ptr = &s_p_sensor_cxt->default_exif;
		CMR_LOGV("SENSOR: Sensor_SetSensorExifInfo the get_exif fun is null error \n");
	}

	switch (cmd) {
	case SENSOR_EXIF_CTRL_EXPOSURETIME:
		{
			SENSOR_MODE_E img_sensor_mode = s_p_sensor_cxt->sensor_mode[Sensor_GetCurId()];
			uint32_t exposureline_time =
				sensor_info_ptr->sensor_mode_info[img_sensor_mode].line_time;
			uint32_t exposureline_num = param;
			uint32_t exposure_time = 0x00;

			exposure_time = exposureline_time * exposureline_num/10;
			sensor_exif_info_ptr->valid.ExposureTime = 1;

			if (0x00 == exposure_time) {
				sensor_exif_info_ptr->valid.ExposureTime = 0;
			} else if (1000000 >= exposure_time) {
				sensor_exif_info_ptr->ExposureTime.numerator =
				    0x01;
				sensor_exif_info_ptr->ExposureTime.denominator =
				    1000000 / exposure_time;
			} else {
				uint32_t second = 0x00;
				do {
					second++;
					exposure_time -= 1000000;
					if (1000000 >= exposure_time) {
						break;
					}
				} while (1);
				sensor_exif_info_ptr->ExposureTime.denominator =
					1000000 / exposure_time;
				sensor_exif_info_ptr->ExposureTime.numerator =
					sensor_exif_info_ptr->ExposureTime.denominator * second;
			}
			break;
		}
	case SENSOR_EXIF_CTRL_FNUMBER:
		break;
	case SENSOR_EXIF_CTRL_EXPOSUREPROGRAM:
		break;
	case SENSOR_EXIF_CTRL_SPECTRALSENSITIVITY:
		break;
	case SENSOR_EXIF_CTRL_ISOSPEEDRATINGS:
		sensor_exif_info_ptr->valid.ISOSpeedRatings = 1;
		sensor_exif_info_ptr->ISOSpeedRatings.count = 1;
		sensor_exif_info_ptr->ISOSpeedRatings.type  = EXIF_SHORT;
		sensor_exif_info_ptr->ISOSpeedRatings.size  = 2;
		memcpy((void*)&sensor_exif_info_ptr->ISOSpeedRatings.ptr[0],
			   (void*)&param,2);
		break;
	case SENSOR_EXIF_CTRL_OECF:
		break;
	case SENSOR_EXIF_CTRL_SHUTTERSPEEDVALUE:
		break;
	case SENSOR_EXIF_CTRL_APERTUREVALUE:
		break;
	case SENSOR_EXIF_CTRL_BRIGHTNESSVALUE:
		{
			sensor_exif_info_ptr->valid.BrightnessValue = 1;
			switch (param) {
			case 0:
			case 1:
			case 2:
				sensor_exif_info_ptr->BrightnessValue.numerator = 1;
				sensor_exif_info_ptr->BrightnessValue.denominator = 1;
				break;
			case 3:
				sensor_exif_info_ptr->BrightnessValue.numerator = 0;
				sensor_exif_info_ptr->BrightnessValue.denominator = 0;
				break;
			case 4:
			case 5:
			case 6:
				sensor_exif_info_ptr->BrightnessValue.numerator = 2;
				sensor_exif_info_ptr->BrightnessValue.denominator = 2;
				break;
			default:
				sensor_exif_info_ptr->BrightnessValue.numerator = 0xff;
				sensor_exif_info_ptr->BrightnessValue.denominator = 0xff;
				break;
			}
			break;
		}
		break;
	case SENSOR_EXIF_CTRL_EXPOSUREBIASVALUE:
		break;
	case SENSOR_EXIF_CTRL_MAXAPERTUREVALUE:
		break;
	case SENSOR_EXIF_CTRL_SUBJECTDISTANCE:
		break;
	case SENSOR_EXIF_CTRL_METERINGMODE:
		break;
	case SENSOR_EXIF_CTRL_LIGHTSOURCE:
		{
			sensor_exif_info_ptr->valid.LightSource = 1;
			switch (param) {
			case 0:
				sensor_exif_info_ptr->LightSource = 0x00;
				break;
			case 1:
				sensor_exif_info_ptr->LightSource = 0x03;
				break;
			case 2:
				sensor_exif_info_ptr->LightSource = 0x0f;
				break;
			case 3:
				sensor_exif_info_ptr->LightSource = 0x0e;
				break;
			case 4:
				sensor_exif_info_ptr->LightSource = 0x03;
				break;
			case 5:
				sensor_exif_info_ptr->LightSource = 0x01;
				break;
			case 6:
				sensor_exif_info_ptr->LightSource = 0x0a;
				break;
			default:
				sensor_exif_info_ptr->LightSource = 0xff;
				break;
			}
			break;
		}
	case SENSOR_EXIF_CTRL_FLASH:
		sensor_exif_info_ptr->valid.Flash = 1;
		sensor_exif_info_ptr->Flash = param;
		break;
	case SENSOR_EXIF_CTRL_FOCALLENGTH:
		break;
	case SENSOR_EXIF_CTRL_SUBJECTAREA:
		break;
	case SENSOR_EXIF_CTRL_FLASHENERGY:
		break;
	case SENSOR_EXIF_CTRL_SPATIALFREQUENCYRESPONSE:
		break;
	case SENSOR_EXIF_CTRL_FOCALPLANEXRESOLUTION:
		break;
	case SENSOR_EXIF_CTRL_FOCALPLANEYRESOLUTION:
		break;
	case SENSOR_EXIF_CTRL_FOCALPLANERESOLUTIONUNIT:
		break;
	case SENSOR_EXIF_CTRL_SUBJECTLOCATION:
		break;
	case SENSOR_EXIF_CTRL_EXPOSUREINDEX:
		break;
	case SENSOR_EXIF_CTRL_SENSINGMETHOD:
		break;
	case SENSOR_EXIF_CTRL_FILESOURCE:
		break;
	case SENSOR_EXIF_CTRL_SCENETYPE:
		break;
	case SENSOR_EXIF_CTRL_CFAPATTERN:
		break;
	case SENSOR_EXIF_CTRL_CUSTOMRENDERED:
		break;
	case SENSOR_EXIF_CTRL_EXPOSUREMODE:
		break;

	case SENSOR_EXIF_CTRL_WHITEBALANCE:
		sensor_exif_info_ptr->valid.WhiteBalance = 1;
		if(param)
			sensor_exif_info_ptr->WhiteBalance = 1;
		else
			sensor_exif_info_ptr->WhiteBalance = 0;
		break;

	case SENSOR_EXIF_CTRL_DIGITALZOOMRATIO:
		break;
	case SENSOR_EXIF_CTRL_FOCALLENGTHIN35MMFILM:
		break;
	case SENSOR_EXIF_CTRL_SCENECAPTURETYPE:
		{
			sensor_exif_info_ptr->valid.SceneCaptureType = 1;
			switch (param) {
			case 0:
				sensor_exif_info_ptr->SceneCaptureType = 0x00;
				break;
			case 1:
				sensor_exif_info_ptr->SceneCaptureType = 0x03;
				break;
			default:
				sensor_exif_info_ptr->LightSource = 0xff;
				break;
			}
			break;
		}
	case SENSOR_EXIF_CTRL_GAINCONTROL:
		break;
	case SENSOR_EXIF_CTRL_CONTRAST:
		{
			sensor_exif_info_ptr->valid.Contrast = 1;
			switch (param) {
			case 0:
			case 1:
			case 2:
				sensor_exif_info_ptr->Contrast = 0x01;
				break;
			case 3:
				sensor_exif_info_ptr->Contrast = 0x00;
				break;
			case 4:
			case 5:
			case 6:
				sensor_exif_info_ptr->Contrast = 0x02;
				break;
			default:
				sensor_exif_info_ptr->Contrast = 0xff;
				break;
			}
			break;
		}
	case SENSOR_EXIF_CTRL_SATURATION:
		{
			sensor_exif_info_ptr->valid.Saturation = 1;
			switch (param) {
			case 0:
			case 1:
			case 2:
				sensor_exif_info_ptr->Saturation = 0x01;
				break;
			case 3:
				sensor_exif_info_ptr->Saturation = 0x00;
				break;
			case 4:
			case 5:
			case 6:
				sensor_exif_info_ptr->Saturation = 0x02;
				break;
			default:
				sensor_exif_info_ptr->Saturation = 0xff;
				break;
			}
			break;
		}
	case SENSOR_EXIF_CTRL_SHARPNESS:
		{
			sensor_exif_info_ptr->valid.Sharpness = 1;
			switch (param) {
			case 0:
			case 1:
			case 2:
				sensor_exif_info_ptr->Sharpness = 0x01;
				break;
			case 3:
				sensor_exif_info_ptr->Sharpness = 0x00;
				break;
			case 4:
			case 5:
			case 6:
				sensor_exif_info_ptr->Sharpness = 0x02;
				break;
			default:
				sensor_exif_info_ptr->Sharpness = 0xff;
				break;
			}
			break;
		}
	case SENSOR_EXIF_CTRL_DEVICESETTINGDESCRIPTION:
		break;
	case SENSOR_EXIF_CTRL_SUBJECTDISTANCERANGE:
		break;
	default:
		break;
	}
	return SENSOR_SUCCESS;
}

EXIF_SPEC_PIC_TAKING_COND_T *Sensor_GetSensorExifInfo(void)
{
	SENSOR_EXP_INFO_T_PTR sensor_info_ptr = Sensor_GetInfo();
	EXIF_SPEC_PIC_TAKING_COND_T *sensor_exif_info_ptr = PNULL;

	if (PNULL != sensor_info_ptr->ioctl_func_ptr->get_exif) {
		sensor_exif_info_ptr =
			(EXIF_SPEC_PIC_TAKING_COND_T *)
			sensor_info_ptr->ioctl_func_ptr->get_exif(0x00);
		CMR_LOGV("get_exif.");
	} else {
		sensor_exif_info_ptr = &s_p_sensor_cxt->default_exif;
		CMR_LOGV("SENSOR: get_exif fun null, so use the default\n");
	}
	return sensor_exif_info_ptr;
}

int Sensor_SetMark(uint8_t *buf)
{
	uint32_t i;
	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);

	if ((SIGN_0 != buf[0]) && (SIGN_1 != buf[1])
		&& (SIGN_2 != buf[2]) && (SIGN_3 != buf[3])) {
		s_p_sensor_cxt->sensor_identified = SCI_FALSE;
	} else {
		s_p_sensor_cxt->sensor_identified = SCI_TRUE;
		for( i=0 ; i<2 ; i++) {
			s_p_sensor_cxt->sensor_index[i] = buf[4+i];
		}
	}
	CMR_LOGV("Sensor_SetSensorParam:sensor_identified=%d,idex is %d,%d.\n",
		s_p_sensor_cxt->sensor_identified,
		s_p_sensor_cxt->sensor_index[SENSOR_MAIN],
		s_p_sensor_cxt->sensor_index[SENSOR_SUB]);
	return 0;
}

int Sensor_GetMark(uint8_t *buf,uint8_t *is_saved_ptr)
{
	uint32_t i,j=0;
	uint8_t *ptr=buf;
	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);

	if(SCI_TRUE == s_p_sensor_cxt->sensor_param_saved) {
		*is_saved_ptr = 1;
		*ptr++ = SIGN_0;
		*ptr++ = SIGN_1;
		*ptr++ = SIGN_2;
		*ptr++ = SIGN_3;
		for( i=0 ; i<2 ; i++) {
			*ptr++ = s_p_sensor_cxt->sensor_index[i];
		}
		CMR_LOGV("Sensor_GetSensorParam:index is %d,%d.\n",
			s_p_sensor_cxt->sensor_index[SENSOR_MAIN],
			s_p_sensor_cxt->sensor_index[SENSOR_SUB]);
	} else {
		*is_saved_ptr = 0;
	}
	return 0;
}

int Sensor_WriteData(uint8_t *regPtr, uint32_t length)
{
	int ret;
	ret = _Sensor_Device_Write(regPtr, length);

	return ret;
}

int Sensor_EventReg(cmr_evt_cb  event_cb)
{
	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);
	pthread_mutex_lock(&s_p_sensor_cxt->cb_mutex);
	s_p_sensor_cxt->sensor_event_cb = event_cb;
	pthread_mutex_unlock(&s_p_sensor_cxt->cb_mutex);
	return 0;
}

int Sensor_GetRawSettings(void **raw_setting, uint32_t *length)
{
	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);
	if (PNULL == s_p_sensor_cxt->sensor_info_ptr) {
		CMR_LOGE("No sensor info!");
		return -1;
	}

	SENSOR_INFO_T *ptr = s_p_sensor_cxt->sensor_info_ptr;
	*raw_setting = s_p_sensor_cxt->sensor_info_ptr;

	return 0;
}

LOCAL int   _Sensor_CreateThread(void)
{
	int                      ret = 0;
	pthread_attr_t           attr;
	CMR_MSG_INIT(message);

	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);
	sem_init(&s_p_sensor_cxt->st_on_sem, 0, 0);
	sem_init(&s_p_sensor_cxt->st_off_sem, 0, 0);
	sem_init(&s_p_sensor_cxt->st_af_sem, 0, 0);
	sem_init(&s_p_sensor_cxt->sensor_sync_sem, 0, 0);
	sem_init(&s_p_sensor_cxt->st_setmode_sem, 0, 0);
	s_p_sensor_cxt->exit_flag = 0;
	ret = cmr_msg_queue_create(SENSOR_MSG_QUEUE_SIZE, &s_p_sensor_cxt->queue_handle);
	if (ret) {
		CMR_LOGE("NO Memory, Frailed to create message queue");
	}

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	ret = pthread_create(&s_p_sensor_cxt->sensor_thread, &attr, _Sensor_ThreadProc, NULL);
	pthread_attr_destroy(&attr);

	message.msg_type = SENSOR_EVT_INIT;
	ret = cmr_msg_post(s_p_sensor_cxt->queue_handle, &message);
	if (ret) {
		CMR_LOGE("Fail to send message to camera main thread");
		return ret;
	}

	ret = _Sensor_WaitSync();

	return ret;
}

LOCAL int _Sensor_KillThread(void)
{
	int                      ret = 0;
	void                     *dummy;

	CMR_MSG_INIT(message);
	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);

	CMR_LOGV("To kill sensor thread");

	s_p_sensor_cxt->exit_flag = 0;
	message.msg_type = SENSOR_EVT_DEINIT;
	ret = cmr_msg_post(s_p_sensor_cxt->queue_handle, &message);
	if (ret) {
		CMR_LOGE("Fail to send message to camera main thread");
		return ret;
	}

	while (0 == s_p_sensor_cxt->exit_flag) {
		CMR_LOGE("Wait 10 ms");
		usleep(10000);
	}

	ret = pthread_join(s_p_sensor_cxt->sensor_thread, &dummy);
	s_p_sensor_cxt->sensor_thread = 0;
	sem_destroy(&s_p_sensor_cxt->st_on_sem);
	sem_destroy(&s_p_sensor_cxt->st_off_sem);
	sem_destroy(&s_p_sensor_cxt->st_setmode_sem);
	sem_destroy(&s_p_sensor_cxt->st_af_sem);
	sem_destroy(&s_p_sensor_cxt->sensor_sync_sem);

	cmr_msg_queue_destroy(s_p_sensor_cxt->queue_handle);
	s_p_sensor_cxt->queue_handle = 0;
	return ret;
}

LOCAL void* _Sensor_ThreadProc(void* data)
{
	CMR_MSG_INIT(message);
	uint32_t                 evt;
	uint32_t                 exit_flag = 0;
	int                      ret = 0;

	CMR_LOGV("In");

	while(1) {
		ret = cmr_msg_get(s_p_sensor_cxt->queue_handle, &message);
		if (ret) {
			CMR_LOGE("Message queue destroied");
			break;
		}

		CMR_LOGE("Msg, 0x%x", message.msg_type);
		CMR_PRINT_TIME;

		switch (message.msg_type) {
		case SENSOR_EVT_INIT:
			CMR_LOGV("SENSOR_EVT_INIT");
			ret = _Sensor_SyncDone();
			break;

		case SENSOR_EVT_SET_MODE:
			ret = _Sensor_SetMode(message.sub_msg_type);
			break;

		case SENSOR_EVT_STREAM_ON:
			CMR_LOGV("SENSOR_EVT_STREAM_ON");
			_Sensor_StreamOn();

			break;

		case SENSOR_EVT_STREAM_OFF:
			CMR_LOGV("SENSOR_EVT_STREAM_OFF");
			_Sensor_StreamOff();
			break;

		case SENSOR_EVT_DEINIT:
			CMR_LOGV("SENSOR_EVT_DEINIT");
			s_p_sensor_cxt->exit_flag = 1;
			break;
		case SENSOR_EVT_AF_INIT:
			CMR_LOGV("SENSOR_EVT_AF_INIT");
			ret = _Sensor_AutoFocusInit();
			sem_post(&s_p_sensor_cxt->st_af_sem);
			CMR_LOGV("SENSOR_EVT_AF_INIT, Done");
			break;
		case SENSOR_EVT_SET_MODE_DONE:
			sem_post(&s_p_sensor_cxt->st_setmode_sem);
			break;
		default:
			CMR_LOGE("Unsupported MSG");
			break;
		}

		if (message.alloc_flag) {
			free(message.data);
		}

		if (s_p_sensor_cxt->exit_flag) {
			CMR_LOGV("Sensor thread Exit!");
			break;
		}
	}

	CMR_LOGV("Out");
	return NULL;
}

LOCAL int _Sensor_WaitSync(void)
{
	int                      ret = 0;

	sem_wait(&s_p_sensor_cxt->sensor_sync_sem);
	CMR_LOGI("wait done.");

	return ret;
}

LOCAL int _Sensor_SyncDone(void)
{
	int                      ret = 0;

	sem_post(&s_p_sensor_cxt->sensor_sync_sem);
	return ret;
}

LOCAL int _Sensor_AutoFocusInit(void)
{
	SENSOR_EXT_FUN_PARAM_T   af_param;
	int                      ret = 0;

	CMR_LOGV("Enter");

	af_param.cmd = SENSOR_EXT_FUNC_INIT;
	af_param.param = SENSOR_EXT_FOCUS_TRIG;
	ret = Sensor_Ioctl(SENSOR_IOCTL_FOCUS, (uint32_t)&af_param);
	if (ret) {
		CMR_LOGE("Failed to init AF");
	} else {
		CMR_LOGV("OK to init auto focus");
	}

	return ret;
}


LOCAL void* _Sensor_MonitorProc(void* data)
{
	uint32_t                 ret = 0, param = 0;

	while (1) {
		usleep(SENSOR_CHECK_STATUS_INTERVAL);

		if (s_p_sensor_cxt->monitor_exit) {
			s_p_sensor_cxt->monitor_exit = 0;
			CMR_LOGV("EXIT");
			break;
		} else {
			if (s_p_sensor_cxt->stream_on) {
				ret = Sensor_Ioctl(SENSOR_IOCTL_GET_STATUS, (uint32_t)&param);
				if (ret) {
					CMR_LOGE("Sensor run in wrong way");
					pthread_mutex_lock(&s_p_sensor_cxt->cb_mutex);
					if (s_p_sensor_cxt->sensor_event_cb)
						(*s_p_sensor_cxt->sensor_event_cb)(CMR_SENSOR_ERROR, NULL);
					pthread_mutex_unlock(&s_p_sensor_cxt->cb_mutex);
				}
			}
		}

	}

	return NULL;
}

LOCAL int   _Sensor_CreateMonitorThread(void)
{
	int                      ret = 0;
	pthread_attr_t           attr;

	CMR_LOGV("Create status monitor thread");
	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);

	if (0 == s_p_sensor_cxt->monitor_thread) {
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
		ret = pthread_create(&s_p_sensor_cxt->monitor_thread, &attr, _Sensor_MonitorProc, NULL);
		pthread_attr_destroy(&attr);
	}

	return ret;
}

LOCAL int _Sensor_KillMonitorThread(void)
{
	int                      ret = 0;
	void                     *dummy;

	CMR_LOGV("To kill sensor monitor thread");
	SENSOR_DRV_CHECK_ZERO(s_p_sensor_cxt);

	if (s_p_sensor_cxt->monitor_thread) {
		s_p_sensor_cxt->monitor_exit = 1;
		while (1 == s_p_sensor_cxt->monitor_exit) {
			CMR_LOGE("Wait 10 ms");
			usleep(10000);
		}
		ret = pthread_join(s_p_sensor_cxt->monitor_thread, &dummy);
		s_p_sensor_cxt->monitor_thread = 0;
	}

	return ret;
}
