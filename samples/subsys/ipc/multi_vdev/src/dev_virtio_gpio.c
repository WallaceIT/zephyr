/*
 * Virtio GPIO device implementation
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
#include "dev_virtio_gpio.h"

#include "virtio_gpio.h"

LOG_MODULE_REGISTER(virtio_gpio, CONFIG_LOG_DEFAULT_LEVEL);

/**
 * @internal
 *
 * @brief Request callback function.
 *
 * @param vq	Pointer to virtqueue on which request is received
 */
static void dev_virtio_gpio_req_callback(struct virtqueue *vq)
{
	struct virtio_device *vdev = vq->vq_dev;
	struct dev_virtio_gpio_device *vgdev = vdev->priv;
	const struct virtio_gpio_request *gpio_req = NULL;
	void *buf_rq = NULL, *buf_rs = NULL;
	uint16_t idx_rq, idx_rs;
	uint32_t len_rq, len_rs;
	int ret;

	while (1) {
		/* Process the received data from remote node */
		metal_mutex_acquire(&vgdev->lock);

		buf_rq = virtqueue_get_first_avail_buffer(vq, &idx_rq, &len_rq);

		/* No more filled rx buffers */
		if (!buf_rq) {
			metal_mutex_release(&vgdev->lock);
			break;
		}

		buf_rs = virtqueue_get_next_avail_buffer(vq, idx_rq, &idx_rs, &len_rs);
		if (!buf_rs) {
			virtqueue_add_consumed_buffer(vq, idx_rq, 0);
			virtqueue_kick(vq);
			metal_mutex_release(&vgdev->lock);
			metal_log(METAL_LOG_ERROR, "no response buffer");
			break;
		}

		metal_mutex_release(&vgdev->lock);

		if (len_rq < sizeof(struct virtio_gpio_request)) {
			LOG_ERR("invalid request length %u", len_rq);
		} else if (len_rs < sizeof(struct virtio_gpio_response)) {
			LOG_ERR("invalid response length %u", len_rs);
		} else {
			uint16_t type, gpio;
			uint32_t value;

			/* Invalidate cache before using buffer */
			BUFFER_INVALIDATE(buf_rq, len_rq);

			gpio_req = (struct virtio_gpio_request *)buf_rq;

			type = sys_get_le16((const uint8_t *)&gpio_req->type);
			gpio = sys_get_le16((const uint8_t *)&gpio_req->gpio);
			value = sys_get_le32((const uint8_t *)&gpio_req->value);

			/* Set error as default response */
			*(uint8_t *)buf_rs = VIRTIO_GPIO_STATUS_ERR;

			switch (type) {
			case VIRTIO_GPIO_MSG_GET_NAMES:
				LOG_DBG("GET_NAMES");
				if (vgdev->callbacks->get_names != NULL) {
					struct virtio_gpio_response_get_names *res =
						(struct virtio_gpio_response_get_names *)buf_rs;

					ret = vgdev->callbacks->get_names(res->value, len_rs - sizeof(uint8_t));
					if (ret == 0)
						res->status = VIRTIO_GPIO_STATUS_OK;
					else {
						LOG_ERR("get_names callback failed with %d", ret);
						res->status = VIRTIO_GPIO_STATUS_ERR;
					}
				} else {
					LOG_ERR("No get_names callback");
				}
				break;
			case VIRTIO_GPIO_MSG_GET_DIRECTION:
				LOG_DBG("GET_DIRECTION(%u)", gpio);
				if (vgdev->callbacks->get_direction != NULL) {
					struct virtio_gpio_response *res =
						(struct virtio_gpio_response *)buf_rs;
					dev_virtio_gpio_direction_t direction;

					ret = vgdev->callbacks->get_direction(gpio, &direction);
					if (ret == 0) {
						switch (direction) {
						case DEV_VIRTIO_GPIO_DIRECTION_NONE:
							res->value = VIRTIO_GPIO_DIRECTION_NONE;
							res->status = VIRTIO_GPIO_STATUS_OK;
							break;
						case DEV_VIRTIO_GPIO_DIRECTION_OUT:
							res->value = VIRTIO_GPIO_DIRECTION_NONE;
							res->status = VIRTIO_GPIO_STATUS_OK;
							break;
						case DEV_VIRTIO_GPIO_DIRECTION_IN:
							res->value = VIRTIO_GPIO_DIRECTION_NONE;
							res->status = VIRTIO_GPIO_STATUS_OK;
							break;
						default:
							LOG_ERR("Invalid direction %u", direction);
							res->status = VIRTIO_GPIO_STATUS_ERR;
							break;
						}
					} else {
						LOG_ERR("get_direction callback failed with %d", ret);
						res->status = VIRTIO_GPIO_STATUS_ERR;
					}
				} else {
					LOG_ERR("No get_direction callback");
				}
				break;
			case VIRTIO_GPIO_MSG_SET_DIRECTION:
				LOG_DBG("SET_DIRECTION(%u, %u)", gpio, value);
				if (vgdev->callbacks->set_direction != NULL) {
					struct virtio_gpio_response *res =
						(struct virtio_gpio_response *)buf_rs;

					switch (value) {
					case VIRTIO_GPIO_DIRECTION_NONE:
						ret = vgdev->callbacks->set_direction(gpio, DEV_VIRTIO_GPIO_DIRECTION_NONE);
						break;
					case VIRTIO_GPIO_DIRECTION_OUT:
						ret = vgdev->callbacks->set_direction(gpio, DEV_VIRTIO_GPIO_DIRECTION_OUT);
						break;
					case VIRTIO_GPIO_DIRECTION_IN:
						ret = vgdev->callbacks->set_direction(gpio, DEV_VIRTIO_GPIO_DIRECTION_IN);
						break;
					default:
						LOG_ERR("Invalid direction %u", value);
						ret = -1;
						break;
					}

					if (ret == 0) {
						res->value = value;
						res->status = VIRTIO_GPIO_STATUS_OK;
					} else {
						LOG_ERR("set_direction callback failed with %d", ret);
						res->status = VIRTIO_GPIO_STATUS_ERR;
					}
				} else {
					LOG_ERR("No set_direction callback");
				}
				break;
			case VIRTIO_GPIO_MSG_GET_VALUE:
				LOG_DBG("GET_VALUE(%u)", gpio);
				if (vgdev->callbacks->get_value != NULL) {
					struct virtio_gpio_response *res =
						(struct virtio_gpio_response *)buf_rs;
					unsigned int tmp;

					ret = vgdev->callbacks->get_value(gpio, &tmp);
					if (ret == 0) {
						res->status = VIRTIO_GPIO_STATUS_OK;
						res->value = !!tmp;
					} else {
						LOG_ERR("get_value callback failed with %d", ret);
						res->status = VIRTIO_GPIO_STATUS_ERR;
					}
				} else {
					LOG_ERR("No get_value callback");
				}
				break;
			case VIRTIO_GPIO_MSG_SET_VALUE:
				LOG_DBG("SET_VALUE(%u, %u)", gpio, value);
				if (vgdev->callbacks->set_value != NULL) {
					struct virtio_gpio_response *res =
						(struct virtio_gpio_response *)buf_rs;

					ret = vgdev->callbacks->set_value(gpio, value);
					if (ret == 0) {
						res->status = VIRTIO_GPIO_STATUS_OK;
						res->value = value;
					} else {
						LOG_ERR("set_value callback failed with %d", ret);
						res->status = VIRTIO_GPIO_STATUS_ERR;
					}
				}
				break;
			case VIRTIO_GPIO_MSG_IRQ_TYPE:
				LOG_DBG("IRQ_TYPE(%u, %u)", gpio, value);
				if (vgdev->callbacks->set_irq_type != NULL) {
					struct virtio_gpio_response *res =
						(struct virtio_gpio_response *)buf_rs;

					switch (value) {
					case VIRTIO_GPIO_IRQ_TYPE_NONE:
						ret = vgdev->callbacks->set_irq_type(gpio, DEV_VIRTIO_GPIO_IRQ_TYPE_NONE);
						break;
					case VIRTIO_GPIO_IRQ_TYPE_EDGE_RISING:
						ret = vgdev->callbacks->set_irq_type(gpio, DEV_VIRTIO_GPIO_IRQ_TYPE_EDGE_RISING);
						break;
					case VIRTIO_GPIO_IRQ_TYPE_EDGE_FALLING:
						ret = vgdev->callbacks->set_irq_type(gpio, DEV_VIRTIO_GPIO_IRQ_TYPE_EDGE_FALLING);
						break;
					case VIRTIO_GPIO_IRQ_TYPE_EDGE_BOTH:
						ret = vgdev->callbacks->set_irq_type(gpio, DEV_VIRTIO_GPIO_IRQ_TYPE_EDGE_BOTH);
						break;
					case VIRTIO_GPIO_IRQ_TYPE_LEVEL_HIGH:
						ret = vgdev->callbacks->set_irq_type(gpio, DEV_VIRTIO_GPIO_IRQ_TYPE_LEVEL_HIGH);
						break;
					case VIRTIO_GPIO_IRQ_TYPE_LEVEL_LOW:
						ret = vgdev->callbacks->set_irq_type(gpio, DEV_VIRTIO_GPIO_IRQ_TYPE_LEVEL_LOW);
						break;
					default:
						LOG_ERR("Invalid IRQ type %u", value);
						ret = -1;
						break;
					}

					if (ret == 0) {
						res->status = VIRTIO_GPIO_STATUS_OK;
						res->value = value;
					} else {
						LOG_ERR("set_irq_type callback failed with %d", ret);
						res->status = VIRTIO_GPIO_STATUS_ERR;
					}
				} else {
					LOG_ERR("No set_irq_type callback");
				}
				break;
			default:
				LOG_ERR("Unknown request type %u", type);
				break;
			}

			BUFFER_FLUSH(buf_rs, len_rs);
		}

		metal_mutex_acquire(&vgdev->lock);

		ret = virtqueue_add_consumed_buffer(vq, idx_rq, len_rs);
		if (ret != 0)
			LOG_ERR("Add consumed buffer failed: %d", ret);

		virtqueue_kick(vq);

		metal_mutex_release(&vgdev->lock);
	}
}

