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
#include "sensor.h"
#include "jpeg_exif_header.h"
#include "sensor_drv_u.h"
#include "sensor_raw.h"
#include "sensor_s5k4e1ga_raw_param.c"


#define s5k4e1ga_I2C_ADDR_W        0x10
#define s5k4e1ga_I2C_ADDR_R         0x10

LOCAL uint32_t _s5k4e1ga_GetResolutionTrimTab(uint32_t param);
LOCAL uint32_t _s5k4e1ga_PowerOn(uint32_t power_on);
LOCAL uint32_t _s5k4e1ga_Identify(uint32_t param);
LOCAL uint32_t _s5k4e1ga_BeforeSnapshot(uint32_t param);
LOCAL uint32_t _s5k4e1ga_after_snapshot(uint32_t param);
LOCAL uint32_t _s5k4e1ga_StreamOn(uint32_t param);
LOCAL uint32_t _s5k4e1ga_StreamOff(uint32_t param);
LOCAL uint32_t _s5k4e1ga_write_exposure(uint32_t param);
LOCAL uint32_t _s5k4e1ga_write_gain(uint32_t param);
LOCAL uint32_t _s5k4e1ga_write_af(uint32_t param);
LOCAL uint32_t _s5k4e1ga_ReadGain(uint32_t param);
LOCAL uint32_t _s5k4e1ga_SetEV(uint32_t param);
LOCAL uint32_t _s5k4e1ga_ExtFunc(uint32_t ctl_param);


static uint32_t s_s5k4e1ga_gain = 0;

LOCAL const SENSOR_REG_T s5k4e1ga_com_mipi_raw[] = {
	// ******************* //
	// S5K4E1GX EVT3 MIPI Setting
	//
	// last update date : 2011. 11. 30
	//
	// Full size output (2608 x 1960)
	// This Setfile is optimized for 15fps or lower frame rate
	//
	// ******************* //
	//$MIPI[Width:2608,Height:1960,Format:Raw10,Lane:2,ErrorCheck:0,PolarityData:0,PolarityClock:0,Buffer:2]
	//+++++++++++++++++++++++++++++++//
	// Reset for operation ...
	{0x0100, 0x00},//stream off
	{0x3030, 0x07},//shut streaming off

	//--> The below registers are for FACTORY ONLY. If you change them without prior notification.
	// YOU are RESPONSIBLE for the FAILURE that will happen in the future.
	//+++++++++++++++++++++++++++++++//
	//Factory only set START
	// Analog Setting
	{0x3000, 0x05},
	{0x3001, 0x03},
	{0x3002, 0x08},
	{0x3003, 0x09},
	{0x3004, 0x2E},
	{0x3005, 0x06},
	{0x3006, 0x34},
	{0x3007, 0x00},
	{0x3008, 0x3C},
	{0x3009, 0x3C},
	{0x300A, 0x28},
	{0x300B, 0x04},
	{0x300C, 0x0A},
	{0x300D, 0x02},
	{0x300E, 0xE8},
	{0x300F, 0x82},
	{0x3010, 0x00},
	{0x3011, 0x4C},
	{0x3012, 0x30},
	{0x3013, 0xC0},
	{0x3014, 0x00},
	{0x3015, 0x00},
	{0x3016, 0x2C},
	{0x3017, 0x94},
	{0x3018, 0x78},
	{0x301B, 0x75},
	{0x301C, 0x04},
	{0x301D, 0xD4},
	{0x3021, 0x02},
	{0x3022, 0x24},
	{0x3024, 0x40},
	{0x3027, 0x08},
	{0x3029, 0xC6},
	{0x30BC, 0xB0},
	{0x302B, 0x01},
	{0x30D8, 0x3F},
	//+++++++++++++++++++++++++++++++//
	// ADLC setting ...
	{0x3070, 0x5F},
	{0x3071, 0x00},
	{0x3080, 0x04},
	{0x3081, 0x38},
};

LOCAL const SENSOR_REG_T s5k4e1ga_1280X960_mipi_raw[] = {

	// MIPI 1304 x 980 @ 30fps
	// MCLK 24Mhz, 404Mbps per lane

	// Analog Setting
	{0x301B, 0x83},
	//+++++++++++++++++++++++++++++++//
	// MIPI setting
	{0x30BD, 0x00},//SEL_CCP[0]
	{0x3084, 0x15},//SYNC Mode
	{0x30BE, 0x1A},//M_PCLKDIV_AUTO[4], M_DIV_PCLK[3:0]
	{0x30C1, 0x01},//pack video enable [0]
	{0x30EE, 0x02},//DPHY enable [1]
	{0x3111, 0xC6},//Embedded data off [5]
	{0x0105, 0x01},

	//Factory only set END
	//+++++++++++++++++++++++++++++++//
	// Integration setting ...
	{0x0202, 0x03},//coarse integration time
	{0x0203, 0xD4},
	{0x0204, 0x00},//analog gain[msb] 0100 x8 0080 x4
	{0x0205, 0x80},//analog gain[lsb] 0040 x2 0020 x1
	// Frame Length
	{0x0340, 0x03},//Capture 07B4(1960[# of row]+12[V-blank])
	{0x0341, 0xE0},//SXGA 03E0(980[# of row]+12[V-blank])
	// Line Length
	{0x0342, 0x0A},//2738
	{0x0343, 0xB2},

	//+++++++++++++++++++++++++++++++//
	// PLL setting ...
	//// input clock 24MHz
	////// (3) MIPI 2-lane Serial(TST = 0000b or TST = 0010b), 15 fps
	{0x0305, 0x06},//PLL P = 6
	{0x0306, 0x00},//PLL M[8] = 0
	{0x0307, 0x65},//PLL M = 101
	{0x30B5, 0x01},//PLL S = 1
	{0x30E2, 0x02},//num lanes[1:0] = 2
	{0x30F1, 0xB0},//DPHY BANDCTRL 404MHz=40.4MHz

	// MIPI Size Setting
	{0x30A9, 0x02},//Horizontal Binning On
	{0x300E, 0xEB},//Vertical Binning On

	//////////////////////////for 1304*980 //////////////

	{0x0344,0x00},//x_addr_start	  //24=0x18,1280x2=2560,2608-2560=48d 48d/2=24d  Jack_20120419
	{0x0345,0x18},
	{0x0346,0x00},//y_addr_start
	{0x0347,0x14},
	{0x0348,0x0A},//x_addr_end 2583   //2583=0x0A17,24+2560=2584	Jack_20120419
	{0x0349,0x17},
	{0x034A,0x07},//y_addr_end 1939
	{0x034B,0x93},

	{0x0380, 0x00},//x_even_inc 1
	{0x0381, 0x01},
	{0x0382, 0x00},//x_odd_inc 1
	{0x0383, 0x01},
	{0x0384, 0x00},//y_even_inc 1
	{0x0385, 0x01},
	{0x0386, 0x00},//y_odd_inc 3
	{0x0387, 0x03},
	{0x034C,0x05},//x_output_size 1280  //1280=0x0500 for Simmian Jack_20120419
	{0x034D,0x00},
	{0x034E,0x03},//y_output_size 960
	{0x034F,0xC0},

	{0x30BF, 0xAB}, //outif_enable[7], data_type[5:0](2Bh = bayer 10bit)
	{0x30C0, 0x40}, //video_offset[7:4] 1630%12
	{0x30C8, 0x06}, //video_data_length 1630 = 1304 * 1.25
	{0x30C9, 0x40},
};

