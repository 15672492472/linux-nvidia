/*
 * Copyright (c) 2017-2019 NVIDIA Corporation.  All rights reserved.
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

/**
 * @file drivers/media/platform/tegra/camera/fusa-capture/capture-isp.c
 *
 * @brief ISP channel operations for the T186/T194 Camera RTCPU platform.
 */

#include <linux/completion.h>
#include <linux/nospec.h>
#include <linux/nvhost.h>
#include <linux/of_platform.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/tegra-capture-ivc.h>
#include <asm/arch_timer.h>
#include <soc/tegra/chip-id.h>

#include <camera/nvcamera_log.h>
#include <uapi/linux/nvhost_events.h>
#include "soc/tegra/camrtc-capture.h"
#include "soc/tegra/camrtc-capture-messages.h"
#include <media/mc_common.h>
#include <media/fusa-capture/capture-isp-channel.h>
#include <media/fusa-capture/capture-common.h>

#include <media/fusa-capture/capture-isp.h>

/**
 * @brief Invalid ISP channel ID; the channel is not initialized.
 */
#define CAPTURE_CHANNEL_ISP_INVALID_ID	U16_C(0xFFFF)

/**
 * @brief ISP channel process descriptor queue context.
 */
struct isp_desc_rec {
	struct capture_common_buf requests; /**< Process descriptor queue */
	size_t request_buf_size; /**< Size of process descriptor queue [byte] */
	uint32_t queue_depth; /**< No. of process descriptors in queue */
	uint32_t request_size;
		/**< Size of a single process descriptor [byte] */

	uint32_t progress_status_buffer_depth;
		/**< No. of process descriptors. */

	struct mutex unpins_list_lock; /**< Lock for unpins_list */
	struct capture_common_unpins **unpins_list;
		/**< List of process request buffer unpins */
};

/**
 * @brief ISP channel capture context.
 */
struct isp_capture {
	uint16_t channel_id; /**< RCE-assigned ISP FW channel id */
	struct device *rtcpu_dev; /**< rtcpu device */
	struct tegra_isp_channel *isp_channel; /**< ISP channel context */
	struct capture_buffer_table *buffer_ctx;
		/**< Surface buffer management table */

	struct isp_desc_rec capture_desc_ctx;
		/**< Capture process descriptor queue context */
	struct isp_desc_rec program_desc_ctx;
		/**< Program process descriptor queue context */

	struct capture_common_status_notifier progress_status_notifier;
		/**< Process progress status notifier context */
	bool is_progress_status_notifier_set;
		/**< Whether progress_status_notifer has been initialized */

#ifdef HAVE_ISP_GOS_TABLES
	uint32_t num_gos_tables; /**< No. of cv devices in gos_tables */
	const dma_addr_t *gos_tables; /**< IOVA addresses of all GoS devices */
#endif

	struct syncpoint_info progress_sp; /**< Syncpoint for frame progress */
	struct syncpoint_info stats_progress_sp;
		/**< Syncpoint for stats progress */

	struct completion control_resp;
		/**< Completion for capture-control IVC response */
	struct completion capture_resp;
		/**<
		 * Completion for capture process requests (frame), if progress
		 * status notifier is not in use
		 */
	struct completion capture_program_resp;
		/**<
		 * Completion for program process requests (frame), if progress
		 * status notifier is not in use
		 */

	struct mutex control_msg_lock;
		/**< Lock for capture-control IVC control_resp_msg */
	struct CAPTURE_CONTROL_MSG control_resp_msg;
		/**< capture-control IVC resp msg written to by callback */

	struct mutex reset_lock;
		/**< Channel lock for reset/abort support (via RCE) */
	bool reset_capture_program_flag;
		/**< Reset flag to drain pending program process requests */
	bool reset_capture_flag;
		/**< Reset flag to drain pending capture process requests */
};

/**
 * @brief Initialize an ISP syncpoint and get its GoS backing.
 *
 * @param[in]	chan	ISP channel context
 * @param[in]	name	Syncpoint name
 * @param[in]	enable	Whether to initialize or just clear @a sp
 * @param[out]	sp	Syncpoint handle
 *
 * @returns	0 (success), neg. errno (failure)
 */
static int isp_capture_setup_syncpt(
	struct tegra_isp_channel *chan,
	const char *name,
	bool enable,
	struct syncpoint_info *sp)
{
	struct platform_device *pdev = chan->ndev;
	uint32_t gos_index = GOS_INDEX_INVALID;
	uint32_t gos_offset = 0;
	int err;

	memset(sp, 0, sizeof(*sp));

	if (!enable)
		return 0;


	err = chan->ops->alloc_syncpt(pdev, name, &sp->id);
	if (err)
		return err;

	err = nvhost_syncpt_read_ext_check(pdev, sp->id, &sp->threshold);
	if (err)
		goto cleanup;

	err = chan->ops->get_syncpt_gos_backing(pdev, sp->id, &sp->shim_addr,
				&gos_index, &gos_offset);
	if (err)
		goto cleanup;

	sp->gos_index = gos_index;
	sp->gos_offset = gos_offset;

	return 0;

cleanup:
	chan->ops->release_syncpt(pdev, sp->id);
	memset(sp, 0, sizeof(*sp));

	return err;
}

/**
 * @brief Release an ISP syncpoint and clear its handle.
 *
 * @param[in]	chan	ISP channel context
 * @param[out]	sp	Syncpoint handle
 */
static void isp_capture_release_syncpt(
	struct tegra_isp_channel *chan,
	struct syncpoint_info *sp)
{
	if (sp->id)
		chan->ops->release_syncpt(chan->ndev, sp->id);

	memset(sp, 0, sizeof(*sp));
}

/**
 * @brief Release the ISP channel progress and stats progress syncpoints.
 *
 * @param[in]	chan	ISP channel context
 *
 * @returns	0 (success), neg. errno (failure)
 */
static void isp_capture_release_syncpts(
	struct tegra_isp_channel *chan)
{
	struct isp_capture *capture = chan->capture_data;

	isp_capture_release_syncpt(chan, &capture->progress_sp);
	isp_capture_release_syncpt(chan, &capture->stats_progress_sp);
}

/**
 * @brief Set up the ISP channel progress and stats progress syncpoints.
 *
 * @param[in]	chan	ISP channel context
 *
 * @returns	0 (success), neg. errno (failure)
 */
static int isp_capture_setup_syncpts(
	struct tegra_isp_channel *chan)
{
	struct isp_capture *capture = chan->capture_data;
	int err = 0;

#ifdef HAVE_ISP_GOS_TABLES
	capture->num_gos_tables = chan->ops->get_gos_table(chan->ndev,
							&capture->gos_tables);
#endif

	err = isp_capture_setup_syncpt(chan, "progress", true,
			&capture->progress_sp);
	if (err < 0)
		goto fail;

	err = isp_capture_setup_syncpt(chan, "stats_progress",
				true,
				&capture->stats_progress_sp);
	if (err < 0)
		goto fail;

	return 0;

fail:
	isp_capture_release_syncpts(chan);
	return err;
}

/**
 * @brief Read the value of an ISP channel syncpoint.
 *
 * @param[in]	chan	ISP channel context
 * @param[in]	sp	Syncpoint handle
 * @param[out]	val	Syncpoint value
 *
 * @returns	0 (success), neg. errno (failure)
 */
