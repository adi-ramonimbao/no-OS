/*
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file   selftest_secure.c
 * @brief  Portable Secure-world gateways for the TrustZone CAPI self-test.
 *
 * Nothing here touches a platform register, so this file needs no platform
 * header and is reused unchanged once a second (e.g. STM32) Secure world
 * exists. Two secure gateways back the Non-Secure TRUSTZONE test group:
 *
 *   IncrementCount_S()  - validates the caller-supplied pointer with the CMSE
 *                         intrinsic before dereferencing it, then increments the
 *                         Non-Secure counter. A NULL or Secure-memory pointer
 *                         fails the check and returns -EINVAL, so a hostile
 *                         Non-Secure pointer can never reach Secure memory.
 *   GetSecureMagic_S()  - returns a Secure-owned constant, exercising the
 *                         Secure -> Non-Secure return path (no pointer involved).
 */

#include <stdint.h>
#include <errno.h>
#include <arm_cmse.h>

/* This TU defines the gateways with the attribute; suppress the plain
 * prototypes so they do not clash with the attributed definitions below. */
#define TZ_GATEWAYS_SECURE_IMPL
#include "tz_gateways.h"

/* Secure gateway called from the Non-Secure world. cmse_nonsecure_entry makes
 * the linker emit an SG veneer for it in the Non-Secure Callable region and
 * record it in secure_implib.o. */
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

/* Secure gateway that returns a Secure-owned value. No pointer crosses the
 * boundary, so this exercises the plain Secure -> Non-Secure return path. */
__attribute__((cmse_nonsecure_entry)) int GetSecureMagic_S(void)
{
	return TZ_SECURE_MAGIC;
}
