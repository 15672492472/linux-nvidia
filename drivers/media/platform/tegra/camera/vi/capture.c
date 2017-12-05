/*
 * Tegra Video Input capture operations
 *
 * Tegra Graphics Host VI
 *
 * Copyright (c) 2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * Author: David Wang <davidw@nvidia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/completion.h>
#include <linux/nvhost.h>
#include <linux/of_platform.h>
#include <linux/printk.h>
#include <linux/tegra-capture-ivc.h>
#include <media/capture.h>
#include <media/capture_common.h>
#include <media/capture_vi_channel.h>

#include "soc/tegra/camrtc-capture.h"
#include "soc/tegra/camrtc-capture-messages.h"

/* Remove after constants integrated into camrtc-capture-messages.h */
#ifndef CAPTURE_CHANNEL_TPG_SETUP_REQ
#define CAPTURE_CHANNEL_TPG_SETUP_REQ   U32_C(0x30)
#define CAPTURE_CHANNEL_TPG_SETUP_RESP  U32_C(0x31)
#define CAPTURE_CHANNEL_TPG_START_REQ   U32_C(0x32)
#define CAPTURE_CHANNEL_TPG_START_RESP  U32_C(0x33)
#define CAPTURE_CHANNEL_TPG_STOP_REQ    U32_C(0x34)
#define CAPTURE_CHANNEL_TPG_STOP_RESP   U32_C(0x35)
#endif

#define CAPTURE_CHANNEL_UNKNOWN_RESP 0xFFFFFFFF
#define CAPTURE_CHANNEL_INVALID_ID 0xFFFF
#define CAPTURE_CHANNEL_INVALID_MASK 0llu

struct vi_capture {
	uint16_t channel_id;
	struct device *rtcpu_dev;
	struct tegra_vi_channel *vi_channel;
	struct capture_common_buf requests;
	size_t request_buf_size;
	uint32_t queue_depth;
	uint32_t request_size;

	struct syncpoint_info progress_sp;
	struct syncpoint_info embdata_sp;
	struct syncpoint_info linetimer_sp;

	struct completion control_resp;
	struct completion capture_resp;
	struct mutex control_msg_lock;
	struct CAPTURE_CONTROL_MSG control_resp_msg;

	struct mutex unpins_list_lock;
	struct capture_common_unpins **unpins_list;
};

static void vi_capture_ivc_control_callback(const void *ivc_resp,
		const void *pcontext)
{
	const struct CAPTURE_CONTROL_MSG *control_msg = ivc_resp;
	struct vi_capture *capture = (struct vi_capture *)pcontext;
	struct tegra_vi_channel *chan = capture->vi_channel;

	if (unlikely(capture == NULL)) {
		dev_err(chan->dev, "%s: invalid context", __func__);
		return;
	}

	if (unlikely(control_msg == NULL)) {
		dev_err(chan->dev, "%s: invalid response", __func__);
		return;
	}

	switch (control_msg->header.msg_id) {
	case CAPTURE_CHANNEL_SETUP_RESP:
	case CAPTURE_CHANNEL_RESET_RESP:
	case CAPTURE_CHANNEL_RELEASE_RESP:
	case CAPTURE_COMPAND_CONFIG_RESP:
	case CAPTURE_PDAF_CONFIG_RESP:
	case CAPTURE_SYNCGEN_ENABLE_RESP:
	case CAPTURE_SYNCGEN_DISABLE_RESP:
	case CAPTURE_CHANNEL_TPG_SETUP_RESP:
	case CAPTURE_CHANNEL_TPG_START_RESP:
	case CAPTURE_CHANNEL_TPG_STOP_RESP:
		memcpy(&capture->control_resp_msg, control_msg,
				sizeof(*control_msg));
		complete(&capture->control_resp);
		break;
	default:
		dev_err(chan->dev,
			"%s: unknown capture control resp 0x%x", __func__,
			control_msg->header.msg_id);
		break;
	}
}

