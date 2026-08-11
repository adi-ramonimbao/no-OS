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
#include "maxim_i2c.h"
#include "maxim_uart.h"
#include "maxim_uart_stdio.h"

int example_main(void)
{
	uint8_t tx_data[1] = {0xAA};
	int ret;

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

	/* I2C setup */
	printf("Setting up I2C\n\r");
	struct no_os_i2c_desc *i2c = NULL;
	struct max_i2c_init_param i2c_extra = {
		.vssel = MXC_GPIO_VSSEL_VDDIOH,
	};
	struct no_os_i2c_init_param i2c_param = {
		.extra = &i2c_extra,
		.device_id = 0,
		.platform_ops = &max_i2c_ops,
		.max_speed_hz = MAX_I2C_FAST_PLUS_MODE,
		.slave_address = 0x55, /* So the address is 0b01010101 */
	};
	ret = no_os_i2c_init(&i2c, &i2c_param);
	if (ret)
		return ret;

	while (1) {
		no_os_i2c_write(i2c, tx_data, 1, 1);
	}

	return 0;
}
