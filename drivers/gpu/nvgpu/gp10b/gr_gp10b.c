/*
 * GP10B GPU GR
 *
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

#include "gk20a/gk20a.h" /* FERMI and MAXWELL classes defined here */

#include "gk20a/gr_gk20a.h"

#include "gm20b/gr_gm20b.h" /* for MAXWELL classes */
#include "gp10b/gr_gp10b.h"
#include "hw_gr_gp10b.h"
#include "hw_fifo_gp10b.h"
#include "hw_proj_gp10b.h"
#include "hw_ctxsw_prog_gp10b.h"

static bool gr_gp10b_is_valid_class(struct gk20a *g, u32 class_num)
{
	bool valid = false;

	switch (class_num) {
	case PASCAL_COMPUTE_A:
	case PASCAL_A:
	case PASCAL_DMA_COPY_A:
		valid = true;
		break;

	case MAXWELL_COMPUTE_B:
	case MAXWELL_B:
	case FERMI_TWOD_A:
	case KEPLER_DMA_COPY_A:
	case MAXWELL_DMA_COPY_A:
		valid = true;
		break;

	default:
		break;
	}
	gk20a_dbg_info("class=0x%x valid=%d", class_num, valid);
	return valid;
}

static int gr_gp10b_commit_global_cb_manager(struct gk20a *g,
			struct channel_gk20a *c, bool patch)
{
	struct gr_gk20a *gr = &g->gr;
	struct channel_ctx_gk20a *ch_ctx = NULL;
	u32 attrib_offset_in_chunk = 0;
	u32 alpha_offset_in_chunk = 0;
	u32 pd_ab_max_output;
	u32 gpc_index, ppc_index;
	u32 temp;
	u32 cbm_cfg_size1, cbm_cfg_size2;

	gk20a_dbg_fn("");

	if (patch) {
		int err;
		ch_ctx = &c->ch_ctx;
		err = gr_gk20a_ctx_patch_write_begin(g, ch_ctx);
		if (err)
			return err;
	}

	gr_gk20a_ctx_patch_write(g, ch_ctx, gr_ds_tga_constraintlogic_beta_r(),
		gr->attrib_cb_default_size, patch);
	gr_gk20a_ctx_patch_write(g, ch_ctx, gr_ds_tga_constraintlogic_alpha_r(),
		gr->alpha_cb_default_size, patch);

	pd_ab_max_output = (gr->alpha_cb_default_size *
		gr_gpc0_ppc0_cbm_beta_cb_size_v_granularity_v()) /
		gr_pd_ab_dist_cfg1_max_output_granularity_v();

	gr_gk20a_ctx_patch_write(g, ch_ctx, gr_pd_ab_dist_cfg1_r(),
		gr_pd_ab_dist_cfg1_max_output_f(pd_ab_max_output) |
		gr_pd_ab_dist_cfg1_max_batches_init_f(), patch);

	attrib_offset_in_chunk = alpha_offset_in_chunk +
		gr->tpc_count * gr->alpha_cb_size;

	for (gpc_index = 0; gpc_index < gr->gpc_count; gpc_index++) {
		temp = proj_gpc_stride_v() * gpc_index;
		for (ppc_index = 0; ppc_index < gr->gpc_ppc_count[gpc_index];
		     ppc_index++) {
			cbm_cfg_size1 = gr->attrib_cb_default_size *
				gr->pes_tpc_count[ppc_index][gpc_index];
			cbm_cfg_size2 = gr->alpha_cb_default_size *
				gr->pes_tpc_count[ppc_index][gpc_index];

			gr_gk20a_ctx_patch_write(g, ch_ctx,
				gr_gpc0_ppc0_cbm_beta_cb_size_r() + temp +
				proj_ppc_in_gpc_stride_v() * ppc_index,
				cbm_cfg_size1, patch);

			gr_gk20a_ctx_patch_write(g, ch_ctx,
				gr_gpc0_ppc0_cbm_beta_cb_offset_r() + temp +
				proj_ppc_in_gpc_stride_v() * ppc_index,
				attrib_offset_in_chunk, patch);

			gr_gk20a_ctx_patch_write(g, ch_ctx,
				gr_gpc0_ppc0_cbm_beta_steady_state_cb_size_r() + temp +
				proj_ppc_in_gpc_stride_v() * ppc_index,
				gr_gpc0_ppc0_cbm_beta_steady_state_cb_size_v_default_v(),
				patch);

			attrib_offset_in_chunk += gr->attrib_cb_size *
				gr->pes_tpc_count[ppc_index][gpc_index];

			gr_gk20a_ctx_patch_write(g, ch_ctx,
				gr_gpc0_ppc0_cbm_alpha_cb_size_r() + temp +
				proj_ppc_in_gpc_stride_v() * ppc_index,
				cbm_cfg_size2, patch);

			gr_gk20a_ctx_patch_write(g, ch_ctx,
				gr_gpc0_ppc0_cbm_alpha_cb_offset_r() + temp +
				proj_ppc_in_gpc_stride_v() * ppc_index,
				alpha_offset_in_chunk, patch);

			alpha_offset_in_chunk += gr->alpha_cb_size *
				gr->pes_tpc_count[ppc_index][gpc_index];

			gr_gk20a_ctx_patch_write(g, ch_ctx,
				gr_gpcs_swdx_tc_beta_cb_size_r(ppc_index + gpc_index),
				gr_gpcs_swdx_tc_beta_cb_size_v_f(cbm_cfg_size1),
				patch);
		}
	}

