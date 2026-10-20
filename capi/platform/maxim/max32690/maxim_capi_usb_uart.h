/***************************************************************************//**
 *   @file   maxim_capi_usb_uart.h
 *   @brief  CAPI UART backend over the MAX32690 USB CDC-ACM device class.
 *   @author Ramon Miguel Imbao (ramonmiguel.imbao@analog.com)
********************************************************************************
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
*******************************************************************************/
#ifndef __MAXIM_CAPI_USB_UART_H__
#define __MAXIM_CAPI_USB_UART_H__

#include <stdint.h>
#include "capi_uart.h"

#if defined(__cplusplus)
extern "C" {
#endif /* __cplusplus */

/** Default USB IDs, used when no extra configuration is supplied */
#define MAX_CAPI_USB_UART_DEFAULT_VID	0x0B6AU
#define MAX_CAPI_USB_UART_DEFAULT_PID	0x003CU

/**
 * @struct max_capi_usb_uart_extra
 * @brief Platform-specific configuration, passed through capi_uart_config.extra
 *
 * The USB interrupt is always connected through CAPI IRQ (capi_irq_init()
 * must have been called), as the USB stack is entirely interrupt driven.
 */
struct max_capi_usb_uart_extra {
	/** USB vendor ID reported to the host */
	uint16_t vid;
	/** USB product ID reported to the host */
	uint16_t pid;
};

/**
 * @brief CAPI UART operations backed by USB CDC-ACM.
 *
 * Select with capi_uart_config.ops = &max_capi_usb_uart_ops. The MAX32690 has
 * a single USB device controller, so capi_uart_config.identifier is ignored.
 * Line settings are owned by the USB host: capi_uart_config.line_config is
 * ignored,
 * capi_uart_get_line_config() reports what the host requested and
 * capi_uart_set_line_config() returns -ENOTSUP.
 *
 * RX_DONE is delivered from the USB interrupt. Re-arming receive_async() from
 * the callback is fine, but avoid large blocking transmits there: queued data
 * can only drain from that same interrupt. Like the MSDK class driver it
 * wraps, a blocking transmit can still stall if the host closes the port
 * while the CDC-ACM write FIFO is full; unplug and bus reset are detected.
 */
extern const struct capi_uart_ops max_capi_usb_uart_ops;

/**
 * @brief Route stdio (printf/scanf) through the USB CDC-ACM UART.
 * @param handle - Handle returned by capi_uart_init() for this backend.
 * @return 0 on success, negative error code otherwise.
 */
int max_capi_usb_uart_stdio_enable(struct capi_uart_handle *handle);

#if defined(__cplusplus)
}
#endif /* __cplusplus */

#endif /* __MAXIM_CAPI_USB_UART_H__ */