LOCAL const SENSOR_REG_T s5k4e1ga_2592X1944_mipi_raw[] = {
	// MIPI 2608 x 1960 @ 15fps
	// MCLK 24Mhz, 404Mbps per lane

	// Analog Setting
	{0x301B, 0x75},
	//+++++++++++++++++++++++++++++++//
	// MIPI setting
	{0x30BD, 0x00},//SEL_CCP[0]
	{0x3084, 0x15},//SYNC Mode
	{0x30BE, 0x1A},//M_PCLKDIV_AUTO[4], M_DIV_PCLK[3:0]
	{0x30C1, 0x01},//pack video enable [0]
	{0x30EE, 0x02},//DPHY enable [1]
	{0x3111, 0xC6},//Embedded data off [5]
	{0x0105, 0x01},


	//Factory only set END
	//+++++++++++++++++++++++++++++++//
	// Integration setting ...
	//{0x0202, 0x07},//coarse integration time
	//{0x0203, 0xA8},
	//{0x0204, 0x00},//analog gain[msb] 0100 x8 0080 x4
	//{0x0205, 0x80},//analog gain[lsb] 0040 x2 0020 x1
	// Frame Length
	{0x0340, 0x07},//Capture 07B4(1960[# of row]+12[V-blank])
	{0x0341, 0xB4},//SXGA 03E0(980[# of row]+12[V-blank])
	// Line Length
	{0x0342, 0x0A},//2738
	{0x0343, 0xB2},

	//+++++++++++++++++++++++++++++++//
	// PLL setting ...
	//// input clock 24MHz
	////// (3) MIPI 2-lane Serial(TST = 0000b or TST = 0010b), 15 fps
	{0x0305, 0x06},//PLL P = 6
	{0x0306, 0x00},//PLL M[8] = 0
	{0x0307, 0x65},//PLL M = 101
	{0x30B5, 0x01},//PLL S = 1
	{0x30E2, 0x02},//num lanes[1:0] = 2
	{0x30F1, 0xB0},//DPHY BANDCTRL 404MHz=40.4MHz

	// MIPI Size Setting
	{0x30A9, 0x03},//Horizontal Binning Off
	{0x300E, 0xE8},//Vertical Binning Off

	////////////////////////////for 2608*1960 //////////////
	{0x0344,0x00},//x_addr_start	  //24=0x18,1280x2=2560,2608-2560=48d 48d/2=24d  Jack_20120419
	{0x0345,0x10},
	{0x0346,0x00},//y_addr_start
	{0x0347,0x14},
	{0x0348,0x0A},//x_addr_end 2583   //2583=0x0A17,24+2560=2584	Jack_20120419
	{0x0349,0x2F},
	{0x034A,0x07},//y_addr_end 1939
	{0x034B,0xAB},

	{0x0380, 0x00},//x_even_inc 1
	{0x0381, 0x01},
	{0x0382, 0x00},//x_odd_inc 1
	{0x0383, 0x01},
	{0x0384, 0x00},//y_even_inc 1
	{0x0385, 0x01},
	{0x0386, 0x00},//y_odd_inc 3
	{0x0387, 0x01},


	{0x034C, 0x0A},//x_output size
	{0x034D, 0x20},
	{0x034E, 0x07},//y_output size
	{0x034F, 0x98},

	{0x30BF, 0xAB},//outif_enable[7], data_type[5:0](2Bh = bayer 10bit)
	{0x30C0, 0x00},//video_offset[7:4] 3260%12
	{0x30C8, 0x0C},//video_data_length 3260 = 2608 * 1.25
	{0x30C9, 0xA8},
};

LOCAL SENSOR_REG_TAB_INFO_T s_s5k4e1ga_resolution_Tab_RAW[] = {
	{ADDR_AND_LEN_OF_ARRAY(s5k4e1ga_com_mipi_raw), 0, 0, 12, SENSOR_IMAGE_FORMAT_RAW},

	{ADDR_AND_LEN_OF_ARRAY(s5k4e1ga_1280X960_mipi_raw), 1280, 960, 24, SENSOR_IMAGE_FORMAT_RAW},
	{ADDR_AND_LEN_OF_ARRAY(s5k4e1ga_2592X1944_mipi_raw), 2592, 1944, 24, SENSOR_IMAGE_FORMAT_RAW},
	{PNULL, 0, 0, 0, 0, 0},
	{PNULL, 0, 0, 0, 0, 0},
	{PNULL, 0, 0, 0, 0, 0},
	{PNULL, 0, 0, 0, 0, 0},
	{PNULL, 0, 0, 0, 0, 0},
	{PNULL, 0, 0, 0, 0, 0}
};

LOCAL SENSOR_TRIM_T s_s5k4e1ga_Resolution_Trim_Tab[] = {
	{0, 0, 0, 0, 0, 0},
	{0, 0, 1280, 960, 339, 80},
	{0, 0, 2592, 1944, 339, 80},
	{0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0}
};

static struct sensor_raw_info s_s5k4e1ga_mipi_raw_info={
	&s_s5k4e1ga_version_info,
	&s_s5k4e1ga_tune_info,
	&s_s5k4e1ga_fix_info,
	&s_s5k4e1ga_cali_info,
};

struct sensor_raw_info* s_s5k4e1ga_mipi_raw_info_ptr=&s_s5k4e1ga_mipi_raw_info;

LOCAL SENSOR_IOCTL_FUNC_TAB_T s_s5k4e1ga_ioctl_func_tab = {
	PNULL,
	_s5k4e1ga_PowerOn,
	PNULL,
	_s5k4e1ga_Identify,

	PNULL,			// write register
	PNULL,			// read  register
	PNULL,
	_s5k4e1ga_GetResolutionTrimTab,

	// External
	PNULL,
	PNULL,
	PNULL,

	PNULL, //_s5k4e1ga_set_brightness,
	PNULL, // _s5k4e1ga_set_contrast,
	PNULL,
	PNULL,			//_s5k4e1ga_set_saturation,

	PNULL, //_s5k4e1ga_set_work_mode,
	PNULL, //_s5k4e1ga_set_image_effect,

	_s5k4e1ga_BeforeSnapshot,
	_s5k4e1ga_after_snapshot,
	PNULL,//_ov540_flash,
	PNULL,
	_s5k4e1ga_write_exposure,
	PNULL,
	_s5k4e1ga_write_gain,
	PNULL,
	PNULL,
	_s5k4e1ga_write_af,
	PNULL,
	PNULL, //_s5k4e1ga_set_awb,
	PNULL,
	PNULL,
	PNULL, //_s5k4e1ga_set_ev,
	PNULL,
	PNULL,
	PNULL,
	PNULL, //_s5k4e1ga_GetExifInfo,
	_s5k4e1ga_ExtFunc,
	PNULL, //_s5k4e1ga_set_anti_flicker,
	PNULL, //_s5k4e1ga_set_video_mode,
	PNULL, //pick_jpeg_stream
	PNULL,  //meter_mode
	PNULL, //get_status
	_s5k4e1ga_StreamOn,
	_s5k4e1ga_StreamOff,
};


