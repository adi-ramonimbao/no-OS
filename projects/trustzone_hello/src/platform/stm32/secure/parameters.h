/*
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file   parameters.h
 * @brief  Secure-world platform parameters for the STM32H5 TZ hello demo.
 *
 * The Secure world only brings up the console UART (USART3, ST-LINK VCP) to
 * print its banner before handing it to the Non-Secure world (delegation is
 * done automatically by CubeMX-generated MX_GTZC_S_Init(), called from
 * stm32_init(), based on the .ioc's per-peripheral security context).
 */

#ifndef __PARAMETERS_H__
#define __PARAMETERS_H__

#include "stm32_capi_uart.h"
#include "capi_uart.h"

extern UART_HandleTypeDef huart3;

#define UART_IDENTIFIER		0U
#define UART_OPS		&stm32_capi_uart_ops
#define UART_BAUDRATE		115200U
#define UART_EXTRA_TYPE		struct stm32_uart_extra_config
#define UART_EXTRA_INIT		{ .huart = &huart3 }
#define PLATFORM_NAME		"STM32H563"

/* Resolves the one platform-specific line hello_secure.c's platform main.c
 * needs: routing printf/stdio through the CAPI UART handle. */
#define CAPI_UART_STDIO_ENABLE(h)	stm32_uart_stdio_enable(h)

#endif /* __PARAMETERS_H__ */
