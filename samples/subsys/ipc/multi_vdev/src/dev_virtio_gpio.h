/*
 * Virtio GPIO device implementation
 *
 * Copyright (C) 2026 Francesco Valla
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _DEV_VIRTIO_GPIO_H_
#define _DEV_VIRTIO_GPIO_H_

#include <metal/io.h>
#include <metal/mutex.h>
#include <metal/cache.h>

#include <openamp/virtio.h>

#define VIRTIO_GPIO_VRINGS	2

#if defined __cplusplus
extern "C" {
#endif

typedef enum {
	DEV_VIRTIO_GPIO_DIRECTION_NONE,
	DEV_VIRTIO_GPIO_DIRECTION_OUT,
	DEV_VIRTIO_GPIO_DIRECTION_IN
} dev_virtio_gpio_direction_t;

typedef enum {
	DEV_VIRTIO_GPIO_IRQ_TYPE_NONE,
	DEV_VIRTIO_GPIO_IRQ_TYPE_EDGE_RISING,
	DEV_VIRTIO_GPIO_IRQ_TYPE_EDGE_FALLING,
	DEV_VIRTIO_GPIO_IRQ_TYPE_EDGE_BOTH,
	DEV_VIRTIO_GPIO_IRQ_TYPE_LEVEL_HIGH,
	DEV_VIRTIO_GPIO_IRQ_TYPE_LEVEL_LOW,
} dev_virtio_irq_type_t;

/** @brief Callback handlers for gpio requests */
typedef struct {
	/** Get names of the GPIO lines */
	int (*get_names)(uint8_t *buffer, size_t len);

	/** Get direction of specified GPIO line */
	int (*get_direction)(unsigned int gpio, dev_virtio_gpio_direction_t *dir);

	/** Set direction of specified GPIO line */
	int (*set_direction)(unsigned int gpio, dev_virtio_gpio_direction_t dir);

	/** Get value of specified GPIO line */
	int (*get_value)(unsigned int gpio, unsigned int *value);

	/** Set value of specified GPIO line */
	int (*set_value)(unsigned int gpio, unsigned int value);

	/** Get type of IRQ tied to specified GPIO line */
	int (*set_irq_type)(unsigned int gpio, dev_virtio_irq_type_t type);

	/** Unmask IRQ tied to specified GPIO line */
	int (*unmask_irq)(unsigned int gpio, void *ctx, unsigned int ctx2);
} dev_virtio_gpio_cb_set;

/** @brief Representation of a gpio device based on virtio */
struct dev_virtio_gpio_device {
	/** Pointer to the virtio device */
	struct virtio_device *vdev;

	/** Pointer to request virtqueue */
	struct virtqueue *vq_request;

	/** Pointer to event virtqueue */
	struct virtqueue *vq_event;

	/** Callback handlers for gpio request */
	const dev_virtio_gpio_cb_set *callbacks;

	/** Mutex for callbacks */
	metal_mutex_t lock;
};

/**
 * @brief Send GPIO interrupt
 *
 * @param vgdev		Pointer to the GPIO virtio device
 * @param gpio		GPIO number
 * @param ctx		IRQ context as defined by unmask_irq()
 * @param ctx2		IRQ context as defined by unmask_irq()
 */
void dev_virtio_send_irq(struct dev_virtio_gpio_device *vgdev,
			 unsigned int gpio, void *ctx, unsigned int ctx2);

/**
 * @brief Initialize virtio GPIO device
 *
 * @param vgdev		Pointer to the GPIO virtio device
 * @param vdev		Pointer to the virtio device
 * @param shm_io	Pointer to the share memory I/O region.
 *
 * @return Status of function execution
 */
int dev_virtio_gpio_init_vdev(struct dev_virtio_gpio_device *vgdev,
			      struct virtio_device *vdev,
			      const dev_virtio_gpio_cb_set *callbacks,
			      struct metal_io_region *shm_io);

/**
 * @brief Deinitialize GPIO virtio device
 *
 * @param vgdev	Pointer to the gpio virtio device
 */
void dev_virtio_gpio_deinit_vdev(struct dev_virtio_gpio_device *vgdev);

#if defined __cplusplus
}
#endif

#endif	/* _DEV_VIRTIO_GPIO_H_ */
