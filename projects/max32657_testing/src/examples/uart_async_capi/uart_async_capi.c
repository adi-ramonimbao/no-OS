/***************************************************************************//**
 *   @file   uart_async_capi.c
 *   @brief  Async UART example using CAPI for MAX32657
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

#include "no_os_delay.h"
#include "capi/capi_irq.h"
#include "capi/capi_uart.h"
#include "maxim_capi_uart.h"
#include "maxim_capi_irq.h"

int example_main(void)
{
	int ret;
	uint8_t i;

	/* UART config */
	struct max_capi_uart_extra uart_capi_extra_config = {
		.vssel = MXC_GPIO_VSSEL_VDDIOH,
		.use_irq = true,
	};
	struct capi_uart_line_config uart_capi_line_config = {
		.baudrate = 115200,
		.size = CAPI_UART_DATA_BITS_8,
		.parity = CAPI_UART_PARITY_NONE,
		.stop_bits = CAPI_UART_STOP_1_BIT,
	};
	struct capi_uart_config uart_capi_config = {
		.identifier = 0, /*UART0 */
		.line_config = &uart_capi_line_config,
		.ops = &max_capi_uart_ops,
		.extra = &uart_capi_extra_config,
	};
	struct capi_uart_handle *uart_capi;
	/* The UART driver connects its own IRQ when use_irq is set */
	struct capi_irq_config irq_config = {
		.irq_ctrl_id = 0,
	};

	ret = capi_irq_init(&irq_config);
	if (ret)
		return ret;

	ret = capi_uart_init(&uart_capi, &uart_capi_config);
	if (ret)
		return ret;

	ret = capi_uart_transmit(uart_capi, "Hello CAPI\n\r", 12);
	if (ret < 0)
		return ret;

	max_capi_uart_stdio_enable(uart_capi);

	for (i = 0; i < 10; i++)
	{
		printf("Hello CAPI stdio! %d\n\r", i);

		no_os_mdelay(100);
	}

	// capi_uart_deinit(uart_capi);

	// Send whatever we type to the serial monitor
	char buf;
	while (1)
	{
		ret = capi_uart_receive(uart_capi, &buf, 1);
		if (ret < 0)
			return ret;

		ret = capi_uart_transmit(uart_capi, &buf, 1);
		if (ret < 0)
			return ret;
	}

	return 0;
}
