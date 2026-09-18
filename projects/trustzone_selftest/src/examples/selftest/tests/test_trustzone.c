/*
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file test_trustzone.c
 * @brief TrustZone secure-gateway tests, run from the Non-Secure world.
 *
 * These are the TrustZone-specific tests: they call the Secure __ns_entry
 * gateways (resolved from secure_implib.o) across the security boundary and
 * check both the functional path and the CMSE pointer guard.
 *
 *   SECURE_RETURN     - GetSecureMagic_S() returns the Secure-owned constant,
 *                       proving the plain Secure -> Non-Secure return path.
 *   INCREMENT         - IncrementCount_S() advances a counter that lives in
 *                       Non-Secure memory (the happy path with a valid pointer).
 *   REJECT_NULL       - IncrementCount_S(NULL) is rejected with -EINVAL by the
 *                       gateway's cmse_check_pointed_object() guard.
 *   REJECT_SECURE_PTR - IncrementCount_S() given a pointer into Secure SRAM is
 *                       rejected with -EINVAL: the object is not accessible to a
 *                       Non-Secure caller, so the CMSE check returns NULL. This
 *                       is the security guarantee - a hostile Non-Secure pointer
 *                       cannot make Secure code touch Secure memory. The address
 *                       is only ever passed, never dereferenced here.
 *
 * All checks stay in software; no external wiring is required.
 */

#include <errno.h>
#include <stdint.h>

#include "test_framework.h"
#include "test_trustzone.h"
#include "tz_gateways.h"

#define TZ_MODULE		"TRUSTZONE"

/*
 * Base of a Secure-only SRAM alias/address on the active platform.
 * parameters.h may override this per platform; default keeps the existing
 * MAX32657 address used by this test.
 */
#ifndef TZ_SECURE_SRAM_BASE
#define TZ_SECURE_SRAM_BASE	0x30000000u
#endif /* TZ_SECURE_SRAM_BASE */

/**
 * @brief GetSecureMagic_S() returns the Secure-owned constant.
 */
static int tz_secure_return(void)
{
	uint32_t magic;

	TEST_SECTION("SECURE_RETURN");

	magic = GetSecureMagic_S();
	TEST_VALUE("GOT", magic);
	TEST_VALUE("EXPECTED", TZ_SECURE_MAGIC);
	TEST_ASSERT_EQ(magic, TZ_SECURE_MAGIC, "MAGIC");

	return 0;
}

/**
 * @brief IncrementCount_S() advances a counter in Non-Secure memory.
 */
static int tz_increment(void)
{
	volatile int count = 0;

	TEST_SECTION("INCREMENT");

	TEST_ASSERT_EQ(IncrementCount_S(&count), 0, "CALL_1");
	TEST_VALUE("COUNT", count);
	TEST_ASSERT_EQ(count, 1, "COUNT_1");
	TEST_ASSERT_EQ(IncrementCount_S(&count), 0, "CALL_2");
	TEST_VALUE("COUNT", count);
	TEST_ASSERT_EQ(count, 2, "COUNT_2");

	return 0;
}

/**
 * @brief A NULL pointer is rejected by the gateway's CMSE guard.
 */
static int tz_reject_null(void)
{
	TEST_SECTION("REJECT_NULL");

	TEST_ASSERT_EQ(IncrementCount_S(NULL), -EINVAL, "EINVAL");

	return 0;
}

/**
 * @brief A pointer into Secure memory is rejected by the gateway's CMSE guard.
 */
static int tz_reject_secure_ptr(void)
{
	volatile int *secure_ptr = (volatile int *)TZ_SECURE_SRAM_BASE;

	TEST_SECTION("REJECT_SECURE_PTR");

	/* Only the address is passed across the boundary; the Non-Secure world
	 * never dereferences it. The Secure gateway's cmse_check_pointed_object()
	 * finds it is not Non-Secure-accessible and returns -EINVAL. */
	TEST_ASSERT_EQ(IncrementCount_S(secure_ptr), -EINVAL, "EINVAL");

	return 0;
}

static const struct test_case tz_subtests[] = {
	{ "SECURE_RETURN",     tz_secure_return,     false },
	{ "INCREMENT",         tz_increment,         false },
	{ "REJECT_NULL",       tz_reject_null,       false },
	{ "REJECT_SECURE_PTR", tz_reject_secure_ptr, false },
};

int test_trustzone(void)
{
	return test_framework_run_cases(TZ_MODULE, tz_subtests,
					sizeof(tz_subtests) / sizeof(tz_subtests[0]));
}
