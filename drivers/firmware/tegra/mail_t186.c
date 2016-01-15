/*
 * Copyright (c) 2015-2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/tegra-hsp.h>
#include <linux/tegra-ivc.h>
#include <soc/tegra/bpmp_abi.h>
#include "bpmp.h"
#include "mail_t186.h"

struct transport_layer_ops trans_ops;

static bool ivc_rx_ready(int ch)
{
	struct ivc *ivc;
	void *frame;
	bool ready;

	ivc = trans_ops.channel_to_ivc(ch);
	frame = tegra_ivc_read_get_next_frame(ivc);
	ready = !IS_ERR_OR_NULL(frame);
	channel_area[ch].ib = ready ? frame : NULL;

	return ready;
}

bool bpmp_master_acked(int ch)
{
	return ivc_rx_ready(ch);
}

bool bpmp_slave_signalled(int ch)
{
	return ivc_rx_ready(ch);
}

void bpmp_free_master(int ch)
{
	struct ivc *ivc;

	ivc = trans_ops.channel_to_ivc(ch);
	if (tegra_ivc_read_advance(ivc))
		WARN_ON(1);
}

void bpmp_signal_slave(int ch)
{
	struct ivc *ivc;

	ivc = trans_ops.channel_to_ivc(ch);
	if (tegra_ivc_write_advance(ivc))
		WARN_ON(1);
}

bool bpmp_master_free(int ch)
{
	struct ivc *ivc;
	void *frame;
	bool ready;

	ivc = trans_ops.channel_to_ivc(ch);
	frame = tegra_ivc_write_get_next_frame(ivc);
	ready = !IS_ERR_OR_NULL(frame);
	channel_area[ch].ob = ready ? frame : NULL;

	return ready;
}

void tegra_bpmp_mail_return_data(int ch, int code, void *data, int sz)
{
	const int flags = channel_area[ch].ib->flags;
	struct ivc *ivc;
	struct mb_data *frame;
	int r;

	if (sz > MSG_DATA_SZ) {
		WARN_ON(1);
		return;
	}

	ivc = trans_ops.channel_to_ivc(ch);
	r = tegra_ivc_read_advance(ivc);
	WARN_ON(r);

	if (!(flags & DO_ACK))
		return;

	frame = tegra_ivc_write_get_next_frame(ivc);
	if (IS_ERR_OR_NULL(frame)) {
		WARN_ON(1);
		return;
	}

	frame->code = code;
	memcpy(frame->data, data, sz);
	r = tegra_ivc_write_advance(ivc);
	WARN_ON(r);

	if (flags & RING_DOORBELL)
		bpmp_ring_doorbell();
}
EXPORT_SYMBOL(tegra_bpmp_mail_return_data);

void bpmp_ring_doorbell(void)
{
	if (mail_ops.ring_doorbell)
		mail_ops.ring_doorbell();
}

int bpmp_thread_ch_index(int ch)
{
	if (ch < CPU_NA_0_TO_BPMP_CH || ch > CPU_NA_6_TO_BPMP_CH)
		return -1;
	return ch - CPU_NA_0_TO_BPMP_CH;
}

int bpmp_thread_ch(int idx)
{
	return CPU_NA_0_TO_BPMP_CH + idx;
}

int bpmp_ob_channel(void)
{
	return smp_processor_id() + CPU_0_TO_BPMP_CH;
}

int bpmp_init_irq(void)
{
	if (mail_ops.init_irq)
		return mail_ops.init_irq();

	return 0;
}

static int bpmp_channel_init(void)
{
	int e = 0;
	int i;

	if (!mail_ops.channel_init)
		return 0;

	for (i = 0; i < NR_CHANNELS && !e; i++)
		e = mail_ops.channel_init(i);

	return e;
}

void tegra_bpmp_resume(void)
{
	if (mail_ops.resume)
		mail_ops.resume();
}

int bpmp_connect(void)
{
	unsigned long flags;
	int ret = 0;

	if (connected)
		return 0;

	if (mail_ops.iomem_init)
		ret = mail_ops.iomem_init();

	if (ret) {
		pr_err("bpmp iomem init failed (%d)\n", ret);
		return ret;
	}

	ret = mail_ops.handshake();
	if (ret) {
		pr_err("bpmp handshake failed (%d)\n", ret);
		return ret;
	}

	bpmp_channel_init();
	connected = 1;

	local_irq_save(flags);
	ret = __bpmp_do_ping();
	local_irq_restore(flags);
	pr_info("bpmp: ping status is %d\n", ret);
	WARN_ON(ret);

	return ret;
}

/* FIXME: consider using an attr */
static const char *ofm_native = "nvidia,tegra186-bpmp";
struct mail_ops mail_ops;

static int mail_ops_probe(void)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, ofm_native);
	if (np) {
		of_node_put(np);
		return init_native_override();
	}

#ifdef CONFIG_TEGRA_HV_MANAGER
	np = of_find_compatible_node(NULL, NULL, ofm_virt);
	if (np) {
		of_node_put(np);
		return init_virt_override();
	}
#endif

	WARN_ON(1);
	return -ENODEV;
}

int bpmp_mail_init_prepare(void)
{
	int r;

	r = mail_ops_probe();
	if (r)
		return r;

	if (mail_ops.init_prepare)
		return mail_ops.init_prepare();

	return 0;
}

/*
 * FIXME: this initcall is here due to legacy reasons.
 * This should be moved nearer to the function definition.
 */
postcore_initcall(bpmp_mail_init);

void tegra_bpmp_init_early(void)
{
	bpmp_mail_init();
}