static int isp_capture_read_syncpt(
	struct tegra_isp_channel *chan,
	struct syncpoint_info *sp,
	uint32_t *val)
{
	int err;

	if (sp->id) {
		err = nvhost_syncpt_read_ext_check(chan->ndev,
						sp->id, val);
		if (err < 0) {
			dev_err(chan->isp_dev,
				"%s: get syncpt %i val failed\n", __func__,
				sp->id);
			return -EINVAL;
		}
	}

	return 0;
}

/**
 * @brief Patch the descriptor GoS SID (@a gos_relative) and syncpoint shim
 * address (@a sp_relative) with the ISP IOVA-mapped addresses of a syncpoint
 * (@a fence_offset).
 *
 * @param[in]	chan		ISP channel context
 * @param[in]	fence_offset	Syncpoint offset from process descriptor queue
 *				[byte]
 * @param[in]	gos_relative	GoS SID offset from @a fence_offset [byte]
 * @param[in]	sp_relative	Shim address from @a fence_offset [byte]
 *
 * @returns	0 (success), neg. errno (failure)
 */
static int isp_capture_populate_fence_info(
	struct tegra_isp_channel *chan,
	int fence_offset,
	uint32_t gos_relative,
	uint32_t sp_relative)
{
	struct isp_capture *capture = chan->capture_data;
	void *reloc_page_addr = NULL;
	int err = 0;
	uint64_t sp_raw;
	uint32_t sp_id;
	dma_addr_t syncpt_addr;
	uint32_t gos_index;
	uint32_t gos_offset;
	uint64_t gos_info = 0;

	reloc_page_addr = dma_buf_kmap(capture->capture_desc_ctx.requests.buf,
				fence_offset >> PAGE_SHIFT);

	if (unlikely(reloc_page_addr == NULL)) {
		dev_err(chan->isp_dev, "%s: couldn't map request\n", __func__);
		return -ENOMEM;
	}

	sp_raw = __raw_readq(
			(void __iomem *)(reloc_page_addr +
			(fence_offset & ~PAGE_MASK)));
	sp_id = sp_raw & 0xFFFFFFFF;

	err = chan->ops->get_syncpt_gos_backing(chan->ndev, sp_id, &syncpt_addr,
			&gos_index, &gos_offset);
	if (err) {
		dev_err(chan->isp_dev,
			"%s: get GoS backing failed\n", __func__);
		goto ret;
	}

	gos_info = ((((uint16_t)gos_offset << 16) | ((uint8_t)gos_index) << 8)
			& 0xFFFFFFFF);

	__raw_writeq(gos_info, (void __iomem *)(reloc_page_addr +
			((fence_offset + gos_relative) & ~PAGE_MASK)));

	__raw_writeq((uint64_t)syncpt_addr, (void __iomem *)(reloc_page_addr +
			((fence_offset + sp_relative) & ~PAGE_MASK)));

ret:
	dma_buf_kunmap(capture->capture_desc_ctx.requests.buf,
			fence_offset >> PAGE_SHIFT, reloc_page_addr);
	return err;
}

/**
 * @brief Patch the inputfence syncpoints of a process descriptor w/ ISP
 * IOVA-mapped addresses.
 *
 * @param[in]	chan		ISP channel context
 * @param[in]	req		ISP process request
 * @param[in]	request_offset	Descriptor offset from process descriptor queue
 *				[byte]
 *
 * @returns	0 (success), neg. errno (failure)
 */
static int isp_capture_setup_inputfences(
	struct tegra_isp_channel *chan,
	struct isp_capture_req *req,
	int request_offset)
{
	uint32_t __user *inpfences_reloc_user;
	uint32_t *inpfences_relocs = NULL;
	uint32_t inputfences_offset = 0;
	int i = 0;
	int err = 0;

	/* It is valid not to have inputfences for given frame capture */
	if (!req->inputfences_relocs.num_relocs)
		return 0;

	inpfences_reloc_user = (uint32_t __user *)
			(uintptr_t)req->inputfences_relocs.reloc_relatives;

	inpfences_relocs = kcalloc(req->inputfences_relocs.num_relocs,
				sizeof(uint32_t), GFP_KERNEL);
	if (unlikely(inpfences_relocs == NULL)) {
		dev_err(chan->isp_dev,
			"failed to allocate inputfences reloc array\n");
		return -ENOMEM;
	}

	err = copy_from_user(inpfences_relocs, inpfences_reloc_user,
		req->inputfences_relocs.num_relocs * sizeof(uint32_t)) ?
			-EFAULT : 0;
	if (err < 0) {
		dev_err(chan->isp_dev, "failed to copy inputfences relocs\n");
		goto fail;
	}

	for (i = 0; i < req->inputfences_relocs.num_relocs; i++) {
		inputfences_offset = request_offset +
					inpfences_relocs[i];
		err = isp_capture_populate_fence_info(chan, inputfences_offset,
				req->gos_relative, req->sp_relative);
		if (err < 0) {
			dev_err(chan->isp_dev,
				"Populate inputfences info failed\n");
			goto fail;
		}
	}
	speculation_barrier();

fail:
	kfree(inpfences_relocs);
	return err;
}

/**
 * @brief Patch the prefence syncpoints of a process descriptor w/ ISP
 * IOVA-mapped addresses.
 *
 * @param[in]	chan		ISP channel context
 * @param[in]	req		ISP process request
 * @param[in]	request_offset	Descriptor offset from process descriptor queue
 *				[byte]
 *
 * @returns	0 (success), neg. errno (failure)
 */
static int isp_capture_setup_prefences(
	struct tegra_isp_channel *chan,
	struct isp_capture_req *req,
	int request_offset)
{
	uint32_t __user *prefence_reloc_user;
	uint32_t *prefence_relocs = NULL;
	uint32_t prefence_offset = 0;
	int i = 0;
	int err = 0;

	/* It is valid not to have prefences for given frame capture */
	if (!req->prefences_relocs.num_relocs)
		return 0;

	prefence_reloc_user = (uint32_t __user *)
			(uintptr_t)req->prefences_relocs.reloc_relatives;

	prefence_relocs = kcalloc(req->prefences_relocs.num_relocs,
		sizeof(uint32_t), GFP_KERNEL);
	if (unlikely(prefence_relocs == NULL)) {
		dev_err(chan->isp_dev,
			"failed to allocate prefences reloc array\n");
		return -ENOMEM;
	}

	err = copy_from_user(prefence_relocs, prefence_reloc_user,
		req->prefences_relocs.num_relocs * sizeof(uint32_t)) ?
			-EFAULT : 0;
	if (err < 0) {
		dev_err(chan->isp_dev, "failed to copy prefences relocs\n");
		goto fail;
	}

	for (i = 0; i < req->prefences_relocs.num_relocs; i++) {
		prefence_offset = request_offset +
					prefence_relocs[i];
		err = isp_capture_populate_fence_info(chan, prefence_offset,
				req->gos_relative, req->sp_relative);
		if (err < 0) {
			dev_err(chan->isp_dev, "Populate prefences info failed\n");
			goto fail;
		}
	}
	speculation_barrier();

fail:
	kfree(prefence_relocs);
	return err;
}

/**
 * @brief Unpin and free the list of pinned capture_mapping's associated with an
 * ISP process request.
 *
 * @param[in]	chan		ISP channel context
 * @param[in]	buffer_index	Process descriptor queue index
 */