static void vi_capture_request_unpin(struct tegra_vi_channel *chan,
		uint32_t buffer_index);
static void vi_capture_ivc_status_callback(const void *ivc_resp,
		const void *pcontext)
{
	struct CAPTURE_MSG *status_msg = (struct CAPTURE_MSG *)ivc_resp;
	struct vi_capture *capture = (struct vi_capture *)pcontext;
	struct tegra_vi_channel *chan = capture->vi_channel;
	uint32_t buffer_index;

	if (unlikely(capture == NULL)) {
		dev_err(chan->dev, "%s: invalid context", __func__);
		return;
	}

	if (unlikely(status_msg == NULL)) {
		dev_err(chan->dev, "%s: invalid response", __func__);
		return;
	}

	switch (status_msg->header.msg_id) {
	case CAPTURE_STATUS_IND:
		buffer_index = status_msg->capture_status_ind.buffer_index;
		vi_capture_request_unpin(chan, buffer_index);
		dma_sync_single_range_for_cpu(capture->rtcpu_dev,
		    capture->requests.iova,
		    buffer_index * capture->request_size,
		    capture->request_size, DMA_FROM_DEVICE);
		complete(&capture->capture_resp);
		dev_dbg(chan->dev, "%s: status chan_id %u msg_id %u\n",
				__func__, status_msg->header.channel_id,
				status_msg->header.msg_id);
		break;
	default:
		dev_err(chan->dev,
			"%s: unknown capture resp", __func__);
		break;
	}
}

int vi_capture_init(struct tegra_vi_channel *chan)
{
	struct vi_capture *capture;
	struct device_node *dn;
	struct platform_device *rtc_pdev;

	dev_dbg(chan->dev, "%s++\n", __func__);
	dn = of_find_node_by_path("tegra-camera-rtcpu");
	if (of_device_is_available(dn) == 0) {
		dev_err(chan->dev, "failed to find rtcpu device node\n");
		return -ENODEV;
	}
	rtc_pdev = of_find_device_by_node(dn);
	if (rtc_pdev == NULL) {
		dev_err(chan->dev, "failed to find rtcpu platform\n");
		return -ENODEV;
	}

	capture = devm_kzalloc(chan->dev,
			sizeof(*capture), GFP_KERNEL);
	if (unlikely(capture == NULL)) {
		dev_err(chan->dev, "failed to allocate capture channel\n");
		return -ENOMEM;
	}

	capture->rtcpu_dev = &rtc_pdev->dev;

	init_completion(&capture->control_resp);
	init_completion(&capture->capture_resp);

	mutex_init(&capture->control_msg_lock);
	mutex_init(&capture->unpins_list_lock);

	capture->vi_channel = chan;
	chan->capture_data = capture;

	capture->channel_id = CAPTURE_CHANNEL_INVALID_ID;

	return 0;
}

void vi_capture_shutdown(struct tegra_vi_channel *chan)
{
	struct vi_capture *capture = chan->capture_data;

	dev_dbg(chan->dev, "%s--\n", __func__);
	if (capture == NULL)
		return;

	if (capture->channel_id != CAPTURE_CHANNEL_INVALID_ID)
		vi_capture_release(chan, 0);

	devm_kfree(chan->dev, capture);
	chan->capture_data = NULL;
}

