/*
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file   main.c
 * @brief  Secure-world producer for the MAX32657 TrustZone split demo.
 *
 * This is the minimal "set up the Secure side and hand off" Secure world for the
 * producer/consumer split: it is built on its own (Secure-only) and shipped as
 * separate deliverables (Secure HEX + import library + contract). A Non-Secure
 * image built elsewhere links the gateways below against the import library and
 * is merged with this Secure HEX before deployment.
 *
 * The Secure world:
 *   1. brings up the console UART through CAPI and prints a banner,
 *   2. exposes the flash code region as Non-Secure Callable so the gateway
 *      veneers are reachable from the Non-Secure world,
 *   3. hands GPIO0 / GCR / UART to the Non-Secure world via the SPC,
 *   4. branches into the Non-Secure image with NonSecure_Init() -- but only if a
 *      Non-Secure image has actually been programmed, so this Secure-only image
 *      can be flashed standalone without faulting on an erased region.
 *
 * Two secure gateways (__ns_entry) are exported for the Non-Secure world:
 *   IncrementCount_S() - increments a caller-supplied Non-Secure counter, after
 *                        validating the pointer with the CMSE intrinsic, and
 *   GetSecureMagic_S() - returns a Secure-owned constant (Secure -> Non-Secure
 *                        return path).
 * Both are recorded in the emitted import library (<name>_implib.o).
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

/* Secure-owned constant returned across the boundary (contract with the NS app). */
#define SECURE_MAGIC		0x5A5A5A5AU

/* Secure gateway called from the Non-Secure world. __ns_entry expands to
 * __attribute((cmse_nonsecure_entry)); the linker emits an SG veneer for it in
 * the Non-Secure Callable region and records it in the import library. */
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

/* Secure gateway returning a Secure-owned constant (no pointer to validate). */
__ns_entry uint32_t GetSecureMagic_S(void)
{
	return SECURE_MAGIC;
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

/* True if a Non-Secure image looks programmed at NS_FLASH_ORIGIN: the reset
 * vector (initial PC, word 1 of the vector table) is not erased flash. The
 * Secure world can read Non-Secure flash, so this is safe before the handover. */
static bool nonsecure_image_present(void)
{
	const volatile uint32_t *ns_vectors = (const volatile uint32_t *)NS_FLASH_ORIGIN;

	return ns_vectors[1] != 0xFFFFFFFFU;
}

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

	printf("\n\r**** MAX32657 TrustZone Secure producer (no-OS CAPI) ****\n\r");
	printf("Currently in the Secure world.\n\r");

	if (!nonsecure_image_present()) {
		/* Standalone Secure-only image: no Non-Secure world to enter. This
		 * is expected until a Non-Secure image is merged/flashed alongside. */
		printf("No Non-Secure image programmed; staying in the Secure world.\n\r");
		while (1)
			;
	}

	printf("Beginning transition to the Non-Secure world.\n\r");

	/* Let the UART finish transmitting before handing it to the Non-Secure
	 * world (poll the CAPI TX-complete status; no IRQ used). */
	do {
		ret = capi_uart_irq_tx_complete(uart, &tx_complete);
	} while (!ret && !tx_complete);

	/* Expose the flash code region as Non-Secure Callable so the gateway
	 * veneers can be called from Non-Secure code. */
	MXC_SPC_SetCode_NSC(true);

	/* Delegate the peripherals the Non-Secure app needs. Anything left Secure
	 * that the Non-Secure app touches will (correctly) SecureFault. */
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
