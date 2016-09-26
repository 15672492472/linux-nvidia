/*
 * Tegra Graphics Init for T194 Architecture Chips
 *
 * Copyright (c) 2016, NVIDIA Corporation. All rights reserved.
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
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/tegra-soc.h>
#include <linux/platform/tegra/emc_bwmgr.h>

#include <linux/platform/tegra/tegra18_kfuse.h>

#include "dev.h"
#include "class_ids.h"
#include "class_ids_t194.h"

#include "t194.h"
#include "host1x/host1x.h"
#if defined(CONFIG_TEGRA_GRHOST_TSEC)
#include "tsec/tsec.h"
#endif
#include "flcn/flcn.h"
#if defined(CONFIG_TEGRA_GRHOST_NVDEC)
#include "nvdec/nvdec.h"
#endif
#if defined(CONFIG_TEGRA_GRHOST_PVA)
#include "pva/pva.h"
#endif
#include "hardware_t194.h"
#if defined(CONFIG_TEGRA_GRHOST_NVDLA)
#include "nvdla/nvdla.h"
#endif

#include "chip_support.h"

#include "streamid_regs.c"

#define HOST_EMC_FLOOR 204000000
#define HOST_NVDEC_EMC_FLOOR 102000000

/*
 * TODO: Move following functions to the corresponding files under
 * kernel-3.18 once kernel-t19x gets merged there. Until that
 * happens we can keep these here to avoid extensive amount of
 * added infra
 */

static inline u32 flcn_thi_sec(void)
{
	return 0x00000038;
}

static inline u32 flcn_thi_sec_ch_lock(void)
{
	return (1 << 8);
}

static dma_addr_t nvhost_t194_get_reloc_phys_addr(dma_addr_t phys_addr,
						  u32 reloc_type)
{
	if (reloc_type == NVHOST_RELOC_TYPE_BLOCK_LINEAR)
		phys_addr += BIT(39);

	return phys_addr;
}

#if defined(CONFIG_TEGRA_GRHOST_TSEC)
static int nvhost_tsec_t194_finalize_poweron(struct platform_device *dev)
{
	/* Disable access to non-THI registers through channel */
	host1x_writel(dev, flcn_thi_sec(), flcn_thi_sec_ch_lock());

	return nvhost_tsec_finalize_poweron(dev);
}
#endif

static int nvhost_flcn_t194_finalize_poweron(struct platform_device *dev)
{
	/* Disable access to non-THI registers through channel */
	host1x_writel(dev, flcn_thi_sec(), flcn_thi_sec_ch_lock());

	return nvhost_flcn_finalize_poweron(dev);
}

#if defined(CONFIG_TEGRA_GRHOST_NVDEC)
static int nvhost_nvdec_t194_finalize_poweron(struct platform_device *dev)
{
	int ret;

	if (!tegra_platform_is_vdk()) {
		ret = tegra_kfuse_enable_sensing();
		if (ret)
			return ret;
	}

	/* Disable access to non-THI registers through channel */
	host1x_writel(dev, flcn_thi_sec(), flcn_thi_sec_ch_lock());

	ret = nvhost_nvdec_finalize_poweron(dev);
	if (ret)
		tegra_kfuse_disable_sensing();

	return ret;
}

static int nvhost_nvdec_t194_prepare_poweroff(struct platform_device *dev)
{
	if (!tegra_platform_is_vdk())
		tegra_kfuse_disable_sensing();

	return 0;
}
#endif

static struct host1x_device_info host1x04_info = {
	.nb_channels	= T194_NVHOST_NUMCHANNELS,
	.ch_base	= 0,
	.ch_limit	= T194_NVHOST_NUMCHANNELS,
	.nb_mlocks	= NV_HOST1X_NB_MLOCKS,
	.initialize_chip_support = nvhost_init_t194_support,
	.nb_hw_pts	= NV_HOST1X_SYNCPT_NB_PTS,
	.nb_pts		= NV_HOST1X_SYNCPT_NB_PTS,
	.pts_base	= 0,
	.pts_limit	= NV_HOST1X_SYNCPT_NB_PTS,
	.syncpt_policy	= SYNCPT_PER_CHANNEL_INSTANCE,
	.channel_policy	= MAP_CHANNEL_ON_SUBMIT,
	.firmware_area_size = SZ_1M,
	.nb_actmons	= 1,
};

