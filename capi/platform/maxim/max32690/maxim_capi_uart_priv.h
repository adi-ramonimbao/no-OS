/*******************************************************************************
 *   @file   maxim_capi_uart_priv.h
 *   @brief  Header file for the UART private handle
 *   @author Edelweise Escala (edelweise.escala@analog.com)
********************************************************************************
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
*******************************************************************************/

#ifndef MAXIM_CAPI_UART_PRIV_H_
#define MAXIM_CAPI_UART_PRIV_H_

#include "maxim_capi_uart.h"

#if defined(__cplusplus)
extern "C" {
#endif /* __cplusplus */

/**
 * @struct max_capi_uart_priv
 * @brief Private structure for UART
 */
struct max_capi_uart_priv {
	/** Identifier */
	uint32_t id;
	/** UART registers */
	mxc_uart_regs_t *uart;
	/** Clock source storage */
	mxc_uart_clock_t clk_src;
	/** Line config storage */
	struct capi_uart_line_config line_config;
	/** DMA handle */
	struct capi_dma_handle *dma_handle;
	/** Async callback */
	capi_uart_callback callback;
	/** Callback arg */
	void *callback_arg;
	/**
	 * UART request storage for async TX/RX. The MSDK tracks TX and RX as
	 * independent concurrent slots per instance (AsyncTxRequests[]/
	 * AsyncRxRequests[] in uart_revb.c) and keeps a pointer to whichever
	 * request object was passed in, so a transfer in one direction must
	 * not reuse the other direction's still-live request storage.
	 */
	mxc_uart_req_t async_tx_req;
	/** UART request storage for async RX; see async_tx_req. */
	mxc_uart_req_t async_rx_req;
	/**
	 * DMA transfer storage for async TX/RX. Kept per-direction for the
	 * same reason as async_tx_req/async_rx_req: TX and RX DMA channels
	 * can run concurrently.
	 */
	struct capi_dma_transfer dma_xfer_tx;
	/** DMA transfer storage for async RX; see dma_xfer_tx. */
	struct capi_dma_transfer dma_xfer_rx;
	/** DMA transfer extra struct storage for async TX; see dma_xfer_tx. */
	struct max_capi_dma_xfer_extra dma_xfer_extra_tx;
	/** DMA transfer extra struct storage for async RX; see dma_xfer_tx. */
	struct max_capi_dma_xfer_extra dma_xfer_extra_rx;
	/**
	 * Async TX/RX in-progress flags, tracked independently: the MSDK
	 * itself allows one TX and one RX transfer concurrently per instance,
	 * so blocking a transmit_async() while only a receive_async() is
	 * outstanding (or vice versa) would reject a legitimate full-duplex
	 * call.
	 */
	volatile bool async_tx_in_progress;
	/** Async RX in-progress flag; see async_tx_in_progress. */
	volatile bool async_rx_in_progress;
	/** DMA completion flag */
	volatile bool dma_completed;
	/** TX DMA channel */
	struct capi_dma_chan *dma_channel_tx;
	/** RX DMA channel */
	struct capi_dma_chan *dma_channel_rx;
	/** Whether to enable IRQ connection on init */
	bool use_irq;
};

#define CAPI_UART_HANDLE_MAXIM_INIT()			\
	(&(struct capi_uart_handle) {			\
		.ops = NULL,				\
		.init_allocated = false,		\
		.priv = &(struct max_capi_uart_priv){0}	\
	})

#if defined(__cplusplus)
}
#endif /* __cplusplus */

#endif /* MAXIM_CAPI_UART_PRIV_H_ */
