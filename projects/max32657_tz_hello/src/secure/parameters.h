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
#include "maxim_capi_irq.h"
#include "capi_uart.h"
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
#define UART_EXTRA_INIT		{}
#define PLATFORM_NAME		"MAX32657"

#endif /* __PARAMETERS_H__ */