SENSOR_INFO_T g_s5k4e1ga_mipi_raw_info = {
	s5k4e1ga_I2C_ADDR_W,	// salve i2c write address
	s5k4e1ga_I2C_ADDR_R,	// salve i2c read address

	SENSOR_I2C_REG_16BIT | SENSOR_I2C_VAL_8BIT,	// bit0: 0: i2c register value is 8 bit, 1: i2c register value is 16 bit
	// bit1: 0: i2c register addr  is 8 bit, 1: i2c register addr  is 16 bit
	// other bit: reseved
	SENSOR_HW_SIGNAL_PCLK_N | SENSOR_HW_SIGNAL_VSYNC_N | SENSOR_HW_SIGNAL_HSYNC_P,	// bit0: 0:negative; 1:positive -> polarily of pixel clock
	// bit2: 0:negative; 1:positive -> polarily of horizontal synchronization signal
	// bit4: 0:negative; 1:positive -> polarily of vertical synchronization signal
	// other bit: reseved

	// preview mode
	SENSOR_ENVIROMENT_NORMAL | SENSOR_ENVIROMENT_NIGHT,

	// image effect
	SENSOR_IMAGE_EFFECT_NORMAL |
	    SENSOR_IMAGE_EFFECT_BLACKWHITE |
	    SENSOR_IMAGE_EFFECT_RED |
	    SENSOR_IMAGE_EFFECT_GREEN |
	    SENSOR_IMAGE_EFFECT_BLUE |
	    SENSOR_IMAGE_EFFECT_YELLOW |
	    SENSOR_IMAGE_EFFECT_NEGATIVE | SENSOR_IMAGE_EFFECT_CANVAS,

	// while balance mode
	0,

	7,			// bit[0:7]: count of step in brightness, contrast, sharpness, saturation
	// bit[8:31] reseved

	SENSOR_HIGH_PULSE_RESET,	// reset pulse level
	50,			// reset pulse width(ms)

	SENSOR_LOW_LEVEL_PWDN,	// 1: high level valid; 0: low level valid

	1,			// count of identify code
	{{0x00, 0x4e},		// supply two code to identify sensor.
	 {0x01, 0x10}},		// for Example: index = 0-> Device id, index = 1 -> version id

	SENSOR_AVDD_2800MV,	// voltage of avdd

	2592,			// max width of source image
	1944,			// max height of source image
	"s5k4e1ga",		// name of sensor

	SENSOR_IMAGE_FORMAT_RAW,	// define in SENSOR_IMAGE_FORMAT_E enum,SENSOR_IMAGE_FORMAT_MAX
	// if set to SENSOR_IMAGE_FORMAT_MAX here, image format depent on SENSOR_REG_TAB_INFO_T

	SENSOR_IMAGE_PATTERN_RAWRGB_GR,// pattern of input image form sensor;

	s_s5k4e1ga_resolution_Tab_RAW,	// point to resolution table information structure
	&s_s5k4e1ga_ioctl_func_tab,	// point to ioctl function table
	&s_s5k4e1ga_mipi_raw_info,		// information and table about Rawrgb sensor
	NULL,			//&g_s5k4e1ga_ext_info,                // extend information about sensor
	SENSOR_AVDD_1800MV,	// iovdd
	SENSOR_AVDD_1800MV,	// dvdd
	3,			// skip frame num before preview
	3,			// skip frame num before capture
	0,			// deci frame num during preview
	0,			// deci frame num during video preview

	0,
	0,
	0,
	0,
	0,
	{SENSOR_INTERFACE_TYPE_CSI2, 2, 10, 0},
	PNULL,
	3,			// skip frame num while change setting
};

LOCAL struct sensor_raw_info* Sensor_GetContext(void)
{
	return s_s5k4e1ga_mipi_raw_info_ptr;
}

