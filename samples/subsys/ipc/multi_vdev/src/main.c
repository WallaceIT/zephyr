/*
 * Copyright (c) 2026 Francesco Valla
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "zephyr/sys/util.h"
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/ipm.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/random/random.h>

#include <metal/sys.h>
#include <metal/io.h>

#include <openamp/open_amp.h>
#include <openamp/remoteproc.h>
#include <openamp/remoteproc_virtio.h>
#include <openamp/virtio.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(multi_vdev);

#include "addr_translation.h"
#include "resource_table.h"

#include "dev_virtio_entropy.h"
#include "dev_virtio_gpio.h"
#include "dev_virtio_i2c.h"
#include "dev_virtio_spi.h"
#include "dev_virtio_can.h"

#include "app_virtio_config.h"

#define VIRTIO_RPMSG_VRINGS 2

#define VDEV_THREAD_PRIORITY 8

enum {
	SEM_RPMSG,
	SEM_ENTROPY,
	SEM_GPIO,
	SEM_I2C,
	SEM_SPI,
	SEM_CAN,
	NUM_SEMAPHORES
};

struct app_data {
	// Resource table
	metal_phys_addr_t physmap_rsc_table;
	struct metal_io_region mr_rsc_table;

	// Shared memory
	metal_phys_addr_t physmap_shm;
	struct metal_io_region mr_shm;

	// Shared IPM
	const struct device *ipm_handle;

	// RPMSG vdev
	struct {
		struct virtio_device *vdev;
		struct rpmsg_virtio_device dev_ctx;
		struct k_thread thread;
		K_KERNEL_STACK_MEMBER(stack, 4096);
	} rpmsg;

	// Entropy vdev
	struct {
		struct virtio_device *vdev;
		struct dev_virtio_entropy_device dev_ctx;
		struct k_thread thread;
		K_KERNEL_STACK_MEMBER(stack, 4096);
	} entropy;

	// GPIO vdev
	struct {
		struct virtio_device *vdev;
		struct dev_virtio_gpio_device dev_ctx;
		struct k_thread thread;
		K_KERNEL_STACK_MEMBER(stack, 4096);

		struct gpio_callback irq_cb;
	} gpio;

	// I2C vdev
	struct {
		struct virtio_device *vdev;
		struct dev_virtio_i2c_device dev_ctx;
		struct k_thread thread;
		K_KERNEL_STACK_MEMBER(stack, 4096);
	} i2c;

	// SPI vdev
	struct {
		struct virtio_device *vdev;
		struct dev_virtio_spi_device dev_ctx;
		struct k_thread thread;
		K_KERNEL_STACK_MEMBER(stack, 4096);
	} spi;

	// CAN vdev
	struct {
		struct virtio_device *vdev;
		struct dev_virtio_can_device dev_ctx;
		struct k_thread thread;
		K_KERNEL_STACK_MEMBER(stack, 4096);
	} can;

	struct k_sem sem[NUM_SEMAPHORES];
} static data_inst;

static int vdev_notify(void *priv, uint32_t id)
{
	struct app_data *data = priv;

	// On i.MX, the notify ID is sent in the upper 16 bits of the message,
	// so we need to shift it down to get the actual ID.
	if (IS_ENABLED(CONFIG_MBOX_NXP_IMX_MU))
		id >>= 16;

	return ipm_send(data->ipm_handle, -1, 0, &id, sizeof(id));
}

static int init_vrings(unsigned int vdev_idx, struct virtio_device *vdev,
		       unsigned int num_vrings, struct metal_io_region *shm)
{
	const struct fw_rsc_vdev_vring *rsc_vring;
	unsigned int v;
	int ret;

	for (v = 0; v < num_vrings; v++) {
		rsc_vring = resource_table_get_vring(vdev_idx, v);
		if (!rsc_vring)
			return -ENODEV;
	
		LOG_DBG("vdev%uvring%u is at %08X with notify %u", vdev_idx, v,
			(unsigned int)rsc_vring->da, rsc_vring->notifyid);

		ret = rproc_virtio_init_vring(vdev, v, rsc_vring->notifyid,
					      (void *)rsc_vring->da, shm,
					      rsc_vring->num, rsc_vring->align);
		if (ret)
			return ret;
	}

	return 0;
}

/*******************************************************************************
 ************************************ RPMSG ************************************
 ******************************************************************************/
static void rpmsg_reset_callback(struct virtio_device *vdev)
{
	LOG_INF("[RPMSG] reset callback");
}

