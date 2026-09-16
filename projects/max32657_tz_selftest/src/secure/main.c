/*
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file   main.c
 * @brief  Secure-world entry for the MAX32657 TrustZone CAPI self-test.
 *
 * The Secure world:
 *   1. brings up the console UART through CAPI and prints a banner,
 *   2. exposes the flash code region as Non-Secure Callable so the secure
 *      gateway veneers are reachable from the Non-Secure world,
 *   3. hands GPIO0 / GCR / UART to the Non-Secure world via the SPC,
 *   4. branches into the Non-Secure image with NonSecure_Init().
 *
 * Two secure gateways (__ns_entry) back the Non-Secure TRUSTZONE test group:
 *
 *   IncrementCount_S()  - validates the caller-supplied pointer with the CMSE
 *                         intrinsic before dereferencing it, then increments the
 *                         Non-Secure counter. A NULL or Secure-memory pointer
 *                         fails the check and returns -EINVAL, so a hostile
 *                         Non-Secure pointer can never reach Secure memory.
 *   GetSecureMagic_S()  - returns a Secure-owned constant, exercising the
 *                         Secure -> Non-Secure return path (no pointer involved).
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

#include "mxc.h"
#include "spc.h"
#include "system_max32657.h"

#include "capi_uart.h"
#include "maxim_capi_uart.h"

#include "parameters.h"

/* This TU defines the gateways with __ns_entry; suppress the plain prototypes. */
#define TZ_GATEWAYS_SECURE_IMPL
#include "tz_gateways.h"

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

/* Secure gateway that returns a Secure-owned value. No pointer crosses the
 * boundary, so this exercises the plain Secure -> Non-Secure return path. */
__ns_entry int GetSecureMagic_S(void)
{
	return TZ_SECURE_MAGIC;
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

	ret = capi_uart_init(&uart, &uart_config);
	if (ret)
		return ret;

	/* Route printf/stdio through the CAPI UART for the Secure banner. */
	max_capi_uart_stdio_enable(uart);

	printf("\n\r**** MAX32657 TrustZone CAPI self-test (Secure world) ****\n\r");
	printf("Handing peripherals to the Non-Secure world; tests run there.\n\r");

	/* Let the UART finish transmitting before handing it to the Non-Secure
	 * world. CAPI equivalent of polling the hardware TX-busy flag:
	 * irq_tx_complete reports true once the TX FIFO is empty and the shift
	 * register is idle. */
	do {
		ret = capi_uart_irq_tx_complete(uart, &tx_complete);
	} while (!ret && !tx_complete);

	/* Expose the flash code region as Non-Secure Callable so the gateway
	 * veneers can be called from Non-Secure code. */
	MXC_SPC_SetCode_NSC(true);

	/* Hand the peripherals the Non-Secure app needs over to it. These are no
	 * longer accessible from the Secure world afterwards. DMA0 is hardwired
	 * Non-Secure and needs no SPC handover; it only needs its clock, which is
	 * reachable once the GCR is Non-Secure. */
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
