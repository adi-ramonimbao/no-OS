/***************************************************************************//**
 * @file   main.c
 * @brief  Secure-world entry for the MAX32657 TrustZone hello demo.
 *
 * Ported from msdk/Examples/MAX32657/Hello_World_TZ/Secure/main.c into the
 * no-OS CAPI structure. The Secure world:
 *   1. brings up the console UART through CAPI and prints a banner,
 *   2. exposes the flash code region as Non-Secure Callable so the
 *      IncrementCount_S() veneer is reachable from the Non-Secure world,
 *   3. hands GPIO0 / GCR / UART to the Non-Secure world via the SPC,
 *   4. branches into the Non-Secure image with NonSecure_Init().
 *
 * IncrementCount_S() is the secure gateway (__ns_entry) the Non-Secure app
 * calls back into; it validates the caller-supplied pointer with the CMSE
 * intrinsic before dereferencing it, so a malicious Non-Secure pointer cannot
 * be used to reach Secure memory.
 *
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *******************************************************************************/

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

#include "mxc.h"
#include "spc.h"
#include "system_max32657.h"

#include "capi_uart.h"
#include "capi_irq.h"
#include "maxim_capi_uart.h"

#include "parameters.h"

static struct capi_irq_config irq_config = {
	.irq_ctrl_id = IRQ_CTRL_IDENTIFIER,
	.extra = IRQ_CTRL_EXTRA,
};

/* Secure gateway called from the Non-Secure world. __ns_entry expands to
 * __attribute((cmse_nonsecure_entry)); the linker emits an SG veneer for it in
 * the Non-Secure Callable region and records it in secure_implib.o. */
__ns_entry int IncrementCount_S(volatile int *count_ns)
{
	/* Validate the Non-Secure pointer before dereferencing: on a failed
	 * check cmse_check_pointed_object() returns NULL, so a Non-Secure caller
	 * cannot trick Secure code into touching Secure memory. */
	count_ns = cmse_check_pointed_object((int *)count_ns, CMSE_NONSECURE);
	if (count_ns == NULL)
		return -EINVAL;

	(*count_ns)++;

	return 0;
}

static struct capi_uart_line_config uart_line_config = {
	.baudrate = UART_BAUDRATE,
	.size = CAPI_UART_DATA_BITS_8,
	.parity = CAPI_UART_PARITY_NONE,
	.stop_bits = CAPI_UART_STOP_1_BIT,
};

static UART_EXTRA_TYPE uart_extra = UART_EXTRA_INIT;

static const struct capi_uart_config uart_config = {
	.line_config = &uart_line_config,
	.extra = &uart_extra,
	.ops = UART_OPS,
};

int main(void)
{
	struct capi_uart_handle *uart = NULL;
	bool tx_complete = false;
	int ret;

	/* The UART is IRQ-driven, so bring up the CAPI IRQ controller first. */
	ret = capi_irq_init(&irq_config);
	if (ret)
		return ret;
	capi_irq_global_enable();

	ret = capi_uart_init(&uart, &uart_config);
	if (ret)
		return ret;

	/* Route printf/stdio through the CAPI UART for the Secure banner. */
	max_capi_uart_stdio_enable(uart);

	printf("\n\r**** MAX32657 Hello World with TrustZone (no-OS CAPI) ****\n\r");
	printf("Currently in the Secure world.\n\r");
	printf("Beginning transition to the Non-Secure world.\n\r");

	/* Let the UART finish transmitting before handing it to the Non-Secure
	 * world. CAPI equivalent of polling the hardware TX-busy flag:
	 * irq_tx_complete reports true once the TX FIFO is empty and the shift
	 * register is idle. */
	do {
		ret = capi_uart_irq_tx_complete(uart, &tx_complete);
	} while (!ret && !tx_complete);

	/* Expose the flash code region as Non-Secure Callable so the
	 * IncrementCount_S() veneer can be called from Non-Secure code. */
	MXC_SPC_SetCode_NSC(true);

	/* Hand the peripherals the Non-Secure app needs over to it. These are no
	 * longer accessible from the Secure world afterwards. */
	MXC_SPC_SetNonSecure(MXC_SPC_PERIPH_GPIO0);
	MXC_SPC_SetNonSecure(MXC_SPC_PERIPH_GCR);
	MXC_SPC_SetNonSecure(MXC_SPC_PERIPH_UART);

	/* Branch into the Non-Secure image (weak impl in system_max32657.c). */
	ret = NonSecure_Init();

	/* Should never return. Reclaim the UART only to report the failure. */
	MXC_SPC_SetSecure(MXC_SPC_PERIPH_UART);
	printf("[Error] Non-Secure transition failed. Error Code: %d\n\r", ret);

	while (1)
		;
}