static void rpmsg_ns_cb(struct rpmsg_device *rdev, const char *name,
			uint32_t src)
{
	ARG_UNUSED(rdev);
	ARG_UNUSED(src);

	LOG_ERR("[RPMSG] unexpected ns service receive for name %s", name);
}

static int init_rpmsg(struct app_data *data)
{
	const struct fw_rsc_vdev *rsc_vdev;
	int ret;

	rsc_vdev = resource_table_get_vdev(RSC_TABLE_VDEV_RPMSG);
	if (!rsc_vdev) {
		LOG_ERR("[RPMSG] Failed to find vdev");
		return -1;
	}

	data->rpmsg.vdev = rproc_virtio_create_vdev(VIRTIO_DEV_DEVICE,
						    rsc_vdev->notifyid,
						    (void *)rsc_vdev,
						    &data->mr_rsc_table,
						    (void *)data,
						    vdev_notify,
						    rpmsg_reset_callback);
	if (!data->rpmsg.vdev) {
		LOG_ERR("[RPMSG] Failed to create vdev");
		return -1;
	}

	LOG_INF("[RPMSG] Wait for remote driver to be ready...");
	rproc_virtio_wait_remote_ready(data->rpmsg.vdev);

	ret = init_vrings(RSC_TABLE_VDEV_RPMSG, data->rpmsg.vdev,
			  VIRTIO_RPMSG_VRINGS, &data->mr_shm);
	if (ret != 0) {
		LOG_ERR("[RPMSG] Failed to init vrings: %d", ret);
		return ret;
	}

	ret = rpmsg_init_vdev(&data->rpmsg.dev_ctx, data->rpmsg.vdev,
			      rpmsg_ns_cb, &data->mr_shm, NULL);
	if (ret) {
		LOG_ERR("[RPMSG] Failed to init vdev: %d", ret);
		return ret;
	}

	LOG_INF("[RPMSG] vdev ready");

	return 0;
}

static void rpmsg_thread(void *p1, void *p2, void *p3)
{
	struct app_data *data = p1;
	struct k_sem *sem = p2;
	int ret;

	ARG_UNUSED(p3);

	ret = init_rpmsg(data);
	if (ret)
		return;

	for (;;) {
		k_sem_take(sem, K_FOREVER);
		rproc_virtio_notified(data->rpmsg.vdev, RSC_NOTIFY_ID_ANY);
	}
}

/*******************************************************************************
 ********************************* ENTROPY *************************************
 ******************************************************************************/
static void entropy_reset_callback(struct virtio_device *vdev)
{
	LOG_INF("[ENTROPY] reset callback");
}

static void entropy_vedev_callback(uint8_t *data, uint32_t *len)
{
	LOG_DBG("[ENTROPY] fill buffer");

	sys_rand_get((void *)data, *len);
}

static int init_entropy(struct app_data *data)
{
	const struct fw_rsc_vdev *rsc_vdev;
	int ret;

	rsc_vdev = resource_table_get_vdev(RSC_TABLE_VDEV_ENTROPY);
	if (!rsc_vdev) {
		LOG_ERR("[ENTROPY] Failed to find vdev");
		return -1;
	}

	data->entropy.vdev = rproc_virtio_create_vdev(VIRTIO_DEV_DEVICE,
						      rsc_vdev->notifyid,
						      (void *)rsc_vdev,
						      &data->mr_rsc_table,
						      (void *)data,
						      vdev_notify,
						      entropy_reset_callback);
	if (!data->entropy.vdev) {
		LOG_ERR("[ENTROPY] Failed to create vdev");
		return -1;
	}

	LOG_INF("[ENTROPY] Wait for remote driver to be ready...");
	rproc_virtio_wait_remote_ready(data->entropy.vdev);

	ret = init_vrings(RSC_TABLE_VDEV_ENTROPY, data->entropy.vdev,
			  VIRTIO_ENTROPY_VRINGS, &data->mr_shm);
	if (ret != 0) {
		LOG_ERR("[ENTROPY] Failed to init vrings: %d", ret);
		return ret;
	}

	ret = dev_virtio_entropy_init_vdev(&data->entropy.dev_ctx,
					   data->entropy.vdev,
					   entropy_vedev_callback,
					   &data->mr_shm);
	if (ret) {
		LOG_ERR("[ENTROPY] Failed to init vdev");
		return ret;
	}

	LOG_INF("[ENTROPY] vdev ready");

	return 0;
}

