/*
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file   tz_gateways.h
 * @brief  Shared contract for the Secure-world keystore gateways (__ns_entry).
 *
 * Included by both worlds so the Secure definitions and the Non-Secure caller
 * agree on the gateway signatures and the known-answer test (KAT) vector. The
 * Non-Secure side resolves the symbols at link time from secure_implib.o.
 *
 * The protected asset - the key - lives ONLY in the Secure translation unit
 * (secure/main.c). It is never declared here, so the Non-Secure world has no
 * way to name it. What the Non-Secure world does know is the KAT vector below:
 * a fixed plaintext and the ciphertext the Secure key is expected to produce.
 * The KAT passing proves the Secure world holds the right key WITHOUT the key
 * ever crossing the boundary - that is the whole point of the demo.
 *
 * NOTE: the cipher is a repeating-key XOR. It is deliberately trivial and is
 * NOT real cryptography - do not reuse it. Here it only stands in for whatever
 * real operation (AES, ECDSA sign, ...) a production Secure world would run
 * behind the same boundary. The security feature on show is the boundary, not
 * the cipher.
 */

#ifndef __TZ_GATEWAYS_H__
#define __TZ_GATEWAYS_H__

#include <stddef.h>
#include <stdint.h>

/** Length of the known-answer test vector, in bytes. */
#define TZ_KS_LEN		9U

/** KAT plaintext ("TrustZone"). */
#define TZ_KS_PLAINTEXT		{ 0x54, 0x72, 0x75, 0x73, 0x74, 0x5A, 0x6F, 0x6E, 0x65 }

/**
 * KAT ciphertext = plaintext XOR the (Secure-only) key. Precomputed against the
 * key in secure/main.c; regenerate both together if either changes. The
 * Non-Secure world checks the gateway output against this without ever holding
 * the key that produces it.
 */
#define TZ_KS_CIPHERTEXT	{ 0xF1, 0x28, 0x49, 0xB0, 0xD1, 0x00, 0x53, 0xAD, 0xC0 }

/*
 * The Secure translation unit defines the gateways with the __ns_entry (cmse
 * nonsecure_entry) attribute; it defines TZ_GATEWAYS_SECURE_IMPL before this
 * include so it does not also see the plain, attribute-less prototypes (which
 * would clash with the attributed definitions). Non-Secure callers get the
 * plain prototypes and resolve the symbols from secure_implib.o at link time.
 */
#ifndef TZ_GATEWAYS_SECURE_IMPL

/**
 * @brief  Transform a Non-Secure buffer in place with the Secure-held key.
 *
 * XOR is involutive, so calling twice restores the plaintext: "decrypt" is the
 * same call as "encrypt". The buffer is validated with a CMSE range check
 * before it is touched, so a pointer into Secure memory (or any address the
 * Non-Secure caller cannot itself access) is rejected instead of dereferenced.
 *
 * @param buf_ns - Pointer (in Non-Secure memory) to the buffer to transform.
 * @param len    - Length of the buffer in bytes.
 * @return 0 on success, -EINVAL if the buffer fails the Secure CMSE check.
 */
int KeystoreTransform_S(uint8_t *buf_ns, size_t len);

/**
 * @brief  Return how many security faults the Secure world has caught so far.
 *
 * The Secure SecureFault handler increments an internal counter each time it
 * catches (and recovers from) a Non-Secure access to Secure memory. The
 * Non-Secure demo reads this to confirm its deliberate illegal read really was
 * trapped by the hardware and handled by the Secure world.
 *
 * @return Running count of caught security faults.
 */
uint32_t KeystoreFaultCount_S(void);

#endif /* TZ_GATEWAYS_SECURE_IMPL */

#endif /* __TZ_GATEWAYS_H__ */
