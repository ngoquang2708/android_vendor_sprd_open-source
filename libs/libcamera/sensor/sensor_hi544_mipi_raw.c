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
#include "sensor_hi544_raw_param.c"
#include "sensor_hi544_otp.c"

#define hi544_I2C_ADDR_W        0x20
#define hi544_I2C_ADDR_R         0x20

#define DW9714_VCM_SLAVE_ADDR 	(0x18>>1)  //0x0c
#define DW9806_VCM_SLAVE_ADDR 	(0x18>>1)  //0x0c

#define hi544_MIN_FRAME_LEN_PRV  0x5e8
#define hi544_4_LANES
//#define AF_DRIVER_DW9806 1
static int s_hi544_gain = 0;
static int s_capture_shutter = 0;
static int s_capture_VTS = 0;
static int s_video_min_framerate = 0;
static int s_video_max_framerate = 0;

LOCAL uint32_t _hi544_GetResolutionTrimTab(uint32_t param);
LOCAL uint32_t _hi544_PowerOn(uint32_t power_on);
LOCAL uint32_t _hi544_Identify(uint32_t param);
LOCAL uint32_t _hi544_BeforeSnapshot(uint32_t param);
LOCAL uint32_t _hi544_after_snapshot(uint32_t param);
LOCAL uint32_t _hi544_StreamOn(uint32_t param);
LOCAL uint32_t _hi544_StreamOff(uint32_t param);
LOCAL uint32_t _hi544_write_exposure(uint32_t param);
LOCAL uint32_t _hi544_write_gain(uint32_t param);
LOCAL uint32_t _hi544_write_af(uint32_t param);
LOCAL uint32_t _hi544_flash(uint32_t param);
LOCAL uint32_t _hi544_ExtFunc(uint32_t ctl_param);
LOCAL int _hi544_get_VTS(void);
LOCAL int _hi544_set_VTS(int VTS);
LOCAL uint32_t _hi544_ReadGain(uint32_t param);
LOCAL uint32_t _hi544_set_video_mode(uint32_t param);
LOCAL int _hi544_get_shutter(void);
LOCAL uint32_t _hi544_cfg_otp(uint32_t  param);
LOCAL uint32_t _hi544_com_Identify_otp(void* param_ptr);
LOCAL uint32_t _dw9174_SRCInit(uint32_t mode);
LOCAL uint32_t _dw9806_SRCInit(uint32_t mode);


LOCAL const struct raw_param_info_tab s_hi544_raw_param_tab[]={
	{hi544_RAW_PARAM_COM, &s_hi544_mipi_raw_info, _hi544_com_Identify_otp, PNULL},
	{RAW_INFO_END_ID, PNULL, PNULL, PNULL}
};

struct sensor_raw_info* s_hi544_mipi_raw_info_ptr=NULL;

static uint32_t g_hi544_module_id = 0;

static uint32_t g_flash_mode_en = 0;
static uint32_t g_af_slewrate = 1;