static void entropy_thread(void *p1, void *p2, void *p3)
{
	struct app_data *data = p1;
	struct k_sem *sem = p2;
	int ret;

	ARG_UNUSED(p3);

	ret = init_entropy(data);
	if (ret)
		return;

	for (;;) {
		k_sem_take(sem, K_FOREVER);
		rproc_virtio_notified(data->entropy.vdev, RSC_NOTIFY_ID_ANY);
	}
}

/*******************************************************************************
 ************************************* GPIO ************************************
 ******************************************************************************/
static void gpio_reset_callback(struct virtio_device *vdev)
{
	LOG_INF("[GPIO] reset callback");
}

static int gpio_get_names(uint8_t *data, size_t len)
{
	if (len != v_gpio_names_len)
		return -1;

	memcpy(data, v_gpio_names, v_gpio_names_len);
	
	return 0;
}

static const struct device *gpio_controller = DEVICE_DT_GET(DT_CHOSEN(gpio_controller));

static struct {
	dev_virtio_gpio_direction_t dir;
	gpio_flags_t flags;

	void *irq_ctx;
	unsigned int irq_ctx2;
} gpio_conf[V_GPIO_NUM];

static atomic_t gpio_irq_pending_mask = ATOMIC_INIT(0);

static bool is_function_pin(unsigned int gpio)
{
	switch (gpio) {
	// I2C pins
	case 2:
	case 3:
		return true;

	// SPI pins
	case 7:
	case 9:
	case 10:
	case 11:
	case 25:
		return true;

	default:
		return false;
	}
}

static int gpio_get_direction(unsigned int gpio, dev_virtio_gpio_direction_t *dir)
{
	if (gpio >= V_GPIO_NUM)
		return -1;

	if (is_function_pin(gpio)) {
		*dir = DEV_VIRTIO_GPIO_DIRECTION_NONE;
	} else {
		*dir = gpio_conf[gpio].dir;
	}

	return 0;
}

static int gpio_set_direction(unsigned int gpio, dev_virtio_gpio_direction_t dir)
{
	if (gpio >= V_GPIO_NUM || is_function_pin(gpio))
		return -1;

	gpio_conf[gpio].dir = dir;

	return 0;
}

static int gpio_get_value(unsigned int gpio, unsigned int *value)
{
	int ret;

	if (gpio >= V_GPIO_NUM || is_function_pin(gpio))
		return -1;

	ret = gpio_pin_get(gpio_controller, gpio);
	if (ret < 0)
		return -1;

	*value = (ret == 0) ? 0 : 1;

	return 0;
}

static int gpio_set_value(unsigned int gpio, unsigned int value)
{
	if (gpio >= V_GPIO_NUM || is_function_pin(gpio))
		return -1;

	gpio_conf[gpio].dir = DEV_VIRTIO_GPIO_DIRECTION_OUT;

	return gpio_pin_configure(gpio_controller, gpio,
				  value ? GPIO_OUTPUT_ACTIVE : GPIO_OUTPUT_INACTIVE);
}

static int gpio_set_irq_type(unsigned int gpio, dev_virtio_irq_type_t type)
{
	if (gpio >= V_GPIO_NUM || is_function_pin(gpio))
		return -1;

	LOG_INF("[GPIO] Set IRQ %u type %u", gpio, type);

	switch (type) {
	case DEV_VIRTIO_GPIO_IRQ_TYPE_NONE:
		gpio_conf[gpio].flags = GPIO_INT_DISABLE;
		break;
	case DEV_VIRTIO_GPIO_IRQ_TYPE_EDGE_RISING:
		gpio_conf[gpio].flags = GPIO_INT_EDGE_RISING;
		break;
	case DEV_VIRTIO_GPIO_IRQ_TYPE_EDGE_FALLING:
		gpio_conf[gpio].flags = GPIO_INT_EDGE_FALLING;
		break;
	case DEV_VIRTIO_GPIO_IRQ_TYPE_EDGE_BOTH:
		gpio_conf[gpio].flags = GPIO_INT_EDGE_BOTH;
		break;
	case DEV_VIRTIO_GPIO_IRQ_TYPE_LEVEL_HIGH:
		gpio_conf[gpio].flags = GPIO_INT_LEVEL_HIGH;
		break;
	case DEV_VIRTIO_GPIO_IRQ_TYPE_LEVEL_LOW:
		gpio_conf[gpio].flags = GPIO_INT_LEVEL_LOW;
		break;
	default:
		return -1;
	}

	return 0;
}

