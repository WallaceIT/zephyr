/*
 * Virtio entropy device implementation
 *
 * Copyright (C) 2026 Francesco Valla
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <metal/alloc.h>
#include <metal/mutex.h>
#include <metal/sleep.h>
#include <metal/sys.h>
#include <metal/utilities.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include <openamp/virtqueue.h>
#include <openamp/remoteproc_virtio.h>

#include "dev_virtio_common.h"
#include "dev_virtio_entropy.h"

LOG_MODULE_REGISTER(virtio_entropy, CONFIG_LOG_DEFAULT_LEVEL);

/**
 * @internal
 *
 * @brief Request callback function.
 *
 * @param vq	Pointer to virtqueue on which request is received
 */
static void dev_virtio_entropy_req_callback(struct virtqueue *vq)
{
	struct virtio_device *vdev = vq->vq_dev;
	struct dev_virtio_entropy_device *vedev = vdev->priv;
	uint32_t len;
	uint16_t idx;
	void *buf = NULL;
	int ret;

	while (1) {
		/* Process the received data from remote node */
		metal_mutex_acquire(&vedev->lock);

		buf = virtqueue_get_first_avail_buffer(vq, &idx, &len);

		/* No more filled rx buffers */
		if (!buf) {
			metal_mutex_release(&vedev->lock);
			break;
		}

		/* Invalidate cache before using buffer */
		BUFFER_INVALIDATE(buf, len);

		metal_mutex_release(&vedev->lock);

		if (vedev->entropy_cb) {
			vedev->entropy_cb(buf, &len);
		}

		metal_mutex_acquire(&vedev->lock);

		BUFFER_FLUSH(buf, len);
		
		ret = virtqueue_add_consumed_buffer(vq, idx, len);
		if (ret != 0)
			LOG_ERR("Add consumed buffer failed: %d", ret);

		virtqueue_kick(vq);

		metal_mutex_release(&vedev->lock);
	}
}

int dev_virtio_entropy_init_vdev(struct dev_virtio_entropy_device *vedev,
				 struct virtio_device *vdev,
				 dev_virtio_entropy_cb entropy_cb,
				 struct metal_io_region *shm_io)
{
	const char *vq_names[VIRTIO_ENTROPY_VRINGS];
	vq_callback callbacks[VIRTIO_ENTROPY_VRINGS];
	int status;

	if (!vedev || !vdev || !shm_io)
		return -EINVAL;

	if (VIRTIO_ROLE_IS_DRIVER(vdev)) {
		return -ENOTSUP;
	}

	memset(vedev, 0, sizeof(*vedev));

	vedev->vdev = vdev;
	vdev->priv = vedev;
	vedev->entropy_cb = entropy_cb;

	metal_mutex_init(&vedev->lock);

	vq_names[0] = "requestq";
	callbacks[0] = dev_virtio_entropy_req_callback;

	/* Create virtqueues for remote device */
	status = virtio_create_virtqueues(vdev, 0, VIRTIO_ENTROPY_VRINGS,
					  vq_names, callbacks, NULL);
	if (status != 0)
		return status;

	vedev->vq = vdev->vrings_info[0].vq;
	vedev->vq->shm_io = shm_io;

	return 0;
}

void dev_virtio_entropy_deinit_vdev(struct dev_virtio_entropy_device *vedev)
{
	if (vedev) {
		vedev->vq = NULL;

		virtio_delete_virtqueues(vedev->vdev);
		vedev->vdev = NULL;

		metal_mutex_deinit(&vedev->lock);
	}
}
