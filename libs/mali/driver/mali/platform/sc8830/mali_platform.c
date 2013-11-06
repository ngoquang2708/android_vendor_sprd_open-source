/*
 * Copyright (C) 2010-2011 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file mali_platform.c
 * Platform specific Mali driver functions for a default platform
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/workqueue.h>
#include <asm/system.h>
#include <asm/io.h>

#include <mach/hardware.h>
#include <mach/sci.h>
#include <mach/sci_glb_regs.h>

#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "mali_platform.h"
#include "mali_device_pause_resume.h"

#define GPU_GLITCH_FREE_DFS 0
#define GPU_FREQ_CONTROL	1

#define GPU_MIN_DIVISION	1
#define GPU_MAX_DIVISION	3

#if GPU_FREQ_CONTROL
#define GPU_LEVEL0_MAX		208
#define GPU_LEVEL0_MIN		(GPU_LEVEL0_MAX/GPU_MAX_DIVISION)
#define GPU_LEVEL1_MAX		256
#define GPU_LEVEL1_MIN		(GPU_LEVEL1_MAX/GPU_MAX_DIVISION)
#define GPU_LEVEL2_MAX		300
#define GPU_LEVEL2_MIN		(GPU_LEVEL1_MAX/GPU_MAX_DIVISION)
#define GPU_LEVEL3_MAX		312
#define GPU_LEVEL3_MIN		(GPU_LEVEL1_MAX/GPU_MAX_DIVISION)
#endif

extern int gpuinfo_min_freq;
extern int gpuinfo_max_freq;
extern int gpuinfo_transition_latency;
extern int scaling_min_freq;
extern int scaling_max_freq;
extern int scaling_cur_freq;
extern int gpu_level;
static struct clk* gpu_clock = NULL;
static struct clk* gpu_clock_i = NULL;
static struct clk* clock_256m = NULL;
static struct clk* clock_312m = NULL;
static int max_div = GPU_MAX_DIVISION;
const int min_div = GPU_MIN_DIVISION;
#if GPU_FREQ_CONTROL
static int mali_freq_select = 1;
static int old_mali_freq_select = 1;
#endif
static int gpu_clock_div = 1;
static int old_gpu_clock_div = 1;

static int gpu_clock_on = 0;
static int gpu_power_on = 0;
#if !GPU_GLITCH_FREE_DFS
static struct workqueue_struct *gpu_dfs_workqueue = NULL;
#endif
static void gpu_change_freq_div(void);

_mali_osk_errcode_t mali_platform_init(void)
{
	gpu_clock = clk_get(NULL, "clk_gpu");
	gpu_clock_i = clk_get(NULL, "clk_gpu_i");
	clock_256m = clk_get(NULL, "clk_256m");
	clock_312m = clk_get(NULL, "clk_312m");

	gpuinfo_min_freq=52;
	gpuinfo_max_freq=312;
	gpuinfo_transition_latency=300;
	scaling_max_freq=256;
	scaling_min_freq=85;

	MALI_DEBUG_ASSERT(gpu_clock);
	MALI_DEBUG_ASSERT(gpu_clock_i);
	MALI_DEBUG_ASSERT(clock_256m);
	MALI_DEBUG_ASSERT(clock_312m);

	if(!gpu_power_on)
	{
		old_gpu_clock_div = 1;
		gpu_power_on = 1;
		sci_glb_clr(REG_PMU_APB_PD_GPU_TOP_CFG, BIT_PD_GPU_TOP_FORCE_SHUTDOWN);
		mdelay(2);
	}
	if(!gpu_clock_on)
	{
		gpu_clock_on = 1;
		clk_enable(gpu_clock_i);
		clk_set_parent(gpu_clock,clock_256m);
		clk_enable(gpu_clock);
		udelay(300);
	}
#if !GPU_GLITCH_FREE_DFS
	if(gpu_dfs_workqueue == NULL)
	{
		gpu_dfs_workqueue = create_singlethread_workqueue("gpu_dfs");
	}
#endif
	MALI_SUCCESS;
}

_mali_osk_errcode_t mali_platform_deinit(void)
{
#if !GPU_GLITCH_FREE_DFS
	if(gpu_dfs_workqueue)
	{
		destroy_workqueue(gpu_dfs_workqueue);
		gpu_dfs_workqueue = NULL;
	}
#endif
	if(gpu_clock_on)
	{
		gpu_clock_on = 0;
		clk_disable(gpu_clock);
		clk_disable(gpu_clock_i);
	}
	if(gpu_power_on)
	{
		gpu_power_on = 0;
		sci_glb_set(REG_PMU_APB_PD_GPU_TOP_CFG, BIT_PD_GPU_TOP_FORCE_SHUTDOWN);
	}
	MALI_SUCCESS;
}

_mali_osk_errcode_t mali_platform_power_mode_change(mali_power_mode power_mode)
{
	#if 1
	switch(power_mode)
	{
	case MALI_POWER_MODE_ON:
		if(!gpu_power_on)
		{
			old_gpu_clock_div = 1;
			old_mali_freq_select = 1;
			scaling_cur_freq = GPU_LEVEL1_MAX;
			gpu_power_on = 1;
			sci_glb_clr(REG_PMU_APB_PD_GPU_TOP_CFG, BIT_PD_GPU_TOP_FORCE_SHUTDOWN);
			mdelay(2);
		}
		if(!gpu_clock_on)
		{
			gpu_clock_on = 1;
			clk_enable(gpu_clock_i);
			clk_set_parent(gpu_clock,clock_256m);
			clk_enable(gpu_clock);
			udelay(300);
		}
		break;
	case MALI_POWER_MODE_LIGHT_SLEEP:
		if(gpu_clock_on)
		{
			gpu_clock_on = 0;
			clk_disable(gpu_clock);
			clk_disable(gpu_clock_i);
		}
		if(gpu_power_on)
		{
			gpu_power_on = 0;
			sci_glb_set(REG_PMU_APB_PD_GPU_TOP_CFG, BIT_PD_GPU_TOP_FORCE_SHUTDOWN);
		}
		break;
	case MALI_POWER_MODE_DEEP_SLEEP:
		if(gpu_clock_on)
		{
			gpu_clock_on = 0;
			clk_disable(gpu_clock);
			clk_disable(gpu_clock_i);
		}
		if(gpu_power_on)
		{
			gpu_power_on = 0;
			sci_glb_set(REG_PMU_APB_PD_GPU_TOP_CFG, BIT_PD_GPU_TOP_FORCE_SHUTDOWN);
		}
		break;
	};
	#endif
	MALI_SUCCESS;
}

static inline void mali_set_div(int clock_div)
{
	MALI_DEBUG_PRINT(3,("GPU_DFS clock_div %d\n",clock_div));
	sci_glb_write(REG_GPU_APB_APB_CLK_CTRL,BITS_CLK_GPU_DIV(clock_div-1),BITS_CLK_GPU_DIV(3));
}

#if GPU_FREQ_CONTROL
static inline void mali_set_freq(u32 gpu_freq)
{
	MALI_DEBUG_PRINT(3,("GPU_DFS gpu_freq select %u\n",gpu_freq));
	sci_glb_write(REG_GPU_APB_APB_CLK_CTRL,BITS_CLK_GPU_SEL(gpu_freq),BITS_CLK_GPU_SEL(3));
	return;
}
#endif
#if !GPU_GLITCH_FREE_DFS
static void gpu_dfs_func(struct work_struct *work);
static DECLARE_WORK(gpu_dfs_work, &gpu_dfs_func);

static void gpu_dfs_func(struct work_struct *work)
{
	gpu_change_freq_div();
}
#endif

void mali_gpu_utilization_handler(u32 utilization)
{
#if GPU_FREQ_CONTROL
	MALI_DEBUG_PRINT(3,("GPU_DFS  gpu_level:%d\n",gpu_level));
	switch(gpu_level)
	{
		case 3:
			scaling_max_freq=GPU_LEVEL3_MAX;
			scaling_min_freq=GPU_LEVEL3_MAX;
			max_div=GPU_MIN_DIVISION;
			mali_freq_select=3;
			gpu_level=1;
			break;
		case 2:
			scaling_max_freq=GPU_LEVEL1_MAX;
			scaling_min_freq=GPU_LEVEL1_MAX;
			max_div=GPU_MIN_DIVISION;
			mali_freq_select=1;
			gpu_level=1;
			break;
		case 0:
			if(scaling_max_freq>GPU_LEVEL1_MAX)
			{
				scaling_max_freq=GPU_LEVEL3_MAX;
				mali_freq_select=3;
				max_div=GPU_MIN_DIVISION;
			}
			else
			{
				scaling_max_freq=GPU_LEVEL1_MAX;
				mali_freq_select=1;
				max_div=GPU_MAX_DIVISION;
			}
			gpu_level=0;
			break;
		case 1:
		default:
			scaling_max_freq=GPU_LEVEL1_MAX;
			scaling_min_freq=GPU_LEVEL1_MIN;
			max_div=GPU_MAX_DIVISION;
			mali_freq_select=1;
			gpu_level=0;
			break;
	}
#endif

	// if the loading ratio is greater then 90%, switch the clock to the maximum
	if(utilization >= (256*9/10))
	{
		gpu_clock_div = min_div;
	}
	else
	{
		if(utilization == 0)
		{
			utilization = 1;
		}

		// the absolute loading ratio is 1/gpu_clock_div * utilization/256
		// to keep the loading ratio above 70% at a certain level,
		// the absolute loading level is ceil(1/(1/gpu_clock_div * utilization/256 / (7/10)))
		gpu_clock_div = gpu_clock_div*(256*7/10)/utilization + 1;

		// if the 90% of max loading ratio of new level is smaller than the current loading ratio, shift up
		// 1/old_div * utilization/256 > 1/gpu_clock_div * 90%
		if(gpu_clock_div*utilization > old_gpu_clock_div*256*9/10)
			gpu_clock_div--;

		if(gpu_clock_div < min_div) gpu_clock_div = min_div;
		if(gpu_clock_div > max_div) gpu_clock_div = max_div;
	}
#if GPU_FREQ_CONTROL
	MALI_DEBUG_PRINT(3,("GPU_DFS gpu util %d: old %d-> div %d max_div:%d  old_freq %d ->new_freq %d \n", utilization,old_gpu_clock_div, gpu_clock_div,max_div,old_mali_freq_select,mali_freq_select));
	if((gpu_clock_div != old_gpu_clock_div)||(old_mali_freq_select!=mali_freq_select))
#else
	MALI_DEBUG_PRINT(3,("GPU_DFS gpu util %d: old %d-> div %d max_div:%d \n", utilization,old_gpu_clock_div, gpu_clock_div,max_div));
	if(gpu_clock_div != old_gpu_clock_div)
#endif
	{
#if !GPU_GLITCH_FREE_DFS
		if(gpu_dfs_workqueue)
			queue_work(gpu_dfs_workqueue, &gpu_dfs_work);
#else
		gpu_change_freq();
		mali_set_div(gpu_clock_div);
#endif
	}
}

void set_mali_parent_power_domain(void* dev)
{
}

#if GPU_FREQ_CONTROL
static void gpu_change_freq_div(void)
{
	mali_bool power_on;
	mali_dev_pause(&power_on);
	if(gpu_power_on&&gpu_clock_on)
	{
		if(old_mali_freq_select!=mali_freq_select)
		{
			old_mali_freq_select=mali_freq_select;
			switch(old_mali_freq_select)
			{
				case 3:
					scaling_max_freq=GPU_LEVEL3_MAX;
					clk_disable(clock_256m);
					clk_enable(clock_312m);
					udelay(200);
					clk_set_parent(gpu_clock,clock_312m);
					break;
				case 0:
				case 1:
				case 2:
				default:
					scaling_max_freq=GPU_LEVEL1_MAX;
					clk_disable(clock_312m);
					clk_enable(clock_256m);
					udelay(200);
					clk_set_parent(gpu_clock,clock_256m);
					break;
			}

			if(1!=old_gpu_clock_div)
			{
				old_gpu_clock_div=1;
				gpu_clock_div=1;
				mali_set_div(gpu_clock_div);
			}
			udelay(100);
		}
		else
		{
			old_gpu_clock_div = gpu_clock_div;
			mali_set_div(gpu_clock_div);
			udelay(100);
		}
		scaling_cur_freq=scaling_max_freq/gpu_clock_div;
	}
	mali_dev_resume();
}
#else
static void gpu_change_freq_div(void)
{
	mali_bool power_on;
	mali_dev_pause(&power_on);
	if(gpu_power_on)
	{
		old_gpu_clock_div = gpu_clock_div;
		mali_set_div(gpu_clock_div);
		udelay(100);
	}
	mali_dev_resume();
}
#endif
