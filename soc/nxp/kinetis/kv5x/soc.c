/*
 * Copyright (c) 2019 SEAL AG
 *
 * Based on NXP K6x soc.c, which is:
 * Copyright (c) 2014-2015 Wind River Systems, Inc.
 * Copyright (c) 2016, Freescale Semiconductor, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/cache.h>
#include <fsl_common.h>
#include <fsl_clock.h>

#define RUNM_RUN		(0)
#define RUNM_VLPR		(2)
#define RUNM_HSRUN		(3)

#define CLOCK_NODEID(clk) \
	DT_CHILD(DT_INST(0, nxp_kinetis_sim), clk)

#define CLOCK_DIVIDER(clk) \
	DT_PROP_OR(CLOCK_NODEID(clk), clock_div, 1) - 1

static const osc_config_t osc_config = {
	.freq = CONFIG_OSC_XTAL0_FREQ,
	.capLoad = 0,

#if defined(CONFIG_OSC_EXTERNAL)
	.workMode = kOSC_ModeExt,
#elif defined(CONFIG_OSC_LOW_POWER)
	.workMode = kOSC_ModeOscLowPower,
#elif defined(CONFIG_OSC_HIGH_GAIN)
	.workMode = kOSC_ModeOscHighGain,
#else
#error "An oscillator mode must be defined"
#endif

	.oscerConfig = {
		.enableMode = kOSC_ErClkEnable,
#if (defined(FSL_FEATURE_OSC_HAS_EXT_REF_CLOCK_DIVIDER) && \
	FSL_FEATURE_OSC_HAS_EXT_REF_CLOCK_DIVIDER)
		.erclkDiv = 0U,
#endif
	},
};

static const mcg_pll_config_t pll0_config = {
	.enableMode = 0U,
	.prdiv = CONFIG_MCG_PRDIV0,
	.vdiv = CONFIG_MCG_VDIV0,
};

static const sim_clock_config_t sim_config = {
	.pllFllSel = DT_PROP(DT_INST(0, nxp_kinetis_sim), pllfll_select),
	.er32kSrc = DT_PROP(DT_INST(0, nxp_kinetis_sim), er32k_select),
	.clkdiv1 = SIM_CLKDIV1_OUTDIV1(CLOCK_DIVIDER(core_clk)) |
		   SIM_CLKDIV1_OUTDIV2(CLOCK_DIVIDER(bus_clk)) |
		   SIM_CLKDIV1_OUTDIV3(CLOCK_DIVIDER(flexbus_clk)) |
		   SIM_CLKDIV1_OUTDIV4(CLOCK_DIVIDER(flash_clk)),
};

__weak void clk_init(void)
{
	CLOCK_SetSimSafeDivs();

	CLOCK_InitOsc0(&osc_config);
	CLOCK_SetXtal0Freq(CONFIG_OSC_XTAL0_FREQ);

	CLOCK_BootToPeeMode(kMCG_OscselOsc, kMCG_PllClkSelPll0, &pll0_config);

	CLOCK_SetInternalRefClkConfig(kMCG_IrclkEnable, kMCG_IrcSlow,
				      CONFIG_MCG_FCRDIV);

	CLOCK_SetSimConfig(&sim_config);
}

void soc_early_init_hook(void)
{
	/* release I/O power hold to allow normal run state */
	PMC->REGSC |= PMC_REGSC_ACKISO_MASK;

	/* Switch to HSRUN mode */
	SMC->PMPROT |= SMC_PMPROT_AHSRUN_MASK;
	SMC->PMCTRL = (SMC->PMCTRL & ~SMC_PMCTRL_RUNM_MASK) |
		       SMC_PMCTRL_RUNM(RUNM_HSRUN);

	/* Initialize system clocks and PLL */
	clk_init();

	sys_cache_instr_enable();
	sys_cache_data_enable();
}

#ifdef CONFIG_SOC_RESET_HOOK

void soc_reset_hook(void)
{
	SystemInit();
}

#endif /* CONFIG_SOC_RESET_HOOK */
