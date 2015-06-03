/*
 * Copyright (c) 2014-2015, NVIDIA CORPORATION.  All rights reserved.
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

#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/debugfs.h>
#include <linux/cpu.h>
#include <linux/notifier.h>
#include <linux/tegra-mce.h>
#include <linux/tegra-soc.h>

#include <asm/smp_plat.h>

#define SMC_SIP_INVOKE_MCE	0x82FFFF00

#define NR_SMC_REGS		6

/* MCE command enums for SMC calls */
enum {
	MCE_SMC_ENTER_CSTATE,
	MCE_SMC_UPDATE_CSTATE_INFO,
	MCE_SMC_UPDATE_XOVER_TIME,
	MCE_SMC_READ_CSTATE_STATS,
	MCE_SMC_WRITE_CSTATE_STATS,
	MCE_SMC_IS_SC7_ALLOWED,
	MCE_SMC_ONLINE_CORE,
	MCE_SMC_CC3_CTRL,
	MCE_SMC_ECHO_DATA,
	MCE_SMC_READ_VERSIONS,
	MCE_SMC_ENUM_FEATURES,
};

struct mce_regs {
	u64 args[NR_SMC_REGS];
};

static noinline notrace int __send_smc(u64 func, struct mce_regs *regs)
{
	u32 ret = SMC_SIP_INVOKE_MCE;
	asm volatile (
	"	mov	x0, %0 \n"
	"	mov	x1, %1 \n"
	"	ldp	x2, x3, [%2, #16 * 0] \n"
	"	ldp	x4, x5, [%2, #16 * 1] \n"
	"	ldp	x6, x7, [%2, #16 * 2] \n"
	"	isb \n"
	"	smc	#0 \n"
	"	mov	%0, x0 \n"
	"	stp	x2, x3, [%2, #16 * 0] \n"
	"	stp	x4, x5, [%2, #16 * 1] \n"
	"	stp	x6, x7, [%2, #16 * 2] \n"
	: "+r" (ret)
	: "r" (func), "r" (regs)
	: "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7");
	return ret;
}

#define send_smc(func, regs) \
({ \
	int __ret = __send_smc(func, regs); \
	if (__ret) { \
		pr_err("%s: failed (ret=%d)\n", __func__, __ret); \
		return __ret; \
	} \
	__ret; \
})

/**
 * Specify power state and wake time for entering upon STANDBYWFI
 *
 * @state:		requested core power state
 * @wake_time:	wake time in TSC ticks
 *
 * Returns 0 if success.
 */
int tegra_mce_enter_cstate(u32 state, u32 wake_time)
{
	struct mce_regs regs;
	regs.args[0] = state;
	regs.args[1] = wake_time;
	return send_smc(MCE_SMC_ENTER_CSTATE, &regs);
}
EXPORT_SYMBOL(tegra_mce_enter_cstate);

/**
 * Specify deepest cluster/ccplex/system states allowed.
 *
 * @cluster:	deepest cluster-wide state
 * @ccplex:		deepest ccplex-wide state
 * @system:		deepest system-wide state
 * @force:		forced system state
 * @wake_mask:	wake mask to be updated
 *
 * Returns 0 if success.
 */
int tegra_mce_update_cstate_info(u32 cluster, u32 ccplex, u32 system,
	u8 force, u32 wake_mask)
{
	struct mce_regs regs;
	regs.args[0] = cluster;
	regs.args[1] = ccplex;
	regs.args[2] = system;
	regs.args[3] = force;
	regs.args[4] = wake_mask;
	return send_smc(MCE_SMC_UPDATE_CSTATE_INFO, &regs);
}
EXPORT_SYMBOL(tegra_mce_update_cstate_info);

/**
 * Update threshold for one specific c-state crossover
 *
 * @type: type of state crossover.
 * @time: idle time threshold.
 *
 * Returns 0 if success.
 */
int tegra_mce_update_crossover_time(u32 type, u32 time)
{
	struct mce_regs regs;
	regs.args[0] = type;
	regs.args[1] = time;
	return send_smc(MCE_SMC_UPDATE_XOVER_TIME, &regs);
}
EXPORT_SYMBOL(tegra_mce_update_crossover_time);

/**
 * Query the runtime stats of a specific cstate
 *
 * @state: c-state of the stats.
 * @stats: output integer to hold the stats.
 *
 * Returns 0 if success.
 */
int tegra_mce_read_cstate_stats(u32 state, u32 *stats)
{
	struct mce_regs regs;
	regs.args[0] = state;
	send_smc(MCE_SMC_READ_CSTATE_STATS, &regs);
	*stats = (u32)regs.args[1];
	return 0;
}
EXPORT_SYMBOL(tegra_mce_read_cstate_stats);

/**
 * Overwrite the runtime stats of a specific c-state
 *
 * @state: c-state of the stats.
 * @stats: integer represents the new stats.
 *
 * Returns 0 if success.
 */
