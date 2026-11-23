/***************************************************************************//**
 * @file main.c
 * @brief Bench test for MAX32657 CAPI I2C reads longer than 255 bytes.
 *
 * Runs against the ADXL367 on the MAX32657EVKIT I2C0 bus (P0.0/P0.1). Reads
 * longer than the 8-bit term_rd count must go out as one message: one START,
 * the bytes, a NACK on the last byte and a STOP, with no repeated START in
 * between. The test checks that from three sides:
 *
 * - The data: a long read must return the same byte stream as a series of
 *   short reads that continue from the ADXL367 register pointer.
 * - The byte count on the controller side: the driver returns -EOVERFLOW if
 *   the controller clocked in more bytes than requested.
 * - The byte count on the target side: a short read after the long one must
 *   continue at the next byte of the stream.
 *
 * A raw probe also measures the two hardware facts the driver relies on: the
 * controller holds SCL once its receive FIFO is full, and after term_rd = 1
 * it clocks in exactly MAX_CAPI_I2C_TERM_TAIL more bytes.
 *
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 ******************************************************************************/

#include <errno.h>
#include <malloc.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "capi_dma.h"
#include "capi_i2c.h"
#include "capi_irq.h"
#include "capi_platform.h"
#include "capi_time.h"
#include "capi_timer.h"
#include "capi_uart.h"
#include "maxim_capi_dma.h"
#include "maxim_capi_i2c.h"
#include "maxim_capi_i2c_priv.h"
#include "maxim_capi_timer.h"
#include "maxim_capi_uart.h"
#include "mxc_device.h"
#include "i3c.h"

/*
 * Constants of the long-read path. The fallbacks let the test also build
 * against a driver without it, to show how such a driver behaves.
 */
#ifdef MAX_CAPI_I2C_TERM_TAIL
#define DRIVER_LONG_READ	"yes"
#define RX_FIFO_DEPTH		MAX_CAPI_I2C_FIFO_DEPTH
#else
#define DRIVER_LONG_READ	"no"
#define RX_FIFO_DEPTH		8U
#define MAX_CAPI_I2C_TERM_RD_MAX	255U
#define MAX_CAPI_I2C_TERM_TAIL		1U
#endif

/* Console: UART0 is the J-Link VCOM (/dev/ttyACM0). */
#define CONSOLE_UART_IDENTIFIER	0U
#define CONSOLE_BAUDRATE	115200U

/* I2C0 is the I3C peripheral in I2C mode, on P0.0 (SCL) and P0.1 (SDA). */
#define BUS_I2C_IDENTIFIER	0U
#define ADXL367_ADDR		0x53U
#define ADXL367_REG_DEVID_AD	0x00U
#define ADXL367_DEVID_AD	0xADU
#define ADXL367_DEVID_MST	0x1DU
#define ADXL367_REG_SOFT_RESET	0x1FU
#define ADXL367_SOFT_RESET_KEY	0x52U
/* Nothing answers at this address on the EVKIT. */
#define ABSENT_ADDR		0x2AU

/* Register the long reads start at. */
#define STREAM_START		0x00U
#define MAX_READ_LEN		1024U
/* Bytes read after a long read to find where the target pointer stopped. */
#define CONT_LEN		4U
#define REF_LEN			(MAX_READ_LEN + CONT_LEN + 16U)
/* Short reads used to build the reference stream (term_rd = len). */
#define REF_CHUNK		200U
#define REF_CAPTURES		4U
#define ITERATIONS		20U

/* Long write: zeros from THRESH_ACT_H on, then a soft reset to undo it. */
#define WRITE_START_REG		0x20U
#define WRITE_LEN		300U
/* Registers compared after the soft reset */
#define WRITE_CHECK_LEN		0x50U

/* Read-back check: THRESH_ACT_H .. TIME_INACT_L, harmless in standby */
#define RW_REG			0x20U
#define RW_LEN			7U

/* Interrupt load for the stress cases: TMR1, counting at about 1 MHz. */
#define STRESS_TIMER_IDENTIFIER	1U
#define STRESS_TIMER_HZ		1000000U

#define ARRAY_LEN(a)		(sizeof(a) / sizeof((a)[0]))

static struct capi_uart_handle *console;
static struct capi_i2c_controller_handle *i2c;
/* DMA1 (secure); async transfers use it while the controller holds it. */
static struct capi_dma_handle *dma;
static bool dma_mode;
static struct capi_i2c_device dev = {
	.address = ADXL367_ADDR,
	.speed = CAPI_I2C_SPEED_STANDARD,
};

/* Byte stream of the ADXL367 starting at STREAM_START. */
static uint8_t ref[REF_LEN];
/* 1 where the reference byte read the same in every capture. */
static uint8_t stable[REF_LEN];
static uint8_t scratch[REF_LEN];
static uint8_t buf[MAX_READ_LEN];

static uint32_t n_pass, n_fail;

static struct capi_timer_handle *stress_timer;
static volatile uint32_t stress_busy_ticks;
static volatile uint32_t stress_hits;

static int console_init(void)
{
	static struct capi_uart_line_config line_config = {
		.baudrate = CONSOLE_BAUDRATE,
		.size = CAPI_UART_DATA_BITS_8,
		.parity = CAPI_UART_PARITY_NONE,
		.stop_bits = CAPI_UART_STOP_1_BIT,
		.flow_control = CAPI_UART_FLOW_CONTROL_NONE,
		.address_mode = CAPI_UART_ADDRESS_MODE_DISABLED,
	};
	static struct max_capi_uart_extra extra = { .use_irq = false };
	const struct capi_uart_config config = {
		.identifier = CONSOLE_UART_IDENTIFIER,
		.line_config = &line_config,
		.extra = &extra,
		.ops = &max_capi_uart_ops,
	};

	int ret;

	ret = capi_uart_init(&console, &config);
	if (ret)
		return ret;

	return capi_stdio_init(console);
}

static void result(bool ok)
{
	if (ok)
		n_pass++;
	else
		n_fail++;

	printf(" -> %s\r\n", ok ? "PASS" : "FAIL");
}

static uint64_t now_us(void)
{
	uint64_t us = 0;

	capi_uptime(&us);

	return us;
}

/* Register address, repeated START, read: the SMBus block read shape. */
static int read_reg(uint16_t addr, uint8_t reg, uint8_t *data, uint32_t len)
{
	struct capi_i2c_transfer xfer = {
		.sub_address = &reg,
		.sub_address_len = 1,
		.buf = data,
		.target_addr = addr,
		.len = len,
	};

	return capi_i2c_receive(&dev, &xfer);
}

/* Read that continues from the target's register pointer. */
static int read_plain(uint8_t *data, uint32_t len)
{
	struct capi_i2c_transfer xfer = { .buf = data, .len = len };

	return capi_i2c_receive(&dev, &xfer);
}

static int set_pointer(uint8_t reg)
{
	struct capi_i2c_transfer xfer = { .buf = &reg, .len = 1 };

	return capi_i2c_transmit(&dev, &xfer);
}

static bool devid_ok(void)
{
	uint8_t id = 0;

	return !read_reg(ADXL367_ADDR, ADXL367_REG_DEVID_AD, &id, 1) &&
	       id == ADXL367_DEVID_AD;
}

/* Read @len bytes of the stream with short reads only. */
static int capture_stream(uint8_t *out, uint32_t len)
{
	uint32_t chunk = len < REF_CHUNK ? len : REF_CHUNK;
	uint32_t off;
	int ret;

	ret = read_reg(ADXL367_ADDR, STREAM_START, out, chunk);
	for (off = chunk; !ret && off < len; off += chunk) {
		chunk = len - off < REF_CHUNK ? len - off : REF_CHUNK;
		ret = read_plain(out + off, chunk);
	}

	return ret;
}

/* Index of the first byte that differs from the stream at @offset, or -1. */
static int32_t first_mismatch(const uint8_t *data, uint32_t offset,
			      uint32_t len)
{
	for (uint32_t i = 0; i < len; i++)
		if (stable[offset + i] && data[i] != ref[offset + i])
			return i;

	return -1;
}

/* Whether CONT_LEN bytes at @off tell @off apart from @off - 1 and @off + 1. */
static bool count_check_exact(uint32_t off)
{
	bool lo = false, hi = false;

	for (uint32_t i = 0; i < CONT_LEN; i++) {
		if (ref[off + i] != ref[off - 1 + i])
			lo = true;
		if (ref[off + i] != ref[off + 1 + i])
			hi = true;
	}

	return lo && hi;
}

static bool test_continuation(void)
{
	uint8_t whole[8], split[8];
	bool ok;

	ok = !read_reg(ADXL367_ADDR, STREAM_START, whole, sizeof(whole)) &&
	     !read_reg(ADXL367_ADDR, STREAM_START, split, 4) &&
	     !read_plain(split + 4, 4) && !memcmp(whole, split, sizeof(whole));

	printf("continuation: 8 bytes read whole and as 4 + 4");
	result(ok);

	return ok;
}