	if (patch)
		gr_gk20a_ctx_patch_write_end(g, ch_ctx);

	return 0;
}

static void gr_gp10b_commit_global_pagepool(struct gk20a *g,
					    struct channel_ctx_gk20a *ch_ctx,
					    u64 addr, u32 size, bool patch)
{
	gr_gk20a_ctx_patch_write(g, ch_ctx, gr_scc_pagepool_base_r(),
		gr_scc_pagepool_base_addr_39_8_f(addr), patch);

	gr_gk20a_ctx_patch_write(g, ch_ctx, gr_scc_pagepool_r(),
		gr_scc_pagepool_total_pages_f(size) |
		gr_scc_pagepool_valid_true_f(), patch);

	gr_gk20a_ctx_patch_write(g, ch_ctx, gr_gpcs_gcc_pagepool_base_r(),
		gr_gpcs_gcc_pagepool_base_addr_39_8_f(addr), patch);

	gr_gk20a_ctx_patch_write(g, ch_ctx, gr_gpcs_gcc_pagepool_r(),
		gr_gpcs_gcc_pagepool_total_pages_f(size), patch);
}

static int gr_gp10b_add_zbc_color(struct gk20a *g, struct gr_gk20a *gr,
				  struct zbc_entry *color_val, u32 index)
{
	u32 i;
	u32 zbc_c;

	/* update l2 table */
	g->ops.ltc.set_zbc_color_entry(g, color_val, index);

	/* update ds table */
	gk20a_writel(g, gr_ds_zbc_color_r_r(),
		gr_ds_zbc_color_r_val_f(color_val->color_ds[0]));
	gk20a_writel(g, gr_ds_zbc_color_g_r(),
		gr_ds_zbc_color_g_val_f(color_val->color_ds[1]));
	gk20a_writel(g, gr_ds_zbc_color_b_r(),
		gr_ds_zbc_color_b_val_f(color_val->color_ds[2]));
	gk20a_writel(g, gr_ds_zbc_color_a_r(),
		gr_ds_zbc_color_a_val_f(color_val->color_ds[3]));

	gk20a_writel(g, gr_ds_zbc_color_fmt_r(),
		gr_ds_zbc_color_fmt_val_f(color_val->format));

	gk20a_writel(g, gr_ds_zbc_tbl_index_r(),
		gr_ds_zbc_tbl_index_val_f(index + GK20A_STARTOF_ZBC_TABLE));

	/* trigger the write */
	gk20a_writel(g, gr_ds_zbc_tbl_ld_r(),
		gr_ds_zbc_tbl_ld_select_c_f() |
		gr_ds_zbc_tbl_ld_action_write_f() |
		gr_ds_zbc_tbl_ld_trigger_active_f());

	/* update local copy */
	for (i = 0; i < GK20A_ZBC_COLOR_VALUE_SIZE; i++) {
		gr->zbc_col_tbl[index].color_l2[i] = color_val->color_l2[i];
		gr->zbc_col_tbl[index].color_ds[i] = color_val->color_ds[i];
	}
	gr->zbc_col_tbl[index].format = color_val->format;
	gr->zbc_col_tbl[index].ref_cnt++;

	gk20a_writel(g, gr_gpcs_swdx_dss_zbc_color_r_r(index), color_val->color_ds[0]);
	gk20a_writel(g, gr_gpcs_swdx_dss_zbc_color_g_r(index), color_val->color_ds[1]);
	gk20a_writel(g, gr_gpcs_swdx_dss_zbc_color_b_r(index), color_val->color_ds[2]);
	gk20a_writel(g, gr_gpcs_swdx_dss_zbc_color_a_r(index), color_val->color_ds[3]);
	zbc_c = gk20a_readl(g, gr_gpcs_swdx_dss_zbc_c_01_to_04_format_r() + (index & ~3));
	zbc_c &= ~(0x7f << ((index % 4) * 7));
	zbc_c |= color_val->format << ((index % 4) * 7);
	gk20a_writel(g, gr_gpcs_swdx_dss_zbc_c_01_to_04_format_r() + (index & ~3), zbc_c);

	return 0;
}

static int gr_gp10b_add_zbc_depth(struct gk20a *g, struct gr_gk20a *gr,
				struct zbc_entry *depth_val, u32 index)
{
	u32 zbc_z;

	/* update l2 table */
	g->ops.ltc.set_zbc_depth_entry(g, depth_val, index);

	/* update ds table */
	gk20a_writel(g, gr_ds_zbc_z_r(),
		gr_ds_zbc_z_val_f(depth_val->depth));

	gk20a_writel(g, gr_ds_zbc_z_fmt_r(),
		gr_ds_zbc_z_fmt_val_f(depth_val->format));

