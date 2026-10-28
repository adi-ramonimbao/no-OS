/***************************************************************************//**
 * @file test_rtc.h
 * @brief CAPI RTC test entry.
 *
 * Copyright (c) 2026 Analog Devices, Inc.
 * SPDX-License-Identifier: BSD-3-Clause
 *******************************************************************************/

#ifndef TEST_RTC_H
#define TEST_RTC_H

/**
 * @brief Run all RTC test cases available on the mapped platform.
 * @return 0 if all executed cases passed, first non-zero test error otherwise.
 */
int test_rtc(void);

#endif /* TEST_RTC_H */
