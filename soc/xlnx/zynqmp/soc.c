/*
 * Copyright (c) 2019 Lexmark International, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/cache.h>

#include <cmsis_core.h>
#include "soc.h"

void sys_arch_reboot(int type)
{
	ARG_UNUSED(type);

	sys_write32(CRL_APB_RESET_CTRL_SRST_MASK, CRL_APB_RESET_CTRL);
}

void soc_reset_hook(void)
{
	/*
	 * Use normal exception vectors address range (0x0-0x1C).
	 */
	unsigned int sctlr = __get_SCTLR();

	sctlr &= ~SCTLR_V_Msk;
	__set_SCTLR(sctlr);
}

void soc_early_init_hook(void)
{
	/* Enable caches */
	sys_cache_instr_enable();
	sys_cache_data_enable();
}