struct nvhost_device_data t19_host1x_info = {
	.clocks			= {
		{"host1x", UINT_MAX},
		{"actmon", UINT_MAX}
	},
	NVHOST_MODULE_NO_POWERGATE_ID,
	.autosuspend_delay      = 50,
	.private_data		= &host1x04_info,
	.finalize_poweron	= nvhost_host1x_finalize_poweron,
	.prepare_poweroff	= nvhost_host1x_prepare_poweroff,
	.isolate_contexts	= true,
};

static struct host1x_device_info host1xb04_info = {
	.nb_channels	= T194_NVHOST_NUMCHANNELS,
	.ch_base	= 0,
	.ch_limit	= T194_NVHOST_NUMCHANNELS,
	.nb_mlocks	= NV_HOST1X_NB_MLOCKS,
	.initialize_chip_support = nvhost_init_t194_support,
	.nb_hw_pts	= NV_HOST1X_SYNCPT_NB_PTS,
	.nb_pts		= NV_HOST1X_SYNCPT_NB_PTS,
	.pts_base	= 0,
	.pts_limit	= NV_HOST1X_SYNCPT_NB_PTS,
	.syncpt_policy	= SYNCPT_PER_CHANNEL_INSTANCE,
	.channel_policy	= MAP_CHANNEL_ON_SUBMIT,
};

struct nvhost_device_data t19_host1xb_info = {
	.clocks			= {
		{"host1x", UINT_MAX},
		{"actmon", UINT_MAX}
	},
	NVHOST_MODULE_NO_POWERGATE_ID,
	.private_data		= &host1xb04_info,
};

#if defined(CONFIG_TEGRA_GRHOST_NVENC)
struct nvhost_device_data t19_msenc_info = {
	.version		= NVHOST_ENCODE_FLCN_VER(7, 0),
	.devfs_name		= "msenc",
	.class			= NV_VIDEO_ENCODE_NVENC_CLASS_ID,
	.modulemutexes		= {NV_HOST1X_MLOCK_ID_NVENC},
	.autosuspend_delay      = 500,
	.clocks			= {
		{"nvenc", UINT_MAX},
		{"emc", HOST_EMC_FLOOR,
		 NVHOST_MODULE_ID_EXTERNAL_MEMORY_CONTROLLER,
		 0, TEGRA_BWMGR_SET_EMC_SHARED_BW}
	},
	.poweron_reset		= true,
	.finalize_poweron	= nvhost_flcn_t194_finalize_poweron,
	.moduleid		= NVHOST_MODULE_MSENC,
	.num_channels		= 1,
	.firmware_name		= "nvhost_nvenc070.fw",
	.serialize		= true,
	.push_work_done		= true,
	.resource_policy	= RESOURCE_PER_CHANNEL_INSTANCE,
	.vm_regs		= {{0x30, true}, {0x34, false} },
	.bwmgr_client_id	= TEGRA_BWMGR_CLIENT_MSENC,
	.get_reloc_phys_addr	= nvhost_t194_get_reloc_phys_addr,
};
#endif

#if defined(CONFIG_TEGRA_GRHOST_NVDEC)
struct nvhost_device_data t19_nvdec_info = {
	.version		= NVHOST_ENCODE_NVDEC_VER(4, 0),
	.devfs_name		= "nvdec",
	.modulemutexes		= {NV_HOST1X_MLOCK_ID_NVDEC},
	.class			= NV_NVDEC_CLASS_ID,
	.autosuspend_delay      = 500,
	.clocks			= {
		{"nvdec", UINT_MAX},
		{"kfuse", 0, 0},
		{"emc", HOST_NVDEC_EMC_FLOOR,
		 NVHOST_MODULE_ID_EXTERNAL_MEMORY_CONTROLLER,
		 0, TEGRA_BWMGR_SET_EMC_FLOOR}
	},
	.poweron_reset		= true,
	.finalize_poweron	= nvhost_nvdec_t194_finalize_poweron,
	.prepare_poweroff	= nvhost_nvdec_t194_prepare_poweroff,
	.moduleid		= NVHOST_MODULE_NVDEC,
	.ctrl_ops		= &tegra_nvdec_ctrl_ops,
	.num_channels		= 1,
	.serialize		= true,
	.push_work_done		= true,
	.resource_policy	= RESOURCE_PER_CHANNEL_INSTANCE,
	.vm_regs		= {{0x30, true}, {0x34, false} },
	.bwmgr_client_id	= TEGRA_BWMGR_CLIENT_NVDEC,
};
#endif