static void dev_virtio_gpio_ev_callback(struct virtqueue *vq)
{
	struct virtio_device *vdev = vq->vq_dev;
	struct dev_virtio_gpio_device *vgdev = vdev->priv;
	const struct virtio_gpio_irq_request *irq_req = NULL;
	struct virtio_gpio_irq_response *irq_resp = NULL;
	void *buf_rq = NULL, *buf_rs = NULL;
	uint16_t idx_rq, idx_rs;
	uint32_t len_rq, len_rs;
	int ret;

	while (1) {
		/* Process the received data from remote node */
		metal_mutex_acquire(&vgdev->lock);

		buf_rq = virtqueue_get_first_avail_buffer(vq, &idx_rq, &len_rq);

		/* No more filled req buffers */
		if (!buf_rq) {
			metal_mutex_release(&vgdev->lock);
			break;
		}

		buf_rs = virtqueue_get_next_avail_buffer(vq, idx_rq, &idx_rs, &len_rs);
		if (!buf_rs) {
			virtqueue_add_consumed_buffer(vq, idx_rq, 0);
			virtqueue_kick(vq);
			metal_mutex_release(&vgdev->lock);
			metal_log(METAL_LOG_ERROR, "no response buffer");
			break;
		}

		metal_mutex_release(&vgdev->lock);

		if (len_rq < sizeof(struct virtio_gpio_irq_request)) {
			LOG_ERR("invalid request length %u", len_rq);
			ret = -1;
		} else if (len_rs < sizeof(struct virtio_gpio_irq_response)) {
			LOG_ERR("invalid response length %u", len_rs);
			ret = -1;
		} else {
			uint16_t gpio;

			/* Invalidate cache before using buffer */
			BUFFER_INVALIDATE(buf_rq, len_rq);

			irq_req = (struct virtio_gpio_irq_request *)buf_rq;
			irq_resp = (struct virtio_gpio_irq_response *)buf_rs;

			gpio = sys_get_le16((const uint8_t *)&irq_req->gpio);

			LOG_DBG("IRQ_REQ(%u)", gpio);
			if (vgdev->callbacks->unmask_irq != NULL) {
				ret = vgdev->callbacks->unmask_irq(gpio,
								   (void *)irq_resp, idx_rq);
				if (ret != 0) {
					LOG_ERR("unmask_irq callback failed with %d", ret);
				}
			} else {
				LOG_ERR("No unmask_irq callback");
				ret = -1;
			}
		}

		// An immediate response is sent only in case of error
		if (ret != 0) {
			irq_resp->status = VIRTIO_GPIO_IRQ_STATUS_INVALID;
			BUFFER_FLUSH(buf_rs, len_rs);

			metal_mutex_acquire(&vgdev->lock);

			ret = virtqueue_add_consumed_buffer(vq, idx_rq, len_rs);
			if (ret != 0)
				LOG_ERR("Add consumed buffer failed: %d", ret);

			virtqueue_kick(vq);

			metal_mutex_release(&vgdev->lock);
		}
	}
}