static int vi_capture_ivc_send_control(struct tegra_vi_channel *chan,
		const struct CAPTURE_CONTROL_MSG *msg, size_t size,
		uint32_t resp_id)
{
	struct vi_capture *capture = chan->capture_data;
	struct CAPTURE_MSG_HEADER resp_header = msg->header;
	uint32_t timeout = HZ;
	int err = 0;

	dev_dbg(chan->dev, "%s: sending chan_id %u msg_id %u\n",
			__func__, resp_header.channel_id, resp_header.msg_id);
	resp_header.msg_id = resp_id;
	/* Send capture control IVC message */
	mutex_lock(&capture->control_msg_lock);
	err = tegra_capture_ivc_control_submit(msg, size);
	if (err < 0) {
		dev_err(chan->dev, "IVC control submit failed\n");
		goto fail;
	}

	timeout = wait_for_completion_killable_timeout(
			&capture->control_resp, timeout);
	if (timeout <= 0) {
		dev_err(chan->dev,
			"no reply from camera processor\n");
		err = -ETIMEDOUT;
		goto fail;
	}

	if (memcmp(&resp_header, &capture->control_resp_msg.header,
			sizeof(resp_header)) != 0) {
		dev_err(chan->dev,
			"unexpected response from camera processor\n");
		err = -EINVAL;
		goto fail;
	}

	mutex_unlock(&capture->control_msg_lock);
	dev_dbg(chan->dev, "%s: response chan_id %u msg_id %u\n",
			__func__, capture->control_resp_msg.header.channel_id,
			capture->control_resp_msg.header.msg_id);
	return 0;

fail:
	mutex_unlock(&capture->control_msg_lock);
	return err;
}

static int vi_capture_setup_syncpts(struct tegra_vi_channel *chan,
				uint32_t flags);
static void vi_capture_release_syncpts(struct tegra_vi_channel *chan);

static int vi_capture_setup_syncpt(struct tegra_vi_channel *chan,
				const char *name, bool enable,
				struct syncpoint_info *sp)
{
	struct platform_device *pdev = chan->ndev;
	uint32_t gos_index, gos_offset;
	int err;

	memset(sp, 0, sizeof(*sp));
	sp->gos_index = GOS_INDEX_INVALID;

	if (!enable)
		return 0;

	err = chan->ops->alloc_syncpt(pdev, name, &sp->id, &sp->shim_addr,
				&gos_index, &gos_offset);
	if (err)
		return err;

	sp->gos_index = gos_index;
	sp->gos_offset = gos_offset;

	return 0;
}

static int vi_capture_setup_syncpts(struct tegra_vi_channel *chan,
				uint32_t flags)
{
	struct vi_capture *capture = chan->capture_data;
	int err = 0;

	err = vi_capture_setup_syncpt(chan, "progress", true,
			&capture->progress_sp);
	if (err < 0)
		goto fail;

	err = vi_capture_setup_syncpt(chan, "embdata",
				(flags & CAPTURE_CHANNEL_FLAG_EMBDATA) != 0,
				&capture->embdata_sp);
	if (err < 0)
		goto fail;

	err = vi_capture_setup_syncpt(chan, "linetimer",
				(flags & CAPTURE_CHANNEL_FLAG_LINETIMER) != 0,
				&capture->linetimer_sp);
	if (err < 0)
		goto fail;

	return 0;

fail:
	vi_capture_release_syncpts(chan);
	return err;
}

static void vi_capture_release_syncpt(struct tegra_vi_channel *chan,
				struct syncpoint_info *sp)
{
	if (sp->id)
		chan->ops->release_syncpt(chan->ndev, sp->id);

	memset(sp, 0, sizeof(*sp));
}

static void vi_capture_release_syncpts(struct tegra_vi_channel *chan)
{
	struct vi_capture *capture = chan->capture_data;

	vi_capture_release_syncpt(chan, &capture->progress_sp);
	vi_capture_release_syncpt(chan, &capture->embdata_sp);
	vi_capture_release_syncpt(chan, &capture->linetimer_sp);
}

int vi_capture_setup(struct tegra_vi_channel *chan,
		struct vi_capture_setup *setup)
{
	struct vi_capture *capture = chan->capture_data;
	uint32_t transaction;
	struct CAPTURE_CONTROL_MSG control_desc;
	struct CAPTURE_CONTROL_MSG *resp_msg = &capture->control_resp_msg;
	struct capture_channel_config *config =
		&control_desc.channel_setup_req.channel_config;
	int err = 0;

