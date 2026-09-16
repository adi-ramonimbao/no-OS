/*
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file   parameters.h
 * @brief  Non-Secure-world platform parameters for the MAX32657 TZ self-test.
 *
 * The Non-Secure world drives the console UART (test report transport) and
 * DMA0 (memory-to-memory tests). Both are reached after the Secure world hands
 * them over. This header supplies the console UART and DMA definitions the
 * shared capi_selftest common_data / test_dma expect; the peripheral groups
 * whose OPS are left undefined (GPIO/SPI/I2C/timer/async-UART) compile out.
 */

#ifndef __PARAMETERS_H__
#define __PARAMETERS_H__

#include "maxim_capi_uart.h"
#include "maxim_capi_dma.h"
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

/*
 * DMA0 on MAX32657 is hardwired Non-Secure, so the Non-Secure world reaches it
 * with no SPC handover. The Maxim CAPI DMA backend selects the DMA0_NS instance
 * automatically for a Non-Secure build (CONFIG_TRUSTED_EXECUTION_SECURE == 0).
 * Memory-to-memory transfers need no external wiring.
 */
#define DMA_OPS			&max_capi_dma_ops
#define DMA_IDENTIFIER		0U
#define DMA_NUM_CHANS		4U
#define DMA_XFER_EXTRA_TYPE	struct max_capi_dma_xfer_extra
#define DMA_XFER_EXTRA_INIT	{ .reqsel = MAX_CAPI_DMA_REQUEST_MEMTOMEM }
#define DMA_PLATFORM_INIT()
#define DMA_XFER_SIZE		64U

#endif /* __PARAMETERS_H__ */
