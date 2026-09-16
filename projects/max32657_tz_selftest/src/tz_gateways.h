/*
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file   tz_gateways.h
 * @brief  Shared contract for the Secure-world gateways (__ns_entry veneers).
 *
 * Included by both worlds so the Secure definitions and the Non-Secure test
 * caller agree on the gateway signatures and the value-return constant. The
 * Non-Secure side resolves the symbols at link time from secure_implib.o.
 */

#ifndef __TZ_GATEWAYS_H__
#define __TZ_GATEWAYS_H__

/** Secure-owned value returned by GetSecureMagic_S(). */
#define TZ_SECURE_MAGIC		0x5A32657C

/*
 * The Secure translation unit defines these with the __ns_entry (cmse
 * nonsecure_entry) attribute; it defines TZ_GATEWAYS_SECURE_IMPL before this
 * include so it does not also see the plain, attribute-less prototypes (which
 * would clash with the attributed definitions). Non-Secure callers get the
 * plain prototypes and resolve the symbols from secure_implib.o at link time.
 */
#ifndef TZ_GATEWAYS_SECURE_IMPL

/**
 * @brief Increment a Non-Secure counter from the Secure world.
 * @param count_ns - Pointer (in Non-Secure memory) to the counter to advance.
 * @return 0 on success, -EINVAL if the pointer fails the Secure CMSE check.
 */
int IncrementCount_S(volatile int *count_ns);

/**
 * @brief Return a Secure-owned constant, exercising the S -> NS return path.
 * @return TZ_SECURE_MAGIC.
 */
int GetSecureMagic_S(void);

#endif /* TZ_GATEWAYS_SECURE_IMPL */

#endif /* __TZ_GATEWAYS_H__ */
