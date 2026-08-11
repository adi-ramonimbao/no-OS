/***************************************************************************//**
 *   @file   uart_noos.c
 *   @brief  UART example using no-OS API for MAX32657
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

#include "maxim_irq.h"
#include "maxim_uart.h"
#include "maxim_uart_stdio.h"

int example_main(void)
{
	int ret;
	struct no_os_uart_desc *uart;
	struct max_uart_init_param extra_param = {
		.vssel = MXC_GPIO_VSSEL_VDDIOH,
	};
	struct no_os_uart_init_param param = {
		.extra = &extra_param,
		.baud_rate = 115200,
		.size = NO_OS_UART_CS_8,
		.parity = NO_OS_UART_PAR_NO,
		.stop = NO_OS_UART_STOP_1_BIT,
		.platform_ops = &max_uart_ops,
	};

	uint8_t i;

	ret = no_os_uart_init(&uart, &param);
	if (ret)
		return ret;

	ret = no_os_uart_write(uart, "Hello\n\r", 7);
	if (ret < 0)
		return ret;

	no_os_uart_stdio(uart);
	printf("Hello stdio!\n\r");

	no_os_mdelay(100);

	for (i = 0; i < 10; i++)
	{
		printf("Hello stdio! %d\n\r", i);

		no_os_mdelay(100);
	}

	// no_os_uart_remove(uart);

	// Send whatever we type to the serial monitor
	char buf;
	while (1)
	{
		ret = no_os_uart_read(uart, &buf, 1);
		if (ret < 0)
			return ret;

		ret = no_os_uart_write(uart, &buf, 1);
		if (ret < 0)
			return ret;
	}

	return 0;
}
