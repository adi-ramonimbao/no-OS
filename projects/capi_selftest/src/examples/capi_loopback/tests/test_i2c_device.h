/***************************************************************************//**
 * @file test_i2c_device.h
 * @brief CAPI I2C controller-mode tests against a real on-bus target device.
 *
 * Copyright (c) 2026 Analog Devices, Inc.
 * SPDX-License-Identifier: BSD-3-Clause
 *******************************************************************************/

#ifndef TEST_I2C_DEVICE_H
#define TEST_I2C_DEVICE_H

/**
 * @brief Run the I2C controller-mode device tests, if a target is mapped.
 * @return 0 if all executed cases passed, first non-zero test error otherwise.
 */
int test_i2c_device(void);

#endif /* TEST_I2C_DEVICE_H */
