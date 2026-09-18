/*
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file   main.c
 * @brief  Secure-world producer for the TrustZone split demo (STM32 platform).
 *
 * The Secure world:
 *   1. calls stm32_init() (CubeMX bring-up: MX_GTZC_S_Init() delegates USART3
 *      and the LED pin to Non-Secure per the .ioc's per-peripheral/per-pin
 *      security context; no manual HAL_GTZC_ or HAL_GPIO_ConfigPinAttributes
 *      calls needed here),
 *   2. enables the SecureFault trap so a Non-Secure access to Secure memory is
 *      recovered instead of escalating,
 *   3. branches into the Non-Secure image, but only if one has actually been
 *      programmed, so this Secure-only image can be flashed standalone
 *      without faulting on an erased region.
 *
 * Unlike Maxim, USART3 is statically Non-Secure from boot (CubeMX's per-
 * peripheral GTZC context, not a runtime SPC handoff), and the Secure build is
 * never given the HAL UART driver source to compile, so there is no Secure
 * banner here.
 *
 * A Secure-owned secret key backs three __ns_entry-equivalent gateways
 * exported for the Non-Secure world:
 *   KeystoreTransform_S()  - XOR a validated Non-Secure buffer with the Secure
 *                            key; a pointer into Secure memory is rejected by a
 *                            CMSE range check, never dereferenced,
 *   KeystoreSelfTest_S()   - verify the key against a Secure-held known-answer
 *                            inside the Secure world and return only pass/fail,
 *   KeystoreFaultCount_S() - report how many Non-Secure accesses to Secure
 *                            memory the SecureFault handler has trapped.
 * KeystoreTransform_S()/KeystoreSelfTest_S() are defined in
 * src/examples/keystore/keystore_secure.c, reused unchanged from any
 * platform's Secure world. KeystoreFaultCount_S() stays here because it
 * shares state with SecureFault_Handler() below.
 *
 * CubeMX generates its own do-nothing SecureFault_Handler() into
 * Secure/Core/Src/stm32h5xx_it.c; stm32_trustzone.cmake patches that
 * definition to __attribute__((weak)) so this strong override here replaces
 * it (see _no_os_stm32_tz_patch_main()'s doc comment).
 */

#include <stdint.h>
#include <stdbool.h>

#include "stm32_hal.h"

#include "parameters.h"

/* Count of Non-Secure security violations trapped by SecureFault_Handler(). */
static volatile uint32_t secure_fault_count;

/* Secure gateway: report the running count of trapped security faults. */
__attribute__((cmse_nonsecure_entry)) uint32_t KeystoreFaultCount_S(void)
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

/* True if a Non-Secure image looks programmed at NS_FLASH_ORIGIN: the reset
 * vector (initial PC, word 1 of the vector table) is not erased flash. The
 * Secure world can read Non-Secure flash, so this is safe before the handover. */
static bool nonsecure_image_present(void)
{
	const volatile uint32_t *ns_vectors = (const volatile uint32_t *)NS_FLASH_ORIGIN;

	return ns_vectors[1] != 0xFFFFFFFFU;
}

typedef void (*funcptr_ns)(void) __attribute__((cmse_nonsecure_call));

extern int stm32_init(void);

/* Standard ARMv8-M Secure->Non-Secure handoff (see trustzone_hello's Secure
 * main.c for the full explanation); CubeMX's MX_GTZC_S_Init() already
 * delegated the peripherals the Non-Secure world needs before this runs. */
static void jump_to_nonsecure(void)
{
	funcptr_ns reset_handler_ns;

	SCB_NS->VTOR = NS_FLASH_ORIGIN;
	__TZ_set_MSP_NS(*(uint32_t *)NS_FLASH_ORIGIN);
	reset_handler_ns = (funcptr_ns)(*(uint32_t *)(NS_FLASH_ORIGIN + 4U));
	reset_handler_ns();
}

int main(void)
{
	stm32_init();

	/* Trap Non-Secure accesses to Secure memory here instead of letting them
	 * escalate to a Secure HardFault. SecureFault_Handler() recovers so the
	 * Non-Secure app can report the blocked access and carry on. */
	SCB->SHCSR |= SCB_SHCSR_SECUREFAULTENA_Msk;

	if (!nonsecure_image_present()) {
		/* Standalone Secure-only image: no Non-Secure world to enter, and
		 * no UART in this build to report it (see the file header comment). */
		while (1)
			;
	}

	jump_to_nonsecure();

	/* Should never return; nothing to report through (no UART driver in
	 * this build). */
	while (1)
		;
}