LOCAL const SENSOR_REG_T hi544_common_init[] = {
	//	Sensor        	 : Hi-544
	//	Initial Ver.  	 : init v0.24
	//	Initial Date	 : 2014-03-11
	//	Image size       : 2592x1944
	//	mclk/pclk        : 24mhz / 88Mhz
	//	MIPI speed(Mbps) : 880Mbps (each lane)
	//	MIPI		 : Non-continuous
	//	Frame Length     : 1984
	//	Line Length	 : 2880
	//	Max Fps          : 30fps (= Exp.time : 33ms )
	//	Pixel order      : Blue 1st (=BGGR)
	//	X/Y-flip         : No-X/Y flip
	//	I2C Address      : 0x40(Write), 0x41(Read)
	//	AG               : x1
	//	DG               : x1

	//////////////////////////////////////////////////////////////////////////
	{0x0A00, 0x0000}, //sleep On
	{0x0E00, 0x0101},
	{0x0E02, 0x0101},
	{0x0E04, 0x0101},
	{0x0E06, 0x0101},
	{0x0E08, 0x0101},
	{0x0E0A, 0x0101},
	{0x0E0C, 0x0101},
	{0x0E0E, 0x0101},
	{0x2000, 0x4031},
	{0x2002, 0x83F8},
	{0x2004, 0x4104},
	{0x2006, 0x4306},
	{0x2008, 0x43A2},
	{0x200a, 0x0B80},
	{0x200c, 0x0C0A},
	{0x200e, 0x40B2},
	{0x2010, 0x0014},
	{0x2012, 0x0B82},
	{0x2014, 0x0C0A},
	{0x2016, 0x4382},
	{0x2018, 0x0B90},
	{0x201a, 0x0C0A},
	{0x201c, 0x40B2},
	{0x201e, 0x1044},
	{0x2020, 0x0B9A},
	{0x2022, 0x0C0A},
	{0x2024, 0x4382},
	{0x2026, 0x0B9C},
	{0x2028, 0x0C0A},
	{0x202a, 0x93D2},
	{0x202c, 0x003D},
	{0x202e, 0x2002},
	{0x2030, 0x4030},
	{0x2032, 0xF60A},
	{0x2034, 0x0900},
	{0x2036, 0x7312},
	{0x2038, 0x43D2},
	{0x203a, 0x003D},
	{0x203c, 0x40B2},
	{0x203e, 0x07CB},
	{0x2040, 0x0B84},
	{0x2042, 0x0C0A},
	{0x2044, 0x40B2},
	{0x2046, 0x5ED7},
	{0x2048, 0x0B86},
	{0x204a, 0x0C0A},
	{0x204c, 0x40B2},
	{0x204e, 0x808B},
	{0x2050, 0x0B88},
	{0x2052, 0x0C0A},
	{0x2054, 0x40B2},
	{0x2056, 0x1009},
	{0x2058, 0x0B8A},
	{0x205a, 0x0C0A},
	{0x205c, 0x40B2},
	{0x205e, 0xC40C},
	{0x2060, 0x0B8C},
	{0x2062, 0x0C0A},
	{0x2064, 0x40B2},
	{0x2066, 0xC9E1},
	{0x2068, 0x0B8E},
	{0x206a, 0x0C0A},
	{0x206c, 0x4382},
	{0x206e, 0x0B98},
	{0x2070, 0x0C0A},
	{0x2072, 0x93C2},
	{0x2074, 0x008C},
	{0x2076, 0x2002},
	{0x2078, 0x4030},
	{0x207a, 0xF5FE},
	{0x207c, 0x40B2},
	{0x207e, 0x141E},
	{0x2080, 0x0B92},
	{0x2082, 0x0C0A},
	{0x2084, 0x43D2},
	{0x2086, 0x0F82},
	{0x2088, 0x0C3C},
	{0x208a, 0x0C3C},
	{0x208c, 0x0C3C},
	{0x208e, 0x0C3C},
	{0x2090, 0x421F},
	{0x2092, 0x00A6},
	{0x2094, 0x503F},
	{0x2096, 0x07D0},
	{0x2098, 0x3811},
	{0x209a, 0x4F82},
	{0x209c, 0x7100},
	{0x209e, 0x0004},
	{0x20a0, 0x0C0D},
	{0x20a2, 0x0005},
	{0x20a4, 0x0C04},
	{0x20a6, 0x000D},
	{0x20a8, 0x0C09},
	{0x20aa, 0x003D},
	{0x20ac, 0x0C1D},
	{0x20ae, 0x003C},
	{0x20b0, 0x0C13},
	{0x20b2, 0x0004},
	{0x20b4, 0x0C09},
	{0x20b6, 0x0004},
	{0x20b8, 0x533F},
	{0x20ba, 0x37EF},
	{0x20bc, 0x4392},
	{0x20be, 0x8092},
	{0x20c0, 0x4382},
	{0x20c2, 0x8094},
	{0x20c4, 0x4382},
	{0x20c6, 0x80AC},
	{0x20c8, 0x4382},
	{0x20ca, 0x80B0},
	{0x20cc, 0x4382},
	{0x20ce, 0x8098},
	{0x20d0, 0x40B2},
	{0x20d2, 0x0028},
	{0x20d4, 0x7000},
	{0x20d6, 0x43A2},
	{0x20d8, 0x8096},
	{0x20da, 0xB3E2},
	{0x20dc, 0x00B4},
	{0x20de, 0x2402},
	{0x20e0, 0x4392},
	{0x20e2, 0x8096},
	{0x20e4, 0x4325},
	{0x20e6, 0xB3D2},
	{0x20e8, 0x00B4},
	{0x20ea, 0x2002},
	{0x20ec, 0x4030},
	{0x20ee, 0xF5EE},
	{0x20f0, 0x4305},
	{0x20f2, 0x43D2},
	{0x20f4, 0x019A},
	{0x20f6, 0x40F2},
	{0x20f8, 0x0009},
	{0x20fa, 0x019B},
	{0x20fc, 0x43D2},
	{0x20fe, 0x0180},
	{0x2100, 0x0260},
	{0x2102, 0x0000},
	{0x2104, 0x0C22},
	{0x2106, 0x0240},
	{0x2108, 0x0000},
	{0x210a, 0x0260},
	{0x210c, 0x0000},
	{0x210e, 0x0C0F},
	{0x2110, 0x4382},
	{0x2112, 0x7602},
	{0x2114, 0x4308},
	{0x2116, 0x5038},
	{0x2118, 0x0030},
	{0x211a, 0x480F},
	{0x211c, 0x12B0},
	{0x211e, 0xFB80},
	{0x2120, 0x403B},
	{0x2122, 0x7606},
	{0x2124, 0x4B29},
	{0x2126, 0x5318},
	{0x2128, 0x480F},
	{0x212a, 0x12B0},
	{0x212c, 0xFB80},
	{0x212e, 0x4B2A},
	{0x2130, 0x5318},
	{0x2132, 0x480F},
	{0x2134, 0x12B0},
	{0x2136, 0xFB80},
	{0x2138, 0x4A0D},
	{0x213a, 0xF03D},
	{0x213c, 0x000F},
	{0x213e, 0x108D},
	{0x2140, 0x4B2E},
	{0x2142, 0x5E0E},
	{0x2144, 0x5E0E},
	{0x2146, 0x5E0E},
	{0x2148, 0x5E0E},
	{0x214a, 0x4A0F},
	{0x214c, 0xC312},
	{0x214e, 0x100F},
	{0x2150, 0x110F},
	{0x2152, 0x110F},
	{0x2154, 0x110F},
	{0x2156, 0x590D},
	{0x2158, 0x4D86},
	{0x215a, 0x5000},
	{0x215c, 0x5F0E},
	{0x215e, 0x4E86},
	{0x2160, 0x6000},
	{0x2162, 0x5326},
	{0x2164, 0x5038},
	{0x2166, 0xFFD1},
	{0x2168, 0x9038},
	{0x216a, 0x0300},
	{0x216c, 0x2BD4},
	{0x216e, 0x0261},
	{0x2170, 0x0000},
	{0x2172, 0x43C2},
	{0x2174, 0x0180},
	{0x2176, 0x43A2},
	{0x2178, 0x0384},
	{0x217a, 0x42B2},
	{0x217c, 0x0386},
	{0x217e, 0x4384},
	{0x2180, 0x0002},
	{0x2182, 0x4384},
	{0x2184, 0x0006},
	{0x2186, 0x4392},
	{0x2188, 0x7326},
	{0x218a, 0x12B0},
	{0x218c, 0xF82E},
	{0x218e, 0x4392},
	{0x2190, 0x731C},
	{0x2192, 0x9382},
	{0x2194, 0x8092},
	{0x2196, 0x200E},
	{0x2198, 0x0B00},
	{0x219a, 0x7302},
	{0x219c, 0x01F4},
	{0x219e, 0x4382},
	{0x21a0, 0x7004},
	{0x21a2, 0xB3D2},
	{0x21a4, 0x0786},
	{0x21a6, 0x2402},
	{0x21a8, 0x4392},
	{0x21aa, 0x7002},
	{0x21ac, 0x0900},
	{0x21ae, 0x7308},
	{0x21b0, 0x12B0},
	{0x21b2, 0xF82E},
	{0x21b4, 0x4392},
	{0x21b6, 0x80AE},
	{0x21b8, 0x4382},
	{0x21ba, 0x740E},
	{0x21bc, 0xB3E2},
	{0x21be, 0x0080},
	{0x21c0, 0x2402},
	{0x21c2, 0x4392},
	{0x21c4, 0x740E},
	{0x21c6, 0x0900},
	{0x21c8, 0x7328},
	{0x21ca, 0x4392},
	{0x21cc, 0x7004},
	{0x21ce, 0x4582},
	{0x21d0, 0x7110},
	{0x21d2, 0x9382},
	{0x21d4, 0x8090},
	{0x21d6, 0x2005},
	{0x21d8, 0x9392},
	{0x21da, 0x7110},
	{0x21dc, 0x2402},
	{0x21de, 0x4030},
	{0x21e0, 0xF524},
	{0x21e2, 0x9392},
	{0x21e4, 0x7110},
	{0x21e6, 0x20E5},
	{0x21e8, 0x0B00},
	{0x21ea, 0x7302},
	{0x21ec, 0x0032},
	{0x21ee, 0x4382},
	{0x21f0, 0x7004},
	{0x21f2, 0x0800},
	{0x21f4, 0x7114},
	{0x21f6, 0x425F},
	{0x21f8, 0x0C9C},
	{0x21fa, 0x4F4E},
	{0x21fc, 0x430F},
	{0x21fe, 0x4E0D},
	{0x2200, 0x430C},
	{0x2202, 0x421F},
	{0x2204, 0x0C9A},
	{0x2206, 0xDF0C},
	{0x2208, 0x1204},
	{0x220a, 0x440F},
	{0x220c, 0x532F},
	{0x220e, 0x120F},
	{0x2210, 0x1212},
	{0x2212, 0x0CA2},
	{0x2214, 0x403E},
	{0x2216, 0x80B2},
	{0x2218, 0x403F},
	{0x221a, 0x8070},
	{0x221c, 0x12B0},
	{0x221e, 0xF696},
	{0x2220, 0x4F09},
	{0x2222, 0x425F},
	{0x2224, 0x0CA0},
	{0x2226, 0x4F4E},
	{0x2228, 0x430F},
	{0x222a, 0x4E0D},
	{0x222c, 0x430C},
	{0x222e, 0x421F},
	{0x2230, 0x0C9E},
	{0x2232, 0xDF0C},
	{0x2234, 0x440F},
	{0x2236, 0x522F},
	{0x2238, 0x120F},
	{0x223a, 0x532F},
	{0x223c, 0x120F},
	{0x223e, 0x1212},
	{0x2240, 0x0CA4},
	{0x2242, 0x403E},
	{0x2244, 0x809A},
	{0x2246, 0x403F},
	{0x2248, 0x8050},
	{0x224a, 0x12B0},
	{0x224c, 0xF696},
	{0x224e, 0x4F0B},
	{0x2250, 0x430D},
	{0x2252, 0x441E},
	{0x2254, 0x0004},
	{0x2256, 0x442F},
	{0x2258, 0x5031},
	{0x225a, 0x000C},
	{0x225c, 0x9E0F},
	{0x225e, 0x2C01},
	{0x2260, 0x431D},
	{0x2262, 0x8E0F},
	{0x2264, 0x930F},
	{0x2266, 0x3402},
	{0x2268, 0xE33F},
	{0x226a, 0x531F},
	{0x226c, 0x421E},
	{0x226e, 0x0CA2},
	{0x2270, 0xC312},
	{0x2272, 0x100E},
	{0x2274, 0x9E0F},
	{0x2276, 0x2804},
	{0x2278, 0x930D},
	{0x227a, 0x2001},
	{0x227c, 0x5319},
	{0x227e, 0x5D0B},
	{0x2280, 0x425F},
	{0x2282, 0x00BA},
	{0x2284, 0xC312},
	{0x2286, 0x104F},
	{0x2288, 0x114F},
	{0x228a, 0x114F},
	{0x228c, 0x114F},
	{0x228e, 0xF37F},
	{0x2290, 0x930F},
	{0x2292, 0x2039},
	{0x2294, 0x4037},
	{0x2296, 0x0030},
	{0x2298, 0x4782},
	{0x229a, 0x0196},
	{0x229c, 0x403F},
	{0x229e, 0x0040},
	{0x22a0, 0x870F},
	{0x22a2, 0x490A},
	{0x22a4, 0x4F0C},
	{0x22a6, 0x12B0},
	{0x22a8, 0xFB94},
	{0x22aa, 0x4E0D},
	{0x22ac, 0xC312},
	{0x22ae, 0x100D},
	{0x22b0, 0x110D},
	{0x22b2, 0x110D},
	{0x22b4, 0x110D},
	{0x22b6, 0x110D},
	{0x22b8, 0x110D},
	{0x22ba, 0x4B0A},
	{0x22bc, 0x4F0C},
	{0x22be, 0x12B0},
	{0x22c0, 0xFB94},
	{0x22c2, 0x4E0F},
	{0x22c4, 0xC312},
	{0x22c6, 0x100F},
	{0x22c8, 0x110F},
	{0x22ca, 0x110F},
	{0x22cc, 0x110F},
	{0x22ce, 0x110F},
	{0x22d0, 0x110F},
	{0x22d2, 0x5F0D},
	{0x22d4, 0xC312},
	{0x22d6, 0x100D},
	{0x22d8, 0x90B2},
	{0x22da, 0x0007},
	{0x22dc, 0x80B0},
	{0x22de, 0x240C},
	{0x22e0, 0x90B2},
	{0x22e2, 0x012C},
	{0x22e4, 0x80AC},
	{0x22e6, 0x2408},
	{0x22e8, 0x0900},
	{0x22ea, 0x710E},
	{0x22ec, 0x0B00},
	{0x22ee, 0x7302},
	{0x22f0, 0x0320},
	{0x22f2, 0x12B0},
	{0x22f4, 0xF658},
	{0x22f6, 0x3F6D},
	{0x22f8, 0x8D09},
	{0x22fa, 0x4982},
	{0x22fc, 0x0CAC},
	{0x22fe, 0x8D0B},
	{0x2300, 0x4B82},
	{0x2302, 0x0CAE},
	{0x2304, 0x3FF1},
	{0x2306, 0x931F},
	{0x2308, 0x2451},
	{0x230a, 0x932F},
	{0x230c, 0x244C},
	{0x230e, 0x903F},
	{0x2310, 0x0003},
	{0x2312, 0x2446},
	{0x2314, 0x922F},
	{0x2316, 0x2441},
	{0x2318, 0x903F},
	{0x231a, 0x0005},
	{0x231c, 0x243B},
	{0x231e, 0x903F},
	{0x2320, 0x0006},
	{0x2322, 0x2435},
	{0x2324, 0x903F},
	{0x2326, 0x0007},
	{0x2328, 0x242F},
	{0x232a, 0x923F},
	{0x232c, 0x242A},
	{0x232e, 0x903F},
	{0x2330, 0x0009},
	{0x2332, 0x2424},
	{0x2334, 0x903F},
	{0x2336, 0x000A},
	{0x2338, 0x241E},
	{0x233a, 0x903F},
	{0x233c, 0x000B},
	{0x233e, 0x2418},
	{0x2340, 0x903F},
	{0x2342, 0x000C},
	{0x2344, 0x2412},
	{0x2346, 0x903F},
	{0x2348, 0x000D},
	{0x234a, 0x240C},
	{0x234c, 0x903F},
	{0x234e, 0x000E},
	{0x2350, 0x2406},
	{0x2352, 0x903F},
	{0x2354, 0x000F},
	{0x2356, 0x23A0},
	{0x2358, 0x4037},
	{0x235a, 0x0012},
	{0x235c, 0x3F9D},
	{0x235e, 0x4037},
	{0x2360, 0x0014},
	{0x2362, 0x3F9A},
	{0x2364, 0x4037},
	{0x2366, 0x0016},
	{0x2368, 0x3F97},
	{0x236a, 0x4037},
	{0x236c, 0x0018},
	{0x236e, 0x3F94},
	{0x2370, 0x4037},
	{0x2372, 0x001A},
	{0x2374, 0x3F91},
	{0x2376, 0x4037},
	{0x2378, 0x001C},
	{0x237a, 0x3F8E},
	{0x237c, 0x4037},
	{0x237e, 0x001E},
	{0x2380, 0x3F8B},
	{0x2382, 0x4037},
	{0x2384, 0x0020},
	{0x2386, 0x3F88},
	{0x2388, 0x4037},
	{0x238a, 0x0022},
	{0x238c, 0x3F85},
	{0x238e, 0x4037},
	{0x2390, 0x0024},
	{0x2392, 0x3F82},
	{0x2394, 0x4037},
	{0x2396, 0x0026},
	{0x2398, 0x3F7F},
	{0x239a, 0x4037},
	{0x239c, 0x0028},
	{0x239e, 0x3F7C},
	{0x23a0, 0x4037},
	{0x23a2, 0x002A},
	{0x23a4, 0x3F79},
	{0x23a6, 0x4037},
	{0x23a8, 0x002C},
	{0x23aa, 0x3F76},
	{0x23ac, 0x4037},
	{0x23ae, 0x002E},
	{0x23b0, 0x3F73},
	{0x23b2, 0x0B00},
	{0x23b4, 0x7302},
	{0x23b6, 0x0002},
	{0x23b8, 0x069A},
	{0x23ba, 0x0C1F},
	{0x23bc, 0x0403},
	{0x23be, 0x0C05},
	{0x23c0, 0x0001},
	{0x23c2, 0x0C01},
	{0x23c4, 0x0003},
	{0x23c6, 0x0C03},
	{0x23c8, 0x000B},
	{0x23ca, 0x0C33},
	{0x23cc, 0x0003},
	{0x23ce, 0x0C03},
	{0x23d0, 0x0653},
	{0x23d2, 0x0C03},
	{0x23d4, 0x065B},
	{0x23d6, 0x0C13},
	{0x23d8, 0x065F},
	{0x23da, 0x0C43},
	{0x23dc, 0x0657},
	{0x23de, 0x0C03},
	{0x23e0, 0x0653},
	{0x23e2, 0x0C03},
	{0x23e4, 0x0643},
	{0x23e6, 0x0C0F},
	{0x23e8, 0x077D},
	{0x23ea, 0x0C01},
	{0x23ec, 0x067F},
	{0x23ee, 0x0C01},
	{0x23f0, 0x0677},
	{0x23f2, 0x0C01},
	{0x23f4, 0x0673},
	{0x23f6, 0x0C2F},
	{0x23f8, 0x0663},
	{0x23fa, 0x0C37},
	{0x23fc, 0x0667},
	{0x23fe, 0x0C01},
	{0x2400, 0x0677},
	{0x2402, 0x0C01},
	{0x2404, 0x067D},
	{0x2406, 0x0C37},
	{0x2408, 0x0113},
	{0x240a, 0x0C27},
	{0x240c, 0x0003},
	{0x240e, 0x0C27},
	{0x2410, 0x0675},
	{0x2412, 0x0C01},
	{0x2414, 0x0671},
	{0x2416, 0x0CBB},
	{0x2418, 0x0661},
	{0x241a, 0x4392},
	{0x241c, 0x7004},
	{0x241e, 0x430F},
	{0x2420, 0x9382},
	{0x2422, 0x80AE},
	{0x2424, 0x2001},
	{0x2426, 0x431F},
	{0x2428, 0x4F82},
	{0x242a, 0x80AE},
	{0x242c, 0x930F},
	{0x242e, 0x246D},
	{0x2430, 0x0B00},
	{0x2432, 0x7302},
	{0x2434, 0x0356},
	{0x2436, 0x0665},
	{0x2438, 0x0C01},
	{0x243a, 0x0675},
	{0x243c, 0x033D},
	{0x243e, 0xAE0C},
	{0x2440, 0x0C01},
	{0x2442, 0x003C},
	{0x2444, 0x0C01},
	{0x2446, 0x0004},
	{0x2448, 0x0C01},
	{0x244a, 0x0642},
	{0x244c, 0x0B00},
	{0x244e, 0x7302},
	{0x2450, 0x0386},
	{0x2452, 0x0643},
	{0x2454, 0x0C05},
	{0x2456, 0x0001},
	{0x2458, 0x0C01},
	{0x245a, 0x0003},
	{0x245c, 0x0C03},
	{0x245e, 0x000B},
	{0x2460, 0x0C33},
	{0x2462, 0x0003},
	{0x2464, 0x0C03},
	{0x2466, 0x0653},
	{0x2468, 0x0C03},
	{0x246a, 0x065B},
	{0x246c, 0x0C13},
	{0x246e, 0x065F},
	{0x2470, 0x0C43},
	{0x2472, 0x0657},
	{0x2474, 0x0C03},
	{0x2476, 0x0653},
	{0x2478, 0x0C03},
	{0x247a, 0x0643},
	{0x247c, 0x0C0F},
	{0x247e, 0x077D},
	{0x2480, 0x0C01},
	{0x2482, 0x067F},
	{0x2484, 0x0C01},
	{0x2486, 0x0677},
	{0x2488, 0x0C01},
	{0x248a, 0x0673},
	{0x248c, 0x0C2F},
	{0x248e, 0x0663},
	{0x2490, 0x0C37},
	{0x2492, 0x0667},
	{0x2494, 0x0C01},
	{0x2496, 0x0677},
	{0x2498, 0x0C01},
	{0x249a, 0x067D},
	{0x249c, 0x0C37},
	{0x249e, 0x0113},
	{0x24a0, 0x0C27},
	{0x24a2, 0x0003},
	{0x24a4, 0x0C27},
	{0x24a6, 0x0675},
	{0x24a8, 0x0C01},
	{0x24aa, 0x0671},
	{0x24ac, 0x0CBB},
	{0x24ae, 0x0661},
	{0x24b0, 0x12B0},
	{0x24b2, 0xF658},
	{0x24b4, 0x9382},
	{0x24b6, 0x80AE},
	{0x24b8, 0x2419},
	{0x24ba, 0x0B00},
	{0x24bc, 0x7302},
	{0x24be, 0x06BA},
	{0x24c0, 0x0665},
	{0x24c2, 0x0C01},
	{0x24c4, 0x0675},
	{0x24c6, 0x0C01},
	{0x24c8, 0x0504},
	{0x24ca, 0x0C01},
	{0x24cc, 0x003C},
	{0x24ce, 0x0C01},
	{0x24d0, 0x0405},
	{0x24d2, 0x0C01},
	{0x24d4, 0x0642},
	{0x24d6, 0x930F},
	{0x24d8, 0x2002},
	{0x24da, 0x4030},
	{0x24dc, 0xF1D2},
	{0x24de, 0x4292},
	{0x24e0, 0x8092},
	{0x24e2, 0x8094},
	{0x24e4, 0x4382},
	{0x24e6, 0x8092},
	{0x24e8, 0x4030},
	{0x24ea, 0xF192},
	{0x24ec, 0x0B00},
	{0x24ee, 0x7302},
	{0x24f0, 0x06BA},
	{0x24f2, 0x0665},
	{0x24f4, 0x0C01},
	{0x24f6, 0x0407},
	{0x24f8, 0x0305},
	{0x24fa, 0xAE0C},
	{0x24fc, 0x0C01},
	{0x24fe, 0x0004},
	{0x2500, 0x0C01},
	{0x2502, 0x06A0},
	{0x2504, 0x0C01},
	{0x2506, 0x0642},
	{0x2508, 0x3FE6},
	{0x250a, 0x0B00},
	{0x250c, 0x7302},
	{0x250e, 0x0356},
	{0x2510, 0x0665},
	{0x2512, 0x0C01},
	{0x2514, 0x0675},
	{0x2516, 0x0305},
	{0x2518, 0xAE0C},
	{0x251a, 0x0C01},
	{0x251c, 0x0004},
	{0x251e, 0x0C03},
	{0x2520, 0x0642},
	{0x2522, 0x3F94},
	{0x2524, 0x0B00},
	{0x2526, 0x7302},
	{0x2528, 0x0002},
	{0x252a, 0x069A},
	{0x252c, 0x0C1F},
	{0x252e, 0x0402},
	{0x2530, 0x0C05},
	{0x2532, 0x0001},
	{0x2534, 0x0C01},
	{0x2536, 0x0003},
	{0x2538, 0x0C03},
	{0x253a, 0x000B},
	{0x253c, 0x0C33},
	{0x253e, 0x0003},
	{0x2540, 0x0C03},
	{0x2542, 0x0653},
	{0x2544, 0x0C03},
	{0x2546, 0x065B},
	{0x2548, 0x0C13},
	{0x254a, 0x065F},
	{0x254c, 0x0C43},
	{0x254e, 0x0657},
	{0x2550, 0x0C03},
	{0x2552, 0x0653},
	{0x2554, 0x0C03},
	{0x2556, 0x0643},
	{0x2558, 0x0C0F},
	{0x255a, 0x077D},
	{0x255c, 0x0C01},
	{0x255e, 0x067F},
	{0x2560, 0x0C01},
	{0x2562, 0x0677},
	{0x2564, 0x0C01},
	{0x2566, 0x0673},
	{0x2568, 0x0C5F},
	{0x256a, 0x0663},
	{0x256c, 0x0C6F},
	{0x256e, 0x0667},
	{0x2570, 0x0C01},
	{0x2572, 0x0677},
	{0x2574, 0x0C01},
	{0x2576, 0x077D},
	{0x2578, 0x0C33},
	{0x257a, 0x0013},
	{0x257c, 0x0C27},
	{0x257e, 0x0003},
	{0x2580, 0x0C4F},
	{0x2582, 0x0675},
	{0x2584, 0x0C01},
	{0x2586, 0x0671},
	{0x2588, 0x0CFF},
	{0x258a, 0x0C78},
	{0x258c, 0x0661},
	{0x258e, 0x4392},
	{0x2590, 0x7004},
	{0x2592, 0x430F},
	{0x2594, 0x9382},
	{0x2596, 0x80AE},
	{0x2598, 0x2001},
	{0x259a, 0x431F},
	{0x259c, 0x4F82},
	{0x259e, 0x80AE},
	{0x25a0, 0x12B0},
	{0x25a2, 0xF658},
	{0x25a4, 0x9382},
	{0x25a6, 0x80AE},
	{0x25a8, 0x2412},
	{0x25aa, 0x0B00},
	{0x25ac, 0x7302},
	{0x25ae, 0x0562},
	{0x25b0, 0x0665},
	{0x25b2, 0x0C02},
	{0x25b4, 0x0339},
	{0x25b6, 0xA60C},
	{0x25b8, 0x023C},
	{0x25ba, 0xAE0C},
	{0x25bc, 0x0C01},
	{0x25be, 0x0004},
	{0x25c0, 0x0C01},
	{0x25c2, 0x0642},
	{0x25c4, 0x0C13},
	{0x25c6, 0x06A1},
	{0x25c8, 0x0C03},
	{0x25ca, 0x06A0},
	{0x25cc, 0x3F84},
	{0x25ce, 0x0B00},
	{0x25d0, 0x7302},
	{0x25d2, 0x0562},
	{0x25d4, 0x0665},
	{0x25d6, 0x0C02},
	{0x25d8, 0x0301},
	{0x25da, 0xA60C},
	{0x25dc, 0x0204},
	{0x25de, 0xAE0C},
	{0x25e0, 0x0C03},
	{0x25e2, 0x0642},
	{0x25e4, 0x0C13},
	{0x25e6, 0x06A1},
	{0x25e8, 0x0C03},
	{0x25ea, 0x06A0},
	{0x25ec, 0x3F74},
	{0x25ee, 0xB3E2},
	{0x25f0, 0x00B4},
	{0x25f2, 0x2002},
	{0x25f4, 0x4030},
	{0x25f6, 0xF0F2},
	{0x25f8, 0x4315},
	{0x25fa, 0x4030},
	{0x25fc, 0xF0F2},
	{0x25fe, 0x40B2},
	{0x2600, 0x0C1E},
	{0x2602, 0x0B92},
	{0x2604, 0x0C0A},
	{0x2606, 0x4030},
	{0x2608, 0xF084},
	{0x260a, 0x40B2},
	{0x260c, 0x00C3},
	{0x260e, 0x0B84},
	{0x2610, 0x0C0A},
	{0x2612, 0x40B2},
	{0x2614, 0x5D17},
	{0x2616, 0x0B86},
	{0x2618, 0x0C0A},
	{0x261a, 0x4382},
	{0x261c, 0x0B88},
	{0x261e, 0x0C0A},
	{0x2620, 0x4382},
	{0x2622, 0x0B8A},
	{0x2624, 0x0C0A},
	{0x2626, 0x40B2},
	{0x2628, 0x000C},
	{0x262a, 0x0B8C},
	{0x262c, 0x0C0A},
	{0x262e, 0x40B2},
	{0x2630, 0xB5E1},
	{0x2632, 0x0B8E},
	{0x2634, 0x0C0A},
	{0x2636, 0x40B2},
	{0x2638, 0x641C},
	{0x263a, 0x0B92},
	{0x263c, 0x0C0A},
	{0x263e, 0x40B2},
	{0x2640, 0x8000},
	{0x2642, 0x0B98},
	{0x2644, 0x0C0A},
	{0x2646, 0x43C2},
	{0x2648, 0x0F82},
	{0x264a, 0x43C2},
	{0x264c, 0x003D},
	{0x264e, 0x4030},
	{0x2650, 0xF034},
	{0x2652, 0x5231},
	{0x2654, 0x4030},
	{0x2656, 0xFB90},
	{0x2658, 0xE3B2},
	{0x265a, 0x740E},
	{0x265c, 0x421F},
	{0x265e, 0x710E},
	{0x2660, 0x93A2},
	{0x2662, 0x7110},
	{0x2664, 0x2411},
	{0x2666, 0x9382},
	{0x2668, 0x710E},
	{0x266a, 0x240C},
	{0x266c, 0x5292},
	{0x266e, 0x8096},
	{0x2670, 0x7110},
	{0x2672, 0x4382},
	{0x2674, 0x740E},
	{0x2676, 0xB3E2},
	{0x2678, 0x0080},
	{0x267a, 0x2402},
	{0x267c, 0x4392},
	{0x267e, 0x740E},
	{0x2680, 0x4392},
	{0x2682, 0x80AE},
	{0x2684, 0x430F},
	{0x2686, 0x4130},
	{0x2688, 0xF31F},
	{0x268a, 0x27ED},
	{0x268c, 0x40B2},
	{0x268e, 0x0003},
	{0x2690, 0x7110},
	{0x2692, 0x431F},
	{0x2694, 0x4130},
	{0x2696, 0x120B},
	{0x2698, 0x120A},
	{0x269a, 0x1209},
	{0x269c, 0x1208},
	{0x269e, 0x1207},
	{0x26a0, 0x1206},
	{0x26a2, 0x1205},
	{0x26a4, 0x1204},
	{0x26a6, 0x8221},
	{0x26a8, 0x403B},
	{0x26aa, 0x0016},
	{0x26ac, 0x510B},
	{0x26ae, 0x4F08},
	{0x26b0, 0x4E09},
	{0x26b2, 0x4BA1},
	{0x26b4, 0x0000},
	{0x26b6, 0x4B1A},
	{0x26b8, 0x0002},
	{0x26ba, 0x4B91},
	{0x26bc, 0x0004},
	{0x26be, 0x0002},
	{0x26c0, 0x4304},
	{0x26c2, 0x4305},
	{0x26c4, 0x4306},
	{0x26c6, 0x4307},
	{0x26c8, 0x9382},
	{0x26ca, 0x80AA},
	{0x26cc, 0x2425},
	{0x26ce, 0x438A},
	{0x26d0, 0x0000},
	{0x26d2, 0x430B},
	{0x26d4, 0x4B0F},
	{0x26d6, 0x5F0F},
	{0x26d8, 0x5F0F},
	{0x26da, 0x580F},
	{0x26dc, 0x4C8F},
	{0x26de, 0x0000},
	{0x26e0, 0x4D8F},
	{0x26e2, 0x0002},
	{0x26e4, 0x4B0F},
	{0x26e6, 0x5F0F},
	{0x26e8, 0x590F},
	{0x26ea, 0x41AF},
	{0x26ec, 0x0000},
	{0x26ee, 0x531B},
	{0x26f0, 0x923B},
	{0x26f2, 0x2BF0},
	{0x26f4, 0x430B},
	{0x26f6, 0x4B0F},
	{0x26f8, 0x5F0F},
	{0x26fa, 0x5F0F},
	{0x26fc, 0x580F},
	{0x26fe, 0x5F34},
	{0x2700, 0x6F35},
	{0x2702, 0x4B0F},
	{0x2704, 0x5F0F},
	{0x2706, 0x590F},
	{0x2708, 0x4F2E},
	{0x270a, 0x430F},
	{0x270c, 0x5E06},
	{0x270e, 0x6F07},
	{0x2710, 0x531B},
	{0x2712, 0x923B},
	{0x2714, 0x2BF0},
	{0x2716, 0x3C18},
	{0x2718, 0x4A2E},
	{0x271a, 0x4E0F},
	{0x271c, 0x5F0F},
	{0x271e, 0x5F0F},
	{0x2720, 0x580F},
	{0x2722, 0x4C8F},
	{0x2724, 0x0000},
	{0x2726, 0x4D8F},
	{0x2728, 0x0002},
	{0x272a, 0x5E0E},
	{0x272c, 0x590E},
	{0x272e, 0x41AE},
	{0x2730, 0x0000},
	{0x2732, 0x4A2F},
	{0x2734, 0x903F},
	{0x2736, 0x0007},
	{0x2738, 0x2404},
	{0x273a, 0x531F},
	{0x273c, 0x4F8A},
	{0x273e, 0x0000},
	{0x2740, 0x3FD9},
	{0x2742, 0x438A},
	{0x2744, 0x0000},
	{0x2746, 0x3FD6},
	{0x2748, 0x440C},
	{0x274a, 0x450D},
	{0x274c, 0x460A},
	{0x274e, 0x470B},
	{0x2750, 0x12B0},
	{0x2752, 0xFBCA},
	{0x2754, 0x4C08},
	{0x2756, 0x4D09},
	{0x2758, 0x4C0E},
	{0x275a, 0x430F},
	{0x275c, 0x4E0A},
	{0x275e, 0x4F0B},
	{0x2760, 0x460C},
	{0x2762, 0x470D},
	{0x2764, 0x12B0},
	{0x2766, 0xFBAA},
	{0x2768, 0x8E04},
	{0x276a, 0x7F05},
	{0x276c, 0x440E},
	{0x276e, 0x450F},
	{0x2770, 0xC312},
	{0x2772, 0x100F},
	{0x2774, 0x100E},
	{0x2776, 0x110F},
	{0x2778, 0x100E},
	{0x277a, 0x110F},
	{0x277c, 0x100E},
	{0x277e, 0x411D},
	{0x2780, 0x0002},
	{0x2782, 0x4E8D},
	{0x2784, 0x0000},
	{0x2786, 0x480F},
	{0x2788, 0x5221},
	{0x278a, 0x4134},
	{0x278c, 0x4135},
	{0x278e, 0x4136},
	{0x2790, 0x4137},
	{0x2792, 0x4138},
	{0x2794, 0x4139},
	{0x2796, 0x413A},
	{0x2798, 0x413B},
	{0x279a, 0x4130},
	{0x279c, 0x120A},
	{0x279e, 0x4F0D},
	{0x27a0, 0x4E0C},
	{0x27a2, 0x425F},
	{0x27a4, 0x00BA},
	{0x27a6, 0x4F4A},
	{0x27a8, 0x503A},
	{0x27aa, 0x0010},
	{0x27ac, 0x931D},
	{0x27ae, 0x242B},
	{0x27b0, 0x932D},
	{0x27b2, 0x2421},
	{0x27b4, 0x903D},
	{0x27b6, 0x0003},
	{0x27b8, 0x2418},
	{0x27ba, 0x922D},
	{0x27bc, 0x2413},
	{0x27be, 0x903D},
	{0x27c0, 0x0005},
	{0x27c2, 0x2407},
	{0x27c4, 0x903D},
	{0x27c6, 0x0006},
	{0x27c8, 0x2028},
	{0x27ca, 0xC312},
	{0x27cc, 0x100A},
	{0x27ce, 0x110A},
	{0x27d0, 0x3C24},
	{0x27d2, 0x4A0E},
	{0x27d4, 0xC312},
	{0x27d6, 0x100E},
	{0x27d8, 0x110E},
	{0x27da, 0x4E0F},
	{0x27dc, 0x110F},
	{0x27de, 0x4E0A},
	{0x27e0, 0x5F0A},
	{0x27e2, 0x3C1B},
	{0x27e4, 0xC312},
	{0x27e6, 0x100A},
	{0x27e8, 0x3C18},
	{0x27ea, 0x4A0E},
	{0x27ec, 0xC312},
	{0x27ee, 0x100E},
	{0x27f0, 0x4E0F},
	{0x27f2, 0x110F},
	{0x27f4, 0x3FF3},
	{0x27f6, 0x4A0F},
	{0x27f8, 0xC312},
	{0x27fa, 0x100F},
	{0x27fc, 0x4F0E},
	{0x27fe, 0x110E},
	{0x2800, 0x4F0A},
	{0x2802, 0x5E0A},
	{0x2804, 0x3C0A},
	{0x2806, 0x4A0F},
	{0x2808, 0xC312},
	{0x280a, 0x100F},
	{0x280c, 0x4F0E},
	{0x280e, 0x110E},
	{0x2810, 0x4F0A},
	{0x2812, 0x5E0A},
	{0x2814, 0x4E0F},
	{0x2816, 0x110F},
	{0x2818, 0x3FE3},
	{0x281a, 0x12B0},
	{0x281c, 0xFB94},
	{0x281e, 0x4E0F},
	{0x2820, 0xC312},
	{0x2822, 0x100F},
	{0x2824, 0x110F},
	{0x2826, 0x110F},
	{0x2828, 0x110F},
	{0x282a, 0x413A},
	{0x282c, 0x4130},
	{0x282e, 0x120B},
	{0x2830, 0x120A},
	{0x2832, 0x1209},
	{0x2834, 0x1208},
	{0x2836, 0x93C2},
	{0x2838, 0x00C6},
	{0x283a, 0x2406},
	{0x283c, 0xB392},
	{0x283e, 0x732A},
	{0x2840, 0x2403},
	{0x2842, 0xB3D2},
	{0x2844, 0x00C7},
	{0x2846, 0x2412},
	{0x2848, 0x4292},
	{0x284a, 0x01A8},
	{0x284c, 0x0688},
	{0x284e, 0x4292},
	{0x2850, 0x01AA},
	{0x2852, 0x068A},
	{0x2854, 0x4292},
	{0x2856, 0x01AC},
	{0x2858, 0x068C},
	{0x285a, 0x4292},
	{0x285c, 0x01AE},
	{0x285e, 0x068E},
	{0x2860, 0x4292},
	{0x2862, 0x0190},
	{0x2864, 0x0A92},
	{0x2866, 0x4292},
	{0x2868, 0x0192},
	{0x286a, 0x0A94},
	{0x286c, 0x430E},
	{0x286e, 0x425F},
	{0x2870, 0x00C7},
	{0x2872, 0xF35F},
	{0x2874, 0xF37F},
	{0x2876, 0xF21F},
	{0x2878, 0x732A},
	{0x287a, 0x200C},
	{0x287c, 0xB3D2},
	{0x287e, 0x00C7},
	{0x2880, 0x2003},
	{0x2882, 0xB392},
	{0x2884, 0x8098},
	{0x2886, 0x2006},
	{0x2888, 0xB3A2},
	{0x288a, 0x732A},
	{0x288c, 0x2003},
	{0x288e, 0x9382},
	{0x2890, 0x8092},
	{0x2892, 0x2401},
	{0x2894, 0x431E},
	{0x2896, 0x4E82},
	{0x2898, 0x80AA},
	{0x289a, 0x930E},
	{0x289c, 0x2555},
	{0x289e, 0x4382},
	{0x28a0, 0x80B0},
	{0x28a2, 0x421F},
	{0x28a4, 0x732A},
	{0x28a6, 0xF31F},
	{0x28a8, 0x4F82},
	{0x28aa, 0x8098},
	{0x28ac, 0x425F},
	{0x28ae, 0x008C},
	{0x28b0, 0x4FC2},
	{0x28b2, 0x8090},
	{0x28b4, 0x43C2},
	{0x28b6, 0x8091},
	{0x28b8, 0x425F},
	{0x28ba, 0x009E},
	{0x28bc, 0x4F4A},
	{0x28be, 0x425F},
	{0x28c0, 0x009F},
	{0x28c2, 0xF37F},
	{0x28c4, 0x5F0A},
	{0x28c6, 0x110A},
	{0x28c8, 0x110A},
	{0x28ca, 0x425F},
	{0x28cc, 0x00B2},
	{0x28ce, 0x4F4B},
	{0x28d0, 0x425F},
	{0x28d2, 0x00B3},
	{0x28d4, 0xF37F},
	{0x28d6, 0x5F0B},
	{0x28d8, 0x110B},
	{0x28da, 0x110B},
	{0x28dc, 0x403D},
	{0x28de, 0x0813},
	{0x28e0, 0x403E},
	{0x28e2, 0x007F},
	{0x28e4, 0x90F2},
	{0x28e6, 0x0011},
	{0x28e8, 0x00BA},
	{0x28ea, 0x2CFF},
	{0x28ec, 0x403D},
	{0x28ee, 0x1009},
	{0x28f0, 0x430E},
	{0x28f2, 0x425F},
	{0x28f4, 0x00BA},
	{0x28f6, 0xF37F},
	{0x28f8, 0x108F},
	{0x28fa, 0xDE0F},
	{0x28fc, 0x4F82},
	{0x28fe, 0x0B90},
	{0x2900, 0x0C0A},
	{0x2902, 0x4D82},
	{0x2904, 0x0B8A},
	{0x2906, 0x0C0A},
	{0x2908, 0x425F},
	{0x290a, 0x0C87},
	{0x290c, 0x4F4E},
	{0x290e, 0x425F},
	{0x2910, 0x0C88},
	{0x2912, 0xF37F},
	{0x2914, 0x12B0},
	{0x2916, 0xF79C},
	{0x2918, 0x4F82},
	{0x291a, 0x0C8C},
	{0x291c, 0x425F},
	{0x291e, 0x0C85},
	{0x2920, 0x4F4E},
	{0x2922, 0x425F},
	{0x2924, 0x0C89},
	{0x2926, 0xF37F},
	{0x2928, 0x12B0},
	{0x292a, 0xF79C},
	{0x292c, 0x4F82},
	{0x292e, 0x0C8A},
	{0x2930, 0x425F},
	{0x2932, 0x00B7},
	{0x2934, 0x5F4F},
	{0x2936, 0x4FC2},
	{0x2938, 0x0CB0},
	{0x293a, 0x425F},
	{0x293c, 0x00B8},
	{0x293e, 0x5F4F},
	{0x2940, 0x4FC2},
	{0x2942, 0x0CB1},
	{0x2944, 0x4A0E},
	{0x2946, 0x5E0E},
	{0x2948, 0x5E0E},
	{0x294a, 0x5E0E},
	{0x294c, 0x5E0E},
	{0x294e, 0x4B0F},
	{0x2950, 0x5F0F},
	{0x2952, 0x5F0F},
	{0x2954, 0x5F0F},
	{0x2956, 0x5F0F},
	{0x2958, 0x5F0F},
	{0x295a, 0x5F0F},
	{0x295c, 0x5F0F},
	{0x295e, 0xDF0E},
	{0x2960, 0x4E82},
	{0x2962, 0x0A8E},
	{0x2964, 0xB22B},
	{0x2966, 0x2401},
	{0x2968, 0x533B},
	{0x296a, 0xB3E2},
	{0x296c, 0x0080},
	{0x296e, 0x2403},
	{0x2970, 0x40F2},
	{0x2972, 0x0003},
	{0x2974, 0x00B5},
	{0x2976, 0x40B2},
	{0x2978, 0x1001},
	{0x297a, 0x7500},
	{0x297c, 0x40B2},
	{0x297e, 0x0803},
	{0x2980, 0x7502},
	{0x2982, 0x40B2},
	{0x2984, 0x080F},
	{0x2986, 0x7504},
	{0x2988, 0x40B2},
	{0x298a, 0x6003},
	{0x298c, 0x7506},
	{0x298e, 0x40B2},
	{0x2990, 0x0801},
	{0x2992, 0x7508},
	{0x2994, 0x40B2},
	{0x2996, 0x0800},
	{0x2998, 0x750A},
	{0x299a, 0x421F},
	{0x299c, 0x0098},
	{0x299e, 0x821F},
	{0x29a0, 0x0092},
	{0x29a2, 0x531F},
	{0x29a4, 0xC312},
	{0x29a6, 0x100F},
	{0x29a8, 0x4F82},
	{0x29aa, 0x0A86},
	{0x29ac, 0x421F},
	{0x29ae, 0x00AC},
	{0x29b0, 0x821F},
	{0x29b2, 0x00A6},
	{0x29b4, 0x531F},
	{0x29b6, 0x4F82},
	{0x29b8, 0x0A88},
	{0x29ba, 0xB0B2},
	{0x29bc, 0x0010},
	{0x29be, 0x0A84},
	{0x29c0, 0x2485},
	{0x29c2, 0x421F},
	{0x29c4, 0x068C},
	{0x29c6, 0xC312},
	{0x29c8, 0x100F},
	{0x29ca, 0x4F82},
	{0x29cc, 0x0782},
	{0x29ce, 0x4292},
	{0x29d0, 0x068E},
	{0x29d2, 0x0784},
	{0x29d4, 0xB3D2},
	{0x29d6, 0x0CB6},
	{0x29d8, 0x2418},
	{0x29da, 0x421A},
	{0x29dc, 0x0CB8},
	{0x29de, 0x430B},
	{0x29e0, 0x425F},
	{0x29e2, 0x0CBA},
	{0x29e4, 0x4F4E},
	{0x29e6, 0x430F},
	{0x29e8, 0x4E0F},
	{0x29ea, 0x430E},
	{0x29ec, 0xDE0A},
	{0x29ee, 0xDF0B},
	{0x29f0, 0x421F},
	{0x29f2, 0x0CBC},
	{0x29f4, 0x4F0C},
	{0x29f6, 0x430D},
	{0x29f8, 0x421F},
	{0x29fa, 0x0CBE},
	{0x29fc, 0x430E},
	{0x29fe, 0xDE0C},
	{0x2a00, 0xDF0D},
	{0x2a02, 0x12B0},
	{0x2a04, 0xFBCA},
	{0x2a06, 0x4C82},
	{0x2a08, 0x0194},
	{0x2a0a, 0xB2A2},
	{0x2a0c, 0x0A84},
	{0x2a0e, 0x2412},
	{0x2a10, 0x421E},
	{0x2a12, 0x0A96},
	{0x2a14, 0xC312},
	{0x2a16, 0x100E},
	{0x2a18, 0x110E},
	{0x2a1a, 0x110E},
	{0x2a1c, 0x43C2},
	{0x2a1e, 0x0A98},
	{0x2a20, 0x431D},
	{0x2a22, 0x4E0F},
	{0x2a24, 0x9F82},
	{0x2a26, 0x0194},
	{0x2a28, 0x2846},
	{0x2a2a, 0x5E0F},
	{0x2a2c, 0x531D},
	{0x2a2e, 0x903D},
	{0x2a30, 0x0009},
	{0x2a32, 0x2BF8},
	{0x2a34, 0x4292},
	{0x2a36, 0x0084},
	{0x2a38, 0x7524},
	{0x2a3a, 0x4292},
	{0x2a3c, 0x0088},
	{0x2a3e, 0x7316},
	{0x2a40, 0x9382},
	{0x2a42, 0x8090},
	{0x2a44, 0x2403},
	{0x2a46, 0x4292},
	{0x2a48, 0x008A},
	{0x2a4a, 0x7316},
	{0x2a4c, 0x430E},
	{0x2a4e, 0x421F},
	{0x2a50, 0x0086},
	{0x2a52, 0x832F},
	{0x2a54, 0x9F82},
	{0x2a56, 0x0084},
	{0x2a58, 0x2801},
	{0x2a5a, 0x431E},
	{0x2a5c, 0x4292},
	{0x2a5e, 0x0086},
	{0x2a60, 0x7314},
	{0x2a62, 0x93C2},
	{0x2a64, 0x00BC},
	{0x2a66, 0x2007},
	{0x2a68, 0xB31E},
	{0x2a6a, 0x2405},
	{0x2a6c, 0x421F},
	{0x2a6e, 0x0084},
	{0x2a70, 0x532F},
	{0x2a72, 0x4F82},
	{0x2a74, 0x7314},
	{0x2a76, 0x425F},
	{0x2a78, 0x00BC},
	{0x2a7a, 0xF37F},
	{0x2a7c, 0xFE0F},
	{0x2a7e, 0x2406},
	{0x2a80, 0x421F},
	{0x2a82, 0x0086},
	{0x2a84, 0x503F},
	{0x2a86, 0xFFFD},
	{0x2a88, 0x4F82},
	{0x2a8a, 0x7524},
	{0x2a8c, 0x430E},
	{0x2a8e, 0x421F},
	{0x2a90, 0x7524},
	{0x2a92, 0x9F82},
	{0x2a94, 0x7528},
	{0x2a96, 0x2C01},
	{0x2a98, 0x431E},
	{0x2a9a, 0x430F},
	{0x2a9c, 0x9382},
	{0x2a9e, 0x8092},
	{0x2aa0, 0x2001},
	{0x2aa2, 0x431F},
	{0x2aa4, 0xFE0F},
	{0x2aa6, 0x40B2},
	{0x2aa8, 0x0032},
	{0x2aaa, 0x7522},
	{0x2aac, 0x2462},
	{0x2aae, 0x50B2},
	{0x2ab0, 0x0032},
	{0x2ab2, 0x7522},
	{0x2ab4, 0x3C5E},
	{0x2ab6, 0x431F},
	{0x2ab8, 0x4D0E},
	{0x2aba, 0x533E},
	{0x2abc, 0x930E},
	{0x2abe, 0x2403},
	{0x2ac0, 0x5F0F},
	{0x2ac2, 0x831E},
	{0x2ac4, 0x23FD},
	{0x2ac6, 0x4FC2},
	{0x2ac8, 0x0A98},
	{0x2aca, 0x3FB4},
	{0x2acc, 0x4292},
	{0x2ace, 0x0A86},
	{0x2ad0, 0x0782},
	{0x2ad2, 0x421F},
	{0x2ad4, 0x0A88},
	{0x2ad6, 0x4B0E},
	{0x2ad8, 0x930E},
	{0x2ada, 0x2404},
	{0x2adc, 0xC312},
	{0x2ade, 0x100F},
	{0x2ae0, 0x831E},
	{0x2ae2, 0x23FC},
	{0x2ae4, 0x4F82},
	{0x2ae6, 0x0784},
	{0x2ae8, 0x3F75},
	{0x2aea, 0x90F2},
	{0x2aec, 0x0011},
	{0x2aee, 0x00BA},
	{0x2af0, 0x2807},
	{0x2af2, 0x90F2},
	{0x2af4, 0x0051},
	{0x2af6, 0x00BA},
	{0x2af8, 0x2C03},
	{0x2afa, 0x403D},
	{0x2afc, 0x0A13},
	{0x2afe, 0x3EF9},
	{0x2b00, 0x90F2},
	{0x2b02, 0x0051},
	{0x2b04, 0x00BA},
	{0x2b06, 0x2807},
	{0x2b08, 0x90F2},
	{0x2b0a, 0xFF91},
	{0x2b0c, 0x00BA},
	{0x2b0e, 0x2C03},
	{0x2b10, 0x403D},
	{0x2b12, 0x0813},
	{0x2b14, 0x3EEE},
	{0x2b16, 0x90F2},
	{0x2b18, 0xFF91},
	{0x2b1a, 0x00BA},
	{0x2b1c, 0x2807},
	{0x2b1e, 0x90F2},
	{0x2b20, 0xFFB1},
	{0x2b22, 0x00BA},
	{0x2b24, 0x2C03},
	{0x2b26, 0x403E},
	{0x2b28, 0x0060},
	{0x2b2a, 0x3EE3},
	{0x2b2c, 0x90F2},
	{0x2b2e, 0xFFB1},
	{0x2b30, 0x00BA},
	{0x2b32, 0x2807},
	{0x2b34, 0x90F2},
	{0x2b36, 0xFFD1},
	{0x2b38, 0x00BA},
	{0x2b3a, 0x2C03},
	{0x2b3c, 0x403E},
	{0x2b3e, 0x0050},
	{0x2b40, 0x3ED8},
	{0x2b42, 0x403E},
	{0x2b44, 0x003C},
	{0x2b46, 0x3ED5},
	{0x2b48, 0x421F},
	{0x2b4a, 0x80B0},
	{0x2b4c, 0x903F},
	{0x2b4e, 0x0009},
	{0x2b50, 0x2C04},
	{0x2b52, 0x531F},
	{0x2b54, 0x4F82},
	{0x2b56, 0x80B0},
	{0x2b58, 0x3EA4},
	{0x2b5a, 0x421F},
	{0x2b5c, 0x80AC},
	{0x2b5e, 0x903F},
	{0x2b60, 0x012E},
	{0x2b62, 0x2C04},
	{0x2b64, 0x531F},
	{0x2b66, 0x4F82},
	{0x2b68, 0x80AC},
	{0x2b6a, 0x3E9B},
	{0x2b6c, 0x4382},
	{0x2b6e, 0x80AC},
	{0x2b70, 0x3E98},
	{0x2b72, 0xD392},
	{0x2b74, 0x7102},
	{0x2b76, 0x4138},
	{0x2b78, 0x4139},
	{0x2b7a, 0x413A},
	{0x2b7c, 0x413B},
	{0x2b7e, 0x4130},
	{0x2b80, 0x4F82},
	{0x2b82, 0x7600},
	{0x2b84, 0x0270},
	{0x2b86, 0x0000},
	{0x2b88, 0x0C0E},
	{0x2b8a, 0x0270},
	{0x2b8c, 0x0001},
	{0x2b8e, 0x4130},
	{0x2b90, 0xDF02},
	{0x2b92, 0x3FFE},
	{0x2b94, 0x430E},
	{0x2b96, 0x930A},
	{0x2b98, 0x2407},
	{0x2b9a, 0xC312},
	{0x2b9c, 0x100C},
	{0x2b9e, 0x2801},
	{0x2ba0, 0x5A0E},
	{0x2ba2, 0x5A0A},
	{0x2ba4, 0x930C},
	{0x2ba6, 0x23F7},
	{0x2ba8, 0x4130},
	{0x2baa, 0x430E},
	{0x2bac, 0x430F},
	{0x2bae, 0x3C08},
	{0x2bb0, 0xC312},
	{0x2bb2, 0x100D},
	{0x2bb4, 0x100C},
	{0x2bb6, 0x2802},
	{0x2bb8, 0x5A0E},
	{0x2bba, 0x6B0F},
	{0x2bbc, 0x5A0A},
	{0x2bbe, 0x6B0B},
	{0x2bc0, 0x930C},
	{0x2bc2, 0x23F6},
	{0x2bc4, 0x930D},
	{0x2bc6, 0x23F4},
	{0x2bc8, 0x4130},
	{0x2bca, 0xEF0F},
	{0x2bcc, 0xEE0E},
	{0x2bce, 0x4039},
	{0x2bd0, 0x0021},
	{0x2bd2, 0x3C0A},
	{0x2bd4, 0x1008},
	{0x2bd6, 0x6E0E},
	{0x2bd8, 0x6F0F},
	{0x2bda, 0x9B0F},
	{0x2bdc, 0x2805},
	{0x2bde, 0x2002},
	{0x2be0, 0x9A0E},
	{0x2be2, 0x2802},
	{0x2be4, 0x8A0E},
	{0x2be6, 0x7B0F},
	{0x2be8, 0x6C0C},
	{0x2bea, 0x6D0D},
	{0x2bec, 0x6808},
	{0x2bee, 0x8319},
	{0x2bf0, 0x23F1},
	{0x2bf2, 0x4130},
	{0x2bf4, 0x0000},
	{0x2ffe, 0xf000},
	{0x3000, 0x00AE},
	{0x3002, 0x00AE},
	{0x3004, 0x00AE},
	{0x3006, 0x00AE},
	{0x3008, 0x02AE},
	{0x300A, 0x00AE},
	{0x300C, 0x00AE},
	{0x300E, 0x02AA},
	{0x4000, 0x0400},
	{0x4002, 0x0400},
	{0x4004, 0x0C04},
	{0x4006, 0x0C04},
	{0x4008, 0x0C3D},
	{0x400A, 0x0C04},
	{0x400C, 0x0C04},
	{0x400E, 0x8C03},

	//--- Initial Set file ---//
	{0x0B04, 0x07CB},
	{0x0B06, 0x5ED7},
	{0x0B14, 0x370B},
	{0x0B16, 0x4A0B},
	{0x004C, 0x0100},
	{0x0032, 0x0101},
	{0x0036, 0x0048},
	{0x0038, 0x4800},
	{0x0138, 0x0004},
	{0x013A, 0x0100},
	{0x0C00, 0x3BC7}, //3BC1}, 140311
	{0x0C0E, 0x0500},
	{0x0C10, 0x0510},
	{0x0C12, 0x0E00}, //140311
	{0x0C16, 0x0000},
	{0x0C18, 0x0000},
	{0x0C36, 0x0100},
	{0x0902, 0x4101},
	{0x090A, 0x03E4},
	{0x090C, 0x0020},
	{0x090E, 0x0020},
	{0x0910, 0x5D07},
	{0x0912, 0x061e},
	{0x0914, 0x0407},
	{0x0916, 0x0b0a},
	{0x0918, 0x0e09},
	{0x000E, 0x0000}, //x_addr_start_lobp_h.
	{0x0014, 0x003F}, //x_addr_end_lobp_h.
	{0x0010, 0x0050}, //x_addr_start_robp_h.
	{0x0016, 0x008F}, //x_addr_end_robp_h.
	{0x0012, 0x00A4}, //x_addr_start_hact_h.
	{0x0018, 0x0AD3}, //x_addr_end_hact_h.
	{0x0020, 0x0700}, //x_regin_sel
	{0x0022, 0x0004}, //y_addr_start_fobp_h.
	{0x0028, 0x000B}, //y_addr_end_fobp_h.
	{0x0024, 0xFFFA}, //y_addr_start_dummy_h.
	{0x002A, 0xFFFF}, //y_addr_end_dummy_h.
	{0x0026, 0x0010}, //y_addr_start_vact_h.
	{0x002C, 0x07B7}, //y_addr_end_vact_h.
	{0x0034, 0x0700}, //Y_region_sel
	{0x0128, 0x0002}, // digital_crop_x_offset_l
	{0x012A, 0x0000}, // digital_crop_y_offset_l
	{0x012C, 0x0A20}, //2C}, //2592 digital_crop_image_width
	{0x012E, 0x0798}, //A4}, //1944 digital_crop_image_height
	{0x0110, 0x0A20}, //X_output_size_h
	{0x0112, 0x0798}, //Y_output_size_h
	{0x0006, 0x07c0}, //frame_length_h 1984
	{0x0008, 0x0B40}, //line_length_h 2880
	{0x000A, 0x0DB0},
	{0x003C, 0x0000},
	{0x0000, 0x0000},
	{0x0500, 0x0000},
	{0x0700, 0x0590},
	{0x003A, 0x0000}, //Analog Gain.  0x00=x1, 0x70=x8, 0xf0=x16.
	{0x0508, 0x0100}, //DG_Gr_h.  0x01=x1, 0x07=x8
	{0x050a, 0x0100}, //DG_Gb_h.  0x01=x1, 0x07=x8.
	{0x050c, 0x0100}, //DG_R_h.  0x01=x1, 0x07=x8.
	{0x050e, 0x0100}, //DG_B_h.  0x01=x1, 0x07=x8.

	//-----< Exp.Time >------------------------//
	// Pclk_88Mhz @ Line_length_pclk : 2880 @Exp.Time 33.33ms
	{0x0002, 0x04b0}, //Fine_int : 33.33ms@Pclk88mhz@Line_length2880
	{0x0004, 0x07F4}, //coarse_int : 33.33ms@Pclk88mhz@Line_length2880
	{0x0A04, 0x0112},
	//{0x0A00, 0x0100}, //sleep Off
};