static int write_rw_regs(const uint8_t *data);
static int soft_reset(void);

/* Mark the reference bytes that differ in a fresh capture as unstable. */
static bool mark_unstable(uint32_t *unstable)
{
	if (capture_stream(scratch, REF_LEN))
		return false;

	for (uint32_t i = 0; i < REF_LEN; i++) {
		if (stable[i] && scratch[i] != ref[i]) {
			stable[i] = 0;
			(*unstable)++;
		}
	}

	return true;
}

static bool build_reference(void)
{
	uint32_t unstable = 0;
	uint32_t period = 0;

	memset(stable, 1, sizeof(stable));
	if (capture_stream(ref, REF_LEN))
		goto fail;

	for (uint32_t k = 1; k < REF_CAPTURES; k++)
		if (!mark_unstable(&unstable))
			goto fail;

	/*
	 * Status bits that follow the bench's own traffic (the ADXL367 sets one
	 * on a soft reset and clears it on any register write) are not data.
	 */
	{
		uint8_t zeros[RW_LEN] = { 0 };

		if (write_rw_regs(zeros) || !mark_unstable(&unstable) ||
		    soft_reset() || !mark_unstable(&unstable))
			goto fail;
	}

	/* Smallest repeat of the stream, to show how the pointer moves. */
	for (uint32_t p = 1; p < REF_LEN / 2 && !period; p++) {
		bool repeats = true;

		for (uint32_t i = 0; i + p < REF_LEN && repeats; i++)
			if (stable[i] && stable[i + p] && ref[i] != ref[i + p])
				repeats = false;

		if (repeats)
			period = p;
	}

	printf("reference: %u bytes from 0x%02X, %lu bytes change between reads, ",
	       REF_LEN, STREAM_START, (unsigned long)unstable);
	if (period)
		printf("repeats every %lu bytes\r\n", (unsigned long)period);
	else
		printf("no repeat\r\n");

	printf("first bytes:");
	for (uint32_t i = 0; i < 32; i++)
		printf(" %02X", ref[i]);
	printf("\r\n");

	return true;

fail:
	printf("reference: capture failed\r\n");
	n_fail++;

	return false;
}

/*
 * Start a read with term_rd = 0, leave the FIFO unread, then end the read with
 * term_rd = 1 and count every byte the controller clocked in.
 */
static bool test_probe(void)
{
	mxc_i3c_regs_t *i3c = MXC_I3C_GET_I3C(BUS_I2C_IDENTIFIER);
	uint8_t reg = STREAM_START;
	uint8_t data[32], cont[CONT_LEN];
	uint32_t lvl_early, lvl_late, errwarn_stall, errwarn_end;
	uint32_t total = 0;
	uint64_t deadline;
	bool done = false, ok;
	int ret;

	MXC_I3C_ClearTXFIFO(i3c);
	MXC_I3C_ClearRXFIFO(i3c);

	ret = MXC_I3C_EmitStart(i3c, true, MXC_I3C_TRANSFER_TYPE_WRITE,
				ADXL367_ADDR, 0);
	if (!ret && MXC_I3C_WriteTXFIFO(i3c, &reg, 1, true, 10000) != 1)
		ret = -1;

	deadline = now_us() + 1000;
	while (!ret && !(i3c->cont_status & MXC_F_I3C_CONT_STATUS_DONE))
		if (now_us() > deadline)
			ret = -1;

	if (!ret)
		ret = MXC_I3C_EmitStart(i3c, true, MXC_I3C_TRANSFER_TYPE_READ,
					ADXL367_ADDR, 0);
	if (ret) {
		MXC_I3C_EmitI2CStop(i3c);
		printf("probe: could not start the read (%d)", ret);
		result(false);
		return false;
	}

	/* Do not read: the FIFO fills and the controller should hold SCL. */
	capi_wait_us(1000);
	lvl_early = MXC_I3C_Controller_GetRXCount(i3c);
	capi_wait_us(500);
	lvl_late = MXC_I3C_Controller_GetRXCount(i3c);
	errwarn_stall = i3c->cont_errwarn;
	i3c->cont_errwarn = MXC_F_I3C_CONT_ERRWARN_TO;

	/* NACK the next byte, then take everything that arrives. */
	i3c->cont_ctrl1 = (i3c->cont_ctrl1 & ~(MXC_F_I3C_CONT_CTRL1_REQ |
					       MXC_F_I3C_CONT_CTRL1_TERM_RD)) |
			  MXC_S_I3C_CONT_CTRL1_REQ_NONE |
			  (1U << MXC_F_I3C_CONT_CTRL1_TERM_RD_POS);

	deadline = now_us() + 5000;
	while (now_us() < deadline && total < sizeof(data)) {
		uint32_t status = i3c->cont_status;

		if (MXC_I3C_Controller_GetRXCount(i3c)) {
			data[total++] = (uint8_t)i3c->cont_rxfifo8;
			continue;
		}

		if (status & MXC_F_I3C_CONT_STATUS_DONE) {
			done = true;
			break;
		}
	}
	errwarn_end = i3c->cont_errwarn;
	MXC_I3C_EmitI2CStop(i3c);

	ret = read_plain(cont, CONT_LEN);

	printf("probe: FIFO %lu bytes after 1 ms, %lu after 1.5 ms (%s), timeout warning %s\r\n",
	       (unsigned long)lvl_early, (unsigned long)lvl_late,
	       lvl_early == lvl_late ? "SCL held" : "still running",
	       (errwarn_stall & MXC_F_I3C_CONT_ERRWARN_TO) ? "set" : "clear");
	printf("       %lu bytes in total, %s; tail %ld (driver assumes %u), errwarn 0x%08lX\r\n",
	       (unsigned long)total, done ? "done" : "NOT DONE",
	       (long)total - (long)lvl_late, MAX_CAPI_I2C_TERM_TAIL,
	       (unsigned long)errwarn_end);
	printf("       data %s, next read %s",
	       first_mismatch(data, 0, total) < 0 ? "matches" : "DIFFERS",
	       ret ? "FAILED" :
	       !count_check_exact(total) ? "inconclusive" :
	       first_mismatch(cont, total, CONT_LEN) < 0 ?
	       "continues at the next byte" : "is OFF the next byte");

	ok = done && lvl_early == lvl_late &&
	     lvl_late == RX_FIFO_DEPTH &&
	     total == lvl_late + MAX_CAPI_I2C_TERM_TAIL &&
	     !(errwarn_end & ~MXC_F_I3C_CONT_ERRWARN_TO) &&
	     first_mismatch(data, 0, total) < 0 && !ret &&
	     (!count_check_exact(total) ||
	      first_mismatch(cont, total, CONT_LEN) < 0) && devid_ok();
	result(ok);

	return ok;
}

static bool test_read(bool sub_address, uint32_t len, uint32_t iterations)
{
	uint32_t errors = 0, bad_data = 0, bad_count = 0, bad_bus = 0;
	uint32_t good = 0;
	int first_err = 0;
	int32_t first_bad = -1;
	uint64_t worst = 0;
	bool exact = count_check_exact(len);
	uint8_t cont[CONT_LEN];
	bool ok;
	int ret;

	for (uint32_t it = 0; it < iterations; it++) {
		uint32_t failures = errors + bad_data + bad_count + bad_bus;
		uint64_t t0, t1;
		int32_t m;

		/* Any byte the read does not write fails the compare. */
		for (uint32_t i = 0; i < len; i++)
			buf[i] = ~ref[i];

		if (!sub_address && set_pointer(STREAM_START)) {
			bad_bus++;
			continue;
		}

		t0 = now_us();
		ret = sub_address ? read_reg(ADXL367_ADDR, STREAM_START, buf, len) :
		      read_plain(buf, len);
		t1 = now_us();
		if (t1 - t0 > worst)
			worst = t1 - t0;

		if (ret) {
			if (!errors)
				first_err = ret;
			errors++;
		} else {
			m = first_mismatch(buf, 0, len);
			if (m >= 0) {
				if (!bad_data)
					first_bad = m;
				bad_data++;
			}

			if (read_plain(cont, CONT_LEN))
				bad_bus++;
			else if (exact && first_mismatch(cont, len, CONT_LEN) >= 0)
				bad_count++;
		}

		if (!devid_ok())
			bad_bus++;

		if (errors + bad_data + bad_count + bad_bus == failures)
			good++;
	}

	ok = !errors && !bad_data && !bad_count && !bad_bus;
	printf("  %-4s %4lu B: %2lu/%lu ok, %lu us max",
	       sub_address ? "Sr" : "cont", (unsigned long)len,
	       (unsigned long)good, (unsigned long)iterations, (unsigned long)worst);
	if (errors)
		printf(", %lu errors (first %d)", (unsigned long)errors,
		       first_err);
	if (bad_data)
		printf(", %lu bad data (first at byte %ld)",
		       (unsigned long)bad_data, (long)first_bad);
	if (bad_count)
		printf(", %lu wrong byte count", (unsigned long)bad_count);
	if (bad_bus)
		printf(", %lu bus checks failed", (unsigned long)bad_bus);
	if (!exact)
		printf(" (count check inconclusive here)");
	result(ok);

	return ok;
}