void dev_virtio_send_irq(struct dev_virtio_gpio_device *vgdev,
			 unsigned int gpio, void *ctx, unsigned int ctx2)
{
	struct virtio_gpio_irq_response *irq_resp =
		(struct virtio_gpio_irq_response *)ctx;
	uint32_t len_rs = sizeof(struct virtio_gpio_irq_response);
	uint16_t idx = ctx2;
	int ret;

	// Sanity check
	if (!irq_resp) {
		LOG_ERR("Invalid IRQ context!");
		return;
	}

	irq_resp->status = VIRTIO_GPIO_IRQ_STATUS_VALID;

	BUFFER_FLUSH(irq_resp, len_rs);

	metal_mutex_acquire(&vgdev->lock);

	ret = virtqueue_add_consumed_buffer(vgdev->vq_event, idx, len_rs);
	if (ret != 0)
		LOG_ERR("Add consumed buffer failed: %d", ret);

	virtqueue_kick(vgdev->vq_event);

	metal_mutex_release(&vgdev->lock);
}

int dev_virtio_gpio_init_vdev(struct dev_virtio_gpio_device *vgdev,
			      struct virtio_device *vdev,
			      const dev_virtio_gpio_cb_set *cb_set,
			      struct metal_io_region *shm_io)
{
	const char *vq_names[VIRTIO_GPIO_VRINGS];
	vq_callback callbacks[VIRTIO_GPIO_VRINGS];
	int status;

