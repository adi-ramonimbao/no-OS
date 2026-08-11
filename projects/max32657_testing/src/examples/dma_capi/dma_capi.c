/***************************************************************************//**
 *   @file   dma_capi.c
 *   @brief  DMA example using CAPI for MAX32657
 *   @author Ramon Miguel Imbao (ramonmiguel.imbao@analog.com)
********************************************************************************
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of Analog Devices, Inc. nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY ANALOG DEVICES, INC. “AS IS” AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL ANALOG DEVICES, INC. BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*******************************************************************************/

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "capi_dma.h"
#include "capi_irq.h"
#include "capi_uart.h"
#include "maxim_capi_gpio.h"
#include "maxim_capi_dma.h"
#include "maxim_capi_irq.h"
#include "maxim_capi_uart.h"

void dma_irq_callback(uint32_t event, void *ctx) {
	printf("Callback called!\n\r");
}

int example_main(void)
{
	int ret;
	uint32_t i;
	bool has_error = false;
	uint8_t dma_src_buffer[64];
	uint8_t dma_dst_buffer[64];

	/* UART setup */
	struct capi_uart_handle *uart_handle = NULL;
	struct max_capi_uart_extra uart_extra = {
		.vssel = MAX_CAPI_GPIO_VSSEL_VDDIO,
	};
	struct capi_uart_line_config uart_line_config = {
		.baudrate = 115200,
		.size = CAPI_UART_DATA_BITS_8,
		.parity = CAPI_UART_PARITY_NONE,
		.stop_bits = CAPI_UART_STOP_1_BIT,
	};
	struct capi_uart_config uart_config = {
		.identifier = 0, /* UART0 */
		.line_config = &uart_line_config,
		.ops = &max_capi_uart_ops,
		.extra = &uart_extra,
	};
	ret = capi_uart_init(&uart_handle, &uart_config);
	if (ret)
		return ret;
	max_capi_uart_stdio_enable(uart_handle);

	/* IRQ setup */
	struct capi_irq_handle *irq_handle = NULL;
	struct capi_irq_config irq_config = {
		.irq_ctrl_id = 0,
	};
	ret = capi_irq_init(&irq_config);
	if (ret && ret != -EBUSY)
		return ret;

	/* DMA setup */
	struct capi_dma_handle *dma_handle = NULL;
	struct capi_dma_config dma_config = {
		.id = 0,
		.ops = &max_capi_dma_ops,
		.irq_handle = irq_handle,
	};
	ret = capi_dma_init(&dma_handle, &dma_config);
	if (ret)
		return ret;

	/* DMA channel setup */
	struct capi_dma_chan *dma_ch_handle = NULL;
	ret = capi_dma_init_chan(dma_handle, &dma_ch_handle, 0);
	if (ret)
		return ret;

	/* DMA transfer setup */
	struct max_capi_dma_xfer_extra xfer_extra = {
		.reqsel = MAX_CAPI_DMA_REQUEST_MEMTOMEM,
	};
	struct capi_dma_transfer dma_transfer = {
		.src = (capi_dma_glbl_addr_t)dma_src_buffer,
		.dst = (capi_dma_glbl_addr_t)dma_dst_buffer,
		.src_inc = CAPI_DMA_BYTE_INCREMENT,
		.dst_inc = CAPI_DMA_BYTE_INCREMENT,
		.src_size = CAPI_DMA_XFER_SIZE_1_BYTE,
		.dst_size = CAPI_DMA_XFER_SIZE_1_BYTE,
		.length = sizeof(dma_src_buffer),
		.extra = &xfer_extra,
		.xfer_type = CAPI_DMA_MEM_TO_MEM,
	};
	ret = capi_dma_config_xfer(dma_ch_handle, &dma_transfer);
	if (ret)
		return ret;

	ret = capi_dma_register_complete_callback(dma_ch_handle,
						  dma_irq_callback, NULL);
	if (ret)
		return ret;

	printf("Setting up test buffers\n\r");
	/* Initialize source buffer with test pattern */
	for (i = 0; i < sizeof(dma_src_buffer); i++)
		dma_src_buffer[i] = (uint8_t)(i ^ 0xAA);

	/* Clear destination buffer */
	memset(dma_dst_buffer, 0, sizeof(dma_dst_buffer));

	printf("Starting DMA transfer\n\r");
	ret = capi_dma_xfer_start(dma_ch_handle);
	if (ret)
		return ret;

	printf("DMA started, waiting for completion...\n\r");
	while (!capi_dma_chan_is_completed(dma_ch_handle)) {}

	printf("DMA complete!\n\r");

	printf("Verifying data\n\r");
	for (i = 0; i < sizeof(dma_src_buffer); i++) {
		printf("[%d]: src = %d, dst = %d\n\r", i, dma_src_buffer[i], dma_dst_buffer[i]);
		if (dma_dst_buffer[i] != dma_src_buffer[i]) {
			has_error = true;
			break;
		}
	}

	if (has_error)
		printf("ERROR: src and dst are not the same!\n\r");
	else
		printf("DMA transfer complete\n\r");

	printf("Deinitializing DMA...\n\r");
	ret = capi_dma_deinit(dma_handle);
	if (ret)
		return ret;

	printf("Deinitializing UART...\n\r");
	ret = capi_uart_deinit(uart_handle);
	if (ret)
		return ret;

	while (1);

	return 0;
}
