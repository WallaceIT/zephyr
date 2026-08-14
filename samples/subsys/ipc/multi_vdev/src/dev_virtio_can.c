/*
 * Virtio CAN device implementation
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
#include "dev_virtio_can.h"

#include "virtio_can.h"

LOG_MODULE_REGISTER(virtio_can, CONFIG_LOG_DEFAULT_LEVEL);

/**
 * @internal
 *
 * @brief TX callback function.
 *
 * @param vq	Pointer to virtqueue on which TX request is received
 */
static void dev_virtio_can_tx_callback(struct virtqueue *vq)
{
	struct virtio_device *vdev = vq->vq_dev;
	struct dev_virtio_can_device *vcdev = vdev->priv;
	const struct virtio_can_tx_out *tx_out = NULL;
	struct virtio_can_tx_in *tx_in = NULL;
	void *buf_out = NULL, *buf_in = NULL;
	uint16_t idx_out, idx_in;
	uint32_t len_out, len_in;
	int ret;

	while (1) {
		/* Process the received data from remote node */
		metal_mutex_acquire(&vcdev->lock);

		buf_out = virtqueue_get_first_avail_buffer(vq, &idx_out, &len_out);

		/* No more filled rx buffers */
		if (!buf_out) {
			metal_mutex_release(&vcdev->lock);
			break;
		}

		buf_in = virtqueue_get_next_avail_buffer(vq, idx_out, &idx_in, &len_in);
		if (!buf_in) {
			virtqueue_add_consumed_buffer(vq, idx_out, 0);
			virtqueue_kick(vq);
			metal_mutex_release(&vcdev->lock);
			metal_log(METAL_LOG_ERROR, "no response buffer");
			break;
		}

		metal_mutex_release(&vcdev->lock);

		if (len_out < sizeof(struct virtio_can_tx_out)) {
			LOG_ERR("invalid tx_out length %u", len_out);
		} else if (len_in < sizeof(struct virtio_can_tx_in)) {
			LOG_ERR("invalid tx_in length %u", len_in);
		} else {
			uint16_t msg_type;

			/* Invalidate cache before using buffer */
			BUFFER_INVALIDATE(buf_out, len_out);

			tx_out = (struct virtio_can_tx_out *)buf_out;
			tx_in = (struct virtio_can_tx_in *)buf_in;

			msg_type = sys_get_le16((const uint8_t *)&tx_out->msg_type);

			/* Set error as default response */
			tx_in->result = VIRTIO_CAN_RESULT_NOT_OK;

			if (unlikely(msg_type != VIRTIO_CAN_TX)) {
				LOG_WRN("Not a CAN TX message!");
			} else if (unlikely(!vcdev->callbacks->send)) {
				LOG_ERR("No send callback");
			} else {
				uint16_t length = sys_get_le16((const uint8_t *)&tx_out->length);
				uint32_t flags = sys_get_le32((const uint8_t *)&tx_out->flags);
				uint32_t can_id = sys_get_le32((const uint8_t *)&tx_out->can_id);

				ret = vcdev->callbacks->send(can_id, length, tx_out->sdu,
							     !!(flags & VIRTIO_CAN_FLAGS_EXTENDED),
							     !!(flags & VIRTIO_CAN_FLAGS_FD),
							     !!(flags & VIRTIO_CAN_FLAGS_RTR));
				if (ret == 0) {
					tx_in->result = VIRTIO_CAN_RESULT_OK;
				} else {
					LOG_ERR("send callback failed with %d", ret);
					tx_in->result = VIRTIO_CAN_RESULT_NOT_OK;
				}
			}

			BUFFER_FLUSH(buf_in, len_in);
		}

		metal_mutex_acquire(&vcdev->lock);

		ret = virtqueue_add_consumed_buffer(vq, idx_out, len_in);
		if (ret != 0)
			LOG_ERR("Add consumed buffer failed: %d", ret);

		virtqueue_kick(vq);

		metal_mutex_release(&vcdev->lock);
	}
}

/**
 * @internal
 *
 * @brief RX callback function.
 *
 * @param vq	Pointer to virtqueue on which RX request is received
 */
