/*
 * Copyright (c) 2026 Francesco Valla
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <metal/compiler.h>

#ifndef _VIRTIO_CAN_H
#define _VIRTIO_CAN_H

/* The device supports classic CAN frames with a maximum payload size of 8 bytes. */
#define VIRTIO_CAN_F_CAN_CLASSIC (0)

/* The device supports CAN FD frames with a maximum payload size of 64 bytes. */
#define VIRTIO_CAN_F_CAN_FD (1)

/* The device supports RTR (remote transmission request) frames. */
#define VIRTIO_CAN_F_RTR_FRAMES (2)

/* The virtio CAN device marks tx requests as used after the CAN message has been transmitted on the bus. */
#define VIRTIO_CAN_F_LATE_TX_ACK (3)

/* The device signals a bus-off status */
#define VIRTIO_CAN_S_CTRL_BUSOFF (1 << 0)

METAL_PACKED_BEGIN
struct virtio_can_config {
	uint16_t status;
} METAL_PACKED_END;

/* Message types */
#define VIRTIO_CAN_TX			0x0001
#define VIRTIO_CAN_RX			0x0101
#define VIRTIO_CAN_SET_CTRL_MODE_START	0x0201
#define VIRTIO_CAN_SET_CTRL_MODE_STOP	0x0202

METAL_PACKED_BEGIN
struct virtio_can_control_out {
	uint16_t msg_type;
} METAL_PACKED_END;

METAL_PACKED_BEGIN
struct virtio_can_control_in {
	uint8_t result;
} METAL_PACKED_END;

/* Flags */
#define VIRTIO_CAN_FLAGS_FD		0x4000
#define VIRTIO_CAN_FLAGS_EXTENDED	0x8000
#define VIRTIO_CAN_FLAGS_RTR		0x2000

METAL_PACKED_BEGIN
struct virtio_can_tx_out {
	uint16_t msg_type;
	uint16_t length; /* 0..8 CC, 0..64 CAN-FD, 0..2048 CAN-XL, 12 bits */
	uint8_t reserved_classic_dlc; /* If CAN classic length = 8 then DLC can be 8..15 */
	uint8_t padding;
	uint16_t reserved_xl_priority; /* May be needed for CAN XL priority */

	uint32_t flags;
	uint32_t can_id;
	uint8_t sdu[];
} METAL_PACKED_END;

METAL_PACKED_BEGIN
struct virtio_can_tx_in {
	uint8_t result;
} METAL_PACKED_END;

METAL_PACKED_BEGIN
struct virtio_can_rx {
	uint16_t msg_type;
	uint16_t length; /* 0..8 CC, 0..64 CAN-FD, 0..2048 CAN-XL, 12 bits */
	uint8_t reserved_classic_dlc;
	uint8_t padding;
	uint16_t reserved_xl_priority;
	uint32_t flags;
	uint32_t can_id;
	uint8_t sdu[];
} METAL_PACKED_END;

#define VIRTIO_CAN_RESULT_OK		0
#define VIRTIO_CAN_RESULT_NOT_OK	1


#endif /* _VIRTIO_CAN_H */