static void isp_capture_request_unpin(
	struct tegra_isp_channel *chan,
	uint32_t buffer_index)
{
	struct isp_capture *capture = chan->capture_data;
	struct capture_common_unpins *unpins;
	int i = 0;

	mutex_lock(&capture->capture_desc_ctx.unpins_list_lock);
	unpins = capture->capture_desc_ctx.unpins_list[buffer_index];
	if (unpins != NULL) {
		for (i = 0; i < unpins->num_unpins; i++)
			put_mapping(capture->buffer_ctx, unpins->data[i]);
		kfree(unpins);
		capture->capture_desc_ctx.unpins_list[buffer_index] = NULL;
	}
	mutex_unlock(&capture->capture_desc_ctx.unpins_list_lock);
}

/**
 * @brief Unpin and free the list of pinned capture_mapping's associated with an
 * ISP program request.
 *
 * @param[in]	chan		ISP channel context
 * @param[in]	buffer_index	Program descriptor queue index
 */
static void isp_capture_program_request_unpin(
	struct tegra_isp_channel *chan,
	uint32_t buffer_index)
{
	struct isp_capture *capture = chan->capture_data;
	struct capture_common_unpins *unpins;
	int i = 0;

	mutex_lock(&capture->program_desc_ctx.unpins_list_lock);
	unpins = capture->program_desc_ctx.unpins_list[buffer_index];
	if (unpins != NULL) {
		for (i = 0; i < unpins->num_unpins; i++)
			put_mapping(capture->buffer_ctx, unpins->data[i]);
		kfree(unpins);
		capture->program_desc_ctx.unpins_list[buffer_index] = NULL;
	}
	mutex_unlock(&capture->program_desc_ctx.unpins_list_lock);
}

/**
 * @brief Prepare and submit a pin and relocation request for a program
 * descriptor, the resultant mappings are added to the channel program
 * descriptor queue's @em unpins_list.
 *
 * @param[in]	chan	ISP channel context
 * @param[in]	req	ISP program request
 *
 * @returns	0 (success), neg. errno (failure)
 */
static int isp_capture_program_prepare(
	struct tegra_isp_channel *chan,
	struct isp_program_req *req)
{
	struct isp_capture *capture = chan->capture_data;
	int err = 0;
	struct capture_common_pin_req cap_common_req;

	if (capture == NULL) {
		dev_err(chan->isp_dev,
			"%s: isp capture uninitialized\n", __func__);
		return -ENODEV;
	}

	if (capture->channel_id == CAPTURE_CHANNEL_ISP_INVALID_ID) {
		dev_err(chan->isp_dev,
			"%s: setup channel first\n", __func__);
		return -ENODEV;
	}

	if (req == NULL) {
		dev_err(chan->isp_dev,
			"%s: Invalid program req\n", __func__);
		return -EINVAL;
	}

