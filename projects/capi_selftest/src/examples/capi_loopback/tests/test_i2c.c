/***************************************************************************//**
 * @file test_i2c.c
 * @brief CAPI I2C single-board loopback tests.
 *
 * Exercises the CAPI I2C API using two buses on the same board: I2C1 as
 * initiator and I2C2 as target. Both sides go through CAPI — no
 * platform-specific fixture is needed for the data path. Every case moves real
 * bytes over the wire and checks them at the other end; the target side runs
 * async so the interrupt path is exercised implicitly (its completion callback
 * is what the initiator's blocking call is proven against).
 *
 * Hardware assumption: two I2C buses with SCL and SDA cross-wired.
 *
 * Copyright (c) 2026 Analog Devices, Inc.
 * SPDX-License-Identifier: BSD-3-Clause
 *******************************************************************************/

#include <stdint.h>
#include <string.h>
#include "parameters.h"
#include "test_framework.h"
#include "test_i2c.h"

#ifndef I2C_OPS

int test_i2c(void)
{
	static const struct test_case stub[] = {
		{ "NOT_CONFIGURED", NULL, false },
	};

	return test_framework_run_cases("I2C", stub, 1U);
}

#else /* I2C_OPS defined — full implementation follows */

#include "capi_i2c.h"
#include "common_data.h"

#define I2C_MODULE		"I2C"
#define I2C_ASYNC_TIMEOUT_US	1000000U
#define I2C_ASYNC_STEP_US	1000U

/*
 * I2C_TARGET_ALT_ADDR, I2C_SPEED_ALT and I2C_DUTY_CYCLE come from common_data.h
 * (defaulted there, overridable per platform).
 */

static volatile unsigned int target_callback_count;
static volatile enum capi_i2c_async_event target_callback_event;

static void target_test_callback(enum capi_i2c_async_event event, void *arg,
				 int event_extra)
{
	(void)arg;
	(void)event_extra;

	target_callback_event = event;
	target_callback_count++;
}

static volatile unsigned int master_callback_count;
static volatile enum capi_i2c_async_event master_callback_event;

static void master_test_callback(enum capi_i2c_async_event event, void *arg,
				 int event_extra)
{
	(void)arg;
	(void)event_extra;

	master_callback_event = event;
	master_callback_count++;
}

#ifdef I2C_TARGET_OPS
/**
 * @brief Drive one initiator TX -> target RX transfer and verify the bytes.
 *
 * Arms the target with an async receive, transmits @p len bytes from @p tx on
 * the initiator, waits for the target's completion callback, and checks the
 * received buffer matches. Shared by the basic and data-pattern cases.
 *
 * @param dev - Initiator device (addresses the target).
 * @param tgt_dev - Target device.
 * @param tgt_handle - Target controller handle (callback source).
 * @param tx - Bytes to send.
 * @param rx - Scratch buffer the target receives into (>= len).
 * @param len - Number of bytes.
 * @param addr - 7-bit address the initiator addresses.
 * @return 0 on pass, negative error code on failure.
 */
static int i2c_xfer_check(struct capi_i2c_device *dev,
			  struct capi_i2c_device *tgt_dev,
			  struct capi_i2c_controller_handle *tgt_handle,
			  const uint8_t *tx, uint8_t *rx, uint32_t len,
			  uint16_t addr)
{
	int ret;

	(void)tgt_handle;

	memset(rx, 0, len);
	target_callback_count = 0U;
	target_callback_event = CAPI_I2C_NONE;

	struct capi_i2c_transfer tgt_rx_xfer = {
		.buf = rx,
		.len = len,
	};
	ret = capi_i2c_receive_async(tgt_dev, &tgt_rx_xfer);
	TEST_ASSERT_EQ(ret, 0, "TARGET_LISTEN_RX");

	struct capi_i2c_transfer tx_xfer = {
		.buf = (uint8_t *)tx,
		.target_addr = addr,
		.len = len,
	};
	ret = capi_i2c_transmit(dev, &tx_xfer);
	TEST_ASSERT_EQ(ret, 0, "MASTER_TX");

	TEST_WAIT_UNTIL(target_callback_count > 0U,
			I2C_ASYNC_TIMEOUT_US, I2C_ASYNC_STEP_US);
	TEST_ASSERT(target_callback_count > 0U, "TARGET_RX_DONE");
	TEST_ASSERT_EQ(target_callback_event, CAPI_I2C_XFR_DONE,
		       "TARGET_RX_EVENT");
	TEST_ASSERT_EQ(memcmp(rx, tx, len), 0, "TX_RX_MATCH");

	return 0;
}

