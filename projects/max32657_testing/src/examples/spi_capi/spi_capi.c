/***************************************************************************//**
 *   @file   spi_capi.c
 *   @brief  SPI loopback example using CAPI for MAX32657
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
#include "dma.h"
#include "capi_dma.h"
#include "capi_irq.h"
#include "capi_spi.h"
#include "capi_uart.h"
#include "maxim_capi_dma.h"
#include "maxim_capi_irq.h"
#include "maxim_capi_spi.h"
#include "maxim_capi_uart.h"
#include "maxim_capi_gpio.h"

void spi_irq_callback(enum capi_async_event event, void *arg, int event_extra)
{
	switch (event) {
	case CAPI_SPI_EVENT_XFR_DONE:
		/* Do whatever... */
		break;
	default:
		break;
	}
}

bool verify_data(uint8_t *data_a, uint8_t *data_b, uint16_t len)
{
	bool has_error = false;
	int i;

	// printf("Verifying data\n\r");
	for (i = 0; i < len; i++) {
		if (data_a[i] != data_b[i]) {
			has_error = true;
			break;
		}
	}

	return has_error;

	// if (has_error)
	// 	printf("ERROR: Data mismatch!\n\r");
	// else
	// 	printf("Transfer complete\n\n\r");
}

int example_main(void)
{
	int ret, i;
	uint8_t tx_data[16] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
			        0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
	uint8_t rx_data[16];
	bool has_error = false;

	/* UART setup */
	struct capi_uart_handle *uart_handle = NULL;
	struct max_capi_uart_extra uart_extra = {
		.vssel = MAX_CAPI_GPIO_VSSEL_VDDIOH,
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

	/* Data buffers setup */
	printf("Setting up data buffers\n\r");
	memset(rx_data, 0, 16);

	/* IRQ setup */
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
	};
	ret = capi_dma_init(&dma_handle, &dma_config);
	if (ret)
		return ret;

	/* SPI setup */
	printf("Setting up SPI\n\r");
	struct capi_spi_controller_handle *spi_handle = NULL;
	struct max_capi_spi_delays spi_delays = {
		.cs_delay_first = 0,
		.cs_delay_last = 0,
	};
	struct max_capi_spi_extra spi_extra = {
		.device_role = MAX_CAPI_SPI_DEVICE_ROLE_CONTROLLER,
		.bus_width = MAX_CAPI_SPI_BUS_WIDTH_STANDARD,
		.num_targets = 0,
		.polarity_mask = 0x000,
		.chip_select = MAX_CAPI_SPI_CS0,
		.vssel = MAX_CAPI_GPIO_VSSEL_VDDIOH,
		.clock_phase = MAX_CAPI_SPI_CLOCK_PHASE_0,
		.clock_polarity = MAX_CAPI_SPI_CLOCK_POLARITY_0,
		.platform_delays = spi_delays,
		.dma_config = &dma_config,
	};
	const struct capi_spi_config spi_config = {
		.identifier = 0,
		.extra = &spi_extra,
		.clk_freq_hz = 400000,
		.ops = &max_capi_spi_ops,
		// .dma_handle = dma_handle,
	};
	ret = capi_spi_init(&spi_handle, &spi_config);
	if (ret)
		return ret;

	struct capi_spi_device spi_device0 = {
		.controller = spi_handle,
		.non_continuous_mode = false,
		.native_cs = MAX_CAPI_SPI_CS1,
		.max_speed_hz = 100000,
	};
	struct capi_spi_device spi_device1 = {
		.controller = spi_handle,
		.non_continuous_mode = false,
		.native_cs = MAX_CAPI_SPI_CS1,
	};

	ret = capi_spi_register_callback(spi_handle, spi_irq_callback, &spi_device0);
	if (ret)
		return ret;

	printf("Starting SPI transfer\n\r");
	struct capi_spi_transfer spi_transfer = {
		.tx_buf = tx_data,
		.tx_size = 16,
		.rx_buf = rx_data,
		.rx_size = 16,
		// .timeout = 0,
	};
	ret = capi_spi_transceive(&spi_device0, &spi_transfer);
	if (ret)
		return ret;
	if (!verify_data(tx_data, rx_data, 16))
		printf("Transfer complete\n\r");
	else
		printf("ERROR: Data mismatched!\n\r");

	ret = capi_spi_transceive(&spi_device1, &spi_transfer);
	if (ret)
		return ret;
	if (!verify_data(tx_data, rx_data, 16))
		printf("Transfer complete\n\r");
	else
		printf("ERROR: Data mismatched!\n\r");

	ret = capi_spi_transceive(&spi_device0, &spi_transfer);
	if (ret)
		return ret;
	if (!verify_data(tx_data, rx_data, 16))
		printf("Transfer complete\n\r");
	else
		printf("ERROR: Data mismatched!\n\r");

	ret = capi_spi_transceive(&spi_device0, &spi_transfer);
	if (ret)
		return ret;
	if (!verify_data(tx_data, rx_data, 16))
		printf("Transfer complete\n\r");
	else
		printf("ERROR: Data mismatched!\n\r");

	ret = capi_spi_transceive(&spi_device1, &spi_transfer);
	if (ret)
		return ret;
	if (!verify_data(tx_data, rx_data, 16))
		printf("Transfer complete\n\r");
	else
		printf("ERROR: Data mismatched!\n\r");

	ret = capi_spi_transceive(&spi_device0, &spi_transfer);
	if (ret)
		return ret;
	if (!verify_data(tx_data, rx_data, 16))
		printf("Transfer complete\n\r");
	else
		printf("ERROR: Data mismatched!\n\r");

	printf("Deinitializing SPI...\n\r");
	ret = capi_spi_deinit(spi_handle);
	if (ret)
		return ret;

	printf("Deinitializing UART...\n\r");
	ret = capi_uart_deinit(uart_handle);
	if (ret)
		return ret;

	while (1);

	return 0;
}