static int gpio_unmask_irq(unsigned int gpio, void *ctx, unsigned int ctx2)
{
	if (gpio >= V_GPIO_NUM || is_function_pin(gpio))
		return -1;

	gpio_conf[gpio].irq_ctx = ctx;
	gpio_conf[gpio].irq_ctx2 = ctx2;

	LOG_INF("[GPIO] Unmask IRQ %u (flags:%Xh)", gpio, gpio_conf[gpio].flags);

	return gpio_pin_interrupt_configure(gpio_controller, gpio,
					    gpio_conf[gpio].flags);
}

static const dev_virtio_gpio_cb_set gpio_callbacks = {
	.get_names = gpio_get_names,
	.get_direction = gpio_get_direction,
	.set_direction = gpio_set_direction,
	.get_value = gpio_get_value,
	.set_value = gpio_set_value,
	.set_irq_type = gpio_set_irq_type,
	.unmask_irq = gpio_unmask_irq,
};

static void gpio_interrupt_callback(const struct device *dev,
				    struct gpio_callback *gpio_cb,
				    uint32_t pins)
{
	struct app_data *data = CONTAINER_OF(gpio_cb, struct app_data, gpio.irq_cb);
	unsigned int gpio;

	LOG_INF("[GPIO] Interrupt received for pins: 0x%X", pins);

	// Mask interrupts, will be re-enabled by gpio_unmask_irq
	for (gpio = 0; gpio < V_GPIO_NUM; gpio++) {
		if (pins & BIT(gpio)) {
			gpio_pin_interrupt_configure(gpio_controller, gpio,
						     GPIO_INT_MODE_DISABLED);
		}
	}

	atomic_or(&gpio_irq_pending_mask, (atomic_val_t)pins);
	k_sem_give(&data->sem[SEM_GPIO]);
}

static void gpio_process_irqs(struct dev_virtio_gpio_device *vgdev)
{
	uint32_t irq_pending_mask = atomic_clear(&gpio_irq_pending_mask);
	unsigned int gpio;

	for (gpio = 0; gpio < V_GPIO_NUM; gpio++) {
		if (irq_pending_mask & BIT(gpio)) {
			LOG_INF("[GPIO] Send IRQ for GPIO %u", gpio);
			dev_virtio_send_irq(vgdev, gpio,
					    gpio_conf[gpio].irq_ctx,
					    gpio_conf[gpio].irq_ctx2);
		}
	}
}

static int init_gpio(struct app_data *data)
{
	const struct fw_rsc_vdev *rsc_vdev;
	int ret;

	rsc_vdev = resource_table_get_vdev(RSC_TABLE_VDEV_GPIO);
	if (!rsc_vdev) {
		LOG_ERR("[GPIO] Failed to find vdev");
		return -1;
	}

	data->gpio.vdev = rproc_virtio_create_vdev(VIRTIO_DEV_DEVICE,
						   rsc_vdev->notifyid,
						   (void *)rsc_vdev,
						   &data->mr_rsc_table,
						   (void *)data,
						   vdev_notify,
						   gpio_reset_callback);
	if (!data->gpio.vdev) {
		LOG_ERR("[GPIO] Failed to create vdev");
		return -1;
	}

	LOG_INF("[GPIO] Wait for remote driver to be ready...");
	rproc_virtio_wait_remote_ready(data->gpio.vdev);

	ret = init_vrings(RSC_TABLE_VDEV_GPIO, data->gpio.vdev,
			  VIRTIO_GPIO_VRINGS, &data->mr_shm);
	if (ret != 0) {
		LOG_ERR("[GPIO] Failed to init vrings: %d", ret);
		return ret;
	}

	ret = dev_virtio_gpio_init_vdev(&data->gpio.dev_ctx, data->gpio.vdev,
					&gpio_callbacks, &data->mr_shm);
	if (ret) {
		LOG_ERR("[GPIO] Failed to init vdev: %d", ret);
		return ret;
	}

	LOG_INF("[GPIO] vdev ready");

	return 0;
}

static void gpio_thread(void *p1, void *p2, void *p3)
{
	struct app_data *data = p1;
	struct k_sem *sem = p2;
	int ret;

	ARG_UNUSED(p3);

	gpio_init_callback(&data->gpio.irq_cb, gpio_interrupt_callback,
			   BIT_MASK(V_GPIO_NUM));
	ret = gpio_add_callback(gpio_controller, &data->gpio.irq_cb);
	if (ret) {
		LOG_ERR("[GPIO] Could not set gpio callback.");
	}

	ret = init_gpio(data);
	if (ret)
		return;

	for (;;) {
		k_sem_take(sem, K_FOREVER);

		gpio_process_irqs(&data->gpio.dev_ctx);

		rproc_virtio_notified(data->gpio.vdev, RSC_NOTIFY_ID_ANY);
	}
}