static void dev_virtio_can_rx_callback(struct virtqueue *vq)
{

}

/**
 * @internal
 *
 * @brief Control callback function.
 *
 * @param vq	Pointer to virtqueue on which control request is received
 */
static void dev_virtio_can_control_callback(struct virtqueue *vq)
{
	struct virtio_device *vdev = vq->vq_dev;
	struct dev_virtio_can_device *vcdev = vdev->priv;
	const struct virtio_can_control_out *ctrl_out = NULL;
	struct virtio_can_control_in *ctrl_in = NULL;
	void *buf_out = NULL, *buf_in = NULL;
	uint16_t idx_out, idx_in;
	uint32_t len_out, len_in;
	int ret;

	while (1) {
		/* Process the received data from remote node */
		metal_mutex_acquire(&vcdev->lock);

		buf_out = virtqueue_get_first_avail_buffer(vq, &idx_out, &len_out);

		/* No more filled rx buffers */
		if (!buf_out) {
			metal_mutex_release(&vcdev->lock);
			break;
		}

		buf_in = virtqueue_get_next_avail_buffer(vq, idx_out, &idx_in, &len_in);
		if (!buf_in) {
			virtqueue_add_consumed_buffer(vq, idx_out, 0);
			virtqueue_kick(vq);
			metal_mutex_release(&vcdev->lock);
			metal_log(METAL_LOG_ERROR, "no response buffer");
			break;
		}

		metal_mutex_release(&vcdev->lock);

		if (len_out < sizeof(struct virtio_can_control_out)) {
			LOG_ERR("invalid ctrl_out length %u", len_out);
		} else if (len_in < sizeof(struct virtio_can_control_in)) {
			LOG_ERR("invalid ctrl_in length %u", len_in);
		} else {
			uint16_t msg_type;

			/* Invalidate cache before using buffer */
			BUFFER_INVALIDATE(buf_out, len_out);

			ctrl_out = (struct virtio_can_control_out *)buf_out;
			ctrl_in = (struct virtio_can_control_in *)buf_in;

			msg_type = sys_get_le16((const uint8_t *)&ctrl_out->msg_type);

			/* Set error as default response */
			ctrl_in->result = VIRTIO_CAN_RESULT_NOT_OK;

			switch (msg_type) {
			case VIRTIO_CAN_SET_CTRL_MODE_START:
				LOG_DBG("START");

				if (unlikely(!vcdev->callbacks->set_started)) {
					LOG_ERR("No set_started callback");
					break;
				}

				ret = vcdev->callbacks->set_started(true);
				if (ret == 0) {
					ctrl_in->result = VIRTIO_CAN_RESULT_OK;
				} else {
					LOG_ERR("set_started(true) callback failed with %d", ret);
					ctrl_in->result = VIRTIO_CAN_RESULT_NOT_OK;
				}
				break;
			case VIRTIO_CAN_SET_CTRL_MODE_STOP:
				LOG_DBG("STOP");

				if (!vcdev->callbacks->set_started) {
					LOG_ERR("No set_started callback");
					break;
				}

				ret = vcdev->callbacks->set_started(false);
				if (ret == 0) {
					ctrl_in->result = VIRTIO_CAN_RESULT_OK;
				} else {
					LOG_ERR("set_started(false) callback failed with %d", ret);
					ctrl_in->result = VIRTIO_CAN_RESULT_NOT_OK;
				}
				break;
			default:
				LOG_ERR("Unknown control type %u", msg_type);
				break;
			}

			BUFFER_FLUSH(buf_in, len_in);
		}

		metal_mutex_acquire(&vcdev->lock);

		ret = virtqueue_add_consumed_buffer(vq, idx_out, len_in);
		if (ret != 0)
			LOG_ERR("Add consumed buffer failed: %d", ret);

		virtqueue_kick(vq);

		metal_mutex_release(&vcdev->lock);
	}
}