#if defined(CONFIG_TEGRA_GRHOST_NVJPG)
struct nvhost_device_data t19_nvjpg_info = {
	.version		= NVHOST_ENCODE_FLCN_VER(1, 2),
	.devfs_name		= "nvjpg",
	.modulemutexes		= {NV_HOST1X_MLOCK_ID_NVJPG},
	.class			= NV_NVJPG_CLASS_ID,
	.autosuspend_delay      = 500,
	.clocks			= {
		{"nvjpg", UINT_MAX},
		{"emc", HOST_EMC_FLOOR,
		 NVHOST_MODULE_ID_EXTERNAL_MEMORY_CONTROLLER,
		 0, TEGRA_BWMGR_SET_EMC_SHARED_BW}
	},
	.poweron_reset		= true,
	.finalize_poweron	= nvhost_flcn_t194_finalize_poweron,
	.moduleid		= NVHOST_MODULE_NVJPG,
	.num_channels		= 1,
	.firmware_name		= "nvhost_nvjpg012.fw",
	.serialize		= true,
	.push_work_done		= true,
	.resource_policy	= RESOURCE_PER_CHANNEL_INSTANCE,
	.vm_regs		= {{0x30, true}, {0x34, false} },
	.bwmgr_client_id	= TEGRA_BWMGR_CLIENT_NVJPG,
};
#endif

#if defined(CONFIG_TEGRA_GRHOST_TSEC)
struct nvhost_device_data t19_tsec_info = {
	.num_channels		= 1,
	.devfs_name		= "tsec",
	.version		= NVHOST_ENCODE_TSEC_VER(1, 0),
	.modulemutexes		= {NV_HOST1X_MLOCK_ID_TSEC},
	.class			= NV_TSEC_CLASS_ID,
	.clocks			= {
		{"tsec", UINT_MAX},
		{"emc", HOST_EMC_FLOOR,
		 NVHOST_MODULE_ID_EXTERNAL_MEMORY_CONTROLLER,
		 0, TEGRA_BWMGR_SET_EMC_FLOOR}
	},
	NVHOST_MODULE_NO_POWERGATE_ID,
	.autosuspend_delay      = 500,
	.keepalive		= true,
	.moduleid		= NVHOST_MODULE_TSEC,
	.poweron_reset		= true,
	.finalize_poweron	= nvhost_tsec_t194_finalize_poweron,
	.prepare_poweroff	= nvhost_tsec_prepare_poweroff,
	.serialize		= true,
	.push_work_done		= true,
	.resource_policy	= RESOURCE_PER_CHANNEL_INSTANCE,
	.vm_regs		= {{0x30, true}, {0x34, false} },
	.bwmgr_client_id	= TEGRA_BWMGR_CLIENT_TSEC,
};

struct nvhost_device_data t19_tsecb_info = {
	.num_channels		= 1,
	.devfs_name		= "tsecb",
	.version		= NVHOST_ENCODE_TSEC_VER(1, 0),
	.modulemutexes		= {NV_HOST1X_MLOCK_ID_TSECB},
	.class			= NV_TSECB_CLASS_ID,
	.clocks			= {
		{"tsecb", UINT_MAX},
		{"emc", HOST_EMC_FLOOR,
		 NVHOST_MODULE_ID_EXTERNAL_MEMORY_CONTROLLER,
		 0, TEGRA_BWMGR_SET_EMC_FLOOR}
	},
	NVHOST_MODULE_NO_POWERGATE_ID,
	.autosuspend_delay      = 500,
	.keepalive		= true,
	.poweron_reset		= true,
	.finalize_poweron	= nvhost_tsec_t194_finalize_poweron,
	.prepare_poweroff	= nvhost_tsec_prepare_poweroff,
	.serialize		= true,
	.push_work_done		= true,
	.resource_policy	= RESOURCE_PER_CHANNEL_INSTANCE,
	.vm_regs		= {{0x30, true}, {0x34, false} },
	.bwmgr_client_id	= TEGRA_BWMGR_CLIENT_TSECB,
};
#endif

