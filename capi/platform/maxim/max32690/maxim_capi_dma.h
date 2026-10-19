/*******************************************************************************
 *   @file   maxim_capi_dma.h
 *   @brief  Header file for DMA functions with CAPI
 *   @author Edelweise Escala (edelweise.escala@analog.com)
********************************************************************************
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
*******************************************************************************/

#ifndef MAXIM_CAPI_DMA_H_
#define MAXIM_CAPI_DMA_H_

#include "dma.h"
#include "capi_dma.h"
#include "capi_alloc.h"

#if defined(__cplusplus)
extern "C" {
#endif /* __cplusplus */

/**
 * @enum max_capi_dma_request
 * @brief DMA transfer request type
 *
 * MAX32690 routes every peripheral instance to its own request line, unlike
 * single-instance parts where one SPI/UART entry suffices.
 */
enum max_capi_dma_request {
	MAX_CAPI_DMA_REQUEST_MEMTOMEM = MXC_DMA_REQUEST_MEMTOMEM,

	MAX_CAPI_DMA_REQUEST_SPI0_RX = MXC_DMA_REQUEST_SPI0RX,
	MAX_CAPI_DMA_REQUEST_SPI1_RX = MXC_DMA_REQUEST_SPI1RX,
	MAX_CAPI_DMA_REQUEST_SPI2_RX = MXC_DMA_REQUEST_SPI2RX,
	MAX_CAPI_DMA_REQUEST_SPI3_RX = MXC_DMA_REQUEST_SPI3RX,
	MAX_CAPI_DMA_REQUEST_SPI4_RX = MXC_DMA_REQUEST_SPI4RX,
	MAX_CAPI_DMA_REQUEST_SPI0_TX = MXC_DMA_REQUEST_SPI0TX,
	MAX_CAPI_DMA_REQUEST_SPI1_TX = MXC_DMA_REQUEST_SPI1TX,
	MAX_CAPI_DMA_REQUEST_SPI2_TX = MXC_DMA_REQUEST_SPI2TX,
	MAX_CAPI_DMA_REQUEST_SPI3_TX = MXC_DMA_REQUEST_SPI3TX,
	MAX_CAPI_DMA_REQUEST_SPI4_TX = MXC_DMA_REQUEST_SPI4TX,

	MAX_CAPI_DMA_REQUEST_UART0_RX = MXC_DMA_REQUEST_UART0RX,
	MAX_CAPI_DMA_REQUEST_UART1_RX = MXC_DMA_REQUEST_UART1RX,
	MAX_CAPI_DMA_REQUEST_UART2_RX = MXC_DMA_REQUEST_UART2RX,
	MAX_CAPI_DMA_REQUEST_UART3_RX = MXC_DMA_REQUEST_UART3RX,
	MAX_CAPI_DMA_REQUEST_UART0_TX = MXC_DMA_REQUEST_UART0TX,
	MAX_CAPI_DMA_REQUEST_UART1_TX = MXC_DMA_REQUEST_UART1TX,
	MAX_CAPI_DMA_REQUEST_UART2_TX = MXC_DMA_REQUEST_UART2TX,
	MAX_CAPI_DMA_REQUEST_UART3_TX = MXC_DMA_REQUEST_UART3TX,

	MAX_CAPI_DMA_REQUEST_I2C0_RX = MXC_DMA_REQUEST_I2C0RX,
	MAX_CAPI_DMA_REQUEST_I2C1_RX = MXC_DMA_REQUEST_I2C1RX,
	MAX_CAPI_DMA_REQUEST_I2C2_RX = MXC_DMA_REQUEST_I2C2RX,
	MAX_CAPI_DMA_REQUEST_I2C0_TX = MXC_DMA_REQUEST_I2C0TX,
	MAX_CAPI_DMA_REQUEST_I2C1_TX = MXC_DMA_REQUEST_I2C1TX,
	MAX_CAPI_DMA_REQUEST_I2C2_TX = MXC_DMA_REQUEST_I2C2TX,

	MAX_CAPI_DMA_REQUEST_ADC = MXC_DMA_REQUEST_ADC,
	MAX_CAPI_DMA_REQUEST_I2S_RX = MXC_DMA_REQUEST_I2SRX,
	MAX_CAPI_DMA_REQUEST_I2S_TX = MXC_DMA_REQUEST_I2STX,
	MAX_CAPI_DMA_REQUEST_CAN0_RX = MXC_DMA_REQUEST_CAN0RX,
	MAX_CAPI_DMA_REQUEST_CAN1_RX = MXC_DMA_REQUEST_CAN1RX,
	MAX_CAPI_DMA_REQUEST_CAN0_TX = MXC_DMA_REQUEST_CAN0TX,
	MAX_CAPI_DMA_REQUEST_CAN1_TX = MXC_DMA_REQUEST_CAN1TX,
};

/**
 * @struct max_capi_dma_xfer_extra
 * @brief MAX32690 platform-specific DMA extra configuration
 */
struct max_capi_dma_xfer_extra {
	/** DMA request type */
	enum max_capi_dma_request reqsel;
};

/**
 * @struct max_capi_dma_extra
 * @brief MAX32690 platform-specific DMA controller configuration
 */
struct max_capi_dma_extra {
	/** Whether to enable IRQ connection during init */
	bool use_irq;
};

extern const struct capi_dma_ops max_capi_dma_ops;

#if defined(__cplusplus)
}
#endif /* __cplusplus */

#endif /* MAXIM_CAPI_DMA_H_ */