	if (!vgdev || !vdev || !cb_set || !shm_io)
		return -EINVAL;

	if (VIRTIO_ROLE_IS_DRIVER(vdev)) {
		return -ENOTSUP;
	}

	memset(vgdev, 0, sizeof(*vgdev));

	vgdev->vdev = vdev;
	vgdev->callbacks = cb_set;

	vdev->priv = vgdev;

	metal_mutex_init(&vgdev->lock);

	vq_names[0] = "requestq";
	callbacks[0] = dev_virtio_gpio_req_callback;

	vq_names[1] = "eventq";
	callbacks[1] = dev_virtio_gpio_ev_callback;

	/* Create virtqueues for remote device */
	status = virtio_create_virtqueues(vdev, 0, VIRTIO_GPIO_VRINGS,
					  vq_names, callbacks, NULL);
	if (status != 0)
		return status;

	vgdev->vq_request = vdev->vrings_info[0].vq;
	vgdev->vq_request->shm_io = shm_io;

	vgdev->vq_event = vdev->vrings_info[1].vq;
	vgdev->vq_event->shm_io = shm_io;

	return 0;
}

void dev_virtio_gpio_deinit_vdev(struct dev_virtio_gpio_device *vgdev)
{
	if (vgdev) {
		vgdev->vq_request = NULL;
		vgdev->vq_event = NULL;

		virtio_delete_virtqueues(vgdev->vdev);
		vgdev->vdev = NULL;

		metal_mutex_deinit(&vgdev->lock);
	}
}
