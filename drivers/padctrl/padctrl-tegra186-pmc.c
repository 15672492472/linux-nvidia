/*
 * Copyright (c) 2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/padctrl/padctrl.h>
#include <linux/tegra-pmc.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <dt-bindings/padctrl/tegra186-pads.h>


#define PMC_DDR_PWR	0x38
#define PMC_E_18V_PWR	0x3C
#define PMC_E_33V_PWR	0x40

struct tegra186_pmc_pads {
	const char *pad_name;
	int pad_id;
	int pad_reg;
	int lo_volt;
	int hi_volt;
	int bit_position;
	bool dynamic_pad_voltage;
};

struct tegra186_pmc_padcontrl {
	struct padctrl_dev *pad_dev;
};

#define TEGRA_186_PADS(_name, _id, _reg, _bit, _lvolt, _hvolt)	\
{								\
	.pad_name = _name,					\
	.pad_id = TEGRA_IO_PAD_GROUP_##_id,			\
	.pad_reg = _reg,					\
	.lo_volt = _lvolt * 1000,				\
	.hi_volt = _hvolt * 1000,				\
	.bit_position = _bit,					\
}

static struct tegra186_pmc_pads tegra186_pads[] = {
	TEGRA_186_PADS("ddr-dvi", DDR_DVI, PMC_DDR_PWR, 0, 1200, 1800),
	TEGRA_186_PADS("ddr-gmi", DDR_GMI, PMC_DDR_PWR, 1, 1200, 1800),
	TEGRA_186_PADS("ddr-sdmmc2", DDR_SDMMC2, PMC_DDR_PWR, 2, 1200, 1800),
	TEGRA_186_PADS("ddr-spi", DDR_SPI, PMC_DDR_PWR, 3, 1200, 1800),
	TEGRA_186_PADS("ufs", UFS, PMC_E_18V_PWR, 1, 1200, 1800),
	TEGRA_186_PADS("dbg", DBG, PMC_E_18V_PWR, 4, 1200, 1800),
	TEGRA_186_PADS("spi", SPI, PMC_E_18V_PWR, 5, 1200, 1800),
	TEGRA_186_PADS("ao-hv", AO_HV, PMC_E_33V_PWR, 0, 1800, 3300),
	TEGRA_186_PADS("audio-hv", AUDIO_HV, PMC_E_33V_PWR, 1, 1800, 3300),
	TEGRA_186_PADS("dmic-hv", DMIC_HV, PMC_E_33V_PWR, 2, 1800, 3300),
	TEGRA_186_PADS("sdmmc1-hv", SDMMC1_HV, PMC_E_33V_PWR, 4, 1800, 3300),
	TEGRA_186_PADS("sdmmc2-hv", SDMMC2_HV, PMC_E_33V_PWR, 5, 1800, 3300),
	TEGRA_186_PADS("sdmmc3-hv", SDMMC3_HV, PMC_E_33V_PWR, 6, 1800, 3300),
};

static int tegra186_pmc_padctrl_set_voltage(struct padctrl_dev *pad_dev,
		int pad_id, u32 voltage)
{
	u32 offset;
	unsigned int pad_mask;
	u32 curr_volt;
	int val;
	int i;

	if ((voltage != 1200000) && (voltage != 1800000)
			&& (voltage != 3300000))
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(tegra186_pads); ++i) {
		if (tegra186_pads[i].pad_id == pad_id)
			break;
	}

	if (i == ARRAY_SIZE(tegra186_pads))
		return -EINVAL;

	if (!tegra186_pads[i].dynamic_pad_voltage) {
		offset = BIT(tegra186_pads[i].bit_position);
		pad_mask = tegra_pmc_pad_voltage_get(tegra186_pads[i].pad_reg);
		curr_volt = (pad_mask & offset) ? tegra186_pads[i].hi_volt :
					tegra186_pads[i].lo_volt;
		if (voltage == curr_volt)
			return 0;

		pr_err("Pad %s: Dynamic pad voltage is not supported\n",
			tegra186_pads[i].pad_name);

		return -EINVAL;
	}

	if ((voltage < tegra186_pads[i].lo_volt) ||
		(voltage > tegra186_pads[i].hi_volt)) {
		pr_err("Voltage %d is not supported for pad %s\n",
			voltage, tegra186_pads[i].pad_name);
		return -EINVAL;
	}

	offset = BIT(tegra186_pads[i].bit_position);
	val = (voltage == tegra186_pads[i].hi_volt) ? offset : 0;
	tegra_pmc_pad_voltage_update(tegra186_pads[i].pad_reg, offset, val);
	udelay(100);
	return 0;
}

static int tegra186_pmc_padctl_get_voltage(struct padctrl_dev *pad_dev,
		int pad_id, u32 *voltage)
{
	unsigned int pad_mask;
	u32 offset;
	int i;

	for (i = 0; i < ARRAY_SIZE(tegra186_pads); ++i) {
		if (tegra186_pads[i].pad_id == pad_id)
			break;
	}

	if (i == ARRAY_SIZE(tegra186_pads))
		return -EINVAL;

	offset = BIT(tegra186_pads[i].bit_position);
	pad_mask = tegra_pmc_pad_voltage_get(tegra186_pads[i].pad_reg);
	*voltage = (pad_mask & offset) ? tegra186_pads[i].hi_volt :
						tegra186_pads[i].lo_volt;
	return 0;
}

static struct padctrl_ops tegra186_pmc_padctrl_ops = {
	.set_voltage = &tegra186_pmc_padctrl_set_voltage,
	.get_voltage = &tegra186_pmc_padctl_get_voltage,
};

static struct padctrl_desc tegra186_pmc_padctrl_desc = {
	.name = "tegra-pmc-padctrl",
	.ops = &tegra186_pmc_padctrl_ops,
};

static int tegra186_pmc_parse_io_pad_init(struct device_node *np,
		struct padctrl_dev *pad_dev)
{
	struct device_node *pad_np, *child;
	u32 pval;
	int pad_id;
	const char *pad_name, *name;
	int n_config;
	u32 *volt_configs;
	int i, index, vcount;
	bool dyn_pad_volt;
	int pindex;
	int ret;

	pad_np = of_get_child_by_name(np, "io-pad-defaults");
	if (!pad_np)
		return 0;

	pr_info("PMC: configuring io pad defaults\n");
	n_config = of_get_child_count(pad_np);
	if (!n_config)
		return 0;
	n_config *= 2;

	volt_configs = kzalloc(n_config * sizeof(*volt_configs), GFP_KERNEL);
	if (!volt_configs)
		return -ENOMEM;

	vcount = 0;
	for_each_child_of_node(pad_np, child) {
		/* Ignore the nodes if disabled */
		ret = of_device_is_available(child);
		if (!ret)
			continue;
		name = of_get_property(child, "nvidia,pad-name", NULL);
		if (!name)
			name = child->name;

		for (i = 0; i < ARRAY_SIZE(tegra186_pads); ++i) {
			if (strcmp(name, tegra186_pads[i].pad_name))
				continue;
			ret = of_property_read_u32(child,
				"nvidia,io-pad-init-voltage", &pval);
			if (!ret) {
				volt_configs[vcount] = i;
				volt_configs[vcount + 1] = pval;
				vcount += 2;
			}
			tegra186_pads[i].dynamic_pad_voltage =
				of_property_read_bool(child,
					"nvidia,enable-dynamic-pad-voltage");
		}
	}

	for (i = 0; i < vcount/2; ++i) {
		index = i * 2;
		if (!volt_configs[index + 1])
			continue;
		pindex = volt_configs[index];
		pad_id = tegra186_pads[pindex].pad_id;
		pad_name = tegra186_pads[pindex].pad_name;

		dyn_pad_volt = tegra186_pads[pindex].dynamic_pad_voltage;
		tegra186_pads[pindex].dynamic_pad_voltage = true;
		ret = tegra186_pmc_padctrl_set_voltage(pad_dev,
				pad_id, volt_configs[index + 1]);
		if (ret < 0) {
			pr_warn("PMC: IO pad %s voltage config failed: %d\n",
					pad_name, ret);
			WARN_ON(1);
		} else {
			pr_info("PMC: IO pad %s voltage is %d\n",
					pad_name, volt_configs[index + 1]);
		}
		tegra186_pads[pindex].dynamic_pad_voltage = dyn_pad_volt;
	}

	kfree(volt_configs);
	return 0;
}