/*******************************************************************************
 ************************************* I2C *************************************
 ******************************************************************************/
static void i2c_reset_callback(struct virtio_device *vdev)
{
	LOG_INF("[I2C] reset callback");
}

static const struct device *i2c_controller = DEVICE_DT_GET_OR_NULL(DT_CHOSEN(i2c_controller));

static int i2c_read_cb(uint16_t addr, uint8_t *data, size_t len)
{
	struct i2c_msg msg;

	if (!i2c_controller)
		return -1;

	msg.buf = data;
	msg.len = len;
	msg.flags = I2C_MSG_READ | I2C_MSG_STOP;

	return i2c_transfer(i2c_controller, &msg, 1, addr);
}

static int i2c_write_cb(uint16_t addr, const uint8_t *data, size_t len)
{
	struct i2c_msg msg;

	if (!i2c_controller)
		return -1;

	msg.buf = (uint8_t *)data;
	msg.len = len;
	msg.flags = I2C_MSG_WRITE | I2C_MSG_STOP;

	return i2c_transfer(i2c_controller, &msg, 1, addr);
}

static const dev_virtio_i2c_cb_set i2c_callbacks = {
	.read = i2c_read_cb,
	.write = i2c_write_cb,
};

static int init_i2c(struct app_data *data)
{
	const struct fw_rsc_vdev *rsc_vdev;
	int ret;

	rsc_vdev = resource_table_get_vdev(RSC_TABLE_VDEV_I2C);
	if (!rsc_vdev) {
		LOG_ERR("[I2C] Failed to find vdev");
		return -1;
	}

	data->i2c.vdev = rproc_virtio_create_vdev(VIRTIO_DEV_DEVICE,
						  rsc_vdev->notifyid,
						  (void *)rsc_vdev,
						  &data->mr_rsc_table,
						  (void *)data,
						  vdev_notify,
						  i2c_reset_callback);
	if (!data->i2c.vdev) {
		LOG_ERR("[I2C] Failed to create vdev");
		return -1;
	}

	LOG_INF("[I2C] Wait for remote driver to be ready...");
	rproc_virtio_wait_remote_ready(data->i2c.vdev);

	ret = init_vrings(RSC_TABLE_VDEV_I2C, data->i2c.vdev,
			  VIRTIO_I2C_VRINGS, &data->mr_shm);
	if (ret != 0) {
		LOG_ERR("[I2C] Failed to init vrings: %d", ret);
		return ret;
	}

	ret = dev_virtio_i2c_init_vdev(&data->i2c.dev_ctx, data->i2c.vdev,
				       &i2c_callbacks, &data->mr_shm);
	if (ret) {
		LOG_ERR("[I2C] Failed to init vdev: %d", ret);
		return ret;
	}

	LOG_INF("[I2C] vdev ready");

	return 0;
}

static void i2c_thread(void *p1, void *p2, void *p3)
{
	struct app_data *data = p1;
	struct k_sem *sem = p2;
	int ret;

	ARG_UNUSED(p3);

	if (i2c_controller) {
		ret = i2c_configure(i2c_controller,
				    I2C_MODE_CONTROLLER | I2C_SPEED_SET(I2C_SPEED_STANDARD));
		if (ret) {
			LOG_ERR("[I2C] Failed to init controller");
			i2c_controller = NULL;
		}
	} else {
		LOG_WRN("[I2C] No controller found!");
	}

	ret = init_i2c(data);
	if (ret)
		return;

	for (;;) {
		k_sem_take(sem, K_FOREVER);
		rproc_virtio_notified(data->i2c.vdev, RSC_NOTIFY_ID_ANY);
	}
}

/*******************************************************************************
 ************************************* SPI *************************************
 ******************************************************************************/
static const struct device *spi_controller = DEVICE_DT_GET_OR_NULL(DT_CHOSEN(spi_controller));

static const struct gpio_dt_spec spi_cd_gpios[] = {
	GPIO_DT_SPEC_GET_BY_IDX(DT_CHOSEN(spi_controller), cs_gpios, 0),
	GPIO_DT_SPEC_GET_BY_IDX(DT_CHOSEN(spi_controller), cs_gpios, 1),
};

