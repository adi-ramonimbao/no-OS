/*
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file   main.c
 * @brief  Maxim entry point for the TrustZone hello demo.
 *
 * Reached from the Secure world via NonSecure_Init(). The application logic
 * lives in src/examples/hello/hello_nonsecure.c (example_main()), which is
 * reused unchanged by any platform's Non-Secure world; this file has nothing
 * platform-specific to add, so it only forwards to it.
 */

extern int example_main(void);

int main(void)
{
	return example_main();
}

