/*
 * Virtio entropy device implementation
 *
 * Copyright (C) 2026 Francesco Valla
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _DEV_VIRTIO_ENTROPY_H_
#define _DEV_VIRTIO_ENTROPY_H_

#include <metal/io.h>
#include <metal/mutex.h>
#include <metal/cache.h>
#include <openamp/virtio.h>

#define VIRTIO_ENTROPY_VRINGS	1

#if defined __cplusplus
extern "C" {
#endif

/* Callback handler for entropy request */
typedef void (*dev_virtio_entropy_cb)(uint8_t *data, uint32_t *len);

/** @brief Representation of a entropy device based on virtio */
struct dev_virtio_entropy_device {
	/** Pointer to the virtio device */
	struct virtio_device *vdev;

	/** Pointer to virtqueue */
	struct virtqueue *vq;

	/** Callback handler for entropy request */
	dev_virtio_entropy_cb entropy_cb;

	/** Mutex for callbacks */
	metal_mutex_t lock;
};

/**
 * @brief Initialize virtio entropy device
 *
 * @param vedev		Pointer to the entropy virtio device
 * @param vdev		Pointer to the virtio device
 * @param entropy_cb	Callback handler for entropy request.
 * @param shm_io	Pointer to the share memory I/O region.
 *
 * @return Status of function execution
 */
int dev_virtio_entropy_init_vdev(struct dev_virtio_entropy_device *vedev,
				 struct virtio_device *vdev,
				 dev_virtio_entropy_cb entropy_cb,
				 struct metal_io_region *shm_io);

/**
 * @brief Deinitialize entropy virtio device
 *
 * @param vedev	Pointer to the entropy virtio device
 */
void dev_virtio_entropy_deinit_vdev(struct dev_virtio_entropy_device *vedev);

#if defined __cplusplus
}
#endif

#endif	/* _DEV_VIRTIO_ENTROPY_H_ */