	gk20a_writel(g, gr_ds_zbc_tbl_index_r(),
		gr_ds_zbc_tbl_index_val_f(index + GK20A_STARTOF_ZBC_TABLE));

	/* trigger the write */
	gk20a_writel(g, gr_ds_zbc_tbl_ld_r(),
		gr_ds_zbc_tbl_ld_select_z_f() |
		gr_ds_zbc_tbl_ld_action_write_f() |
		gr_ds_zbc_tbl_ld_trigger_active_f());

	/* update local copy */
	gr->zbc_dep_tbl[index].depth = depth_val->depth;
	gr->zbc_dep_tbl[index].format = depth_val->format;
	gr->zbc_dep_tbl[index].ref_cnt++;

	gk20a_writel(g, gr_gpcs_swdx_dss_zbc_z_r(index), depth_val->depth);
	zbc_z = gk20a_readl(g, gr_gpcs_swdx_dss_zbc_z_01_to_04_format_r() + (index & ~3));
	zbc_z &= ~(0x7f << (index % 4) * 7);
	zbc_z |= depth_val->format << (index % 4) * 7;
	gk20a_writel(g, gr_gpcs_swdx_dss_zbc_z_01_to_04_format_r() + (index & ~3), zbc_z);

	return 0;
}

static u32 gr_gp10b_pagepool_default_size(struct gk20a *g)
{
	return gr_scc_pagepool_total_pages_hwmax_value_v();
}

static int gr_gp10b_calc_global_ctx_buffer_size(struct gk20a *g)
{
	struct gr_gk20a *gr = &g->gr;
	int size;

	gr->attrib_cb_size = gr->attrib_cb_default_size
		+ (gr->attrib_cb_default_size >> 1);
	gr->alpha_cb_size = gr->alpha_cb_default_size
		+ (gr->alpha_cb_default_size >> 1);

	gr->attrib_cb_size = min(gr->attrib_cb_size,
				 gr_gpc0_ppc0_cbm_beta_cb_size_v_f(0xffffffff));
	gr->alpha_cb_size = min(gr->attrib_cb_size,
				 gr_gpc0_ppc0_cbm_alpha_cb_size_v_f(0xffffffff));

	size = gr->attrib_cb_size *
		gr_gpc0_ppc0_cbm_beta_cb_size_v_granularity_v() *
		gr->max_tpc_count;

	size += gr->alpha_cb_size *
		gr_gpc0_ppc0_cbm_alpha_cb_size_v_granularity_v() *
		gr->max_tpc_count;

	size = ALIGN(size, 128);

	return size;
}

static int gr_gp10b_handle_sw_method(struct gk20a *g, u32 addr,
				     u32 class_num, u32 offset, u32 data)
{
	gk20a_dbg_fn("");

	if (class_num == PASCAL_COMPUTE_A) {
		switch (offset << 2) {
		case NVC0C0_SET_SHADER_EXCEPTIONS:
			gk20a_gr_set_shader_exceptions(g, data);
			break;
		default:
			goto fail;
		}
	}

	if (class_num == PASCAL_A) {
		switch (offset << 2) {
		case NVC097_SET_SHADER_EXCEPTIONS:
			gk20a_gr_set_shader_exceptions(g, data);
			break;
		case NVC097_SET_CIRCULAR_BUFFER_SIZE:
			g->ops.gr.set_circular_buffer_size(g, data);
			break;
		case NVC097_SET_ALPHA_CIRCULAR_BUFFER_SIZE:
			g->ops.gr.set_alpha_circular_buffer_size(g, data);
			break;
		default:
			goto fail;
		}
	}
	return 0;

fail:
	return -EINVAL;
}

static void gr_gp10b_cb_size_default(struct gk20a *g)
{
	struct gr_gk20a *gr = &g->gr;

	if (!gr->attrib_cb_default_size)
		gr->attrib_cb_default_size =
			gr_gpc0_ppc0_cbm_beta_cb_size_v_default_v();
	gr->alpha_cb_default_size =
		gr_gpc0_ppc0_cbm_alpha_cb_size_v_default_v();
}

