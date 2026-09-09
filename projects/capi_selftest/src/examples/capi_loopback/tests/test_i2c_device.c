/***************************************************************************//**
 * @file test_i2c_device.c
 * @brief CAPI I2C controller-mode tests against a real on-bus target device.
 *
 * Unlike the loopback suites, these talk to an actual I2C peripheral wired to
 * the bus (on the MAX32657EVKIT: the on-board ADXL367). The test logic is
 * device-agnostic; the specific part is described entirely by parameters.h:
 *
 *   I2C_HAS_DEVICE     - gate: a target is present and described
 *   I2C_DEV_ADDR       - primary 7-bit address
 *   I2C_DEV_ADDR_ALT   - optional alternate address to probe (strap-dependent)
 *   I2C_DEV_ID_REG     - start of a read-only ID/WHO_AM_I register block
 *   I2C_DEV_ID_LEN     - number of ID bytes
 *   I2C_DEV_ID_VALS    - brace-enclosed list of expected ID byte values
 *   I2C_DEV_RW_REG     - a benign 8-bit read/write register for a round-trip
 *   I2C_DEV_RW_TESTVAL - value written to that register
 *   I2C_DEV_PREP_REG / I2C_DEV_PREP_VAL / I2C_DEV_PREP_WAIT_MS
 *                      - optional: a register write (+ settle delay) that puts
 *                        the device in a state where config writes take effect
 *                        (e.g. a soft reset)
 *
 * Any board with any I2C part can reuse this suite by supplying those macros;
 * only standard register framing (write reg address, repeated-start read) is
 * assumed.
 *
 * Copyright (c) 2026 Analog Devices, Inc.
 * SPDX-License-Identifier: BSD-3-Clause
 *******************************************************************************/

#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "capi_i2c.h"
#include "capi_dma.h"
#include "capi_time.h"
#include "parameters.h"
#include "common_data.h"
#include "test_framework.h"
#include "test_i2c_device.h"

#if !defined(I2C_HAS_DEVICE) || !defined(I2C_OPS)

/* No on-bus target mapped on this platform: the suite compiles out to a skip. */
int test_i2c_device(void)
{
	static const struct test_case stub[] = {
		{ "NO_DEVICE_MAPPED", NULL, false },
	};

	return test_framework_run_cases("I2C_DEV", stub, 1U);
}

#else /* I2C_HAS_DEVICE && I2C_OPS — full implementation follows */

#define I2C_DEV_MODULE		"I2C_DEV"
#define I2C_DEV_ASYNC_TIMEOUT_US	100000U
#define I2C_DEV_ASYNC_STEP_US		1000U

/*
 * Delivery for the current pass: false = blocking (MXC_I3C_Controller_Transaction,
 * the no-DMA path); true = async (routes through DMA when a dma_handle is wired).
 * The read/write helpers consult this so one set of case logic covers both.
 */
static bool i2c_dev_async;
static volatile unsigned int i2c_dev_cb_count;
static volatile enum capi_i2c_async_event i2c_dev_cb_event;

static void i2c_dev_callback(enum capi_i2c_async_event event, void *arg,
			     int event_extra)
{
	(void)arg;
	(void)event_extra;
	i2c_dev_cb_event = event;
	i2c_dev_cb_count++;
}

/** @brief Wait for the async completion callback and map it to a return code. */
static int i2c_dev_wait_done(int start_ret)
{
	if (start_ret)
		return start_ret;
	TEST_WAIT_UNTIL(i2c_dev_cb_count > 0U, I2C_DEV_ASYNC_TIMEOUT_US,
			I2C_DEV_ASYNC_STEP_US);
	if (i2c_dev_cb_count == 0U)
		return -ETIMEDOUT;

	return (i2c_dev_cb_event == CAPI_I2C_XFR_DONE) ? 0 : -EIO;
}

/*
 * The controller handle is opened in every case; a failed assertion that
 * returned early would leak it into the next case (and a still-open controller
 * fails the next capi_i2c_init). CLEANUP releases it on every exit path and is
 * NULL-guarded so it is safe before init succeeds.
 */
#define CLEANUP \
	do { \
		if (i2c_handle != NULL) \
			(void)capi_i2c_deinit(i2c_handle); \
	} while (0)

/**
 * @brief Read a register block over standard I2C framing (write reg, read).
 * @param dev - CAPI I2C device (address already set).
 * @param reg - Starting register address.
 * @param buf - Destination buffer.
 * @param len - Number of bytes to read.
 * @return 0 on success, negative error code otherwise.
 */
static int i2c_dev_read_regs(struct capi_i2c_device *dev, uint8_t reg,
			     uint8_t *buf, uint32_t len)
{
	uint8_t sub = reg;
	struct capi_i2c_transfer xfer = {
		.buf = buf,
		.len = len,
		.sub_address = &sub,
		.sub_address_len = 1U,
		.no_stop = false,
	};

	if (i2c_dev_async) {
		i2c_dev_cb_count = 0U;
		i2c_dev_cb_event = CAPI_I2C_NONE;
		return i2c_dev_wait_done(capi_i2c_receive_async(dev, &xfer));
	}

	return capi_i2c_receive(dev, &xfer);
}

