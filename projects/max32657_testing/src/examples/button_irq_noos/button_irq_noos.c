/***************************************************************************//**
 *   @file   button_irq_noos.c
 *   @brief  Button IRQ example using no-OS API for MAX32657
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
#include "maxim_irq.h"
#include "maxim_gpio_irq.h"
#include "maxim_uart.h"
#include "maxim_uart_stdio.h"
#include "no_os_alloc.h"
#include "no_os_error.h"
#include "max32657.h"

#define BUTTON_PORT 0
#define BUTTON_PIN 12
#define LED_PORT 0
#define LED_NUMBER 13

void button_callback(void *context)
{
	printf("Button callback called!\n\r");
}

int example_main(void)
{
	int ret;
	uint8_t led_state;

	/* UART setup */
	struct no_os_uart_desc *uart;
	struct no_os_uart_init_param param = {
		.baud_rate = 115200,
		.size = NO_OS_UART_CS_8,
		.parity = NO_OS_UART_PAR_NO,
		.stop = NO_OS_UART_STOP_1_BIT,
		.platform_ops = &max_uart_ops,
	};
	/* Button GPIO setup */
	struct no_os_gpio_desc *button_desc;
	struct max_gpio_init_param button_extra_param = {
		.vssel = MXC_GPIO_VSSEL_VDDIOH,
	};
	struct no_os_gpio_init_param button_init_param = {
		.extra = &button_extra_param,
		.platform_ops = &max_gpio_ops,
		.port = BUTTON_PORT,
		.number = BUTTON_PIN,
		.pull = NO_OS_PULL_UP_WEAK,
	};
	/* LED GPIO setup */
	struct no_os_gpio_desc *led_desc;
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
	/* GPIO IRQ setup */
	struct no_os_irq_ctrl_desc *gpio_irq;
	struct no_os_irq_init_param gpio_irq_init_param = {
		.platform_ops = &max_gpio_irq_ops,
		.irq_ctrl_id = 0,
	};
	/* General NVIC IRQ setup */
	struct no_os_irq_ctrl_desc *nvic_irq;
	struct no_os_irq_init_param nvic_irq_init_param = {
		.platform_ops = &max_irq_ops,
		.irq_ctrl_id = 0,
	};
	struct no_os_callback_desc irq_button_cb = {
		.callback = button_callback,
		.event = NO_OS_EVT_GPIO,
		.peripheral = NO_OS_GPIO_IRQ,
		.handle = 0, /* GPIO0. But actually the MXC_GPIO_GET_IDX macro
				always returns 0 for MAX32657 */
	};

	ret = no_os_uart_init(&uart, &param);
	if (ret)
		return ret;

	no_os_uart_stdio(uart);

	/* Initialize NVIC IRQ controller */
	printf("Initializing NVIC IRQ controller\n\r");
	ret = no_os_irq_ctrl_init(&nvic_irq, &nvic_irq_init_param);
	if (ret)
		return ret;

	/* Initialize GPIO IRQ controller */
	printf("Initializing GPIO IRQ controller\n\r");
	ret = no_os_irq_ctrl_init(&gpio_irq, &gpio_irq_init_param);
	if (ret)
		goto free_nvic_irq;

	/* Initialize button GPIO */
	printf("Initializing button GPIO\n\r");
	ret = no_os_gpio_get(&button_desc, &button_init_param);
	if (ret)
		goto free_gpio_irq;

	printf("Setting button direction to INPUT\n\r");
	ret = no_os_gpio_direction_input(button_desc);
	if (ret)
		goto free_button;

	/* Initialize LED GPIO */
	printf("Initializing LED\n\r");
	ret = no_os_gpio_get(&led_desc, &led_init_param);
	if (ret)
		goto free_button;
	ret = no_os_gpio_direction_output(led_desc, NO_OS_GPIO_LOW);
	if (ret)
		goto free_led;

	/* Register GPIO interrupt callback */
	printf("Registering callback to button\n\r");
	ret = no_os_irq_register_callback(gpio_irq, BUTTON_PIN, &irq_button_cb);
	if (ret)
		goto free_led;

	/* Set trigger level for GPIO interrupt */
	printf("Setting trigger to falling edge\n\r");
	ret = no_os_irq_trigger_level_set(gpio_irq, BUTTON_PIN,
					  NO_OS_IRQ_EDGE_FALLING);
	if (ret)
		goto free_led;

	/* Enable GPIO peripheral interrupt for this pin */
	printf("Enabling GPIO IRQ for pin %d\n\r", BUTTON_PIN);
	ret = no_os_irq_enable(gpio_irq, BUTTON_PIN);
	if (ret)
		goto free_led;

	/* Enable NVIC interrupt for GPIO0 */
	printf("Enabling NVIC IRQ for GPIO0\n\r");
	ret = no_os_irq_enable(nvic_irq, GPIO0_IRQn);
	if (ret)
		goto free_led;

	while (1)
	{
		/* Blink an LED forever while we waiting for the interrupt to trigger*/
		ret = no_os_gpio_get_value(led_desc, &led_state);
		if (ret)
			return ret;
		led_state = !led_state;
		ret = no_os_gpio_set_value(led_desc,
					   led_state ? NO_OS_GPIO_HIGH : NO_OS_GPIO_LOW);
		if (ret)
			return ret;

		no_os_mdelay(1000);
	}

	return 0;

free_led:
	no_os_gpio_remove(led_desc);
free_button:
	no_os_gpio_remove(button_desc);
free_gpio_irq:
	no_os_irq_ctrl_remove(gpio_irq);
free_nvic_irq:
	no_os_irq_ctrl_remove(nvic_irq);
	return ret;
}