LOCAL const SENSOR_REG_T hi544_1296x972_setting[] = {
	{0x0A00, 0x0000}, //sleep On
	{0x0B16, 0x4A0B},
	{0x004C, 0x0100}, //
	{0x0032, 0x0301}, //
	{0x001E, 0x0301}, //
	{0x000C, 0x0000}, //
	//--- Pixel Array Addressing ------//
	{0x0012, 0x00A4}, //x_addr_start_hact_h. //
	{0x0018, 0x0AD3}, //x_addr_end_hact_h.   //
	{0x0026, 0x0010}, //y_addr_start_vact_h. //
	{0x002C, 0x07B7}, //y_addr_end_vact_h.   //
	{0x0128, 0x0002}, // digital_crop_x_offset_l  //
	{0x012A, 0x0000}, // digital_crop_y_offset_l  //
	{0x012C, 0x0510}, // digital_crop_image_width //
	{0x012E, 0x03CC}, // digital_crop_image_height //

	//Image size 1296x972
	{0x0110, 0x0510}, //X_output_size_h //
	{0x0112, 0x03CC}, //Y_output_size_h //

	{0x0006, 0x07c0}, //frame_length_h 1984
	{0x0008, 0x0B40}, //line_length_h 2880
	{0x000A, 0x0DB0}, //
	{0x0002, 0x04b0}, //Fine.int //
	{0x0700, 0x00AC}, //
	{0x0A04, 0x0112}, //

	//{0x0A00, 0x0100}, //sleep Off //

};

