/*
 * NVHOST Queue management header for T194
 *
 * Copyright (c) 2016, NVIDIA Corporation.  All rights reserved.
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

#ifndef __NVHOST_NVHOST_QUEUE_H__
#define __NVHOST_NVHOST_QUEUE_H__

#include <linux/kref.h>

/**
 * struct nvhost_queue - Information needed in a Queue
 *
 * @pool:	pointer queue pool
 * @kref:	struct kref for reference count
 * @syncpt_id:	Host1x syncpt id
 * @id:		Queue id
 * @list_lock	mutex for tasks lists control
 * @tasklist	Head of tasks list
 * @sequence	monotonically incrementing task id per queue
 *
 */
struct nvhost_queue {
	struct nvhost_queue_pool *pool;
	struct kref kref;
	u32 syncpt_id;
	u32 id;
	struct mutex list_lock;
	struct list_head tasklist;
	u32 sequence;
};

/**
 * struct nvhost_queue_ops - hardware specific queue callbacks
 *
 * @abort:	abort all tasks from a queue
 * @submit:	submit the given list of tasks to hardware
 *
 */
struct nvhost_queue_ops {
	int (*abort)(struct nvhost_queue *queue);
	int (*submit)(struct nvhost_queue *queue, void *task_arg);
};

/**
 * struct nvhost_queue_pool - Queue pool data structure to hold queue table
 *
 * @pdev:		Pointer to the Queue client device
 * @ops			Pointer to hardware specific queue ops
 * @queues:		Queues available for the client
 * @queue_lock:		Mutex for the bitmap of reserved queues
 * @alloc_table:	Bitmap of allocated queues
 * @max_queue_cnt:	Max number queues available for client
 *
 */
struct nvhost_queue_pool {
	struct platform_device *pdev;
	struct nvhost_queue_ops *ops;
	struct nvhost_queue *queues;
	struct mutex queue_lock;
	unsigned long alloc_table;
	unsigned int max_queue_cnt;
};

/**
 * nvhost_queue_init() - Initialize queue structures
 *
 * @pdev:		Pointer to the Queue client device
 * @ops			Pointer to device speicific callbacks
 * @num_queues:		Max number queues available for client
 *
 * Return:	pointer to queue pool
 *
 * This function allocates and initializes queue data structures.
 */
struct nvhost_queue_pool *nvhost_queue_init(struct platform_device *pdev,
					struct nvhost_queue_ops *ops,
					unsigned int num_queues);

/**
 * nvhost_queue_deinit() - De-initialize queue structures
 *
 * @pool:	pointer to queue pool
 *
 * Return:
 *
 * This function free's all queue data structures.
 */
void nvhost_queue_deinit(struct nvhost_queue_pool *pool);

/**
 * nvhost_queue_put() - Release reference of a queue
 *
 * @queue:	Pointer to an allocated queue.
 *
 * Return:	None
 *
 * This function releases reference for a queue.
 */
void nvhost_queue_put(struct nvhost_queue *queue);

/**
 * nvhost_queue_get() - Get reference on a queue.
 *
 * @queue:	Pointer to an allocated queue.
 *
 * Return:	None
 *
 * This function used to get a reference to an already allocated queue.
 */
void nvhost_queue_get(struct nvhost_queue *queue);

/**
 * nvhost_queue_alloc() - Allocate a queue for client.
 *
 * @pool:	Pointer to a queue pool table
 *
 * Return:	Pointer to a queue struct on Success
 *		or negative error on failure.
 *
 * This function allocates a queue from the pool to client for the user.
 */
struct nvhost_queue *nvhost_queue_alloc(struct nvhost_queue_pool *pool);

/**
 * nvhost_queue_abort() - Abort tasks within a client queue
 *
 * @queue:	Pointer to an allocated queue
 *
 * Return:	None
 *
 * This function aborts all tasks from the given clinet queue. If there is no
 * active tasks, the function call is no-op.
 *
 * It is expected to be called when an active device fd gets closed.
 */
int nvhost_queue_abort(struct nvhost_queue *queue);

/**
 * nvhost_queue_submit() - submits the given list of tasks to hardware
 *
 * @queue:	Pointer to an allocated queue
 * @submit:	Submit the given list of tasks to hardware
 *
 * Return:	0 on success, otherwise a negative error code is returned.
 *
 * This function submits the given list of tasks to hardware.
 * The submit structure is updated with the fence values as appropriate.
 *
 */
int nvhost_queue_submit(struct nvhost_queue *queue, void *submit);

#endif
