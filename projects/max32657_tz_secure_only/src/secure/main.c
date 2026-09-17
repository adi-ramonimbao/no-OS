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
 * A Secure-owned secret key backs three __ns_entry gateways exported for the
 * Non-Secure world, so the Non-Secure app can use the key but never read it:
 *   KeystoreTransform_S()  - XOR a validated Non-Secure buffer with the Secure
 *                            key; a pointer into Secure memory is rejected by a
 *                            CMSE range check, never dereferenced,
 *   KeystoreSelfTest_S()   - verify the key against a Secure-held known-answer
 *                            inside the Secure world and return only pass/fail,
 *                            so the Non-Secure app needs no reference answer, and
 *   KeystoreFaultCount_S() - report how many Non-Secure accesses to Secure
 *                            memory the SecureFault handler has trapped.
 * All are recorded in the emitted import library (<name>_implib.o). The cipher
 * is a trivial repeating-key XOR standing in for a real Secure operation; the
 * security feature on show is the boundary, not the cipher.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <errno.h>

#include "mxc.h"
#include "spc.h"
#include "system_max32657.h"

#include "capi_uart.h"
#include "maxim_capi_uart.h"

#include "parameters.h"

/*
 * The protected asset. It lives in Secure memory and no gateway ever returns
 * it: the Non-Secure world can drive the transform but can never read these
 * bytes. KeystoreSelfTest_S() checks the key against a Secure-held known-answer,
 * so the Non-Secure consumer needs no reference answer of its own.
 */
static const uint8_t secret_key[] = { 0xA5, 0x5A, 0x3C, 0xC3 };

/* Count of Non-Secure security violations trapped by SecureFault_Handler(). */
static volatile uint32_t secure_fault_count;

/* Apply the Secure-held key in place (repeating-key XOR). File-local so the
 * transform gateway and the self-test perform the identical operation. */
static void keystore_apply_key(uint8_t *buf, size_t len)
{
	for (size_t i = 0U; i < len; i++)
		buf[i] ^= secret_key[i % sizeof(secret_key)];
}

/* Secure gateway: transform a Non-Secure buffer with the Secure-held key.
 * __ns_entry expands to __attribute((cmse_nonsecure_entry)); the linker emits
 * an SG veneer for it in the Non-Secure Callable region and records it in the
 * import library. */
__ns_entry int KeystoreTransform_S(uint8_t *buf_ns, size_t len)
{
	/* Validate the whole Non-Secure buffer before touching it: on a failed
	 * check cmse_check_address_range() returns NULL, so a Non-Secure caller
	 * cannot trick the Secure key into reading or writing Secure memory. */
	buf_ns = cmse_check_address_range(buf_ns, len, CMSE_NONSECURE);
	if (buf_ns == NULL)
		return -EINVAL;

	keystore_apply_key(buf_ns, len);

	return 0;
}

/* Secure gateway: verify the provisioned key against a known-answer entirely
 * inside the Secure world. The plaintext and expected ciphertext never leave
 * Secure memory - only the boolean result crosses out - so the Non-Secure app
 * can confirm the key is correct without holding any reference answer itself.
 * Returns 0 on match, -1 on mismatch. */
__ns_entry int KeystoreSelfTest_S(void)
{
	static const uint8_t kat_plaintext[] = {
		0x54, 0x72, 0x75, 0x73, 0x74, 0x5A, 0x6F, 0x6E, 0x65
	};
	static const uint8_t kat_ciphertext[] = {
		0xF1, 0x28, 0x49, 0xB0, 0xD1, 0x00, 0x53, 0xAD, 0xC0
	};
	uint8_t buf[sizeof(kat_plaintext)];
	uint8_t diff = 0U;
	size_t i;

	for (i = 0U; i < sizeof(buf); i++)
		buf[i] = kat_plaintext[i];

	keystore_apply_key(buf, sizeof(buf));

	for (i = 0U; i < sizeof(buf); i++)
		diff |= buf[i] ^ kat_ciphertext[i];

	return diff ? -1 : 0;
}

/* Secure gateway: report the running count of trapped security faults. */
__ns_entry uint32_t KeystoreFaultCount_S(void)
{
	return secure_fault_count;
}

/**
 * @brief Catch and recover from a Non-Secure access to Secure memory.
 *
 * A Non-Secure load/store to a Secure-attributed address raises a SecureFault,
 * always taken in the Secure world. Rather than hang, this handler records the
 * violation and rewrites the stacked return PC to the stacked LR, so exception
 * return resumes as if the faulting Non-Secure function had simply returned to
 * its caller. The offending access never completes and no Secure data leaks.
 * A production Secure world would more likely log and reset.
 */
void SecureFault_Handler(void)
{
	uint32_t *ns_frame;

	/* The faulting Non-Secure context was stacked on the active Non-Secure
	 * stack (PSP_NS if CONTROL_NS.SPSEL is set, else MSP_NS). */
	if (__TZ_get_CONTROL_NS() & 0x2U)
		ns_frame = (uint32_t *)__TZ_get_PSP_NS();
	else
		ns_frame = (uint32_t *)__TZ_get_MSP_NS();

	secure_fault_count++;

	/* Clear the sticky Secure Fault Status bits (write-1-to-clear) so we do
	 * not immediately re-enter on exception return. */
	SCB->SFSR = SCB->SFSR;

	/* Exception stack frame (Armv8-M, no FP context):
	 * [0]=R0 [1]=R1 [2]=R2 [3]=R3 [4]=R12 [5]=LR [6]=PC [7]=xPSR.
	 * PC <- LR: return from the offending Non-Secure function to its caller. */
	ns_frame[6] = ns_frame[5];
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
	printf("Secret key held Secure; Non-Secure world drives it via gateways.\n\r");

	/* Trap Non-Secure accesses to Secure memory here instead of letting them
	 * escalate to a Secure HardFault. SecureFault_Handler() recovers so the
	 * Non-Secure app can report the blocked access and carry on. */
	SCB->SHCSR |= SCB_SHCSR_SECUREFAULTENA_Msk;

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
