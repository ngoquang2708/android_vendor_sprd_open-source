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

#include <linux/platform_device.h>
#include <linux/version.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/pm.h>
#ifdef CONFIG_PM_RUNTIME
#include <linux/pm_runtime.h>
#endif
#include <asm/io.h>
#include <linux/mali/mali_utgard.h>

#include <mach/irqs.h>
#include <mach/hardware.h>
#include <mach/sci.h>
#include <mach/sci_glb_regs.h>
#include <linux/workqueue.h>

#include "mali_kernel_common.h"
#include "base.h"

#define GPU_GLITCH_FREE_DFS 1
#define GPU_FREQ_CONTROL	1

#define GPU_MIN_DIVISION	1
#define GPU_MAX_DIVISION	2

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

#define MAX(x,y)	({typeof(x) _x = (x); typeof(y) _y = (y); (void) (&_x == &_y); _x > _y ? _x : _y;})

extern int gpuinfo_min_freq;
extern int gpuinfo_max_freq;
extern int gpuinfo_transition_latency;
extern int scaling_min_freq;
extern int scaling_max_freq;
extern int scaling_cur_freq;
extern int gpu_level;
extern int mali_dfs_flag;
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

static void mali_platform_device_release(struct device *device);
static void mali_platform_utilization(struct mali_gpu_utilization_data *data);

static struct resource mali_gpu_resources[] =
{
	MALI_GPU_RESOURCES_MALI400_MP1_PMU(SPRD_MALI_PHYS, IRQ_GPU_INT, IRQ_GPU_INT, IRQ_GPU_INT, IRQ_GPU_INT)
};

static struct mali_gpu_device_data mali_gpu_data =
{
	.shared_mem_size = ARCH_MALI_MEMORY_SIZE_DEFAULT,
	.utilization_interval = 300,
	.utilization_callback = mali_platform_utilization,
};

static struct platform_device mali_gpu_device =
{
	.name = MALI_GPU_NAME_UTGARD,
	.id = 0,
	.num_resources = ARRAY_SIZE(mali_gpu_resources),
	.resource = mali_gpu_resources,
	.dev.platform_data = &mali_gpu_data,
	.dev.release = mali_platform_device_release,
};

int mali_platform_device_register(void)
{
	int err = -1;

	MALI_DEBUG_PRINT(4, ("mali_platform_device_register() called\n"));

	gpu_clock = clk_get(NULL, "clk_gpu");
	gpu_clock_i = clk_get(NULL, "clk_gpu_i");
	clock_256m = clk_get(NULL, "clk_256m");
	clock_312m = clk_get(NULL, "clk_312m");

	gpuinfo_min_freq=52;
	gpuinfo_max_freq=312;
	gpuinfo_transition_latency=300;
	scaling_max_freq=256;
	scaling_min_freq=85;
	mali_dfs_flag=0;

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

	err = platform_device_register(&mali_gpu_device);
	if (0 == err)
	{
#ifdef CONFIG_PM_RUNTIME
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
		pm_runtime_set_autosuspend_delay(&(mali_gpu_device.dev), 50);
		pm_runtime_use_autosuspend(&(mali_gpu_device.dev));
#endif
		pm_runtime_enable(&(mali_gpu_device.dev));
#endif

		return 0;
	}

	platform_device_unregister(&mali_gpu_device);

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
	return err;
}

void mali_platform_device_unregister(void)
{
	MALI_DEBUG_PRINT(4, ("mali_platform_device_unregister() called\n"));

	platform_device_unregister(&mali_gpu_device);

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
}

static void mali_platform_device_release(struct device *device)
{
	MALI_DEBUG_PRINT(4, ("mali_platform_device_release() called\n"));
}

void mali_platform_power_mode_change(int power_mode)
{
#if 1
	switch(power_mode)
	{
	//MALI_POWER_MODE_ON
	case 0:
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
	//MALI_POWER_MODE_LIGHT_SLEEP
	case 1:
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
	//MALI_POWER_MODE_DEEP_SLEEP
	case 2:
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

void mali_platform_utilization(struct mali_gpu_utilization_data *data)
{
	unsigned int utilization = data->utilization_gpu;
	MALI_DEBUG_PRINT(3,("GPU_DFS mali_utilization  gpu:%d  gp:%d pp:%d\n",data->utilization_gpu,data->utilization_gp,data->utilization_pp));
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
		gpu_change_freq_div();
#endif
	}
}

#if GPU_FREQ_CONTROL
static void gpu_change_freq_div(void)
{
	if(!mali_dfs_flag) {
		MALI_DEBUG_PRINT(3,("GPU_DFS gpu_change_freq_div disabled\n"));
		return;
	}
#if !GPU_GLITCH_FREE_DFS
	mali_dev_pause();
#endif
	MALI_DEBUG_PRINT(3,("GPU_DFS gpu_change_freq_div enabled\n"));
	if(gpu_power_on&&gpu_clock_on)
	{
		if(old_mali_freq_select!=mali_freq_select)
		{
#if GPU_GLITCH_FREE_DFS
			mali_dev_pause();
#endif
			old_mali_freq_select=mali_freq_select;
			switch(old_mali_freq_select)
			{
				case 3:
					scaling_max_freq=GPU_LEVEL3_MAX;
					clk_enable(clock_312m);
					clk_set_parent(gpu_clock,clock_312m);
					clk_disable(clock_256m);
					udelay(200);
					break;
				case 0:
				case 1:
				case 2:
				default:
					scaling_max_freq=GPU_LEVEL1_MAX;
					clk_enable(clock_256m);
					clk_set_parent(gpu_clock,clock_256m);
					clk_disable(clock_312m);
					udelay(200);
					break;
			}
#if GPU_GLITCH_FREE_DFS
			mali_dev_resume();
#endif
			if(1!=old_gpu_clock_div)
			{
				old_gpu_clock_div=1;
				gpu_clock_div=1;
				mali_set_div(gpu_clock_div);
			}
#if !GPU_GLITCH_FREE_DFS
			udelay(100);
#endif
		}
		else
		{
			old_gpu_clock_div = gpu_clock_div;
			mali_set_div(gpu_clock_div);
#if !GPU_GLITCH_FREE_DFS
			udelay(100);
#endif
		}
		scaling_cur_freq=scaling_max_freq/gpu_clock_div;
	}
#if !GPU_GLITCH_FREE_DFS
	mali_dev_resume();
#endif
}

#else
static void gpu_change_freq_div(void)
{
#if !GPU_GLITCH_FREE_DFS
	mali_dev_pause();
#endif
	if(gpu_power_on&&gpu_clock_on)
	{
		old_gpu_clock_div = gpu_clock_div;
		mali_set_div(gpu_clock_div);
#if !GPU_GLITCH_FREE_DFS
		udelay(100);
#endif
	}
#if !GPU_GLITCH_FREE_DFS
	mali_dev_resume();
#endif
}
#endif
