/*
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file   main.c
 * @brief  Secure-world entry for the MAX32657 TrustZone keystore demo.
 *
 * The Secure world owns a secret key and exposes it only as an operation, never
 * as data. It:
 *   1. brings up the console UART through CAPI and prints a banner,
 *   2. enables the SecureFault exception so a Non-Secure access to Secure memory
 *      is trapped here (and recovered from) instead of escalating,
 *   3. exposes the flash code region as Non-Secure Callable so the keystore
 *      gateway veneers are reachable from the Non-Secure world,
 *   4. hands GPIO0 / GCR / UART to the Non-Secure world via the SPC,
 *   5. branches into the Non-Secure image with NonSecure_Init().
 *
 * Two gateways (__ns_entry) back the Non-Secure demo:
 *
 *   KeystoreTransform_S()  - XOR a Non-Secure buffer with the Secure-held key,
 *                            after validating the buffer with a CMSE range
 *                            check. The key never leaves the Secure world; a
 *                            pointer into Secure memory is rejected, never
 *                            dereferenced.
 *   KeystoreFaultCount_S() - report how many security faults the Secure world
 *                            has caught, so the Non-Secure demo can confirm its
 *                            deliberate illegal read really was trapped.
 *
 * The cipher is a repeating-key XOR: deliberately trivial, NOT real crypto (see
 * tz_gateways.h). It stands in for whatever real operation a production Secure
 * world would run behind the same boundary. The security feature is the
 * boundary, not the cipher.
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

/* This TU defines the gateways with __ns_entry; suppress the plain prototypes. */
#define TZ_GATEWAYS_SECURE_IMPL
#include "tz_gateways.h"

/*
 * The protected asset. It lives in Secure memory and no gateway ever returns
 * it: the Non-Secure world can drive the transform but can never read these
 * bytes. TZ_KS_CIPHERTEXT in tz_gateways.h is TZ_KS_PLAINTEXT XOR this key;
 * regenerate that vector if you change the key here.
 */
static const uint8_t secret_key[] = { 0xA5, 0x5A, 0x3C, 0xC3 };

/* Count of Non-Secure security violations trapped by SecureFault_Handler(). */
static volatile uint32_t secure_fault_count;

/* Secure gateway: transform a Non-Secure buffer with the Secure-held key.
 * __ns_entry expands to __attribute((cmse_nonsecure_entry)); the linker emits
 * an SG veneer for it in the Non-Secure Callable region and records it in
 * secure_implib.o. */
__ns_entry int KeystoreTransform_S(uint8_t *buf_ns, size_t len)
{
	/* Validate the whole Non-Secure buffer before touching it: on a failed
	 * check cmse_check_address_range() returns NULL, so a Non-Secure caller
	 * cannot trick the Secure key into reading or writing Secure memory. */
	buf_ns = cmse_check_address_range(buf_ns, len, CMSE_NONSECURE);
	if (buf_ns == NULL)
		return -EINVAL;

	for (size_t i = 0U; i < len; i++)
		buf_ns[i] ^= secret_key[i % sizeof(secret_key)];

	return 0;
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
 * which is always taken in the Secure world. Rather than hang, this handler
 * records the violation and performs a controlled recovery: it rewrites the
 * stacked return PC to the stacked LR, so the exception return resumes as if
 * the faulting Non-Secure function had simply returned to its caller. The
 * offending access never completes and no Secure data is exposed.
 *
 * This is a demo convenience so the self-test can continue and report the
 * blocked access. A production Secure world would more likely log and reset.
 */
void SecureFault_Handler(void)
{
	uint32_t *ns_frame;

	/* The faulting Non-Secure context was stacked on the active Non-Secure
	 * stack. In thread mode that is PSP_NS if CONTROL_NS.SPSEL is set, else
	 * MSP_NS. no-OS bare metal runs the Non-Secure app on MSP, but decode it
	 * so the handler is correct either way. */
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
	 * PC <- LR: return from the offending Non-Secure function to its caller.
	 * The provoking function is naked (no prologue/epilogue), so the
	 * Non-Secure stack is already balanced for this return. */
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

	printf("\n\r**** MAX32657 TrustZone keystore demo (Secure world) ****\n\r");
	printf("Secret key held Secure; Non-Secure world drives it via gateways.\n\r");

	/* Trap Non-Secure accesses to Secure memory here instead of letting them
	 * escalate to a Secure HardFault. SecureFault_Handler() recovers so the
	 * Non-Secure demo can report the blocked access and carry on. */
	SCB->SHCSR |= SCB_SHCSR_SECUREFAULTENA_Msk;

	/* Let the UART finish transmitting before handing it to the Non-Secure
	 * world. irq_tx_complete reports true once the TX FIFO is empty and the
	 * shift register is idle. */
	do {
		ret = capi_uart_irq_tx_complete(uart, &tx_complete);
	} while (!ret && !tx_complete);

	/* Expose the flash code region as Non-Secure Callable so the gateway
	 * veneers can be called from Non-Secure code. */
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