int tegra_mce_write_cstate_stats(u32 state, u32 stats)
{
	struct mce_regs regs;
	regs.args[0] = state;
	regs.args[1] = stats;
	return send_smc(MCE_SMC_WRITE_CSTATE_STATS, &regs);
}
EXPORT_SYMBOL(tegra_mce_write_cstate_stats);

/**
 * Query MCE to determine if SC7 is allowed
 * given a target core's C-state and wake time
 *
 * @state: c-state of the stats.
 * @stats: integer represents the new stats.
 * @allowed: pointer to result
 *
 * Returns 0 if success.
 */
int tegra_mce_is_sc7_allowed(u32 state, u32 wake, u32 *allowed)
{
	struct mce_regs regs;
	regs.args[0] = state;
	regs.args[1] = wake;
	send_smc(MCE_SMC_IS_SC7_ALLOWED, &regs);
	*allowed = (u32)regs.args[3];
	return 0;
}
EXPORT_SYMBOL(tegra_mce_is_sc7_allowed);

/**
 * Bring another offlined core back online to C0 state.
 *
 * @cpu:		logical cpuid from smp_processor_id()
 *
 * Returns 0 if success.
 */
int tegra_mce_online_core(int cpu)
{
	struct mce_regs regs;
	regs.args[0] = cpu_logical_map(cpu);
	return send_smc(MCE_SMC_ONLINE_CORE, &regs);
}
EXPORT_SYMBOL(tegra_mce_online_core);

/**
 * Program Auto-CC3 feature.
 *
 * @freq:		freq of IDLE voltage/freq register
 * @volt:		volt of IDLE voltage/freq register
 * @enable:		enable bit for Auto-CC3
 *
 * Returns 0 if success.
 */
int tegra_mce_cc3_ctrl(u32 freq, u32 volt, u8 enable)
{
	struct mce_regs regs;
	regs.args[0] = freq;
	regs.args[1] = volt;
	regs.args[2] = enable;
	return send_smc(MCE_SMC_CC3_CTRL, &regs);
}
EXPORT_SYMBOL(tegra_mce_cc3_ctrl);

/**
 * Send data to MCE which echoes it back.
 *
 * @data: data to be sent to MCE.
 * @out: output data to hold the response.
 * @matched: pointer to matching result
 *
 * Returns 0 if success.
 */
int tegra_mce_echo_data(u32 data, int *matched)
{
	struct mce_regs regs;
	regs.args[0] = data;
	send_smc(MCE_SMC_ECHO_DATA, &regs);
	*matched = (u32)regs.args[1];
	return 0;
}
EXPORT_SYMBOL(tegra_mce_echo_data);

/**
 * Read out MCE API major/minor versions
 *
 * @major: output for major number.
 * @minor: output for minor number.
 *
 * Returns 0 if success.
 */
int tegra_mce_read_versions(u32 *major, u32 *minor)
{
	struct mce_regs regs;
	send_smc(MCE_SMC_READ_VERSIONS, &regs);
	*major = (u32)regs.args[0];
	*minor = (u32)regs.args[1];
	return 0;
}
EXPORT_SYMBOL(tegra_mce_read_versions);

/**
 * Enumerate MCE API features
 *
 * @features: output feature vector (4bits each)
 *
 * Returns 0 if success.
 */
int tegra_mce_enum_features(u64 *features)
{
	struct mce_regs regs;
	send_smc(MCE_SMC_ENUM_FEATURES, &regs);
	*features = (u32)regs.args[0];
	return 0;
}
EXPORT_SYMBOL(tegra_mce_enum_features);

#ifdef CONFIG_DEBUG_FS

#define CSTAT_ENTRY(stat) [TEGRA_MCE_CSTATS_##stat] = #stat

static const char * const cstats_table[] = {
	CSTAT_ENTRY(ENTRIES_SC7),
	CSTAT_ENTRY(ENTRIES_SC4),
	CSTAT_ENTRY(ENTRIES_SC3),
	CSTAT_ENTRY(ENTRIES_SC2),
	CSTAT_ENTRY(ENTRIES_CCP3),
	CSTAT_ENTRY(ENTRIES_A57_CC6),
	CSTAT_ENTRY(ENTRIES_A57_CC7),
	CSTAT_ENTRY(ENTRIES_D15_CC6),
	CSTAT_ENTRY(ENTRIES_D15_CC7),
	CSTAT_ENTRY(ENTRIES_D15_CORE0_C6),
	CSTAT_ENTRY(ENTRIES_D15_CORE1_C6),
	CSTAT_ENTRY(ENTRIES_D15_CORE0_C7),
	CSTAT_ENTRY(ENTRIES_D15_CORE1_C7),
	CSTAT_ENTRY(ENTRIES_A57_CORE0_C7),
	CSTAT_ENTRY(ENTRIES_A57_CORE1_C7),
	CSTAT_ENTRY(ENTRIES_A57_CORE2_C7),
	CSTAT_ENTRY(ENTRIES_A57_CORE3_C7),
	CSTAT_ENTRY(LAST_ENTRY_D15_CORE0),
	CSTAT_ENTRY(LAST_ENTRY_D15_CORE1),
	CSTAT_ENTRY(LAST_ENTRY_A57_CORE0),
	CSTAT_ENTRY(LAST_ENTRY_A57_CORE1),
	CSTAT_ENTRY(LAST_ENTRY_A57_CORE2),
	CSTAT_ENTRY(LAST_ENTRY_A57_CORE3),
};

