/*
 * Virtio SPI device implementation
 *
 * Copyright (C) 2026 Francesco Valla
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <metal/alloc.h>
#include <metal/mutex.h>
#include <metal/sleep.h>
#include <metal/sys.h>
#include <metal/utilities.h>

#include <openamp/virtqueue.h>
#include <openamp/remoteproc_virtio.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include "dev_virtio_common.h"
#include "dev_virtio_spi.h"

#include "virtio_spi.h"
#include <string.h>

LOG_MODULE_REGISTER(virtio_spi, CONFIG_LOG_DEFAULT_LEVEL);

enum {
	SPI_REQ_HEAD,
	SPI_REQ_TX,
	SPI_REQ_RX,
	SPI_REQ_RESULT,
	SPI_NUM_REQS
};

/**
 * @internal
 *
 * @brief Request callback function.
 *
 * @param vq	Pointer to virtqueue on which request is received
 */
static void dev_virtio_spi_req_callback(struct virtqueue *vq)
{
	struct virtio_device *vdev = vq->vq_dev;
	struct dev_virtio_spi_device *vsdev = vdev->priv;
	struct dev_virtio_spi_xcv_config xcv_config;
	void *buf[SPI_NUM_REQS] = { NULL, NULL, NULL, NULL };
	uint16_t idx[SPI_NUM_REQS];
	uint32_t len[SPI_NUM_REQS];
	int ret;

	while (1) {
		size_t written_len = 0;

		/* Process the received data from remote node */
		metal_mutex_acquire(&vsdev->lock);

		buf[SPI_REQ_HEAD] = virtqueue_get_first_avail_buffer(vq, &idx[SPI_REQ_HEAD],
								     &len[SPI_REQ_HEAD]);

		/* No more filled rx buffers */
		if (!buf[SPI_REQ_HEAD]) {
			metal_mutex_release(&vsdev->lock);
			break;
		}

		/* A request is composed by 3 to 4 buffers, let's ask for the others */
		buf[SPI_REQ_TX] = virtqueue_get_next_avail_buffer(vq, idx[SPI_REQ_HEAD],
								  &idx[SPI_REQ_TX],
								  &len[SPI_REQ_TX]);
		if (!buf[SPI_REQ_TX]) {
			virtqueue_add_consumed_buffer(vq, idx[SPI_REQ_HEAD], 0);
			virtqueue_kick(vq);
			metal_mutex_release(&vsdev->lock);
			metal_log(METAL_LOG_ERROR, "spi: not enough buffers");
			break;
		}

		buf[SPI_REQ_RX] = virtqueue_get_next_avail_buffer(vq, idx[SPI_REQ_TX],
								  &idx[SPI_REQ_RX],
								  &len[SPI_REQ_RX]);
		if (!buf[SPI_REQ_RX]) {
			virtqueue_add_consumed_buffer(vq, idx[SPI_REQ_HEAD], 0);
			virtqueue_kick(vq);
			metal_mutex_release(&vsdev->lock);
			metal_log(METAL_LOG_ERROR, "spi: not enough buffers");
			break;
		}

		/* The fourth buffer is there only for full-duplex requests */
		buf[SPI_REQ_RESULT] = virtqueue_get_next_avail_buffer(vq, idx[SPI_REQ_RX],
								  &idx[SPI_REQ_RESULT],
								  &len[SPI_REQ_RESULT]);
		if (!buf[SPI_REQ_RESULT]) {
			/* Treat the previous buffer as the RESULT one */
			buf[SPI_REQ_RESULT] = buf[SPI_REQ_RX];
			buf[SPI_REQ_RX] = NULL;

			idx[SPI_REQ_RESULT] = idx[SPI_REQ_RX];

			len[SPI_REQ_RESULT] = len[SPI_REQ_RX];
			len[SPI_REQ_RX] = 0;

			/* Then, decide wether the only data buffer is a TX or RX one */
			if (virtqueue_is_buffer_device_writable(vq, idx[SPI_REQ_TX])) {
				buf[SPI_REQ_RX] = buf[SPI_REQ_TX];
				buf[SPI_REQ_TX] = NULL;

				idx[SPI_REQ_RX] = idx[SPI_REQ_TX];

				len[SPI_REQ_RX] = len[SPI_REQ_TX];
				len[SPI_REQ_TX] = 0;
			}
		}

		metal_mutex_release(&vsdev->lock);

		if (len[SPI_REQ_HEAD] < sizeof(struct virtio_spi_transfer_head)) {
			LOG_ERR("invalid header length %u", len[SPI_REQ_HEAD]);
		} else if (len[SPI_REQ_RESULT] < sizeof(struct virtio_spi_transfer_result)) {
			LOG_ERR("invalid result length %u", len[SPI_REQ_RESULT]);
		} else if (len[SPI_REQ_TX] > 0 && len[SPI_REQ_RX] > 0 &&
			   len[SPI_REQ_TX] != len[SPI_REQ_RX]) {
			LOG_ERR("TX and RX lengths differ (TX=%u RX=%u)",
				len[SPI_REQ_TX], len[SPI_REQ_RX]);
		} else {
			const struct virtio_spi_transfer_head *hdr;
			struct virtio_spi_transfer_result *res;
			size_t xfer_len = MAX(len[SPI_REQ_TX], len[SPI_REQ_RX]);

			/* Invalidate cache before using buffer */
			BUFFER_INVALIDATE(buf[SPI_REQ_HEAD], len[SPI_REQ_HEAD]);

			hdr = (struct virtio_spi_transfer_head *)buf[SPI_REQ_HEAD];
			res = (struct virtio_spi_transfer_result *)buf[SPI_REQ_RESULT];

			xcv_config.chip_select_id = hdr->chip_select_id;
			xcv_config.bits_per_word = hdr->bits_per_word;
			xcv_config.cs_change = hdr->cs_change;
			xcv_config.tx_nbits = MAX(hdr->tx_nbits, 1);
			xcv_config.rx_nbits = MAX(hdr->rx_nbits, 1);
			xcv_config.frequency = hdr->freq;
			xcv_config.word_delay_ns = hdr->word_delay_ns;

			xcv_config.cpol = (hdr->mode & VIRTIO_SPI_MODE_CPOL) ? 1 : 0;
			xcv_config.cpha = (hdr->mode & VIRTIO_SPI_MODE_CPHA) ? 1 : 0;
			xcv_config.cs_active_high = (hdr->mode & VIRTIO_SPI_MODE_CS_HIGH) ? 1 : 0;
			xcv_config.lsb_first = (hdr->mode & VIRTIO_SPI_MODE_LSB_FIRST) ? 1 : 0;
			xcv_config.loopback = (hdr->mode & VIRTIO_SPI_MODE_LOOPBACK) ? 1 : 0;

			ret = vsdev->callbacks->transceive(&xcv_config,
							   buf[SPI_REQ_TX],
							   buf[SPI_REQ_RX],
							   xfer_len);
			if (ret != 0) {
				LOG_ERR("Transceive with len %u failed: %d", xfer_len, ret);
				res->result = VIRTIO_SPI_TRANS_ERR;
			}

			BUFFER_FLUSH(buf[SPI_REQ_RESULT], len[SPI_REQ_RESULT]);
			written_len = len[SPI_REQ_RESULT];

			if (buf[SPI_REQ_RX] != NULL && ret != 0) {
				BUFFER_FLUSH(buf[SPI_REQ_RX], len[SPI_REQ_RX]);
				written_len += len[SPI_REQ_RX];
			}
		}

		metal_mutex_acquire(&vsdev->lock);

		ret = virtqueue_add_consumed_buffer(vq, idx[SPI_REQ_HEAD],
						    written_len);
		if (ret != 0)
			LOG_ERR("Add consumed buffer failed: %d", ret);

		virtqueue_kick(vq);

		metal_mutex_release(&vsdev->lock);
	}
}

