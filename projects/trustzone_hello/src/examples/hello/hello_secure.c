/*
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file   hello_secure.c
 * @brief  Portable Secure-world gateway for the TrustZone hello demo.
 *
 * Contains only the __ns_entry gateway definition; nothing here touches a
 * platform register, so it needs no platform header and is reused unchanged
 * once a second (e.g. STM32) Secure world exists.
 */

#include <stdint.h>
#include <errno.h>
#include <arm_cmse.h>

/* Secure gateway called from the Non-Secure world. cmse_nonsecure_entry makes
 * the linker emit an SG veneer for it in the Non-Secure Callable region and
 * record it in the emitted import library. */
__attribute__((cmse_nonsecure_entry)) int IncrementCount_S(volatile int *count_ns)
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