#if defined(CONFIG_TEGRA_GRHOST_VIC)
struct nvhost_device_data t19_vic_info = {
	.num_channels		= 1,
	.devfs_name		= "vic",
	.clocks			= {
		{"vic", UINT_MAX, 0},
		{"emc", UINT_MAX, NVHOST_MODULE_ID_EXTERNAL_MEMORY_CONTROLLER,
		 0, TEGRA_BWMGR_SET_EMC_SHARED_BW},
	},
	.version		= NVHOST_ENCODE_FLCN_VER(4, 0),
	.autosuspend_delay      = 500,
	.moduleid		= NVHOST_MODULE_VIC,
	.poweron_reset		= true,
	.modulemutexes		= {NV_HOST1X_MLOCK_ID_VIC},
	.class			= NV_GRAPHICS_VIC_CLASS_ID,
	.finalize_poweron	= nvhost_flcn_t194_finalize_poweron,
	.init_class_context	= nvhost_vic_init_context,
	.firmware_name		= "vic04_ucode.bin",
	.serialize		= true,
	.push_work_done		= true,
	.resource_policy	= RESOURCE_PER_CHANNEL_INSTANCE,
	.vm_regs		= {{0x30, true}, {0x34, false} },
	.bwmgr_client_id	= TEGRA_BWMGR_CLIENT_VIC,
	.get_reloc_phys_addr	= nvhost_t194_get_reloc_phys_addr,
};
#endif

#if defined(CONFIG_TEGRA_GRHOST_PVA)
struct nvhost_device_data t19_pvab_info = {
	.num_channels		= 1,
	.clocks			= {
		{"pva"},
	},
	.ctrl_ops		= &tegra_pva_ctrl_ops,
	.devfs_name		= "pvab",
	.class			= NV_PVA1_CLASS_ID,
	.autosuspend_delay      = 500,
	.finalize_poweron	= pva_finalize_poweron,
	.prepare_poweroff	= pva_prepare_poweroff,
	.firmware_name		= "pva.bin",
	.resource_policy	= RESOURCE_PER_CHANNEL_INSTANCE,
	.poweron_reset		= true,
	.serialize		= true,
	.push_work_done		= true,
	.get_reloc_phys_addr	= nvhost_t194_get_reloc_phys_addr,
};

struct nvhost_device_data t19_pvaa_info = {
	.num_channels		= 1,
	.clocks			= {
		{"pva"},
	},
	.ctrl_ops		= &tegra_pva_ctrl_ops,
	.devfs_name		= "pvaa",
	.class			= NV_PVA0_CLASS_ID,
	.autosuspend_delay      = 500,
	.finalize_poweron	= pva_finalize_poweron,
	.prepare_poweroff	= pva_prepare_poweroff,
	.firmware_name		= "pva.bin",
	.resource_policy	= RESOURCE_PER_CHANNEL_INSTANCE,
	.poweron_reset		= true,
	.serialize		= true,
	.push_work_done		= true,
	.get_reloc_phys_addr	= nvhost_t194_get_reloc_phys_addr,
};
#endif

#if defined(CONFIG_TEGRA_GRHOST_NVDLA)
struct nvhost_device_data t19_nvdla0_info = {
	.devfs_name		= "nvdla0",
	.class			= NV_DLA0_CLASS_ID,
	.clocks			= {
		{"nvdla0", UINT_MAX},
		{"nvdla0_flcn", UINT_MAX},
		{"emc", HOST_EMC_FLOOR,
		 NVHOST_MODULE_ID_EXTERNAL_MEMORY_CONTROLLER,
		 0, TEGRA_BWMGR_SET_EMC_FLOOR}
	},
	.resource_policy	= RESOURCE_PER_CHANNEL_INSTANCE,
	.finalize_poweron	= nvhost_nvdla_finalize_poweron,
	.prepare_poweroff	= nvhost_nvdla_prepare_poweroff,
	.flcn_isr               = nvhost_nvdla_flcn_isr,
	.firmware_name		= "nvdla.bin",
	.autosuspend_delay      = 500,
	.keepalive		= true,
	.poweron_reset		= true,
	.serialize		= true,
	.push_work_done		= true,
	.ctrl_ops		= &tegra_nvdla_ctrl_ops,
	.get_reloc_phys_addr	= nvhost_t194_get_reloc_phys_addr,
	.module_irq		= 1,
};