int tegra186_pmc_padctrl_init(struct device *dev, struct device_node *np)
{
	struct tegra186_pmc_padcontrl *pmc_padctrl;
	struct padctrl_config config = { };
	int ret;

	pmc_padctrl = kzalloc(sizeof(*pmc_padctrl), GFP_KERNEL);
	if (!pmc_padctrl) {
		pr_err("Mem allocation for pmc_padctrl failed\n");
		return -ENOMEM;
	}

	config.of_node = (dev && dev->of_node) ? dev->of_node : np;
	pmc_padctrl->pad_dev = padctrl_register(dev, &tegra186_pmc_padctrl_desc,
					&config);
	if (IS_ERR(pmc_padctrl->pad_dev)) {
		ret = PTR_ERR(pmc_padctrl->pad_dev);
		pr_err("T186 padctrl driver init failed: %d\n", ret);
		kfree(pmc_padctrl);
		return ret;
	}
	padctrl_set_drvdata(pmc_padctrl->pad_dev, pmc_padctrl);
	tegra186_pmc_parse_io_pad_init(config.of_node,
				pmc_padctrl->pad_dev);

	pr_info("T186 pmc padctrl driver initialized\n");
	return 0;
}

#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#include <linux/seq_file.h>

static int dbg_io_pad_show(struct seq_file *s, void *unused)
{
	unsigned int pad_mask;
	u32 offset;
	int i;
	unsigned long voltage;

	for (i = 0; i < ARRAY_SIZE(tegra186_pads); ++i) {
		offset = BIT(tegra186_pads[i].bit_position);
		pad_mask = tegra_pmc_pad_voltage_get(tegra186_pads[i].pad_reg);
		voltage = (pad_mask & offset) ? tegra186_pads[i].hi_volt :
						tegra186_pads[i].lo_volt;
		seq_printf(s, "PMC: IO pad %s voltage %lu\n",
			tegra186_pads[i].pad_name, voltage);
	}

	return 0;
}
static int dbg_io_pad_open(struct inode *inode, struct file *file)
{
	return single_open(file, dbg_io_pad_show, &inode->i_private);
}

static const struct file_operations debug_fops = {
	.open	   = dbg_io_pad_open,
	.read	   = seq_read,
	.llseek	 = seq_lseek,
	.release	= single_release,
};

static int __init tegra_io_pad_debuginit(void)
{
	(void)debugfs_create_file("tegra_io_pad", S_IRUGO,
				NULL, NULL, &debug_fops);
	return 0;
}
late_initcall(tegra_io_pad_debuginit);

#endif
