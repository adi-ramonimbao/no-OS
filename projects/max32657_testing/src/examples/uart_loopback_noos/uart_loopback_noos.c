/***************************************************************************//**
 *   @file   uart_loopback_noos.c
 *   @brief  UART loopback example using no-OS API for MAX32657
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

#include "maxim_gpio.h"
#include "no_os_alloc.h"
#include "no_os_delay.h"
#include "no_os_gpio.h"
#include "maxim_irq.h"
#include "maxim_uart.h"
#include "maxim_uart_stdio.h"

#define LED_PORT 0
#define LED_NUMBER 13

int example_main(void)
{
	int ret;
	struct no_os_uart_desc *uart;
	struct max_uart_init_param uart_extra = {
	    .vssel = MXC_GPIO_VSSEL_VDDIOH,
	};
	struct no_os_uart_init_param param = {
	    .extra = &uart_extra,
	    .baud_rate = 115200,
	    .size = NO_OS_UART_CS_8,
	    .parity = NO_OS_UART_PAR_NO,
	    .stop = NO_OS_UART_STOP_1_BIT,
	    .platform_ops = &max_uart_ops,
	    .irq_id = UART_IRQn,
	    .device_id = 0,
	};
	struct no_os_gpio_desc *led_descriptor;
	struct max_gpio_init_param led_extra_param = {
	    .vssel = MXC_GPIO_VSSEL_VDDIOH,
	};

	struct no_os_gpio_init_param led_init_param = {
	    .extra = &led_extra_param,
	    .platform_ops = &max_gpio_ops,
	    .port = LED_PORT,
	    .number = LED_NUMBER,
	    .pull = NO_OS_PULL_NONE,
	};

	uint8_t i;

	ret = no_os_gpio_get(&led_descriptor, &led_init_param);
	if (ret)
		return ret;

	ret = no_os_gpio_direction_output(led_descriptor, NO_OS_GPIO_LOW);
	if (ret)
		return ret;

	ret = no_os_uart_init(&uart, &param);
	if (ret)
		return ret;

	no_os_mdelay(100);

	char src_buf[128] = "The quick brown fox jumps over the lazy dog.\n\r";
	char dst_buf[128];
	uint32_t data_len = strlen(src_buf);
	uint32_t bytes_received = 0;

	memset(dst_buf, 0, sizeof(dst_buf));

	/* Write test data */
	ret = no_os_uart_write(uart, (uint8_t *)src_buf, data_len);
	if (ret < 0)
	{
		no_os_gpio_set_value(led_descriptor, NO_OS_GPIO_HIGH);
		while (1)
			; /* Write failed - LED stays on */
	}

	/* Wait for transmission and loopback */
	no_os_mdelay(100);

	bytes_received = MXC_UART_GetRXFIFOAvailable(MXC_UART_GET_UART(0));

	ret = no_os_uart_read(uart, (uint8_t *)&dst_buf[i], bytes_received);


	/* Check if we got all bytes */
	if (bytes_received != data_len)
	{
		/* Not all bytes received - turn LED ON */
		no_os_gpio_set_value(led_descriptor, NO_OS_GPIO_HIGH);
		while (1);
	}

	/* Compare data */
	for (i = 0; i < data_len; i++)
	{
		if (src_buf[i] != dst_buf[i])
		{
			/* Data mismatch - turn LED ON */
			no_os_gpio_set_value(led_descriptor, NO_OS_GPIO_HIGH);
			while (1);
		}
	}

	/* Test passed - blink LED rapidly */
	while (1)
	{
		no_os_gpio_set_value(led_descriptor, NO_OS_GPIO_HIGH);
		no_os_mdelay(100);
		no_os_gpio_set_value(led_descriptor, NO_OS_GPIO_LOW);
		no_os_mdelay(100);
	}

	return 0;
}
