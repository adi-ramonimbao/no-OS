/*
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file test_dma_instance.c
 * @brief Report which DMA path the STM32 Non-Secure test world uses.
 */

#include <stdint.h>

#include "stm32_hal.h"
#include "test_framework.h"
#include "test_dma_instance.h"

#define DMA_INSTANCE_MODULE	"DMA_INSTANCE"

#if defined(STM32H5)
#define DMA_INSTANCE_BASE	((uintptr_t)GPDMA1_Channel0)
#define DMA_INSTANCE_NAME	"GPDMA1_Channel0 (Non-Secure)"
#else
#define DMA_INSTANCE_BASE	0U
#define DMA_INSTANCE_NAME	"Unsupported STM32 family"
#endif

static int dma_instance_report(void)
{
	TEST_SECTION("SELECTED");

	TEST_INFO("CAPI DMA path for this world: " DMA_INSTANCE_NAME);
	TEST_VALUE("SELECTED_BASE", DMA_INSTANCE_BASE);

#if defined(STM32H5)
	TEST_ASSERT_NEQ(DMA_INSTANCE_BASE, 0U, "BASE_VALID");
#else
	TEST_SKIP_CAT(SKIP_NOT_SUPPORTED, "DMA_INSTANCE only implemented for STM32H5");
#endif

	return 0;
}

static const struct test_case dma_instance_subtests[] = {
	{ "SELECTED", dma_instance_report, false },
};

int test_dma_instance(void)
{
	return test_framework_run_cases(DMA_INSTANCE_MODULE, dma_instance_subtests,
					sizeof(dma_instance_subtests) / sizeof(dma_instance_subtests[0]));
}