static bool bus_idle(void)
{
	mxc_i3c_regs_t *i3c = MXC_I3C_GET_I3C(BUS_I2C_IDENTIFIER);

	return (i3c->cont_status & MXC_F_I3C_CONT_STATUS_STATE) ==
	       MXC_S_I3C_CONT_STATUS_STATE_IDLE;
}

static bool test_absent(void)
{
	bool ok;
	int ret;

	uint8_t reg = WRITE_START_REG;
	struct capi_i2c_transfer wr = {
		.sub_address = &reg,
		.sub_address_len = 1,
		.buf = buf,
		.target_addr = ABSENT_ADDR,
		.len = 300,
	};
	/* No register address: the NACK comes on the read address */
	struct capi_i2c_transfer rd_cont = {
		.buf = buf,
		.target_addr = ABSENT_ADDR,
		.len = 300,
	};
	int ret_wr, ret_cont;

	ret = read_reg(ABSENT_ADDR, STREAM_START, buf, 300);
	ret_cont = capi_i2c_receive(&dev, &rd_cont);
	ret_wr = capi_i2c_transmit(&dev, &wr);
	ok = (ret == -EIO) && (ret_cont == -EIO) && (ret_wr == -EIO) &&
	     bus_idle() && devid_ok();
	printf("  absent target 0x%02X, 300 B read Sr %d, cont %d, write %d (expect %d), bus %s",
	       ABSENT_ADDR, ret, ret_cont, ret_wr, -EIO,
	       devid_ok() ? "ok" : "STUCK");
	result(ok);

	return ok;
}

/*
 * Write @len zeros as one message (register address, then the data), then
 * restore the defaults with a soft reset. A write that asks for a STOP must
 * leave the controller idle when it returns; a missing STOP leaves it in the
 * middle of the message.
 */
static bool test_write(uint32_t len)
{
	mxc_i3c_regs_t *i3c = MXC_I3C_GET_I3C(BUS_I2C_IDENTIFIER);
	uint32_t state, errwarn;
	static uint8_t data[WRITE_LEN];
	uint8_t reg = WRITE_START_REG;
	uint8_t rst[2] = { ADXL367_REG_SOFT_RESET, ADXL367_SOFT_RESET_KEY };
	struct capi_i2c_transfer xfer = {
		.sub_address = &reg,
		.sub_address_len = 1,
		.buf = data,
		.len = len,
	};
	struct capi_i2c_transfer reset = { .buf = rst, .len = sizeof(rst) };
	uint64_t t0, t1;
	int ret, ret_rst, ret_chk;
	bool restored, ok;

	memset(data, 0, sizeof(data));

	t0 = now_us();
	ret = capi_i2c_transmit(&dev, &xfer);
	t1 = now_us();
	state = i3c->cont_status & MXC_F_I3C_CONT_STATUS_STATE;
	errwarn = i3c->cont_errwarn;

	ret_rst = capi_i2c_transmit(&dev, &reset);
	capi_wait_ms(50);

	ret_chk = capture_stream(scratch, WRITE_CHECK_LEN);
	restored = !ret_chk && first_mismatch(scratch, 0, WRITE_CHECK_LEN) < 0;

	printf("  write %4lu B from 0x%02X: %d, %lu us, %s (state %lu, errwarn 0x%05lX); reset %d, regs %s",
	       (unsigned long)len, WRITE_START_REG, ret,
	       (unsigned long)(t1 - t0),
	       state == MXC_S_I3C_CONT_STATUS_STATE_IDLE ? "STOP sent" :
	       "NO STOP", (unsigned long)state, (unsigned long)errwarn,
	       ret_rst,
	       ret_chk ? "unreadable" : restored ? "restored" :
	       "NOT RESTORED");
	ok = !ret && !ret_rst && restored &&
	     state == MXC_S_I3C_CONT_STATUS_STATE_IDLE;
	result(ok);

	return ok;
}

static int soft_reset(void)
{
	uint8_t rst[2] = { ADXL367_REG_SOFT_RESET, ADXL367_SOFT_RESET_KEY };
	struct capi_i2c_transfer xfer = { .buf = rst, .len = sizeof(rst) };
	int ret;

	ret = capi_i2c_transmit(&dev, &xfer);
	capi_wait_ms(50);

	return ret;
}

static int write_rw_regs(const uint8_t *data)
{
	uint8_t reg = RW_REG;
	struct capi_i2c_transfer xfer = {
		.sub_address = &reg,
		.sub_address_len = 1,
		.buf = (uint8_t *)data,
		.len = RW_LEN,
	};

	return capi_i2c_transmit(&dev, &xfer);
}

/* Whether the writable bits of the RW registers read back as @expect. */
static bool rw_regs_match(const uint8_t *expect, const uint8_t *mask)
{
	uint8_t rd[RW_LEN];

	if (read_reg(ADXL367_ADDR, RW_REG, rd, RW_LEN))
		return false;

	for (uint32_t i = 0; i < RW_LEN; i++)
		if ((rd[i] ^ expect[i]) & mask[i])
			return false;

	return true;
}

static const uint8_t rw_pattern[RW_LEN] = {
	0x5A, 0xC3, 0x96, 0x3C, 0xA5, 0x69, 0x0F
};

/* Bits of the RW registers that read back as 1 after writing 0xFF. */
static bool read_rw_mask(uint8_t *mask)
{
	uint8_t ones[RW_LEN];
	bool writable = false;

	memset(ones, 0xFF, sizeof(ones));
	if (write_rw_regs(ones) || read_reg(ADXL367_ADDR, RW_REG, mask, RW_LEN))
		return false;

	for (uint32_t i = 0; i < RW_LEN; i++)
		if (mask[i])
			writable = true;

	return writable;
}

/*
 * Write a pattern to RW_REG.. in the shapes callers use and read it back:
 * 1-byte register address + data, 2-byte "address" (register + first data
 * byte) + rest, one plain buffer holding register and data, and a register
 * write with no STOP followed by a read that starts with a repeated START.
 * Only bits that read back as 1 after writing 0xFF are compared.
 */
static bool test_write_forms(void)
{
	const uint8_t *pat = rw_pattern;
	uint8_t zeros[RW_LEN], mask[RW_LEN], rd[RW_LEN];
	uint8_t frame[RW_LEN + 1];
	uint8_t sub2[2] = { RW_REG, rw_pattern[0] };
	uint8_t reg = RW_REG;
	struct capi_i2c_transfer two = {
		.sub_address = sub2,
		.sub_address_len = 2,
		.buf = (uint8_t *)&rw_pattern[1],
		.len = RW_LEN - 1,
	};
	struct capi_i2c_transfer plain = { .buf = frame, .len = sizeof(frame) };
	struct capi_i2c_transfer addr_only = {
		.buf = &reg,
		.len = 1,
		.no_stop = true,
	};
	struct capi_i2c_transfer rd_sr = { .buf = rd, .len = RW_LEN };
	mxc_i3c_regs_t *i3c = MXC_I3C_GET_I3C(BUS_I2C_IDENTIFIER);
	bool f1, f2, f3, f4 = false, writable, idle;
	int ret;

	memset(zeros, 0, sizeof(zeros));
	frame[0] = RW_REG;
	memcpy(&frame[1], pat, RW_LEN);

	writable = read_rw_mask(mask);

	f1 = !write_rw_regs(zeros) && !write_rw_regs(pat) &&
	     rw_regs_match(pat, mask);
	f2 = !write_rw_regs(zeros) && !capi_i2c_transmit(&dev, &two) &&
	     rw_regs_match(pat, mask);
	f3 = !write_rw_regs(zeros) && !capi_i2c_transmit(&dev, &plain) &&
	     rw_regs_match(pat, mask);

	/* Register write without STOP, then a read after a repeated START */
	ret = write_rw_regs(pat);
	if (!ret)
		ret = capi_i2c_transmit(&dev, &addr_only);
	if (!ret)
		ret = capi_i2c_receive(&dev, &rd_sr);
	idle = (i3c->cont_status & MXC_F_I3C_CONT_STATUS_STATE) ==
	       MXC_S_I3C_CONT_STATUS_STATE_IDLE;
	if (!ret) {
		f4 = idle;
		for (uint32_t i = 0; i < RW_LEN; i++)
			if ((rd[i] ^ pat[i]) & mask[i])
				f4 = false;
	}

	ret = soft_reset();

	printf("  write forms (mask");
	for (uint32_t i = 0; i < RW_LEN; i++)
		printf(" %02X", mask[i]);
	printf("): addr+data %s, 2-byte addr %s, plain %s, no-STOP+Sr read %s, reset %d",
	       f1 ? "ok" : "BAD", f2 ? "ok" : "BAD", f3 ? "ok" : "BAD",
	       f4 ? "ok" : "BAD", ret);
	result(writable && f1 && f2 && f3 && f4 && !ret);

	return writable && f1 && f2 && f3 && f4 && !ret;
}