struct nvhost_device_data t19_nvdla1_info = {
	.devfs_name		= "nvdla1",
	.class			= NV_DLA1_CLASS_ID,
	.clocks			= {
		{"nvdla", UINT_MAX},
		{"emc", HOST_EMC_FLOOR,
		 NVHOST_MODULE_ID_EXTERNAL_MEMORY_CONTROLLER,
		 0, TEGRA_BWMGR_SET_EMC_FLOOR}
	},
	.resource_policy	= RESOURCE_PER_CHANNEL_INSTANCE,
	.finalize_poweron	= nvhost_nvdla_finalize_poweron,
	.prepare_poweroff	= nvhost_nvdla_prepare_poweroff,
	.flcn_isr               = nvhost_nvdla_flcn_isr,
	.autosuspend_delay      = 500,
	.keepalive		= true,
	.poweron_reset		= true,
	.serialize		= true,
	.push_work_done		= true,
	.ctrl_ops		= &tegra_nvdla_ctrl_ops,
	.get_reloc_phys_addr	= nvhost_t194_get_reloc_phys_addr,
};
#endif

#include "host1x/host1x_channel_t186.c"

static void t194_set_nvhost_chanops(struct nvhost_channel *ch)
{
	if (!ch)
		return;

	ch->ops = host1x_channel_ops;

	/* Disable gather filter in simulator */
	if (tegra_platform_is_linsim() || tegra_platform_is_vdk())
		ch->ops.init_gather_filter = NULL;
}

int nvhost_init_t194_channel_support(struct nvhost_master *host,
				     struct nvhost_chip_support *op)
{
	op->nvhost_dev.set_nvhost_chanops = t194_set_nvhost_chanops;

	return 0;
}

static void t194_remove_support(struct nvhost_chip_support *op)
{
	kfree(op->priv);
	op->priv = NULL;
}

static void t194_init_regs(struct platform_device *pdev, bool prod)
{
	struct nvhost_streamid_mapping *map_regs = t19x_host1x_streamid_mapping;

	/* simulator cannot handle following writes - skip them */
	if (tegra_platform_is_linsim() || tegra_platform_is_vdk())
		return;

	while (map_regs->host1x_offset) {
		host1x_hypervisor_writel(pdev,
					 map_regs->host1x_offset,
					 map_regs->client_offset);
		host1x_hypervisor_writel(pdev,
					 map_regs->host1x_offset + sizeof(u32),
					 map_regs->client_limit);
		map_regs++;
	}
}

#include "host1x/host1x_cdma_t186.c"
#include "host1x/host1x_syncpt.c"
#include "host1x/host1x_syncpt_prot_t186.c"
#include "host1x/host1x_intr_t186.c"
#include "host1x/host1x_debug_t186.c"
#include "host1x/host1x_vm_t186.c"
#include "host1x/host1x_actmon_t186.c"

int nvhost_init_t194_support(struct nvhost_master *host,
			     struct nvhost_chip_support *op)
{
	int err;

	op->soc_name = "tegra19x";

	/* don't worry about cleaning up on failure... "remove" does it. */
	err = nvhost_init_t194_channel_support(host, op);
	if (err)
		return err;

	op->cdma = host1x_cdma_ops;
	op->push_buffer = host1x_pushbuffer_ops;
	op->debug = host1x_debug_ops;

	host->sync_aperture = host->aperture;
	op->syncpt = host1x_syncpt_ops;
	op->intr = host1x_intr_ops;
	op->vm = host1x_vm_ops;
	op->actmon = host1x_actmon_ops;
	op->nvhost_dev.load_gating_regs = t194_init_regs;

	/* WAR to bugs 200094901 and 200082771: enable protection
	 * only on silicon/emulation */

	if (!tegra_platform_is_linsim() && !tegra_platform_is_vdk()) {
		op->syncpt.reset = t186_syncpt_reset;
		op->syncpt.mark_used = t186_syncpt_mark_used;
		op->syncpt.mark_unused = t186_syncpt_mark_unused;
	}
	op->syncpt.mutex_owner = t186_syncpt_mutex_owner;

	op->remove_support = t194_remove_support;

	return 0;
}