	if (capture == NULL) {
		dev_err(chan->dev,
			 "%s: vi capture uninitialized\n", __func__);
		return -ENODEV;
	}

	if (capture->channel_id != CAPTURE_CHANNEL_INVALID_ID) {
		dev_err(chan->dev,
			"%s: already setup, release first\n", __func__);
		return -EEXIST;
	}

	dev_dbg(chan->dev, "chan flags %u\n", setup->channel_flags);
	dev_dbg(chan->dev, "chan mask %llx\n", setup->vi_channel_mask);
	dev_dbg(chan->dev, "queue depth %u\n", setup->queue_depth);
	dev_dbg(chan->dev, "request size %u\n", setup->request_size);

	if (setup->vi_channel_mask == CAPTURE_CHANNEL_INVALID_MASK ||
			setup->channel_flags == 0 ||
			setup->queue_depth == 0 ||
			setup->request_size == 0)
		return -EINVAL;

	/* pin the capture descriptor ring buffer */
	dev_dbg(chan->dev, "%s: descr buffer handle %u\n",
			__func__, setup->mem);
	err = capture_common_pin_memory(capture->rtcpu_dev,
			setup->mem, &capture->requests);
	if (err < 0) {
		dev_err(chan->dev, "%s: memory setup failed\n", __func__);
		return -EFAULT;
	}
	capture->queue_depth = setup->queue_depth;
	capture->request_size = setup->request_size;
	capture->request_buf_size = setup->request_size * setup->queue_depth;

	/* allocate for unpin list based on queue depth */
	capture->unpins_list = devm_kzalloc(chan->dev,
			sizeof(struct capture_common_unpins *) *
				capture->queue_depth,
			GFP_KERNEL);
	if (unlikely(capture->unpins_list == NULL)) {
		dev_err(chan->dev, "failed to allocate unpins array\n");
		goto unpins_list_fail;
	}

	err = vi_capture_setup_syncpts(chan, setup->channel_flags);
	if (err < 0)
		goto syncpt_fail;

	err = tegra_capture_ivc_register_control_cb(
			&vi_capture_ivc_control_callback,
			&transaction, capture);
	if (err < 0) {
		dev_err(chan->dev, "failed to register control callback\n");
		goto control_cb_fail;
	}

	memset(&control_desc, 0, sizeof(control_desc));
	control_desc.header.msg_id = CAPTURE_CHANNEL_SETUP_REQ;
	control_desc.header.transaction = transaction;

	config->channel_flags = setup->channel_flags;
	config->vi_channel_mask = setup->vi_channel_mask;
	config->slvsec_stream_main = setup->slvsec_stream_main;
	config->slvsec_stream_sub = setup->slvsec_stream_sub;

	config->queue_depth = setup->queue_depth;
	config->request_size = setup->request_size;
	config->requests = capture->requests.iova;

	config->progress_sp = capture->progress_sp;
	config->embdata_sp = capture->embdata_sp;
	config->linetimer_sp = capture->linetimer_sp;

	err = vi_capture_ivc_send_control(chan, &control_desc,
			sizeof(control_desc), CAPTURE_CHANNEL_SETUP_RESP);
	if (err < 0)
		goto submit_fail;

	if (resp_msg->channel_setup_resp.result != CAPTURE_OK) {
		dev_err(chan->dev, "%s: control failed, errno %d", __func__,
			resp_msg->channel_setup_resp.result);
		err = -EINVAL;
		goto resp_fail;
	}

	capture->channel_id = resp_msg->channel_setup_resp.channel_id;

	err = tegra_capture_ivc_notify_chan_id(capture->channel_id,
			transaction);
	if (err < 0) {
		dev_err(chan->dev, "failed to update control callback\n");
		goto cb_fail;
	}

	err = tegra_capture_ivc_register_capture_cb(
			&vi_capture_ivc_status_callback,
			capture->channel_id, capture);
	if (err < 0) {
		dev_err(chan->dev, "failed to register capture callback\n");
		goto cb_fail;
	}