static volatile bool async_done;
static volatile int async_result;

static void async_cb(enum capi_i2c_async_event event, void *arg, int extra)
{
	(void)arg;

	if (event == CAPI_I2C_XFR_DONE) {
		async_result = extra;
		async_done = true;
	}
}

/* Start an async transfer and wait for its completion callback. */
static int run_async(bool transmit, struct capi_i2c_transfer *xfer)
{
	uint64_t end;
	int ret;

	async_done = false;
	ret = transmit ? capi_i2c_transmit_async(&dev, xfer) :
	      capi_i2c_receive_async(&dev, xfer);
	if (ret)
		return ret;

	end = now_us() + 500000U;
	while (!async_done)
		if (now_us() > end)
			return -ETIMEDOUT;

	return async_result;
}

/* Put the bus back if an async case left a message open. */
static bool async_settle(void)
{
	bool idle = bus_idle();

	if (!idle)
		capi_i2c_recover_bus(i2c);

	return idle;
}

/* Blocking read an async case is compared with. */
static uint8_t async_expect[MAX_READ_LEN];

/* Index of the first byte of buf that differs from async_expect, or -1. */
static int32_t async_mismatch(uint32_t len, uint32_t *nbad)
{
	int32_t bad = -1;

	*nbad = 0;
	for (uint32_t i = 0; i < len; i++) {
		if (buf[i] != async_expect[i]) {
			if (bad < 0)
				bad = i;
			(*nbad)++;
		}
	}

	return bad;
}

static void async_print_data(int32_t bad, uint32_t nbad, uint32_t len)
{
	if (bad < 0) {
		printf("ok");
		return;
	}

	printf("BAD from byte %ld (%lu bad):", (long)bad, (unsigned long)nbad);
	for (uint32_t i = bad; i < len && i < (uint32_t)bad + 6; i++)
		printf(" %02X/%02X", buf[i], async_expect[i]);
}

/*
 * Register address an async read may start with: STREAM_START, then zeros
 * that the target writes to the read-only registers after it (0x00..0x17 on
 * the ADXL367), so a read after an n-byte address starts at register n - 1.
 * Longer than the FIFO, it makes the driver refill the transmit FIFO.
 */
#define ASYNC_SUB_MAX		12U
static uint8_t async_sub[ASYNC_SUB_MAX] = { STREAM_START };

/* Times each async read case runs: the races it covers are timing dependent */
#define ASYNC_ITERATIONS	5U

/* Stream offset a read after a @sub_len-byte register address starts at. */
static uint32_t async_offset(uint32_t sub_len)
{
	return sub_len ? STREAM_START + sub_len - 1 : STREAM_START;
}

/*
 * Blocking read of @len bytes into async_expect, in the shape of an async case:
 * after a @sub_len-byte register address, or for @sub_len = 0 from the pointer
 * set by a blocking write, which is set again afterwards for the async read.
 */
static int async_reference(uint32_t sub_len, uint32_t len)
{
	struct capi_i2c_transfer xfer = {
		.sub_address = sub_len ? async_sub : NULL,
		.sub_address_len = sub_len,
		.buf = async_expect,
		.len = len,
	};
	int ret;

	if (!sub_len && set_pointer(STREAM_START))
		return -EIO;

	ret = capi_i2c_receive(&dev, &xfer);
	if (!ret && !sub_len)
		ret = set_pointer(STREAM_START);

	return ret;
}

static void async_prefill(uint32_t len)
{
	/* Any byte the read does not write fails the compare. */
	for (uint32_t i = 0; i < len; i++)
		buf[i] = ~async_expect[i];
}

/*
 * Check where the target's register pointer stopped after a read of @len bytes
 * from stream offset @off: a short read must continue at @off + @len, which
 * shows the target sent exactly @len bytes. Must run before anything else
 * moves the pointer.
 */
static const char *async_count(uint32_t off, uint32_t len, bool *ok)
{
	uint8_t cont[CONT_LEN];

	*ok = false;
	if (read_plain(cont, CONT_LEN))
		return "next read FAILED";

	*ok = true;
	if (!count_check_exact(off + len))
		return "inconclusive here";
	if (first_mismatch(cont, off + len, CONT_LEN) < 0)
		return "ok";

	*ok = false;

	return "WRONG";
}

/*
 * Async read of @len bytes, ASYNC_ITERATIONS times: after a @sub_len-byte
 * register address and a repeated START, or for @sub_len = 0 from the pointer
 * set by a blocking write. The expected data is a blocking read of the same
 * shape just before each one, so a change in the device's own registers does
 * not count as an async error.
 */
static bool async_case_read(uint32_t sub_len, uint32_t len)
{
	struct capi_i2c_transfer xfer = {
		.sub_address = sub_len ? async_sub : NULL,
		.sub_address_len = sub_len,
		.buf = buf,
		.len = len,
	};
	uint32_t off = async_offset(sub_len);
	uint32_t good = 0, n_ref = 0, n_err = 0, n_stop = 0, n_count = 0;
	uint32_t n_data = 0, n_bus = 0;
	int first_ref = 0, first_err = 0;
	bool exact = count_check_exact(off + len);
	bool ok;

	for (uint32_t it = 0; it < ASYNC_ITERATIONS; it++) {
		uint32_t failures = n_ref + n_err + n_stop + n_count + n_data +
				    n_bus;
		const char *count = "";
		bool count_ok = true;
		uint32_t nbad;
		int32_t bad;
		int ret;

		ret = async_reference(sub_len, len);
		if (ret) {
			if (!n_ref)
				first_ref = ret;
			n_ref++;
			continue;
		}

		async_prefill(len);
		ret = run_async(false, &xfer);

		if (!async_settle()) {
			n_stop++;
		} else if (!ret) {
			count = async_count(off, len, &count_ok);
			if (!count_ok) {
				if (!strcmp(count, "WRONG"))
					n_count++;
				else
					n_bus++;
			}
		}

		if (ret) {
			if (!n_err)
				first_err = ret;
			n_err++;
		} else {
			bad = async_mismatch(len, &nbad);
			if (bad >= 0) {
				if (!n_data) {
					printf("    iteration %lu: data ",
					       (unsigned long)it);
					async_print_data(bad, nbad, len);
					printf("\r\n");
				}
				n_data++;
			}
		}

		if (!devid_ok())
			n_bus++;

		if (n_ref + n_err + n_stop + n_count + n_data + n_bus == failures)
			good++;
	}

	ok = good == ASYNC_ITERATIONS;
	if (sub_len)
		printf("  async read  Sr/%-2lu %4lu B: ", (unsigned long)sub_len,
		       (unsigned long)len);
	else
		printf("  async read  cont  %4lu B: ", (unsigned long)len);
	printf("%lu/%u ok", (unsigned long)good, ASYNC_ITERATIONS);
	if (n_ref)
		printf(", %lu blocking reference reads failed (first %d)",
		       (unsigned long)n_ref, first_ref);
	if (n_err)
		printf(", %lu errors (first %d)", (unsigned long)n_err, first_err);
	if (n_stop)
		printf(", %lu NO STOP", (unsigned long)n_stop);
	if (n_data)
		printf(", %lu bad data", (unsigned long)n_data);
	if (n_count)
		printf(", %lu wrong byte count", (unsigned long)n_count);
	if (n_bus)
		printf(", %lu bus checks failed", (unsigned long)n_bus);
	if (!exact)
		printf(" (count check inconclusive here)");
	result(ok);

	return ok;
}

/* Reads chained from the completion callback in async_case_chain(). */
#define CHAIN_LEN		4U

static struct capi_i2c_transfer chain_xfer;
static volatile uint32_t chain_left;
static volatile uint32_t chain_bad;

/* Completion callback that checks a chained read and starts the next one. */
static void chain_cb(enum capi_i2c_async_event event, void *arg, int extra)
{
	int ret;

	(void)arg;

	if (event != CAPI_I2C_XFR_DONE)
		return;

	if (!extra && memcmp(buf, async_expect, chain_xfer.len))
		chain_bad++;

	if (extra || !--chain_left) {
		async_result = extra;
		async_done = true;
		return;
	}

	async_prefill(chain_xfer.len);
	ret = capi_i2c_receive_async(&dev, &chain_xfer);
	if (ret) {
		async_result = ret;
		async_done = true;
	}
}

/*
 * CHAIN_LEN async reads of @len bytes after the register address, each one
 * started from the completion callback of the one before. Both the FIFO and
 * the DMA path reset their state before they call back, so this must work.
 */
