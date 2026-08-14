/*
 * Copyright (c) 2026 Francesco Valla
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <metal/compiler.h>

#ifndef _VIRTIO_SPI_H
#define _VIRTIO_SPI_H

METAL_PACKED_BEGIN
struct virtio_spi_config {
	uint8_t cs_max_number;
	uint8_t cs_change_supported;
	uint8_t tx_nbits_supported;
	uint8_t rx_nbits_supported;
	uint32_t bits_per_word_mask;
	uint32_t mode_func_supported;
	uint32_t max_freq_hz;
	uint32_t max_word_delay_ns;
	uint32_t max_cs_setup_ns;
	uint32_t max_cs_hold_ns;
	uint32_t max_cs_inactive_ns;
} METAL_PACKED_END;

METAL_PACKED_BEGIN
struct virtio_spi_transfer_head {
	uint8_t chip_select_id;
	uint8_t bits_per_word;
	uint8_t cs_change;
	uint8_t tx_nbits;
	uint8_t rx_nbits;
	uint8_t reserved[3];
	uint32_t mode;
	uint32_t freq;
	uint32_t word_delay_ns;
	uint32_t cs_setup_ns;
	uint32_t cs_delay_hold_ns;
	uint32_t cs_change_delay_inactive_ns;
} METAL_PACKED_END;

METAL_PACKED_BEGIN
struct virtio_spi_transfer_result {
	uint8_t result;
};

/* Flags for the mode_func_supported.mode_func_supported field */
/* Support for the CPHA settings */
#define VIRTIO_SPI_MODE_FUNC_CPHA_0	BIT(0)
#define VIRTIO_SPI_MODE_FUNC_CPHA_1	BIT(1)
#define VIRTIO_SPI_MODE_FUNC_CPHA_0_1	BIT(0) | BIT(1)
/* Support for the CPHA settings */
#define VIRTIO_SPI_MODE_FUNC_CPOL_0	BIT(2)
#define VIRTIO_SPI_MODE_FUNC_CPOL_1	BIT(3)
#define VIRTIO_SPI_MODE_FUNC_CPOL_0_1	BIT(2) | BIT(3)
/* Support for chipselect HIGH when asserted */
#define VIRTIO_SPI_MODE_FUNC_CS_HIGH	BIT(4)
/* Support for LSB first on wire */
#define VIRTIO_SPI_MODE_FUNC_LSB_FIRST	BIT(6)
/* Support for loopback mode */
#define VIRTIO_SPI_MODE_FUNC_LOOPBACK	BIT(6)


/* Flags for the virtio_spi_transfer_head.mode field */
/* Phase of the data bits relative to the clock pulses */
#define VIRTIO_SPI_MODE_CPHA		BIT(0)
/* Polarity of the clock */
#define VIRTIO_SPI_MODE_CPOL		BIT(1)
/* Chipselect HIGH when asserted */
#define VIRTIO_SPI_MODE_CS_HIGH		BIT(2)
/* Per-word bits-on-wire, if 0, MSB first, else LSB first */
#define VIRTIO_SPI_MODE_LSB_FIRST	BIT(3)
/* Device is in loopback mode */
#define VIRTIO_SPI_MODE_LOOPBACK	BIT(4)

/* Valid values for the virtio_spi_transfer_result.status field */
#define VIRTIO_SPI_TRANS_OK	0
#define VIRTIO_SPI_PARAM_ERR	1
#define VIRTIO_SPI_TRANS_ERR	2

#endif /* _VIRTIO_SPI_H */