	return 0;

cb_fail:
	vi_capture_release(chan, CAPTURE_CHANNEL_RESET_FLAG_IMMEDIATE);
resp_fail:
submit_fail:
	tegra_capture_ivc_unregister_control_cb(transaction);
control_cb_fail:
	vi_capture_release_syncpts(chan);
syncpt_fail:
	devm_kfree(chan->dev, capture->unpins_list);
unpins_list_fail:
	capture_common_unpin_memory(&capture->requests);
	return err;
}


int vi_capture_reset(struct tegra_vi_channel *chan,
		uint32_t reset_flags)
{
	struct vi_capture *capture = chan->capture_data;
	struct CAPTURE_CONTROL_MSG control_desc;
	struct CAPTURE_CONTROL_MSG *resp_msg = &capture->control_resp_msg;
	int i;
	int err = 0;

	if (capture == NULL) {
		dev_err(chan->dev,
			 "%s: vi capture uninitialized\n", __func__);
		return -ENODEV;
	}

	if (capture->channel_id == CAPTURE_CHANNEL_INVALID_ID) {
		dev_err(chan->dev,
			"%s: setup channel first\n", __func__);
		return -ENODEV;
	}

	memset(&control_desc, 0, sizeof(control_desc));
	control_desc.header.msg_id = CAPTURE_CHANNEL_RESET_REQ;
	control_desc.header.channel_id = capture->channel_id;
	control_desc.channel_reset_req.reset_flags = reset_flags;

	err = vi_capture_ivc_send_control(chan, &control_desc,
			sizeof(control_desc), CAPTURE_CHANNEL_RESET_RESP);
	if (err < 0)
		goto submit_fail;

	if (resp_msg->channel_reset_resp.result != CAPTURE_OK) {
		dev_err(chan->dev, "%s: control failed, errno %d", __func__,
			resp_msg->channel_reset_resp.result);
		err = -EINVAL;
	}

	for (i = 0; i < capture->queue_depth; i++)
		vi_capture_request_unpin(chan, i);

	return 0;

submit_fail:
	return err;
}

int vi_capture_set_compand(struct tegra_vi_channel *chan,
		struct vi_capture_compand *compand)
{
	uint32_t ii;
	struct vi_capture *capture = chan->capture_data;
	struct CAPTURE_CONTROL_MSG control_desc;
	int32_t result;
	struct vi_compand_config *desc_compand;
	int err = 0;

	if (capture == NULL) {
		dev_err(chan->dev,
			 "%s: vi capture uninitialized\n", __func__);
		return -ENODEV;
	}

	if (capture->channel_id == CAPTURE_CHANNEL_INVALID_ID) {
		dev_err(chan->dev,
			"%s: setup channel first\n", __func__);
		return -ENODEV;
	}

	memset(&control_desc, 0, sizeof(control_desc));
	control_desc.header.msg_id = CAPTURE_COMPAND_CONFIG_REQ;
	control_desc.header.channel_id = capture->channel_id;
	desc_compand = &control_desc.compand_config_req.compand_config;
	for (ii = 0; ii < VI_CAPTURE_NUM_COMPAND_KNEEPTS; ii++) {
		desc_compand->base[ii] = compand->base[ii];
		desc_compand->scale[ii] = compand->scale[ii];
		desc_compand->offset[ii] = compand->offset[ii];
	}

	err = vi_capture_ivc_send_control(chan, &control_desc,
		sizeof(control_desc), CAPTURE_COMPAND_CONFIG_RESP);
	if (err < 0)
		return err;

	result = capture->control_resp_msg.compand_config_resp.result;
	if (result != CAPTURE_OK) {
		dev_err(chan->dev, "%s: setting compand config failed, result: %d",
				__func__, result);
		return -EINVAL;
	}

	return 0;
}

