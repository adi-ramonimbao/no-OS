/*
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file   main.c
 * @brief  Maxim entry point for the MAX32657 TrustZone split demo consumer.
 *
 * Built on its own (Non-Secure-only) and merged with a Secure image produced
 * elsewhere (max32657_tz_secure_only). The application logic lives in
 * src/examples/keystore/keystore_nonsecure.c (example_main()), which is reused
 * unchanged by any platform's Non-Secure world; this file has nothing
 * platform-specific to add, so it only forwards to it.
 */

extern int example_main(void);

int main(void)
{
	return example_main();
}