/**
 * @brief Drive one target TX -> initiator RX transfer and verify the bytes.
 *
 * The mirror of i2c_xfer_check: arms the target with an async transmit, then
 * the initiator does a blocking receive addressed to the target. The target's
 * completion callback is what proves the bytes were clocked out, so this drives
 * the target-transmit direction (the weaker-tested one) over the same buses.
 *
 * @param dev - Initiator device (addresses the target).
 * @param tgt_dev - Target device.
 * @param tx - Bytes the target sends.
 * @param rx - Scratch buffer the initiator receives into (>= len).
 * @param len - Number of bytes.
 * @param addr - 7-bit address the initiator addresses.
 * @return 0 on pass, negative error code on failure.
 */
static int i2c_xfer_check_target_tx(struct capi_i2c_device *dev,
				    struct capi_i2c_device *tgt_dev,
				    const uint8_t *tx, uint8_t *rx,
				    uint32_t len, uint16_t addr)
{
	int ret;

	memset(rx, 0, len);
	target_callback_count = 0U;
	target_callback_event = CAPI_I2C_NONE;

	struct capi_i2c_transfer tgt_tx_xfer = {
		.buf = (uint8_t *)tx,
		.len = len,
	};
	ret = capi_i2c_transmit_async(tgt_dev, &tgt_tx_xfer);
	TEST_ASSERT_EQ(ret, 0, "TARGET_LISTEN_TX");

	struct capi_i2c_transfer rx_xfer = {
		.buf = rx,
		.target_addr = addr,
		.len = len,
	};
	ret = capi_i2c_receive(dev, &rx_xfer);
	TEST_ASSERT_EQ(ret, 0, "MASTER_RX");

	TEST_WAIT_UNTIL(target_callback_count > 0U,
			I2C_ASYNC_TIMEOUT_US, I2C_ASYNC_STEP_US);
	TEST_ASSERT(target_callback_count > 0U, "TARGET_TX_DONE");
	TEST_ASSERT_EQ(target_callback_event, CAPI_I2C_XFR_DONE,
		       "TARGET_TX_EVENT");
	TEST_ASSERT_EQ(memcmp(rx, tx, len), 0, "RX_TX_MATCH");

	return 0;
}

/*
 * Every case below brings both buses fully live: it inits the initiator and
 * target controllers, runs I2C_PLATFORM_INIT, programs the target's address-
 * match register (I2C_PLATFORM_SET_TARGET) and arms an async receive. A bare
 * early return on a failed assertion would leave the target still ACKing its
 * address and the initiator holding the bus, so the NEXT case sees a busy bus
 * and fails for the wrong reason. CLEANUP routes every exit through the same
 * teardown the success path uses: drop the target match, deinit both handles,
 * and I2C_PLATFORM_DEINIT(). It is guarded on each handle != NULL and the
 * platform hooks are idempotent no-ops when nothing was set up, so it is safe
 * on any exit path (including one where the first init failed).
 *
 * Only the mid-body asserts (init, setup, transfers) use the _OR_CLEANUP form:
 * those are the ones that can bail with hardware still live. The success-path
 * TARGET_DEINIT/MASTER_DEINIT asserts stay plain -- they run after everything
 * passed and ARE the teardown, so there is nothing left to rescue.
 */
#define CLEANUP \
	do { \
		if (tgt_handle != NULL) { \
			I2C_PLATFORM_SET_TARGET(NULL); \
			(void)capi_i2c_deinit(tgt_handle); \
		} \
		if (init_handle != NULL) \
			(void)capi_i2c_deinit(init_handle); \
		I2C_PLATFORM_DEINIT(); \
	} while (0)

/**
 * @brief Synchronous initiator TX -> target RX and target TX -> initiator RX.
 *
 * Initialises both buses through CAPI. The target side uses async transfers
 * (interrupt-driven) so that the blocking initiator call and the target
 * listen can overlap on the single thread — the completion interrupt is what
 * the matching data is proven against, so this exercises the IRQ path too.
 *
 * @return 0 on pass, negative error code on failure.
 */
