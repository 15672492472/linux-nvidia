/*
 * GP10B L2
 *
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

#include <linux/types.h>

#include "gk20a/gk20a.h"
#include "gm20b/ltc_gm20b.h"
#include "hw_proj_gp10b.h"
#include "hw_mc_gp10b.h"
#include "hw_ltc_gp10b.h"

#include "gk20a/ltc_common.c"

static int gp10b_determine_L2_size_bytes(struct gk20a *g)
{
	u32 tmp;
	int ret;

	gk20a_dbg_fn("");

	tmp = gk20a_readl(g, ltc_ltc0_lts0_tstg_info_1_r());

	ret = g->ltc_count *
		ltc_ltc0_lts0_tstg_info_1_slice_size_in_kb_v(tmp)*1024 *
		ltc_ltc0_lts0_tstg_info_1_slices_per_l2_v(tmp);

	gk20a_dbg(gpu_dbg_info, "L2 size: %d\n", ret);

	gk20a_dbg_fn("done");

	return ret;
}

static int gp10b_ltc_init_comptags(struct gk20a *g, struct gr_gk20a *gr)
{
	/* max memory size (MB) to cover */
	u32 max_size = gr->max_comptag_mem;
	/* one tag line covers 64KB */
	u32 max_comptag_lines = max_size << 4;

	u32 hw_max_comptag_lines =
		ltc_ltcs_ltss_cbc_ctrl3_clear_upper_bound_init_v();

	u32 cbc_param =
		gk20a_readl(g, ltc_ltcs_ltss_cbc_param_r());
	u32 comptags_per_cacheline =
		ltc_ltcs_ltss_cbc_param_comptags_per_cache_line_v(cbc_param);
	u32 cacheline_size =
		512 << ltc_ltcs_ltss_cbc_param_cache_line_size_v(cbc_param);
	u32 slices_per_ltc =
		ltc_ltcs_ltss_cbc_param_slices_per_ltc_v(cbc_param);
	u32 cbc_param2 =
		gk20a_readl(g, ltc_ltcs_ltss_cbc_param2_r());
	u32 gobs_per_comptagline_per_slice =
		ltc_ltcs_ltss_cbc_param2_gobs_per_comptagline_per_slice_v(cbc_param2);

	u32 compbit_backing_size;

	int err;

	gk20a_dbg_fn("");

	if (max_comptag_lines == 0)
		return 0;

	if (max_comptag_lines > hw_max_comptag_lines)
		max_comptag_lines = hw_max_comptag_lines;

	compbit_backing_size =
		DIV_ROUND_UP(max_comptag_lines, comptags_per_cacheline) *
		cacheline_size * slices_per_ltc * g->ltc_count;

	/* aligned to 2KB * ltc_count */
	compbit_backing_size +=
		g->ltc_count << ltc_ltcs_ltss_cbc_base_alignment_shift_v();

	/* must be a multiple of 64KB */
	compbit_backing_size = roundup(compbit_backing_size, 64*1024);

	max_comptag_lines =
		(compbit_backing_size * comptags_per_cacheline) /
		(cacheline_size * slices_per_ltc * g->ltc_count);

	if (max_comptag_lines > hw_max_comptag_lines)
		max_comptag_lines = hw_max_comptag_lines;

	gk20a_dbg_info("compbit backing store size : %d",
		compbit_backing_size);
	gk20a_dbg_info("max comptag lines : %d",
		max_comptag_lines);
	gk20a_dbg_info("gobs_per_comptagline_per_slice: %d",
		gobs_per_comptagline_per_slice);

	if (tegra_platform_is_linsim())
		err = gk20a_ltc_alloc_phys_cbc(g, compbit_backing_size);
	else
		err = gk20a_ltc_alloc_virt_cbc(g, compbit_backing_size);

	if (err)
		return err;

	gk20a_allocator_init(&gr->comp_tags, "comptag",
			      1, /* start */
			      max_comptag_lines - 1); /* length*/

	gr->comptags_per_cacheline = comptags_per_cacheline;
	gr->slices_per_ltc = slices_per_ltc;
	gr->cacheline_size = cacheline_size;
	gr->gobs_per_comptagline_per_slice = gobs_per_comptagline_per_slice;

	return 0;
}

void gp10b_ltc_isr(struct gk20a *g)
{
	u32 mc_intr, ltc_intr;
	int ltc, slice;

	mc_intr = gk20a_readl(g, mc_intr_ltc_r());
	gk20a_err(dev_from_gk20a(g), "mc_ltc_intr: %08x",
		  mc_intr);
	for (ltc = 0; ltc < g->ltc_count; ltc++) {
		if ((mc_intr & 1 << ltc) == 0)
			continue;
		for (slice = 0; slice < g->gr.slices_per_ltc; slice++) {
			ltc_intr = gk20a_readl(g, ltc_ltc0_lts0_intr_r() +
					   proj_ltc_stride_v() * ltc +
					   proj_lts_stride_v() * slice);
			gk20a_err(dev_from_gk20a(g), "ltc%d, slice %d: %08x",
				  ltc, slice, ltc_intr);
			gk20a_writel(g, ltc_ltc0_lts0_intr_r() +
					   proj_ltc_stride_v() * ltc +
					   proj_lts_stride_v() * slice,
				     ltc_intr);
		}
	}
}

void gp10b_init_ltc(struct gpu_ops *gops)
{
	gops->ltc.determine_L2_size_bytes = gp10b_determine_L2_size_bytes;
	gops->ltc.set_max_ways_evict_last = gk20a_ltc_set_max_ways_evict_last;
	gops->ltc.set_zbc_color_entry = gk20a_ltc_set_zbc_color_entry;
	gops->ltc.set_zbc_depth_entry = gk20a_ltc_set_zbc_depth_entry;
	gops->ltc.init_cbc = gk20a_ltc_init_cbc;

	/* GM20b specific ops. */
	gops->ltc.init_fs_state = gm20b_ltc_init_fs_state;
	gops->ltc.init_comptags = gp10b_ltc_init_comptags;
	gops->ltc.cbc_ctrl = gm20b_ltc_cbc_ctrl;
	gops->ltc.elpg_flush = gm20b_ltc_g_elpg_flush_locked;
	gops->ltc.isr = gp10b_ltc_isr;
	gops->ltc.cbc_fix_config = gm20b_ltc_cbc_fix_config;
	gops->ltc.flush = gm20b_flush_ltc;
#ifdef CONFIG_DEBUG_FS
	gops->ltc.sync_debugfs = gk20a_ltc_sync_debugfs;
#endif
}

