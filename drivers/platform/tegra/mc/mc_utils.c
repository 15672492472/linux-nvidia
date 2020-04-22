/**
 * Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/types.h>
#include <linux/platform/tegra/mc.h>
#include <linux/platform/tegra/mc_utils.h>
#include <soc/tegra/chip-id.h>

#define BYTES_PER_CLK_PER_CH	4
#define CH_16			16
#define CH_8			8
#define CH_4			4
#define CH_16_BYTES_PER_CLK	(BYTES_PER_CLK_PER_CH * CH_16)
#define CH_8_BYTES_PER_CLK	(BYTES_PER_CLK_PER_CH * CH_8)
#define CH_4_BYTES_PER_CLK	(BYTES_PER_CLK_PER_CH * CH_4)

/* EMC regs */
#define MC_BASE 0x02c10000
#define EMC_BASE 0x02c60000

#define EMC_FBIO_CFG5_0 0x103C
#define MC_EMEM_ADR_CFG_CHANNEL_ENABLE_0 0xdf8
#define MC_EMEM_ADR_CFG_0 0x54
#define MC_ECC_CONTROL_0 0x1880

#define CH_MASK 0xFFFF /* Change bit counting if this mask changes */
#define CH4 0xf
#define CH2 0x3

#define ECC_MASK 0x1 /* 1 = enabled, 0 = disabled */
#define RANK_MASK 0x1 /* 1 = 2-RANK, 0 = 1-RANK */
#define DRAM_MASK 0x3

/* EMC_FBIO_CFG5_0(1:0) : DRAM_TYPE */
#define DRAM_LPDDR4 0
#define DRAM_LPDDR5 1
#define DRAM_DDR3 2
#define BR4_MODE 4
#define BR8_MODE 8

/* BANDWIDTH LATENCY COMPONENTS */
#define SMMU_DISRUPTION_DRAM_CLK_LP4 6003
#define SMMU_DISRUPTION_DRAM_CLK_LP5 9005
#define RING0_DISRUPTION_MC_CLK_LP4 63
#define RING0_DISRUPTION_MC_CLK_LP5 63
#define HUM_DISRUPTION_DRAM_CLK_LP4 1247
#define HUM_DISRUPTION_DRAM_CLK_LP5 4768
#define HUM_DISRUPTION_NS_LP4 1406
#define HUM_DISRUPTION_NS_LP5 1707
#define EXPIRED_ISO_DRAM_CLK_LP4 424
#define EXPIRED_ISO_DRAM_CLK_LP5 792
#define EXPIRED_ISO_NS_LP4 279
#define EXPIRED_ISO_NS_LP5 279
#define REFRESH_RATE_LP4 176
#define REFRESH_RATE_LP5 226
#define PERIODIC_TRAINING_LP4 380
#define PERIODIC_TRAINING_LP5 380
#define CALIBRATION_LP4 30
#define CALIBRATION_LP5 30

struct emc_params {
	u32 rank;
	u32 ecc;
	u32 ch;
	u32 dram;
};

static struct emc_params emc_param;
static int ch_num;

static unsigned long freq_to_bw(unsigned long freq)
{
	if (ch_num == CH_16)
		return freq * CH_16_BYTES_PER_CLK;

	if (ch_num == CH_8)
		return freq * CH_8_BYTES_PER_CLK;

	/*4CH and 4CH_ECC*/
	return freq * CH_4_BYTES_PER_CLK;
}

static unsigned long bw_to_freq(unsigned long bw)
{
	if (ch_num == CH_16)
		return (bw + CH_16_BYTES_PER_CLK - 1) / CH_16_BYTES_PER_CLK;

	if (ch_num == CH_8)
		return (bw + CH_8_BYTES_PER_CLK - 1) / CH_8_BYTES_PER_CLK;

	/*4CH and 4CH_ECC*/
	return (bw + CH_4_BYTES_PER_CLK - 1) / CH_4_BYTES_PER_CLK;
}

unsigned long emc_freq_to_bw(unsigned long freq)
{
	return freq_to_bw(freq);
}
EXPORT_SYMBOL_GPL(emc_freq_to_bw);

unsigned long emc_bw_to_freq(unsigned long bw)
{
	return bw_to_freq(bw);
}
EXPORT_SYMBOL_GPL(emc_bw_to_freq);