static int i2c_basic(void)
{
	struct capi_i2c_controller_handle *init_handle = NULL;
	struct capi_i2c_controller_handle *tgt_handle = NULL;
	struct capi_i2c_device dev = i2c_dev;
	struct capi_i2c_device tgt_dev = i2c_target_dev;
	uint8_t tx_data[] = { 0x55, 0xAA, 0x33, 0xCC };
	uint8_t target_rx[sizeof(tx_data)];
	uint8_t target_tx[] = { 0xDE, 0xAD, 0xBE, 0xEF };
	uint8_t master_rx[sizeof(target_tx)];
	int ret;

	TEST_SECTION("BASIC");

	ret = capi_i2c_init(&init_handle, &i2c_master_config);
	TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "MASTER_INIT");
	dev.controller = init_handle;

	ret = I2C_PLATFORM_INIT();
	TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "PLATFORM_INIT");

	ret = capi_i2c_init(&tgt_handle, &i2c_target_config);
	TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "TARGET_INIT");
	tgt_dev.controller = tgt_handle;

	I2C_PLATFORM_SET_TARGET(tgt_handle);

	ret = capi_i2c_register_callback(tgt_handle, target_test_callback, NULL);
	TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "TARGET_REGISTER_CB");

	/* Initiator TX -> target RX. */
	ret = i2c_xfer_check(&dev, &tgt_dev, tgt_handle, tx_data, target_rx,
			     sizeof(tx_data), I2C_TARGET_ADDR);
	TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "XFER_TX");

	/* Target TX -> initiator RX. */
	target_callback_count = 0U;
	target_callback_event = CAPI_I2C_NONE;
	memset(master_rx, 0, sizeof(master_rx));

	struct capi_i2c_transfer tgt_tx_xfer = {
		.buf = target_tx,
		.len = sizeof(target_tx),
	};
	ret = capi_i2c_transmit_async(&tgt_dev, &tgt_tx_xfer);
	TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "TARGET_LISTEN_TX");

	struct capi_i2c_transfer rx_xfer = {
		.buf = master_rx,
		.target_addr = I2C_TARGET_ADDR,
		.len = sizeof(master_rx),
	};
	ret = capi_i2c_receive(&dev, &rx_xfer);
	TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "MASTER_RX");

	TEST_WAIT_UNTIL(target_callback_count > 0U,
			I2C_ASYNC_TIMEOUT_US, I2C_ASYNC_STEP_US);
	TEST_ASSERT_OR_CLEANUP(target_callback_count > 0U, "TARGET_TX_DONE");
	TEST_ASSERT_EQ_OR_CLEANUP(memcmp(master_rx, target_tx, sizeof(target_tx)), 0,
				  "RX_TX_MATCH");

	I2C_PLATFORM_SET_TARGET(NULL);
	TEST_ASSERT_EQ(capi_i2c_deinit(tgt_handle), 0, "TARGET_DEINIT");
	TEST_ASSERT_EQ(capi_i2c_deinit(init_handle), 0, "MASTER_DEINIT");
	I2C_PLATFORM_DEINIT();

	return 0;
}

/**
 * @brief Data integrity across bit patterns and transfer sizes.
 *
 * Sends a set of patterns (all-ones, all-zeros, alternating, walking-one) at
 * several sizes from the initiator to the target and verifies each arrives
 * intact. Catches stuck data lines and length/off-by-one bugs a single fixed
 * payload would miss.
 *
 * @return 0 on pass, negative error code on failure.
 */