static bool async_case_chain(uint32_t len)
{
	uint64_t end;
	const char *count = "not checked";
	bool count_ok = false, idle, ok;
	int ret;

	chain_xfer = (struct capi_i2c_transfer) {
		.sub_address = async_sub,
		.sub_address_len = 1,
		.buf = buf,
		.len = len,
	};

	ret = async_reference(1, len);
	if (ret) {
		printf("  async chain %lu x %4lu B: blocking reference read %d",
		       (unsigned long)CHAIN_LEN, (unsigned long)len, ret);
		result(false);
		return false;
	}

	async_prefill(len);
	chain_left = CHAIN_LEN;
	chain_bad = 0;
	async_done = false;

	capi_i2c_register_callback(i2c, chain_cb, NULL);
	ret = capi_i2c_receive_async(&dev, &chain_xfer);
	if (!ret) {
		end = now_us() + 500000U * CHAIN_LEN;
		while (!async_done && now_us() <= end)
			;
		ret = async_done ? async_result : -ETIMEDOUT;
	}
	capi_i2c_register_callback(i2c, async_cb, NULL);

	idle = async_settle();
	if (!ret && idle)
		count = async_count(async_offset(1), len, &count_ok);

	ok = !ret && !chain_left && !chain_bad && idle && count_ok &&
	     devid_ok();
	printf("  async chain %lu x %4lu B from the callback: %d, %lu done, %lu bad data, %s, count %s",
	       (unsigned long)CHAIN_LEN, (unsigned long)len, ret,
	       (unsigned long)(CHAIN_LEN - chain_left),
	       (unsigned long)chain_bad, idle ? "STOP" : "NO STOP", count);
	result(ok);

	return ok;
}

/*
 * A second async read or write while one is in flight must fail with -EBUSY
 * and leave the first one alone. The second transfer only touches read-only
 * registers in case it does go out.
 */
static bool async_case_busy(void)
{
	struct capi_i2c_transfer xfer = {
		.sub_address = async_sub,
		.sub_address_len = 1,
		.buf = buf,
		.len = MAX_READ_LEN,
	};
	struct capi_i2c_transfer other = {
		.sub_address = async_sub,
		.sub_address_len = 1,
		.buf = scratch,
		.len = 8,
	};
	const char *count = "not checked";
	bool count_ok = false, idle, ok;
	int ret, r_rx = 0, r_tx = 0;
	uint64_t end;
	int32_t bad = -1;
	uint32_t nbad = 0;

	memset(scratch, 0, 8);

	ret = async_reference(1, MAX_READ_LEN);
	if (ret) {
		printf("  async busy: blocking reference read %d", ret);
		result(false);
		return false;
	}

	async_prefill(MAX_READ_LEN);
	async_done = false;
	ret = capi_i2c_receive_async(&dev, &xfer);
	if (!ret) {
		r_rx = capi_i2c_receive_async(&dev, &other);
		r_tx = capi_i2c_transmit_async(&dev, &other);

		end = now_us() + 500000U;
		while (!async_done && now_us() <= end)
			;
		ret = async_done ? async_result : -ETIMEDOUT;
	}

	idle = async_settle();
	if (!ret && idle)
		count = async_count(async_offset(1), MAX_READ_LEN, &count_ok);
	if (!ret)
		bad = async_mismatch(MAX_READ_LEN, &nbad);

	ok = !ret && r_rx == -EBUSY && r_tx == -EBUSY && idle && count_ok &&
	     bad < 0 && devid_ok();
	printf("  async busy: %u B read %d, second read %d, write %d (expect %d), %s, count %s, data ",
	       MAX_READ_LEN, ret, r_rx, r_tx, -EBUSY,
	       idle ? "STOP" : "NO STOP", count);
	if (ret)
		printf("not checked");
	else
		async_print_data(bad, nbad, MAX_READ_LEN);
	result(ok);

	return ok;
}

/*
 * Transfers the driver must refuse: no data, more than the async state can
 * count, no buffer. They are addressed to the absent target, so one that does
 * go out moves no data.
 */
static bool async_case_params(void)
{
	struct capi_i2c_transfer bad_xfers[] = {
		{ .buf = buf, .len = 0, .target_addr = ABSENT_ADDR },
		{
			.buf = buf, .len = (uint32_t)UINT16_MAX + 1,
			.target_addr = ABSENT_ADDR
		},
		{ .buf = NULL, .len = 8, .target_addr = ABSENT_ADDR },
	};
	static const char *const what[] = { "len 0", "len 65536", "no buffer" };
	bool ok = true;

	printf("  async refused:");
	for (uint32_t k = 0; k < ARRAY_LEN(bad_xfers); k++) {
		int r_rx = capi_i2c_receive_async(&dev, &bad_xfers[k]);
		int r_tx = capi_i2c_transmit_async(&dev, &bad_xfers[k]);

		printf(" %s read %d write %d;", what[k], r_rx, r_tx);
		if (r_rx != -EINVAL || r_tx != -EINVAL)
			ok = false;
	}
	ok = async_settle() && ok && devid_ok();
	printf(" (expect %d)", -EINVAL);
	result(ok);

	return ok;
}

/*
 * Async read of @len bytes with no_stop: the message must stay open when the
 * callback runs, and a write that starts with a repeated START must then be
 * ACKed and end the bus with a STOP.
 *
 * The follow-up is a write, not a read: the ADXL367 ends every read with a
 * NACK and a STOP (datasheet Figure 54). After a read left open, it ACKs a
 * repeated START and its address but returns 0xFF for any read that follows.
 * async_case_no_stop_write() checks a repeated START read.
 */
static bool async_case_no_stop(uint32_t len)
{
	mxc_i3c_regs_t *i3c = MXC_I3C_GET_I3C(BUS_I2C_IDENTIFIER);
	uint8_t reg = STREAM_START;
	struct capi_i2c_transfer xfer = {
		.sub_address = &reg,
		.sub_address_len = 1,
		.buf = buf,
		.len = len,
		.no_stop = true,
	};
	struct capi_i2c_transfer sr_write = { .buf = &reg, .len = 1 };
	uint32_t state, nbad;
	int32_t bad;
	bool held, idle, ok;
	int ret, ret_sr = -1;

	ret = read_reg(ADXL367_ADDR, STREAM_START, async_expect, len);
	if (ret) {
		printf("  async no-STOP %4lu B: blocking reference read %d",
		       (unsigned long)len, ret);
		result(false);
		return false;
	}

	for (uint32_t i = 0; i < len; i++)
		buf[i] = ~async_expect[i];

	ret = run_async(false, &xfer);
	state = i3c->cont_status & MXC_F_I3C_CONT_STATUS_STATE;
	held = state != MXC_S_I3C_CONT_STATUS_STATE_IDLE;

	/* Repeated START, then a STOP at the end */
	if (!ret)
		ret_sr = capi_i2c_transmit(&dev, &sr_write);
	idle = async_settle();

	bad = async_mismatch(len, &nbad);
	ok = !ret && held && !ret_sr && idle && bad < 0 && devid_ok();

	printf("  async no-STOP read  %4lu B: %d, %s (state %lu), Sr write %d, %s, data ",
	       (unsigned long)len, ret, held ? "held" : "STOPPED",
	       (unsigned long)state, ret_sr, idle ? "STOP" : "NO STOP");
	async_print_data(bad, nbad, len);
	result(ok);

	return ok;
}

/*
 * Async write of the register address with no_stop: the message must stay
 * open when the callback runs, and a blocking read that starts with a repeated
 * START must then return the register and end the bus with a STOP.
 */
static bool async_case_no_stop_write(bool sub_address)
{
	mxc_i3c_regs_t *i3c = MXC_I3C_GET_I3C(BUS_I2C_IDENTIFIER);
	uint8_t reg = ADXL367_REG_DEVID_AD;
	struct capi_i2c_transfer xfer = {
		.buf = &reg,
		.len = 1,
		.no_stop = true,
	};
	uint8_t id = 0;
	uint32_t state;
	bool held, idle, ok;
	int ret, ret_sr = -1;

	/*
	 * With a register address, the data byte goes to the read-only DEVID_AD
	 * and moves the pointer on to DEVID_MST.
	 */
	if (sub_address) {
		xfer.sub_address = &reg;
		xfer.sub_address_len = 1;
	}

	ret = run_async(true, &xfer);
	state = i3c->cont_status & MXC_F_I3C_CONT_STATUS_STATE;
	held = state != MXC_S_I3C_CONT_STATUS_STATE_IDLE;

	/* Repeated START read, then a STOP at the end */
	if (!ret)
		ret_sr = read_plain(&id, 1);
	idle = async_settle();

	ok = !ret && held && !ret_sr && idle && devid_ok() &&
	     id == (sub_address ? ADXL367_DEVID_MST : ADXL367_DEVID_AD);

	printf("  async no-STOP write %s: %d, %s (state %lu), Sr read %d (0x%02X), %s",
	       sub_address ? "addr+1" : "plain ", ret,
	       held ? "held" : "STOPPED", (unsigned long)state, ret_sr, id,
	       idle ? "STOP" : "NO STOP");
	result(ok);

	return ok;
}

