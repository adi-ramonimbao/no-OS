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
#include "dma.h"
#include "capi_dma.h"
#include "capi_irq.h"
#include "capi_i2c.h"
#include "capi_uart.h"
#include "maxim_capi_gpio.h"
#include "maxim_capi_dma.h"
#include "maxim_capi_irq.h"
#include "maxim_capi_i2c.h"
#include "maxim_capi_uart.h"

int example_main(void)
{
	int ret;
	uint8_t tx_data[1] = {0xAA};

	/* UART setup */
	struct capi_uart_handle *uart_handle = NULL;
	struct max_capi_uart_extra uart_extra = {
		.vssel = MXC_GPIO_VSSEL_VDDIOH,
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

	/* I2C setup */
	printf("Setting up I2C\n\r");
	struct capi_i2c_controller_handle *i2c_handle = NULL;
	struct max_capi_i2c_extra i2c_extra = {
		.vssel = MAX_CAPI_GPIO_VSSEL_VDDIOH,
	};
	const struct capi_i2c_config i2c_config = {
		.identifier = 0,
		.extra = &i2c_extra,
		.ops = &max_capi_i2c_ops,
		.initiator = true,
		.clk_freq_hz = MAX_CAPI_I2C_SPEED_FAST,
	};
	ret = capi_i2c_init(&i2c_handle, &i2c_config);
	if (ret)
		return ret;

	struct capi_i2c_device i2c_device0 = {
		.controller = i2c_handle,
		.address = 0x12,
	};

	struct capi_i2c_device i2c_device1 = {
		.controller = i2c_handle,
		.address = 0x34,
	};

	printf("Starting I2C transfer\n\r");
	struct capi_i2c_transfer i2c_transfer0 = {
		.buf = tx_data,
		.len = 1,
		.no_stop = false,
	};
	struct capi_i2c_transfer i2c_transfer1 = {
		.buf = tx_data,
		.len = 1,
		.no_stop = false,
		.target_addr = 0x56,
	};

	capi_i2c_transmit(&i2c_device0, &i2c_transfer0);
	capi_i2c_configure_bus_speed(i2c_handle, CAPI_I2C_SPEED_STANDARD, 50);
	capi_i2c_transmit(&i2c_device1, &i2c_transfer0);
	capi_i2c_transmit(&i2c_device1, &i2c_transfer0);
	capi_i2c_transmit(&i2c_device1, &i2c_transfer1);
	capi_i2c_configure_bus_speed(i2c_handle, CAPI_I2C_SPEED_FAST, 50);
	capi_i2c_transmit(&i2c_device0, &i2c_transfer1);

	printf("Deinitializing I2C...\n\r");
	ret = capi_i2c_deinit(i2c_handle);
	if (ret)
		return ret;

	printf("Deinitializing UART...\n\r");
	ret = capi_uart_deinit(uart_handle);
	if (ret)
		return ret;

	while (1);

	return 0;
}
