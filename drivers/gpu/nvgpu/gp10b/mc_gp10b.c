/*
 * GP20B master
 *
 * Copyright (c) 2014, NVIDIA CORPORATION.  All rights reserved.
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
#include "mc_gp10b.h"
#include "hw_mc_gp10b.h"

void mc_gp10b_intr_enable(struct gk20a *g)
{
	u32 eng_intr_mask = gk20a_fifo_engine_interrupt_mask(g);

	gk20a_writel(g, mc_intr_en_clear_r(0), 0xffffffff);
	gk20a_writel(g, mc_intr_en_set_r(0),
		     mc_intr_pfifo_pending_f()
		     | eng_intr_mask);
	gk20a_writel(g, mc_intr_en_clear_r(1), 0xffffffff);
	gk20a_writel(g, mc_intr_en_set_r(1),
		     mc_intr_pfifo_pending_f()
		     | mc_intr_priv_ring_pending_f()
		     | mc_intr_ltc_pending_f()
		     | mc_intr_pbus_pending_f()
		     | eng_intr_mask);
}

irqreturn_t mc_gp10b_isr_stall(struct gk20a *g)
{
	u32 mc_intr_0;

	if (!g->power_on)
		return IRQ_NONE;

	/* not from gpu when sharing irq with others */
	mc_intr_0 = gk20a_readl(g, mc_intr_r(0));
	if (unlikely(!mc_intr_0))
		return IRQ_NONE;

	gk20a_writel(g, mc_intr_en_clear_r(0), 0xffffffff);

	return IRQ_WAKE_THREAD;
}

irqreturn_t mc_gp10b_isr_nonstall(struct gk20a *g)
{
	u32 mc_intr_1;

	if (!g->power_on)
		return IRQ_NONE;

	/* not from gpu when sharing irq with others */
	mc_intr_1 = gk20a_readl(g, mc_intr_r(1));
	if (unlikely(!mc_intr_1))
		return IRQ_NONE;

	gk20a_writel(g, mc_intr_en_clear_r(1), 0xffffffff);

	return IRQ_WAKE_THREAD;
}

irqreturn_t mc_gp10b_intr_thread_stall(struct gk20a *g)
{
	u32 mc_intr_0;
	u32 eng_intr_mask = gk20a_fifo_engine_interrupt_mask(g);

	gk20a_dbg(gpu_dbg_intr, "interrupt thread launched");

	mc_intr_0 = gk20a_readl(g, mc_intr_r(0));

	gk20a_dbg(gpu_dbg_intr, "stall intr %08x\n", mc_intr_0);

	if (mc_intr_0 & BIT(g->fifo.engine_info[ENGINE_GR_GK20A].intr_id))
		gr_gk20a_elpg_protected_call(g, gk20a_gr_isr(g));
	if (mc_intr_0 & mc_intr_pfifo_pending_f())
		gk20a_fifo_isr(g);
	if (mc_intr_0 & mc_intr_pmu_pending_f())
		gk20a_pmu_isr(g);
	if (mc_intr_0 & mc_intr_priv_ring_pending_f())
		gk20a_priv_ring_isr(g);
	if (mc_intr_0 & mc_intr_ltc_pending_f())
		g->ops.ltc.isr(g);
	if (mc_intr_0 & mc_intr_pbus_pending_f())
		gk20a_pbus_isr(g);

	gk20a_writel(g, mc_intr_en_set_r(0),
		     mc_intr_pfifo_pending_f()
		     | eng_intr_mask);

	return IRQ_HANDLED;
}

irqreturn_t mc_gp10b_intr_thread_nonstall(struct gk20a *g)
{
	u32 mc_intr_1;
	u32 eng_intr_mask = gk20a_fifo_engine_interrupt_mask(g);

	gk20a_dbg(gpu_dbg_intr, "interrupt thread launched");

	mc_intr_1 = gk20a_readl(g, mc_intr_r(1));

	gk20a_dbg(gpu_dbg_intr, "non-stall intr %08x\n", mc_intr_1);

	if (mc_intr_1 & mc_intr_pfifo_pending_f())
		gk20a_fifo_nonstall_isr(g);
	if (mc_intr_1 & BIT(g->fifo.engine_info[ENGINE_GR_GK20A].intr_id))
		gk20a_gr_nonstall_isr(g);

	gk20a_writel(g, mc_intr_en_set_r(1),
		     mc_intr_pfifo_pending_f()
		     | mc_intr_priv_ring_pending_f()
		     | mc_intr_ltc_pending_f()
		     | mc_intr_pbus_pending_f()
		     | eng_intr_mask);

	return IRQ_HANDLED;
}

void gp10b_init_mc(struct gpu_ops *gops)
{
	gops->mc.intr_enable = mc_gp10b_intr_enable;
	gops->mc.isr_stall = mc_gp10b_isr_stall;
	gops->mc.isr_nonstall = mc_gp10b_isr_nonstall;
	gops->mc.isr_thread_stall = mc_gp10b_intr_thread_stall;
	gops->mc.isr_thread_nonstall = mc_gp10b_intr_thread_nonstall;
}
