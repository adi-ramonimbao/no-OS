/*
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file   parameters.h
 * @brief  Non-Secure-world platform parameters for the MAX32657 TZ consumer.
 *
 * The Non-Secure world drives the console UART and the board LED, both handed
 * over by the Secure world before the transition.
 */

#ifndef __PARAMETERS_H__
#define __PARAMETERS_H__

#include "maxim_capi_uart.h"
#include "maxim_capi_gpio.h"
#include "capi_uart.h"
#include "capi_gpio.h"

/*
 * The console UART runs in polling mode (use_irq = false): printf goes through
 * the blocking CAPI transmit path, so no CAPI IRQ controller is needed.
 */
#define UART_IDENTIFIER		0U
#define UART_OPS		&max_capi_uart_ops
#define UART_BAUDRATE		115200
#define UART_EXTRA_TYPE		struct max_capi_uart_extra
#define UART_EXTRA_INIT		{ .use_irq = false }
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

/* Resolves the one platform-specific line keystore_nonsecure.c needs: routing
 * printf/stdio through the CAPI UART handle. */
#define CAPI_UART_STDIO_ENABLE(h)	max_capi_uart_stdio_enable(h)

/* Base of the Secure SRAM alias; a Non-Secure caller cannot access it. Used by
 * the keystore example to provoke and reject accesses into Secure memory. */
#define TZ_SECURE_SRAM_BASE	0x30000000U

#endif /* __PARAMETERS_H__ */
