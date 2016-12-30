/*
 * drivers/video/tegra/host/host1x/channel_host1x.c
 *
 * Tegra Graphics Host Channel
 *
 * Copyright (c) 2010-2015, NVIDIA CORPORATION.  All rights reserved.
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

#include "nvhost_channel.h"
#include "dev.h"
#include "class_ids.h"
#include "nvhost_acm.h"
#include "nvhost_job.h"
#include <trace/events/nvhost.h>
#include <linux/slab.h>
#include "nvhost_sync.h"

#include "nvhost_intr.h"
#include "class_ids.h"
#include "debug.h"

static void submit_work_done_increment(struct nvhost_job *job)
{
	struct nvhost_channel *ch = job->ch;
	struct nvhost_syncpt *sp = &nvhost_get_host(ch->dev)->syncpt;
	struct nvhost_device_data *pdata = platform_get_drvdata(ch->dev);

	if (!pdata->push_work_done)
		return;

	/* make the last increment at job boundary. this will ensure
	 * that the user command buffer is no longer in use */
	job->sp[0].fence = nvhost_syncpt_incr_max(sp, job->sp[0].id, 1);
	nvhost_cdma_push(&ch->cdma,
			 nvhost_opcode_setclass(NV_HOST1X_CLASS_ID, 0, 1),
			 job->sp[0].id);
}

static void lock_device(struct nvhost_job *job, bool lock)
{
	struct nvhost_channel *ch = job->ch;
	struct nvhost_device_data *pdata = platform_get_drvdata(ch->dev);
	u32 opcode = lock ?
		nvhost_opcode_acquire_mlock(pdata->class) :
		nvhost_opcode_release_mlock(pdata->class);

	/* No need to do anything if we have a channel/engine */
	if (nvhost_get_channel_policy() == MAP_CHANNEL_ON_OPEN)
		return;

	nvhost_cdma_push(&ch->cdma, opcode, NVHOST_OPCODE_NOOP);
}

static void serialize(struct nvhost_job *job)
{
	struct nvhost_channel *ch = job->ch;
	struct nvhost_syncpt *sp = &nvhost_get_host(ch->dev)->syncpt;
	struct nvhost_device_data *pdata = platform_get_drvdata(ch->dev);
	int i;

	if (!job->serialize && !pdata->serialize)
		return;

	/*
	 * Force serialization by inserting a host wait for the
	 * previous job to finish before this one can commence.
	 *
	 * NOTE! This cannot be packed because otherwise we might
	 * overwrite the RESTART opcode at the end of the push
	 * buffer.
	 */

	for (i = 0; i < job->num_syncpts; ++i) {
		u32 id = job->sp[i].id;
		u32 max = nvhost_syncpt_read_max(sp, id);

		nvhost_cdma_push(&ch->cdma,
			nvhost_opcode_setclass(NV_HOST1X_CLASS_ID,
				host1x_uclass_wait_syncpt_r(), 1),
			nvhost_class_host_wait_syncpt(id, max));
	}
}

static void add_sync_waits(struct nvhost_channel *ch, int fd)
{
	struct nvhost_master *host = nvhost_get_host(ch->dev);
	struct nvhost_syncpt *sp = &host->syncpt;
	struct sync_fence *fence;
	struct sync_pt *pt;
	struct list_head *pos;

	if (fd < 0)
		return;

	fence = nvhost_sync_fdget(fd);
	if (!fence)
		return;

	/* validate syncpt ids */
	list_for_each(pos, &fence->pt_list_head) {
		u32 id;
		pt = container_of(pos, struct sync_pt, pt_list);
		id = nvhost_sync_pt_id(pt);
		if (!id || id >= nvhost_syncpt_nb_pts(sp)) {
			sync_fence_put(fence);
			return;
		}
	}

	/*
	 * Force serialization by inserting a host wait for the
	 * previous job to finish before this one can commence.
	 *
	 * NOTE! This cannot be packed because otherwise we might
	 * overwrite the RESTART opcode at the end of the push
	 * buffer.
	 */

	list_for_each(pos, &fence->pt_list_head) {
		u32 id;
		u32 thresh;

		pt = container_of(pos, struct sync_pt, pt_list);
		id = nvhost_sync_pt_id(pt);
		thresh = nvhost_sync_pt_thresh(pt);

		if (nvhost_syncpt_is_expired(sp, id, thresh))
			continue;

		nvhost_cdma_push(&ch->cdma,
			nvhost_opcode_setclass(NV_HOST1X_CLASS_ID,
				host1x_uclass_wait_syncpt_r(), 1),
			nvhost_class_host_wait_syncpt(id, thresh));
	}
	sync_fence_put(fence);
}