static int spi_transceive_cb(const struct dev_virtio_spi_xcv_config *xcv_config,
			     const uint8_t *tx_buf, uint8_t *rx_buf, size_t len)
{
	struct spi_buf txb = { .buf = (uint8_t *)tx_buf, .len = len };
	struct spi_buf rxb = { .buf = rx_buf, .len = len };

	struct spi_buf_set tx_set = { .buffers = &txb, .count = 1 };
	struct spi_buf_set rx_set = { .buffers = &rxb, .count = 1 };

	struct spi_config config = { 0 };
	int ret;

	if (!spi_controller) {
		LOG_ERR("[SPI] Controller not available");
		return -1;
	}

	if (xcv_config->chip_select_id >= ARRAY_SIZE(spi_cd_gpios)) {
		LOG_ERR("[SPI] Invalid CS id %u", xcv_config->chip_select_id);
		return -1;
	}

	config.frequency = xcv_config->frequency;
	config.operation = SPI_OP_MODE_CONTROLLER |
			   SPI_WORD_SET(xcv_config->bits_per_word);

	if (xcv_config->cpol)
		config.operation |= SPI_MODE_CPOL;

	if (xcv_config->cpha)
		config.operation |= SPI_MODE_CPHA;

	if (xcv_config->loopback)
		config.operation |= SPI_MODE_LOOP;

	if (xcv_config->lsb_first)
		config.operation |= SPI_TRANSFER_LSB;

	if (xcv_config->cs_active_high)
		config.operation |= SPI_CS_ACTIVE_HIGH;

	if (!xcv_config->cs_change)
		config.operation |= SPI_HOLD_ON_CS;

	if (xcv_config->tx_nbits != xcv_config->rx_nbits) {
		LOG_ERR("[SPI] TX (%u) and RX (%u) bits differ",
			xcv_config->tx_nbits, xcv_config->rx_nbits);
		return -1;
	}

	switch (xcv_config->tx_nbits) {
		case 1:
			config.operation |= SPI_LINES_SINGLE;
			break;

		case 2:
			config.operation |= SPI_LINES_DUAL;
			break;

		case 4:
			config.operation |= SPI_LINES_QUAD;
			break;

		case 8:
			config.operation |= SPI_LINES_OCTAL;
			break;

		default:
			LOG_ERR("[SPI] Unsupported transfer bits (%u)",
				xcv_config->tx_nbits);
			return -1;
	}

	config.peripheral = xcv_config->chip_select_id;
	config.word_delay = xcv_config->word_delay_ns;
	config.cs.gpio = spi_cd_gpios[xcv_config->chip_select_id];
	config.cs.cs_is_gpio = true;
	config.cs.delay = 1000;

	ret = spi_transceive(spi_controller, &config,
			     tx_buf ? &tx_set : NULL,
			     rx_buf ? &rx_set : NULL);

	if (ret != 0)
		LOG_ERR("[SPI] Transceive failed %u -> %d", len, ret);

	return ret;
}

static const dev_virtio_spi_cb_set spi_callbacks = {
	.transceive = spi_transceive_cb,
};

static int init_spi(struct app_data *data)
{
	const struct fw_rsc_vdev *rsc_vdev;
	int ret;

	rsc_vdev = resource_table_get_vdev(RSC_TABLE_VDEV_SPI);
	if (!rsc_vdev) {
		LOG_ERR("[SPI] Failed to find vdev");
		return -1;
	}

	data->spi.vdev = rproc_virtio_create_vdev(VIRTIO_DEV_DEVICE,
						  rsc_vdev->notifyid,
						  (void *)rsc_vdev,
						  &data->mr_rsc_table,
						  (void *)data,
						  vdev_notify,
						  i2c_reset_callback);
	if (!data->spi.vdev) {
		LOG_ERR("[SPI] Failed to create vdev");
		return -1;
	}

	LOG_INF("[SPI] Wait for remote driver to be ready...");
	rproc_virtio_wait_remote_ready(data->spi.vdev);

	ret = init_vrings(RSC_TABLE_VDEV_SPI, data->spi.vdev,
			  VIRTIO_SPI_VRINGS, &data->mr_shm);
	if (ret != 0) {
		LOG_ERR("[SPI] Failed to init vrings: %d", ret);
		return ret;
	}

	ret = dev_virtio_spi_init_vdev(&data->spi.dev_ctx, data->spi.vdev,
				       &spi_callbacks, &data->mr_shm);
	if (ret) {
		LOG_ERR("[SPI] Failed to init vdev: %d", ret);
		return ret;
	}

	LOG_INF("[SPI] vdev ready");

	return 0;
}

