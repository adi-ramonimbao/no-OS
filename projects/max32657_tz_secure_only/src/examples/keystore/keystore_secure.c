/*
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file   keystore_secure.c
 * @brief  Portable Secure-world keystore gateways for the TrustZone split demo.
 *
 * Nothing here touches a platform register, so this file needs no platform
 * header and is reused unchanged once a second (e.g. STM32) Secure world
 * exists. KeystoreFaultCount_S() stays out of this file: it shares state with
 * SecureFault_Handler(), which does depend on the vendor device header, so
 * both live together in platform/<platform>/secure/main.c.
 *
 * A Secure-owned secret key backs two __ns_entry gateways exported for the
 * Non-Secure world, so the Non-Secure app can use the key but never read it:
 *   KeystoreTransform_S() - XOR a validated Non-Secure buffer with the Secure
 *                           key; a pointer into Secure memory is rejected by a
 *                           CMSE range check, never dereferenced.
 *   KeystoreSelfTest_S()  - verify the key against a Secure-held known-answer
 *                           inside the Secure world and return only pass/fail,
 *                           so the Non-Secure app needs no reference answer.
 * Both are recorded in the emitted import library (<name>_implib.o). The
 * cipher is a trivial repeating-key XOR standing in for a real Secure
 * operation; the security feature on show is the boundary, not the cipher.
 */

#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <arm_cmse.h>

/*
 * The protected asset. It lives in Secure memory and no gateway ever returns
 * it: the Non-Secure world can drive the transform but can never read these
 * bytes. KeystoreSelfTest_S() checks the key against a Secure-held known-answer,
 * so the Non-Secure consumer needs no reference answer of its own.
 */
static const uint8_t secret_key[] = { 0xA5, 0x5A, 0x3C, 0xC3 };

/* Apply the Secure-held key in place (repeating-key XOR). File-local so the
 * transform gateway and the self-test perform the identical operation. */
static void keystore_apply_key(uint8_t *buf, size_t len)
{
	for (size_t i = 0U; i < len; i++)
		buf[i] ^= secret_key[i % sizeof(secret_key)];
}

/* Secure gateway: transform a Non-Secure buffer with the Secure-held key.
 * cmse_nonsecure_entry makes the linker emit an SG veneer for it in the
 * Non-Secure Callable region and record it in the import library. */
__attribute__((cmse_nonsecure_entry)) int KeystoreTransform_S(uint8_t *buf_ns, size_t len)
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
__attribute__((cmse_nonsecure_entry)) int KeystoreSelfTest_S(void)
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