/*
 * Async write of @len bytes from RW_REG, with the register address separate or
 * as the first byte of one plain buffer. Writes up to RW_LEN bytes carry the
 * pattern and are read back; longer ones write zeros. A soft reset follows.
 */
static bool async_case_write(bool sub_address, uint32_t len)
{
	static uint8_t frame[WRITE_LEN + 1];
	uint8_t reg = RW_REG;
	uint8_t mask[RW_LEN], zeros[RW_LEN], expect[RW_LEN];
	struct capi_i2c_transfer xfer;
	bool check = len <= RW_LEN;
	bool idle, readback = true, ok;
	int ret, ret_rst;

	memset(zeros, 0, sizeof(zeros));
	memset(expect, 0, sizeof(expect));
	memset(frame, 0, sizeof(frame));
	if (check) {
		memcpy(expect, rw_pattern, len);
		if (!read_rw_mask(mask) || write_rw_regs(zeros))
			readback = false;
	}

	if (sub_address) {
		memcpy(frame, expect, check ? len : 0);
		xfer = (struct capi_i2c_transfer) {
			.sub_address = &reg,
			.sub_address_len = 1,
			.buf = frame,
			.len = len,
		};
	} else {
		frame[0] = reg;
		memcpy(&frame[1], expect, check ? len : 0);
		xfer = (struct capi_i2c_transfer) {
			.buf = frame,
			.len = len + 1,
		};
	}

	ret = run_async(true, &xfer);
	idle = async_settle();
	if (check && readback)
		readback = rw_regs_match(expect, mask);
	ret_rst = soft_reset();

	ok = !ret && idle && readback && !ret_rst && devid_ok();
	printf("  async write %-5s %3lu B: %d, %s, %s",
	       sub_address ? "addr" : "plain", (unsigned long)len, ret,
	       idle ? "STOP" : "NO STOP",
	       !check ? "no read-back" :
	       readback ? "read back ok" : "READ BACK BAD");
	result(ok);

	return ok;
}

/*
 * Async transfers to an address nobody answers. With a register address the
 * NACK comes on the write address; without one, on the read address, before a
 * short or a long read has started.
 */
static bool async_case_absent(void)
{
	uint8_t reg = RW_REG;
	const struct {
		bool transmit;
		bool sub_address;
		uint32_t len;
	} cases[] = {
		{ true, true, 64 },
		{ false, true, 64 },
		{ false, true, 300 },
		{ false, false, 8 },
		{ false, false, 300 },
	};
	bool ok = true;

	printf("  async absent 0x%02X:", ABSENT_ADDR);
	for (uint32_t k = 0; k < ARRAY_LEN(cases); k++) {
		struct capi_i2c_transfer xfer = {
			.sub_address = cases[k].sub_address ? &reg : NULL,
			.sub_address_len = cases[k].sub_address ? 1 : 0,
			.buf = buf,
			.target_addr = ABSENT_ADDR,
			.len = cases[k].len,
		};
		bool idle;
		int ret;

		ret = run_async(cases[k].transmit, &xfer);
		idle = async_settle();
		if (ret != -EIO || !idle)
			ok = false;

		printf(" %s %s %lu B %d%s;", cases[k].transmit ? "write" : "read",
		       cases[k].sub_address ? "Sr" : "cont",
		       (unsigned long)cases[k].len, ret, idle ? "" : " NO STOP");
	}

	ok = ok && devid_ok();
	printf(" (expect %d), bus %s", -EIO, devid_ok() ? "ok" : "STUCK");
	result(ok);

	return ok;
}

static const uint32_t async_write_lengths[] = { 1, 7, 8, 9, 64, WRITE_LEN };
static const uint32_t async_plain_write_lengths[] = { 1, 7, 9 };
/* Above 255, the driver ends the read with term_rd = 1 (long-read path). */
static const uint32_t async_read_lengths[] = {
	1, 2, 7, 8, 9, 16, 64, 255, 256, 257, 264, 265, 300, 512, MAX_READ_LEN,
};
static const uint32_t async_cont_read_lengths[] = {
	1, 7, 8, 9, 64, 255, 256, 265, MAX_READ_LEN,
};
/*
 * Register addresses longer than one byte: a 16-bit one, one just over the
 * FIFO depth and one the transmit FIFO has to be refilled for.
 */
static const uint32_t async_sub_lens[] = { 2, MAX_CAPI_I2C_FIFO_DEPTH + 1,
					   ASYNC_SUB_MAX
					 };
static const uint32_t async_sub_read_lengths[] = { 8, 300 };
/* A read term_rd ends, and one the driver ends with term_rd = 1 */
static const uint32_t async_no_stop_lengths[] = { 8, 300 };
static const uint32_t async_chain_lengths[] = { 8, 300, MAX_READ_LEN };

/* Async transfers in every shape. */
static void test_async(void)
{
	capi_i2c_register_callback(i2c, async_cb, NULL);

	async_case_params();
	for (uint32_t l = 0; l < ARRAY_LEN(async_write_lengths); l++)
		async_case_write(true, async_write_lengths[l]);
	for (uint32_t l = 0; l < ARRAY_LEN(async_plain_write_lengths); l++)
		async_case_write(false, async_plain_write_lengths[l]);
	for (uint32_t l = 0; l < ARRAY_LEN(async_read_lengths); l++)
		async_case_read(1, async_read_lengths[l]);
	for (uint32_t l = 0; l < ARRAY_LEN(async_cont_read_lengths); l++)
		async_case_read(0, async_cont_read_lengths[l]);
	for (uint32_t s = 0; s < ARRAY_LEN(async_sub_lens); s++)
		for (uint32_t l = 0; l < ARRAY_LEN(async_sub_read_lengths); l++)
			async_case_read(async_sub_lens[s],
					async_sub_read_lengths[l]);
	for (uint32_t l = 0; l < ARRAY_LEN(async_no_stop_lengths); l++)
		async_case_no_stop(async_no_stop_lengths[l]);
	async_case_no_stop_write(false);
	async_case_no_stop_write(true);
	for (uint32_t l = 0; l < ARRAY_LEN(async_chain_lengths); l++)
		async_case_chain(async_chain_lengths[l]);
	async_case_busy();
	async_case_absent();

	capi_i2c_register_callback(i2c, NULL, NULL);
}

/*
 * Overflow callback: stay busy for stress_busy_ticks of the stress timer. The
 * counter restarts at each overflow and the busy time is shorter than the
 * period, so it does not wrap here. capi_uptime() cannot time this: SysTick
 * does not preempt this interrupt, so the uptime stalls at a tick boundary.
 */
static void stress_cb(uint32_t event, void *arg, int extra)
{
	uint32_t start = 0, now = 0;

	(void)event;
	(void)arg;
	(void)extra;

	stress_hits++;
	capi_timer_counter_get(stress_timer, &start);
	do {
		capi_timer_counter_get(stress_timer, &now);
	} while (now >= start && now - start < stress_busy_ticks);
}

static void stress_stop(void)
{
	if (!stress_timer)
		return;

	/*
	 * Disable the event before stopping the counter: an overflow already
	 * pending would otherwise run stress_cb() on a frozen counter and spin.
	 */
	capi_timer_event_irq_disable(stress_timer,
				     CAPI_TIMER_GLOBAL_EVENT_COUNTER_OVERFLOW);
	capi_timer_stop(stress_timer);
	capi_timer_deinit(stress_timer);
	stress_timer = NULL;
}

/* Interrupt every @period_us, each one busy for @busy_us. */
static int stress_start(uint32_t period_us, uint32_t busy_us)
{
	static const struct max_capi_timer_extra extra = {
		.bit_mode = MAX_CAPI_TIMER_BIT_MODE_32BIT,
		.use_irq = true,
	};
	const struct capi_timer_config config = {
		.ops = &max_capi_timer_ops,
		.identifier = STRESS_TIMER_IDENTIFIER,
		.output_freq_hz = STRESS_TIMER_HZ,
		.extra = &extra,
	};
	struct capi_timer_counter_config counter = {
		.direction = CAPI_TIMER_COUNT_UP,
		.rollover = true,
	};
	uint32_t busy_ticks;
	int ret;

	stress_hits = 0;

	ret = capi_timer_init(&stress_timer, &config);
	if (ret)
		return ret;

	ret = capi_timer_nsec_to_ticks(stress_timer, period_us * 1000ULL,
				       &counter.max);
	if (!ret)
		ret = capi_timer_nsec_to_ticks(stress_timer, busy_us * 1000ULL,
					       &busy_ticks);
	if (!ret) {
		stress_busy_ticks = busy_ticks;
		ret = capi_timer_counter_config(stress_timer, &counter);
	}
	if (!ret)
		ret = capi_timer_register_event_callback(stress_timer,
				stress_cb, NULL);
	if (!ret)
		ret = capi_timer_event_irq_enable(stress_timer,
				CAPI_TIMER_GLOBAL_EVENT_COUNTER_OVERFLOW);
	if (!ret)
		ret = capi_timer_start(stress_timer);
	if (ret)
		stress_stop();

	return ret;
}