LOCAL uint32_t Sensor_InitRawTuneInfo(void)
{
	uint32_t rtn=0x00;
	struct sensor_raw_info* raw_sensor_ptr=Sensor_GetContext();
	struct sensor_raw_tune_info* sensor_ptr=raw_sensor_ptr->tune_ptr;

	raw_sensor_ptr->version_info->version_id=0x00000000;
	raw_sensor_ptr->version_info->srtuct_size=sizeof(struct sensor_raw_info);

	//bypass
	sensor_ptr->version_id=0x00000000;
	sensor_ptr->blc_bypass=0x00;
	sensor_ptr->nlc_bypass=0x01;
	sensor_ptr->lnc_bypass=0x00;
	sensor_ptr->ae_bypass=0x00;
	sensor_ptr->awb_bypass=0x00;
	sensor_ptr->bpc_bypass=0x00;
	sensor_ptr->denoise_bypass=0x00;
	sensor_ptr->grgb_bypass=0x01;
	sensor_ptr->cmc_bypass=0x00;
	sensor_ptr->gamma_bypass=0x00;
	sensor_ptr->uvdiv_bypass=0x01;
	sensor_ptr->pref_bypass=0x00;
	sensor_ptr->bright_bypass=0x00;
	sensor_ptr->contrast_bypass=0x00;
	sensor_ptr->hist_bypass=0x01;
	sensor_ptr->auto_contrast_bypass=0x01;
	sensor_ptr->af_bypass=0x00;
	sensor_ptr->edge_bypass=0x01;
	sensor_ptr->fcs_bypass=0x00;
	sensor_ptr->css_bypass=0x01;
	sensor_ptr->saturation_bypass=0x01;
	sensor_ptr->hdr_bypass=0x01;
	sensor_ptr->glb_gain_bypass=0x01;
	sensor_ptr->chn_gain_bypass=0x01;

	//blc
	sensor_ptr->blc.mode=0x00;
	sensor_ptr->blc.offset[0].r=0x38;
	sensor_ptr->blc.offset[0].gr=0x35;
	sensor_ptr->blc.offset[0].gb=0x35;
	sensor_ptr->blc.offset[0].b=0x38;
	//nlc
	sensor_ptr->nlc.r_node[0]=0;
	sensor_ptr->nlc.r_node[1]=16;
	sensor_ptr->nlc.r_node[2]=32;
	sensor_ptr->nlc.r_node[3]=64;
	sensor_ptr->nlc.r_node[4]=96;
	sensor_ptr->nlc.r_node[5]=128;
	sensor_ptr->nlc.r_node[6]=160;
	sensor_ptr->nlc.r_node[7]=192;
	sensor_ptr->nlc.r_node[8]=224;
	sensor_ptr->nlc.r_node[9]=256;
	sensor_ptr->nlc.r_node[10]=288;
	sensor_ptr->nlc.r_node[11]=320;
	sensor_ptr->nlc.r_node[12]=384;
	sensor_ptr->nlc.r_node[13]=448;
	sensor_ptr->nlc.r_node[14]=512;
	sensor_ptr->nlc.r_node[15]=576;
	sensor_ptr->nlc.r_node[16]=640;
	sensor_ptr->nlc.r_node[17]=672;
	sensor_ptr->nlc.r_node[18]=704;
	sensor_ptr->nlc.r_node[19]=736;
	sensor_ptr->nlc.r_node[20]=768;
	sensor_ptr->nlc.r_node[21]=800;
	sensor_ptr->nlc.r_node[22]=832;
	sensor_ptr->nlc.r_node[23]=864;
	sensor_ptr->nlc.r_node[24]=896;
	sensor_ptr->nlc.r_node[25]=928;
	sensor_ptr->nlc.r_node[26]=960;
	sensor_ptr->nlc.r_node[27]=992;
	sensor_ptr->nlc.r_node[28]=1023;

	sensor_ptr->nlc.g_node[0]=0;
	sensor_ptr->nlc.g_node[1]=16;
	sensor_ptr->nlc.g_node[2]=32;
	sensor_ptr->nlc.g_node[3]=64;
	sensor_ptr->nlc.g_node[4]=96;
	sensor_ptr->nlc.g_node[5]=128;
	sensor_ptr->nlc.g_node[6]=160;
	sensor_ptr->nlc.g_node[7]=192;
	sensor_ptr->nlc.g_node[8]=224;
	sensor_ptr->nlc.g_node[9]=256;
	sensor_ptr->nlc.g_node[10]=288;
	sensor_ptr->nlc.g_node[11]=320;
	sensor_ptr->nlc.g_node[12]=384;
	sensor_ptr->nlc.g_node[13]=448;
	sensor_ptr->nlc.g_node[14]=512;
	sensor_ptr->nlc.g_node[15]=576;
	sensor_ptr->nlc.g_node[16]=640;
	sensor_ptr->nlc.g_node[17]=672;
	sensor_ptr->nlc.g_node[18]=704;
	sensor_ptr->nlc.g_node[19]=736;
	sensor_ptr->nlc.g_node[20]=768;
	sensor_ptr->nlc.g_node[21]=800;
	sensor_ptr->nlc.g_node[22]=832;
	sensor_ptr->nlc.g_node[23]=864;
	sensor_ptr->nlc.g_node[24]=896;
	sensor_ptr->nlc.g_node[25]=928;
	sensor_ptr->nlc.g_node[26]=960;
	sensor_ptr->nlc.g_node[27]=992;
	sensor_ptr->nlc.g_node[28]=1023;

	sensor_ptr->nlc.b_node[0]=0;
	sensor_ptr->nlc.b_node[1]=16;
	sensor_ptr->nlc.b_node[2]=32;
	sensor_ptr->nlc.b_node[3]=64;
	sensor_ptr->nlc.b_node[4]=96;
	sensor_ptr->nlc.b_node[5]=128;
	sensor_ptr->nlc.b_node[6]=160;
	sensor_ptr->nlc.b_node[7]=192;
	sensor_ptr->nlc.b_node[8]=224;
	sensor_ptr->nlc.b_node[9]=256;
	sensor_ptr->nlc.b_node[10]=288;
	sensor_ptr->nlc.b_node[11]=320;
	sensor_ptr->nlc.b_node[12]=384;
	sensor_ptr->nlc.b_node[13]=448;
	sensor_ptr->nlc.b_node[14]=512;
	sensor_ptr->nlc.b_node[15]=576;
	sensor_ptr->nlc.b_node[16]=640;
	sensor_ptr->nlc.b_node[17]=672;
	sensor_ptr->nlc.b_node[18]=704;
	sensor_ptr->nlc.b_node[19]=736;
	sensor_ptr->nlc.b_node[20]=768;
	sensor_ptr->nlc.b_node[21]=800;
	sensor_ptr->nlc.b_node[22]=832;
	sensor_ptr->nlc.b_node[23]=864;
	sensor_ptr->nlc.b_node[24]=896;
	sensor_ptr->nlc.b_node[25]=928;
	sensor_ptr->nlc.b_node[26]=960;
	sensor_ptr->nlc.b_node[27]=992;
	sensor_ptr->nlc.b_node[28]=1023;

	sensor_ptr->nlc.l_node[0]=0;
	sensor_ptr->nlc.l_node[1]=16;
	sensor_ptr->nlc.l_node[2]=32;
	sensor_ptr->nlc.l_node[3]=64;
	sensor_ptr->nlc.l_node[4]=96;
	sensor_ptr->nlc.l_node[5]=128;
	sensor_ptr->nlc.l_node[6]=160;
	sensor_ptr->nlc.l_node[7]=192;
	sensor_ptr->nlc.l_node[8]=224;
	sensor_ptr->nlc.l_node[9]=256;
	sensor_ptr->nlc.l_node[10]=288;
	sensor_ptr->nlc.l_node[11]=320;
	sensor_ptr->nlc.l_node[12]=384;
	sensor_ptr->nlc.l_node[13]=448;
	sensor_ptr->nlc.l_node[14]=512;
	sensor_ptr->nlc.l_node[15]=576;
	sensor_ptr->nlc.l_node[16]=640;
	sensor_ptr->nlc.l_node[17]=672;
	sensor_ptr->nlc.l_node[18]=704;
	sensor_ptr->nlc.l_node[19]=736;
	sensor_ptr->nlc.l_node[20]=768;
	sensor_ptr->nlc.l_node[21]=800;
	sensor_ptr->nlc.l_node[22]=832;
	sensor_ptr->nlc.l_node[23]=864;
	sensor_ptr->nlc.l_node[24]=896;
	sensor_ptr->nlc.l_node[25]=928;
	sensor_ptr->nlc.l_node[26]=960;
	sensor_ptr->nlc.l_node[27]=992;
	sensor_ptr->nlc.l_node[28]=1023;

	//ae
	sensor_ptr->ae.skip_frame=0x01;
	sensor_ptr->ae.normal_fix_fps=0x1e;
	sensor_ptr->ae.night_fix_fps=0x1e;
	sensor_ptr->ae.target_lum=60;
	sensor_ptr->ae.target_zone=8;
	sensor_ptr->ae.quick_mode=1;
	sensor_ptr->ae.ev[0]=0xe8;
	sensor_ptr->ae.ev[1]=0xf0;
	sensor_ptr->ae.ev[2]=0xf8;
	sensor_ptr->ae.ev[3]=0x00;
	sensor_ptr->ae.ev[4]=0x08;
	sensor_ptr->ae.ev[5]=0x10;
	sensor_ptr->ae.ev[6]=0x18;
	sensor_ptr->ae.ev[7]=0x00;
	sensor_ptr->ae.ev[8]=0x00;
	sensor_ptr->ae.ev[9]=0x00;
	sensor_ptr->ae.ev[10]=0x00;
	sensor_ptr->ae.ev[11]=0x00;
	sensor_ptr->ae.ev[12]=0x00;
	sensor_ptr->ae.ev[13]=0x00;
	sensor_ptr->ae.ev[14]=0x00;
	sensor_ptr->ae.ev[15]=0x00;

	//awb
	sensor_ptr->awb.win_start.x=0x00;
	sensor_ptr->awb.win_start.y=0x00;
	sensor_ptr->awb.win_size.w=40;
	sensor_ptr->awb.win_size.h=30;
	sensor_ptr->awb.r_gain[0]=0x1b0;
	sensor_ptr->awb.g_gain[0]=0xff;
	sensor_ptr->awb.b_gain[0]=0x180;
	sensor_ptr->awb.r_gain[1]=0x100;
	sensor_ptr->awb.g_gain[1]=0xff;
	sensor_ptr->awb.b_gain[1]=0x210;
	sensor_ptr->awb.r_gain[2]=0xff;
	sensor_ptr->awb.g_gain[2]=0xff;
	sensor_ptr->awb.b_gain[2]=0xff;
	sensor_ptr->awb.r_gain[3]=0xff;
	sensor_ptr->awb.g_gain[3]=0xff;
	sensor_ptr->awb.b_gain[3]=0xff;
	sensor_ptr->awb.r_gain[4]=0x13e;
	sensor_ptr->awb.g_gain[4]=0xff;
	sensor_ptr->awb.b_gain[4]=0x1b4;
	sensor_ptr->awb.r_gain[5]=0x194;
	sensor_ptr->awb.g_gain[5]=0xff;
	sensor_ptr->awb.b_gain[5]=0x134;
	sensor_ptr->awb.r_gain[6]=0x240;
	sensor_ptr->awb.g_gain[6]=0xff;
	sensor_ptr->awb.b_gain[6]=0x12c;
	sensor_ptr->awb.r_gain[7]=0xff;
	sensor_ptr->awb.g_gain[7]=0xff;
	sensor_ptr->awb.b_gain[7]=0xff;
	sensor_ptr->awb.r_gain[8]=0xff;
	sensor_ptr->awb.g_gain[8]=0xff;
	sensor_ptr->awb.b_gain[8]=0xff;
	sensor_ptr->awb.target_zone=0x40;

	/*awb cali*/
	sensor_ptr->awb.quick_mode=1;

	/*awb win*/
	sensor_ptr->awb.win[0].x=0x7e;
	sensor_ptr->awb.win[1].x=0x85;
	sensor_ptr->awb.win[2].x=0x8b;
	sensor_ptr->awb.win[3].x=0x91;
	sensor_ptr->awb.win[4].x=0x95;
	sensor_ptr->awb.win[5].x=0x99;
	sensor_ptr->awb.win[6].x=0x9f;
	sensor_ptr->awb.win[7].x=0xa4;
	sensor_ptr->awb.win[8].x=0xab;
	sensor_ptr->awb.win[9].x=0xb2;
	sensor_ptr->awb.win[10].x=0xbb;
	sensor_ptr->awb.win[11].x=0xc4;
	sensor_ptr->awb.win[12].x=0xd1;
	sensor_ptr->awb.win[13].x=0xdd;
	sensor_ptr->awb.win[14].x=0xe5;
	sensor_ptr->awb.win[15].x=0xef;
	sensor_ptr->awb.win[16].x=0xf7;
	sensor_ptr->awb.win[17].x=0xff;
	sensor_ptr->awb.win[18].x=0x010a;
	sensor_ptr->awb.win[19].x=0x011b;

	sensor_ptr->awb.win[0].yt=0x0108;
	sensor_ptr->awb.win[1].yt=0x010f;
	sensor_ptr->awb.win[2].yt=0x010d;
	sensor_ptr->awb.win[3].yt=0x010d;
	sensor_ptr->awb.win[4].yt=0x0109;
	sensor_ptr->awb.win[5].yt=0x0105;
	sensor_ptr->awb.win[6].yt=0xff;
	sensor_ptr->awb.win[7].yt=0xf5;
	sensor_ptr->awb.win[8].yt=0xe8;
	sensor_ptr->awb.win[9].yt=0xd0;
	sensor_ptr->awb.win[10].yt=0xbe;
	sensor_ptr->awb.win[11].yt=0xac;
	sensor_ptr->awb.win[12].yt=0x9c;
	sensor_ptr->awb.win[13].yt=0x8d;
	sensor_ptr->awb.win[14].yt=0x8f;
	sensor_ptr->awb.win[15].yt=0x8f;
	sensor_ptr->awb.win[16].yt=0x92;
	sensor_ptr->awb.win[17].yt=0x8f;
	sensor_ptr->awb.win[18].yt=0x8b;
	sensor_ptr->awb.win[19].yt=0x84;

	sensor_ptr->awb.win[0].yb=0xf9;
	sensor_ptr->awb.win[1].yb=0xee;
	sensor_ptr->awb.win[2].yb=0xde;
	sensor_ptr->awb.win[3].yb=0xd0;
	sensor_ptr->awb.win[4].yb=0xc5;
	sensor_ptr->awb.win[5].yb=0xb6;
	sensor_ptr->awb.win[6].yb=0xa3;
	sensor_ptr->awb.win[7].yb=0x91;
	sensor_ptr->awb.win[8].yb=0x7b;
	sensor_ptr->awb.win[9].yb=0x74;
	sensor_ptr->awb.win[10].yb=0x6f;
	sensor_ptr->awb.win[11].yb=0x6e;
	sensor_ptr->awb.win[12].yb=0x7e;
	sensor_ptr->awb.win[13].yb=0x78;
	sensor_ptr->awb.win[14].yb=0x6a;
	sensor_ptr->awb.win[15].yb=0x61;
	sensor_ptr->awb.win[16].yb=0x63;
	sensor_ptr->awb.win[17].yb=0x63;
	sensor_ptr->awb.win[18].yb=0x63;
	sensor_ptr->awb.win[19].yb=0x59;

	//bpc
	sensor_ptr->bpc.flat_thr=80;
	sensor_ptr->bpc.std_thr=20;
	sensor_ptr->bpc.texture_thr=2;

	// denoise
	sensor_ptr->denoise.write_back=0x02;
	sensor_ptr->denoise.r_thr=0x08;
	sensor_ptr->denoise.g_thr=0x08;
	sensor_ptr->denoise.b_thr=0x08;

	sensor_ptr->denoise.diswei[0]=255;
	sensor_ptr->denoise.diswei[1]=247;
	sensor_ptr->denoise.diswei[2]=239;
	sensor_ptr->denoise.diswei[3]=232;
	sensor_ptr->denoise.diswei[4]=225;
	sensor_ptr->denoise.diswei[5]=218;
	sensor_ptr->denoise.diswei[6]=211;
	sensor_ptr->denoise.diswei[7]=204;
	sensor_ptr->denoise.diswei[8]=198;
	sensor_ptr->denoise.diswei[9]=192;
	sensor_ptr->denoise.diswei[10]=186;
	sensor_ptr->denoise.diswei[11]=180;
	sensor_ptr->denoise.diswei[12]=175;
	sensor_ptr->denoise.diswei[13]=169;
	sensor_ptr->denoise.diswei[14]=164;
	sensor_ptr->denoise.diswei[15]=159;
	sensor_ptr->denoise.diswei[16]=154;
	sensor_ptr->denoise.diswei[17]=149;
	sensor_ptr->denoise.diswei[18]=145;

	sensor_ptr->denoise.ranwei[0]=255;
	sensor_ptr->denoise.ranwei[1]=247;
	sensor_ptr->denoise.ranwei[2]=225;
	sensor_ptr->denoise.ranwei[3]=192;
	sensor_ptr->denoise.ranwei[4]=154;
	sensor_ptr->denoise.ranwei[5]=116;
	sensor_ptr->denoise.ranwei[6]=82;
	sensor_ptr->denoise.ranwei[7]=55;
	sensor_ptr->denoise.ranwei[8]=34;
	sensor_ptr->denoise.ranwei[9]=20;
	sensor_ptr->denoise.ranwei[10]=19;
	sensor_ptr->denoise.ranwei[11]=18;
	sensor_ptr->denoise.ranwei[12]=17;
	sensor_ptr->denoise.ranwei[13]=16;
	sensor_ptr->denoise.ranwei[14]=15;
	sensor_ptr->denoise.ranwei[15]=14;
	sensor_ptr->denoise.ranwei[16]=13;
	sensor_ptr->denoise.ranwei[17]=12;
	sensor_ptr->denoise.ranwei[18]=11;
	sensor_ptr->denoise.ranwei[19]=10;
	sensor_ptr->denoise.ranwei[20]=9;
	sensor_ptr->denoise.ranwei[21]=8;
	sensor_ptr->denoise.ranwei[22]=7;
	sensor_ptr->denoise.ranwei[23]=6;
	sensor_ptr->denoise.ranwei[24]=5;
	sensor_ptr->denoise.ranwei[25]=4;
	sensor_ptr->denoise.ranwei[26]=3;
	sensor_ptr->denoise.ranwei[27]=2;
	sensor_ptr->denoise.ranwei[28]=1;
	sensor_ptr->denoise.ranwei[29]=1;
	sensor_ptr->denoise.ranwei[30]=1;

	//GrGb
	sensor_ptr->grgb.edge_thr=26;
	sensor_ptr->grgb.diff_thr=80;
	//cfa
	sensor_ptr->cfa.edge_thr=0x1a;
	sensor_ptr->cfa.diff_thr=0x00;
	//cmc
	sensor_ptr->cmc.matrix[0][0]=0x06f5;
	sensor_ptr->cmc.matrix[0][1]=0x3e0b;
	sensor_ptr->cmc.matrix[0][2]=0x3f00;
	sensor_ptr->cmc.matrix[0][3]=0x3e67;
	sensor_ptr->cmc.matrix[0][4]=0x06b8;
	sensor_ptr->cmc.matrix[0][5]=0x3ee1;
	sensor_ptr->cmc.matrix[0][6]=0x3fc3;
	sensor_ptr->cmc.matrix[0][7]=0x3d71;
	sensor_ptr->cmc.matrix[0][8]=0x06cc;
	//Gamma
	sensor_ptr->gamma.axis[0][0]=0;
	sensor_ptr->gamma.axis[0][1]=8;
	sensor_ptr->gamma.axis[0][2]=16;
	sensor_ptr->gamma.axis[0][3]=24;
	sensor_ptr->gamma.axis[0][4]=32;
	sensor_ptr->gamma.axis[0][5]=48;
	sensor_ptr->gamma.axis[0][6]=64;
	sensor_ptr->gamma.axis[0][7]=80;
	sensor_ptr->gamma.axis[0][8]=96;
	sensor_ptr->gamma.axis[0][9]=128;
	sensor_ptr->gamma.axis[0][10]=160;
	sensor_ptr->gamma.axis[0][11]=192;
	sensor_ptr->gamma.axis[0][12]=224;
	sensor_ptr->gamma.axis[0][13]=256;
	sensor_ptr->gamma.axis[0][14]=288;
	sensor_ptr->gamma.axis[0][15]=320;
	sensor_ptr->gamma.axis[0][16]=384;
	sensor_ptr->gamma.axis[0][17]=448;
	sensor_ptr->gamma.axis[0][18]=512;
	sensor_ptr->gamma.axis[0][19]=576;
	sensor_ptr->gamma.axis[0][20]=640;
	sensor_ptr->gamma.axis[0][21]=768;
	sensor_ptr->gamma.axis[0][22]=832;
	sensor_ptr->gamma.axis[0][23]=896;
	sensor_ptr->gamma.axis[0][24]=960;
	sensor_ptr->gamma.axis[0][25]=1023;

	sensor_ptr->gamma.axis[1][0]=0x00;
	sensor_ptr->gamma.axis[1][1]=0x05;
	sensor_ptr->gamma.axis[1][2]=0x09;
	sensor_ptr->gamma.axis[1][3]=0x0e;
	sensor_ptr->gamma.axis[1][4]=0x15;
	sensor_ptr->gamma.axis[1][5]=0x20;
	sensor_ptr->gamma.axis[1][6]=0x33;
	sensor_ptr->gamma.axis[1][7]=0x41;
	sensor_ptr->gamma.axis[1][8]=0x50;
	sensor_ptr->gamma.axis[1][9]=0x61;
	sensor_ptr->gamma.axis[1][10]=0x6e;
	sensor_ptr->gamma.axis[1][11]=0x7c;
	sensor_ptr->gamma.axis[1][12]=0x86;
	sensor_ptr->gamma.axis[1][13]=0x90;
	sensor_ptr->gamma.axis[1][14]=0x99;
	sensor_ptr->gamma.axis[1][15]=0xa3;
	sensor_ptr->gamma.axis[1][16]=0xb0;
	sensor_ptr->gamma.axis[1][17]=0xbd;
	sensor_ptr->gamma.axis[1][18]=0xcb;
	sensor_ptr->gamma.axis[1][19]=0xd3;
	sensor_ptr->gamma.axis[1][20]=0xdc;
	sensor_ptr->gamma.axis[1][21]=0xe8;
	sensor_ptr->gamma.axis[1][22]=0xef;
	sensor_ptr->gamma.axis[1][23]=0xf5;
	sensor_ptr->gamma.axis[1][24]=0xf9;
	sensor_ptr->gamma.axis[1][25]=0xff;

	//uv div
	sensor_ptr->uv_div.thrd[0]=252;
	sensor_ptr->uv_div.thrd[1]=250;
	sensor_ptr->uv_div.thrd[2]=248;
	sensor_ptr->uv_div.thrd[3]=246;
	sensor_ptr->uv_div.thrd[4]=244;
	sensor_ptr->uv_div.thrd[5]=242;
	sensor_ptr->uv_div.thrd[6]=240;

	//pref
	sensor_ptr->pref.write_back=0x00;
	sensor_ptr->pref.y_thr=0x04;
	sensor_ptr->pref.u_thr=0x04;
	sensor_ptr->pref.v_thr=0x04;
	//bright
	sensor_ptr->bright.factor[0]=0xd0;
	sensor_ptr->bright.factor[1]=0xe0;
	sensor_ptr->bright.factor[2]=0xf0;
	sensor_ptr->bright.factor[3]=0x00;
	sensor_ptr->bright.factor[4]=0x10;
	sensor_ptr->bright.factor[5]=0x20;
	sensor_ptr->bright.factor[6]=0x30;
	sensor_ptr->bright.factor[7]=0x00;
	sensor_ptr->bright.factor[8]=0x00;
	sensor_ptr->bright.factor[9]=0x00;
	sensor_ptr->bright.factor[10]=0x00;
	sensor_ptr->bright.factor[11]=0x00;
	sensor_ptr->bright.factor[12]=0x00;
	sensor_ptr->bright.factor[13]=0x00;
	sensor_ptr->bright.factor[14]=0x00;
	sensor_ptr->bright.factor[15]=0x00;
	//contrast
	sensor_ptr->contrast.factor[0]=0x10;
	sensor_ptr->contrast.factor[1]=0x20;
	sensor_ptr->contrast.factor[2]=0x30;
	sensor_ptr->contrast.factor[3]=0x40;
	sensor_ptr->contrast.factor[4]=0x50;
	sensor_ptr->contrast.factor[5]=0x60;
	sensor_ptr->contrast.factor[6]=0x70;
	sensor_ptr->contrast.factor[7]=0x40;
	sensor_ptr->contrast.factor[8]=0x40;
	sensor_ptr->contrast.factor[9]=0x40;
	sensor_ptr->contrast.factor[10]=0x40;
	sensor_ptr->contrast.factor[11]=0x40;
	sensor_ptr->contrast.factor[12]=0x40;
	sensor_ptr->contrast.factor[13]=0x40;
	sensor_ptr->contrast.factor[14]=0x40;
	sensor_ptr->contrast.factor[15]=0x40;
	//hist
	sensor_ptr->hist.mode;
	sensor_ptr->hist.low_ratio;
	sensor_ptr->hist.high_ratio;
	//auto contrast
	sensor_ptr->auto_contrast.mode;
	//saturation
	sensor_ptr->saturation.factor[0]=0x40;
	sensor_ptr->saturation.factor[1]=0x40;
	sensor_ptr->saturation.factor[2]=0x40;
	sensor_ptr->saturation.factor[3]=0x40;
	sensor_ptr->saturation.factor[4]=0x40;
	sensor_ptr->saturation.factor[5]=0x40;
	sensor_ptr->saturation.factor[6]=0x40;
	sensor_ptr->saturation.factor[7]=0x40;
	sensor_ptr->saturation.factor[8]=0x40;
	sensor_ptr->saturation.factor[9]=0x40;
	sensor_ptr->saturation.factor[10]=0x40;
	sensor_ptr->saturation.factor[11]=0x40;
	sensor_ptr->saturation.factor[12]=0x40;
	sensor_ptr->saturation.factor[13]=0x40;
	sensor_ptr->saturation.factor[14]=0x40;
	sensor_ptr->saturation.factor[15]=0x40;

	//af info
	sensor_ptr->af.max_step=1024;
	sensor_ptr->af.stab_period=10;

	//edge
	sensor_ptr->edge.info[0].detail_thr=0x03;
	sensor_ptr->edge.info[0].smooth_thr=0x05;
	sensor_ptr->edge.info[0].strength=24;
	//emboss
	sensor_ptr->emboss.step=0x00;
	//global gain
	sensor_ptr->global.gain=0x40;
	//chn gain
	sensor_ptr->chn.r_gain=0x40;
	sensor_ptr->chn.g_gain=0x40;
	sensor_ptr->chn.b_gain=0x40;
	sensor_ptr->chn.r_offset=0x00;
	sensor_ptr->chn.r_offset=0x00;
	sensor_ptr->chn.r_offset=0x00;

	return rtn;
}