static void gr_gp10b_set_alpha_circular_buffer_size(struct gk20a *g, u32 data)
{
	struct gr_gk20a *gr = &g->gr;
	u32 gpc_index, ppc_index, stride, val;
	u32 pd_ab_max_output;
	u32 alpha_cb_size = data * 4;

	gk20a_dbg_fn("");

	if (alpha_cb_size > gr->alpha_cb_size)
		alpha_cb_size = gr->alpha_cb_size;

	gk20a_writel(g, gr_ds_tga_constraintlogic_alpha_r(),
		(gk20a_readl(g, gr_ds_tga_constraintlogic_alpha_r()) &
		 ~gr_ds_tga_constraintlogic_alpha_cbsize_f(~0)) |
		 gr_ds_tga_constraintlogic_alpha_cbsize_f(alpha_cb_size));

	pd_ab_max_output = alpha_cb_size *
		gr_gpc0_ppc0_cbm_alpha_cb_size_v_granularity_v() /
		gr_pd_ab_dist_cfg1_max_output_granularity_v();

	gk20a_writel(g, gr_pd_ab_dist_cfg1_r(),
		gr_pd_ab_dist_cfg1_max_output_f(pd_ab_max_output));

	for (gpc_index = 0; gpc_index < gr->gpc_count; gpc_index++) {
		stride = proj_gpc_stride_v() * gpc_index;

		for (ppc_index = 0; ppc_index < gr->gpc_ppc_count[gpc_index];
			ppc_index++) {

			val = gk20a_readl(g, gr_gpc0_ppc0_cbm_alpha_cb_size_r() +
				stride +
				proj_ppc_in_gpc_stride_v() * ppc_index);

			val = set_field(val, gr_gpc0_ppc0_cbm_alpha_cb_size_v_m(),
					gr_gpc0_ppc0_cbm_alpha_cb_size_v_f(alpha_cb_size *
						gr->pes_tpc_count[ppc_index][gpc_index]));

			gk20a_writel(g, gr_gpc0_ppc0_cbm_alpha_cb_size_r() +
				stride +
				proj_ppc_in_gpc_stride_v() * ppc_index, val);
		}
	}
}

static void gr_gp10b_set_circular_buffer_size(struct gk20a *g, u32 data)
{
	struct gr_gk20a *gr = &g->gr;
	u32 gpc_index, ppc_index, stride, val;
	u32 cb_size = data * 4;

	gk20a_dbg_fn("");

	if (cb_size > gr->attrib_cb_size)
		cb_size = gr->attrib_cb_size;

	gk20a_writel(g, gr_ds_tga_constraintlogic_beta_r(),
		(gk20a_readl(g, gr_ds_tga_constraintlogic_beta_r()) &
		 ~gr_ds_tga_constraintlogic_beta_cbsize_f(~0)) |
		 gr_ds_tga_constraintlogic_beta_cbsize_f(cb_size));

	for (gpc_index = 0; gpc_index < gr->gpc_count; gpc_index++) {
		stride = proj_gpc_stride_v() * gpc_index;

		for (ppc_index = 0; ppc_index < gr->gpc_ppc_count[gpc_index];
			ppc_index++) {

			val = gk20a_readl(g, gr_gpc0_ppc0_cbm_beta_cb_size_r() +
				stride +
				proj_ppc_in_gpc_stride_v() * ppc_index);

			val = set_field(val,
				gr_gpc0_ppc0_cbm_beta_cb_size_v_m(),
				gr_gpc0_ppc0_cbm_beta_cb_size_v_f(cb_size *
					gr->pes_tpc_count[ppc_index][gpc_index]));

			gk20a_writel(g, gr_gpc0_ppc0_cbm_beta_cb_size_r() +
				stride +
				proj_ppc_in_gpc_stride_v() * ppc_index, val);

			val = gk20a_readl(g, gr_gpcs_swdx_tc_beta_cb_size_r(
						ppc_index + gpc_index));

			val = set_field(val,
				gr_gpcs_swdx_tc_beta_cb_size_v_m(),
				gr_gpcs_swdx_tc_beta_cb_size_v_f(cb_size *
					gr->gpc_ppc_count[gpc_index]));

			gk20a_writel(g, gr_gpcs_swdx_tc_beta_cb_size_r(
						ppc_index + gpc_index), val);
		}
	}
}

static int gr_gp10b_init_ctx_state(struct gk20a *g)
{
	struct fecs_method_op_gk20a op = {
		.mailbox = { .id = 0, .data = 0,
			     .clr = ~0, .ok = 0, .fail = 0},
		.method.data = 0,
		.cond.ok = GR_IS_UCODE_OP_NOT_EQUAL,
		.cond.fail = GR_IS_UCODE_OP_SKIP,
		};
	int err;

	gk20a_dbg_fn("");

	err = gr_gk20a_init_ctx_state(g);
	if (err)
		return err;

	if (!g->gr.t18x.ctx_vars.preempt_image_size) {
		op.method.addr =
			gr_fecs_method_push_adr_discover_preemption_image_size_v();
		op.mailbox.ret = &g->gr.t18x.ctx_vars.preempt_image_size;
		err = gr_gk20a_submit_fecs_method_op(g, op, false);
		if (err) {
			gk20a_err(dev_from_gk20a(g),
					"query preempt image size failed");
			return err;
		}
	}

	gk20a_dbg_info("preempt image size: %u",
		g->gr.t18x.ctx_vars.preempt_image_size);

	gk20a_dbg_fn("done");

	return 0;
}