/*
 * 1 MHz is skipped unless TEST_FAST_PLUS is 1. The ADXL367 is rated for
 * 100 kHz and 400 kHz only, and with the EVKIT's 4.7 kOhm pull-ups to 1.5 V,
 * SCL barely rises in time at 1 MHz: a probe on the bus is enough to break it.
 */
#ifndef TEST_FAST_PLUS
#define TEST_FAST_PLUS		0
#endif

static const struct {
	enum capi_i2c_speed speed;
	const char *name;
	uint32_t hz;
	bool skip;
} speeds[] = {
	{ CAPI_I2C_SPEED_STANDARD, "100 kHz", 100000, false },
	{ CAPI_I2C_SPEED_FAST, "400 kHz", 400000, false },
	{ CAPI_I2C_SPEED_FAST_PLUS, "1 MHz", 1000000, !TEST_FAST_PLUS },
};

/* 255 and below end with term_rd = len; the rest take the long-read path. */
static const uint32_t lengths[] = {
	1, 2, 3, 4, 7, 8, 9, 16, 32, 64, 128, 200, 254, 255,
	256, 257, 258, 263, 264, 265, 300, 511, 512, 513, 1000, MAX_READ_LEN,
};

static const struct {
	uint32_t period_us;
	uint32_t busy_us;
	const char *what;
} stresses[] = {
	{ 50, 30, "ISR 30 us every 50 us" },
	{ 400, 150, "ISR 150 us every 400 us (SCL held > 100 us)" },
};

static const uint32_t stress_lengths[] = { 257, MAX_READ_LEN };

/*
 * Long async reads under the stress interrupt load. Without it the controller
 * interrupt (or the DMA completion) keeps up byte by byte, so the long-read
 * ending always starts on a nearly empty receive FIFO. The busy interrupt
 * delays it until the FIFO is full and the controller holds SCL, which is the
 * other order the ending has to handle.
 */
static const uint32_t async_stress_lengths[] = { 256, 265, MAX_READ_LEN };

static void test_async_stress(const char *speed, const char *mode)
{
	for (uint32_t k = 0; k < ARRAY_LEN(stresses); k++) {
		int ret;

		printf("\r\n== %s, async via %s, %s ==\r\n", speed, mode,
		       stresses[k].what);
	
		ret = stress_start(stresses[k].period_us, stresses[k].busy_us);
		if (ret) {
			printf("stress timer: %d", ret);
			result(false);
			continue;
		}

		capi_i2c_register_callback(i2c, async_cb, NULL);
		for (uint32_t l = 0; l < ARRAY_LEN(async_stress_lengths); l++) {
			async_case_read(1, async_stress_lengths[l]);
			async_case_read(0, async_stress_lengths[l]);
		}
		capi_i2c_register_callback(i2c, NULL, NULL);

		stress_stop();
		printf("  (%lu interrupts)\r\n", (unsigned long)stress_hits);
	}
}

static const uint32_t write_lengths[] = {
	1, 2, 3, 4, 5, 6, 8, 9, 16, 64, 255, 256, 257, WRITE_LEN,
};

static void write_sweep(void)
{
	for (uint32_t l = 0; l < ARRAY_LEN(write_lengths); l++)
		test_write(write_lengths[l]);
}

static uint32_t cur_speed;

static bool set_speed(uint32_t s)
{
	int ret = capi_i2c_configure_bus_speed(i2c, speeds[s].speed, 0);

	cur_speed = s;
	printf("bus speed %s: %d\r\n", speeds[s].name, ret);

	return !ret && devid_ok();
}

static struct max_capi_i2c_extra i2c_extra = {
	.vssel = MAX_CAPI_GPIO_VSSEL_VDDIO,
	.use_irq = true,
};

/* Init the controller, with the DMA handle when @with_dma is set. */
static int i2c_open(bool with_dma)
{
	const struct capi_i2c_config config = {
		.identifier = BUS_I2C_IDENTIFIER,
		.clk_freq_hz = MAX_CAPI_I2C_SPEED_STANDARD,
		.initiator = true,
		.dma_handle = with_dma ? dma : NULL,
		.extra = &i2c_extra,
		.ops = &max_capi_i2c_ops,
	};
	int ret;

	i2c = NULL;
	ret = capi_i2c_init(&i2c, &config);
	if (ret)
		return ret;

	dev.controller = i2c;
	dma_mode = with_dma;

	return capi_i2c_configure_bus_speed(i2c, speeds[cur_speed].speed, 0);
}

static bool set_dma_mode(bool with_dma)
{
	int ret = capi_i2c_deinit(i2c);

	if (!ret)
		ret = i2c_open(with_dma);
	printf("controller re-init, async via %s: %d\r\n",
	       with_dma ? "DMA" : "FIFO interrupts", ret);

	return !ret && devid_ok();
}

/*
 * Target stalls. A one-shot timer interrupt takes SCL from the controller and
 * drives it low as a GPIO partway through a blocking transfer, which to the
 * controller is a target holding SCL. The call must give up with an error
 * instead of hanging. Once SCL is released, the next transfer must work
 * without re-initialising the controller or recovering the bus: a target cut
 * off in the middle of a byte goes on sending once it gets SCL back.
 *
 * Every wait gives up after a poll budget of SystemCoreClock / bus speed bit
 * times, so the transfer under test runs with SystemCoreClock lowered to
 * STALL_K times the bus speed, which is still ample for a healthy byte. A
 * stalled read times out. Holding SCL before the START or during a write
 * makes the controller report a bus error at once, or the wait times out,
 * depending on where in the bit it lands.
 */
#define STALL_K			8U
/* Either -EIO or -ETIMEDOUT */
#define STALL_ERR		1

struct stall {
	const char *id;
	const char *desc;
	/* When SCL is held, in bit times from the start; 0: before the call */
	uint32_t at_bits;
	bool write;
	/* Register address, then zeros */
	uint32_t sub_len;
	uint32_t len;
	int ret;
};

static const struct stall stall_cases[] = {
	{ "T1", "read 16, SCL low before the START", 0, false, 1, 16, STALL_ERR },
	{ "T2", "read 257, held in the data", 300, false, 0, 257, -ETIMEDOUT },
	{ "T3", "Sr/1 + read 257, held in the data", 300, false, 1, 257, -ETIMEDOUT },
	{ "T4", "Sr/12 + read 16, held in the sub-address", 40, false, 12, 16, -ETIMEDOUT },
	{ "T5", "write 300, held in the data", 300, true, 1, WRITE_LEN, STALL_ERR },
	{ "T6", "read 16, held in the data", 60, false, 1, 16, -ETIMEDOUT },
};

static volatile bool stall_fired;
static uint8_t stall_sub[ASYNC_SUB_MAX] = { STREAM_START };

/* Take SCL (P0.0) from the controller and drive it low, or give it back. */
static void scl_hold(bool hold)
{
	mxc_gpio_cfg_t pin = gpio_cfg_i3c;

	pin.mask = MXC_GPIO_PIN_0;
	pin.vssel = (mxc_gpio_vssel_t)i2c_extra.vssel;
	if (hold) {
		MXC_GPIO_OutClr(pin.port, pin.mask);
		pin.func = MXC_GPIO_FUNC_OUT;
	}
	MXC_GPIO_Config(&pin);
}

static void stall_cb(uint32_t event, void *arg, int extra)
{
	(void)event;
	(void)arg;
	(void)extra;

	if (stall_fired)
		return;
	scl_hold(true);
	stall_fired = true;
}

/* Hold SCL @after_us from now, from the stress timer. */
static int stall_arm(uint32_t after_us)
{
	static const struct max_capi_timer_extra extra = {
		.bit_mode = MAX_CAPI_TIMER_BIT_MODE_32BIT,
		.use_irq = true,
	};
	const struct capi_timer_config config = {
		.ops = &max_capi_timer_ops,
		.identifier = STRESS_TIMER_IDENTIFIER,
		.output_freq_hz = STRESS_TIMER_HZ,
		.extra = &extra,
	};
	struct capi_timer_counter_config counter = {
		.direction = CAPI_TIMER_COUNT_UP,
		.rollover = true,
	};
	int ret;

	stall_fired = false;
	ret = capi_timer_init(&stress_timer, &config);
	if (ret)
		return ret;

	ret = capi_timer_nsec_to_ticks(stress_timer, after_us * 1000ULL,
				       &counter.max);
	if (!ret)
		ret = capi_timer_counter_config(stress_timer, &counter);
	if (!ret)
		ret = capi_timer_register_event_callback(stress_timer, stall_cb,
				NULL);
	if (!ret)
		ret = capi_timer_event_irq_enable(stress_timer,
				CAPI_TIMER_GLOBAL_EVENT_COUNTER_OVERFLOW);
	if (!ret)
		ret = capi_timer_start(stress_timer);
	if (ret)
		stress_stop();

	return ret;
}