u8 get_dram_num_channels(void)
{
	return ch_num;
}
EXPORT_SYMBOL_GPL(get_dram_num_channels);

/* DRAM clock in MHz
 *
 * Return: MC clock in MHz
*/
unsigned long dram_clk_to_mc_clk(unsigned long dram_clk)
{
	unsigned long mc_clk;

	if (dram_clk <= 1600)
		mc_clk = (dram_clk + BR4_MODE - 1) / BR4_MODE;
	else
		mc_clk = (dram_clk + BR8_MODE - 1) / BR8_MODE;
	return mc_clk;
}
EXPORT_SYMBOL_GPL(dram_clk_to_mc_clk);

unsigned long bw_disruption_latency(unsigned long dram_freq,
				    unsigned long mc_freq,
				    u32 dram)
{
	unsigned long smmu_disruption, ring0_disruption, hum_disruption,
		      exprired_iso, refresh, periodic_training, calibration,
		      total_bw_disruption_latency;

	if (dram == DRAM_LPDDR4) {
		smmu_disruption = ((SMMU_DISRUPTION_DRAM_CLK_LP4 * 1000) + dram_freq - 1)
				  / dram_freq;
		ring0_disruption = ((RING0_DISRUPTION_MC_CLK_LP4 * 1000) + mc_freq - 1)
				   / mc_freq;
		hum_disruption = (((HUM_DISRUPTION_DRAM_CLK_LP4 * 1000) + dram_freq - 1)
				 / dram_freq) + HUM_DISRUPTION_NS_LP4;
		exprired_iso = (((EXPIRED_ISO_DRAM_CLK_LP4 * 1000) + dram_freq - 1)
			       / dram_freq) + EXPIRED_ISO_NS_LP4;
		refresh = REFRESH_RATE_LP4;
		periodic_training = PERIODIC_TRAINING_LP4;
		calibration = CALIBRATION_LP4;
	} else if (dram == DRAM_LPDDR5) {
		smmu_disruption = ((SMMU_DISRUPTION_DRAM_CLK_LP5 * 1000) + dram_freq - 1)
				  / dram_freq;
		ring0_disruption = ((RING0_DISRUPTION_MC_CLK_LP5 * 1000) + mc_freq - 1)
				   / mc_freq;
		hum_disruption = (((HUM_DISRUPTION_DRAM_CLK_LP5 * 1000) + dram_freq - 1)
				 / dram_freq) + HUM_DISRUPTION_NS_LP5;
		exprired_iso = (((EXPIRED_ISO_DRAM_CLK_LP5 * 1000) + dram_freq - 1)
			       / dram_freq) + EXPIRED_ISO_NS_LP5;
		refresh = REFRESH_RATE_LP5;
		periodic_training = PERIODIC_TRAINING_LP5;
		calibration = CALIBRATION_LP5;
	} else {
		pr_err("Unknown frequency configuration\n");
		return 0;
	}

	total_bw_disruption_latency = smmu_disruption +
				      ring0_disruption +
				      hum_disruption +
				      exprired_iso +
				      refresh +
				      periodic_training +
				      calibration;
	return total_bw_disruption_latency;
}
EXPORT_SYMBOL_GPL(bw_disruption_latency);

enum dram_types tegra_dram_types(void)
{
	enum dram_types dram_type = DRAM_TYPE_NONE;

