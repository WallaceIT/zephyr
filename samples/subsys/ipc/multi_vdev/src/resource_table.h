/*
 * Copyright (c) 2026 Francesco Valla
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _RESOURCE_TABLE_H
#define _RESOURCE_TABLE_H

enum {
	RSC_TABLE_VDEV_RPMSG = 0,
	RSC_TABLE_VDEV_ENTROPY,
	RSC_TABLE_VDEV_GPIO,
	RSC_TABLE_VDEV_I2C,
	RSC_TABLE_VDEV_SPI,
	RSC_TABLE_VDEV_CAN,

	RSC_TABLE_NUM_ENTRIES
};

void *resource_table_get(size_t *size);
struct fw_rsc_vdev * resource_table_get_vdev(unsigned int idx);
struct fw_rsc_vdev_vring * resource_table_get_vring(unsigned int vdev, unsigned int n);

#endif // _RESOURCE_TABLE_H