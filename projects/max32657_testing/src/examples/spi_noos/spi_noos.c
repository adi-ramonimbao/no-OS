/***************************************************************************//**
 *   @file   spi_noos.c
 *   @brief  SPI loopback example using no-OS API for MAX32657
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

#include "dma.h"
#include "no_os_alloc.h"
#include "no_os_dma.h"
#include "no_os_error.h"
#include "maxim_dma.h"
#include "maxim_spi.h"
#include "maxim_uart.h"
#include "maxim_uart_stdio.h"

int example_main(void)
{
	uint8_t tx_data[16] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
			       0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
	uint8_t rx_data[16];
	int ret, i;
	bool has_error = false;

	/* UART setup */
	struct no_os_uart_desc *uart = NULL;
	struct max_uart_init_param uart_extra = {
	    .vssel = MXC_GPIO_VSSEL_VDDIOH,
	};
	struct no_os_uart_init_param uart_param = {
	    .baud_rate = 115200,
	    .size = NO_OS_UART_CS_8,
	    .parity = NO_OS_UART_PAR_NO,
	    .stop = NO_OS_UART_STOP_1_BIT,
	    .platform_ops = &max_uart_ops,
	    .extra = &uart_extra,
	};

	ret = no_os_uart_init(&uart, &uart_param);
	if (ret)
		return ret;
	no_os_uart_stdio(uart);

	/* Data buffers setup */
	printf("Setting up data buffers\n\r");
	memset(rx_data, 0, 16);

	/* SPI setup */
	printf("Setting up SPI\n\r");
	struct no_os_spi_desc *spi = NULL;
	struct max_spi_init_param spi_extra = {
		.num_slaves = 1,
		.vssel = MXC_GPIO_VSSEL_VDDIOH,
		.polarity = SPI_SS_POL_HIGH,
	};
	struct no_os_spi_init_param spi_param = {
		.extra = &spi_extra,
		.device_id = 0,
		.platform_ops = &max_spi_ops,
		.mode = NO_OS_SPI_MODE_0,
		.bit_order = NO_OS_SPI_BIT_ORDER_MSB_FIRST,
		.lanes = NO_OS_SPI_SINGLE_LANE,
		.max_speed_hz = 400000, /* 1 MHz */
		.parent = &spi,
	};
	ret = no_os_spi_init(&spi, &spi_param);
	if (ret)
		return ret;

	printf("Starting SPI transfer\n\r");
	struct no_os_spi_msg messages[] = {
		{
			.bytes_number = 16,
			.rx_buff = rx_data,
			.tx_buff = tx_data,
		},
	};
	ret = no_os_spi_transfer(spi, &messages, 1);
	if (ret)
		return ret;

	printf("Verifying data\n\r");
	for (i = 0; i < 16; i++) {
		if (tx_data[i] != rx_data[i]) {
			has_error = true;
			break;
		}
	}

	if (has_error)
		printf("ERROR: Data mismatch!\n\r");
	else
		printf("Transfer complete\n\r");

	no_os_spi_remove(spi);

	return 0;
}