static int gr_gp10b_alloc_gr_ctx(struct gk20a *g,
			  struct gr_ctx_desc **gr_ctx, struct vm_gk20a *vm,
			  u32 class,
			  u32 flags)
{
	int err;

	gk20a_dbg_fn("");

	err = gr_gk20a_alloc_gr_ctx(g, gr_ctx, vm, class, flags);
	if (err)
		return err;

	if (flags == NVGPU_GR_PREEMPTION_MODE_GFXP) {
		u32 spill_size =
			gr_gpc0_swdx_rm_spill_buffer_size_256b_default_v();
		u32 betacb_size = ALIGN(
			(gr_gpc0_ppc0_cbm_beta_cb_size_v_gfxp_v() *
			 gr_gpc0_ppc0_cbm_beta_cb_size_v_granularity_v() *
			 g->gr.max_tpc_count) +
			(g->gr.alpha_cb_size *
			 gr_gpc0_ppc0_cbm_beta_cb_size_v_granularity_v() *
			 g->gr.max_tpc_count),
			128);
		u32 pagepool_size = g->ops.gr.pagepool_default_size(g) *
			gr_scc_pagepool_total_pages_byte_granularity_v();

		err = gk20a_gmmu_alloc_map(vm, g->gr.t18x.ctx_vars.preempt_image_size,
				&(*gr_ctx)->t18x.preempt_ctxsw_buffer);
		if (err) {
			gk20a_err(dev_from_gk20a(vm->mm->g),
				  "cannot allocate preempt buffer");
			goto fail_free_gk20a_ctx;
		}

		err = gk20a_gmmu_alloc_map(vm, spill_size,
				&(*gr_ctx)->t18x.spill_ctxsw_buffer);
		if (err) {
			gk20a_err(dev_from_gk20a(vm->mm->g),
				  "cannot allocate spill buffer");
			goto fail_free_preempt;
		}

		err = gk20a_gmmu_alloc_map(vm, betacb_size,
					   &(*gr_ctx)->t18x.betacb_ctxsw_buffer);
		if (err) {
			gk20a_err(dev_from_gk20a(vm->mm->g),
				  "cannot allocate beta buffer");
			goto fail_free_spill;
		}

		err = gk20a_gmmu_alloc_map(vm, pagepool_size,
					   &(*gr_ctx)->t18x.pagepool_ctxsw_buffer);
		if (err) {
			gk20a_err(dev_from_gk20a(vm->mm->g),
				  "cannot allocate page pool");
			goto fail_free_betacb;
		}

		(*gr_ctx)->preempt_mode = flags;
	}

	if (class == PASCAL_COMPUTE_A) {
		if (flags == NVGPU_GR_PREEMPTION_MODE_CILP)
			(*gr_ctx)->preempt_mode = NVGPU_GR_PREEMPTION_MODE_CILP;
		else
			(*gr_ctx)->preempt_mode = NVGPU_GR_PREEMPTION_MODE_CTA;
	}

	gk20a_dbg_fn("done");

	return err;

fail_free_betacb:
	gk20a_gmmu_unmap_free(vm, &(*gr_ctx)->t18x.betacb_ctxsw_buffer);
fail_free_spill:
	gk20a_gmmu_unmap_free(vm, &(*gr_ctx)->t18x.spill_ctxsw_buffer);
fail_free_preempt:
	gk20a_gmmu_unmap_free(vm, &(*gr_ctx)->t18x.preempt_ctxsw_buffer);
fail_free_gk20a_ctx:
	gr_gk20a_free_gr_ctx(g, vm, *gr_ctx);
	*gr_ctx = NULL;

	return err;
}

static void gr_gp10b_free_gr_ctx(struct gk20a *g, struct vm_gk20a *vm,
			  struct gr_ctx_desc *gr_ctx)
{
	gk20a_dbg_fn("");

	if (!gr_ctx)
		return;

	gk20a_gmmu_unmap_free(vm, &gr_ctx->t18x.pagepool_ctxsw_buffer);
	gk20a_gmmu_unmap_free(vm, &gr_ctx->t18x.betacb_ctxsw_buffer);
	gk20a_gmmu_unmap_free(vm, &gr_ctx->t18x.spill_ctxsw_buffer);
	gk20a_gmmu_unmap_free(vm, &gr_ctx->t18x.preempt_ctxsw_buffer);
	gr_gk20a_free_gr_ctx(g, vm, gr_ctx);

	gk20a_dbg_fn("done");
}