static bool test_stall(const struct stall *t)
{
	static uint8_t zeros[WRITE_LEN];
	uint8_t reg = WRITE_START_REG;
	struct capi_i2c_transfer xfer = {
		.sub_address = t->write ? &reg : (t->sub_len ? stall_sub : NULL),
		.sub_address_len = t->sub_len,
		.buf = t->write ? zeros : buf,
		.len = t->len,
	};
	uint32_t real_hz = SystemCoreClock;
	uint32_t at_us = t->at_bits * 1000000U / speeds[cur_speed].hz;
	uint8_t id = 0;
	uint64_t t0, t1;
	int ret, ret_arm = 0, ret_id;
	bool fired, ok;

	if (!t->sub_len && set_pointer(STREAM_START)) {
		printf("  %-3s %-42s set pointer failed", t->id, t->desc);
		result(false);
		return false;
	}

	if (t->at_bits) {
		ret_arm = stall_arm(at_us);
	} else {
		stall_fired = true;
		scl_hold(true);
	}

	/* Nothing between these two lines may print or wait on the core clock */
	SystemCoreClock = STALL_K * speeds[cur_speed].hz;
	t0 = now_us();
	ret = t->write ? capi_i2c_transmit(&dev, &xfer) :
	      capi_i2c_receive(&dev, &xfer);
	t1 = now_us();
	SystemCoreClock = real_hz;

	fired = stall_fired;
	stress_stop();
	scl_hold(false);

	/* The very next transfer, with no re-init and no bus recovery */
	ret_id = read_reg(ADXL367_ADDR, ADXL367_REG_DEVID_AD, &id, 1);
	if (t->write)
		soft_reset();

	ok = !ret_arm && fired && !ret_id && id == ADXL367_DEVID_AD &&
	     (t->ret == STALL_ERR ? ret == -EIO || ret == -ETIMEDOUT :
	      ret == t->ret);
	printf("  %-3s %-42s %d after %lu us (want %s)%s, next read %d (0x%02X)",
	       t->id, t->desc, ret, (unsigned long)(t1 - t0),
	       t->ret == STALL_ERR ? "-5 or -116" : "-116",
	       fired ? "" : ", SCL NEVER HELD", ret_id, id);
	result(ok);

	if (!ok && (ret_id || id != ADXL367_DEVID_AD)) {
		capi_i2c_recover_bus(i2c);
		if (!devid_ok())
			set_dma_mode(dma_mode);
	}

	return ok;
}

static void test_stalls(void)
{
	printf("\r\n== %s, target stalls: SCL held low, poll budget k = %u ==\r\n",
	       speeds[cur_speed].name, STALL_K);
	for (uint32_t i = 0; i < ARRAY_LEN(stall_cases); i++)
		test_stall(&stall_cases[i]);
}

/* Heap in use, as malloc() sees it: the CAPI allocator is malloc() */
static uint32_t heap_used(void)
{
	return (uint32_t)mallinfo().uordblks;
}

static void heap_check(const char *what, uint32_t before)
{
	uint32_t after = heap_used();

	printf("  heap in use %s: %lu -> %lu bytes", what,
	       (unsigned long)before, (unsigned long)after);
	result(before == after);
}

static void run_suite(void)
{
	uint32_t heap_start, heap_mode;

	n_pass = 0;
	n_fail = 0;
	heap_start = heap_used();

	for (uint32_t s = 0; s < ARRAY_LEN(speeds); s++) {
		if (speeds[s].skip) {
			printf("\r\n== %s: skipped (TEST_FAST_PLUS 0) ==\r\n",
			       speeds[s].name);
			continue;
		}

		printf("\r\n== %s ==\r\n", speeds[s].name);

		if (!set_speed(s)) {
			printf("bus not usable at %s", speeds[s].name);
			result(false);
			continue;
		}

		test_probe();
		for (uint32_t l = 0; l < ARRAY_LEN(lengths); l++) {
			test_read(true, lengths[l], ITERATIONS);
			test_read(false, lengths[l], ITERATIONS);
		}
		write_sweep();
		test_write_forms();
		test_absent();

		test_stalls();
	}

	for (uint32_t s = 1; s < ARRAY_LEN(speeds); s++) {
		if (speeds[s].skip || !set_speed(s))
			continue;

		for (uint32_t k = 0; k < ARRAY_LEN(stresses); k++) {
			int ret;

			printf("\r\n== %s, %s ==\r\n", speeds[s].name,
			       stresses[k].what);

			ret = stress_start(stresses[k].period_us,
					   stresses[k].busy_us);
			if (ret) {
				printf("stress timer: %d", ret);
				result(false);
				continue;
			}

			for (uint32_t l = 0; l < ARRAY_LEN(stress_lengths); l++)
				test_read(true, stress_lengths[l], ITERATIONS);

			stress_stop();
			printf("  (%lu interrupts)\r\n",
			       (unsigned long)stress_hits);
		}
	}

	/* Last: async in both modes, then blocking with the DMA attached */
	for (uint32_t m = 0; m < 2; m++) {
		/* Re-init at 100 kHz: the previous pass ends at a faster speed */
		cur_speed = 0;
		if (!set_dma_mode(m == 1)) {
			printf("controller re-init");
			result(false);
			continue;
		}

		for (uint32_t s = 0; s < ARRAY_LEN(speeds); s++) {
			if (speeds[s].skip || !set_speed(s))
				continue;
			printf("\r\n== %s, async via %s ==\r\n",
			       speeds[s].name, m ? "DMA" : "FIFO interrupts");
			test_async();
			/* The async state now stays until the next re-init */
			if (!s)
				heap_mode = heap_used();

			/* As for blocking reads, the load is sized for 400 kHz up */
			if (s)
				test_async_stress(speeds[s].name,
						  m ? "DMA" : "FIFO interrupts");

			if (m) {
				printf("\r\n== %s, blocking with DMA attached ==\r\n",
				       speeds[s].name);
				test_read(true, 8, ITERATIONS);
				test_read(true, 255, ITERATIONS);
				test_read(true, 257, ITERATIONS);
				test_read(false, MAX_READ_LEN, ITERATIONS);
				test_write(WRITE_LEN);
				test_write_forms();
			}
		}

		printf("\r\n");
		heap_check(m ? "over the DMA passes" : "over the FIFO interrupt passes",
			   heap_mode);
	}
	set_dma_mode(false);

	set_speed(0);
	heap_check("over the whole suite", heap_start);
	printf("\r\nRESULT: %lu passed, %lu failed -> %s\r\n",
	       (unsigned long)n_pass, (unsigned long)n_fail,
	       n_fail ? "FAIL" : "PASS");
}

int main(void)
{
	static struct max_capi_dma_extra dma_extra = { .use_irq = true };
	const struct capi_dma_config dma_config = {
		.id = 0,
		.num_chans = 4,
		.ops = &max_capi_dma_ops,
		.extra = &dma_extra,
	};
	struct capi_irq_config irq_config = { .irq_ctrl_id = 0 };
	uint64_t core_hz = 0;
	int ret;

	/* The J-Link VCOM garbles the first bytes after a reset. */
	capi_wait_ms(2000);


	ret = console_init();
	if (ret)
		return ret;

	printf("\r\nMAX32657 CAPI I2C long-read test (I2C%u, ADXL367 @ 0x%02X)\r\n",
	       BUS_I2C_IDENTIFIER, ADXL367_ADDR);
	capi_platform_get_clock_hz(&core_hz);
	printf("core %lu Hz; driver long-read path: %s (FIFO depth %u, term tail %u, term_rd = len up to %u B)\r\n",
	       (unsigned long)core_hz, DRIVER_LONG_READ,
	       RX_FIFO_DEPTH, MAX_CAPI_I2C_TERM_TAIL,
	       MAX_CAPI_I2C_TERM_RD_MAX);

	ret = capi_irq_init(&irq_config);
	if (!ret)
		ret = capi_irq_global_enable();
	if (ret) {
		printf("capi_irq_init failed: %d\r\n", ret);
		return ret;
	}

	ret = capi_dma_init(&dma, &dma_config);
	if (ret) {
		printf("capi_dma_init failed: %d\r\n", ret);
		return ret;
	}

	ret = i2c_open(false);
	if (ret) {
		printf("capi_i2c_init failed: %d\r\n", ret);
		return ret;
	}

	if (!devid_ok()) {
		printf("ADXL367 not found: fit JP16/JP17 (pull-ups) and JP9/JP10 (ADXL367)\r\n");
		return -ENODEV;
	}

	if (!test_continuation() || !build_reference())
		return -EIO;

	run_suite();

	return n_fail ? -EIO : 0;
}
