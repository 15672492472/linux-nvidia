/*
 * Tegra Graphics Chip support for T186
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
#ifndef _NVHOST_T186_H_
#define _NVHOST_T186_H_

#include "chip_support.h"

#define T186_NVHOST_NUMCHANNELS 63

extern struct nvhost_device_data t18_host1x_info;
extern struct nvhost_device_data t18_host1xb_info;
extern struct nvhost_device_data t18_vic_info;
extern struct nvhost_device_data t18_nvdec_info;
extern struct nvhost_device_data t18_nvjpg_info;
extern struct nvhost_device_data t18_msenc_info;
extern struct nvhost_device_data t18_isp_info;
extern struct nvhost_device_data t18_vi_info;
extern struct nvhost_device_data t18_tsec_info;
extern struct nvhost_device_data t18_tsecb_info;
extern struct nvhost_device_data t18_nvcsi_info;

int nvhost_init_t186_support(struct nvhost_master *host,
			     struct nvhost_chip_support *op);
int nvhost_init_t186_channel_support(struct nvhost_master *host,
			     struct nvhost_chip_support *op);

#endif /* _NVHOST_T186_H_ */