static void gr_gp10b_update_ctxsw_preemption_mode(struct gk20a *g,
		struct channel_ctx_gk20a *ch_ctx,
		void *ctx_ptr)
{
	struct gr_ctx_desc *gr_ctx = ch_ctx->gr_ctx;
	u32 gfxp_preempt_option =
		ctxsw_prog_main_image_graphics_preemption_options_control_gfxp_f();
	u32 cilp_preempt_option =
		ctxsw_prog_main_image_compute_preemption_options_control_cilp_f();
	int err;

	gk20a_dbg_fn("");

	if (gr_ctx->preempt_mode == NVGPU_GR_PREEMPTION_MODE_GFXP) {
		gk20a_dbg_info("GfxP: %x", gfxp_preempt_option);
		gk20a_mem_wr32(ctx_ptr + ctxsw_prog_main_image_graphics_preemption_options_o(), 0,
				gfxp_preempt_option);
	}

	if (gr_ctx->preempt_mode == NVGPU_GR_PREEMPTION_MODE_CILP) {
		gk20a_dbg_info("CILP: %x", cilp_preempt_option);
		gk20a_mem_wr32(ctx_ptr + ctxsw_prog_main_image_compute_preemption_options_o(), 0,
				cilp_preempt_option);
	}

	if (gr_ctx->t18x.preempt_ctxsw_buffer.gpu_va) {
		u32 addr;
		u32 size;
		u32 cbes_reserve;

		gk20a_mem_wr32(ctx_ptr + ctxsw_prog_main_image_full_preemption_ptr_o(), 0,
				gr_ctx->t18x.preempt_ctxsw_buffer.gpu_va >> 8);

		err = gr_gk20a_ctx_patch_write_begin(g, ch_ctx);

		addr = (u64_lo32(gr_ctx->t18x.betacb_ctxsw_buffer.gpu_va) >>
			gr_gpcs_setup_attrib_cb_base_addr_39_12_align_bits_v()) |
			(u64_hi32(gr_ctx->t18x.betacb_ctxsw_buffer.gpu_va) <<
			 (32 - gr_gpcs_setup_attrib_cb_base_addr_39_12_align_bits_v()));

		gk20a_dbg_info("attrib cb addr : 0x%016x", addr);
		g->ops.gr.commit_global_attrib_cb(g, ch_ctx, addr, true);

		addr = (u64_lo32(gr_ctx->t18x.pagepool_ctxsw_buffer.gpu_va) >>
			gr_scc_pagepool_base_addr_39_8_align_bits_v()) |
			(u64_hi32(gr_ctx->t18x.pagepool_ctxsw_buffer.gpu_va) <<
			 (32 - gr_scc_pagepool_base_addr_39_8_align_bits_v()));
		size = gr_ctx->t18x.pagepool_ctxsw_buffer.size;
		g->ops.gr.commit_global_pagepool(g, ch_ctx, addr, size, true);

		addr = (u64_lo32(gr_ctx->t18x.spill_ctxsw_buffer.gpu_va) >>
			gr_gpc0_swdx_rm_spill_buffer_addr_39_8_align_bits_v()) |
			(u64_hi32(gr_ctx->t18x.spill_ctxsw_buffer.gpu_va) <<
			 (32 - gr_gpc0_swdx_rm_spill_buffer_addr_39_8_align_bits_v()));
		size = gr_ctx->t18x.spill_ctxsw_buffer.size;

		gr_gk20a_ctx_patch_write(g, ch_ctx,
				gr_gpc0_swdx_rm_spill_buffer_addr_r(),
				gr_gpc0_swdx_rm_spill_buffer_addr_39_8_f(addr),
				true);
		gr_gk20a_ctx_patch_write(g, ch_ctx,
				gr_gpc0_swdx_rm_spill_buffer_size_r(),
				gr_gpc0_swdx_rm_spill_buffer_size_256b_f(size),
				true);

		cbes_reserve = gr_gpcs_swdx_beta_cb_ctrl_cbes_reserve_gfxp_v();
		gr_gk20a_ctx_patch_write(g, ch_ctx,
				gr_gpcs_swdx_beta_cb_ctrl_r(),
				gr_gpcs_swdx_beta_cb_ctrl_cbes_reserve_f(
					cbes_reserve),
				true);
		gr_gk20a_ctx_patch_write(g, ch_ctx,
				gr_gpcs_ppcs_cbm_beta_cb_ctrl_r(),
				gr_gpcs_ppcs_cbm_beta_cb_ctrl_cbes_reserve_f(
					cbes_reserve),
				true);

		gr_gk20a_ctx_patch_write_end(g, ch_ctx);
	}

	gk20a_dbg_fn("done");
}

static int gr_gp10b_dump_gr_status_regs(struct gk20a *g,
			   struct gk20a_debug_output *o)
{
	struct gr_gk20a *gr = &g->gr;