static void spi_thread(void *p1, void *p2, void *p3)
{
	struct app_data *data = p1;
	struct k_sem *sem = p2;
	unsigned int pin;
	int ret;

	ARG_UNUSED(p3);

	if (!spi_controller)
		LOG_WRN("No SPI controller found!");
	else
		for (pin = 0; pin < ARRAY_SIZE(spi_cd_gpios); pin++)
			gpio_pin_configure_dt(&spi_cd_gpios[pin],
					      GPIO_OUTPUT_INACTIVE);

	ret = init_spi(data);
	if (ret)
		return;

	for (;;) {
		k_sem_take(sem, K_FOREVER);
		rproc_virtio_notified(data->spi.vdev, RSC_NOTIFY_ID_ANY);
	}
}

/*******************************************************************************
 ************************************* CAN *************************************
 ******************************************************************************/
static void can_reset_callback(struct virtio_device *vdev)
{
	LOG_INF("[CAN] reset callback");
}

static int can_set_started(bool start)
{
	LOG_INF("[CAN] %s", start ? "started" : "stopped");

	return 0;
}

static int can_send_cb(uint32_t id, uint16_t length, const uint8_t *sdu,
		       bool extended, bool fd, bool rtr)
{
	LOG_INF("[CAN] send %s msg ID=%0*Xh length=%u%s", fd ? "FD" : "CC",
		extended ? 8 : 3, id, length, rtr ? " (RTR)" : "");

	return 0;
}

static const dev_virtio_can_cb_set can_callbacks = {
	.set_started = can_set_started,
	.send = can_send_cb,
};

static int init_can(struct app_data *data)
{
	const struct fw_rsc_vdev *rsc_vdev;
	int ret;

	rsc_vdev = resource_table_get_vdev(RSC_TABLE_VDEV_CAN);
	if (!rsc_vdev) {
		LOG_ERR("[CAN] Failed to find vdev");
		return -1;
	}

	data->can.vdev = rproc_virtio_create_vdev(VIRTIO_DEV_DEVICE,
						  rsc_vdev->notifyid,
						  (void *)rsc_vdev,
						  &data->mr_rsc_table,
						  (void *)data,
						  vdev_notify,
						  can_reset_callback);
	if (!data->can.vdev) {
		LOG_ERR("[CAN] Failed to create vdev");
		return -1;
	}

	LOG_INF("[CAN] Wait for remote driver to be ready...");
	rproc_virtio_wait_remote_ready(data->can.vdev);

	ret = init_vrings(RSC_TABLE_VDEV_CAN, data->can.vdev,
			  VIRTIO_CAN_VRINGS, &data->mr_shm);
	if (ret != 0) {
		LOG_ERR("[CAN] Failed to init vrings: %d", ret);
		return ret;
	}

	ret = dev_virtio_can_init_vdev(&data->can.dev_ctx, data->can.vdev,
				       &can_callbacks, &data->mr_shm);
	if (ret) {
		LOG_ERR("[CAN] Failed to init vdev: %d", ret);
		return ret;
	}

	LOG_INF("[CAN] vdev ready");

	return 0;
}

static void can_thread(void *p1, void *p2, void *p3)
{
	struct app_data *data = p1;
	struct k_sem *sem = p2;
	int ret;

	ARG_UNUSED(p3);

	ret = init_can(data);
	if (ret)
		return;

	for (;;) {
		k_sem_take(sem, K_FOREVER);
		rproc_virtio_notified(data->can.vdev, RSC_NOTIFY_ID_ANY);
	}
}

/*******************************************************************************
 ************************************* IPM *************************************
 ******************************************************************************/
static void ipm_callback(const struct device *ipm, void *user_data, uint32_t id,
			 volatile void *msg)
{
	struct app_data *data = user_data;
	uint32_t notify_id;
	unsigned int s;

	ARG_UNUSED(ipm);
	ARG_UNUSED(id);

	if (msg == NULL) {
		LOG_ERR("[IPM] received invalid data");
		return;
	}

	notify_id = *(uint32_t *)(msg);

	// On i.MX, the notify ID is sent in the upper 16 bits of the message,
	// so we need to shift it down to get the actual ID.
	if (IS_ENABLED(CONFIG_MBOX_NXP_IMX_MU))
		notify_id >>= 16;

	LOG_DBG("[IPM] received notification for %02Xh", notify_id);

	// Single mailbox design: all semaphores need to be notified, or we might
	// miss a notification under load
	for (s = 0; s < NUM_SEMAPHORES; s++)
		k_sem_give(&data->sem[s]);
}

