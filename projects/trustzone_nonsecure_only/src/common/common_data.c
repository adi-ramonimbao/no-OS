/*
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file   common_data.c
 * @brief  Common data source for the MAX32657 TrustZone split demo consumer.
 */

#include "common_data.h"
#include "parameters.h"

static struct capi_uart_line_config uart_line_config = {
	.baudrate = UART_BAUDRATE,
	.size = CAPI_UART_DATA_BITS_8,
	.parity = CAPI_UART_PARITY_NONE,
	.stop_bits = CAPI_UART_STOP_1_BIT,
	.flow_control = CAPI_UART_FLOW_CONTROL_NONE,
	.address_mode = CAPI_UART_ADDRESS_MODE_DISABLED,
};

static UART_EXTRA_TYPE uart_extra = UART_EXTRA_INIT;

const struct capi_uart_config uart_config = {
	.identifier = UART_IDENTIFIER,
	.dma_handle = NULL,
	.clk_freq_hz = 0U,
	.line_config = &uart_line_config,
	.extra = &uart_extra,
	.ops = UART_OPS,
};

static LED_EXTRA_TYPE led_extra = LED_EXTRA_INIT;

const struct capi_gpio_port_config led_port_config = {
	.ops = LED_OPS,
	.identifier = LED_IDENTIFIER,
	.num_pins = LED_NUM_PINS,
	.flags = NULL,
	.extra = &led_extra,
};
