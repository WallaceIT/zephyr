cmake_minimum_required(VERSION 3.20.0)
# Copyright (c) 2020 STMicroelectronics
#
# SPDX-License-Identifier: Apache-2.0
#

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(multi_vdev)

target_include_directories(app PRIVATE ${LIBMETAL_INCLUDE_DIR} ${OPENAMP_INCLUDE_DIR} ${PLATFORM_DIR})

target_sources(app
        PRIVATE
        src/main.c
        src/resource_table.c
        src/addr_translation.c
        src/dev_virtio_can.c
        src/dev_virtio_entropy.c
        src/dev_virtio_gpio.c
        src/dev_virtio_i2c.c
        src/dev_virtio_spi.c
)