static int i2c_data(void)
{
	struct capi_i2c_controller_handle *init_handle = NULL;
	struct capi_i2c_controller_handle *tgt_handle = NULL;
	struct capi_i2c_device dev = i2c_dev;
	struct capi_i2c_device tgt_dev = i2c_target_dev;
	static const uint8_t fills[] = { 0xFFU, 0x00U, 0xAAU, 0x55U };
	static const uint32_t sizes[] = { 1U, 16U, 32U };
	uint8_t tx[32];
	uint8_t rx[32];
	int ret;

	TEST_SECTION("DATA");

	ret = capi_i2c_init(&init_handle, &i2c_master_config);
	TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "MASTER_INIT");
	dev.controller = init_handle;

	ret = I2C_PLATFORM_INIT();
	TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "PLATFORM_INIT");

	ret = capi_i2c_init(&tgt_handle, &i2c_target_config);
	TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "TARGET_INIT");
	tgt_dev.controller = tgt_handle;

	I2C_PLATFORM_SET_TARGET(tgt_handle);
	ret = capi_i2c_register_callback(tgt_handle, target_test_callback, NULL);
	TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "TARGET_REGISTER_CB");

	/* Constant-fill patterns at each size. */
	for (uint32_t s = 0U; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
		for (uint32_t p = 0U; p < sizeof(fills); p++) {
			memset(tx, fills[p], sizes[s]);
			ret = i2c_xfer_check(&dev, &tgt_dev, tgt_handle, tx, rx,
					     sizes[s], I2C_TARGET_ADDR);
			TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "XFER_FILL");
		}
	}

	/* Walking-one over a full 32-byte payload. */
	for (uint32_t bit = 0U; bit < 8U; bit++) {
		memset(tx, (uint8_t)(1U << bit), sizeof(tx));
		ret = i2c_xfer_check(&dev, &tgt_dev, tgt_handle, tx, rx,
				     sizeof(tx), I2C_TARGET_ADDR);
		TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "XFER_WALK");
	}

	I2C_PLATFORM_SET_TARGET(NULL);
	TEST_ASSERT_EQ(capi_i2c_deinit(tgt_handle), 0, "TARGET_DEINIT");
	TEST_ASSERT_EQ(capi_i2c_deinit(init_handle), 0, "MASTER_DEINIT");
	I2C_PLATFORM_DEINIT();

	return 0;
}

/**
 * @brief Runtime target readdressing, proven over the bus.
 *
 * register_target() rewrites the controller's own address-match register. This
 * moves the target to a second address at runtime, then proves a transfer
 * addressed to the new value is received while the original address is no
 * longer matched — externally observable evidence the register took effect,
 * not just a return code.
 *
 * @return 0 on pass, negative error code on failure.
 */
static int i2c_readdress(void)
{
	struct capi_i2c_controller_handle *init_handle = NULL;
	struct capi_i2c_controller_handle *tgt_handle = NULL;
	struct capi_i2c_device dev = i2c_dev;
	struct capi_i2c_device tgt_dev = i2c_target_dev;
	uint8_t tx_data[] = { 0x0F, 0xF0, 0x12, 0x34 };
	uint8_t target_rx[sizeof(tx_data)];
	int ret;

	TEST_SECTION("READDRESS");

	ret = capi_i2c_init(&init_handle, &i2c_master_config);
	TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "MASTER_INIT");
	dev.controller = init_handle;

	ret = I2C_PLATFORM_INIT();
	TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "PLATFORM_INIT");

	ret = capi_i2c_init(&tgt_handle, &i2c_target_config);
	TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "TARGET_INIT");
	tgt_dev.controller = tgt_handle;

	I2C_PLATFORM_SET_TARGET(tgt_handle);
	ret = capi_i2c_register_callback(tgt_handle, target_test_callback, NULL);
	TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "TARGET_REGISTER_CB");

	/* Move the target to the alternate address. */
	ret = capi_i2c_register_target(tgt_handle, I2C_TARGET_ALT_ADDR);
	TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "REGISTER_TARGET");

	/* A transfer to the new address must now be received and match. */
	ret = i2c_xfer_check(&dev, &tgt_dev, tgt_handle, tx_data, target_rx,
			     sizeof(tx_data), I2C_TARGET_ALT_ADDR);
	TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "XFER_ALT_ADDR");

	/*
	 * Negative half of the readdress proof: the OLD address must no longer
	 * be answered. Arm the target, transmit to I2C_TARGET_ADDR, and require
	 * the target callback to stay silent. A transmit that NACKs (no target
	 * at that address) may itself return an error -- that is also acceptable
	 * evidence the old address is gone, so the transmit's return is not
	 * asserted; only the absence of a completed target receive is. Without
	 * this, a driver that ADDED the new address without dropping the old one
	 * would still pass XFER_ALT_ADDR above.
	 */
	memset(target_rx, 0, sizeof(target_rx));
	target_callback_count = 0U;
	target_callback_event = CAPI_I2C_NONE;

	struct capi_i2c_transfer old_rx_xfer = {
		.buf = target_rx,
		.len = sizeof(tx_data),
	};
	ret = capi_i2c_receive_async(&tgt_dev, &old_rx_xfer);
	TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "OLD_ADDR_LISTEN");

	struct capi_i2c_transfer old_tx_xfer = {
		.buf = tx_data,
		.target_addr = I2C_TARGET_ADDR,
		.len = sizeof(tx_data),
	};
	(void)capi_i2c_transmit(&dev, &old_tx_xfer);

	TEST_WAIT_UNTIL(target_callback_count > 0U,
			I2C_ASYNC_TIMEOUT_US, I2C_ASYNC_STEP_US);
	TEST_ASSERT_EQ_OR_CLEANUP(target_callback_count, 0U, "OLD_ADDR_SILENT");

	/*
	 * The target's async receive is still armed (nothing addressed it), and
	 * a failed initiator transmit may have left the bus held. recover_bus
	 * clears both so the teardown and later cases start clean; its own return
	 * is best-effort (see the driver note) so it is not asserted.
	 *
	 * Cancelling that armed receive invokes the target callback with a
	 * non-success status, which would otherwise make the count checked above
	 * look like a late delivery to the old address. The OLD_ADDR_SILENT
	 * assertion has already run, so reset the counters here and let the
	 * cancellation land against a clean slate.
	 */
	(void)capi_i2c_recover_bus(tgt_handle);
	(void)capi_i2c_recover_bus(init_handle);

	target_callback_count = 0U;
	target_callback_event = CAPI_I2C_NONE;

	/*
	 * With the in-flight receive cancelled, the role change must now succeed.
	 * This is the real assertion of the case's cleanup contract: a target that
	 * cannot be unregistered after a recovery would leave every later case
	 * failing -EBUSY on a controller stuck in target mode.
	 */
	ret = capi_i2c_unregister_target(tgt_handle);
	TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "UNREGISTER_TARGET");

	I2C_PLATFORM_SET_TARGET(NULL);
	TEST_ASSERT_EQ(capi_i2c_deinit(tgt_handle), 0, "TARGET_DEINIT");
	TEST_ASSERT_EQ(capi_i2c_deinit(init_handle), 0, "MASTER_DEINIT");
	I2C_PLATFORM_DEINIT();

	return 0;
}

