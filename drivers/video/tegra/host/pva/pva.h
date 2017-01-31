/*
 * drivers/video/tegra/host/pva/pva.h
 *
 * Tegra PVA header
 *
 * Copyright (c) 2016-2017, NVIDIA Corporation.  All rights reserved.
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

#ifndef __NVHOST_PVA_H__
#define __NVHOST_PVA_H__

#include <linux/dma-attrs.h>
#include <linux/mutex.h>

#include "nvhost_queue.h"
#include "pva_regs.h"

extern const struct file_operations tegra_pva_ctrl_ops;

/**
 * Queue count of 8 is maintained per PVA.
 */
#define MAX_PVA_QUEUE_COUNT 8

/**
 * Maximum task count that a queue can support
 */
#define MAX_PVA_TASK_COUNT	16

/**
 * @brief		struct to hold the segment details
 *
 * addr:		virtual addr of the segment from PRIV2 address base
 * size:		segment size
 * offset:		offset of the addr from priv2 base
 *
 */
struct pva_seg_info {
	void *addr;
	u32 size;
	u32 offset;
};

/**
 * @breif		struct to hold the segment details for debug purpose
 *
 * pva			Pointer to pva struct
 * seg_info		pva_seg_info struct
 *
 */
struct pva_crashdump_debugfs_entry {
	struct pva *pva;
	struct pva_seg_info seg_info;
};

/**
 * @brief		struct to handle dma alloc memory info
 *
 * size			size allocated
 * phys_addr		physical address
 * va			virtual address
 *
 */
struct pva_dma_alloc_info {
	size_t size;
	dma_addr_t pa;
	void *va;
};

/**
 * @brief		struct to handle the PVA firmware information
 *
 * hdr			pointer to the pva_code_hdr struct
 * priv1_buffer		pva_dma_alloc_info for priv1_buffer
 * priv2_buffer		pva_dma_alloc_info for priv2_buffer
 * priv2_reg_offset	priv2 register offset from uCode
 * attrs		dma_attrs struct information
 * trace_buffer_size	buffer size for trace log
 *
 */
struct pva_fw {
	struct pva_ucode_hdr *hdr;

	struct pva_dma_alloc_info priv1_buffer;
	struct pva_dma_alloc_info priv2_buffer;
	u32 priv2_reg_offset;
	struct dma_attrs attrs;

	u32 trace_buffer_size;
};

/**
 * @brief		Driver private data, shared with all applications
 *
 * pdev			Pointer to the PVA device
 * pool			Pointer to Queue table available for the PVA
 * fw_info		firmware information struct
 * irq			IRQ number obtained on registering the module
 * mailbox_mutex	Mutex to avoid concurrent mailbox accesses
 * mailbox_waitq	Mailbox waitqueue for response waiters
 * mailbox_status_regs	Response is stored into this structure temporarily
 * mailbox_status	Status of the mailbox interface
 * debugfs_entry_r5	debugfs segment information for r5
 * debugfs_entry_vpu0	debugfs segment information for vpu0
 * debugfs_entry_vpu1	debugfs segment information for vpu1
 *
 */
struct pva {
	struct platform_device *pdev;
	struct nvhost_queue_pool *pool;
	struct pva_fw fw_info;

	int irq;

	wait_queue_head_t mailbox_waitqueue;
	struct pva_mailbox_status_regs mailbox_status_regs;
	enum pva_mailbox_status mailbox_status;
	struct mutex mailbox_mutex;

	struct pva_crashdump_debugfs_entry debugfs_entry_r5;
	struct pva_crashdump_debugfs_entry debugfs_entry_vpu0;
	struct pva_crashdump_debugfs_entry debugfs_entry_vpu1;

	struct pva_dma_alloc_info priv1_dma;
	struct pva_dma_alloc_info priv2_dma;
};

/**
 * @brief	Finalize the PVA Power-on-Sequence.
 *
 * This function called from host subsystem driver after the PVA
 * partition has been brought up, clocks enabled and reset deasserted.
 * In production mode, the function needs to wait until the ready  bit
 * within the PVA aperture has been set. After that enable the PVA IRQ.
 * Register the queue priorities on the PVA.
 *
 * @param pdev	Pointer to PVA device
 * @return:	0 on Success or negative error code
 *
 */
int pva_finalize_poweron(struct platform_device *pdev);

/**
 * @brief	Prepare PVA poweroff.
 *
 * This function called from host subsystem driver before turning off
 * the PVA. The function should turn off the PVA IRQ.
 *
 * @param pdev	Pointer to PVA device
 * @return	0 on Success or negative error code
 *
 */
int pva_prepare_poweroff(struct platform_device *pdev);

/**
 * @brief	Register PVA ISR.
 *
 * This function called from driver to register the
 * PVA ISR with IRQ.
 *
 * @param pdev	Pointer to PVA device
 * @return	0 on Success or negative error code
 *
 */
int pva_register_isr(struct platform_device *dev);
/**
 * @brief	Initiallze pva debug utils
 *
 * @param pdev	Pointer to PVA device
 * @return	none
 *
 */
void pva_debugfs_init(struct platform_device *pdev);
#endif
