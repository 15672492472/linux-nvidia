/*
 * Tegra Graphics Init for T186 Architecture Chips
 *
 * Copyright (c) 2014-2015, NVIDIA Corporation.  All rights reserved.
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

#include <linux/slab.h>
#include <linux/io.h>
#include <linux/tegra-soc.h>

#include "dev.h"
#include "class_ids.h"
#include "class_ids_t186.h"

#include "t186.h"
#include "host1x/host1x.h"
#include "tsec/tsec.h"
#include "flcn/flcn.h"
#include "isp/isp.h"
#include "isp/isp_isr_v2.h"
#include "vi/vi.h"
#include "nvdec/nvdec.h"
#include "hardware_t186.h"

#include "chip_support.h"

static struct host1x_device_info host1x04_info = {
	.nb_channels	= T186_NVHOST_NUMCHANNELS,
	.ch_base	= 0,
	.ch_limit	= T186_NVHOST_NUMCHANNELS,
	.nb_mlocks	= NV_HOST1X_NB_MLOCKS,
	.initialize_chip_support = nvhost_init_t186_support,
	.nb_hw_pts	= NV_HOST1X_SYNCPT_NB_PTS,
	.nb_pts		= NV_HOST1X_SYNCPT_NB_PTS,
	.pts_base	= 0,
	.pts_limit	= NV_HOST1X_SYNCPT_NB_PTS,
	.syncpt_policy	= SYNCPT_PER_CHANNEL_INSTANCE,
	.channel_policy	= MAP_CHANNEL_ON_SUBMIT,
};

struct nvhost_device_data t18_host1x_info = {
	.clocks			= {{"host1x", UINT_MAX},
				   {"actmon", UINT_MAX}, {} },
	NVHOST_MODULE_NO_POWERGATE_IDS,
	.private_data		= &host1x04_info,
};

#ifdef CONFIG_TEGRA_GRHOST_ISP
struct nvhost_device_data t18_isp_info = {
	.num_channels		= 1,
	.moduleid		= NVHOST_MODULE_ISP,
	.class			= NV_VIDEO_STREAMING_ISP_CLASS_ID,
	.devfs_name		= "isp",
	.exclusive		= true,
	/* HACK: Mark as keepalive until 1188795 is fixed */
	.keepalive		= true,
#ifdef TEGRA_POWERGATE_VE
	.powergate_ids		= {TEGRA_POWERGATE_VE, -1},
#else
	NVHOST_MODULE_NO_POWERGATE_IDS,
#endif
	NVHOST_DEFAULT_CLOCKGATE_DELAY,
	.powergate_delay	= 500,
	.can_powergate		= true,
	.clocks			= {{ "isp", UINT_MAX, 0 }},
	.finalize_poweron	= nvhost_isp_t210_finalize_poweron,
	.prepare_poweroff	= nvhost_isp_t124_prepare_poweroff,
	.hw_init		= nvhost_isp_register_isr_v2,
	.ctrl_ops		= &tegra_isp_ctrl_ops,
	.serialize		= 1,
	.push_work_done		= 1,
	.resource_policy	= RESOURCE_PER_CHANNEL_INSTANCE,
};
#endif

#if defined(CONFIG_TEGRA_GRHOST_VI) || defined(CONFIG_TEGRA_GRHOST_VI_MODULE)
struct nvhost_device_data t18_vi_info = {
	.devfs_name		= "vi",
	.exclusive		= true,
	.class			= NV_VIDEO_STREAMING_VI_CLASS_ID,
	/* HACK: Mark as keepalive until 1188795 is fixed */
	.keepalive		= true,
#ifdef TEGRA_POWERGATE_VE
	.powergate_ids		= {TEGRA_POWERGATE_VE, -1},
#else
	NVHOST_MODULE_NO_POWERGATE_IDS,
#endif
	NVHOST_DEFAULT_CLOCKGATE_DELAY,
	.powergate_delay	= 500,
	.moduleid		= NVHOST_MODULE_VI,
	.clocks = {
		{"vi", UINT_MAX},
		{"csi", 0},
		{"cilab", 102000000},
		{"cilcd", 102000000},
		{"cile", 102000000},
		{"vii2c", 86400000},
		{"i2cslow", 1000000},
		{"emc", 0, NVHOST_MODULE_ID_EXTERNAL_MEMORY_CONTROLLER} },
	.ctrl_ops		= &tegra_vi_ctrl_ops,
	.num_channels		= 6,
	.prepare_poweroff = nvhost_vi_prepare_poweroff,
	.finalize_poweron = nvhost_vi_finalize_poweron,
	.serialize		= 1,
	.push_work_done		= 1,
	.resource_policy	= RESOURCE_PER_CHANNEL_INSTANCE,
};
#endif