	switch (emc_param.dram) {
	case DRAM_LPDDR5:
		if (emc_param.ecc) {
			if (ch_num == 16) {
				if (emc_param.rank)
					dram_type =
					DRAM_TYPE_LPDDR5_16CH_ECC_2RANK;
				else
					dram_type =
					DRAM_TYPE_LPDDR5_16CH_ECC_1RANK;
			} else if (ch_num == 8) {
				if (emc_param.rank)
					dram_type =
					DRAM_TYPE_LPDDR5_8CH_ECC_2RANK;
				else
					dram_type =
					DRAM_TYPE_LPDDR5_8CH_ECC_1RANK;
			} else if (ch_num == 4) {
				if (emc_param.rank)
					dram_type =
					DRAM_TYPE_LPDDR5_4CH_ECC_2RANK;
				else
					dram_type =
					DRAM_TYPE_LPDDR5_4CH_ECC_1RANK;
			}
		} else {
			if (ch_num == 16) {
				if (emc_param.rank)
					dram_type =
					DRAM_TYPE_LPDDR5_16CH_2RANK;
				else
					dram_type =
					DRAM_TYPE_LPDDR5_16CH_1RANK;
			} else if (ch_num == 8) {
				if (emc_param.rank)
					dram_type =
					DRAM_TYPE_LPDDR5_8CH_2RANK;
				else
					dram_type =
					DRAM_TYPE_LPDDR5_8CH_1RANK;
			} else if (ch_num == 4) {
				if (emc_param.rank)
					dram_type =
					DRAM_TYPE_LPDDR5_4CH_2RANK;
				else
					dram_type =
					DRAM_TYPE_LPDDR5_4CH_1RANK;
			}
		}

		if (ch_num < 4) {
			pr_err("DRAM_LPDDR5: Unknown memory channel configuration\n");
			WARN_ON(true);
		}

		break;
	case DRAM_LPDDR4:
		if (emc_param.ecc) {
			if (ch_num == 16) {
				if (emc_param.rank)
					dram_type =
					DRAM_TYPE_LPDDR4_16CH_ECC_2RANK;
				else
					dram_type =
					DRAM_TYPE_LPDDR4_16CH_ECC_1RANK;
			} else if (ch_num == 8) {
				if (emc_param.rank)
					dram_type =
					DRAM_TYPE_LPDDR4_8CH_ECC_2RANK;
				else
					dram_type =
					DRAM_TYPE_LPDDR4_8CH_ECC_1RANK;
			} else if (ch_num == 4) {
				if (emc_param.rank)
					dram_type =
					DRAM_TYPE_LPDDR4_4CH_ECC_2RANK;
				else
					dram_type =
					DRAM_TYPE_LPDDR4_4CH_ECC_1RANK;
			}
		} else {
			if (ch_num == 16) {
				if (emc_param.rank)
					dram_type =
					DRAM_TYPE_LPDDR4_16CH_2RANK;
				else
					dram_type =
					DRAM_TYPE_LPDDR4_16CH_1RANK;
			} else if (ch_num == 8) {
				if (emc_param.rank)
					dram_type =
					DRAM_TYPE_LPDDR4_8CH_2RANK;
				else
					dram_type =
					DRAM_TYPE_LPDDR4_8CH_1RANK;
			} else if (ch_num == 4) {
				if (emc_param.rank)
					dram_type =
					DRAM_TYPE_LPDDR5_4CH_2RANK;
				else
					dram_type =
					DRAM_TYPE_LPDDR5_4CH_1RANK;
			}
		}

		if (ch_num < 4) {
			pr_err("DRAM_LPDDR4: Unknown memory channel configuration\n");
			WARN_ON(true);
		}
		break;
	default:
		pr_err("mc_util: ddr config not supported\n");
		WARN_ON(true);
	}

	return dram_type;
}
EXPORT_SYMBOL_GPL(tegra_dram_types);

void tegra_mc_utils_init(void)
{
	u32 dram, ch, ecc, rank;
	void __iomem *emc_base;

	emc_base = ioremap(EMC_BASE, 0x00010000);
	if (is_tegra_safety_build())
		dram = 0x1;
	else
		dram = readl(emc_base + EMC_FBIO_CFG5_0) & DRAM_MASK;

	ch = mc_readl(MC_EMEM_ADR_CFG_CHANNEL_ENABLE_0) & CH_MASK;
	ecc = mc_readl(MC_ECC_CONTROL_0) & ECC_MASK;
	rank = mc_readl(MC_EMEM_ADR_CFG_0) & RANK_MASK;

	iounmap(emc_base);

	while (ch) {
		if (ch & 1)
			ch_num++;
		ch >>= 1;
	}

	/* pre silicon use LPDDR4 16ch no ecc 1-rank config*/
	if (tegra_platform_is_sim() || tegra_platform_is_fpga()) {
		dram = DRAM_LPDDR4;
		ch_num = 16;
	}

	emc_param.ch = ch;
	emc_param.ecc = ecc;
	emc_param.rank = rank;
	emc_param.dram = dram;
}