int vi_capture_release(struct tegra_vi_channel *chan,
		uint32_t reset_flags)
{
	struct vi_capture *capture = chan->capture_data;
	struct CAPTURE_CONTROL_MSG control_desc;
	struct CAPTURE_CONTROL_MSG *resp_msg = &capture->control_resp_msg;
	int i;
	int err = 0;
	int ret = 0;

	if (capture == NULL) {
		dev_err(chan->dev,
			 "%s: vi capture uninitialized\n", __func__);
		return -ENODEV;
	}

	if (capture->channel_id == CAPTURE_CHANNEL_INVALID_ID) {
		dev_err(chan->dev,
			"%s: setup channel first\n", __func__);
		return -ENODEV;
	}

	memset(&control_desc, 0, sizeof(control_desc));
	control_desc.header.msg_id = CAPTURE_CHANNEL_RELEASE_REQ;
	control_desc.header.channel_id = capture->channel_id;
	control_desc.channel_release_req.reset_flags = reset_flags;

	err = vi_capture_ivc_send_control(chan, &control_desc,
			sizeof(control_desc), CAPTURE_CHANNEL_RELEASE_RESP);
	if (err < 0)
		goto submit_fail;

	if (resp_msg->channel_release_resp.result != CAPTURE_OK) {
		dev_err(chan->dev, "%s: control failed, errno %d", __func__,
			resp_msg->channel_release_resp.result);
		err = -EINVAL;
	}

	ret = tegra_capture_ivc_unregister_capture_cb(capture->channel_id);
	if (ret < 0 && err == 0) {
		dev_err(chan->dev,
			"failed to unregister capture callback\n");
		err = ret;
	}

	ret = tegra_capture_ivc_unregister_control_cb(capture->channel_id);
	if (ret < 0 && err == 0) {
		dev_err(chan->dev,
			"failed to unregister control callback\n");
		err = ret;
	}

	for (i = 0; i < capture->queue_depth; i++)
		vi_capture_request_unpin(chan, i);

	vi_capture_release_syncpts(chan);
	capture_common_unpin_memory(&capture->requests);

	capture->channel_id = CAPTURE_CHANNEL_INVALID_ID;

	return 0;

submit_fail:
	return err;
}

static int vi_capture_read_syncpt(struct tegra_vi_channel *chan,
		struct syncpoint_info *sp, uint32_t *val)
{
	int err;

	if (sp->id) {
		err = nvhost_syncpt_read_ext_check(chan->ndev,
						sp->id, val);
		if (err < 0) {
			dev_err(chan->dev,
				"%s: get syncpt %i val failed\n", __func__,
				sp->id);
			return -EINVAL;
		}
	}

	return 0;
}

int vi_capture_get_info(struct tegra_vi_channel *chan,
		struct vi_capture_info *info)
{
	struct vi_capture *capture = chan->capture_data;
	int err;

	if (capture == NULL) {
		dev_err(chan->dev,
			 "%s: vi capture uninitialized\n", __func__);
		return -ENODEV;
	}

	if (capture->channel_id == CAPTURE_CHANNEL_INVALID_ID) {
		dev_err(chan->dev,
			"%s: setup channel first\n", __func__);
		return -ENODEV;
	}

	if (info == NULL)
		return -EINVAL;

	info->syncpts.progress_syncpt = capture->progress_sp.id;
	info->syncpts.emb_data_syncpt = capture->embdata_sp.id;
	info->syncpts.line_timer_syncpt = capture->linetimer_sp.id;

	err = vi_capture_read_syncpt(chan, &capture->progress_sp,
			&info->syncpts.progress_syncpt_val);
	if (err < 0)
		return err;
	err = vi_capture_read_syncpt(chan, &capture->embdata_sp,
			&info->syncpts.emb_data_syncpt_val);
	if (err < 0)
		return err;
	err = vi_capture_read_syncpt(chan, &capture->linetimer_sp,
			&info->syncpts.line_timer_syncpt_val);
	if (err < 0)
		return err;

