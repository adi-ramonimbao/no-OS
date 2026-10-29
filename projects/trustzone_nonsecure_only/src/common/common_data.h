/*
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file   common_data.h
 * @brief  Common data header for the MAX32657 TrustZone split demo consumer.
 *
 * Declares the CAPI config structs shared by the Non-Secure example(s); the
 * struct definitions live in common_data.c, built from parameters.h macros.
 */

#ifndef __COMMON_DATA_H__
#define __COMMON_DATA_H__

#include "capi_uart.h"
#include "capi_gpio.h"

/** @brief Console UART configuration for the Non-Secure world. */
extern const struct capi_uart_config uart_config;
/** @brief GPIO port configuration for the delegated LED. */
extern const struct capi_gpio_port_config led_port_config;

#endif /* __COMMON_DATA_H__ */
