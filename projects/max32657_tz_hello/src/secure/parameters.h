/***************************************************************************//**
 * @file   parameters.h
 * @brief  Secure-world platform parameters for the MAX32657 TZ hello demo.
 *
 * Only the console UART is configured here: the Secure world prints a banner
 * and then hands the UART to the Non-Secure world.
 *
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *******************************************************************************/

#ifndef __PARAMETERS_H__
#define __PARAMETERS_H__

#include "maxim_capi_uart.h"
#include "capi_uart.h"

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

#endif /* __PARAMETERS_H__ */
