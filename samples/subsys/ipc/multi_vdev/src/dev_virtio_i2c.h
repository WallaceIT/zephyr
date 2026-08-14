/*
 * Virtio I2C device implementation
 *
 * Copyright (C) 2026 Francesco Valla
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _DEV_VIRTIO_I2C_H_
#define _DEV_VIRTIO_I2C_H_

#include <metal/io.h>
#include <metal/mutex.h>
#include <metal/cache.h>

#include <openamp/virtio.h>

#define VIRTIO_I2C_VRINGS	1

#if defined __cplusplus
extern "C" {
#endif

/** @brief Callback handlers for i2c requests */
typedef struct {
	/** I2C read transaction */
	int (*read)(uint16_t addr, uint8_t *buffer, size_t len);

	/** I2C write transaction */
	int (*write)(uint16_t addr, const uint8_t *buffer, size_t len);
} dev_virtio_i2c_cb_set;

/** @brief Representation of a i2c device based on virtio */
struct dev_virtio_i2c_device {
	/** Pointer to the virtio device */
	struct virtio_device *vdev;

	/** Pointer to request virtqueue */
	struct virtqueue *vq_request;

	/** Callback handlers for i2c request */
	const dev_virtio_i2c_cb_set *callbacks;

	/** Mutex for callbacks */
	metal_mutex_t lock;
};

/**
 * @brief Initialize virtio I2C device
 *
 * @param videv		Pointer to the I2C virtio device
 * @param vdev		Pointer to the virtio device
 * @param callbacks	Pointer to set of callbacks
 * @param shm_io	Pointer to the share memory I/O region.
 *
 * @return Status of function execution
 */
int dev_virtio_i2c_init_vdev(struct dev_virtio_i2c_device *videv,
			     struct virtio_device *vdev,
			     const dev_virtio_i2c_cb_set *callbacks,
			     struct metal_io_region *shm_io);

/**
 * @brief Deinitialize I2C virtio device
 *
 * @param videv	Pointer to the i2c virtio device
 */
void dev_virtio_i2c_deinit_vdev(struct dev_virtio_i2c_device *videv);

#if defined __cplusplus
}
#endif

#endif	/* _DEV_VIRTIO_I2C_H_ */