LOCAL const SENSOR_REG_T hi544_2592x1944_setting[] = {
	{0x0A00, 0x0000}, //sleep On
	{0x0B16, 0x4A0B},
	{0x004C, 0x0100}, //
	{0x0032, 0x0101}, //
	{0x000C, 0x0000}, //
	{0x001E, 0x0101},  //

	//--- Pixel Array Addressing ------//
	{0x0012, 0x00A4}, //x_addr_start_hact_h. //
	{0x0018, 0x0AD3}, //x_addr_end_hact_h.   //

	{0x0026, 0x0010}, //y_addr_start_vact_h. //
	{0x002C, 0x07B7}, //y_addr_end_vact_h.   //

	{0x0128, 0x0002}, // digital_crop_x_offset_l  //
	{0x012A, 0x0000}, // digital_crop_y_offset_l  //
	{0x012C, 0x0A20}, //2C}, //2592 digital_crop_image_width //
	{0x012E, 0x0798}, //A4}, //1944 digital_crop_image_height //

	//Image size 2592x1944
	{0x0110, 0x0A20}, //X_output_size_h //
	{0x0112, 0x0798}, //Y_output_size_h //

	{0x0006, 0x07c0}, //frame_length_h 1984
	{0x0008, 0x0B40}, //line_length_h 2880
	{0x000A, 0x0DB0}, //
	{0x0002, 0x04b0}, //Fine.int //
	{0x0700, 0x0590}, //
	{0x0A04, 0x0112}, //

	//{0x0A00, 0x0100}, //sleep Off //

};

LOCAL SENSOR_REG_TAB_INFO_T s_hi544_resolution_Tab_RAW[] = {
	{ADDR_AND_LEN_OF_ARRAY(hi544_common_init), 0, 0, 24, SENSOR_IMAGE_FORMAT_RAW},
	//{ADDR_AND_LEN_OF_ARRAY(hi544_1296x972_setting), 1296, 972, 24, SENSOR_IMAGE_FORMAT_RAW},
	{ADDR_AND_LEN_OF_ARRAY(hi544_2592x1944_setting), 2592, 1944, 24, SENSOR_IMAGE_FORMAT_RAW},
	{PNULL, 0, 0, 0, 0, 0},

	{PNULL, 0, 0, 0, 0, 0},
	{PNULL, 0, 0, 0, 0, 0},
	{PNULL, 0, 0, 0, 0, 0},
	{PNULL, 0, 0, 0, 0, 0},
	{PNULL, 0, 0, 0, 0, 0},
	{PNULL, 0, 0, 0, 0, 0}
};

LOCAL SENSOR_TRIM_T s_hi544_Resolution_Trim_Tab[] = {
	{0, 0, 0, 0, 0, 0, 0, {0, 0, 0, 0}},
	//{0, 0, 1296, 972, 163, 440, 0x07c0},
	{0, 0, 2592, 1944, 163, 440, 0x07c0, {0, 0, 2592, 1944}},
	{0, 0, 0, 0, 0, 0, 0, {0, 0, 0, 0}},

	{0, 0, 0, 0, 0, 0, 0, {0, 0, 0, 0}},
	{0, 0, 0, 0, 0, 0, 0, {0, 0, 0, 0}},
	{0, 0, 0, 0, 0, 0, 0, {0, 0, 0, 0}},
	{0, 0, 0, 0, 0, 0, 0, {0, 0, 0, 0}},
	{0, 0, 0, 0, 0, 0, 0, {0, 0, 0, 0}},
	{0, 0, 0, 0, 0, 0, 0, {0, 0, 0, 0}}
};


LOCAL const SENSOR_REG_T s_hi544_1296x972_video_tab[SENSOR_VIDEO_MODE_MAX][1] = {
	/*video mode 0: ?fps*/
	{
		{0xffff, 0xff}
	},
	/* video mode 1:?fps*/
	{
		{0xffff, 0xff}
	},
	/* video mode 2:?fps*/
	{
		{0xffff, 0xff}
	},
	/* video mode 3:?fps*/
	{
		{0xffff, 0xff}
	}
};

LOCAL const SENSOR_REG_T  s_hi544_2592x1944_video_tab[SENSOR_VIDEO_MODE_MAX][1] = {
	/*video mode 0: ?fps*/
	{
		{0xffff, 0xff}
	},
	/* video mode 1:?fps*/
	{
		{0xffff, 0xff}
	},
	/* video mode 2:?fps*/
	{
		{0xffff, 0xff}
	},
	/* video mode 3:?fps*/
	{
		{0xffff, 0xff}
	}
};

