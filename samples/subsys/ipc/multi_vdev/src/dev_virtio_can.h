/*
 * Virtio CAN device implementation
 *
 * Copyright (C) 2026 Francesco Valla
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _DEV_VIRTIO_CAN_H_
#define _DEV_VIRTIO_CAN_H_

#include <metal/io.h>
#include <metal/mutex.h>
#include <metal/cache.h>

#include <openamp/virtio.h>

#define VIRTIO_CAN_VRINGS	3

#if defined __cplusplus
extern "C" {
#endif

/** @brief Callback handlers for CAN requests */
typedef struct {
	/** Start or stop CAN communication */
	int (*set_started)(bool start);
	/** Send CAN message */
	int (*send)(uint32_t id, uint16_t length, const uint8_t *sdu,
		    bool extended, bool fd, bool rtr);
} dev_virtio_can_cb_set;

/** @brief Representation of a CAN device based on virtio */
struct dev_virtio_can_device {
	/** Pointer to the virtio device */
	struct virtio_device *vdev;

	/** Pointer to TX virtqueue */
	struct virtqueue *vq_tx;

	/** Pointer to RX virtqueue */
	struct virtqueue *vq_rx;

	/** Pointer to control virtqueue */
	struct virtqueue *vq_control;

	/** Callback handlers */
	const dev_virtio_can_cb_set *callbacks;

	/** Mutex for callbacks */
	metal_mutex_t lock;
};

/**
 * @brief Initialize virtio CAN device
 *
 * @param vcdev		Pointer to the CAN virtio device
 * @param vdev		Pointer to the virtio device
 * @param callbacks	Pointer to set of callbacks
 * @param shm_io	Pointer to the share memory I/O region.
 *
 * @return Status of function execution
 */
int dev_virtio_can_init_vdev(struct dev_virtio_can_device *vcdev,
			     struct virtio_device *vdev,
			     const dev_virtio_can_cb_set *callbacks,
			     struct metal_io_region *shm_io);

/**
 * @brief Deinitialize CAN virtio device
 *
 * @param vcdev	Pointer to the CAN virtio device
 */
void dev_virtio_can_deinit_vdev(struct dev_virtio_can_device *vcdev);

/**
 * @brief Receive CAN message (driver point-of-view)
 *
 * @param vcdev		Pointer to the CAN virtio device
 * @param id		CAN ID
 * @param length	Payload length
 * @param sdu		Payload buffer
 * @param extended	CAN ID is extended
 * @param fd		Wheter the CAN message is a CAN-FD one or not
 * @param rtr		Wheter the CAN message is a RTR one or not
 *
 * @return 0 on success, a negative number on error
 */
int dev_virtio_can_receive(struct dev_virtio_can_device *vcdev, uint32_t id,
			    uint16_t length, const uint8_t *sdu, bool extended,
			    bool fd, bool rtr);

#if defined __cplusplus
}
#endif

#endif	/* _DEV_VIRTIO_CAN_H_ */