	info->hw_channel_id = capture->channel_id;

	return 0;
}

int vi_capture_control_message(struct tegra_vi_channel *chan,
		struct vi_capture_control_msg *msg)
{
	struct vi_capture *capture = chan->capture_data;
	const void __user *msg_ptr;
	void __user *response;
	void *msg_cpy;
	struct CAPTURE_MSG_HEADER *header;
	uint32_t resp_id;
	struct CAPTURE_CONTROL_MSG *resp_msg = &capture->control_resp_msg;
	int err = 0;

	if (capture == NULL) {
		dev_err(chan->dev,
			 "%s: vi capture uninitialized\n", __func__);
		return -ENODEV;
	}

	if (msg->ptr == 0ull || msg->response == 0ull || msg->size == 0)
		return -EINVAL;

	msg_ptr = (const void __user *)(uintptr_t)msg->ptr;
	response = (void __user *)(uintptr_t)msg->response;

	msg_cpy = devm_kzalloc(chan->dev, msg->size, GFP_KERNEL);
	if (unlikely(msg_cpy == NULL))
		return -ENOMEM;

	err = copy_from_user(msg_cpy, msg_ptr, msg->size) ? -EFAULT : 0;
	if (err < 0)
		goto fail;
	header = (struct CAPTURE_MSG_HEADER *)msg_cpy;
	header->channel_id = capture->channel_id;

	switch (header->msg_id) {
	case CAPTURE_COMPAND_CONFIG_REQ:
		resp_id = CAPTURE_COMPAND_CONFIG_RESP;
		break;
	case CAPTURE_PDAF_CONFIG_REQ:
		resp_id = CAPTURE_PDAF_CONFIG_RESP;
		break;
	case CAPTURE_SYNCGEN_ENABLE_REQ:
		resp_id = CAPTURE_SYNCGEN_ENABLE_RESP;
		break;
	case CAPTURE_SYNCGEN_DISABLE_REQ:
		resp_id = CAPTURE_SYNCGEN_DISABLE_RESP;
		break;
	case CAPTURE_CHANNEL_TPG_SETUP_REQ:
		resp_id = CAPTURE_CHANNEL_TPG_SETUP_RESP;
		break;
	case CAPTURE_CHANNEL_TPG_START_REQ:
		resp_id = CAPTURE_CHANNEL_TPG_START_RESP;
		break;
	case CAPTURE_CHANNEL_TPG_STOP_REQ:
		resp_id = CAPTURE_CHANNEL_TPG_STOP_RESP;
		break;
	default:
		dev_err(chan->dev,
				"%s: unknown capture control req %x", __func__,
				header->msg_id);
		err = -EINVAL;
		goto fail;
	}

	err = vi_capture_ivc_send_control(chan, msg_cpy, msg->size, resp_id);
	if (err < 0)
		goto fail;

	err = copy_to_user(response, resp_msg,
			sizeof(*resp_msg)) ? -EFAULT : 0;

fail:
	devm_kfree(chan->dev, msg_cpy);
	return err;
}

struct surface_t {
	uint32_t offset;
	uint32_t offset_hi;
};

static void vi_capture_request_unpin(struct tegra_vi_channel *chan,
		uint32_t buffer_index)
{
	struct vi_capture *capture = chan->capture_data;
	struct capture_common_unpins *unpins;
	int i = 0;

	mutex_lock(&capture->unpins_list_lock);
	unpins = capture->unpins_list[buffer_index];
	if (unpins != NULL) {
		for (i = 0; i < unpins->num_unpins; i++)
			capture_common_unpin_memory(&unpins->data[i]);
		capture->unpins_list[buffer_index] = NULL;
		devm_kfree(chan->dev, unpins);
	}
	mutex_unlock(&capture->unpins_list_lock);
}