/**
 * @brief Sub-address concatenation on the wire.
 *
 * The initiator transmits with a sub_address plus a data payload. The driver
 * must put [sub_address..., buf...] on the bus as one write. The target, which
 * just receives raw bytes, checks the concatenated stream arrives intact —
 * externally observable proof of the sub-address build path, with no register
 * device needed.
 *
 * @return 0 on pass, negative error code on failure.
 */
static int i2c_subaddr(void)
{
	struct capi_i2c_controller_handle *init_handle = NULL;
	struct capi_i2c_controller_handle *tgt_handle = NULL;
	struct capi_i2c_device dev = i2c_dev;
	struct capi_i2c_device tgt_dev = i2c_target_dev;
	uint8_t sub_addr[] = { 0x1A, 0x2B };
	uint8_t payload[] = { 0xDE, 0xAD, 0xBE, 0xEF };
	uint8_t expected[sizeof(sub_addr) + sizeof(payload)];
	uint8_t target_rx[sizeof(expected)];
	int ret;

	TEST_SECTION("SUBADDR");

	ret = capi_i2c_init(&init_handle, &i2c_master_config);
	TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "MASTER_INIT");
	dev.controller = init_handle;

	ret = I2C_PLATFORM_INIT();
	TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "PLATFORM_INIT");

	ret = capi_i2c_init(&tgt_handle, &i2c_target_config);
	TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "TARGET_INIT");
	tgt_dev.controller = tgt_handle;

	I2C_PLATFORM_SET_TARGET(tgt_handle);
	ret = capi_i2c_register_callback(tgt_handle, target_test_callback, NULL);
	TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "TARGET_REGISTER_CB");

	memcpy(expected, sub_addr, sizeof(sub_addr));
	memcpy(expected + sizeof(sub_addr), payload, sizeof(payload));

	memset(target_rx, 0, sizeof(target_rx));
	target_callback_count = 0U;
	target_callback_event = CAPI_I2C_NONE;

	struct capi_i2c_transfer tgt_rx_xfer = {
		.buf = target_rx,
		.len = sizeof(target_rx),
	};
	ret = capi_i2c_receive_async(&tgt_dev, &tgt_rx_xfer);
	TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "TARGET_LISTEN_RX");

	struct capi_i2c_transfer tx_xfer = {
		.sub_address = sub_addr,
		.sub_address_len = sizeof(sub_addr),
		.buf = payload,
		.target_addr = I2C_TARGET_ADDR,
		.len = sizeof(payload),
	};
	ret = capi_i2c_transmit(&dev, &tx_xfer);
	TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "MASTER_TX");

	TEST_WAIT_UNTIL(target_callback_count > 0U,
			I2C_ASYNC_TIMEOUT_US, I2C_ASYNC_STEP_US);
	TEST_ASSERT_OR_CLEANUP(target_callback_count > 0U, "TARGET_RX_DONE");
	TEST_ASSERT_EQ_OR_CLEANUP(target_callback_event, CAPI_I2C_XFR_DONE,
				  "TARGET_RX_EVENT");
	TEST_ASSERT_EQ_OR_CLEANUP(memcmp(target_rx, expected, sizeof(expected)), 0,
				  "SUBADDR_MATCH");

	I2C_PLATFORM_SET_TARGET(NULL);
	TEST_ASSERT_EQ(capi_i2c_deinit(tgt_handle), 0, "TARGET_DEINIT");
	TEST_ASSERT_EQ(capi_i2c_deinit(init_handle), 0, "MASTER_DEINIT");
	I2C_PLATFORM_DEINIT();

	return 0;
}

