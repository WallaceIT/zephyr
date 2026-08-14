/*
 * Copyright (c) 2026 Francesco Valla
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <openamp/remoteproc.h>
#include <openamp/virtio.h>
#include <openamp/rpmsg_virtio.h>

#include "virtio_ids.h"

#include "virtio_can.h"
#include "virtio_gpio.h"
#include "virtio_i2c.h"
#include "virtio_spi.h"

#include "app_virtio_config.h"
#include "resource_table.h"

#define DECL_RSC_TABLE_VDEV(_name, _n_vrings) \
	struct { \
		struct fw_rsc_vdev vdev; \
		struct fw_rsc_vdev_vring vrings[_n_vrings]; \
	} vdev_##_name

#define DECL_RSC_TABLE_VDEV_CONFIG(_name, _n_vrings, _config_t) \
	struct { \
		struct fw_rsc_vdev vdev; \
		struct fw_rsc_vdev_vring vrings[_n_vrings]; \
		_config_t config; \
	} vdev_##_name

#define DEF_VDEV_ENTRY(_id, _notify_id, _features, _config_len, _n_vrings) \
	{ \
		.type = RSC_VDEV, \
		.id = _id, \
		.notifyid = _notify_id, \
		.dfeatures = _features, \
		.gfeatures = 0, \
		.config_len = _config_len, \
		.status = 0, \
		.num_of_vrings = _n_vrings \
	}

#define DEF_VRING_ENTRY(_da, _n_buffers, _notify_id) \
	{ \
		.da = _da, \
		.align = 16, \
		.num = _n_buffers, \
		.notifyid = _notify_id, \
		.reserved = 0 \
	}

const uint8_t v_gpio_names[] = {
	'G', 'P', 'I', 'O', '0', '0', 0,
	'G', 'P', 'I', 'O', '0', '1', 0,
	'G', 'P', 'I', 'O', '0', '2', 0,
	'G', 'P', 'I', 'O', '0', '3', 0,
	'G', 'P', 'I', 'O', '0', '4', 0,
	'G', 'P', 'I', 'O', '0', '5', 0,
	'G', 'P', 'I', 'O', '0', '6', 0,
	'G', 'P', 'I', 'O', '0', '7', 0,
	'G', 'P', 'I', 'O', '0', '8', 0,
	'G', 'P', 'I', 'O', '0', '9', 0,
	'G', 'P', 'I', 'O', '1', '0', 0,
	'G', 'P', 'I', 'O', '1', '1', 0,
	'G', 'P', 'I', 'O', '1', '2', 0,
	'G', 'P', 'I', 'O', '1', '3', 0,
	'G', 'P', 'I', 'O', '1', '4', 0,
	'G', 'P', 'I', 'O', '1', '5', 0,
	'G', 'P', 'I', 'O', '1', '6', 0,
	'G', 'P', 'I', 'O', '1', '7', 0,
	'G', 'P', 'I', 'O', '1', '8', 0,
	'G', 'P', 'I', 'O', '1', '9', 0,
	'G', 'P', 'I', 'O', '2', '0', 0,
	'G', 'P', 'I', 'O', '2', '1', 0,
	'G', 'P', 'I', 'O', '2', '2', 0,
	'G', 'P', 'I', 'O', '2', '3', 0,
	'G', 'P', 'I', 'O', '2', '4', 0,
	'G', 'P', 'I', 'O', '2', '5', 0,
	'G', 'P', 'I', 'O', '2', '6', 0,
	'G', 'P', 'I', 'O', '2', '7', 0,
};
const size_t v_gpio_names_len = sizeof(v_gpio_names);

METAL_PACKED_BEGIN
struct fw_resource_table {
	struct resource_table hdr;
	uint32_t offset[RSC_TABLE_NUM_ENTRIES];

	DECL_RSC_TABLE_VDEV(rpmsg, 2);
	DECL_RSC_TABLE_VDEV(entropy, 1);
	DECL_RSC_TABLE_VDEV_CONFIG(gpio, 2, struct virtio_gpio_config);
	DECL_RSC_TABLE_VDEV(i2c, 1);
	DECL_RSC_TABLE_VDEV_CONFIG(spi, 1, struct virtio_spi_config);
	DECL_RSC_TABLE_VDEV_CONFIG(can, 3, struct virtio_can_config);

} METAL_PACKED_END __aligned(8) static my_resource_table Z_GENERIC_SECTION(.resource_table) = {
	.hdr = {
		.ver = 1,
		.num = RSC_TABLE_NUM_ENTRIES,
	},

	.offset = {
		[RSC_TABLE_VDEV_RPMSG] = offsetof(struct fw_resource_table, vdev_rpmsg),
		[RSC_TABLE_VDEV_ENTROPY] = offsetof(struct fw_resource_table, vdev_entropy),
		[RSC_TABLE_VDEV_GPIO] = offsetof(struct fw_resource_table, vdev_gpio),
		[RSC_TABLE_VDEV_I2C] = offsetof(struct fw_resource_table, vdev_i2c),
		[RSC_TABLE_VDEV_SPI] = offsetof(struct fw_resource_table, vdev_spi),
		[RSC_TABLE_VDEV_CAN] = offsetof(struct fw_resource_table, vdev_can),
	},

	.vdev_rpmsg = {
		.vdev = DEF_VDEV_ENTRY(VIRTIO_DEVICE_ID_RPMSG, 0xE0,
				       (1 << VIRTIO_RPMSG_F_NS), 0, 2),
		.vrings[0] = DEF_VRING_ENTRY(FW_RSC_U32_ADDR_ANY, 8, 0),
		.vrings[1] = DEF_VRING_ENTRY(FW_RSC_U32_ADDR_ANY, 8, 1),
	},

	.vdev_entropy = {
		.vdev = DEF_VDEV_ENTRY(VIRTIO_DEVICE_ID_ENTROPY, 0xE1, 0, 0, 1),
		.vrings[0] = DEF_VRING_ENTRY(FW_RSC_U32_ADDR_ANY, 1, 0x10),
	},

	.vdev_gpio = {
		.vdev = DEF_VDEV_ENTRY(VIRTIO_DEVICE_ID_GPIO, 0xE2,
				       (1 << VIRTIO_GPIO_F_IRQ),
				       sizeof(struct virtio_gpio_config), 2),
		.vrings[0] = DEF_VRING_ENTRY(FW_RSC_U32_ADDR_ANY, 8, 0x20),
		.vrings[1] = DEF_VRING_ENTRY(FW_RSC_U32_ADDR_ANY, 8, 0x21),
		.config = {
			.ngpio = V_GPIO_NUM,
			.gpio_names_size = sizeof(v_gpio_names),
		},
	},

	.vdev_i2c = {
		.vdev = DEF_VDEV_ENTRY(VIRTIO_DEVICE_ID_I2C, 0xE3,
				       (1 << VIRTIO_I2C_F_ZERO_LENGTH_REQUEST),
				       0, 1),
		.vrings[0] = DEF_VRING_ENTRY(FW_RSC_U32_ADDR_ANY, 16, 0x30),
	},

	.vdev_spi = {
		.vdev = DEF_VDEV_ENTRY(VIRTIO_DEVICE_ID_SPI, 0xE4, 0,
				       sizeof(struct virtio_spi_config), 1),
		.vrings[0] = DEF_VRING_ENTRY(FW_RSC_U32_ADDR_ANY, 16, 0x40),
		.config = {
			.cs_max_number = 2,
			.cs_change_supported = 1,
			.tx_nbits_supported = 1,
			.rx_nbits_supported = 1,
			.bits_per_word_mask = 0x000000FE,
			.mode_func_supported = (VIRTIO_SPI_MODE_FUNC_CPHA_0_1 |
						VIRTIO_SPI_MODE_FUNC_CPOL_0_1 |
						VIRTIO_SPI_MODE_FUNC_CS_HIGH |
						VIRTIO_SPI_MODE_FUNC_LSB_FIRST),
			.max_freq_hz = 10 * 1000 * 1000,
			.max_word_delay_ns = 0,
			.max_cs_setup_ns = 0,
			.max_cs_hold_ns = 0,
			.max_cs_inactive_ns = 0,
		},
	},

	.vdev_can = {
		.vdev = DEF_VDEV_ENTRY(VIRTIO_DEVICE_ID_CAN, 0xE5,
				       (1 << VIRTIO_CAN_F_CAN_CLASSIC),
				       sizeof(struct virtio_can_config), 3),
		.vrings[0] = DEF_VRING_ENTRY(FW_RSC_U32_ADDR_ANY, 16, 0x50),
		.vrings[1] = DEF_VRING_ENTRY(FW_RSC_U32_ADDR_ANY, 16, 0x51),
		.vrings[2] = DEF_VRING_ENTRY(FW_RSC_U32_ADDR_ANY, 16, 0x52),
		.config = {
			.status = 0,
		},
	},
};

void *resource_table_get(size_t *size)
{
	if (size)
		*size = sizeof(my_resource_table);

	return &my_resource_table;
}

struct fw_rsc_vdev * resource_table_get_vdev(unsigned int idx)
{
	struct fw_resource_table *_rsc_table =
		(struct fw_resource_table *)resource_table_get(NULL);
	struct fw_rsc_hdr *_rsc_hdr;

	if (!_rsc_table || idx > _rsc_table->hdr.num)
		return NULL;

	_rsc_hdr = (struct fw_rsc_hdr *)((uint8_t *)_rsc_table + _rsc_table->offset[idx]);
	if (_rsc_hdr->type != RSC_VDEV)
		return NULL;

	return (struct fw_rsc_vdev *)_rsc_hdr;
}

struct fw_rsc_vdev_vring * resource_table_get_vring(unsigned int vdev, unsigned int n)
{
	struct fw_rsc_vdev *_vdev = resource_table_get_vdev(vdev);
	if (_vdev && n < _vdev->num_of_vrings) {
		return &_vdev->vring[n];
	}

	return NULL;
}
