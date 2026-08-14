/*
 * Copyright (c) 2026 Francesco Valla
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <metal/compiler.h>

#ifndef _VIRTIO_GPIO_H
#define _VIRTIO_GPIO_H

/* Feature bits */
#define VIRTIO_GPIO_F_IRQ			0

/* Request types */
#define VIRTIO_GPIO_MSG_GET_NAMES		0x0001
#define VIRTIO_GPIO_MSG_GET_DIRECTION		0x0002
#define VIRTIO_GPIO_MSG_SET_DIRECTION		0x0003
#define VIRTIO_GPIO_MSG_GET_VALUE		0x0004
#define VIRTIO_GPIO_MSG_SET_VALUE		0x0005
#define VIRTIO_GPIO_MSG_IRQ_TYPE		0x0006

/* Possible values of the status field */
#define VIRTIO_GPIO_STATUS_OK			0x0
#define VIRTIO_GPIO_STATUS_ERR			0x1

/* Direction types */
#define VIRTIO_GPIO_DIRECTION_NONE		0x00
#define VIRTIO_GPIO_DIRECTION_OUT		0x01
#define VIRTIO_GPIO_DIRECTION_IN		0x02

/* Virtio GPIO IRQ types */
#define VIRTIO_GPIO_IRQ_TYPE_NONE		0x00
#define VIRTIO_GPIO_IRQ_TYPE_EDGE_RISING	0x01
#define VIRTIO_GPIO_IRQ_TYPE_EDGE_FALLING	0x02
#define VIRTIO_GPIO_IRQ_TYPE_EDGE_BOTH		0x03
#define VIRTIO_GPIO_IRQ_TYPE_LEVEL_HIGH		0x04
#define VIRTIO_GPIO_IRQ_TYPE_LEVEL_LOW		0x08

METAL_PACKED_BEGIN
struct virtio_gpio_config {
	uint16_t ngpio;
	uint8_t padding[2];
	uint32_t gpio_names_size;
} METAL_PACKED_END;

METAL_PACKED_BEGIN
struct virtio_gpio_request {
	uint16_t type;
	uint16_t gpio;
	uint32_t value;
} METAL_PACKED_END;

METAL_PACKED_BEGIN
struct virtio_gpio_response {
	uint8_t status;
	uint8_t value;
} METAL_PACKED_END;

METAL_PACKED_BEGIN
struct virtio_gpio_response_get_names {
	uint8_t status;
	uint8_t value[];
} METAL_PACKED_END;

METAL_PACKED_BEGIN
struct virtio_gpio_irq_request {
	uint16_t gpio;
} METAL_PACKED_END;

METAL_PACKED_BEGIN
struct virtio_gpio_irq_response {
	uint8_t status;
} METAL_PACKED_END;

/* Possible values of the interrupt status field */
#define VIRTIO_GPIO_IRQ_STATUS_INVALID		0x0
#define VIRTIO_GPIO_IRQ_STATUS_VALID		0x1

#endif /* _VIRTIO_GPIO_H */