/**
 * @brief Target-transmit direction across transfer sizes.
 *
 * i2c_data covers initiator TX -> target RX; this drives the reverse direction,
 * where the interesting variable is length rather than content. The target send
 * path fills a hardware FIFO from its completion interrupt, so the sizes that
 * matter are below, exactly at, and above the FIFO depth -- a transfer that
 * fits in one load exercises none of the refill logic that a longer one needs.
 * Two patterns are enough to catch a stuck line; i2c_data already sweeps the
 * rest over the same wires.
 *
 * @return 0 on pass, negative error code on failure.
 */
static int i2c_target_tx(void)
{
	struct capi_i2c_controller_handle *init_handle = NULL;
	struct capi_i2c_controller_handle *tgt_handle = NULL;
	struct capi_i2c_device dev = i2c_dev;
	struct capi_i2c_device tgt_dev = i2c_target_dev;
	/*
	 * A narrower matrix than i2c_data's: that case already proves every bit
	 * pattern survives the bus, so repeating the full sweep here would only
	 * re-test the wire. What is specific to the target-transmit path is the
	 * FIFO handling, so the sizes are what matter -- one byte, exactly the
	 * FIFO depth, and past it (the refill boundary).
	 */
	static const uint8_t fills[] = { 0xAAU, 0x55U };
	static const uint32_t sizes[] = { 1U, 16U, 32U };
	uint8_t tx[32];
	uint8_t rx[32];
	int ret;

	TEST_SECTION("TARGET_TX");

	ret = capi_i2c_init(&init_handle, &i2c_master_config);
	TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "MASTER_INIT");
	dev.controller = init_handle;

	ret = I2C_PLATFORM_INIT();
	TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "PLATFORM_INIT");

	ret = capi_i2c_init(&tgt_handle, &i2c_target_config);
	TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "TARGET_INIT");
	tgt_dev.controller = tgt_handle;

	I2C_PLATFORM_SET_TARGET(tgt_handle);
	ret = capi_i2c_register_callback(tgt_handle, target_test_callback, NULL);
	TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "TARGET_REGISTER_CB");

	for (uint32_t s = 0U; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
		for (uint32_t p = 0U; p < sizeof(fills); p++) {
			memset(tx, fills[p], sizes[s]);
			ret = i2c_xfer_check_target_tx(&dev, &tgt_dev, tx, rx,
						       sizes[s], I2C_TARGET_ADDR);
			TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "XFER_FILL");
		}
	}

	I2C_PLATFORM_SET_TARGET(NULL);
	TEST_ASSERT_EQ(capi_i2c_deinit(tgt_handle), 0, "TARGET_DEINIT");
	TEST_ASSERT_EQ(capi_i2c_deinit(init_handle), 0, "MASTER_DEINIT");
	I2C_PLATFORM_DEINIT();

	return 0;
}