struct nvhost_device_data t18_msenc_info = {
	.version		= NVHOST_ENCODE_FLCN_VER(6, 1),
	.devfs_name		= "msenc",
	.class			= NV_VIDEO_ENCODE_NVENC_CLASS_ID,
#ifdef TEGRA_POWERGATE_NVENC
	.powergate_ids		= { TEGRA_POWERGATE_NVENC, -1 },
#else
	NVHOST_MODULE_NO_POWERGATE_IDS,
#endif
	NVHOST_DEFAULT_CLOCKGATE_DELAY,
	.clocks			= {{"msenc", UINT_MAX, 0},
				   {"emc", UINT_MAX} },
	.poweron_reset		= true,
	.finalize_poweron	= nvhost_flcn_finalize_poweron,
	.moduleid		= NVHOST_MODULE_MSENC,
	.num_channels		= 1,
	.firmware_name		= "nvhost_nvenc061.fw",
	.serialize		= 1,
	.push_work_done		= 1,
	.resource_policy	= RESOURCE_PER_CHANNEL_INSTANCE,
};

struct nvhost_device_data t18_nvdec_info = {
	.version		= NVHOST_ENCODE_NVDEC_VER(3, 0),
	.devfs_name		= "nvdec",
	.class			= NV_NVDEC_CLASS_ID,
#ifdef TEGRA_POWERGATE_NVDEC
	.powergate_ids		= { TEGRA_POWERGATE_NVDEC, -1 },
#else
	NVHOST_MODULE_NO_POWERGATE_IDS,
#endif
	NVHOST_DEFAULT_CLOCKGATE_DELAY,
	.clocks			= {{"nvdec", UINT_MAX, 0},
				   {"emc", UINT_MAX} },
	.poweron_reset		= true,
	.finalize_poweron	= nvhost_nvdec_finalize_poweron,
	.moduleid		= NVHOST_MODULE_NVDEC,
	.ctrl_ops		= &tegra_nvdec_ctrl_ops,
	.num_channels		= 1,
	.serialize		= 1,
	.push_work_done		= 1,
	.resource_policy	= RESOURCE_PER_CHANNEL_INSTANCE,
};

struct nvhost_device_data t18_nvjpg_info = {
	.version		= NVHOST_ENCODE_FLCN_VER(1, 1),
	.devfs_name		= "nvjpg",
	.class			= NV_NVJPG_CLASS_ID,
#ifdef TEGRA_POWERGATE_NVJPG
	.powergate_ids		= { TEGRA_POWERGATE_NVJPG, -1 },
#else
	NVHOST_MODULE_NO_POWERGATE_IDS,
#endif
	NVHOST_DEFAULT_CLOCKGATE_DELAY,
	.powergate_delay	= 500,
	.can_powergate		= true,
	.clocks			= { {"nvjpg", UINT_MAX, 0},
				    {"emc", UINT_MAX} },
	.poweron_reset		= true,
	.finalize_poweron	= nvhost_flcn_finalize_poweron,
	.moduleid		= NVHOST_MODULE_NVJPG,
	.num_channels		= 1,
	.firmware_name		= "nvhost_nvjpg011.fw",
	.serialize		= 1,
	.push_work_done		= 1,
	.resource_policy	= RESOURCE_PER_CHANNEL_INSTANCE,
};

struct nvhost_device_data t18_tsec_info = {
	.num_channels		= 1,
	.devfs_name		= "tsec",
	.version		= NVHOST_ENCODE_TSEC_VER(1, 0),
	.class			= NV_TSEC_CLASS_ID,
	.clocks			= {{"tsec", UINT_MAX, 0, TEGRA_MC_CLIENT_TSEC},
				   {"emc"} },
	NVHOST_MODULE_NO_POWERGATE_IDS,
	NVHOST_DEFAULT_CLOCKGATE_DELAY,
	.keepalive		= true,
	.moduleid		= NVHOST_MODULE_TSEC,
	.poweron_reset		= true,
	.finalize_poweron	= nvhost_tsec_finalize_poweron,
	.prepare_poweroff	= nvhost_tsec_prepare_poweroff,
	.serialize		= 1,
	.push_work_done		= 1,
	.resource_policy	= RESOURCE_PER_CHANNEL_INSTANCE,
};

