/*
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file   parameters.h
 * @brief  Secure-world platform parameters for the MAX32657 TZ producer demo.
 *
 * Only the console UART is configured here: the Secure world prints a banner
 * and then hands the UART (and the other delegated peripherals) to whatever
 * Non-Secure image is combined with this Secure deliverable.
 */

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

/*
 * Non-Secure flash origin (default TrustZone split). Used to sanity-check that a
 * Non-Secure image has actually been programmed before branching into it, so a
 * Secure-only deliverable flashed on its own does not fault on an erased region.
 */
#define NS_FLASH_ORIGIN		0x01080000U

#endif /* __PARAMETERS_H__ */