/**
 * @brief Write a single register over standard I2C framing.
 * @param dev - CAPI I2C device (address already set).
 * @param reg - Register address.
 * @param val - Value to write.
 * @return 0 on success, negative error code otherwise.
 */
static int i2c_dev_write_reg(struct capi_i2c_device *dev, uint8_t reg,
			     uint8_t val)
{
	uint8_t sub = reg;
	uint8_t data = val;
	struct capi_i2c_transfer xfer = {
		.buf = &data,
		.len = 1U,
		.sub_address = &sub,
		.sub_address_len = 1U,
		.no_stop = false,
	};

	if (i2c_dev_async) {
		i2c_dev_cb_count = 0U;
		i2c_dev_cb_event = CAPI_I2C_NONE;
		return i2c_dev_wait_done(capi_i2c_transmit_async(dev, &xfer));
	}

	return capi_i2c_transmit(dev, &xfer);
}

/**
 * @brief Locate the target by probing its candidate address(es).
 *
 * Reads the ID register at each candidate and accepts the first whose leading
 * ID byte matches the expected value, leaving dev->address set to it. This both
 * finds a strap-dependent address and confirms a real device answered (an empty
 * bus NACKs and the read returns an error).
 *
 * @param dev - CAPI I2C device; address is set on success.
 * @return 0 if the device was found, -ENODEV otherwise.
 */
static int i2c_dev_probe(struct capi_i2c_device *dev)
{
	static const uint8_t expected[] = I2C_DEV_ID_VALS;
	static const uint16_t candidates[] = {
		I2C_DEV_ADDR,
#ifdef I2C_DEV_ADDR_ALT
		I2C_DEV_ADDR_ALT,
#endif
	};
	uint8_t id = 0U;
	uint32_t i;

	for (i = 0U; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
		dev->address = candidates[i];
		if (i2c_dev_read_regs(dev, I2C_DEV_ID_REG, &id, 1U) == 0 &&
		    id == expected[0])
			return 0;
	}

	return -ENODEV;
}

/** @brief Build a device descriptor for the mapped target. */
static struct capi_i2c_device i2c_dev_make(struct capi_i2c_controller_handle *h)
{
	struct capi_i2c_device dev = {
		.controller = h,
		.address = I2C_DEV_ADDR,
		.b10addr = false,
		.speed = CAPI_I2C_SPEED_STANDARD,
		.duty_cycle = 0U,
		.clk_stretch = 0,
		.extra = NULL,
	};

	return dev;
}

/**
 * @brief Probe the device and verify its whole ID block.
 *
 * Finds the target address, then reads the ID block in one burst and asserts
 * every byte. A correct read proves controller addressing, the write-register /
 * repeated-start-read sequence, and real data integrity end to end.
 *
 * @return 0 on pass, negative error code on failure.
 */
static int i2c_dev_probe_id(void)
{
	static const uint8_t expected[] = I2C_DEV_ID_VALS;
	struct capi_i2c_controller_handle *i2c_handle = NULL;
	struct capi_i2c_device dev;
	uint8_t id[I2C_DEV_ID_LEN];
	uint32_t i;
	int ret;

	TEST_SECTION("PROBE_ID");
	ret = capi_i2c_init(&i2c_handle, &i2c_master_config);
	TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "INIT");
	TEST_ASSERT_EQ_OR_CLEANUP(capi_i2c_register_callback(i2c_handle,
				  i2c_dev_callback, NULL), 0, "REGISTER_CB");

	dev = i2c_dev_make(i2c_handle);
	TEST_ASSERT_EQ_OR_CLEANUP(i2c_dev_probe(&dev), 0, "PROBE");
	TEST_VALUE("I2C_DEV.address", dev.address);

	memset(id, 0, sizeof(id));
	TEST_ASSERT_EQ_OR_CLEANUP(i2c_dev_read_regs(&dev, I2C_DEV_ID_REG, id,
				  sizeof(id)), 0, "ID_READ");
	for (i = 0U; i < sizeof(id); i++)
		TEST_ASSERT_EQ_OR_CLEANUP(id[i], expected[i], "ID_MATCH");

	TEST_ASSERT_EQ_OR_CLEANUP(capi_i2c_deinit(i2c_handle), 0, "DEINIT");

	return 0;
}

/**
 * @brief Write a benign register and read it back.
 *
 * After optionally putting the device in a writable state (I2C_DEV_PREP_*),
 * reads the current value of the R/W register, writes a test value, reads it
 * back and asserts it stuck, then restores the original. Exercises the
 * controller transmit path against a real target that ACKs each byte.
 *
 * @return 0 on pass, negative error code on failure.
 */