int vi_capture_request(struct tegra_vi_channel *chan,
		struct vi_capture_req *req)
{
	struct vi_capture *capture = chan->capture_data;
	struct CAPTURE_MSG capture_desc;
	struct capture_common_unpins *unpins = NULL;
	struct capture_common_pin_req cap_common_req = {0};
	int err = 0;

	if (capture == NULL) {
		dev_err(chan->dev,
			"%s: vi capture uninitialized\n", __func__);
		return -ENODEV;
	}

	if (capture->channel_id == CAPTURE_CHANNEL_INVALID_ID) {
		dev_err(chan->dev,
			"%s: setup channel first\n", __func__);
		return -ENODEV;
	}

	if (req == NULL) {
		dev_err(chan->dev,
			"%s: Invalid req\n", __func__);
		return -EINVAL;
	}

	if (req->num_relocs == 0) {
		dev_err(chan->dev,
			"%s: request must have non-zero relocs\n", __func__);
		return -EINVAL;
	}

	memset(&capture_desc, 0, sizeof(capture_desc));
	capture_desc.header.msg_id = CAPTURE_REQUEST_REQ;
	capture_desc.header.channel_id = capture->channel_id;
	capture_desc.capture_request_req.buffer_index = req->buffer_index;

	/* pin and reloc */
	cap_common_req.dev = chan->dev;
	cap_common_req.rtcpu_dev = capture->rtcpu_dev;
	cap_common_req.unpins = unpins;
	cap_common_req.requests = &capture->requests;
	cap_common_req.requests_dev = NULL;
	cap_common_req.request_size = capture->request_size;
	cap_common_req.request_offset = req->buffer_index *
			capture->request_size;
	cap_common_req.num_relocs = req->num_relocs;
	cap_common_req.reloc_user = (uint32_t __user *)
			(uintptr_t)req->reloc_relatives;

	err = capture_common_request_pin_and_reloc(&cap_common_req);
	if (err < 0) {
		dev_err(chan->dev, "request relocation failed\n");
		return err;
	}

	/* assign the unpins list to the capture to be unpinned and */
	/* freed at capture completion (vi_capture_request_unpin) */
	mutex_lock(&capture->unpins_list_lock);
	capture->unpins_list[req->buffer_index] = unpins;
	mutex_unlock(&capture->unpins_list_lock);

	dev_dbg(chan->dev, "%s: sending chan_id %u msg_id %u buf:%u\n",
			__func__, capture_desc.header.channel_id,
			capture_desc.header.msg_id, req->buffer_index);
	err = tegra_capture_ivc_capture_submit(&capture_desc,
			sizeof(capture_desc));
	if (err < 0) {
		dev_err(chan->dev, "IVC capture submit failed\n");
		goto fail;
	}

	return 0;

fail:
	vi_capture_request_unpin(chan, req->buffer_index);
	return err;
}

int vi_capture_status(struct tegra_vi_channel *chan,
		int32_t timeout_ms)
{
	struct vi_capture *capture = chan->capture_data;
	int ret = 0;

	if (capture == NULL) {
		dev_err(chan->dev,
			 "%s: vi capture uninitialized\n", __func__);
		return -ENODEV;
	}

	if (capture->channel_id == CAPTURE_CHANNEL_INVALID_ID) {
		dev_err(chan->dev,
			"%s: setup channel first\n", __func__);
		return -ENODEV;
	}

	dev_dbg(chan->dev, "%s: waiting for status, timeout:%d ms\n",
		__func__, timeout_ms);

	ret = wait_for_completion_killable_timeout(
			&capture->capture_resp,
			msecs_to_jiffies(timeout_ms));
	/* Possible return values:
	 * -ERESTARTSYS if interrupted, 0 if timed out, positive (at least 1,
	 * or number of jiffies left till timeout) if completed */
	if (ret <= 0) {
		dev_err(chan->dev,
			"no reply from camera processor\n");
		return -ETIMEDOUT;
	} else
		return 0;
}
