/***************************************************************************//**
 *   @file   trng_capi.c
 *   @brief  TRNG example using CAPI for MAX32657
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

#include "capi_irq.h"
#include "capi_uart.h"
#include "capi_trng.h"
#include "maxim_capi_uart.h"
#include "maxim_capi_irq.h"
#include "maxim_capi_trng.h"

int example_main(void)
{
	int ret;
	uint32_t i;

	/* UART setup */
	struct capi_uart_handle *uart_handle = NULL;
	struct max_capi_uart_extra uart_capi_extra = {
		.vssel = MXC_GPIO_VSSEL_VDDIOH,
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
		.extra = &uart_capi_extra,
	};

	ret = capi_uart_init(&uart_handle, &uart_capi_config);
	if (ret)
		return ret;
	max_capi_uart_stdio_enable(uart_handle);

	/* TRNG setup */
	struct capi_trng_handle *trng_handle = NULL;
	struct capi_trng_config trng_config = {
		.identifier = 0,
		.ops = &max_capi_trng_ops,
	};
	ret = capi_trng_init(&trng_handle, &trng_config);
	if (ret)
		return ret;

	uint32_t value;

	printf("Generating 10 random 32-bit values...\n\r");
	for (i = 0; i < 10; i++) {
		ret = capi_trng_generate_u32(trng_handle, &value);
		if (ret)
			return ret;
		printf("%08x\n\r", value);
	}

	uint8_t buffer[256];
	printf("Filling a 256-byte buffer with random values...\n\r");
	ret = capi_trng_fill_buffer(trng_handle, buffer, 256);
	if (ret)
		return ret;

	for (i = 0; i < 256; i += 8) {
		printf("%02x %02x %02x %02x %02x %02x %02x %02x\n\r",
			buffer[i + 0], buffer[i + 1], buffer[i + 2], buffer[i + 3],
			buffer[i + 4], buffer[i + 5], buffer[i + 6], buffer[i + 7]);
	}

	printf("Deinitializing TRNG...\n\r");
	ret = capi_trng_deinit(trng_handle);
	if (ret)
		return ret;

	printf("Deinitializing UART...\n\r");
	ret = capi_uart_deinit(uart_handle);
	if (ret)
		return ret;

	while (1);

	return 0;
}
