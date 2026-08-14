/*
 * Virtio SPI device implementation
 *
 * Copyright (C) 2026 Francesco Valla
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _DEV_VIRTIO_SPI_H_
#define _DEV_VIRTIO_SPI_H_

#include <metal/io.h>
#include <metal/mutex.h>
#include <metal/cache.h>

#include <openamp/virtio.h>

#define VIRTIO_SPI_VRINGS	1

#if defined __cplusplus
extern "C" {
#endif

struct dev_virtio_spi_xcv_config {
	uint32_t frequency;

	uint8_t chip_select_id;
	uint8_t bits_per_word;

	uint8_t cpol : 1;
	uint8_t cpha : 1;
	uint8_t loopback : 1;
	uint8_t lsb_first : 1;
	uint8_t cs_active_high : 1;
	uint8_t cs_change : 1;
	uint8_t padding : 2;

	uint8_t tx_nbits : 4;
	uint8_t rx_nbits : 4;

	uint32_t word_delay_ns;
};

/** @brief Callback handlers for spi requests */
typedef struct {
	/** SPI transaction */
	int (*transceive)(const struct dev_virtio_spi_xcv_config *config,
			  const uint8_t *tx_buffer, uint8_t *rx_buffer,
			  size_t len);
} dev_virtio_spi_cb_set;

/** @brief Representation of a spi device based on virtio */
struct dev_virtio_spi_device {
	/** Pointer to the virtio device */
	struct virtio_device *vdev;

	/** Pointer to request virtqueue */
	struct virtqueue *vq;

	/** Callback handlers for spi request */
	const dev_virtio_spi_cb_set *callbacks;

	/** Mutex for callbacks */
	metal_mutex_t lock;
};

/**
 * @brief Initialize virtio SPI device
 *
 * @param videv		Pointer to the SPI virtio device
 * @param vdev		Pointer to the virtio device
 * @param callbacks	Pointer to set of callbacks
 * @param shm_io	Pointer to the share memory I/O region.
 *
 * @return Status of function execution
 */
int dev_virtio_spi_init_vdev(struct dev_virtio_spi_device *videv,
			     struct virtio_device *vdev,
			     const dev_virtio_spi_cb_set *callbacks,
			     struct metal_io_region *shm_io);

/**
 * @brief Deinitialize SPI virtio device
 *
 * @param videv	Pointer to the spi virtio device
 */
void dev_virtio_spi_deinit_vdev(struct dev_virtio_spi_device *videv);

#if defined __cplusplus
}
#endif

#endif	/* _DEV_VIRTIO_SPI_H_ */