static int mce_echo_set(void *data, u64 val)
{
	u32 matched;
	if (tegra_mce_echo_data((u32)val, &matched))
		return -EINVAL;
	return 0;
}

static int mce_versions_get(void *data, u64 *val)
{
	u32 major, minor;
	int ret = tegra_mce_read_versions(&major, &minor);
	if (!ret)
		*val = ((u64)major << 32) | minor;
	return ret;
}

static int mce_features_get(void *data, u64 *val)
{
	return tegra_mce_enum_features(val);
}

static int mce_dbg_cstats_show(struct seq_file *s, void *data)
{
	int st;
	u32 val;
	seq_printf(s, "%-30s%-10s\n", "name", "count");
	seq_printf(s, "----------------------------------------\n");
	for(st = 1; st <= TEGRA_MCE_CSTATS_MAX; st++) {
		if (!cstats_table[st])
			continue;
		if (tegra_mce_read_cstate_stats(st, &val)) {
			pr_err("mce: failed to read cstat: %d\n", st);
			break;
		}
		seq_printf(s, "%-30s%-10d\n", cstats_table[st], val);
	}
	return 0;
}

static int mce_dbg_cstats_open(struct inode *inode, struct file *file)
{
	return single_open(file, mce_dbg_cstats_show, inode->i_private);
}

static const struct file_operations mce_cstats_fops = {
	.open = mce_dbg_cstats_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

DEFINE_SIMPLE_ATTRIBUTE(mce_echo_fops, NULL, mce_echo_set, "%llu\n");
DEFINE_SIMPLE_ATTRIBUTE(mce_versions_fops, mce_versions_get, NULL, "%llu\n");
DEFINE_SIMPLE_ATTRIBUTE(mce_features_fops, mce_features_get, NULL, "%llu\n");

static struct dentry *mce_debugfs_root;

struct debugfs_entry {
	const char *name;
	const struct file_operations *fops;
	mode_t mode;
};

static struct debugfs_entry mce_dbg_attrs[] = {
	{ "echo", &mce_echo_fops, S_IWUSR },
	{ "versions", &mce_versions_fops, S_IRUGO },
	{ "features", &mce_features_fops, S_IRUGO },
	{ "cstats", &mce_cstats_fops, S_IRUGO },
	{ NULL, NULL, 0 }
};

static __init int mce_debugfs_init(void)
{
	struct dentry *dent;
	struct debugfs_entry *fent;

	mce_debugfs_root = debugfs_create_dir("tegra_mce", NULL);
	if (!mce_debugfs_root)
		return -ENOMEM;

	fent = mce_dbg_attrs;
	while (fent->name) {
		dent = debugfs_create_file(fent->name, fent->mode,
			mce_debugfs_root, NULL, fent->fops);
		if (IS_ERR_OR_NULL(dent))
			goto abort;
		fent++;
	}

	return 0;

abort:
	debugfs_remove_recursive(mce_debugfs_root);
	return -EFAULT;
}
fs_initcall(mce_debugfs_init);

#endif

static __init int tegra_mce_init(void)
{
	u32 major = 0;
	u32 minor = 0;

	/* Skip validation for pre-Si */
	if (!tegra_platform_is_silicon())
		return 0;

	/* Validate ARI version */
	if (tegra_mce_read_versions(&major, &minor))
		panic("tegra-mce: ARI version query failed.\n");

	pr_info("tegra-mce: ARI versions HW=%d:%d, SW=%d:%d\n",
		major, minor, CUR_ARI_VER_MAJOR, CUR_ARI_VER_MINOR);

	if (major > CUR_ARI_VER_MAJOR)
		pr_warn("tegra-mce: ARI HW is newer than SW (%d:%d > %d:%d).\n",
			CUR_ARI_VER_MAJOR, CUR_ARI_VER_MINOR,
			major, minor);

	if (major < CUR_ARI_VER_MAJOR || minor < CUR_ARI_VER_MINOR)
		pr_warn("tegra-mce: ARI HW is older than SW (%d:%d > %d:%d).\n",
			CUR_ARI_VER_MAJOR, CUR_ARI_VER_MINOR,
			major, minor);

	return 0;
}
early_initcall(tegra_mce_init);
