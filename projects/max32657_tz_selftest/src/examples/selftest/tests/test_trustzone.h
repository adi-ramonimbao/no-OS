/*
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file test_trustzone.h
 * @brief TrustZone secure-gateway test entry.
 */

#ifndef TEST_TRUSTZONE_H
#define TEST_TRUSTZONE_H

/**
 * @brief Run the TrustZone secure-gateway tests from the Non-Secure world.
 * @return 0 if all cases passed, first non-zero test error otherwise.
 */
int test_trustzone(void);

#endif /* TEST_TRUSTZONE_H */
