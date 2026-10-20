/***************************************************************************//**
 *   @file   maxim_capi_usb_uart_priv.h
 *   @brief  Private definitions of the MAX32690 CAPI USB UART backend.
 *   @author Ramon Miguel Imbao (ramonmiguel.imbao@analog.com)
********************************************************************************
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
*******************************************************************************/
#ifndef __MAXIM_CAPI_USB_UART_PRIV_H__
#define __MAXIM_CAPI_USB_UART_PRIV_H__

#include <stdbool.h>
#include <stdint.h>
#include "maxim_capi_usb_uart.h"

#if defined(__cplusplus)
extern "C" {
#endif /* __cplusplus */

/**
 * @struct max_capi_usb_uart_priv
 * @brief Runtime state of the USB CDC-ACM UART
 */
struct max_capi_usb_uart_priv {
	/** User callback for async completion events */
	capi_uart_callback callback;
	/** Argument passed to the user callback */
	void *callback_arg;
	/** Destination buffer of the pending receive_async() */
	uint8_t *rx_buf;
	/** Requested length of the pending receive_async() */
	uint32_t rx_len;
	/** Bytes already stored into rx_buf */
	uint32_t rx_count;
	/** A receive_async() is pending */
	volatile bool async_rx_in_progress;
	/** Re-entrancy guard: acm_canread() can re-trigger ACM_CB_READ_READY */
	volatile bool rx_draining;
	/** The host sent a BREAK, reported by get_line_status() */
	volatile bool break_detected;
};

#define CAPI_USB_UART_HANDLE_MAXIM_INIT()			\
	(&(struct capi_uart_handle) {				\
		.ops = NULL,					\
		.init_allocated = false,			\
		.priv = &(struct max_capi_usb_uart_priv){0}	\
	})

#if defined(__cplusplus)
}
#endif /* __cplusplus */

#endif /* __MAXIM_CAPI_USB_UART_PRIV_H__ */