LOCAL uint32_t _s5k4e1ga_GetResolutionTrimTab(uint32_t param)
{
	SENSOR_PRINT("0x%x", (uint32_t)s_s5k4e1ga_Resolution_Trim_Tab);
	return (uint32_t) s_s5k4e1ga_Resolution_Trim_Tab;
}

LOCAL uint32_t _s5k4e1ga_PowerOn(uint32_t power_on)
{
	SENSOR_AVDD_VAL_E dvdd_val = g_s5k4e1ga_mipi_raw_info.dvdd_val;
	SENSOR_AVDD_VAL_E avdd_val = g_s5k4e1ga_mipi_raw_info.avdd_val;
	SENSOR_AVDD_VAL_E iovdd_val = g_s5k4e1ga_mipi_raw_info.iovdd_val;
	BOOLEAN power_down = g_s5k4e1ga_mipi_raw_info.power_down_level;
	BOOLEAN reset_level = g_s5k4e1ga_mipi_raw_info.reset_pulse_level;
	//uint32_t reset_width=g_s5k4e1ga_yuv_info.reset_pulse_width;

	if (SENSOR_TRUE == power_on) {
		Sensor_PowerDown(power_down);
		// Open power
		Sensor_SetMonitorVoltage(SENSOR_AVDD_2800MV);
		Sensor_SetVoltage(dvdd_val, avdd_val, iovdd_val);
		usleep(20*1000);
		Sensor_SetMCLK(SENSOR_DEFALUT_MCLK);
		usleep(10*1000);
		Sensor_PowerDown(!power_down);
		// Reset sensor
		Sensor_Reset(reset_level);
	} else {
		Sensor_PowerDown(power_down);
		Sensor_SetMCLK(SENSOR_DISABLE_MCLK);
		Sensor_SetVoltage(SENSOR_AVDD_CLOSED, SENSOR_AVDD_CLOSED, SENSOR_AVDD_CLOSED);
		Sensor_SetMonitorVoltage(SENSOR_AVDD_CLOSED);
	}
	SENSOR_PRINT("SENSOR_s5k4e1ga: _s5k4e1ga_Power_On(1:on, 0:off): %d", power_on);
	return SENSOR_SUCCESS;
}