struct nvhost_device_data t18_tsecb_info = {
	.num_channels		= 1,
	.devfs_name		= "tsecb",
	.version		= NVHOST_ENCODE_TSEC_VER(1, 0),
	.class			= NV_TSECB_CLASS_ID,
	.clocks			= {{"tsecb", UINT_MAX, 0, TEGRA_MC_CLIENT_TSECB},
				   {"emc"} },
	NVHOST_MODULE_NO_POWERGATE_IDS,
	NVHOST_DEFAULT_CLOCKGATE_DELAY,
	.keepalive		= true,
	.poweron_reset		= true,
	.finalize_poweron	= nvhost_tsec_finalize_poweron,
	.prepare_poweroff	= nvhost_tsec_prepare_poweroff,
	.serialize		= 1,
	.push_work_done		= 1,
	.resource_policy	= RESOURCE_PER_CHANNEL_INSTANCE,
};

struct nvhost_device_data t18_vic_info = {
	.num_channels		= 1,
	.devfs_name		= "vic",
	.clocks			= {{"vic03", UINT_MAX, 0},
				   {"emc", UINT_MAX,
				   NVHOST_MODULE_ID_EXTERNAL_MEMORY_CONTROLLER},
				   {"vic_floor", 0,
				   NVHOST_MODULE_ID_CBUS_FLOOR},
				   {"emc_shared", 0,
				   NVHOST_MODULE_ID_EMC_SHARED}, {} },
	.version		= NVHOST_ENCODE_FLCN_VER(4, 0),
#ifdef TEGRA_POWERGATE_VIC
	.powergate_ids	= { TEGRA_POWERGATE_VIC, -1 },
#else
	NVHOST_MODULE_NO_POWERGATE_IDS,
#endif
	NVHOST_DEFAULT_CLOCKGATE_DELAY,
	.moduleid		= NVHOST_MODULE_VIC,
	.poweron_reset		= true,
	.class			= NV_GRAPHICS_VIC_CLASS_ID,
	.finalize_poweron	= nvhost_vic_finalize_poweron,
	.firmware_name		= "vic04_ucode.bin",
	.serialize		= 1,
	.push_work_done		= 1,
	.resource_policy	= RESOURCE_PER_CHANNEL_INSTANCE,
};

struct nvhost_device_data t18_nvcsi_info = {
	.num_channels		= 1,
	.clocks			= {{"nvcsi", UINT_MAX, 0}},
	.devfs_name		= "nvcsi",
	.class			= NV_VIDEO_STREAMING_NVCSI_CLASS_ID,
	NVHOST_MODULE_NO_POWERGATE_IDS,
	NVHOST_DEFAULT_CLOCKGATE_DELAY,
	.serialize		= 1,
	.push_work_done		= 1,
	.resource_policy	= RESOURCE_PER_CHANNEL_INSTANCE,
};

#include "host1x/host1x_channel_t186.c"

static void t186_set_nvhost_chanops(struct nvhost_channel *ch)
{
	if (ch)
		ch->ops = host1x_channel_ops;
}

int nvhost_init_t186_channel_support(struct nvhost_master *host,
				     struct nvhost_chip_support *op)
{
	op->nvhost_dev.set_nvhost_chanops = t186_set_nvhost_chanops;

	return 0;
}

static void t186_remove_support(struct nvhost_chip_support *op)
{
	kfree(op->priv);
	op->priv = NULL;
}

#include "host1x/host1x_cdma_t186.c"
#include "host1x/host1x_syncpt.c"
#include "host1x/host1x_syncpt_prot_t186.c"
#include "host1x/host1x_intr_t186.c"
#include "host1x/host1x_debug_t186.c"

int nvhost_init_t186_support(struct nvhost_master *host,
			     struct nvhost_chip_support *op)
{
	int err;

	op->soc_name = "tegra18x";

	/* create a symlink for host1x if it is not under platform bus or
	 * it has been created with different name */

	if ((host->dev->dev.parent != &platform_bus) ||
	    !strcmp(dev_name(&host->dev->dev), "host1x")) {
		err = sysfs_create_link(&platform_bus.kobj,
					&host->dev->dev.kobj,
					"host1x");
		if (err)
			dev_warn(&host->dev->dev, "could not create sysfs links\n");
	}

	/* don't worry about cleaning up on failure... "remove" does it. */
	err = nvhost_init_t186_channel_support(host, op);
	if (err)
		return err;

	op->cdma = host1x_cdma_ops;
	op->push_buffer = host1x_pushbuffer_ops;
	op->debug = host1x_debug_ops;

	host->sync_aperture = host->aperture;
	op->syncpt = host1x_syncpt_ops;
	op->intr = host1x_intr_ops;

	/* WAR to bugs 200094901 and 200082771: enable protection
	 * only on silicon/emulation */

	if (!tegra_platform_is_linsim()) {
		op->syncpt.reset = t186_syncpt_reset;
		op->syncpt.mark_used = t186_syncpt_mark_used;
		op->syncpt.mark_unused = t186_syncpt_mark_unused;
	}

	op->remove_support = t186_remove_support;

	return 0;
}