	if (req->isp_program_relocs.num_relocs == 0) {
		dev_err(chan->isp_dev,
			"%s: req must have non-zero relocs\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&capture->reset_lock);
	if (capture->reset_capture_program_flag) {
		/* consume any pending completions when coming out of reset */
		while (try_wait_for_completion(&capture->capture_program_resp))
			; /* do nothing */
	}
	capture->reset_capture_program_flag = false;

	/* memory pin and reloc */
	cap_common_req.table = capture->buffer_ctx;
	cap_common_req.dev = chan->isp_dev;
	cap_common_req.rtcpu_dev = capture->rtcpu_dev;
	cap_common_req.unpins = NULL;
	cap_common_req.requests = &capture->program_desc_ctx.requests;
	cap_common_req.request_size = capture->program_desc_ctx.request_size;
	cap_common_req.request_offset = req->buffer_index *
					capture->program_desc_ctx.request_size;
	cap_common_req.requests_mem = capture->program_desc_ctx.requests.buf;
	cap_common_req.num_relocs = req->isp_program_relocs.num_relocs;
	cap_common_req.reloc_user = (uint32_t __user *)
			(uintptr_t)req->isp_program_relocs.reloc_relatives;

	err = capture_common_request_pin_and_reloc(&cap_common_req);
	if (err < 0) {
		dev_err(chan->isp_dev, "request pin and reloc failed\n");
		goto fail;
	}

	/* add pinned memory ctx to unpins_list */
	mutex_lock(&capture->program_desc_ctx.unpins_list_lock);
	capture->program_desc_ctx.unpins_list[req->buffer_index] =
		cap_common_req.unpins;
	mutex_unlock(&capture->program_desc_ctx.unpins_list_lock);

fail:
	mutex_unlock(&capture->reset_lock);
	return err;
}

/**
 * @brief Unpin an ISP process request and flush the memory.
 *
 * @param[in]	capture		ISP channel capture context
 * @param[in]	buffer_index	Process descriptor queue index
 */
static inline void isp_capture_ivc_capture_cleanup(
	struct isp_capture *capture,
	uint32_t buffer_index)
{
	struct tegra_isp_channel *chan = capture->isp_channel;

	isp_capture_request_unpin(chan, buffer_index);
	dma_sync_single_range_for_cpu(capture->rtcpu_dev,
		capture->capture_desc_ctx.requests.iova,
		buffer_index * capture->capture_desc_ctx.request_size,
		capture->capture_desc_ctx.request_size,
		DMA_FROM_DEVICE);
}

/**
 * @brief Signal completion or write progress status to notifier for ISP capture
 * indication from RCE.
 *
 * If the ISP channel's progress status notifier is not set, the capture
 * completion will be signalled.
 *
 * @param[in]	capture		ISP channel capture context
 * @param[in]	buffer_index	Process descriptor queue index
 */
static inline void isp_capture_ivc_capture_signal(
	struct isp_capture *capture,
	uint32_t buffer_index)
{
	if (capture->is_progress_status_notifier_set) {
		(void)capture_common_set_progress_status(
			&capture->progress_status_notifier,
			buffer_index,
			capture->capture_desc_ctx.progress_status_buffer_depth,
			PROGRESS_STATUS_DONE);
	} else {
		/*
		 * Only fire completions if not using
		 * the new progress status buffer mechanism
		 */
		complete(&capture->capture_resp);
	}
}

/**
 * @brief Unpin an ISP program request and flush the memory.
 *
 * @param[in]	capture		ISP channel capture context
 * @param[in]	buffer_index	Program descriptor queue index
 */
static inline void isp_capture_ivc_program_cleanup(
	struct isp_capture *capture,
	uint32_t buffer_index)
{
	struct tegra_isp_channel *chan = capture->isp_channel;

	isp_capture_program_request_unpin(chan, buffer_index);
	dma_sync_single_range_for_cpu(capture->rtcpu_dev,
		capture->program_desc_ctx.requests.iova,
		buffer_index * capture->program_desc_ctx.request_size,
		capture->program_desc_ctx.request_size,
		DMA_FROM_DEVICE);
}

/**
 * @brief Signal completion or write progress status to notifier for ISP program
 * indication from RCE.
 *
 * If the ISP channel's progress status notifier is not set, the program
 * completion will be signalled.
 *
 * @param[in]	capture		ISP channel capture context
 * @param[in]	buffer_index	Program descriptor queue index
 */
static inline void isp_capture_ivc_program_signal(
	struct isp_capture *capture,
	uint32_t buffer_index)
{
	if (capture->is_progress_status_notifier_set) {
		/*
		 * Program status notifiers are after the process status
		 * notifiers; add the process status buffer depth as an offset.
		 */
		(void)capture_common_set_progress_status(
			&capture->progress_status_notifier,
			buffer_index +
			capture->capture_desc_ctx.progress_status_buffer_depth,
			capture->program_desc_ctx.progress_status_buffer_depth +
			capture->capture_desc_ctx.progress_status_buffer_depth,
			PROGRESS_STATUS_DONE);
	} else {
		/*
		 * Only fire completions if not using
		 * the new progress status buffer mechanism
		 */
		complete(&capture->capture_program_resp);
	}
}

/**
 * @brief ISP channel callback function for @em capture IVC messages.
 *
 * @param[in]	ivc_resp	IVC @ref CAPTURE_MSG from RCE
 * @param[in]	pcontext	ISP channel capture context
 */
static void isp_capture_ivc_status_callback(
	const void *ivc_resp,
	const void *pcontext)
{
	struct CAPTURE_MSG *status_msg = (struct CAPTURE_MSG *)ivc_resp;
	struct isp_capture *capture = (struct isp_capture *)pcontext;
	struct tegra_isp_channel *chan = capture->isp_channel;
	uint32_t buffer_index;

	if (unlikely(capture == NULL)) {
		dev_err(chan->isp_dev, "%s: invalid context", __func__);
		return;
	}

	if (unlikely(status_msg == NULL)) {
		dev_err(chan->isp_dev, "%s: invalid response", __func__);
		return;
	}

	switch (status_msg->header.msg_id) {
	case CAPTURE_ISP_STATUS_IND:
		buffer_index = status_msg->capture_isp_status_ind.buffer_index;
		isp_capture_ivc_capture_cleanup(capture, buffer_index);
		isp_capture_ivc_capture_signal(capture, buffer_index);
		dev_dbg(chan->isp_dev, "%s: status chan_id %u msg_id %u\n",
			__func__, status_msg->header.channel_id,
			status_msg->header.msg_id);
		break;
	case CAPTURE_ISP_PROGRAM_STATUS_IND:
		buffer_index =
			status_msg->capture_isp_program_status_ind.buffer_index;
		isp_capture_ivc_program_cleanup(capture, buffer_index);
		isp_capture_ivc_program_signal(capture, buffer_index);
		dev_dbg(chan->isp_dev,
			"%s: isp_ program status chan_id %u msg_id %u\n",
			__func__, status_msg->header.channel_id,
			status_msg->header.msg_id);
		break;
	case CAPTURE_ISP_EX_STATUS_IND:
		buffer_index =
			status_msg->capture_isp_ex_status_ind
			.process_buffer_index;
		isp_capture_ivc_program_cleanup(capture,
			status_msg->capture_isp_ex_status_ind
			.program_buffer_index);
		isp_capture_ivc_capture_cleanup(capture, buffer_index);
		isp_capture_ivc_capture_signal(capture, buffer_index);

		dev_dbg(chan->isp_dev,
			"%s: isp extended status chan_id %u msg_id %u\n",
			__func__, status_msg->header.channel_id,
			status_msg->header.msg_id);
		break;
	default:
		dev_err(chan->isp_dev,
			"%s: unknown capture resp", __func__);
		break;
	}
}

/**
 * @brief Send a @em capture-control IVC message to RCE on an ISP channel, and
 * block w/ timeout, waiting for the RCE response.
 *
 * @param[in]	chan	ISP channel context
 * @param[in]	msg	IVC message payload
 * @param[in]	size	Size of @a msg [byte]
 * @param[in]	resp_id	IVC message identifier, see @CAPTURE_MSG_IDS
 *
 * @returns	0 (success), neg. errno (failure)
 */
static int isp_capture_ivc_send_control(struct tegra_isp_channel *chan,
		const struct CAPTURE_CONTROL_MSG *msg, size_t size,
		uint32_t resp_id)
{
	struct isp_capture *capture = chan->capture_data;
	struct CAPTURE_MSG_HEADER resp_header = msg->header;
	uint32_t timeout = HZ;
	int err = 0;

	dev_dbg(chan->isp_dev, "%s: sending chan_id %u msg_id %u\n",
			__func__, resp_header.channel_id, resp_header.msg_id);

	resp_header.msg_id = resp_id;

	/* Send capture control IVC message */
	mutex_lock(&capture->control_msg_lock);
	err = tegra_capture_ivc_control_submit(msg, size);
	if (err < 0) {
		dev_err(chan->isp_dev, "IVC control submit failed\n");
		goto fail;
	}

	timeout = wait_for_completion_timeout(&capture->control_resp, timeout);
	if (timeout <= 0) {
		dev_err(chan->isp_dev,
			"no reply from camera processor\n");
		err = -ETIMEDOUT;
		goto fail;
	}

	if (memcmp(&resp_header, &capture->control_resp_msg.header,
			sizeof(resp_header)) != 0) {
		dev_err(chan->isp_dev,
			"unexpected response from camera processor\n");
		err = -EINVAL;
		goto fail;
	}
	mutex_unlock(&capture->control_msg_lock);

	dev_dbg(chan->isp_dev, "%s: response chan_id %u msg_id %u\n",
			__func__, capture->control_resp_msg.header.channel_id,
			capture->control_resp_msg.header.msg_id);
	return 0;

fail:
	mutex_unlock(&capture->control_msg_lock);
	return err;
}

/**
 * @brief ISP channel callback function for @em capture-control IVC messages,
 * this unblocks the channel's @em capture-control completion.
 *
 * @param[in]	ivc_resp	IVC @ref CAPTURE_CONTROL_MSG from RCE
 * @param[in]	pcontext	ISP channel capture context
 */
static void isp_capture_ivc_control_callback(
	const void *ivc_resp,
	const void *pcontext)
{
	const struct CAPTURE_CONTROL_MSG *control_msg = ivc_resp;
	struct isp_capture *capture = (struct isp_capture *)pcontext;
	struct tegra_isp_channel *chan = capture->isp_channel;

	if (unlikely(capture == NULL)) {
		dev_err(chan->isp_dev, "%s: invalid context", __func__);
		return;
	}

	if (unlikely(control_msg == NULL)) {
		dev_err(chan->isp_dev, "%s: invalid response", __func__);
		return;
	}

	switch (control_msg->header.msg_id) {
	case CAPTURE_CHANNEL_ISP_SETUP_RESP:
	case CAPTURE_CHANNEL_ISP_RESET_RESP:
	case CAPTURE_CHANNEL_ISP_RELEASE_RESP:
		memcpy(&capture->control_resp_msg, control_msg,
				sizeof(*control_msg));
		complete(&capture->control_resp);
		break;
	default:
		dev_err(chan->isp_dev,
			"%s: unknown capture isp control resp", __func__);
		break;
	}
}

int isp_capture_init(
	struct tegra_isp_channel *chan)
{
	struct isp_capture *capture;
	struct device_node *dn;
	struct platform_device *rtc_pdev;

	dev_dbg(chan->isp_dev, "%s++\n", __func__);
	dn = of_find_node_by_path("tegra-camera-rtcpu");
	if (of_device_is_available(dn) == 0) {
		dev_err(chan->isp_dev, "failed to find rtcpu device node\n");
		return -ENODEV;
	}
	rtc_pdev = of_find_device_by_node(dn);
	if (rtc_pdev == NULL) {
		dev_err(chan->isp_dev, "failed to find rtcpu platform\n");
		return -ENODEV;
	}

	capture = kzalloc(sizeof(*capture), GFP_KERNEL);
	if (unlikely(capture == NULL)) {
		dev_err(chan->isp_dev, "failed to allocate capture channel\n");
		return -ENOMEM;
	}

	capture->rtcpu_dev = &rtc_pdev->dev;

	init_completion(&capture->control_resp);
	init_completion(&capture->capture_resp);
	init_completion(&capture->capture_program_resp);

	mutex_init(&capture->control_msg_lock);
	mutex_init(&capture->capture_desc_ctx.unpins_list_lock);
	mutex_init(&capture->program_desc_ctx.unpins_list_lock);
	mutex_init(&capture->reset_lock);

	capture->isp_channel = chan;
	chan->capture_data = capture;

	capture->channel_id = CAPTURE_CHANNEL_ISP_INVALID_ID;

	capture->reset_capture_program_flag = false;
	capture->reset_capture_flag = false;

	return 0;
}

void isp_capture_shutdown(
	struct tegra_isp_channel *chan)
{
	struct isp_capture *capture = chan->capture_data;

	dev_dbg(chan->isp_dev, "%s--\n", __func__);
	if (capture == NULL)
		return;

	if (capture->channel_id != CAPTURE_CHANNEL_ISP_INVALID_ID) {
		/* No valid ISP reset flags defined now, use zero */
		isp_capture_reset(chan, 0);
		isp_capture_release(chan, 0);
	}

	kfree(capture);
	chan->capture_data = NULL;
}

int isp_capture_setup(
	struct tegra_isp_channel *chan,
	struct isp_capture_setup *setup)
{
	struct capture_buffer_table *buffer_ctx;
	struct isp_capture *capture = chan->capture_data;
	uint32_t transaction;
	struct CAPTURE_CONTROL_MSG control_msg;
	struct CAPTURE_CONTROL_MSG *resp_msg = &capture->control_resp_msg;
	struct capture_channel_isp_config *config =
		&control_msg.channel_isp_setup_req.channel_config;
	int err = 0;
#ifdef HAVE_ISP_GOS_TABLES
	int i;
#endif

	nv_camera_log(chan->ndev,
		arch_counter_get_cntvct(),
		NVHOST_CAMERA_ISP_CAPTURE_SETUP);

	if (capture == NULL) {
		dev_err(chan->isp_dev,
			 "%s: isp capture uninitialized\n", __func__);
		return -ENODEV;
	}

	if (capture->channel_id != CAPTURE_CHANNEL_ISP_INVALID_ID) {
		dev_err(chan->isp_dev,
			"%s: already setup, release first\n", __func__);
		return -EEXIST;
	}

	dev_dbg(chan->isp_dev, "chan flags %u\n", setup->channel_flags);
	dev_dbg(chan->isp_dev, "queue depth %u\n", setup->queue_depth);
	dev_dbg(chan->isp_dev, "request size %u\n", setup->request_size);

	if (setup->channel_flags == 0 ||
			setup->queue_depth == 0 ||
			setup->request_size == 0)
		return -EINVAL;

	buffer_ctx = create_buffer_table(chan->isp_dev);
	if (unlikely(buffer_ctx == NULL)) {
		dev_err(chan->isp_dev, "cannot setup buffer context");
		return -ENOMEM;
	}

	/* pin the process descriptor ring buffer to RTCPU */
	dev_dbg(chan->isp_dev, "%s: descr buffer handle 0x%x\n",
			__func__, setup->mem);
	err = capture_common_pin_memory(capture->rtcpu_dev,
			setup->mem, &capture->capture_desc_ctx.requests);
	if (err < 0) {
		dev_err(chan->isp_dev, "%s: memory setup failed\n", __func__);
		goto pin_fail;
	}

	/* pin the process descriptor ring buffer to ISP */
	err = capture_buffer_add(buffer_ctx, setup->mem);
	if (err < 0) {
		dev_err(chan->isp_dev, "%s: memory setup failed\n", __func__);
		goto pin_fail;
	}

	/* cache isp capture desc ring buffer details */
	capture->capture_desc_ctx.queue_depth = setup->queue_depth;
	capture->capture_desc_ctx.request_size = setup->request_size;
	capture->capture_desc_ctx.request_buf_size = setup->request_size *
							setup->queue_depth;

	/* allocate isp capture desc unpin list based on queue depth */
	capture->capture_desc_ctx.unpins_list = kcalloc(
				capture->capture_desc_ctx.queue_depth,
				sizeof(struct capture_common_unpins *),
				GFP_KERNEL);
	if (unlikely(capture->capture_desc_ctx.unpins_list == NULL)) {
		dev_err(chan->isp_dev, "failed to allocate unpins array\n");
		goto unpins_list_fail;
	}

	/* pin the isp program descriptor ring buffer */
	dev_dbg(chan->isp_dev, "%s: descr buffer handle %u\n",
			__func__, setup->isp_program_mem);
	err = capture_common_pin_memory(capture->rtcpu_dev,
				setup->isp_program_mem,
				&capture->program_desc_ctx.requests);
	if (err < 0) {
		dev_err(chan->isp_dev,
			"%s: isp_program memory setup failed\n", __func__);
		goto prog_pin_fail;
	}

	/* pin the isp program descriptor ring buffer to ISP */
	err = capture_buffer_add(buffer_ctx, setup->isp_program_mem);
	if (err < 0) {
		dev_err(chan->isp_dev,
			"%s: isp_program memory setup failed\n", __func__);
		goto prog_pin_fail;
	}

	/* cache isp program desc ring buffer details */
	capture->program_desc_ctx.queue_depth = setup->isp_program_queue_depth;
	capture->program_desc_ctx.request_size =
					setup->isp_program_request_size;
	capture->program_desc_ctx.request_buf_size =
					setup->isp_program_request_size *
						setup->isp_program_queue_depth;

	/* allocate isp program unpin list based on queue depth */
	capture->program_desc_ctx.unpins_list = kcalloc(
				capture->program_desc_ctx.queue_depth,
				sizeof(struct capture_common_unpins *),
				GFP_KERNEL);
	if (unlikely(capture->program_desc_ctx.unpins_list == NULL)) {
		dev_err(chan->isp_dev,
			"failed to allocate isp program unpins array\n");
		goto prog_unpins_list_fail;
	}

	err = isp_capture_setup_syncpts(chan);
	if (err < 0) {
		dev_err(chan->isp_dev, "%s: syncpt setup failed\n", __func__);
		goto syncpt_fail;
	}

	err = tegra_capture_ivc_register_control_cb(
			&isp_capture_ivc_control_callback,
			&transaction, capture);
	if (err < 0) {
		dev_err(chan->isp_dev, "failed to register control callback\n");
		goto control_cb_fail;
	}

	/* Fill in control config msg to be sent over ctrl ivc chan to RTCPU */
	memset(&control_msg, 0, sizeof(control_msg));

	control_msg.header.msg_id = CAPTURE_CHANNEL_ISP_SETUP_REQ;
	control_msg.header.transaction = transaction;

	config->channel_flags = setup->channel_flags;

	config->request_queue_depth = setup->queue_depth;
	config->request_size = setup->request_size;
	config->requests = capture->capture_desc_ctx.requests.iova;

	config->program_queue_depth = setup->isp_program_queue_depth;
	config->program_size = setup->isp_program_request_size;
	config->programs = capture->program_desc_ctx.requests.iova;

	config->progress_sp = capture->progress_sp;
	config->stats_progress_sp = capture->stats_progress_sp;

#ifdef HAVE_ISP_GOS_TABLES
	dev_dbg(chan->isp_dev, "%u GoS tables configured.\n",
		capture->num_gos_tables);
	for (i = 0; i < capture->num_gos_tables; i++) {
		config->isp_gos_tables[i] = (iova_t)capture->gos_tables[i];
		dev_dbg(chan->isp_dev, "gos[%d] = 0x%08llx\n",
			i, (u64)capture->gos_tables[i]);
	}
	config->num_isp_gos_tables = capture->num_gos_tables;
#endif

	err = isp_capture_ivc_send_control(chan, &control_msg,
			sizeof(control_msg), CAPTURE_CHANNEL_ISP_SETUP_RESP);
	if (err < 0)
		goto submit_fail;

	if (resp_msg->channel_isp_setup_resp.result != CAPTURE_OK) {
		dev_err(chan->isp_dev, "%s: control failed, errno %d", __func__,
			resp_msg->channel_setup_resp.result);
		err = -EINVAL;
		goto submit_fail;
	}

	capture->channel_id = resp_msg->channel_isp_setup_resp.channel_id;

	err = tegra_capture_ivc_notify_chan_id(capture->channel_id,
			transaction);
	if (err < 0) {
		dev_err(chan->isp_dev, "failed to update control callback\n");
		goto cb_fail;
	}

	err = tegra_capture_ivc_register_capture_cb(
			&isp_capture_ivc_status_callback,
			capture->channel_id, capture);
	if (err < 0) {
		dev_err(chan->isp_dev, "failed to register capture callback\n");
		goto cb_fail;
	}

	capture->buffer_ctx = buffer_ctx;

	return 0;

cb_fail:
	isp_capture_release(chan, CAPTURE_CHANNEL_RESET_FLAG_IMMEDIATE);
	return err;
submit_fail:
	tegra_capture_ivc_unregister_control_cb(transaction);
control_cb_fail:
	isp_capture_release_syncpts(chan);
syncpt_fail:
	kfree(capture->program_desc_ctx.unpins_list);
prog_unpins_list_fail:
	capture_common_unpin_memory(&capture->program_desc_ctx.requests);
prog_pin_fail:
	kfree(capture->capture_desc_ctx.unpins_list);
unpins_list_fail:
	capture_common_unpin_memory(&capture->capture_desc_ctx.requests);
pin_fail:
	destroy_buffer_table(buffer_ctx);
	return err;
}

int isp_capture_release(
	struct tegra_isp_channel *chan,
	uint32_t reset_flags)
{
	struct isp_capture *capture = chan->capture_data;
	struct CAPTURE_CONTROL_MSG control_msg;
	struct CAPTURE_CONTROL_MSG *resp_msg = &capture->control_resp_msg;
	int i;
	int err = 0;

	nv_camera_log(chan->ndev,
		arch_counter_get_cntvct(),
		NVHOST_CAMERA_ISP_CAPTURE_RELEASE);

	if (capture == NULL) {
		dev_err(chan->isp_dev,
			 "%s: isp capture uninitialized\n", __func__);
		return -ENODEV;
	}

	if (capture->channel_id == CAPTURE_CHANNEL_ISP_INVALID_ID) {
		dev_err(chan->isp_dev,
			"%s: setup channel first\n", __func__);
		return -ENODEV;
	}

	memset(&control_msg, 0, sizeof(control_msg));

	control_msg.header.msg_id = CAPTURE_CHANNEL_ISP_RELEASE_REQ;
	control_msg.header.channel_id = capture->channel_id;
	control_msg.channel_release_req.reset_flags = reset_flags;

	err = isp_capture_ivc_send_control(chan, &control_msg,
			sizeof(control_msg), CAPTURE_CHANNEL_ISP_RELEASE_RESP);
	if (err < 0)
		goto error;

	if (resp_msg->channel_isp_release_resp.result != CAPTURE_OK) {
		dev_err(chan->isp_dev, "%s: control failed, errno %d", __func__,
			resp_msg->channel_release_resp.result);
		err = -EINVAL;
		goto error;
	}

	err = tegra_capture_ivc_unregister_capture_cb(capture->channel_id);
	if (err < 0) {
		dev_err(chan->isp_dev,
			"failed to unregister capture callback\n");
		goto error;
	}

	err = tegra_capture_ivc_unregister_control_cb(capture->channel_id);
	if (err < 0) {
		dev_err(chan->isp_dev,
			"failed to unregister control callback\n");
		goto error;
	}

	for (i = 0; i < capture->program_desc_ctx.queue_depth; i++) {
		complete(&capture->capture_program_resp);
		isp_capture_program_request_unpin(chan, i);
	}

	capture_common_unpin_memory(&capture->program_desc_ctx.requests);

	for (i = 0; i < capture->capture_desc_ctx.queue_depth; i++) {
		complete(&capture->capture_resp);
		isp_capture_request_unpin(chan, i);
	}
	speculation_barrier();

	isp_capture_release_syncpts(chan);

	capture_common_unpin_memory(&capture->capture_desc_ctx.requests);

	kfree(capture->program_desc_ctx.unpins_list);
	kfree(capture->capture_desc_ctx.unpins_list);

	if (capture->is_progress_status_notifier_set)
		capture_common_release_progress_status_notifier(
			&capture->progress_status_notifier);

	destroy_buffer_table(capture->buffer_ctx);

	capture->channel_id = CAPTURE_CHANNEL_ISP_INVALID_ID;

	return 0;

error:
	return err;
}

int isp_capture_reset(
	struct tegra_isp_channel *chan,
	uint32_t reset_flags)
{
	struct isp_capture *capture = chan->capture_data;
#ifdef CAPTURE_ISP_RESET_BARRIER_IND
	struct CAPTURE_MSG capture_msg;
#endif
	struct CAPTURE_CONTROL_MSG control_msg;
	struct CAPTURE_CONTROL_MSG *resp_msg = &capture->control_resp_msg;
	int i;
	int err = 0;

	nv_camera_log(chan->ndev,
		arch_counter_get_cntvct(),
		NVHOST_CAMERA_ISP_CAPTURE_RESET);

	if (capture == NULL) {
		dev_err(chan->isp_dev,
			 "%s: isp capture uninitialized\n", __func__);
		return -ENODEV;
	}

	if (capture->channel_id == CAPTURE_CHANNEL_ISP_INVALID_ID) {
		dev_err(chan->isp_dev,
			"%s: setup channel first\n", __func__);
		return -ENODEV;
	}

	mutex_lock(&capture->reset_lock);
	capture->reset_capture_program_flag = true;
	capture->reset_capture_flag = true;

#ifdef CAPTURE_ISP_RESET_BARRIER_IND
	memset(&capture_msg, 0, sizeof(capture_msg));
	capture_msg.header.msg_id = CAPTURE_ISP_RESET_BARRIER_IND;
	capture_msg.header.channel_id = capture->channel_id;

	err = tegra_capture_ivc_capture_submit(&capture_msg,
			sizeof(capture_msg));
	if (err < 0) {
		dev_err(chan->isp_dev, "IVC capture submit failed\n");
		goto error;
	}
#endif

	memset(&control_msg, 0, sizeof(control_msg));
	control_msg.header.msg_id = CAPTURE_CHANNEL_ISP_RESET_REQ;
	control_msg.header.channel_id = capture->channel_id;
	control_msg.channel_isp_reset_req.reset_flags = reset_flags;

	err = isp_capture_ivc_send_control(chan, &control_msg,
			sizeof(control_msg), CAPTURE_CHANNEL_ISP_RESET_RESP);
	if (err < 0)
		goto error;

#ifdef CAPTURE_ISP_RESET_BARRIER_IND
	if (resp_msg->channel_isp_reset_resp.result == CAPTURE_ERROR_TIMEOUT) {
		dev_dbg(chan->isp_dev, "%s: isp reset timedout\n", __func__);
		err = -EAGAIN;
		goto error;
	}
#endif

	if (resp_msg->channel_isp_reset_resp.result != CAPTURE_OK) {
		dev_err(chan->isp_dev, "%s: control failed, errno %d", __func__,
			resp_msg->channel_isp_reset_resp.result);
		err = -EINVAL;
		goto error;
	}

	for (i = 0; i < capture->program_desc_ctx.queue_depth; i++) {
		isp_capture_program_request_unpin(chan, i);
		complete(&capture->capture_program_resp);
	}
	speculation_barrier();

	for (i = 0; i < capture->capture_desc_ctx.queue_depth; i++) {
		isp_capture_request_unpin(chan, i);
		complete(&capture->capture_resp);
	}
	speculation_barrier();

	err = 0;

error:
	mutex_unlock(&capture->reset_lock);
	return err;
}

int isp_capture_get_info(
	struct tegra_isp_channel *chan,
	struct isp_capture_info *info)
{
	struct isp_capture *capture = chan->capture_data;
	int err;

	nv_camera_log(chan->ndev,
		arch_counter_get_cntvct(),
		NVHOST_CAMERA_ISP_CAPTURE_GET_INFO);

	if (capture == NULL) {
		dev_err(chan->isp_dev,
			 "%s: isp capture uninitialized\n", __func__);
		return -ENODEV;
	}

	if (capture->channel_id == CAPTURE_CHANNEL_ISP_INVALID_ID) {
		dev_err(chan->isp_dev,
			"%s: setup channel first\n", __func__);
		return -ENODEV;
	}

	if (info == NULL) {
		dev_err(chan->isp_dev,
			"%s: Invalid user parameter\n", __func__);
		return -EINVAL;
	}

	info->channel_id = capture->channel_id;

	info->syncpts.progress_syncpt = capture->progress_sp.id;
	info->syncpts.stats_progress_syncpt =
			capture->stats_progress_sp.id;

	err = isp_capture_read_syncpt(chan, &capture->progress_sp,
			&info->syncpts.progress_syncpt_val);
	if (err < 0)
		return err;

	err = isp_capture_read_syncpt(chan, &capture->stats_progress_sp,
			&info->syncpts.stats_progress_syncpt_val);
	if (err < 0)
		return err;

	return 0;
}

int isp_capture_request(
	struct tegra_isp_channel *chan,
	struct isp_capture_req *req)
{
	struct isp_capture *capture = chan->capture_data;
	struct CAPTURE_MSG capture_msg;
	struct capture_common_pin_req cap_common_req;
	uint32_t request_offset;
	int err = 0;

	if (capture == NULL) {
		dev_err(chan->isp_dev,
			"%s: isp capture uninitialized\n", __func__);
		return -ENODEV;
	}

	if (capture->channel_id == CAPTURE_CHANNEL_ISP_INVALID_ID) {
		dev_err(chan->isp_dev,
			"%s: setup channel first\n", __func__);
		return -ENODEV;
	}

	if (req == NULL) {
		dev_err(chan->isp_dev,
			"%s: Invalid req\n", __func__);
		return -EINVAL;
	}

	if (req->isp_relocs.num_relocs == 0) {
		dev_err(chan->isp_dev,
			"%s: req must have non-zero relocs\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&capture->reset_lock);
	if (capture->reset_capture_flag) {
		/* consume any pending completions when coming out of reset */
		while (try_wait_for_completion(&capture->capture_resp))
			; /* do nothing */
	}
	capture->reset_capture_flag = false;

	memset(&capture_msg, 0, sizeof(capture_msg));
	capture_msg.header.msg_id = CAPTURE_ISP_REQUEST_REQ;
	capture_msg.header.channel_id = capture->channel_id;
	capture_msg.capture_isp_request_req.buffer_index = req->buffer_index;

	request_offset = req->buffer_index *
			capture->capture_desc_ctx.request_size;

	err = isp_capture_setup_inputfences(chan, req, request_offset);
	if (err < 0) {
		dev_err(chan->isp_dev, "failed to setup inputfences\n");
		goto fail;
	}

	err = isp_capture_setup_prefences(chan, req, request_offset);
	if (err < 0) {
		dev_err(chan->isp_dev, "failed to setup prefences\n");
		goto fail;
	}

	/* pin and reloc */
	cap_common_req.table = capture->buffer_ctx;
	cap_common_req.dev = chan->isp_dev;
	cap_common_req.rtcpu_dev = capture->rtcpu_dev;
	cap_common_req.unpins = NULL;
	cap_common_req.requests = &capture->capture_desc_ctx.requests;
	cap_common_req.request_size = capture->capture_desc_ctx.request_size;
	cap_common_req.request_offset = request_offset;
	cap_common_req.requests_mem = capture->capture_desc_ctx.requests.buf;
	cap_common_req.num_relocs = req->isp_relocs.num_relocs;
	cap_common_req.reloc_user = (uint32_t __user *)
			(uintptr_t)req->isp_relocs.reloc_relatives;

	err = capture_common_request_pin_and_reloc(&cap_common_req);
	if (err < 0) {
		dev_err(chan->isp_dev, "request pin and reloc failed\n");
		goto fail;
	}

	/* add pinned memory ctx to unpins_list */
	mutex_lock(&capture->capture_desc_ctx.unpins_list_lock);
	capture->capture_desc_ctx.unpins_list[req->buffer_index] =
		cap_common_req.unpins;
	mutex_unlock(&capture->capture_desc_ctx.unpins_list_lock);

	nv_camera_log_submit(
			chan->ndev,
			capture->progress_sp.id,
			capture->progress_sp.threshold,
			capture_msg.header.channel_id,
			arch_counter_get_cntvct());

	dev_dbg(chan->isp_dev, "%s: sending chan_id %u msg_id %u buf:%u\n",
			__func__, capture_msg.header.channel_id,
			capture_msg.header.msg_id, req->buffer_index);


	err = tegra_capture_ivc_capture_submit(&capture_msg,
			sizeof(capture_msg));
	if (err < 0) {
		dev_err(chan->isp_dev, "IVC capture submit failed\n");
		goto fail;
	}

	mutex_unlock(&capture->reset_lock);

	return 0;

fail:
	mutex_unlock(&capture->reset_lock);
	isp_capture_request_unpin(chan, req->buffer_index);
	return err;
}

int isp_capture_status(
	struct tegra_isp_channel *chan,
	int32_t timeout_ms)
{
	struct isp_capture *capture = chan->capture_data;
	int err = 0;

	nv_camera_log(chan->ndev,
		arch_counter_get_cntvct(),
		NVHOST_CAMERA_ISP_CAPTURE_STATUS);

	if (capture == NULL) {
		dev_err(chan->isp_dev,
			 "%s: isp capture uninitialized\n", __func__);
		return -ENODEV;
	}

	if (capture->channel_id == CAPTURE_CHANNEL_ISP_INVALID_ID) {
		dev_err(chan->isp_dev,
			"%s: setup channel first\n", __func__);
		return -ENODEV;
	}

	if (tegra_platform_is_sim() && timeout_ms > 0) {
		dev_dbg(chan->isp_dev, "%s timeout : %d extended by 10x on VDK",
			__func__, timeout_ms);
		timeout_ms *= 10;
	}

	/* negative timeout means wait forever */
	if (timeout_ms < 0) {
		err = wait_for_completion_killable(&capture->capture_resp);
	} else {
		err = wait_for_completion_killable_timeout(
				&capture->capture_resp,
				msecs_to_jiffies(timeout_ms));
		if (err == 0) {
			dev_err(chan->isp_dev,
				"no reply from camera processor\n");
			return -ETIMEDOUT;
		}
	}

	if (err < 0) {
		dev_err(chan->isp_dev,
			"wait for capture status failed\n");
		return err;
	}

	mutex_lock(&capture->reset_lock);
	if (capture->reset_capture_flag) {
		mutex_unlock(&capture->reset_lock);
		return -EIO;
	}
	mutex_unlock(&capture->reset_lock);

	return 0;
}

int isp_capture_program_request(
	struct tegra_isp_channel *chan,
	struct isp_program_req *req)
{
	struct isp_capture *capture = chan->capture_data;
	struct CAPTURE_MSG capture_msg;
	int err = 0;

	nv_camera_log(chan->ndev,
		arch_counter_get_cntvct(),
		NVHOST_CAMERA_ISP_CAPTURE_PROGRAM_REQUEST);

	err = isp_capture_program_prepare(chan, req);
	if (err < 0) {
		/* no cleanup needed */
		return err;
	}

	memset(&capture_msg, 0, sizeof(capture_msg));
	capture_msg.header.msg_id = CAPTURE_ISP_PROGRAM_REQUEST_REQ;
	capture_msg.header.channel_id = capture->channel_id;
	capture_msg.capture_isp_program_request_req.buffer_index =
				req->buffer_index;

	dev_dbg(chan->isp_dev, "%s: sending chan_id %u msg_id %u buf:%u\n",
			__func__, capture_msg.header.channel_id,
			capture_msg.header.msg_id, req->buffer_index);

	err = tegra_capture_ivc_capture_submit(&capture_msg,
			sizeof(capture_msg));
	if (err < 0) {
		dev_err(chan->isp_dev, "IVC program submit failed\n");
		isp_capture_program_request_unpin(chan, req->buffer_index);
		return err;
	}

	return 0;
}

int isp_capture_program_status(
	struct tegra_isp_channel *chan)
{
	struct isp_capture *capture = chan->capture_data;
	int err = 0;

	nv_camera_log(chan->ndev,
		arch_counter_get_cntvct(),
		NVHOST_CAMERA_ISP_CAPTURE_PROGRAM_STATUS);

	if (capture == NULL) {
		dev_err(chan->isp_dev,
			 "%s: isp capture uninitialized\n", __func__);
		return -ENODEV;
	}

	if (capture->channel_id == CAPTURE_CHANNEL_ISP_INVALID_ID) {
		dev_err(chan->isp_dev,
			"%s: setup channel first\n", __func__);
		return -ENODEV;
	}

	dev_dbg(chan->isp_dev, "%s: waiting for isp program status\n",
		__func__);

	/* no timeout as an isp_program may get used for mutliple frames */
	err = wait_for_completion_killable(&capture->capture_program_resp);
	if (err < 0) {
		dev_err(chan->isp_dev,
			"no reply from camera processor\n");
		return err;
	}

	mutex_lock(&capture->reset_lock);
	if (capture->reset_capture_program_flag) {
		mutex_unlock(&capture->reset_lock);
		return -EIO;
	}
	mutex_unlock(&capture->reset_lock);

	return 0;
}

int isp_capture_request_ex(
	struct tegra_isp_channel *chan,
	struct isp_capture_req_ex *req)
{
	int err;

	nv_camera_log(chan->ndev,
		arch_counter_get_cntvct(),
		NVHOST_CAMERA_ISP_CAPTURE_REQUEST_EX);

	if (req->program_req.buffer_index == U32_MAX) {
		/* forward to process request */
		return isp_capture_request(chan, &req->capture_req);
	}

	err = isp_capture_program_prepare(chan, &req->program_req);

	if (err < 0) {
		/* no cleanup required */
		return err;
	}

	err = isp_capture_request(chan, &req->capture_req);

	if (err < 0) {
		/* unpin prepared program */
		isp_capture_program_request_unpin(
			chan, req->program_req.buffer_index);
	}

	return err;
}

int isp_capture_set_progress_status_notifier(
	struct tegra_isp_channel *chan,
	struct isp_capture_progress_status_req *req)
{
	int err = 0;
	struct isp_capture *capture = chan->capture_data;

	nv_camera_log(chan->ndev,
		arch_counter_get_cntvct(),
		NVHOST_CAMERA_ISP_CAPTURE_SET_PROGRESS_STATUS);

	if (req->mem == 0 ||
		req->process_buffer_depth == 0) {
		dev_err(chan->isp_dev,
				"%s: process request buffer is invalid\n",
				__func__);
		return -EINVAL;
	}

	if (req->mem == 0 ||
		req->program_buffer_depth == 0) {
		dev_err(chan->isp_dev,
				"%s: program request buffer is invalid\n",
				__func__);
		return -EINVAL;
	}

	if (capture == NULL) {
		dev_err(chan->isp_dev,
				"%s: isp capture uninitialized\n", __func__);
		return -ENODEV;
	}

	if (req->process_buffer_depth < capture->capture_desc_ctx.queue_depth) {
		dev_err(chan->isp_dev,
			"%s: Process progress status buffer smaller than queue depth\n",
			__func__);
		return -EINVAL;
	}

	if (req->program_buffer_depth < capture->program_desc_ctx.queue_depth) {
		dev_err(chan->isp_dev,
			"%s: Program progress status buffer smaller than queue depth\n",
			__func__);
		return -EINVAL;
	}

	/* Setup the progress status buffer */
	err = capture_common_setup_progress_status_notifier(
		&capture->progress_status_notifier,
		req->mem,
		(req->process_buffer_depth + req->program_buffer_depth) *
			sizeof(uint32_t),
		req->mem_offset);

	if (err < 0) {
		dev_err(chan->isp_dev,
			"%s: Process progress status setup failed\n",
			__func__);
		return -EFAULT;
	}

	dev_dbg(chan->isp_dev, "Progress status mem offset %u\n",
			req->mem_offset);
	dev_dbg(chan->isp_dev, "Process buffer depth %u\n",
			req->process_buffer_depth);
	dev_dbg(chan->isp_dev, "Program buffer depth %u\n",
			req->program_buffer_depth);

	capture->capture_desc_ctx.progress_status_buffer_depth =
			req->process_buffer_depth;
	capture->program_desc_ctx.progress_status_buffer_depth =
			req->program_buffer_depth;

	capture->is_progress_status_notifier_set = true;
	return err;
}

int isp_capture_buffer_request(
	struct tegra_isp_channel *chan,
	struct isp_buffer_req *req)
{
	struct isp_capture *capture = chan->capture_data;
	int err;

	err = capture_buffer_request(
		capture->buffer_ctx, req->mem, req->flag);
	return err;
}