int dev_virtio_can_receive(struct dev_virtio_can_device *vcdev, uint32_t id,
			   uint16_t length, const uint8_t *sdu, bool extended,
			   bool fd, bool rtr)
{
	struct virtqueue *vq = vcdev->vq_rx;
	struct virtio_can_rx *rx = NULL;
	size_t req_len;
	void *buf = NULL;
	uint16_t idx;
	uint32_t len;
	int ret;

	/* Process the received data from remote node */
	metal_mutex_acquire(&vcdev->lock);

	buf = virtqueue_get_first_avail_buffer(vq, &idx, &len);

	/* No buffer available */
	if (!buf) {
		metal_mutex_release(&vcdev->lock);
		return -1;
	}

	metal_mutex_release(&vcdev->lock);

	req_len = sizeof(struct virtio_can_rx) + length;
	if (len < req_len) {
		LOG_ERR("TX buffer too small");
		req_len = 0;
	} else {
		uint32_t flags = 0;

		/* Invalidate cache before using buffer */
		BUFFER_INVALIDATE(buf, len);

		rx = (struct virtio_can_rx *)buf;

		sys_put_le16(VIRTIO_CAN_RX, (uint8_t *)&rx->msg_type);
		sys_put_le32(id, (uint8_t *)&rx->can_id);
		sys_put_le16(length, (uint8_t *)&rx->length);

		if (extended)
			flags |= VIRTIO_CAN_FLAGS_EXTENDED;

		if (fd)
			flags |= VIRTIO_CAN_FLAGS_FD;

		if (rtr)
			flags |= VIRTIO_CAN_FLAGS_RTR;

		sys_put_le32(flags, (uint8_t *)&rx->flags);

		memcpy(rx->sdu, sdu, length);

		BUFFER_FLUSH(buf_in, req_len);
	}

	metal_mutex_acquire(&vcdev->lock);

	ret = virtqueue_add_consumed_buffer(vq, idx, req_len);
	if (ret != 0)
		LOG_ERR("Add consumed buffer failed: %d", ret);

	virtqueue_kick(vq);

	metal_mutex_release(&vcdev->lock);

	return ret;
}

int dev_virtio_can_init_vdev(struct dev_virtio_can_device *vcdev,
			     struct virtio_device *vdev,
			     const dev_virtio_can_cb_set *cb_set,
			     struct metal_io_region *shm_io)
{
	const char *vq_names[VIRTIO_CAN_VRINGS];
	vq_callback callbacks[VIRTIO_CAN_VRINGS];
	int status;

	if (!vcdev || !vdev || !cb_set || !shm_io)
		return -EINVAL;

	if (VIRTIO_ROLE_IS_DRIVER(vdev)) {
		return -ENOTSUP;
	}

	memset(vcdev, 0, sizeof(*vcdev));

	vcdev->vdev = vdev;
	vcdev->callbacks = cb_set;

	vdev->priv = vcdev;

	metal_mutex_init(&vcdev->lock);

	vq_names[0] = "txq";
	callbacks[0] = dev_virtio_can_tx_callback;

	vq_names[1] = "rxq";
	callbacks[1] = dev_virtio_can_rx_callback;

	vq_names[2] = "controlq";
	callbacks[2] = dev_virtio_can_control_callback;

	/* Create virtqueues for remote device */
	status = virtio_create_virtqueues(vdev, 0, VIRTIO_CAN_VRINGS,
					  vq_names, callbacks, NULL);
	if (status != 0)
		return status;

	vcdev->vq_tx = vdev->vrings_info[0].vq;
	vcdev->vq_tx->shm_io = shm_io;

	vcdev->vq_rx = vdev->vrings_info[1].vq;
	vcdev->vq_rx->shm_io = shm_io;

	vcdev->vq_control = vdev->vrings_info[2].vq;
	vcdev->vq_control->shm_io = shm_io;

	return 0;
}

void dev_virtio_can_deinit_vdev(struct dev_virtio_can_device *vcdev)
{
	if (vcdev) {
		vcdev->vq_tx = NULL;
		vcdev->vq_rx = NULL;
		vcdev->vq_control = NULL;

		virtio_delete_virtqueues(vcdev->vdev);
		vcdev->vdev = NULL;

		metal_mutex_deinit(&vcdev->lock);
	}
}