/**
 * @brief Initiator-side async transfers, completed on the master's callback.
 *
 * Every other case drives the master synchronously (polled) and waits on the
 * target's completion callback. That leaves the master's OWN async path -- its
 * transmit_async/receive_async and the interrupt-driven completion behind them
 * -- with no coverage at all. Here the initiator runs async in both directions
 * and the test waits on the MASTER's callback, so the master ISR completion
 * path is what each assertion is proven against.
 *
 * The target still listens async (its RX must be armed for the master's TX to
 * land, and vice versa); both sides being async is fine on the single thread
 * because neither call blocks -- completion arrives via interrupt on each side
 * independently. This case needs an IRQ-backed master AND target, so it is
 * gated on both flags.
 *
 * Scope: this covers direct master async TX and RX. It deliberately does NOT
 * exercise the master sub-address register-read (the RX_SUBADDR state machine),
 * which needs a peer that re-arms mid-frame -- not expressible against this
 * one-shot-armed loopback target. That path belongs in a driver unit test.
 *
 * @return 0 on pass, negative error code on failure.
 */
static int i2c_master_async(void)
{
	struct capi_i2c_controller_handle *init_handle = NULL;
	struct capi_i2c_controller_handle *tgt_handle = NULL;
	struct capi_i2c_device dev = i2c_dev;
	struct capi_i2c_device tgt_dev = i2c_target_dev;
	uint8_t tx_data[] = { 0x11, 0x22, 0x33, 0x44 };
	uint8_t target_rx[sizeof(tx_data)];
	uint8_t target_tx[] = { 0xA5, 0x5A, 0xC3, 0x3C };
	uint8_t master_rx[sizeof(target_tx)];
	int ret;

	TEST_SECTION("MASTER_ASYNC");

	ret = capi_i2c_init(&init_handle, &i2c_master_config);
	TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "MASTER_INIT");
	dev.controller = init_handle;

	ret = I2C_PLATFORM_INIT();
	TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "PLATFORM_INIT");

	ret = capi_i2c_init(&tgt_handle, &i2c_target_config);
	TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "TARGET_INIT");
	tgt_dev.controller = tgt_handle;

	I2C_PLATFORM_SET_TARGET(tgt_handle);
	ret = capi_i2c_register_callback(tgt_handle, target_test_callback, NULL);
	TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "TARGET_REGISTER_CB");
	ret = capi_i2c_register_callback(init_handle, master_test_callback, NULL);
	TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "MASTER_REGISTER_CB");

	/* Master async TX -> target async RX; wait on the MASTER callback. */
	memset(target_rx, 0, sizeof(target_rx));
	target_callback_count = 0U;
	master_callback_count = 0U;
	master_callback_event = CAPI_I2C_NONE;

	struct capi_i2c_transfer tgt_rx_xfer = {
		.buf = target_rx,
		.len = sizeof(target_rx),
	};
	ret = capi_i2c_receive_async(&tgt_dev, &tgt_rx_xfer);
	TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "TARGET_LISTEN_RX");

	struct capi_i2c_transfer tx_xfer = {
		.buf = tx_data,
		.target_addr = I2C_TARGET_ADDR,
		.len = sizeof(tx_data),
	};
	ret = capi_i2c_transmit_async(&dev, &tx_xfer);
	TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "MASTER_TX_ASYNC");

	TEST_WAIT_UNTIL(master_callback_count > 0U,
			I2C_ASYNC_TIMEOUT_US, I2C_ASYNC_STEP_US);
	TEST_ASSERT_OR_CLEANUP(master_callback_count > 0U, "MASTER_TX_DONE");
	TEST_ASSERT_EQ_OR_CLEANUP(master_callback_event, CAPI_I2C_XFR_DONE,
				  "MASTER_TX_EVENT");
	TEST_ASSERT_EQ_OR_CLEANUP(memcmp(target_rx, tx_data, sizeof(tx_data)), 0,
				  "MASTER_TX_MATCH");

	/* Target async TX -> master async RX; wait on the MASTER callback. */
	memset(master_rx, 0, sizeof(master_rx));
	target_callback_count = 0U;
	master_callback_count = 0U;
	master_callback_event = CAPI_I2C_NONE;

	struct capi_i2c_transfer tgt_tx_xfer = {
		.buf = target_tx,
		.len = sizeof(target_tx),
	};
	ret = capi_i2c_transmit_async(&tgt_dev, &tgt_tx_xfer);
	TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "TARGET_LISTEN_TX");

	struct capi_i2c_transfer rx_xfer = {
		.buf = master_rx,
		.target_addr = I2C_TARGET_ADDR,
		.len = sizeof(master_rx),
	};
	ret = capi_i2c_receive_async(&dev, &rx_xfer);
	TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "MASTER_RX_ASYNC");

	TEST_WAIT_UNTIL(master_callback_count > 0U,
			I2C_ASYNC_TIMEOUT_US, I2C_ASYNC_STEP_US);
	TEST_ASSERT_OR_CLEANUP(master_callback_count > 0U, "MASTER_RX_DONE");
	TEST_ASSERT_EQ_OR_CLEANUP(master_callback_event, CAPI_I2C_XFR_DONE,
				  "MASTER_RX_EVENT");
	TEST_ASSERT_EQ_OR_CLEANUP(memcmp(master_rx, target_tx, sizeof(target_tx)), 0,
				  "MASTER_RX_MATCH");

	I2C_PLATFORM_SET_TARGET(NULL);
	TEST_ASSERT_EQ(capi_i2c_deinit(tgt_handle), 0, "TARGET_DEINIT");
	TEST_ASSERT_EQ(capi_i2c_deinit(init_handle), 0, "MASTER_DEINIT");
	I2C_PLATFORM_DEINIT();

	return 0;
}

