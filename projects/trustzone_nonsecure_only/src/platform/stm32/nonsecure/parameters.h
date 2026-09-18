/*
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file   parameters.h
 * @brief  Non-Secure-world platform parameters for the STM32H5 TZ consumer.
 *
 * The Non-Secure world drives the console UART (USART3, ST-LINK VCP) and the
 * board LED (PB0 / LD1), both delegated to Non-Secure by CubeMX-generated
 * MX_GTZC_NS_Init() (called from stm32_init(), based on the .ioc's
 * per-peripheral/per-pin security context).
 */

#ifndef __PARAMETERS_H__
#define __PARAMETERS_H__

#include "stm32_capi_uart.h"
#include "stm32_capi_gpio.h"
#include "capi_uart.h"
#include "capi_gpio.h"

extern UART_HandleTypeDef huart3;

#define UART_IDENTIFIER		0U
#define UART_OPS		&stm32_capi_uart_ops
#define UART_BAUDRATE		115200U
#define UART_EXTRA_TYPE		struct stm32_uart_extra_config
#define UART_EXTRA_INIT		{ .huart = &huart3 }
#define PLATFORM_NAME		"STM32H563"

/* Resolves the one platform-specific line keystore_nonsecure.c needs: routing
 * printf/stdio through the CAPI UART handle. */
#define CAPI_UART_STDIO_ENABLE(h)	stm32_uart_stdio_enable(h)

/*
 * On-board LED (LD1) on the NUCLEO-H563ZI is PB0, delegated to Non-Secure
 * whole (num_pins=1 so bit 0 maps to physical pin 0).
 */
#define LED_IDENTIFIER		((uint64_t)(uintptr_t)GPIOB)
#define LED_NUM_PINS		1U
#define LED_PIN_NUMBER		0U
#define LED_OPS			&stm32_capi_gpio_ops
#define LED_EXTRA_TYPE		struct stm32_capi_gpio_port_config
#define LED_EXTRA_INIT		{ .mode = GPIO_MODE_OUTPUT_PP, \
				  .speed = GPIO_SPEED_FREQ_LOW, \
				  .alternate = 0U, \
				  .pull = GPIO_NOPULL }

/* Base of the Secure SRAM alias; a Non-Secure caller cannot access it. Used by
 * the keystore example to provoke and reject accesses into Secure memory.
 * Coincidentally the same value as MAX32657's (bit 28 set on the NS base) --
 * not a rule to assume for other STM32 parts. */
#define TZ_SECURE_SRAM_BASE	0x30000000U

#endif /* __PARAMETERS_H__ */