int dev_virtio_spi_init_vdev(struct dev_virtio_spi_device *vsdev,
			     struct virtio_device *vdev,
			     const dev_virtio_spi_cb_set *cb_set,
			     struct metal_io_region *shm_io)
{
	const char *vq_names[VIRTIO_SPI_VRINGS];
	vq_callback callbacks[VIRTIO_SPI_VRINGS];
	int status;

	if (!vsdev || !vdev || !cb_set || !shm_io)
		return -EINVAL;

	if (!cb_set->transceive)
		return -EINVAL;

	if (VIRTIO_ROLE_IS_DRIVER(vdev)) {
		return -ENOTSUP;
	}

	memset(vsdev, 0, sizeof(*vsdev));

	vsdev->vdev = vdev;
	vsdev->callbacks = cb_set;

	vdev->priv = vsdev;

	metal_mutex_init(&vsdev->lock);

	vq_names[0] = "requestq";
	callbacks[0] = dev_virtio_spi_req_callback;

	/* Create virtqueues for remote device */
	status = virtio_create_virtqueues(vdev, 0, VIRTIO_SPI_VRINGS,
					  vq_names, callbacks, NULL);
	if (status != 0)
		return status;

	vsdev->vq = vdev->vrings_info[0].vq;
	vsdev->vq->shm_io = shm_io;

	return 0;
}

void dev_virtio_spi_deinit_vdev(struct dev_virtio_spi_device *vsdev)
{
	if (vsdev) {
		vsdev->vq = NULL;

		virtio_delete_virtqueues(vsdev->vdev);
		vsdev->vdev = NULL;

		metal_mutex_deinit(&vsdev->lock);
	}
}