LOCAL uint32_t _s5k4e1ga_Identify(uint32_t param)
{
#define s5k4e1ga_PID_VALUE    0x4e
#define s5k4e1ga_PID_ADDR     0x0000
#define s5k4e1ga_VER_VALUE    0x10
#define s5k4e1ga_VER_ADDR     0x0001

	uint8_t pid_value = 0x00;
	uint8_t ver_value = 0x00;
	uint32_t ret_value = SENSOR_FAIL;

	SENSOR_PRINT("SENSOR_s5k4e1ga: mipi raw identify\n");

	pid_value = Sensor_ReadReg(s5k4e1ga_PID_ADDR);

	if (s5k4e1ga_PID_VALUE == pid_value) {
		ver_value = Sensor_ReadReg(s5k4e1ga_VER_ADDR);
		SENSOR_PRINT("SENSOR_s5k4e1ga: Identify: PID = %x, VER = %x", pid_value, ver_value);
		if (s5k4e1ga_VER_VALUE == ver_value) {
			Sensor_InitRawTuneInfo();
			ret_value = SENSOR_SUCCESS;
			SENSOR_PRINT("SENSOR_s5k4e1ga: this is s5k4e1ga sensor !");
		} else {
			SENSOR_PRINT
			    ("SENSOR_s5k4e1ga: identify fail,ver_value=%d", ver_value);
		}
	} else {
		SENSOR_PRINT("SENSOR_s5k4e1ga: identify fail,pid_value=%d", pid_value);
	}

	return ret_value;
}