#define SHM_START_ADDR		DT_REG_ADDR(DT_CHOSEN(zephyr_ipc_shm))
#define SHM_SIZE		DT_REG_SIZE(DT_CHOSEN(zephyr_ipc_shm))

static int init_vdevs(struct app_data *data)
{
	size_t rsc_table_size;
	void *rsc_table = resource_table_get(&rsc_table_size);
	unsigned int s;
	k_tid_t tid;
	int ret;

	// Map resource table
	data->physmap_rsc_table = (metal_phys_addr_t)rsc_table;
	metal_io_init(&data->mr_rsc_table, rsc_table,
		      &data->physmap_rsc_table, rsc_table_size,
		      -1, 0, NULL);

	// Map shared memory (vrings + buffer)
	data->physmap_shm = SHM_START_ADDR;
	metal_io_init(&data->mr_shm, (void *)SHM_START_ADDR,
		      &data->physmap_shm, SHM_SIZE,
		      -1, 0, addr_translation_get_ops(data->physmap_shm));

	// Init semaphores
	for (s = 0; s < NUM_SEMAPHORES; s++) {
		ret = k_sem_init(&data->sem[s], 1, 1);
		if (ret) {
			LOG_ERR("Failed to init semaphore %u: %d", s, ret);
			return ret;
		}
	}

	// Init shared IPM
	if (!device_is_ready(data->ipm_handle)) {
		LOG_ERR("IPM device is not ready");
		return -1;
	}

	ipm_register_callback(data->ipm_handle, ipm_callback, data);

	ret = ipm_set_enabled(data->ipm_handle, 1);
	if (ret) {
		LOG_ERR("ipm_set_enabled failed");
		return ret;
	}

	/* Start threads */
	tid = k_thread_create(&data->rpmsg.thread, data->rpmsg.stack,
			      K_THREAD_STACK_SIZEOF(data->rpmsg.stack),
			      rpmsg_thread,
			      (void *)data, (void *)&data->sem[SEM_RPMSG], NULL,
			      VDEV_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(tid, "rpmsg");

	tid = k_thread_create(&data->entropy.thread, data->entropy.stack,
			      K_THREAD_STACK_SIZEOF(data->entropy.stack),
			      entropy_thread,
			      (void *)data, (void *)&data->sem[SEM_ENTROPY], NULL,
			      VDEV_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(tid, "entropy");

	tid = k_thread_create(&data->gpio.thread, data->gpio.stack,
			      K_THREAD_STACK_SIZEOF(data->gpio.stack),
			      gpio_thread,
			      (void *)data, (void *)&data->sem[SEM_GPIO], NULL,
			      VDEV_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(tid, "gpio");

	tid = k_thread_create(&data->i2c.thread, data->i2c.stack,
			      K_THREAD_STACK_SIZEOF(data->i2c.stack),
			      i2c_thread,
			      (void *)data, (void *)&data->sem[SEM_I2C], NULL,
			      VDEV_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(tid, "i2c");

	tid = k_thread_create(&data->spi.thread, data->spi.stack,
			      K_THREAD_STACK_SIZEOF(data->spi.stack),
			      spi_thread,
			      (void *)data, (void *)&data->sem[SEM_SPI], NULL,
			      VDEV_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(tid, "spi");

	tid = k_thread_create(&data->can.thread, data->can.stack,
			      K_THREAD_STACK_SIZEOF(data->can.stack),
			      can_thread,
			      (void *)data, (void *)&data->sem[SEM_CAN], NULL,
			      VDEV_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(tid, "can");

	return 0;
}

int main(void)
{
	struct metal_init_params metal_params = METAL_INIT_DEFAULTS;
	struct app_data *data = &data_inst;
	int ret;

	ret = metal_init(&metal_params);
	if (ret != 0) {
		LOG_INF("Failed to init metal");
		return ret;
	}

	data->ipm_handle = DEVICE_DT_GET(DT_CHOSEN(zephyr_ipc));

	ret = init_vdevs(data);
	if (ret != 0) {
		LOG_INF("Failed to init virtio devices");
		return ret;
	}

	return 0;
}
