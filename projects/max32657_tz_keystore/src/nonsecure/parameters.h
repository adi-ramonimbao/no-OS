/*
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file   parameters.h
 * @brief  Non-Secure-world platform parameters for the MAX32657 TZ keystore demo.
 *
 * The Non-Secure world drives only the console UART (test report transport),
 * handed over by the Secure world. This header supplies the console UART
 * definitions the shared capi_selftest common_data expects; the peripheral
 * groups whose OPS are left undefined (GPIO/SPI/I2C/timer/DMA/async-UART)
 * compile out of common_data.
 */

#ifndef __PARAMETERS_H__
#define __PARAMETERS_H__

#include "maxim_capi_uart.h"
#include "capi_uart.h"

/*
 * Console / report UART. Polling mode (use_irq = false): the framework's write
 * callback uses the blocking CAPI transmit path, so no CAPI IRQ is needed.
 */
#define UART_IDENTIFIER		0U
#define UART_OPS		&max_capi_uart_ops
#define UART_BAUDRATE		115200
#define UART_EXTRA_TYPE		struct max_capi_uart_extra
#define UART_EXTRA_INIT		{ .use_irq = false }
#define PLATFORM_NAME		"MAX32657-TZ-NS"

#endif /* __PARAMETERS_H__ */