LOCAL uint32_t _s5k4e1ga_write_exposure(uint32_t param)
{
	uint32_t ret_value = SENSOR_SUCCESS;
	uint16_t expsure_line = 0x00;
	uint16_t frame_len_cur = 0x00;
	uint16_t frame_len = 0x00;
	uint16_t value=0x00;
	uint16_t value0=0x00;
	uint16_t value1=0x00;
	uint16_t value2=0x00;

	expsure_line=param&0xffff;
	if(expsure_line < 3){
		expsure_line = 3;
	}

	frame_len = expsure_line + 8;
	frame_len = (frame_len > 0x3e0) ? frame_len : 0x3e0;

	value = Sensor_ReadReg(0x0341);
	frame_len_cur = value&0xff;
	value = Sensor_ReadReg(0x0340);
	frame_len_cur |= (value<<0x08)&0xff00;

	SENSOR_PRINT("SENSOR_s5k4e1ga: write_exposure line:%d, frame_len_cur:%d, frame_len:%d", expsure_line, frame_len_cur, frame_len);

	ret_value = Sensor_WriteReg(0x104, 0x01);

	if(frame_len_cur != frame_len){
		value = frame_len & 0xff;
		ret_value = Sensor_WriteReg(0x0341, value);
		value = (frame_len >> 0x08) & 0xff;
		ret_value = Sensor_WriteReg(0x0340, value);
	}
	value = expsure_line & 0xff;
	ret_value = Sensor_WriteReg(0x203, value);
	value = (expsure_line >> 0x08) & 0xff;
	ret_value = Sensor_WriteReg(0x202, value);
	ret_value = Sensor_WriteReg(0x104, 0x00);

	return ret_value;
}

