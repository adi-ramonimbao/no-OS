/***************************************************************************//**
 *   @file   button_irq_capi.c
 *   @brief  Button IRQ example using CAPI for MAX32657
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

#include "capi_time.h"
#include "capi_gpio.h"
#include "capi_irq.h"
#include "capi_uart.h"
#include "maxim_capi_gpio.h"
#include "maxim_capi_irq.h"
#include "maxim_capi_uart.h"

#define BUTTON_PORT 0
#define BUTTON_PIN 12

#define LED_PORT 0
#define LED_PIN 13

static uint32_t count = 0;

void button_callback(void *arg)
{
	count++;
	printf("Button callback called! %d\r\n", count);
}

int example_main(void)
{
	int ret;
	uint8_t led_state;

	/* UART config */
	struct capi_uart_handle *uart = NULL;
	struct max_capi_uart_extra uart_extra_config = {
		.vssel = MXC_GPIO_VSSEL_VDDIOH,
	};
	struct capi_uart_line_config uart_line_config = {
		.baudrate = 115200,
		.size = CAPI_UART_DATA_BITS_8,
		.parity = CAPI_UART_PARITY_NONE,
		.stop_bits = CAPI_UART_STOP_1_BIT,
	};
	struct capi_uart_config uart_capi_config = {
		.line_config = &uart_line_config,
		.ops = &max_capi_uart_ops,
		.extra = &uart_extra_config,
	};
	/* Button GPIO config */
	struct capi_gpio_port_handle *gpio_port = NULL;
	struct max_capi_gpio_extra_config gpio_extra_config = {
		.pad = MXC_GPIO_PAD_WEAK_PULL_UP,
		.vssel = MXC_GPIO_VSSEL_VDDIOH,
		.drvstr = MXC_GPIO_DRVSTR_0,
	};
	struct capi_gpio_port_config gpio_port_config = {
		.identifier = 0,
		.flags = NULL,
		.ops = &max_capi_gpio_ops,
		.extra = &gpio_extra_config,
		.num_pins = 32,
	};
	/* NVIC IRQ config */
	struct capi_irq_config irq_config = {
		.irq_ctrl_id = 0,
	};

	/* Initialize UART */
	ret = capi_uart_init(&uart, &uart_capi_config);
	if (ret)
		return ret;
	max_capi_uart_stdio_enable(uart);

	/* Initialize button GPIO */
	printf("Initializing button GPIO\r\n");
	ret = capi_gpio_port_init(&gpio_port, &gpio_port_config);
	if (ret)
		return ret;

	/* Initialize button and LED after GPIO port is initialized */
	struct capi_gpio_pin button_pin = {
		.port_handle = gpio_port,
		.number = BUTTON_PIN,
		.flags = CAPI_GPIO_ACTIVE_LOW,
	};

	/* Initialize LED GPIO */
	struct capi_gpio_pin led_pin = {
		.port_handle = gpio_port,
		.number = LED_PIN,
		.flags = CAPI_GPIO_ACTIVE_HIGH,
	};

	printf("Setting button direction to INPUT\r\n");
	ret = capi_gpio_pin_set_direction(&button_pin, CAPI_GPIO_INPUT);
	if (ret)
		return ret;

	printf("Setting LED direction to OUTPUT\r\n");
	ret = capi_gpio_pin_set_direction(&led_pin, CAPI_GPIO_OUTPUT);
	if (ret)
		return ret;

	/* Initialize IRQ */
	ret = capi_irq_init(&irq_config);
	if (ret && ret != -EBUSY)
		return ret;

	/* Initialize GPIO0 IRQ */
	ret = capi_irq_enable(MXC_GPIO_GET_IRQ(0));
	if (ret)
		return ret;

	/* Connect the callback to the GPIO0 button pin */
	ret = max_capi_gpio_irq_connect(&button_pin, button_callback, NULL);
	if (ret)
		return ret;

	/* Enable the interrupt on the button pin */
	ret = max_capi_gpio_irq_enable(&button_pin);
	if (ret)
		return ret;

	/* Set the edge trigger for the button pin */
	ret = max_capi_gpio_irq_set_level_edge_trigger(&button_pin,
						       CAPI_IRQ_EDGE_FALLING);
	if (ret)
		return ret;

	/* Blink forever while waiting for the interrupt from the button */
	while (1)
	{
		ret = capi_gpio_pin_get_value(&led_pin, &led_state);
		if (ret)
			return ret;

		led_state = !led_state;

		ret = capi_gpio_pin_set_value(&led_pin,
				led_state ? CAPI_GPIO_HIGH : CAPI_GPIO_LOW);
		if (ret)
			return ret;

		capi_wait_ms(100);

		if (count >= 25)
			break;
	}

	printf("Deinitializing IRQ...\r\n");
	ret = capi_irq_deinit();
	if (ret)
		return ret;

	printf("Deinitializing the GPIO port...\r\n");
	ret = capi_gpio_port_deinit(&gpio_port);
	if (ret)
		return ret;

	printf("Deinitializing UART...\r\n");
	ret =  capi_uart_deinit(uart);
	if (ret)
		return ret;

	while (1);

	return 0;
}
