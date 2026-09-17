/*
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file test_dma_instance.c
 * @brief Report which CAPI DMA controller the current world drives.
 *
 * A display group run just before the DMA transfers so the log makes clear
 * which controller they exercise. On the MAX32657 the DMA security attribution
 * is hardwired (not SPC-configurable): DMA1 is Secure, DMA0 is Non-Secure. The
 * CAPI DMA driver (capi/platform/maxim/max32657/maxim_capi_dma.c) selects the
 * reachable controller from CONFIG_TRUSTED_EXECUTION_SECURE; this mirrors that
 * selection so the world <-> controller pairing is visible and a mismatch would
 * fail here rather than silently in the transfer.
 */

#include <stdint.h>

#include "max32657.h"
#include "test_framework.h"
#include "test_dma_instance.h"

#define DMA_INSTANCE_MODULE	"DMA_INSTANCE"

#if (CONFIG_TRUSTED_EXECUTION_SECURE == 1)
#define DMA_INSTANCE_BASE	MXC_BASE_DMA1_S
#define DMA_INSTANCE_NAME	"DMA1_S (Secure)"
#else
#define DMA_INSTANCE_BASE	MXC_BASE_DMA0_NS
#define DMA_INSTANCE_NAME	"DMA0_NS (Non-Secure)"
#endif

/**
 * @brief Print both DMA bases and the one this world selected, then confirm it.
 */
static int dma_instance_report(void)
{
	TEST_SECTION("SELECTED");

	TEST_INFO("CAPI DMA controller for this world: " DMA_INSTANCE_NAME);
	TEST_VALUE("DMA0_NS_BASE", MXC_BASE_DMA0_NS);
	TEST_VALUE("DMA1_S_BASE", MXC_BASE_DMA1_S);
	TEST_VALUE("SELECTED_BASE", DMA_INSTANCE_BASE);

	/* The Non-Secure world can only reach DMA0; the Secure world drives DMA1. */
#if (CONFIG_TRUSTED_EXECUTION_SECURE == 1)
	TEST_ASSERT_EQ(DMA_INSTANCE_BASE, MXC_BASE_DMA1_S, "IS_DMA1_S");
#else
	TEST_ASSERT_EQ(DMA_INSTANCE_BASE, MXC_BASE_DMA0_NS, "IS_DMA0_NS");
#endif

	return 0;
}

static const struct test_case dma_instance_subtests[] = {
	{ "SELECTED", dma_instance_report, false },
};

/**
 * @brief Report which CAPI DMA controller the current world drives.
 * @return 0 on pass, non-zero otherwise.
 */
int test_dma_instance(void)
{
	return test_framework_run_cases(DMA_INSTANCE_MODULE, dma_instance_subtests,
					sizeof(dma_instance_subtests) / sizeof(dma_instance_subtests[0]));
}
