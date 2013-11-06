/*
 * Copyright (C) 2010-2011 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __ARCH_CONFIG_H__
#define __ARCH_CONFIG_H__

#include "base.h"
#include <mach/irqs.h>
#include <mach/hardware.h>

/* Configuration for the PB platform with ZBT memory enabled */

static _mali_osk_resource_t arch_configuration [] =
{
	{
		.type = PMU,
		.description = "Mali-400 PMU",
		.base = SPRD_MALI_PHYS+0x2000,
		.irq = IRQ_G3D_INT,
		.mmu_id = 0

	},
	{
		.type = MALI400GP,
		.description = "Mali-400 GP",
		.base = SPRD_MALI_PHYS,
		.irq = IRQ_G3D_INT,
		.mmu_id = 1
	},
	{
		.type = MALI400PP,
		.base = SPRD_MALI_PHYS+0x8000,
		.irq = IRQ_G3D_INT,
		.description = "Mali-400 PP 0",
		.mmu_id = 2
	},
	{
		.type = MALI400PP,
		.base = SPRD_MALI_PHYS+0xA000,
		.irq = IRQ_G3D_INT,
		.description = "Mali-400 PP 1",
		.mmu_id = 3
        },
#if 1
	{
		.type = MMU,
		.base = SPRD_MALI_PHYS+0x3000,
		.irq = IRQ_G3D_INT,
		.description = "Mali-400 MMU for GP",
		.mmu_id = 1
	},
	{
		.type = MMU,
		.base = SPRD_MALI_PHYS+0x4000,
		.irq = IRQ_G3D_INT,
		.description = "Mali-400 MMU for PP 0",
		.mmu_id = 2
	},
	{
		.type = MMU,
		.base = SPRD_MALI_PHYS+0x5000,
		.irq = IRQ_G3D_INT,
		.description = "Mali-400 MMU for PP 1",
		.mmu_id = 3
	},
#endif
#if 0
	{
		.type = MEMORY,
		.description = "Mali SDRAM remapped to baseboard",
		.cpu_usage_adjust = -0x50000000,
		.alloc_order = 0, /* Highest preference for this memory */
		.base = 0xD0000000,
		.size = 0x10000000,
		.flags = _MALI_CPU_WRITEABLE | _MALI_CPU_READABLE | _MALI_PP_READABLE | _MALI_PP_WRITEABLE |_MALI_GP_READABLE | _MALI_GP_WRITEABLE
	},
	{
		.type = MEMORY,
		.description = "Mali ZBT",
		.alloc_order = 5, /* Medium preference for this memory */
		.base = 0xe1000000,
		.size = 0x01000000,
		.flags = _MALI_CPU_WRITEABLE | _MALI_CPU_READABLE | _MALI_PP_READABLE | _MALI_PP_WRITEABLE |_MALI_GP_READABLE | _MALI_GP_WRITEABLE
	},
	{
		.type = MEM_VALIDATION,
		.description = "Framebuffer",
		.base = 0xe0000000,
		.size = 0x01000000,
		.flags = _MALI_CPU_WRITEABLE | _MALI_CPU_READABLE | _MALI_PP_WRITEABLE | _MALI_PP_READABLE
	},
#endif
#if 0
        {
                .type = MEMORY,
                .description = "Mali Video",
//                .alloc_order = 5, /* Medium preference for this memory */
                .base = 0x8c800000,
                .size = 0x01000000,
                .flags = _MALI_CPU_WRITEABLE | _MALI_CPU_READABLE | _MALI_PP_READABLE | _MALI_PP_WRITEABLE |_MALI_GP_READABLE | _MALI_GP_WRITEABLE
        },
#endif
#if 0
        {
                .type = MEMORY,
                .description = "Mali Video",
//                .alloc_order = 5, /* Medium preference for this memory */
                .base = 0x8c800000,
                .size = 0x01000000,
                .flags = _MALI_CPU_WRITEABLE | _MALI_CPU_READABLE | _MALI_MMU_WRITEABLE | _MALI_MMU_READABLE
        },
#endif
#if 1
	{
		.type = OS_MEMORY,
		.description = "OS Memory",
//		.base = 0x00000000,
		.size = ARCH_MALI_MEMORY_SIZE_DEFAULT >> 1,
		.flags = _MALI_CPU_WRITEABLE | _MALI_CPU_READABLE | _MALI_MMU_WRITEABLE | _MALI_MMU_READABLE
	},
#endif
#if 1
	{
		.type = MALI400L2,
		.base = SPRD_MALI_PHYS+0x1000,
		.description = "Mali-400 L2 cache"
	},
#endif
};

#endif /* __ARCH_CONFIG_H__ */