	gk20a_debug_output(o, "NV_PGRAPH_STATUS: 0x%x\n",
		gk20a_readl(g, gr_status_r()));
	gk20a_debug_output(o, "NV_PGRAPH_STATUS1: 0x%x\n",
		gk20a_readl(g, gr_status_1_r()));
	gk20a_debug_output(o, "NV_PGRAPH_STATUS2: 0x%x\n",
		gk20a_readl(g, gr_status_2_r()));
	gk20a_debug_output(o, "NV_PGRAPH_ENGINE_STATUS: 0x%x\n",
		gk20a_readl(g, gr_engine_status_r()));
	gk20a_debug_output(o, "NV_PGRAPH_GRFIFO_STATUS : 0x%x\n",
		gk20a_readl(g, gr_gpfifo_status_r()));
	gk20a_debug_output(o, "NV_PGRAPH_GRFIFO_CONTROL : 0x%x\n",
		gk20a_readl(g, gr_gpfifo_ctl_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_FECS_HOST_INT_STATUS : 0x%x\n",
		gk20a_readl(g, gr_fecs_host_int_status_r()));
	gk20a_debug_output(o, "NV_PGRAPH_EXCEPTION  : 0x%x\n",
		gk20a_readl(g, gr_exception_r()));
	gk20a_debug_output(o, "NV_PGRAPH_FECS_INTR  : 0x%x\n",
		gk20a_readl(g, gr_fecs_intr_r()));
	gk20a_debug_output(o, "NV_PFIFO_ENGINE_STATUS(GR) : 0x%x\n",
		gk20a_readl(g, fifo_engine_status_r(ENGINE_GR_GK20A)));
	gk20a_debug_output(o, "NV_PGRAPH_ACTIVITY0: 0x%x\n",
		gk20a_readl(g, gr_activity_0_r()));
	gk20a_debug_output(o, "NV_PGRAPH_ACTIVITY1: 0x%x\n",
		gk20a_readl(g, gr_activity_1_r()));
	gk20a_debug_output(o, "NV_PGRAPH_ACTIVITY2: 0x%x\n",
		gk20a_readl(g, gr_activity_2_r()));
	gk20a_debug_output(o, "NV_PGRAPH_ACTIVITY4: 0x%x\n",
		gk20a_readl(g, gr_activity_4_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_SKED_ACTIVITY: 0x%x\n",
		gk20a_readl(g, gr_pri_sked_activity_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_GPC0_GPCCS_GPC_ACTIVITY0: 0x%x\n",
		gk20a_readl(g, gr_pri_gpc0_gpccs_gpc_activity0_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_GPC0_GPCCS_GPC_ACTIVITY1: 0x%x\n",
		gk20a_readl(g, gr_pri_gpc0_gpccs_gpc_activity1_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_GPC0_GPCCS_GPC_ACTIVITY2: 0x%x\n",
		gk20a_readl(g, gr_pri_gpc0_gpccs_gpc_activity2_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_GPC0_GPCCS_GPC_ACTIVITY3: 0x%x\n",
		gk20a_readl(g, gr_pri_gpc0_gpccs_gpc_activity3_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_GPC0_TPC0_TPCCS_TPC_ACTIVITY0: 0x%x\n",
		gk20a_readl(g, gr_pri_gpc0_tpc0_tpccs_tpc_activity_0_r()));
	if (gr->gpc_tpc_count[0] == 2)
		gk20a_debug_output(o, "NV_PGRAPH_PRI_GPC0_TPC1_TPCCS_TPC_ACTIVITY0: 0x%x\n",
			gk20a_readl(g, gr_pri_gpc0_tpc1_tpccs_tpc_activity_0_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_GPC0_TPCS_TPCCS_TPC_ACTIVITY0: 0x%x\n",
		gk20a_readl(g, gr_pri_gpc0_tpcs_tpccs_tpc_activity_0_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_GPCS_GPCCS_GPC_ACTIVITY0: 0x%x\n",
		gk20a_readl(g, gr_pri_gpcs_gpccs_gpc_activity_0_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_GPCS_GPCCS_GPC_ACTIVITY1: 0x%x\n",
		gk20a_readl(g, gr_pri_gpcs_gpccs_gpc_activity_1_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_GPCS_GPCCS_GPC_ACTIVITY2: 0x%x\n",
		gk20a_readl(g, gr_pri_gpcs_gpccs_gpc_activity_2_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_GPCS_GPCCS_GPC_ACTIVITY3: 0x%x\n",
		gk20a_readl(g, gr_pri_gpcs_gpccs_gpc_activity_3_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_GPCS_TPC0_TPCCS_TPC_ACTIVITY0: 0x%x\n",
		gk20a_readl(g, gr_pri_gpcs_tpc0_tpccs_tpc_activity_0_r()));
	if (gr->gpc_tpc_count[0] == 2)
		gk20a_debug_output(o, "NV_PGRAPH_PRI_GPCS_TPC1_TPCCS_TPC_ACTIVITY0: 0x%x\n",
			gk20a_readl(g, gr_pri_gpcs_tpc1_tpccs_tpc_activity_0_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_GPCS_TPCS_TPCCS_TPC_ACTIVITY0: 0x%x\n",
		gk20a_readl(g, gr_pri_gpcs_tpcs_tpccs_tpc_activity_0_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_BE0_BECS_BE_ACTIVITY0: 0x%x\n",
		gk20a_readl(g, gr_pri_be0_becs_be_activity0_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_BE1_BECS_BE_ACTIVITY0: 0x%x\n",
		gk20a_readl(g, gr_pri_be1_becs_be_activity0_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_BES_BECS_BE_ACTIVITY0: 0x%x\n",
		gk20a_readl(g, gr_pri_bes_becs_be_activity0_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_DS_MPIPE_STATUS: 0x%x\n",
		gk20a_readl(g, gr_pri_ds_mpipe_status_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_FE_GO_IDLE_TIMEOUT : 0x%x\n",
		gk20a_readl(g, gr_fe_go_idle_timeout_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_FE_GO_IDLE_INFO : 0x%x\n",
		gk20a_readl(g, gr_pri_fe_go_idle_info_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_GPC0_TPC0_TEX_M_TEX_SUBUNITS_STATUS: 0x%x\n",
		gk20a_readl(g, gr_pri_gpc0_tpc0_tex_m_tex_subunits_status_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_CWD_FS: 0x%x\n",
		gk20a_readl(g, gr_cwd_fs_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_FE_TPC_FS: 0x%x\n",
		gk20a_readl(g, gr_fe_tpc_fs_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_CWD_GPC_TPC_ID(0): 0x%x\n",
		gk20a_readl(g, gr_cwd_gpc_tpc_id_r(0)));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_CWD_SM_ID(0): 0x%x\n",
		gk20a_readl(g, gr_cwd_sm_id_r(0)));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_FECS_CTXSW_STATUS_FE_0: 0x%x\n",
		gk20a_readl(g, gr_fecs_ctxsw_status_fe_0_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_FECS_CTXSW_STATUS_1: 0x%x\n",
		gk20a_readl(g, gr_fecs_ctxsw_status_1_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_GPC0_GPCCS_CTXSW_STATUS_GPC_0: 0x%x\n",
		gk20a_readl(g, gr_gpc0_gpccs_ctxsw_status_gpc_0_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_GPC0_GPCCS_CTXSW_STATUS_1: 0x%x\n",
		gk20a_readl(g, gr_gpc0_gpccs_ctxsw_status_1_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_FECS_CTXSW_IDLESTATE : 0x%x\n",
		gk20a_readl(g, gr_fecs_ctxsw_idlestate_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_GPC0_GPCCS_CTXSW_IDLESTATE : 0x%x\n",
		gk20a_readl(g, gr_gpc0_gpccs_ctxsw_idlestate_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_FECS_CURRENT_CTX : 0x%x\n",
		gk20a_readl(g, gr_fecs_current_ctx_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_FECS_NEW_CTX : 0x%x\n",
		gk20a_readl(g, gr_fecs_new_ctx_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_BE0_CROP_STATUS1 : 0x%x\n",
		gk20a_readl(g, gr_pri_be0_crop_status1_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_BES_CROP_STATUS1 : 0x%x\n",
		gk20a_readl(g, gr_pri_bes_crop_status1_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_BE0_ZROP_STATUS : 0x%x\n",
		gk20a_readl(g, gr_pri_be0_zrop_status_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_BE0_ZROP_STATUS2 : 0x%x\n",
		gk20a_readl(g, gr_pri_be0_zrop_status2_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_BES_ZROP_STATUS : 0x%x\n",
		gk20a_readl(g, gr_pri_bes_zrop_status_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_BES_ZROP_STATUS2 : 0x%x\n",
		gk20a_readl(g, gr_pri_bes_zrop_status2_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_BE0_BECS_BE_EXCEPTION: 0x%x\n",
		gk20a_readl(g, gr_pri_be0_becs_be_exception_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_BE0_BECS_BE_EXCEPTION_EN: 0x%x\n",
		gk20a_readl(g, gr_pri_be0_becs_be_exception_en_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_GPC0_GPCCS_GPC_EXCEPTION: 0x%x\n",
		gk20a_readl(g, gr_pri_gpc0_gpccs_gpc_exception_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_GPC0_GPCCS_GPC_EXCEPTION_EN: 0x%x\n",
		gk20a_readl(g, gr_pri_gpc0_gpccs_gpc_exception_en_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_GPC0_TPC0_TPCCS_TPC_EXCEPTION: 0x%x\n",
		gk20a_readl(g, gr_pri_gpc0_tpc0_tpccs_tpc_exception_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_GPC0_TPC0_TPCCS_TPC_EXCEPTION_EN: 0x%x\n",
		gk20a_readl(g, gr_pri_gpc0_tpc0_tpccs_tpc_exception_en_r()));
	return 0;
}

void gp10b_init_gr(struct gpu_ops *gops)
{
	gm20b_init_gr(gops);
	gops->gr.is_valid_class = gr_gp10b_is_valid_class;
	gops->gr.commit_global_cb_manager = gr_gp10b_commit_global_cb_manager;
	gops->gr.commit_global_pagepool = gr_gp10b_commit_global_pagepool;
	gops->gr.add_zbc_color = gr_gp10b_add_zbc_color;
	gops->gr.add_zbc_depth = gr_gp10b_add_zbc_depth;
	gops->gr.pagepool_default_size = gr_gp10b_pagepool_default_size;
	gops->gr.calc_global_ctx_buffer_size =
		gr_gp10b_calc_global_ctx_buffer_size;
	gops->gr.handle_sw_method = gr_gp10b_handle_sw_method;
	gops->gr.cb_size_default = gr_gp10b_cb_size_default;
	gops->gr.set_alpha_circular_buffer_size =
		gr_gp10b_set_alpha_circular_buffer_size;
	gops->gr.set_circular_buffer_size =
		gr_gp10b_set_circular_buffer_size;
	gops->gr.init_ctx_state = gr_gp10b_init_ctx_state;
	gops->gr.alloc_gr_ctx = gr_gp10b_alloc_gr_ctx;
	gops->gr.free_gr_ctx = gr_gp10b_free_gr_ctx;
	gops->gr.update_ctxsw_preemption_mode =
		gr_gp10b_update_ctxsw_preemption_mode;
	gops->gr.dump_gr_regs = gr_gp10b_dump_gr_status_regs;
}