static int i2c_dev_write_read(void)
{
	struct capi_i2c_controller_handle *i2c_handle = NULL;
	struct capi_i2c_device dev;
	uint8_t original = 0U;
	uint8_t readback = 0U;
	int ret;

	TEST_SECTION("WRITE_READ");
	ret = capi_i2c_init(&i2c_handle, &i2c_master_config);
	TEST_ASSERT_EQ_OR_CLEANUP(ret, 0, "INIT");
	TEST_ASSERT_EQ_OR_CLEANUP(capi_i2c_register_callback(i2c_handle,
				  i2c_dev_callback, NULL), 0, "REGISTER_CB");

	dev = i2c_dev_make(i2c_handle);
	TEST_ASSERT_EQ_OR_CLEANUP(i2c_dev_probe(&dev), 0, "PROBE");

#if defined(I2C_DEV_PREP_REG) && defined(I2C_DEV_PREP_VAL)
	/* Put the device in a state where config writes take effect. */
	TEST_ASSERT_EQ_OR_CLEANUP(i2c_dev_write_reg(&dev, I2C_DEV_PREP_REG,
				  I2C_DEV_PREP_VAL), 0, "PREP");
#ifdef I2C_DEV_PREP_WAIT_MS
	capi_wait_ms(I2C_DEV_PREP_WAIT_MS);
#endif
#endif

	/* Save, write a test value, read it back, then restore. */
	TEST_ASSERT_EQ_OR_CLEANUP(i2c_dev_read_regs(&dev, I2C_DEV_RW_REG,
				  &original, 1U), 0, "SAVE");
	TEST_ASSERT_EQ_OR_CLEANUP(i2c_dev_write_reg(&dev, I2C_DEV_RW_REG,
				  I2C_DEV_RW_TESTVAL), 0, "WRITE");
	TEST_ASSERT_EQ_OR_CLEANUP(i2c_dev_read_regs(&dev, I2C_DEV_RW_REG,
				  &readback, 1U), 0, "READBACK");
	TEST_ASSERT_EQ_OR_CLEANUP(readback, I2C_DEV_RW_TESTVAL, "WRITE_READ_MATCH");

	/* Restore; report but do not fail the case if restore alone fails. */
	TEST_ASSERT_EQ_OR_CLEANUP(i2c_dev_write_reg(&dev, I2C_DEV_RW_REG,
				  original), 0, "RESTORE");

	TEST_ASSERT_EQ_OR_CLEANUP(capi_i2c_deinit(i2c_handle), 0, "DEINIT");

	return 0;
}

#undef CLEANUP

static const struct test_case i2c_dev_subtests[] = {
	{ "PROBE_ID",   i2c_dev_probe_id,   false },
	{ "WRITE_READ", i2c_dev_write_read, false },
};

/**
 * @brief Exercise CAPI I2C controller mode against the mapped on-bus target.
 *
 * PROBE_ID finds the device and verifies its ID block (read path, addressing,
 * repeated-start register read). WRITE_READ round-trips a benign register (write
 * path). Both need a real target on the bus.
 *
 * The cases run once per delivery. The first pass is blocking (no DMA), using
 * MXC_I3C_Controller_Transaction. When DMA is available a second pass repeats
 * them through the async API with a DMA handle wired -- and because I2C DMA is
 * async-only, that is the only way to route transmit/receive through the DMA
 * path (_max_capi_i2c_transmit_dma / _receive_dma). On a build with no target
 * mapped the suite reports a single skipped case.
 *
 * API coverage:
 *   capi_i2c_init() / capi_i2c_deinit()
 *   capi_i2c_receive()  / capi_i2c_receive_async()   (register read)
 *   capi_i2c_transmit() / capi_i2c_transmit_async()  (register write)
 *   capi_i2c_register_callback()                     (async completion)
 *
 * @return 0 on pass, first non-zero subtest error across all passes otherwise.
 */
int test_i2c_device(void)
{
	const size_t n = sizeof(i2c_dev_subtests) / sizeof(i2c_dev_subtests[0]);
	int first_error = 0;
	int ret;

	/* Pass 1 - no DMA: blocking transfers (MXC_I3C_Controller_Transaction). */
	i2c_dev_async = false;
	i2c_master_config.dma_handle = NULL;
	ret = test_framework_run_cases(I2C_DEV_MODULE, i2c_dev_subtests, n);
	if (ret != 0 && first_error == 0)
		first_error = ret;

#if I2C_HAS_DMA
	/*
	 * Pass 2 - DMA: I2C DMA is reached only through the async API, so the same
	 * cases run async with a DMA handle wired. The handle is a singleton
	 * (capi_dma_init hands back the same one) and I2C deinit only tears down
	 * channels, so one init here serves every case across the pass.
	 */
	{
		static struct capi_dma_handle *i2c_dma_handle;

		if (i2c_dma_handle == NULL) {
			ret = capi_dma_init(&i2c_dma_handle, &dma_config);
			if (ret != 0)
				return first_error != 0 ? first_error : ret;
		}
		i2c_master_config.dma_handle = i2c_dma_handle;
	}
	i2c_dev_async = true;
	ret = test_framework_run_cases(I2C_DEV_MODULE "-DMA", i2c_dev_subtests, n);
	if (ret != 0 && first_error == 0)
		first_error = ret;

	/* Restore defaults. */
	i2c_master_config.dma_handle = NULL;
	i2c_dev_async = false;
#endif /* I2C_HAS_DMA */

	return first_error;
}

#endif /* I2C_HAS_DEVICE && I2C_OPS */