#endif /* I2C_TARGET_OPS */

static int i2c_bus_speed(void)
{
	struct capi_i2c_controller_handle *init_handle = NULL;
	struct capi_i2c_controller_handle *tgt_handle = NULL;
	struct capi_i2c_device dev = i2c_dev;
	struct capi_i2c_device tgt_dev = i2c_target_dev;
	uint8_t tx_data[] = { 0x5A, 0xC3, 0x0F, 0xE1 };
	uint8_t target_rx[sizeof(tx_data)];
	int ret;

	TEST_SECTION("BUS_SPEED");

	ret = capi_i2c_init(&init_handle, &i2c_master_config);
	TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "MASTER_INIT");
	dev.controller = init_handle;

	ret = I2C_PLATFORM_INIT();
	TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "PLATFORM_INIT");

	ret = capi_i2c_init(&tgt_handle, &i2c_target_config);
	TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "TARGET_INIT");
	tgt_dev.controller = tgt_handle;

	I2C_PLATFORM_SET_TARGET(tgt_handle);
	ret = capi_i2c_register_callback(tgt_handle, target_test_callback, NULL);
	TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "TARGET_REGISTER_CB");

	ret = i2c_xfer_check(&dev, &tgt_dev, tgt_handle, tx_data, target_rx,
			     sizeof(tx_data), I2C_TARGET_ADDR);
	TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "XFER");

	I2C_PLATFORM_SET_TARGET(NULL);
	TEST_ASSERT_EQ(capi_i2c_deinit(tgt_handle), 0, "TARGET_DEINIT");
	TEST_ASSERT_EQ(capi_i2c_deinit(init_handle), 0, "MASTER_DEINIT");
	I2C_PLATFORM_DEINIT();

	return 0;
}

#undef CLEANUP

/*
 * Every case needs the second (target) bus to observe traffic, so the whole
 * table is gated on I2C_TARGET_OPS. A build with only an initiator mapped runs
 * the single stub entry.
 *
 * The target always listens async, so every case here needs an IRQ-backed
 * target and skips (not fails) on a no-IRQ board -- hence !I2C_TARGET_USE_IRQ
 * in the skip column. MASTER_ASYNC additionally drives the initiator async, so
 * it needs a master IRQ too and is gated on both flags.
 */
static const struct test_case i2c_subtests[] = {
#ifdef I2C_TARGET_OPS
	{ "BASIC",        i2c_basic,        !I2C_TARGET_USE_IRQ },
	{ "DATA",         i2c_data,         !I2C_TARGET_USE_IRQ },
	{ "READDRESS",    i2c_readdress,    !I2C_TARGET_USE_IRQ },
	{ "SUBADDR",      i2c_subaddr,      !I2C_TARGET_USE_IRQ },
	{ "BUS_SPEED",    i2c_bus_speed,    !I2C_TARGET_USE_IRQ },
	{ "TARGET_TX",    i2c_target_tx,    !I2C_TARGET_USE_IRQ },
	{ "MASTER_ASYNC", i2c_master_async, !(I2C_MASTER_USE_IRQ && I2C_TARGET_USE_IRQ) },
#else
	{ "BASIC",        NULL,             false },
#endif
};

int test_i2c(void)
{
	return test_framework_run_cases(I2C_MODULE, i2c_subtests,
					sizeof(i2c_subtests) / sizeof(i2c_subtests[0]));
}

#endif /* I2C_OPS */