LOCAL SENSOR_VIDEO_INFO_T s_hi544_video_info[] = {
	{{{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}, PNULL},
	//{{{30, 30, 163, 100}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},(SENSOR_REG_T**)s_hi544_1296x972_video_tab},
	{{{30, 30, 163, 100}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},(SENSOR_REG_T**)s_hi544_2592x1944_video_tab},
	{{{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}, PNULL},
	{{{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}, PNULL},
	{{{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}, PNULL},
	{{{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}, PNULL},
	{{{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}, PNULL},
	{{{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}, PNULL},
	{{{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}, PNULL}
};

LOCAL uint32_t _hi544_set_video_mode(uint32_t param)
{
	SENSOR_REG_T_PTR sensor_reg_ptr;
	uint16_t         i = 0x00;
	uint32_t         mode;

	if (param >= SENSOR_VIDEO_MODE_MAX)
		return 0;

	if (SENSOR_SUCCESS != Sensor_GetMode(&mode)) {
		SENSOR_PRINT("fail.");
		return SENSOR_FAIL;
	}

	if (PNULL == s_hi544_video_info[mode].setting_ptr) {
		SENSOR_PRINT("fail.");
		return SENSOR_FAIL;
	}

	sensor_reg_ptr = (SENSOR_REG_T_PTR)&s_hi544_video_info[mode].setting_ptr[param];
	if (PNULL == sensor_reg_ptr) {
		SENSOR_PRINT("fail.");
		return SENSOR_FAIL;
	}

	for (i=0x00; (0xffff!=sensor_reg_ptr[i].reg_addr)||(0xff!=sensor_reg_ptr[i].reg_value); i++) {
		Sensor_WriteReg(sensor_reg_ptr[i].reg_addr, sensor_reg_ptr[i].reg_value);
	}

	SENSOR_PRINT("0x%02x", param);
	return 0;
}


LOCAL SENSOR_IOCTL_FUNC_TAB_T s_hi544_ioctl_func_tab = {
	PNULL,
	_hi544_PowerOn,
	PNULL,
	_hi544_Identify,

	PNULL,			// write register
	PNULL,			// read  register
	PNULL,
	_hi544_GetResolutionTrimTab,

	// External
	PNULL,
	PNULL,
	PNULL,

	PNULL, //_hi544_set_brightness,
	PNULL, // _hi544_set_contrast,
	PNULL,
	PNULL,			//_hi544_set_saturation,

	PNULL, //_hi544_set_work_mode,
	PNULL, //_hi544_set_image_effect,

	_hi544_BeforeSnapshot,
	_hi544_after_snapshot,
	PNULL,  //_hi544_flash,
	PNULL,
	_hi544_write_exposure,
	PNULL,
	_hi544_write_gain,
	PNULL,
	PNULL,
	_hi544_write_af,
	PNULL,
	PNULL, //_hi544_set_awb,
	PNULL,
	PNULL,
	PNULL, //_hi544_set_ev,
	PNULL,
	PNULL,
	PNULL,
	PNULL, //_hi544_GetExifInfo,
	PNULL,   //_hi544_ExtFunc,
	PNULL, //_hi544_set_anti_flicker,
	PNULL,  //_hi544_set_video_mode,
	PNULL, //pick_jpeg_stream
	PNULL,  //meter_mode
	PNULL, //get_status
	_hi544_StreamOn,
	_hi544_StreamOff,
	PNULL
};


SENSOR_INFO_T g_hi544_mipi_raw_info = {
	hi544_I2C_ADDR_W,	// salve i2c write address
	hi544_I2C_ADDR_R,	// salve i2c read address

	SENSOR_I2C_REG_16BIT | SENSOR_I2C_VAL_16BIT | SENSOR_I2C_FREQ_400,	// bit0: 0: i2c register value is 8 bit, 1: i2c register value is 16 bit
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

	SENSOR_LOW_PULSE_RESET,	// reset pulse level
	5,			// reset pulse width(ms)

	SENSOR_LOW_LEVEL_PWDN,	// 1: high level valid; 0: low level valid

	1,			// count of identify code
	{{0x0f16, 0x4405}},		// for Example: index = 0-> Device id, index = 1 -> version id

	SENSOR_AVDD_2800MV,	// voltage of avdd

	2592,			// max width of source image
	1944,			// max height of source image
	"hi544",		// name of sensor

	SENSOR_IMAGE_FORMAT_RAW,	// define in SENSOR_IMAGE_FORMAT_E enum,SENSOR_IMAGE_FORMAT_MAX
	// if set to SENSOR_IMAGE_FORMAT_MAX here, image format depent on SENSOR_REG_TAB_INFO_T

	SENSOR_IMAGE_PATTERN_RAWRGB_B,// pattern of input image form sensor;

	s_hi544_resolution_Tab_RAW,	// point to resolution table information structure
	&s_hi544_ioctl_func_tab,	// point to ioctl function table
	&s_hi544_mipi_raw_info_ptr,		// information and table about Rawrgb sensor
	NULL,			//&g_hi544_ext_info,                // extend information about sensor
	SENSOR_AVDD_1800MV,	// iovdd
	SENSOR_AVDD_1800MV,	// dvdd
	1,			// skip frame num before preview
	1,			// skip frame num before capture
	0,			// deci frame num during preview
	0,			// deci frame num during video preview

	0,
	0,
	0,
	0,
	0,
	{SENSOR_INTERFACE_TYPE_CSI2, 2, 10, 0},
	s_hi544_video_info,
	3,			// skip frame num while change setting
};

LOCAL struct sensor_raw_info* Sensor_GetContext(void)
{
	return s_hi544_mipi_raw_info_ptr;
}

LOCAL uint32_t Sensor_hi544_InitRawTuneInfo(void)
{
	uint32_t rtn=0x00;
	struct sensor_raw_info* raw_sensor_ptr=Sensor_GetContext();
	struct sensor_raw_tune_info* sensor_ptr=raw_sensor_ptr->tune_ptr;
	struct sensor_raw_cali_info* cali_ptr=raw_sensor_ptr->cali_ptr;
#if 0
	raw_sensor_ptr->version_info->version_id=0x00010000;
	raw_sensor_ptr->version_info->srtuct_size=sizeof(struct sensor_raw_info);

	//bypass
	sensor_ptr->version_id=0x00010000;
	sensor_ptr->blc_bypass=0x01;
	sensor_ptr->nlc_bypass=0x01;
	sensor_ptr->lnc_bypass=0x01;
	sensor_ptr->ae_bypass=0x00;
	sensor_ptr->awb_bypass=0x01;
	sensor_ptr->bpc_bypass=0x01;
	sensor_ptr->denoise_bypass=0x01;
	sensor_ptr->grgb_bypass=0x00;
	sensor_ptr->cmc_bypass=0x01;
	sensor_ptr->gamma_bypass=0x01;
	sensor_ptr->uvdiv_bypass=0x01;
	sensor_ptr->pref_bypass=0x01;
	sensor_ptr->bright_bypass=0x01;
	sensor_ptr->contrast_bypass=0x01;
	sensor_ptr->hist_bypass=0x01;
	sensor_ptr->auto_contrast_bypass=0x01;
	sensor_ptr->af_bypass=0x00;
	sensor_ptr->edge_bypass=0x01;
	sensor_ptr->fcs_bypass=0x01;
	sensor_ptr->css_bypass=0x01;
	sensor_ptr->saturation_bypass=0x01;
	sensor_ptr->hdr_bypass=0x01;
	sensor_ptr->glb_gain_bypass=0x01;
	sensor_ptr->chn_gain_bypass=0x01;

	//blc
	sensor_ptr->blc.mode=0x00;
	sensor_ptr->blc.offset[0].r=0x0f;
	sensor_ptr->blc.offset[0].gr=0x0f;
	sensor_ptr->blc.offset[0].gb=0x0f;
	sensor_ptr->blc.offset[0].b=0x0f;

	sensor_ptr->blc.offset[1].r=0x0f;
	sensor_ptr->blc.offset[1].gr=0x0f;
	sensor_ptr->blc.offset[1].gb=0x0f;
	sensor_ptr->blc.offset[1].b=0x0f;

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
	sensor_ptr->ae.normal_fix_fps=0;
	sensor_ptr->ae.night_fix_fps=0;
	sensor_ptr->ae.video_fps=0x1e;
	sensor_ptr->ae.target_lum=120;
	sensor_ptr->ae.target_zone=8;
	sensor_ptr->ae.quick_mode=1;
	sensor_ptr->ae.smart=0x00;// bit0: denoise bit1: edge bit2: startion
	sensor_ptr->ae.smart_rotio=255;
	sensor_ptr->ae.smart_mode=0; // 0: gain 1: lum
	sensor_ptr->ae.smart_base_gain=64;
	sensor_ptr->ae.smart_wave_min=0;
	sensor_ptr->ae.smart_wave_max=1023;
	sensor_ptr->ae.smart_pref_min=0;
	sensor_ptr->ae.smart_pref_max=255;
	sensor_ptr->ae.smart_denoise_min_index=0;
	sensor_ptr->ae.smart_denoise_max_index=254;
	sensor_ptr->ae.smart_edge_min_index=0;
	sensor_ptr->ae.smart_edge_max_index=6;
	sensor_ptr->ae.smart_sta_low_thr=40;
	sensor_ptr->ae.smart_sta_high_thr=120;
	sensor_ptr->ae.smart_sta_rotio=128;
	sensor_ptr->ae.ev[0]=0xd0;
	sensor_ptr->ae.ev[1]=0xe0;
	sensor_ptr->ae.ev[2]=0xf0;
	sensor_ptr->ae.ev[3]=0x00;
	sensor_ptr->ae.ev[4]=0x10;
	sensor_ptr->ae.ev[5]=0x20;
	sensor_ptr->ae.ev[6]=0x30;
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
	sensor_ptr->awb.quick_mode = 1;
	sensor_ptr->awb.r_gain[0]=0x6c0;
	sensor_ptr->awb.g_gain[0]=0x400;
	sensor_ptr->awb.b_gain[0]=0x600;
	sensor_ptr->awb.r_gain[1]=0x480;
	sensor_ptr->awb.g_gain[1]=0x400;
	sensor_ptr->awb.b_gain[1]=0xc00;
	sensor_ptr->awb.r_gain[2]=0x400;
	sensor_ptr->awb.g_gain[2]=0x400;
	sensor_ptr->awb.b_gain[2]=0x400;
	sensor_ptr->awb.r_gain[3]=0x3fc;
	sensor_ptr->awb.g_gain[3]=0x400;
	sensor_ptr->awb.b_gain[3]=0x400;
	sensor_ptr->awb.r_gain[4]=0x480;
	sensor_ptr->awb.g_gain[4]=0x400;
	sensor_ptr->awb.b_gain[4]=0x800;
	sensor_ptr->awb.r_gain[5]=0x700;
	sensor_ptr->awb.g_gain[5]=0x400;
	sensor_ptr->awb.b_gain[5]=0x500;
	sensor_ptr->awb.r_gain[6]=0xa00;
	sensor_ptr->awb.g_gain[6]=0x400;
	sensor_ptr->awb.b_gain[6]=0x4c0;
	sensor_ptr->awb.r_gain[7]=0x400;
	sensor_ptr->awb.g_gain[7]=0x400;
	sensor_ptr->awb.b_gain[7]=0x400;
	sensor_ptr->awb.r_gain[8]=0x400;
	sensor_ptr->awb.g_gain[8]=0x400;
	sensor_ptr->awb.b_gain[8]=0x400;
	sensor_ptr->awb.target_zone=0x10;

	/*awb win*/
	sensor_ptr->awb.win[0].x=135;
	sensor_ptr->awb.win[0].yt=232;
	sensor_ptr->awb.win[0].yb=219;

	sensor_ptr->awb.win[1].x=139;
	sensor_ptr->awb.win[1].yt=254;
	sensor_ptr->awb.win[1].yb=193;

	sensor_ptr->awb.win[2].x=145;
	sensor_ptr->awb.win[2].yt=259;
	sensor_ptr->awb.win[2].yb=170;

	sensor_ptr->awb.win[3].x=155;
	sensor_ptr->awb.win[3].yt=259;
	sensor_ptr->awb.win[3].yb=122;

	sensor_ptr->awb.win[4].x=162;
	sensor_ptr->awb.win[4].yt=256;
	sensor_ptr->awb.win[4].yb=112;

	sensor_ptr->awb.win[5].x=172;
	sensor_ptr->awb.win[5].yt=230;
	sensor_ptr->awb.win[5].yb=110;

	sensor_ptr->awb.win[6].x=180;
	sensor_ptr->awb.win[6].yt=195;
	sensor_ptr->awb.win[6].yb=114;

	sensor_ptr->awb.win[7].x=184;
	sensor_ptr->awb.win[7].yt=185;
	sensor_ptr->awb.win[7].yb=120;

	sensor_ptr->awb.win[8].x=190;
	sensor_ptr->awb.win[8].yt=179;
	sensor_ptr->awb.win[8].yb=128;

	sensor_ptr->awb.win[9].x=199;
	sensor_ptr->awb.win[9].yt=175;
	sensor_ptr->awb.win[9].yb=131;

	sensor_ptr->awb.win[10].x=205;
	sensor_ptr->awb.win[10].yt=172;
	sensor_ptr->awb.win[10].yb=129;

	sensor_ptr->awb.win[11].x=210;
	sensor_ptr->awb.win[11].yt=169;
	sensor_ptr->awb.win[11].yb=123;

	sensor_ptr->awb.win[12].x=215;
	sensor_ptr->awb.win[12].yt=166;
	sensor_ptr->awb.win[12].yb=112;

	sensor_ptr->awb.win[13].x=226;
	sensor_ptr->awb.win[13].yt=159;
	sensor_ptr->awb.win[13].yb=98;

	sensor_ptr->awb.win[14].x=234;
	sensor_ptr->awb.win[14].yt=153;
	sensor_ptr->awb.win[14].yb=92;

	sensor_ptr->awb.win[15].x=248;
	sensor_ptr->awb.win[15].yt=144;
	sensor_ptr->awb.win[15].yb=84;

	sensor_ptr->awb.win[16].x=265;
	sensor_ptr->awb.win[16].yt=133;
	sensor_ptr->awb.win[16].yb=81;

	sensor_ptr->awb.win[17].x=277;
	sensor_ptr->awb.win[17].yt=126;
	sensor_ptr->awb.win[17].yb=79;

	sensor_ptr->awb.win[18].x=291;
	sensor_ptr->awb.win[18].yt=119;
	sensor_ptr->awb.win[18].yb=80;

	sensor_ptr->awb.win[19].x=305;
	sensor_ptr->awb.win[19].yt=109;
	sensor_ptr->awb.win[19].yb=90;

	sensor_ptr->awb.gain_convert[0].r=0x100;
	sensor_ptr->awb.gain_convert[0].g=0x100;
	sensor_ptr->awb.gain_convert[0].b=0x100;

	sensor_ptr->awb.gain_convert[1].r=0x100;
	sensor_ptr->awb.gain_convert[1].g=0x100;
	sensor_ptr->awb.gain_convert[1].b=0x100;

	//hi544 awb param
	sensor_ptr->awb.t_func.a = 274;
	sensor_ptr->awb.t_func.b = -335;
	sensor_ptr->awb.t_func.shift = 10;

	sensor_ptr->awb.wp_count_range.min_proportion = 256 / 128;
	sensor_ptr->awb.wp_count_range.max_proportion = 256 / 4;

	sensor_ptr->awb.g_estimate.num = 4;
	sensor_ptr->awb.g_estimate.t_thr[0] = 2000;
	sensor_ptr->awb.g_estimate.g_thr[0][0] = 406;    //0.404
	sensor_ptr->awb.g_estimate.g_thr[0][1] = 419;    //0.414
	sensor_ptr->awb.g_estimate.w_thr[0][0] = 255;
	sensor_ptr->awb.g_estimate.w_thr[0][1] = 0;

	sensor_ptr->awb.g_estimate.t_thr[1] = 3000;
	sensor_ptr->awb.g_estimate.g_thr[1][0] = 406;    //0.404
	sensor_ptr->awb.g_estimate.g_thr[1][1] = 419;    //0.414
	sensor_ptr->awb.g_estimate.w_thr[1][0] = 255;
	sensor_ptr->awb.g_estimate.w_thr[1][1] = 0;

	sensor_ptr->awb.g_estimate.t_thr[2] = 6500;
	sensor_ptr->awb.g_estimate.g_thr[2][0] = 445;
	sensor_ptr->awb.g_estimate.g_thr[2][1] = 478;
	sensor_ptr->awb.g_estimate.w_thr[2][0] = 255;
	sensor_ptr->awb.g_estimate.w_thr[2][1] = 0;

	sensor_ptr->awb.g_estimate.t_thr[3] = 20000;
	sensor_ptr->awb.g_estimate.g_thr[3][0] = 407;
	sensor_ptr->awb.g_estimate.g_thr[3][1] = 414;
	sensor_ptr->awb.g_estimate.w_thr[3][0] = 255;
	sensor_ptr->awb.g_estimate.w_thr[3][1] = 0;

	sensor_ptr->awb.gain_adjust.num = 5;
	sensor_ptr->awb.gain_adjust.t_thr[0] = 1600;
	sensor_ptr->awb.gain_adjust.w_thr[0] = 192;
	sensor_ptr->awb.gain_adjust.t_thr[1] = 2200;
	sensor_ptr->awb.gain_adjust.w_thr[1] = 208;
	sensor_ptr->awb.gain_adjust.t_thr[2] = 3500;
	sensor_ptr->awb.gain_adjust.w_thr[2] = 256;
	sensor_ptr->awb.gain_adjust.t_thr[3] = 10000;
	sensor_ptr->awb.gain_adjust.w_thr[3] = 256;
	sensor_ptr->awb.gain_adjust.t_thr[4] = 12000;
	sensor_ptr->awb.gain_adjust.w_thr[4] = 128;

	sensor_ptr->awb.light.num = 7;
	sensor_ptr->awb.light.t_thr[0] = 2300;
	sensor_ptr->awb.light.w_thr[0] = 2;
	sensor_ptr->awb.light.t_thr[1] = 2850;
	sensor_ptr->awb.light.w_thr[1] = 4;
	sensor_ptr->awb.light.t_thr[2] = 4150;
	sensor_ptr->awb.light.w_thr[2] = 8;
	sensor_ptr->awb.light.t_thr[3] = 5500;
	sensor_ptr->awb.light.w_thr[3] = 160;
	sensor_ptr->awb.light.t_thr[4] = 6500;
	sensor_ptr->awb.light.w_thr[4] = 192;
	sensor_ptr->awb.light.t_thr[5] = 7500;
	sensor_ptr->awb.light.w_thr[5] = 96;
	sensor_ptr->awb.light.t_thr[6] = 8200;
	sensor_ptr->awb.light.w_thr[6] = 8;

	sensor_ptr->awb.steady_speed = 6;
	sensor_ptr->awb.debug_level = 2;
	sensor_ptr->awb.smart = 1;
#endif
	sensor_ptr->awb.alg_id = 1;
	sensor_ptr->awb.smart_index = 4;
#if 0
	//bpc
	sensor_ptr->bpc.flat_thr=80;
	sensor_ptr->bpc.std_thr=20;
	sensor_ptr->bpc.texture_thr=2;

	// denoise
	sensor_ptr->denoise.write_back=0x00;
	sensor_ptr->denoise.r_thr=0x08;
	sensor_ptr->denoise.g_thr=0x08;
	sensor_ptr->denoise.b_thr=0x08;

	sensor_ptr->denoise.diswei[0]=255;
	sensor_ptr->denoise.diswei[1]=253;
	sensor_ptr->denoise.diswei[2]=251;
	sensor_ptr->denoise.diswei[3]=249;
	sensor_ptr->denoise.diswei[4]=247;
	sensor_ptr->denoise.diswei[5]=245;
	sensor_ptr->denoise.diswei[6]=243;
	sensor_ptr->denoise.diswei[7]=241;
	sensor_ptr->denoise.diswei[8]=239;
	sensor_ptr->denoise.diswei[9]=237;
	sensor_ptr->denoise.diswei[10]=235;
	sensor_ptr->denoise.diswei[11]=234;
	sensor_ptr->denoise.diswei[12]=232;
	sensor_ptr->denoise.diswei[13]=230;
	sensor_ptr->denoise.diswei[14]=228;
	sensor_ptr->denoise.diswei[15]=226;
	sensor_ptr->denoise.diswei[16]=225;
	sensor_ptr->denoise.diswei[17]=223;
	sensor_ptr->denoise.diswei[18]=221;

	sensor_ptr->denoise.ranwei[0]=255;
	sensor_ptr->denoise.ranwei[1]=252;
	sensor_ptr->denoise.ranwei[2]=243;
	sensor_ptr->denoise.ranwei[3]=230;
	sensor_ptr->denoise.ranwei[4]=213;
	sensor_ptr->denoise.ranwei[5]=193;
	sensor_ptr->denoise.ranwei[6]=170;
	sensor_ptr->denoise.ranwei[7]=147;
	sensor_ptr->denoise.ranwei[8]=125;
	sensor_ptr->denoise.ranwei[9]=103;
	sensor_ptr->denoise.ranwei[10]=83;
	sensor_ptr->denoise.ranwei[11]=66;
	sensor_ptr->denoise.ranwei[12]=51;
	sensor_ptr->denoise.ranwei[13]=38;
	sensor_ptr->denoise.ranwei[14]=28;
	sensor_ptr->denoise.ranwei[15]=20;
	sensor_ptr->denoise.ranwei[16]=14;
	sensor_ptr->denoise.ranwei[17]=10;
	sensor_ptr->denoise.ranwei[18]=6;
	sensor_ptr->denoise.ranwei[19]=4;
	sensor_ptr->denoise.ranwei[20]=2;
	sensor_ptr->denoise.ranwei[21]=1;
	sensor_ptr->denoise.ranwei[22]=0;
	sensor_ptr->denoise.ranwei[23]=0;
	sensor_ptr->denoise.ranwei[24]=0;
	sensor_ptr->denoise.ranwei[25]=0;
	sensor_ptr->denoise.ranwei[26]=0;
	sensor_ptr->denoise.ranwei[27]=0;
	sensor_ptr->denoise.ranwei[28]=0;
	sensor_ptr->denoise.ranwei[29]=0;
	sensor_ptr->denoise.ranwei[30]=0;

	//GrGb
	sensor_ptr->grgb.edge_thr=26;
	sensor_ptr->grgb.diff_thr=80;

	//cfa
	sensor_ptr->cfa.edge_thr=0x1a;
	sensor_ptr->cfa.diff_thr=0x00;

	//cmc
	sensor_ptr->cmc.matrix[0][0]=0x6f3;
	sensor_ptr->cmc.matrix[0][1]=0x3e0a;
	sensor_ptr->cmc.matrix[0][2]=0x3f03;
	sensor_ptr->cmc.matrix[0][3]=0x3ec0;
	sensor_ptr->cmc.matrix[0][4]=0x693;
	sensor_ptr->cmc.matrix[0][5]=0x3eae;
	sensor_ptr->cmc.matrix[0][6]=0x0d;
	sensor_ptr->cmc.matrix[0][7]=0x3c03;
	sensor_ptr->cmc.matrix[0][8]=0x7f0;

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
	sensor_ptr->gamma.axis[1][4]=0x13;
	sensor_ptr->gamma.axis[1][5]=0x1f;
	sensor_ptr->gamma.axis[1][6]=0x2a;
	sensor_ptr->gamma.axis[1][7]=0x36;
	sensor_ptr->gamma.axis[1][8]=0x40;
	sensor_ptr->gamma.axis[1][9]=0x58;
	sensor_ptr->gamma.axis[1][10]=0x68;
	sensor_ptr->gamma.axis[1][11]=0x76;
	sensor_ptr->gamma.axis[1][12]=0x84;
	sensor_ptr->gamma.axis[1][13]=0x8f;
	sensor_ptr->gamma.axis[1][14]=0x98;
	sensor_ptr->gamma.axis[1][15]=0xa0;
	sensor_ptr->gamma.axis[1][16]=0xb0;
	sensor_ptr->gamma.axis[1][17]=0xbd;
	sensor_ptr->gamma.axis[1][18]=0xc6;
	sensor_ptr->gamma.axis[1][19]=0xcf;
	sensor_ptr->gamma.axis[1][20]=0xd8;
	sensor_ptr->gamma.axis[1][21]=0xe4;
	sensor_ptr->gamma.axis[1][22]=0xea;
	sensor_ptr->gamma.axis[1][23]=0xf0;
	sensor_ptr->gamma.axis[1][24]=0xf6;
	sensor_ptr->gamma.axis[1][25]=0xff;

	sensor_ptr->gamma.tab[0].axis[0][0]=0;
	sensor_ptr->gamma.tab[0].axis[0][1]=8;
	sensor_ptr->gamma.tab[0].axis[0][2]=16;
	sensor_ptr->gamma.tab[0].axis[0][3]=24;
	sensor_ptr->gamma.tab[0].axis[0][4]=32;
	sensor_ptr->gamma.tab[0].axis[0][5]=48;
	sensor_ptr->gamma.tab[0].axis[0][6]=64;
	sensor_ptr->gamma.tab[0].axis[0][7]=80;
	sensor_ptr->gamma.tab[0].axis[0][8]=96;
	sensor_ptr->gamma.tab[0].axis[0][9]=128;
	sensor_ptr->gamma.tab[0].axis[0][10]=160;
	sensor_ptr->gamma.tab[0].axis[0][11]=192;
	sensor_ptr->gamma.tab[0].axis[0][12]=224;
	sensor_ptr->gamma.tab[0].axis[0][13]=256;
	sensor_ptr->gamma.tab[0].axis[0][14]=288;
	sensor_ptr->gamma.tab[0].axis[0][15]=320;
	sensor_ptr->gamma.tab[0].axis[0][16]=384;
	sensor_ptr->gamma.tab[0].axis[0][17]=448;
	sensor_ptr->gamma.tab[0].axis[0][18]=512;
	sensor_ptr->gamma.tab[0].axis[0][19]=576;
	sensor_ptr->gamma.tab[0].axis[0][20]=640;
	sensor_ptr->gamma.tab[0].axis[0][21]=768;
	sensor_ptr->gamma.tab[0].axis[0][22]=832;
	sensor_ptr->gamma.tab[0].axis[0][23]=896;
	sensor_ptr->gamma.tab[0].axis[0][24]=960;
	sensor_ptr->gamma.tab[0].axis[0][25]=1023;

	sensor_ptr->gamma.tab[0].axis[1][0]=0x00;
	sensor_ptr->gamma.tab[0].axis[1][1]=0x05;
	sensor_ptr->gamma.tab[0].axis[1][2]=0x09;
	sensor_ptr->gamma.tab[0].axis[1][3]=0x0e;
	sensor_ptr->gamma.tab[0].axis[1][4]=0x13;
	sensor_ptr->gamma.tab[0].axis[1][5]=0x1f;
	sensor_ptr->gamma.tab[0].axis[1][6]=0x2a;
	sensor_ptr->gamma.tab[0].axis[1][7]=0x36;
	sensor_ptr->gamma.tab[0].axis[1][8]=0x40;
	sensor_ptr->gamma.tab[0].axis[1][9]=0x58;
	sensor_ptr->gamma.tab[0].axis[1][10]=0x68;
	sensor_ptr->gamma.tab[0].axis[1][11]=0x76;
	sensor_ptr->gamma.tab[0].axis[1][12]=0x84;
	sensor_ptr->gamma.tab[0].axis[1][13]=0x8f;
	sensor_ptr->gamma.tab[0].axis[1][14]=0x98;
	sensor_ptr->gamma.tab[0].axis[1][15]=0xa0;
	sensor_ptr->gamma.tab[0].axis[1][16]=0xb0;
	sensor_ptr->gamma.tab[0].axis[1][17]=0xbd;
	sensor_ptr->gamma.tab[0].axis[1][18]=0xc6;
	sensor_ptr->gamma.tab[0].axis[1][19]=0xcf;
	sensor_ptr->gamma.tab[0].axis[1][20]=0xd8;
	sensor_ptr->gamma.tab[0].axis[1][21]=0xe4;
	sensor_ptr->gamma.tab[0].axis[1][22]=0xea;
	sensor_ptr->gamma.tab[0].axis[1][23]=0xf0;
	sensor_ptr->gamma.tab[0].axis[1][24]=0xf6;
	sensor_ptr->gamma.tab[0].axis[1][25]=0xff;

	sensor_ptr->gamma.tab[1].axis[0][0]=0;
	sensor_ptr->gamma.tab[1].axis[0][1]=8;
	sensor_ptr->gamma.tab[1].axis[0][2]=16;
	sensor_ptr->gamma.tab[1].axis[0][3]=24;
	sensor_ptr->gamma.tab[1].axis[0][4]=32;
	sensor_ptr->gamma.tab[1].axis[0][5]=48;
	sensor_ptr->gamma.tab[1].axis[0][6]=64;
	sensor_ptr->gamma.tab[1].axis[0][7]=80;
	sensor_ptr->gamma.tab[1].axis[0][8]=96;
	sensor_ptr->gamma.tab[1].axis[0][9]=128;
	sensor_ptr->gamma.tab[1].axis[0][10]=160;
	sensor_ptr->gamma.tab[1].axis[0][11]=192;
	sensor_ptr->gamma.tab[1].axis[0][12]=224;
	sensor_ptr->gamma.tab[1].axis[0][13]=256;
	sensor_ptr->gamma.tab[1].axis[0][14]=288;
	sensor_ptr->gamma.tab[1].axis[0][15]=320;
	sensor_ptr->gamma.tab[1].axis[0][16]=384;
	sensor_ptr->gamma.tab[1].axis[0][17]=448;
	sensor_ptr->gamma.tab[1].axis[0][18]=512;
	sensor_ptr->gamma.tab[1].axis[0][19]=576;
	sensor_ptr->gamma.tab[1].axis[0][20]=640;
	sensor_ptr->gamma.tab[1].axis[0][21]=768;
	sensor_ptr->gamma.tab[1].axis[0][22]=832;
	sensor_ptr->gamma.tab[1].axis[0][23]=896;
	sensor_ptr->gamma.tab[1].axis[0][24]=960;
	sensor_ptr->gamma.tab[1].axis[0][25]=1023;

	sensor_ptr->gamma.tab[1].axis[1][0]=0x00;
	sensor_ptr->gamma.tab[1].axis[1][1]=0x05;
	sensor_ptr->gamma.tab[1].axis[1][2]=0x09;
	sensor_ptr->gamma.tab[1].axis[1][3]=0x0e;
	sensor_ptr->gamma.tab[1].axis[1][4]=0x13;
	sensor_ptr->gamma.tab[1].axis[1][5]=0x1f;
	sensor_ptr->gamma.tab[1].axis[1][6]=0x2a;
	sensor_ptr->gamma.tab[1].axis[1][7]=0x36;
	sensor_ptr->gamma.tab[1].axis[1][8]=0x40;
	sensor_ptr->gamma.tab[1].axis[1][9]=0x58;
	sensor_ptr->gamma.tab[1].axis[1][10]=0x68;
	sensor_ptr->gamma.tab[1].axis[1][11]=0x76;
	sensor_ptr->gamma.tab[1].axis[1][12]=0x84;
	sensor_ptr->gamma.tab[1].axis[1][13]=0x8f;
	sensor_ptr->gamma.tab[1].axis[1][14]=0x98;
	sensor_ptr->gamma.tab[1].axis[1][15]=0xa0;
	sensor_ptr->gamma.tab[1].axis[1][16]=0xb0;
	sensor_ptr->gamma.tab[1].axis[1][17]=0xbd;
	sensor_ptr->gamma.tab[1].axis[1][18]=0xc6;
	sensor_ptr->gamma.tab[1].axis[1][19]=0xcf;
	sensor_ptr->gamma.tab[1].axis[1][20]=0xd8;
	sensor_ptr->gamma.tab[1].axis[1][21]=0xe4;
	sensor_ptr->gamma.tab[1].axis[1][22]=0xea;
	sensor_ptr->gamma.tab[1].axis[1][23]=0xf0;
	sensor_ptr->gamma.tab[1].axis[1][24]=0xf6;
	sensor_ptr->gamma.tab[1].axis[1][25]=0xff;

	sensor_ptr->gamma.tab[2].axis[0][0]=0;
	sensor_ptr->gamma.tab[2].axis[0][1]=8;
	sensor_ptr->gamma.tab[2].axis[0][2]=16;
	sensor_ptr->gamma.tab[2].axis[0][3]=24;
	sensor_ptr->gamma.tab[2].axis[0][4]=32;
	sensor_ptr->gamma.tab[2].axis[0][5]=48;
	sensor_ptr->gamma.tab[2].axis[0][6]=64;
	sensor_ptr->gamma.tab[2].axis[0][7]=80;
	sensor_ptr->gamma.tab[2].axis[0][8]=96;
	sensor_ptr->gamma.tab[2].axis[0][9]=128;
	sensor_ptr->gamma.tab[2].axis[0][10]=160;
	sensor_ptr->gamma.tab[2].axis[0][11]=192;
	sensor_ptr->gamma.tab[2].axis[0][12]=224;
	sensor_ptr->gamma.tab[2].axis[0][13]=256;
	sensor_ptr->gamma.tab[2].axis[0][14]=288;
	sensor_ptr->gamma.tab[2].axis[0][15]=320;
	sensor_ptr->gamma.tab[2].axis[0][16]=384;
	sensor_ptr->gamma.tab[2].axis[0][17]=448;
	sensor_ptr->gamma.tab[2].axis[0][18]=512;
	sensor_ptr->gamma.tab[2].axis[0][19]=576;
	sensor_ptr->gamma.tab[2].axis[0][20]=640;
	sensor_ptr->gamma.tab[2].axis[0][21]=768;
	sensor_ptr->gamma.tab[2].axis[0][22]=832;
	sensor_ptr->gamma.tab[2].axis[0][23]=896;
	sensor_ptr->gamma.tab[2].axis[0][24]=960;
	sensor_ptr->gamma.tab[2].axis[0][25]=1023;

	sensor_ptr->gamma.tab[2].axis[1][0]=0x00;
	sensor_ptr->gamma.tab[2].axis[1][1]=0x05;
	sensor_ptr->gamma.tab[2].axis[1][2]=0x09;
	sensor_ptr->gamma.tab[2].axis[1][3]=0x0e;
	sensor_ptr->gamma.tab[2].axis[1][4]=0x13;
	sensor_ptr->gamma.tab[2].axis[1][5]=0x1f;
	sensor_ptr->gamma.tab[2].axis[1][6]=0x2a;
	sensor_ptr->gamma.tab[2].axis[1][7]=0x36;
	sensor_ptr->gamma.tab[2].axis[1][8]=0x40;
	sensor_ptr->gamma.tab[2].axis[1][9]=0x58;
	sensor_ptr->gamma.tab[2].axis[1][10]=0x68;
	sensor_ptr->gamma.tab[2].axis[1][11]=0x76;
	sensor_ptr->gamma.tab[2].axis[1][12]=0x84;
	sensor_ptr->gamma.tab[2].axis[1][13]=0x8f;
	sensor_ptr->gamma.tab[2].axis[1][14]=0x98;
	sensor_ptr->gamma.tab[2].axis[1][15]=0xa0;
	sensor_ptr->gamma.tab[2].axis[1][16]=0xb0;
	sensor_ptr->gamma.tab[2].axis[1][17]=0xbd;
	sensor_ptr->gamma.tab[2].axis[1][18]=0xc6;
	sensor_ptr->gamma.tab[2].axis[1][19]=0xcf;
	sensor_ptr->gamma.tab[2].axis[1][20]=0xd8;
	sensor_ptr->gamma.tab[2].axis[1][21]=0xe4;
	sensor_ptr->gamma.tab[2].axis[1][22]=0xea;
	sensor_ptr->gamma.tab[2].axis[1][23]=0xf0;
	sensor_ptr->gamma.tab[2].axis[1][24]=0xf6;
	sensor_ptr->gamma.tab[2].axis[1][25]=0xff;

	sensor_ptr->gamma.tab[3].axis[0][0]=0;
	sensor_ptr->gamma.tab[3].axis[0][1]=8;
	sensor_ptr->gamma.tab[3].axis[0][2]=16;
	sensor_ptr->gamma.tab[3].axis[0][3]=24;
	sensor_ptr->gamma.tab[3].axis[0][4]=32;
	sensor_ptr->gamma.tab[3].axis[0][5]=48;
	sensor_ptr->gamma.tab[3].axis[0][6]=64;
	sensor_ptr->gamma.tab[3].axis[0][7]=80;
	sensor_ptr->gamma.tab[3].axis[0][8]=96;
	sensor_ptr->gamma.tab[3].axis[0][9]=128;
	sensor_ptr->gamma.tab[3].axis[0][10]=160;
	sensor_ptr->gamma.tab[3].axis[0][11]=192;
	sensor_ptr->gamma.tab[3].axis[0][12]=224;
	sensor_ptr->gamma.tab[3].axis[0][13]=256;
	sensor_ptr->gamma.tab[3].axis[0][14]=288;
	sensor_ptr->gamma.tab[3].axis[0][15]=320;
	sensor_ptr->gamma.tab[3].axis[0][16]=384;
	sensor_ptr->gamma.tab[3].axis[0][17]=448;
	sensor_ptr->gamma.tab[3].axis[0][18]=512;
	sensor_ptr->gamma.tab[3].axis[0][19]=576;
	sensor_ptr->gamma.tab[3].axis[0][20]=640;
	sensor_ptr->gamma.tab[3].axis[0][21]=768;
	sensor_ptr->gamma.tab[3].axis[0][22]=832;
	sensor_ptr->gamma.tab[3].axis[0][23]=896;
	sensor_ptr->gamma.tab[3].axis[0][24]=960;
	sensor_ptr->gamma.tab[3].axis[0][25]=1023;

	sensor_ptr->gamma.tab[3].axis[1][0]=0x00;
	sensor_ptr->gamma.tab[3].axis[1][1]=0x05;
	sensor_ptr->gamma.tab[3].axis[1][2]=0x09;
	sensor_ptr->gamma.tab[3].axis[1][3]=0x0e;
	sensor_ptr->gamma.tab[3].axis[1][4]=0x13;
	sensor_ptr->gamma.tab[3].axis[1][5]=0x1f;
	sensor_ptr->gamma.tab[3].axis[1][6]=0x2a;
	sensor_ptr->gamma.tab[3].axis[1][7]=0x36;
	sensor_ptr->gamma.tab[3].axis[1][8]=0x40;
	sensor_ptr->gamma.tab[3].axis[1][9]=0x58;
	sensor_ptr->gamma.tab[3].axis[1][10]=0x68;
	sensor_ptr->gamma.tab[3].axis[1][11]=0x76;
	sensor_ptr->gamma.tab[3].axis[1][12]=0x84;
	sensor_ptr->gamma.tab[3].axis[1][13]=0x8f;
	sensor_ptr->gamma.tab[3].axis[1][14]=0x98;
	sensor_ptr->gamma.tab[3].axis[1][15]=0xa0;
	sensor_ptr->gamma.tab[3].axis[1][16]=0xb0;
	sensor_ptr->gamma.tab[3].axis[1][17]=0xbd;
	sensor_ptr->gamma.tab[3].axis[1][18]=0xc6;
	sensor_ptr->gamma.tab[3].axis[1][19]=0xcf;
	sensor_ptr->gamma.tab[3].axis[1][20]=0xd8;
	sensor_ptr->gamma.tab[3].axis[1][21]=0xe4;
	sensor_ptr->gamma.tab[3].axis[1][22]=0xea;
	sensor_ptr->gamma.tab[3].axis[1][23]=0xf0;
	sensor_ptr->gamma.tab[3].axis[1][24]=0xf6;
	sensor_ptr->gamma.tab[3].axis[1][25]=0xff;

	sensor_ptr->gamma.tab[4].axis[0][0]=0;
	sensor_ptr->gamma.tab[4].axis[0][1]=8;
	sensor_ptr->gamma.tab[4].axis[0][2]=16;
	sensor_ptr->gamma.tab[4].axis[0][3]=24;
	sensor_ptr->gamma.tab[4].axis[0][4]=32;
	sensor_ptr->gamma.tab[4].axis[0][5]=48;
	sensor_ptr->gamma.tab[4].axis[0][6]=64;
	sensor_ptr->gamma.tab[4].axis[0][7]=80;
	sensor_ptr->gamma.tab[4].axis[0][8]=96;
	sensor_ptr->gamma.tab[4].axis[0][9]=128;
	sensor_ptr->gamma.tab[4].axis[0][10]=160;
	sensor_ptr->gamma.tab[4].axis[0][11]=192;
	sensor_ptr->gamma.tab[4].axis[0][12]=224;
	sensor_ptr->gamma.tab[4].axis[0][13]=256;
	sensor_ptr->gamma.tab[4].axis[0][14]=288;
	sensor_ptr->gamma.tab[4].axis[0][15]=320;
	sensor_ptr->gamma.tab[4].axis[0][16]=384;
	sensor_ptr->gamma.tab[4].axis[0][17]=448;
	sensor_ptr->gamma.tab[4].axis[0][18]=512;
	sensor_ptr->gamma.tab[4].axis[0][19]=576;
	sensor_ptr->gamma.tab[4].axis[0][20]=640;
	sensor_ptr->gamma.tab[4].axis[0][21]=768;
	sensor_ptr->gamma.tab[4].axis[0][22]=832;
	sensor_ptr->gamma.tab[4].axis[0][23]=896;
	sensor_ptr->gamma.tab[4].axis[0][24]=960;
	sensor_ptr->gamma.tab[4].axis[0][25]=1023;

	sensor_ptr->gamma.tab[4].axis[1][0]=0x00;
	sensor_ptr->gamma.tab[4].axis[1][1]=0x05;
	sensor_ptr->gamma.tab[4].axis[1][2]=0x09;
	sensor_ptr->gamma.tab[4].axis[1][3]=0x0e;
	sensor_ptr->gamma.tab[4].axis[1][4]=0x13;
	sensor_ptr->gamma.tab[4].axis[1][5]=0x1f;
	sensor_ptr->gamma.tab[4].axis[1][6]=0x2a;
	sensor_ptr->gamma.tab[4].axis[1][7]=0x36;
	sensor_ptr->gamma.tab[4].axis[1][8]=0x40;
	sensor_ptr->gamma.tab[4].axis[1][9]=0x58;
	sensor_ptr->gamma.tab[4].axis[1][10]=0x68;
	sensor_ptr->gamma.tab[4].axis[1][11]=0x76;
	sensor_ptr->gamma.tab[4].axis[1][12]=0x84;
	sensor_ptr->gamma.tab[4].axis[1][13]=0x8f;
	sensor_ptr->gamma.tab[4].axis[1][14]=0x98;
	sensor_ptr->gamma.tab[4].axis[1][15]=0xa0;
	sensor_ptr->gamma.tab[4].axis[1][16]=0xb0;
	sensor_ptr->gamma.tab[4].axis[1][17]=0xbd;
	sensor_ptr->gamma.tab[4].axis[1][18]=0xc6;
	sensor_ptr->gamma.tab[4].axis[1][19]=0xcf;
	sensor_ptr->gamma.tab[4].axis[1][20]=0xd8;
	sensor_ptr->gamma.tab[4].axis[1][21]=0xe4;
	sensor_ptr->gamma.tab[4].axis[1][22]=0xea;
	sensor_ptr->gamma.tab[4].axis[1][23]=0xf0;
	sensor_ptr->gamma.tab[4].axis[1][24]=0xf6;
	sensor_ptr->gamma.tab[4].axis[1][25]=0xff;

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
	sensor_ptr->saturation.factor[0]=0x28;
	sensor_ptr->saturation.factor[1]=0x30;
	sensor_ptr->saturation.factor[2]=0x38;
	sensor_ptr->saturation.factor[3]=0x40;
	sensor_ptr->saturation.factor[4]=0x48;
	sensor_ptr->saturation.factor[5]=0x50;
	sensor_ptr->saturation.factor[6]=0x58;
	sensor_ptr->saturation.factor[7]=0x40;
	sensor_ptr->saturation.factor[8]=0x40;
	sensor_ptr->saturation.factor[9]=0x40;
	sensor_ptr->saturation.factor[10]=0x40;
	sensor_ptr->saturation.factor[11]=0x40;
	sensor_ptr->saturation.factor[12]=0x40;
	sensor_ptr->saturation.factor[13]=0x40;
	sensor_ptr->saturation.factor[14]=0x40;
	sensor_ptr->saturation.factor[15]=0x40;

	//css
	sensor_ptr->css.lum_thr=255;
	sensor_ptr->css.chr_thr=2;
	sensor_ptr->css.low_thr[0]=3;
	sensor_ptr->css.low_thr[1]=4;
	sensor_ptr->css.low_thr[2]=5;
	sensor_ptr->css.low_thr[3]=6;
	sensor_ptr->css.low_thr[4]=7;
	sensor_ptr->css.low_thr[5]=8;
	sensor_ptr->css.low_thr[6]=9;
	sensor_ptr->css.low_sum_thr[0]=6;
	sensor_ptr->css.low_sum_thr[1]=8;
	sensor_ptr->css.low_sum_thr[2]=10;
	sensor_ptr->css.low_sum_thr[3]=12;
	sensor_ptr->css.low_sum_thr[4]=14;
	sensor_ptr->css.low_sum_thr[5]=16;
	sensor_ptr->css.low_sum_thr[6]=18;

	//af info
	sensor_ptr->af.max_step=0x3ff;
	sensor_ptr->af.min_step=0;
	sensor_ptr->af.max_tune_step=0;
	sensor_ptr->af.stab_period=120;
	sensor_ptr->af.alg_id=3;
	sensor_ptr->af.rough_count=12;
	sensor_ptr->af.af_rough_step[0]=320;
	sensor_ptr->af.af_rough_step[2]=384;
	sensor_ptr->af.af_rough_step[3]=448;
	sensor_ptr->af.af_rough_step[4]=512;
	sensor_ptr->af.af_rough_step[5]=576;
	sensor_ptr->af.af_rough_step[6]=640;
	sensor_ptr->af.af_rough_step[7]=704;
	sensor_ptr->af.af_rough_step[8]=768;
	sensor_ptr->af.af_rough_step[9]=832;
	sensor_ptr->af.af_rough_step[10]=896;
	sensor_ptr->af.af_rough_step[11]=960;
	sensor_ptr->af.af_rough_step[12]=1023;
	sensor_ptr->af.fine_count=4;

	//edge
	sensor_ptr->edge.info[0].detail_thr=0x00;
	sensor_ptr->edge.info[0].smooth_thr=0x30;
	sensor_ptr->edge.info[0].strength=0;
	sensor_ptr->edge.info[1].detail_thr=0x01;
	sensor_ptr->edge.info[1].smooth_thr=0x20;
	sensor_ptr->edge.info[1].strength=3;
	sensor_ptr->edge.info[2].detail_thr=0x2;
	sensor_ptr->edge.info[2].smooth_thr=0x10;
	sensor_ptr->edge.info[2].strength=5;
	sensor_ptr->edge.info[3].detail_thr=0x03;
	sensor_ptr->edge.info[3].smooth_thr=0x05;
	sensor_ptr->edge.info[3].strength=10;
	sensor_ptr->edge.info[4].detail_thr=0x06;
	sensor_ptr->edge.info[4].smooth_thr=0x05;
	sensor_ptr->edge.info[4].strength=20;
	sensor_ptr->edge.info[5].detail_thr=0x09;
	sensor_ptr->edge.info[5].smooth_thr=0x05;
	sensor_ptr->edge.info[5].strength=30;
	sensor_ptr->edge.info[6].detail_thr=0x0c;
	sensor_ptr->edge.info[6].smooth_thr=0x05;
	sensor_ptr->edge.info[6].strength=40;

	//emboss
	sensor_ptr->emboss.step=0x02;

	//global gain
	sensor_ptr->global.gain=0x40;

	//chn gain
	sensor_ptr->chn.r_gain=0x40;
	sensor_ptr->chn.g_gain=0x40;
	sensor_ptr->chn.b_gain=0x40;
	sensor_ptr->chn.r_offset=0x00;
	sensor_ptr->chn.r_offset=0x00;
	sensor_ptr->chn.r_offset=0x00;

	sensor_ptr->edge.info[0].detail_thr=0x00;
	sensor_ptr->edge.info[0].smooth_thr=0x30;
	sensor_ptr->edge.info[0].strength=0;
	sensor_ptr->edge.info[1].detail_thr=0x01;
	sensor_ptr->edge.info[1].smooth_thr=0x20;
	sensor_ptr->edge.info[1].strength=3;
	sensor_ptr->edge.info[2].detail_thr=0x2;
	sensor_ptr->edge.info[2].smooth_thr=0x10;
	sensor_ptr->edge.info[2].strength=5;
	sensor_ptr->edge.info[3].detail_thr=0x03;
	sensor_ptr->edge.info[3].smooth_thr=0x05;
	sensor_ptr->edge.info[3].strength=10;
	sensor_ptr->edge.info[4].detail_thr=0x06;
	sensor_ptr->edge.info[4].smooth_thr=0x05;
	sensor_ptr->edge.info[4].strength=20;
	sensor_ptr->edge.info[5].detail_thr=0x09;
	sensor_ptr->edge.info[5].smooth_thr=0x05;
	sensor_ptr->edge.info[5].strength=30;
	sensor_ptr->edge.info[6].detail_thr=0x0c;
	sensor_ptr->edge.info[6].smooth_thr=0x05;
	sensor_ptr->edge.info[6].strength=40;
	sensor_ptr->edge.info[7].detail_thr=0x0f;
	sensor_ptr->edge.info[7].smooth_thr=0x05;
	sensor_ptr->edge.info[7].strength=60;

	/*normal*/
	sensor_ptr->special_effect[0].matrix[0]=0x004d;
	sensor_ptr->special_effect[0].matrix[1]=0x0096;
	sensor_ptr->special_effect[0].matrix[2]=0x001d;
	sensor_ptr->special_effect[0].matrix[3]=0xffd5;
	sensor_ptr->special_effect[0].matrix[4]=0xffab;
	sensor_ptr->special_effect[0].matrix[5]=0x0080;
	sensor_ptr->special_effect[0].matrix[6]=0x0080;
	sensor_ptr->special_effect[0].matrix[7]=0xff95;
	sensor_ptr->special_effect[0].matrix[8]=0xffeb;
	sensor_ptr->special_effect[0].y_shift=0xff00;
	sensor_ptr->special_effect[0].u_shift=0x0000;
	sensor_ptr->special_effect[0].v_shift=0x0000;

	/*gray*/
	sensor_ptr->special_effect[1].matrix[0]=0x004d;
	sensor_ptr->special_effect[1].matrix[1]=0x0096;
	sensor_ptr->special_effect[1].matrix[2]=0x001d;
	sensor_ptr->special_effect[1].matrix[3]=0x0000;
	sensor_ptr->special_effect[1].matrix[4]=0x0000;
	sensor_ptr->special_effect[1].matrix[5]=0x0000;
	sensor_ptr->special_effect[1].matrix[6]=0x0000;
	sensor_ptr->special_effect[1].matrix[7]=0x0000;
	sensor_ptr->special_effect[1].matrix[8]=0x0000;
	sensor_ptr->special_effect[1].y_shift=0xff00;
	sensor_ptr->special_effect[1].u_shift=0x0000;
	sensor_ptr->special_effect[1].v_shift=0x0000;
	/*warm*/
	sensor_ptr->special_effect[2].matrix[0]=0x004d;
	sensor_ptr->special_effect[2].matrix[1]=0x0096;
	sensor_ptr->special_effect[2].matrix[2]=0x001d;
	sensor_ptr->special_effect[2].matrix[3]=0xffd5;
	sensor_ptr->special_effect[2].matrix[4]=0xffab;
	sensor_ptr->special_effect[2].matrix[5]=0x0080;
	sensor_ptr->special_effect[2].matrix[6]=0x0080;
	sensor_ptr->special_effect[2].matrix[7]=0xff95;
	sensor_ptr->special_effect[2].matrix[8]=0xffeb;
	sensor_ptr->special_effect[2].y_shift=0xff00;
	sensor_ptr->special_effect[2].u_shift=0xffd4;
	sensor_ptr->special_effect[2].v_shift=0x0080;
	/*green*/
	sensor_ptr->special_effect[3].matrix[0]=0x004d;
	sensor_ptr->special_effect[3].matrix[1]=0x0096;
	sensor_ptr->special_effect[3].matrix[2]=0x001d;
	sensor_ptr->special_effect[3].matrix[3]=0xffd5;
	sensor_ptr->special_effect[3].matrix[4]=0xffab;
	sensor_ptr->special_effect[3].matrix[5]=0x0080;
	sensor_ptr->special_effect[3].matrix[6]=0x0080;
	sensor_ptr->special_effect[3].matrix[7]=0xff95;
	sensor_ptr->special_effect[3].matrix[8]=0xffeb;
	sensor_ptr->special_effect[3].y_shift=0xff00;
	sensor_ptr->special_effect[3].u_shift=0xffd5;
	sensor_ptr->special_effect[3].v_shift=0xffca;
	/*cool*/
	sensor_ptr->special_effect[4].matrix[0]=0x004d;
	sensor_ptr->special_effect[4].matrix[1]=0x0096;
	sensor_ptr->special_effect[4].matrix[2]=0x001d;
	sensor_ptr->special_effect[4].matrix[3]=0xffd5;
	sensor_ptr->special_effect[4].matrix[4]=0xffab;
	sensor_ptr->special_effect[4].matrix[5]=0x0080;
	sensor_ptr->special_effect[4].matrix[6]=0x0080;
	sensor_ptr->special_effect[4].matrix[7]=0xff95;
	sensor_ptr->special_effect[4].matrix[8]=0xffeb;
	sensor_ptr->special_effect[4].y_shift=0xff00;
	sensor_ptr->special_effect[4].u_shift=0x0040;
	sensor_ptr->special_effect[4].v_shift=0x000a;
	/*orange*/
	sensor_ptr->special_effect[5].matrix[0]=0x004d;
	sensor_ptr->special_effect[5].matrix[1]=0x0096;
	sensor_ptr->special_effect[5].matrix[2]=0x001d;
	sensor_ptr->special_effect[5].matrix[3]=0xffd5;
	sensor_ptr->special_effect[5].matrix[4]=0xffab;
	sensor_ptr->special_effect[5].matrix[5]=0x0080;
	sensor_ptr->special_effect[5].matrix[6]=0x0080;
	sensor_ptr->special_effect[5].matrix[7]=0xff95;
	sensor_ptr->special_effect[5].matrix[8]=0xffeb;
	sensor_ptr->special_effect[5].y_shift=0xff00;
	sensor_ptr->special_effect[5].u_shift=0xff00;
	sensor_ptr->special_effect[5].v_shift=0x0028;
	/*negtive*/
	sensor_ptr->special_effect[6].matrix[0]=0xffb3;
	sensor_ptr->special_effect[6].matrix[1]=0xff6a;
	sensor_ptr->special_effect[6].matrix[2]=0xffe3;
	sensor_ptr->special_effect[6].matrix[3]=0x002b;
	sensor_ptr->special_effect[6].matrix[4]=0x0055;
	sensor_ptr->special_effect[6].matrix[5]=0xff80;
	sensor_ptr->special_effect[6].matrix[6]=0xff80;
	sensor_ptr->special_effect[6].matrix[7]=0x006b;
	sensor_ptr->special_effect[6].matrix[8]=0x0015;
	sensor_ptr->special_effect[6].y_shift=0x00ff;
	sensor_ptr->special_effect[6].u_shift=0x0000;
	sensor_ptr->special_effect[6].v_shift=0x0000;
	/*old*/
	sensor_ptr->special_effect[7].matrix[0]=0x004d;
	sensor_ptr->special_effect[7].matrix[1]=0x0096;
	sensor_ptr->special_effect[7].matrix[2]=0x001d;
	sensor_ptr->special_effect[7].matrix[3]=0x0000;
	sensor_ptr->special_effect[7].matrix[4]=0x0000;
	sensor_ptr->special_effect[7].matrix[5]=0x0000;
	sensor_ptr->special_effect[7].matrix[6]=0x0000;
	sensor_ptr->special_effect[7].matrix[7]=0x0000;
	sensor_ptr->special_effect[7].matrix[8]=0x0000;
	sensor_ptr->special_effect[7].y_shift=0xff00;
	sensor_ptr->special_effect[7].u_shift=0xffe2;
	sensor_ptr->special_effect[7].v_shift=0x0028;
#endif
	return rtn;
}


LOCAL uint32_t _hi544_GetResolutionTrimTab(uint32_t param)
{
	SENSOR_PRINT("0x%x",  (uint32_t)s_hi544_Resolution_Trim_Tab);
	return (uint32_t) s_hi544_Resolution_Trim_Tab;
}
LOCAL uint32_t _hi544_PowerOn(uint32_t power_on)
{
	SENSOR_AVDD_VAL_E dvdd_val = g_hi544_mipi_raw_info.dvdd_val;
	SENSOR_AVDD_VAL_E avdd_val = g_hi544_mipi_raw_info.avdd_val;
	SENSOR_AVDD_VAL_E iovdd_val = g_hi544_mipi_raw_info.iovdd_val;
	BOOLEAN power_down = g_hi544_mipi_raw_info.power_down_level;
	BOOLEAN reset_level = g_hi544_mipi_raw_info.reset_pulse_level;
	//uint32_t reset_width=g_hi544_yuv_info.reset_pulse_width;

	if (SENSOR_TRUE == power_on) {
		//set all power pin to disable status
		Sensor_SetMCLK(SENSOR_DISABLE_MCLK);
		Sensor_SetMonitorVoltage(SENSOR_AVDD_CLOSED);
		Sensor_SetVoltage(SENSOR_AVDD_CLOSED, SENSOR_AVDD_CLOSED, SENSOR_AVDD_CLOSED);
		Sensor_SetResetLevel(reset_level);
		Sensor_PowerDown(power_down);
		usleep(20*1000);
		//step 0 power up DOVDD, the AVDD
		Sensor_SetMonitorVoltage(SENSOR_AVDD_3300MV);
		#if AF_DRIVER_DW9806
			_dw9806_SRCInit(2);
		#else
			_dw9174_SRCInit(2);
		#endif
		Sensor_SetIovddVoltage(iovdd_val);
		usleep(2000);
		Sensor_SetAvddVoltage(avdd_val);
		usleep(2000);
		//step 1 power up DVDD
		Sensor_SetDvddVoltage(dvdd_val);
		usleep(2000);
		//step 2 power down pin high
		Sensor_PowerDown(!power_down);
		usleep(2000);
		//step 4 xvclk
		Sensor_SetMCLK(SENSOR_DEFALUT_MCLK);
		usleep(15*1000);
		//step 3 reset pin high
		Sensor_SetResetLevel(!reset_level);
		usleep(700*1000);
	} else {
		usleep(4*1000);
		Sensor_SetResetLevel(reset_level);
		usleep(12*1000);
		Sensor_SetMCLK(SENSOR_DISABLE_MCLK);
		usleep(2000);
		Sensor_PowerDown(power_down);
		usleep(2000);
		Sensor_SetDvddVoltage(SENSOR_AVDD_CLOSED);
		usleep(2000);
		Sensor_SetAvddVoltage(SENSOR_AVDD_CLOSED);
		Sensor_SetIovddVoltage(SENSOR_AVDD_CLOSED);
		Sensor_SetMonitorVoltage(SENSOR_AVDD_CLOSED);
	}
	SENSOR_PRINT("SENSOR_hi544: _hi544_Power_On(1:on, 0:off): %d", power_on);
	return SENSOR_SUCCESS;
}

LOCAL uint32_t _hi544_cfg_otp(uint32_t param)
{
	uint32_t rtn=SENSOR_SUCCESS;
	struct raw_param_info_tab* tab_ptr = (struct raw_param_info_tab*)s_hi544_raw_param_tab;
	uint32_t module_id=g_hi544_module_id;

	SENSOR_PRINT("SENSOR_hi544: _hi544_cfg_otp module_id:0x%x", module_id);

	/*be called in sensor thread, so not call Sensor_SetMode_WaitDone()*/
	usleep(10 * 1000);

	if (PNULL!=tab_ptr[module_id].cfg_otp) {
		tab_ptr[module_id].cfg_otp(0);
	}

	/* do streamoff, and not sleep
	_hi544_StreamOff(0);
	*/
	Sensor_WriteReg(0x0100, 0x00);

	SENSOR_PRINT("SENSOR_hi544: _hi544_cfg_otp end");

	return rtn;
}

LOCAL uint32_t _hi544_com_Identify_otp(void* param_ptr)
{
	uint32_t rtn=SENSOR_FAIL;
	uint32_t param_id;

	SENSOR_PRINT("SENSOR_HI544: _hi544_com_Identify_otp");

	/*read param id from sensor omap*/
	param_id=hi544_RAW_PARAM_COM;

	if(hi544_RAW_PARAM_COM==param_id){
		rtn=SENSOR_SUCCESS;
	}

	return rtn;
}

LOCAL uint32_t _hi544_GetRawInof(void)
{
	uint32_t rtn=SENSOR_SUCCESS;
	struct raw_param_info_tab* tab_ptr = (struct raw_param_info_tab*)s_hi544_raw_param_tab;
	uint32_t i=0x00;
	uint16_t stream_value = 0;

	stream_value = Sensor_ReadReg(0x0100);
	if (1 != (stream_value & 0x01)) {
		Sensor_WriteReg(0x0100, 0x01);
		usleep(5 * 1000);
	}

	for(i=0x00; ; i++)
	{
		g_hi544_module_id = i;
		if(RAW_INFO_END_ID==tab_ptr[i].param_id){
			if(NULL==s_hi544_mipi_raw_info_ptr){
				SENSOR_PRINT("SENSOR_hi544: hi544_GetRawInof no param error");
				rtn=SENSOR_FAIL;
			}
			SENSOR_PRINT("SENSOR_hi544: hi544_GetRawInof end");
			break;
		}
		else if(PNULL!=tab_ptr[i].identify_otp){
			if(SENSOR_SUCCESS==tab_ptr[i].identify_otp(0))
			{
				s_hi544_mipi_raw_info_ptr = tab_ptr[i].info_ptr;
				SENSOR_PRINT("SENSOR_hi544: hi544_GetRawInof id:0x%x success", g_hi544_module_id);
				break;
			}
		}
	}

	if(1 != (stream_value & 0x01)) {
		Sensor_WriteReg(0x0100, stream_value);
		usleep(5 * 1000);
	}

	return rtn;
}

LOCAL uint32_t _hi544_GetMaxFrameLine(uint32_t index)
{
	uint32_t max_line=0x00;
	SENSOR_TRIM_T_PTR trim_ptr=s_hi544_Resolution_Trim_Tab;

	max_line=trim_ptr[index].frame_line;

	return max_line;
}

LOCAL uint32_t _hi544_Identify(uint32_t param)
{
#define hi544_PID_VALUE    0x4405
#define hi544_PID_ADDR     0x0f16

	uint16_t pid_value = 0x00;
	uint8_t ver_value = 0x00;
	uint32_t ret_value = SENSOR_FAIL;

	SENSOR_PRINT("SENSOR_HI544: mipi raw identify\n");

	pid_value = Sensor_ReadReg(hi544_PID_ADDR);
	if (hi544_PID_VALUE == pid_value) {
		SENSOR_PRINT("SENSOR_HI544: this is hi544 sensor !");
		ret_value=_hi544_GetRawInof();
		if(SENSOR_SUCCESS != ret_value)
		{
			SENSOR_PRINT("SENSOR_HI544: the module is unknow error !");
		}
		Sensor_hi544_InitRawTuneInfo();
	} else {
            SENSOR_PRINT("SENSOR_HI544: identify fail,pid_value=%d", pid_value);
            SENSOR_PRINT("SENSOR_HI544: slave id :0x%x(7bit)\n", Sensor_ReadReg(0x0f14));
	}

	return ret_value;
}

LOCAL uint32_t _hi544_write_exposure(uint32_t param)
{
	uint32_t ret_value = SENSOR_SUCCESS;
	uint16_t expsure_line=0x00;
	uint16_t dummy_line=0x00;
	uint16_t size_index=0x00;
	uint16_t frame_len=0x00;
	uint16_t frame_len_cur=0x00;
	uint16_t max_frame_len=0x00;
	uint16_t line_length = 0x00;
	uint16_t value=0x00;
	uint16_t value0=0x00;
	uint16_t value1=0x00;
	uint16_t value2=0x00;

	expsure_line=param&0xffff;
	dummy_line=(param>>0x10)&0x0fff;
	size_index=(param>>0x1c)&0x0f;

	SENSOR_PRINT("SENSOR_hi544: write_exposure line:%d, dummy:%d, size_index:%d", expsure_line, dummy_line, size_index);

	line_length = Sensor_ReadReg(0x0008);
	max_frame_len=_hi544_GetMaxFrameLine(size_index);

	if(0x00!=max_frame_len)
	{
		frame_len = ((expsure_line+4)> max_frame_len) ? (expsure_line+4) : max_frame_len;
		frame_len = (frame_len+1)>>1<<1;

		frame_len_cur = Sensor_ReadReg(0x0006);

		if(frame_len_cur != frame_len){
			ret_value = Sensor_WriteReg(0x0006, frame_len);
		}
	}

	value = expsure_line;
	SENSOR_PRINT("SENSOR_hi544:line_length = %d  coarse = %d\n", line_length, value);
	ret_value = Sensor_WriteReg(0x0046, 0x01);
	ret_value = Sensor_WriteReg(0x0004, value);

	return ret_value;
}

LOCAL uint32_t _hi544_write_gain(uint32_t param)
{
	uint32_t ret_value = SENSOR_SUCCESS;
	uint16_t value=0x00;
	uint32_t real_gain = 0;
	uint8_t bit_11_8 = 0;
	uint8_t bit_7_0 = 0;
	real_gain = ((param&0xf)+16)*(((param>>4)&0x01)+1)*(((param>>5)&0x01)+1)*(((param>>6)&0x01)+1)*(((param>>7)&0x01)+1);
	real_gain = real_gain*(((param>>8)&0x01)+1)*(((param>>9)&0x01)+1)*(((param>>10)&0x01)+1)*(((param>>11)&0x01)+1);

	SENSOR_PRINT("SENSOR_hi544: write_gain:0x%x, real_gain:0x%x\n", param, real_gain);

	value = real_gain - 16;
	ret_value = Sensor_WriteReg(0x003a, (value&0xff)<<8);  /* code */

	ret_value = Sensor_WriteReg(0x0046, 0x00);

	return ret_value;
}

LOCAL uint32_t _hi544_write_af(uint32_t param)
{
#if AF_DRIVER_DW9806
	uint32_t ret_value = SENSOR_SUCCESS;
	uint8_t cmd_val[2] = {0x00};
	uint16_t  slave_addr = 0;
	uint16_t cmd_len = 0;
	uint32_t time_out = 0;

	SENSOR_PRINT("SENSOR_hi544: _write_af %d", param);

	slave_addr = DW9806_VCM_SLAVE_ADDR;
	do{
		usleep(1000);
		cmd_val[0] = 0x05;
		ret_value = Sensor_ReadI2C(slave_addr,(uint8_t*)&cmd_val[0], 1);
		if(time_out++>200)
			break;

	}while(cmd_val[0]>0);
	cmd_len = 2;
	cmd_val[0] = 0x03;
	cmd_val[1] = (param&0x300)>>8;
	ret_value = Sensor_WriteI2C(slave_addr,(uint8_t*)&cmd_val[0], cmd_len);
	cmd_val[0] = 0x04;
	cmd_val[1] = (param&0xff);
	ret_value = Sensor_WriteI2C(slave_addr,(uint8_t*)&cmd_val[0], cmd_len);

	SENSOR_PRINT("SENSOR_hi544: _write_af, ret =  %d, MSL:%x, LSL:%x\n", ret_value, cmd_val[0], cmd_val[1]);

	return ret_value;
#else
	uint32_t ret_value = SENSOR_SUCCESS;
	uint8_t cmd_val[2] = {0x00};
	uint16_t  slave_addr = 0;
	uint16_t cmd_len = 0;

	SENSOR_PRINT("SENSOR_hi544: _write_af %d", param);

	slave_addr = DW9714_VCM_SLAVE_ADDR;
	cmd_val[0] = (param&0xfff0)>>4;
	cmd_val[1] = ((param&0x0f)<<4)|0x09;
	cmd_len = 2;
	ret_value = Sensor_WriteI2C(slave_addr,(uint8_t*)&cmd_val[0], cmd_len);

	SENSOR_PRINT("SENSOR_hi544: _write_af, ret =  %d, MSL:%x, LSL:%x\n", ret_value, cmd_val[0], cmd_val[1]);

	return ret_value;
#endif
}

LOCAL uint32_t _hi544_BeforeSnapshot(uint32_t param)
{
	uint8_t ret_l, ret_m, ret_h;
	uint32_t capture_coarse, preview_maxline;
	uint32_t capture_maxline, preview_coarse;
	uint32_t capture_mode = param & 0xffff;
	uint32_t preview_mode = (param >> 0x10 ) & 0xffff;
	uint32_t prv_linetime=s_hi544_Resolution_Trim_Tab[preview_mode].line_time;
	uint32_t cap_linetime = s_hi544_Resolution_Trim_Tab[capture_mode].line_time;
	uint32_t prv_gain;


	SENSOR_PRINT("SENSOR_hi544: BeforeSnapshot mode: 0x%08x",param);

	if (preview_mode == capture_mode) {
		SENSOR_PRINT("SENSOR_hi544: prv mode equal to capmode");
		//goto CFG_INFO;
	}

	preview_coarse = Sensor_ReadReg(0x0004);

	prv_gain = _hi544_ReadGain(0);

	Sensor_SetMode(capture_mode);
	Sensor_SetMode_WaitDone();

	if (prv_linetime == cap_linetime) {
		SENSOR_PRINT("SENSOR_hi544: prvline equal to capline");
		//goto CFG_INFO;
	}

	capture_maxline = Sensor_ReadReg(0x0006);
	capture_coarse = preview_coarse * prv_linetime/cap_linetime;

/*
	while(prv_gain > 64){
		prv_gain >>= 1;
		capture_coarse <<= 1;
	}
*/

	if(0 == capture_coarse){
		capture_coarse = 1;
	}

	if(capture_coarse > (capture_maxline - 4)){
		capture_maxline = capture_coarse + 4;
		capture_maxline = (capture_maxline+1)>>1<<1;
		Sensor_WriteReg(0x0006, capture_maxline);
	}

	Sensor_WriteReg(0x0004, capture_coarse);
	Sensor_WriteReg(0x003a, ((prv_gain - 16)&0xff)<<8);

CFG_INFO:
	s_capture_shutter = _hi544_get_shutter();
	s_capture_VTS = _hi544_get_VTS();
	_hi544_ReadGain(capture_mode);
	Sensor_SetSensorExifInfo(SENSOR_EXIF_CTRL_EXPOSURETIME, s_capture_shutter);

	return SENSOR_SUCCESS;
}


LOCAL uint32_t _hi544_after_snapshot(uint32_t param)
{
	SENSOR_PRINT("SENSOR_hi544: after_snapshot mode:%d", param);
	Sensor_SetMode(param);
	return SENSOR_SUCCESS;
}

LOCAL uint32_t _hi544_flash(uint32_t param)
{
	SENSOR_PRINT("SENSOR_hi544: param=%d", param);

	/* enable flash, disable in _hi544_BeforeSnapshot */
	g_flash_mode_en = param;
	Sensor_SetFlash(param);
	SENSOR_PRINT_HIGH("end");
	return SENSOR_SUCCESS;
}

LOCAL uint32_t _hi544_StreamOn(uint32_t param)
{
	SENSOR_PRINT("SENSOR_hi544: StreamOn");

	Sensor_WriteReg(0x0a00, 0x0100);

	return 0;
}

LOCAL uint32_t _hi544_StreamOff(uint32_t param)
{
	SENSOR_PRINT("SENSOR_hi544: StreamOff");

	Sensor_WriteReg(0x0a00, 0x0000);
	usleep(100*1000);

	return 0;
}

int _hi544_get_shutter(void)
{
	// read shutter, in number of line period
	int shutter;

	shutter = (Sensor_ReadReg(0x0004) & 0xffff);

	return shutter;
}

int _hi544_set_shutter(int shutter)
{
	return 0;
}

int _hi544_set_gain16(int gain16)
{
	return 0;
}

static void _calculate_hdr_exposure(int capture_gain16,int capture_VTS, int capture_shutter)
{
	_hi544_set_gain16(capture_gain16);
	_hi544_set_shutter(capture_shutter);
}

static uint32_t _hi544_SetEV(uint32_t param)
{
	uint32_t rtn = SENSOR_SUCCESS;
	SENSOR_EXT_FUN_PARAM_T_PTR ext_ptr = (SENSOR_EXT_FUN_PARAM_T_PTR) param;

	uint16_t value=0x00;
	uint32_t gain = s_hi544_gain;
	uint32_t ev = ext_ptr->param;

	SENSOR_PRINT("SENSOR_hi544: _hi544_SetEV param: 0x%x", ext_ptr->param);

	switch(ev) {
	case SENSOR_HDR_EV_LEVE_0:
		_calculate_hdr_exposure(s_hi544_gain/2,s_capture_VTS,s_capture_shutter);
		break;
	case SENSOR_HDR_EV_LEVE_1:
		_calculate_hdr_exposure(s_hi544_gain,s_capture_VTS,s_capture_shutter);
		break;
	case SENSOR_HDR_EV_LEVE_2:
		_calculate_hdr_exposure(s_hi544_gain,s_capture_VTS,s_capture_shutter *4);
		break;
	default:
		break;
	}
	return rtn;
}
LOCAL uint32_t _hi544_ExtFunc(uint32_t ctl_param)
{
	uint32_t rtn = SENSOR_SUCCESS;
	SENSOR_EXT_FUN_PARAM_T_PTR ext_ptr =
	    (SENSOR_EXT_FUN_PARAM_T_PTR) ctl_param;
	SENSOR_PRINT_HIGH("0x%x", ext_ptr->cmd);

	switch (ext_ptr->cmd) {
	case SENSOR_EXT_FUNC_INIT:
		break;
	case SENSOR_EXT_FOCUS_START:
		break;
	case SENSOR_EXT_EXPOSURE_START:
		break;
	case SENSOR_EXT_EV:
		rtn = _hi544_SetEV(ctl_param);
		break;
	default:
		break;
	}
	return rtn;
}

LOCAL int _hi544_get_VTS(void)
{
	// read VTS from register settings
	int VTS;

	VTS = Sensor_ReadReg(0x0006);//total vertical size[15:8] high byte

	return VTS;
}

LOCAL int _hi544_set_VTS(int VTS)
{
	// write VTS to registers
	int temp;

	temp = VTS & 0xffff;
	Sensor_WriteReg(0x0006, temp);

	return 0;
}
LOCAL uint32_t _hi544_ReadGain(uint32_t param)
{
	uint32_t rtn = SENSOR_SUCCESS;
	uint16_t value=0x00;
	uint32_t gain = 0;

	value = Sensor_ReadReg(0x003a);
	value = (value>>8) & 0xff;
	gain = value + 16;

	s_hi544_gain=(int)gain;

	SENSOR_PRINT("SENSOR_hi544: _hi544_ReadGain gain: 0x%x", s_hi544_gain);

	return gain;
}

#if AF_DRIVER_DW9806
LOCAL uint32_t _dw9806_SRCInit(uint32_t mode)
{
	uint8_t cmd_val[6] = {0x00};
	uint16_t  slave_addr = 0;
	uint16_t cmd_len = 0;
	uint32_t ret_value = SENSOR_SUCCESS;

	slave_addr = DW9806_VCM_SLAVE_ADDR;

	switch (mode) {
		case 1:
		break;

		case 2:
		{
			cmd_len = 2;
			cmd_val[0] = 0x02;
			cmd_val[1] = 0x01;
			Sensor_WriteI2C(slave_addr,(uint8_t*)&cmd_val[0], cmd_len);
			usleep(1000);
			cmd_val[0] = 0x02;
			cmd_val[1] = 0x00;
			Sensor_WriteI2C(slave_addr,(uint8_t*)&cmd_val[0], cmd_len);
			usleep(1000);
			cmd_val[0] = 0x02;
			cmd_val[1] = 0x02;
			Sensor_WriteI2C(slave_addr,(uint8_t*)&cmd_val[0], cmd_len);
			usleep(1000);
			cmd_val[0] = 0x06;
			cmd_val[1] = 0x61;
			Sensor_WriteI2C(slave_addr,(uint8_t*)&cmd_val[0], cmd_len);
			usleep(1000);
			cmd_val[0] = 0x07;
			cmd_val[1] = 0x1c;
			Sensor_WriteI2C(slave_addr,(uint8_t*)&cmd_val[0], cmd_len);
			usleep(1000);
		}
		break;

		case 3:
		break;

	}

	return ret_value;
}
#else
LOCAL uint32_t _dw9174_SRCInit(uint32_t mode)
{
	uint8_t cmd_val[6] = {0x00};
	uint16_t  slave_addr = 0;
	uint16_t cmd_len = 0;
	uint32_t ret_value = SENSOR_SUCCESS;

	slave_addr = DW9714_VCM_SLAVE_ADDR;

	switch (mode) {
		case 1:
		break;

		case 2:
		{
			cmd_val[0] = 0xec;
			cmd_val[1] = 0xa3;
			cmd_val[2] = 0xf2;
			cmd_val[3] = 0x00;
			cmd_val[4] = 0xdc;
			cmd_val[5] = 0x51;
			cmd_len = 6;
			Sensor_WriteI2C(slave_addr,(uint8_t*)&cmd_val[0], cmd_len);
		}
		break;

		case 3:
		break;

	}

	return ret_value;
}
#endif
