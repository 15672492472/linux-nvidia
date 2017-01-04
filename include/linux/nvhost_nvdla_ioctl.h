/*
 * include/linux/nvhost_nvdla_ioctl.h
 *
 * Tegra NvDLA Driver
 *
 * Copyright (c) 2016-2017, NVIDIA CORPORATION.  All rights reserved.
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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __LINUX_NVHOST_NVDLA_IOCTL_H
#define __LINUX_NVHOST_NVDLA_IOCTL_H

#include <linux/ioctl.h>
#include <linux/types.h>

#if !defined(__KERNEL__)
#define __user
#endif

/**
 * struct nvdla_queue_stat_args strcture
 *
 * @status		queue status flags
 *
 */
struct nvdla_queue_status_args {
#define NVDLA_QUEUE_FLAGS_SUSPEND	(1 << 0)
#define NVDLA_QUEUE_FLAGS_RESUME	(1 << 1)
	__u64 status;
};

/**
 * struct nvdla_ping_args structure for ping data
 *
 * @in_challenge	challenge data to be sent
 * @out_response	response/CRC on challenge data from engine
 *
 */
struct nvdla_ping_args {
	__u32 in_challenge;
	__u32 out_response;
};


/**
 * struct nvdla_pin_unpin_args strcture args for buffer pin/unpin
 *
 * @buffers		list of buffers to pin/unpin'ed
 * @num_buffers		number of buffers count
 * @reserved		reserved for future use
 *
 */
struct nvdla_pin_unpin_args {
	__u64 buffers;
	__u32 num_buffers;
	__u32 reserved;
};

/**
 * struct nvdla_submit_args structure for task submit
 *
 * @tasks		pointer to task list
 * @num_tasks		number of tasks count
 * @flags		flags for task submit, like atomic
 * @version		version of task structure
 *
 */
struct nvdla_submit_args {
	__u64 tasks;
	__u16 num_tasks;
#define MAX_TASKS_PER_SUBMIT	32
#define NVDLA_SUBMIT_FLAGS_ATOMIC	(1 << 0)
	__u16 flags;
	__u32 version;
};

/**
 * struct nvdla_mem_handle structure for memory handles
 *
 * @handle		handle to buffer allocated in userspace
 * @offset		offset in buffer
 *
 */
struct nvdla_mem_handle {
	__u32 handle;
	__u32 offset;
};

/**
 * struct nvdla_ioctl_submit_task structure for single task information
 *
 * @num_prefences		number of pre-fences in task
 * @num_postfences		number of post-fences in task
 * @num_input_task_status	number of input task status
 * @num_output_task_status	number of output task status
 * @flags			flags for bitwise task info embeddeing
 * @reserved			reserved for future use
 * @prefences			pointer to pre-fence struct table
 * @postfences			pointer to post-fence struct table
 * @input_task_status		pointer to input task status struct table
 * @output_task_status		pointer to output task status struct table
 * @num_addresses		total number of addressed passed in structure
 * @address_list		pointer to address list
 *
 */
struct nvdla_ioctl_submit_task {
	__u8 num_prefences;
	__u8 num_postfences;
	__u8 num_input_task_status;
	__u8 num_output_task_status;
	__u16 flags;
	__u16 reserved;

	__u64 prefences;
	__u64 postfences;

	__u64 input_task_status;
	__u64 output_task_status;

	__u32 num_addresses;
	struct nvdla_mem_handle address_list;
};

/**
 * struct nvdla_fence structure for passing fence information
 *
 * @type			type of fence: syncpoint, Linux Sync Fd
 * @syncpoint_index		syncpoint id
 * @syncpoint_value		value of syncpoint id
 * @sync_fd			Linux sync FD handle
 */
struct nvdla_fence {
	__u32 type;
#define NVDLA_FENCE_TYPE_SYNCPT		0
#define NVDLA_FENCE_TYPE_SYNC_FD	1
#define NVDLA_FENCE_TYPE_SEMAPHORE	2
#define NVDLA_FENCE_TYPE_TS_SEMAPHORE	3
	__u32 syncpoint_index;
	__u32 syncpoint_value;
	__u32 sync_fd;
	__u32 sem_handle;
	__u32 sem_offset;
	__u32 sem_val;
};

/**
 * struct nvdla_status_notify structure for passing status notify information
 *
 * @handle		handle to buffer allocated in userspace
 * @offset		offset in buffer
 * @status		status
 *
 */
struct nvdla_status_notify {
	__u32 handle;
	__u32 offset;
	__u32 status;
};

#define NVHOST_NVDLA_IOCTL_MAGIC 'D'

#define NVDLA_IOCTL_PING		\
		_IOWR(NVHOST_NVDLA_IOCTL_MAGIC, 1, struct nvdla_ping_args)
#define NVDLA_IOCTL_PIN   \
	_IOW(NVHOST_NVDLA_IOCTL_MAGIC, 2, struct nvdla_pin_unpin_args)
#define NVDLA_IOCTL_UNPIN \
	_IOW(NVHOST_NVDLA_IOCTL_MAGIC, 3, struct nvdla_pin_unpin_args)
#define NVDLA_IOCTL_SUBMIT \
	_IOW(NVHOST_NVDLA_IOCTL_MAGIC, 4, struct nvdla_submit_args)
#define NVDLA_IOCTL_SET_QUEUE_STATUS \
	_IOW(NVHOST_NVDLA_IOCTL_MAGIC, 5, struct nvdla_queue_status_args)
#define NVDLA_IOCTL_LAST		\
		_IOC_NR(NVDLA_IOCTL_SET_QUEUE_STATUS)

#define NVDLA_IOCTL_MAX_ARG_SIZE  \
		sizeof(struct nvdla_pin_unpin_args)

#endif /* __LINUX_NVHOST_NVDLA_IOCTL_H */