LOCAL uint32_t _s5k4e1ga_write_gain(uint32_t param)
{
	uint32_t ret_value = SENSOR_SUCCESS;
	uint16_t value=0x00;
	uint32_t real_gain = 0;

	real_gain = ((param&0xf)+16)*(((param>>4)&0x01)+1)*(((param>>5)&0x01)+1);
	real_gain = real_gain*(((param>>6)&0x01)+1)*(((param>>7)&0x01)+1)*(((param>>8)&0x01)+1);

	real_gain = real_gain<<1;

	SENSOR_PRINT("SENSOR_s5k4e1ga: real_gain:0x%x, param: 0x%x", real_gain, param);

	ret_value = Sensor_WriteReg(0x104, 0x01);
	value = real_gain>>0x08;
	ret_value = Sensor_WriteReg(0x204, value);
	value = real_gain&0xff;
	ret_value = Sensor_WriteReg(0x205, value);
	ret_value = Sensor_WriteReg(0x104, 0x00);

	return ret_value;
}

LOCAL uint32_t _s5k4e1ga_write_af(uint32_t param)
{
#define DW8714_VCM_SLAVE_ADDR (0x18>>1)

		uint32_t ret_value = SENSOR_SUCCESS;
		uint8_t cmd_val[2] = {0x00};
		uint16_t  slave_addr = 0;
		uint16_t cmd_len = 0;

		SENSOR_PRINT("SENSOR_s5k4e1ga: _write_af %d", param);

		//for direct mode
		slave_addr = DW8714_VCM_SLAVE_ADDR;
		cmd_val[0] = (param&0xfff0)>>4;
		cmd_val[1] = (param&0x0f)<<4;
		cmd_len = 2;
		ret_value = Sensor_WriteI2C(slave_addr,(uint8_t*)&cmd_val[0], cmd_len);
		SENSOR_PRINT("SENSOR_s5k4e1ga: _write_af, ret =  %d, MSL:%x, LSL:%x\n", ret_value, cmd_val[0], cmd_val[1]);

		return ret_value;

}

LOCAL uint32_t _s5k4e1ga_ReadGain(uint32_t param)
{
	uint32_t rtn = SENSOR_SUCCESS;
	uint16_t value=0x00;
	uint32_t gain = 0;

	value = Sensor_ReadReg(0x205);/*0-7*/
	gain = value&0xff;
	value = Sensor_ReadReg(0x204);/*8*/
	gain |= (value<<0x08)&0xff00;

	s_s5k4e1ga_gain=gain;

	SENSOR_PRINT("SENSOR: _s5k4e1ga_ReadGain gain: 0x%x", s_s5k4e1ga_gain);

	return rtn;
}

LOCAL uint32_t _s5k4e1ga_SetEV(uint32_t param)
{
	uint32_t rtn = SENSOR_SUCCESS;
	SENSOR_EXT_FUN_T_PTR ext_ptr = (SENSOR_EXT_FUN_T_PTR) param;
	uint16_t value=0x00;
	uint32_t gain = s_s5k4e1ga_gain;
	uint32_t ev = ext_ptr->param;

	SENSOR_PRINT("SENSOR: _s5k4e1ga_SetEV param: 0x%x", ev);

	return rtn;
}

LOCAL uint32_t _s5k4e1ga_ExtFunc(uint32_t ctl_param)
{
	uint32_t rtn = SENSOR_SUCCESS;
	SENSOR_EXT_FUN_PARAM_T_PTR ext_ptr = (SENSOR_EXT_FUN_PARAM_T_PTR) ctl_param;

	switch (ext_ptr->cmd) {
		//case SENSOR_EXT_EV:
		case 10:
			rtn = _s5k4e1ga_SetEV(ctl_param);
			break;
		default:
			break;
	}

	return rtn;
}

LOCAL uint32_t _s5k4e1ga_BeforeSnapshot(uint32_t param)
{
	uint8_t ret_l, ret_m, ret_h;
	uint32_t capture_exposure, preview_maxline;
	uint32_t capture_maxline, preview_exposure;
	uint32_t prv_linetime=s_s5k4e1ga_Resolution_Trim_Tab[SENSOR_MODE_PREVIEW_ONE].line_time;
	uint32_t cap_linetime;
	uint32_t frame_len = 0x00;

	param = param & 0xffff;
	cap_linetime = s_s5k4e1ga_Resolution_Trim_Tab[param].line_time;
	SENSOR_PRINT("SENSOR_s5k4e1ga: BeforeSnapshot moe: %d",param);

	if (SENSOR_MODE_PREVIEW_ONE >= param){
		SENSOR_PRINT("SENSOR_s5k4e1ga: prvmode equal to capmode");
		return SENSOR_SUCCESS;
	}


	ret_h = (uint8_t) Sensor_ReadReg(0x202);
	ret_l = (uint8_t) Sensor_ReadReg(0x203);
	preview_exposure = (ret_h << 8) + (ret_l);

	Sensor_SetMode(param);

	capture_exposure = preview_exposure;

	frame_len = Sensor_ReadReg(0x0341)&0xff;
	frame_len |= (Sensor_ReadReg(0x0340)<<0x08)&0xff00;

	Sensor_WriteReg(0x104, 0x01);
	if(capture_exposure >= (frame_len - 8)){
		frame_len = capture_exposure+8;
		Sensor_WriteReg(0x0341, frame_len & 0xff);
		Sensor_WriteReg(0x0340, (frame_len >> 0x08) & 0xff);
	}
	Sensor_WriteReg(0x203, capture_exposure & 0xff);
	Sensor_WriteReg(0x202, (capture_exposure >> 0x08) & 0xff);
	Sensor_WriteReg(0x104, 0x00);

	Sensor_SetSensorExifInfo(SENSOR_EXIF_CTRL_EXPOSURETIME, capture_exposure);

	return SENSOR_SUCCESS;
}

LOCAL uint32_t _s5k4e1ga_after_snapshot(uint32_t param)
{
	SENSOR_PRINT("SENSOR_s5k4e1ga: after_snapshot mode:%d", param);
	Sensor_SetMode(param);
	return SENSOR_SUCCESS;
}

LOCAL uint32_t _s5k4e1ga_StreamOn(uint32_t param)
{
	SENSOR_PRINT("SENSOR_s5k4e1ga: StreamOn");

	Sensor_WriteReg(0x0100, 0x01);

	return 0;
}

LOCAL uint32_t _s5k4e1ga_StreamOff(uint32_t param)
{
	SENSOR_PRINT("SENSOR_s5k4e1ga: StreamOff");

	Sensor_WriteReg(0x0100, 0x00);
	usleep(40*1000);

	return 0;
}

