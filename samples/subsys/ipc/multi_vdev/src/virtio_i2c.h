/*
 * Copyright (c) 2026 Francesco Valla
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <metal/compiler.h>

#ifndef _VIRTIO_I2C_H
#define _VIRTIO_I2C_H

/* Support zero-length requests. Mandatory feature. */
#define VIRTIO_I2C_F_ZERO_LENGTH_REQUEST	0

/* Do not attempt next request if the current one fails. Shall not be set on
 * the last request of a group. Flag used in virtio_i2c_out_hdr.flags. */
#define VIRTIO_I2C_FLAGS_FAIL_NEXT	BIT(0)

/* Mark a "read" request. Flag used in virtio_i2c_out_hdr.flags. */
#define VIRTIO_I2C_FLAGS_M_RD		BIT(1)

METAL_PACKED_BEGIN
struct virtio_i2c_out_hdr {
	uint16_t addr;
	uint16_t padding;
	uint32_t flags;
} METAL_PACKED_END;

METAL_PACKED_BEGIN
struct virtio_i2c_in_hdr {
	uint8_t status;
} METAL_PACKED_END;

/* Valid values for the virtio_i2c_in_hdr.status member */
#define VIRTIO_I2C_MSG_OK	0
#define VIRTIO_I2C_MSG_ERR	1

#endif /* _VIRTIO_I2C_H */
