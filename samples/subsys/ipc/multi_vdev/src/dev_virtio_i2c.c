/*
 * Virtio I2C device implementation
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
#include "dev_virtio_i2c.h"

#include "virtio_i2c.h"

LOG_MODULE_REGISTER(virtio_i2c, CONFIG_LOG_DEFAULT_LEVEL);

enum {
	I2C_REQ_OUT,
	I2C_REQ_BUF,
	I2C_REQ_IN,
	I2C_NUM_REQS
};

/**
 * @internal
 *
 * @brief Request callback function.
 *
 * @param vq	Pointer to virtqueue on which request is received
 */
static void dev_virtio_i2c_req_callback(struct virtqueue *vq)
{
	struct virtio_device *vdev = vq->vq_dev;
	struct dev_virtio_i2c_device *videv = vdev->priv;
	void *buf[I2C_NUM_REQS] = { NULL, NULL, NULL };
	uint16_t idx[I2C_NUM_REQS];
	uint32_t len[I2C_NUM_REQS];
	bool fail_next = false;
	bool last_failed = false;
	int ret;

	while (1) {
		/* Process the received data from remote node */
		metal_mutex_acquire(&videv->lock);

		buf[I2C_REQ_OUT] = virtqueue_get_first_avail_buffer(vq, &idx[I2C_REQ_OUT],
								    &len[I2C_REQ_OUT]);

		/* No more filled rx buffers */
		if (!buf[I2C_REQ_OUT]) {
			metal_mutex_release(&videv->lock);
			break;
		}

		/* Each request is composed by at least two parts */
		buf[I2C_REQ_BUF] = virtqueue_get_next_avail_buffer(vq, idx[I2C_REQ_OUT],
								   &idx[I2C_REQ_BUF],
								   &len[I2C_REQ_BUF]);
		if (!buf[I2C_REQ_BUF]) {
			virtqueue_add_consumed_buffer(vq, idx[I2C_REQ_OUT], 0);
			virtqueue_kick(vq);
			metal_mutex_release(&videv->lock);
			metal_log(METAL_LOG_ERROR, "i2c: not enough buffers");
			break;
		}

		/* A third part is there only for non-zero-length requests */
		buf[I2C_REQ_IN] = virtqueue_get_next_avail_buffer(vq, idx[I2C_REQ_BUF],
								  &idx[I2C_REQ_IN],
								  &len[I2C_REQ_IN]);
		if (!buf[I2C_REQ_IN]) {
			/* Treat the previous buffer as the IN one */
			buf[I2C_REQ_IN] = buf[I2C_REQ_BUF];
			buf[I2C_REQ_BUF] = NULL;

			len[I2C_REQ_IN] = len[I2C_REQ_BUF];
			len[I2C_REQ_BUF] = 0;
		}

		metal_mutex_release(&videv->lock);

		if (len[I2C_REQ_OUT] < sizeof(struct virtio_i2c_out_hdr)) {
			LOG_ERR("invalid out header length %u", len[I2C_REQ_OUT]);
		} else if (len[I2C_REQ_IN] < sizeof(struct virtio_i2c_in_hdr)) {
			LOG_ERR("invalid in header length %u", len[I2C_REQ_IN]);
		} else {
			const struct virtio_i2c_out_hdr *out;
			struct virtio_i2c_in_hdr *in;
			uint16_t addr;
			uint32_t flags;

			/* Invalidate cache before using buffer */
			BUFFER_INVALIDATE(buf[I2C_REQ_OUT], len[I2C_REQ_OUT]);

			out = (struct virtio_i2c_out_hdr *)buf[I2C_REQ_OUT];
			in = (struct virtio_i2c_in_hdr *)buf[I2C_REQ_IN];

			addr = sys_get_le16((const uint8_t *)&out->addr);
			flags = sys_get_le32((const uint8_t *)&out->flags);

			/* TODO: only 7-bit addressing is supported */
			addr >>= 1;

			/* Check if we are failing "next" requests */
			if (fail_next && last_failed) {
				LOG_INF("Forcibly failing next request");
				ret = -1;
			} else if (flags & VIRTIO_I2C_FLAGS_M_RD) {
				ret = videv->callbacks->read(addr, buf[I2C_REQ_BUF], len[I2C_REQ_BUF]);
				if (ret != 0)
					LOG_ERR("Read from %02Xh failed: %d", addr, ret);
			} else {
				ret = videv->callbacks->write(addr, buf[I2C_REQ_BUF], len[I2C_REQ_BUF]);
				if (ret != 0)
					LOG_ERR("Write to %02Xh failed: %d", addr, ret);
			}

			if (ret != 0) {
				last_failed = true;
				in->status = VIRTIO_I2C_MSG_ERR;
			} else {
				last_failed = false;
				in->status = VIRTIO_I2C_MSG_OK;
			}

			fail_next = !!(flags & VIRTIO_I2C_FLAGS_FAIL_NEXT);

			BUFFER_FLUSH(buf[I2C_REQ_IN], len[I2C_REQ_IN]);
			if (buf[I2C_REQ_BUF] != NULL)
				BUFFER_FLUSH(buf[I2C_REQ_BUF], len[I2C_REQ_BUF]);
		}

		metal_mutex_acquire(&videv->lock);

		ret = virtqueue_add_consumed_buffer(vq, idx[I2C_REQ_OUT],
						    len[I2C_REQ_IN] + len[I2C_REQ_BUF]);
		if (ret != 0)
			LOG_ERR("Add consumed buffer failed: %d", ret);

		virtqueue_kick(vq);

		metal_mutex_release(&videv->lock);
	}
}

int dev_virtio_i2c_init_vdev(struct dev_virtio_i2c_device *videv,
			     struct virtio_device *vdev,
			     const dev_virtio_i2c_cb_set *cb_set,
			     struct metal_io_region *shm_io)
{
	const char *vq_names[VIRTIO_I2C_VRINGS];
	vq_callback callbacks[VIRTIO_I2C_VRINGS];
	int status;

	if (!videv || !vdev || !cb_set || !shm_io)
		return -EINVAL;

	if (!cb_set->read || !cb_set->write)
		return -EINVAL;

	if (VIRTIO_ROLE_IS_DRIVER(vdev)) {
		return -ENOTSUP;
	}

	memset(videv, 0, sizeof(*videv));

	videv->vdev = vdev;
	videv->callbacks = cb_set;

	vdev->priv = videv;

	metal_mutex_init(&videv->lock);

	vq_names[0] = "requestq";
	callbacks[0] = dev_virtio_i2c_req_callback;

	/* Create virtqueues for remote device */
	status = virtio_create_virtqueues(vdev, 0, VIRTIO_I2C_VRINGS,
					  vq_names, callbacks, NULL);
	if (status != 0)
		return status;

	videv->vq_request = vdev->vrings_info[0].vq;
	videv->vq_request->shm_io = shm_io;

	return 0;
}

void dev_virtio_i2c_deinit_vdev(struct dev_virtio_i2c_device *videv)
{
	if (videv) {
		videv->vq_request = NULL;

		virtio_delete_virtqueues(videv->vdev);
		videv->vdev = NULL;

		metal_mutex_deinit(&videv->lock);
	}
}