static void push_waits(struct nvhost_job *job)
{
	struct nvhost_channel *ch = job->ch;
	int i;

	if (nvhost_get_channel_policy() == MAP_CHANNEL_ON_OPEN)
		return;

	for (i = 0; i < job->num_gathers; i++) {
		struct nvhost_job_gather *g = &job->gathers[i];
		add_sync_waits(job->ch, g->pre_fence);
	}

	for (i = 0; i < job->num_waitchk; i++)
		nvhost_cdma_push(&ch->cdma,
			nvhost_opcode_setclass(NV_HOST1X_CLASS_ID,
				host1x_uclass_wait_syncpt_r(), 1),
			nvhost_class_host_wait_syncpt(
				job->waitchk[i].syncpt_id,
				job->waitchk[i].thresh));
}

static inline u32 gather_regnum(u32 word)
{
	return (word >> 16) & 0xfff;
}

static inline  u32 gather_type(u32 word)
{
	return (word >> 28) & 1;
}

static inline u32 gather_count(u32 word)
{
	return word & 0x3fff;
}

static void submit_gathers(struct nvhost_job *job)
{
	void *cpuva = NULL;
	int i;

	/* push user gathers */
	for (i = 0; i < job->num_gathers; i++) {
		struct nvhost_job_gather *g = &job->gathers[i];
		u32 op1;
		u32 op2;

		if (nvhost_get_channel_policy() == MAP_CHANNEL_ON_OPEN)
			add_sync_waits(job->ch, g->pre_fence);

		nvhost_cdma_push(&job->ch->cdma,
			nvhost_opcode_setclass(g->class_id, 0, 0),
			NVHOST_OPCODE_NOOP);

		/* If register is specified, add a gather with incr/nonincr.
		 * This allows writing large amounts of data directly from
		 * memory to a register. */
		if (gather_regnum(g->words))
			op1 = nvhost_opcode_gather_insert(
					gather_regnum(g->words),
					gather_type(g->words),
					gather_count(g->words));
		else
			op1 = nvhost_opcode_gather(g->words);
		op2 = job->gathers[i].mem_base + g->offset;

		if (nvhost_debug_trace_cmdbuf)
			cpuva = dma_buf_vmap(g->buf);
		nvhost_cdma_push_gather(&job->ch->cdma,
				cpuva,
				job->gathers[i].mem_base,
				g->offset,
				op1, op2);
		if (cpuva)
			dma_buf_vunmap(g->buf, cpuva);
	}
}

int host1x_channel_set_low_priority(struct nvhost_channel *ch)
{
	struct nvhost_master *host = nvhost_get_host(ch->dev);
	void __iomem *regs = host->aperture;
	u32 val;

	mutex_lock(&host->priority_lock);
	val = readl(regs + host1x_channel_ch_hipri_r() + BIT_WORD(ch->chid));
	val &= ~BIT_MASK(ch->chid);
	writel(val, regs + host1x_channel_ch_hipri_r() + BIT_WORD(ch->chid));
	mutex_unlock(&host->priority_lock);

	return 0;
}

int host1x_channel_update_priority(struct nvhost_job *job)
{
	struct nvhost_channel *ch = job->ch;
	struct nvhost_master *host = nvhost_get_host(ch->dev);
	void __iomem *regs = host->aperture;
	u32 val;

	if (job->priority < NVHOST_PRIORITY_HIGH)
		return 0;

	mutex_lock(&host->priority_lock);
	val = readl(regs + host1x_channel_ch_hipri_r() + BIT_WORD(ch->chid));
	val |= BIT_MASK(ch->chid);
	writel(val, regs + host1x_channel_ch_hipri_r() + BIT_WORD(ch->chid));
	mutex_unlock(&host->priority_lock);

	return 0;
}

