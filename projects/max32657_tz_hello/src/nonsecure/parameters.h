/***************************************************************************//**
 * @file   parameters.h
 * @brief  Non-Secure-world platform parameters for the MAX32657 TZ hello demo.
 *
 * The Non-Secure world drives the console UART and the board LED, both handed
 * over by the Secure world before the transition.
 *
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *******************************************************************************/

#ifndef __PARAMETERS_H__
#define __PARAMETERS_H__

#include "maxim_capi_uart.h"
#include "maxim_capi_gpio.h"
#include "maxim_capi_irq.h"
#include "capi_uart.h"
#include "capi_gpio.h"
#include "capi_irq.h"

/*
 * CAPI interrupt controller. The UART is opened IRQ-driven (use_irq = true), so
 * capi_uart_init() calls capi_irq_connect() internally; that requires the IRQ
 * controller to have been initialised first via capi_irq_init().
 */
#define IRQ_CTRL_IDENTIFIER	0U
#define IRQ_CTRL_EXTRA		&(struct max_capi_irq_extra_config) {}

#define UART_IDENTIFIER		0U
#define UART_OPS		&max_capi_uart_ops
#define UART_BAUDRATE		115200
#define UART_EXTRA_TYPE		struct max_capi_uart_extra
#define UART_EXTRA_INIT		{ .use_irq = true }
#define PLATFORM_NAME		"MAX32657"

/*
 * On-board LED on the MAX32657EVKIT is P0.13 (led_pin[0] in the MSDK BSP).
 * The GPIO0 port is handed to the Non-Secure world by the Secure image; the
 * port must span enough pins to reach pin 13.
 */
#define LED_IDENTIFIER		0U
#define LED_NUM_PINS		14U
#define LED_PIN_NUMBER		13U
#define LED_OPS			&max_capi_gpio_ops
#define LED_EXTRA_TYPE		struct max_capi_gpio_extra_config
#define LED_EXTRA_INIT		{ .func = MAX_CAPI_GPIO_FUNC_OUT }

#endif /* __PARAMETERS_H__ */