static int host1x_channel_submit(struct nvhost_job *job)
{
	struct nvhost_channel *ch = job->ch;
	struct nvhost_syncpt *sp = &nvhost_get_host(job->ch->dev)->syncpt;
	u32 user_syncpt_incrs;
	u32 prev_max = 0;
	int err, i;
	void *completed_waiters[job->num_syncpts];
	struct nvhost_job_syncpt *hwctx_sp = job->sp + job->hwctx_syncpt_idx;

	host1x_channel_update_priority(job);

	memset(completed_waiters, 0, sizeof(void *) * job->num_syncpts);

	/* Turn on the client module and host1x */
	for (i = 0; i < job->num_syncpts; ++i) {
		err = nvhost_module_busy(ch->dev);
		if (err) {
			nvhost_module_idle_mult(ch->dev, i);
			nvhost_putchannel(ch, i);
			return err;
		}

		nvhost_getchannel(ch);
	}

	/* before error checks, return current max */
	prev_max = hwctx_sp->fence = nvhost_syncpt_read_max(sp, hwctx_sp->id);

	/* get submit lock */
	err = mutex_lock_interruptible(&ch->submitlock);
	if (err) {
		nvhost_module_idle_mult(ch->dev, job->num_syncpts);
		nvhost_putchannel(ch, job->num_syncpts);
		goto error;
	}

	for (i = 0; i < job->num_syncpts; ++i) {
		completed_waiters[i] = nvhost_intr_alloc_waiter();
		if (!completed_waiters[i]) {
			nvhost_module_idle_mult(ch->dev, job->num_syncpts);
			nvhost_putchannel(ch, job->num_syncpts);
			mutex_unlock(&ch->submitlock);
			err = -ENOMEM;
			goto error;
		}
		if (nvhost_intr_has_pending_jobs(
			&nvhost_get_host(ch->dev)->intr, job->sp[i].id, ch))
			dev_warn(&ch->dev->dev,
				"%s: cross-channel dependencies on syncpt %d\n",
				__func__, job->sp[i].id);
	}

	/* begin a CDMA submit */
	err = nvhost_cdma_begin(&ch->cdma, job);
	if (err) {
		nvhost_module_idle_mult(ch->dev, job->num_syncpts);
		nvhost_putchannel(ch, job->num_syncpts);
		mutex_unlock(&ch->submitlock);
		goto error;
	}

	push_waits(job);
	lock_device(job, true);

	/* submit_ctxsave() and submit_ctxrestore() use the channel syncpt */
	user_syncpt_incrs = hwctx_sp->incrs;

	/* determine fences for all syncpoints */
	for (i = 0; i < job->num_syncpts; ++i) {
		u32 incrs = (i == job->hwctx_syncpt_idx) ?
			user_syncpt_incrs :
			job->sp[i].incrs;

		/* create a valid max for client managed syncpoints */
		if (nvhost_syncpt_client_managed(sp, job->sp[i].id)) {
			u32 min = nvhost_syncpt_read(sp, job->sp[i].id);
			if (min)
				dev_warn(&job->ch->dev->dev,
					"converting an active unmanaged syncpoint %d to managed\n",
					job->sp[i].id);
			nvhost_syncpt_set_max(sp, job->sp[i].id, min);
			nvhost_syncpt_set_manager(sp, job->sp[i].id, false);
		}

		job->sp[i].fence =
			nvhost_syncpt_incr_max(sp, job->sp[i].id, incrs);

		/* mark syncpoint used by this channel */
		nvhost_syncpt_mark_used(sp, ch->chid, job->sp[i].id);
	}

	/* mark also client managed syncpoint used by this channel */
	if (job->client_managed_syncpt)
		nvhost_syncpt_mark_used(sp, ch->chid,
					job->client_managed_syncpt);

	submit_gathers(job);
	serialize(job);
	lock_device(job, false);
	submit_work_done_increment(job);

	/* end CDMA submit & stash pinned hMems into sync queue */
	nvhost_cdma_end(&ch->cdma, job);

	trace_nvhost_channel_submitted(ch->dev->name, prev_max,
		hwctx_sp->fence);

	for (i = 0; i < job->num_syncpts; ++i) {
		/* schedule a submit complete interrupt */
		err = nvhost_intr_add_action(&nvhost_get_host(ch->dev)->intr,
			job->sp[i].id, job->sp[i].fence,
			NVHOST_INTR_ACTION_SUBMIT_COMPLETE, ch,
			completed_waiters[i],
			NULL);
		WARN(err, "Failed to set submit complete interrupt");
	}

	mutex_unlock(&ch->submitlock);

	return 0;

error:
	for (i = 0; i < job->num_syncpts; ++i)
		kfree(completed_waiters[i]);
	return err;
}

static int host1x_channel_init_security(struct nvhost_channel *ch)
{

	struct platform_device *pdev = ch->dev;
	struct nvhost_master *host = nvhost_get_host(pdev);
	void __iomem *aperture = host->aperture;

	writel(BIT_MASK(ch->chid), aperture +
	       host1x_channel_filter_gbuffer_r() + BIT_WORD(ch->chid));

	return 0;
}

static int host1x_channel_init(struct nvhost_channel *ch,
	struct nvhost_master *dev)
{
	ch->aperture = host1x_channel_aperture(dev->aperture, ch->chid);

	return 0;
}

static const struct nvhost_channel_ops host1x_channel_ops = {
	.init = host1x_channel_init,
	.submit = host1x_channel_submit,
	.init_gather_filter = host1x_channel_init_security,
	.set_low_ch_prio = host1x_channel_set_low_priority,
};
