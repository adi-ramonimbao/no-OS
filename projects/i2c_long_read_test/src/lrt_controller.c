/*
 * Controller side of the I2C long-read bench test. Every transfer under test
 * goes through the no-OS API (no_os_i2c_write/no_os_i2c_read), so it covers
 * max_i2c_read(), the prologue path and _max_i2c_long_read() as built.
 *
 * Each test is checked three ways:
 * - the return value of every no-OS call;
 * - the data: every read must return lrt_pat(0..n-1) unbroken, since the
 *   target restarts the pattern on each read address match;
 * - the target's record of the transaction, fetched over the bus: exactly
 *   one new transaction (none for the NACK tests) with the expected
 *   read/write address matches, written byte count and hash, and no
 *   target-side errors. A read split by a repeated START shows up as
 *   rd_match = 2.
 * Fetching the record is itself a write + repeated START + read, so it
 * also checks the bus was left idle and usable.
 */

#include <malloc.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "no_os_delay.h"
#include "no_os_error.h"
#include "no_os_i2c.h"
#include "no_os_util.h"
#include "i2c.h"
#include "maxim_i2c.h"
#include "lrt_common.h"
#include "lrt_controller.h"

#define LRT_MAX_LEN	1024
#define LRT_STRESS_N	200

/* Any non-zero return; MSDK errors are not errno values */
#define ANY_ERR		1

enum op_kind {
	OP_NONE,
	OP_READ,
	OP_WRITE,
};

struct op {
	enum op_kind kind;
	uint32_t len;
	uint8_t stop;
	/* Address nothing answers at */
	bool absent;
	/* 0, -EBUSY, -EIO or ANY_ERR */
	int ret;
};

struct test {
	const char *id;
	const char *desc;
	struct op ops[3];
	/* Transactions the target must record: 0 or 1; -1: don't check */
	int txns;
	uint32_t rd_match;
	uint32_t wr_match;
};

#define RD(n, s)	{ OP_READ, (n), (s), false, 0 }
#define WR(n, s)	{ OP_WRITE, (n), (s), false, 0 }

static const struct test tests[] = {
	/* Plain reads, either side of the 256-byte limit */
	{ "R1", "read 16",			{ RD(16, 1) },	1, 1, 0 },
	{ "R2", "read 256 (short path)",	{ RD(256, 1) },	1, 1, 0 },
	{ "R3", "read 257 (1 IRXM byte)",	{ RD(257, 1) },	1, 1, 0 },
	{ "R4", "read 300",			{ RD(300, 1) },	1, 1, 0 },
	{ "R5", "read 512 (256 IRXM bytes)",	{ RD(512, 1) },	1, 1, 0 },
	{ "R6", "read 600",			{ RD(600, 1) },	1, 1, 0 },
	{ "R7", "read 1024",			{ RD(1024, 1) }, 1, 1, 0 },

	/* Write without STOP + read: the stored prologue, then Sr */
	{ "P1", "w1 + Sr + read 257 (block read)",
	  { WR(1, 0), RD(257, 1) }, 1, 1, 1 },
	{ "P2", "w1 + Sr + read 600",
	  { WR(1, 0), RD(600, 1) }, 1, 1, 1 },
	{ "P3", "w300 + Sr + read 257 (long prologue)",
	  { WR(300, 0), RD(257, 1) }, 1, 1, 1 },
	{ "P4", "w300 + Sr + read 16 (long prologue)",
	  { WR(300, 0), RD(16, 1) }, 1, 1, 1 },
	{ "P5", "w1 + Sr + read 16",
	  { WR(1, 0), RD(16, 1) }, 1, 1, 1 },

	/* Long writes */
	{ "W1", "write 300",			{ WR(300, 1) },	1, 0, 1 },
	{ "W2", "write 1024",			{ WR(1024, 1) }, 1, 0, 1 },

	/* Long read ending in a repeated START instead of a STOP */
	{ "S1", "read 257 + Sr + read 16",
	  { RD(257, 0), RD(16, 1) }, 1, 2, 0 },
	{ "S2", "read 600 + Sr + read 256",
	  { RD(600, 0), RD(256, 1) }, 1, 2, 0 },
	/* Documented limitation: a long read can't continue an open bus */
	{ "S3", "read 16 + Sr + read 257: -EBUSY, then read 16 closes",
	  { RD(16, 0), { OP_READ, 257, 1, false, -EBUSY }, RD(16, 1) },
	  -1, 0, 0 },

	/* Address NACK: error path, then the bus must still work */
	{ "E1", "read 257 at absent 0x52: -EIO",
	  { { OP_READ, 257, 1, true, -EIO } }, 0, 0, 0 },
	{ "E2", "w1 + Sr + read 257 at absent 0x52: -EIO",
	  { { OP_WRITE, 1, 0, true, 0 }, { OP_READ, 257, 1, true, -EIO } },
	  0, 0, 0 },
	{ "E3", "read 16 at absent 0x52: error (MSDK path)",
	  { { OP_READ, 16, 1, true, ANY_ERR } }, 0, 0, 0 },
	{ "R3b", "read 257 after the errors",	{ RD(257, 1) },	1, 1, 0 },
};

struct ctx {
	uint32_t i2c_id;
	uint32_t speed;
	struct no_os_i2c_desc *tgt;
	struct no_os_i2c_desc *absent;
	uint32_t seq;
	int failed;
};

static uint8_t rx[LRT_MAX_LEN];
static uint8_t tx[LRT_MAX_LEN];

static int ctl_init(struct ctx *c)
{
	struct max_i2c_init_param extra = {
		.vssel = MXC_GPIO_VSSEL_VDDIO,
	};
	struct no_os_i2c_init_param ip = {
		.device_id = c->i2c_id,
		.max_speed_hz = c->speed,
		.platform_ops = &max_i2c_ops,
		.extra = &extra,
	};
	int ret;

	ip.slave_address = LRT_TGT_ADDR;
	ret = no_os_i2c_init(&c->tgt, &ip);
	if (ret)
		return ret;

	ip.slave_address = LRT_ABSENT_ADDR;
	ret = no_os_i2c_init(&c->absent, &ip);
	if (ret) {
		no_os_i2c_remove(c->tgt);
		c->tgt = NULL;
	}

	return ret;
}

static void ctl_remove(struct ctx *c)
{
	if (c->absent)
		no_os_i2c_remove(c->absent);
	if (c->tgt)
		no_os_i2c_remove(c->tgt);
	c->absent = NULL;
	c->tgt = NULL;
}

/* Re-create both descriptors; the last remove resets the peripheral */
static void ctl_recover(struct ctx *c)
{
	int ret;

	ctl_remove(c);
	no_os_mdelay(5);
	ret = ctl_init(c);
	printf("      (controller re-initialised: %d)\n", ret);
}

static int get_record(struct ctx *c, struct lrt_record *r)
{
	uint8_t cmd = LRT_CMD_STATS;
	int ret;

	/* Let the target record the STOP of the transfer under test */
	no_os_mdelay(2);

	ret = no_os_i2c_write(c->tgt, &cmd, 1, 0);
	if (ret)
		return ret;

	return no_os_i2c_read(c->tgt, (uint8_t *)r, sizeof(*r), 1);
}

static int32_t pat_mismatch(const uint8_t *b, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++)
		if (b[i] != lrt_pat(i))
			return i;

	return -1;
}

static bool ret_ok(int want, int got)
{
	return want == ANY_ERR ? got != 0 : want == got;
}

static bool run_test(struct ctx *c, const struct test *t)
{
	uint32_t want_wr_bytes = 0, want_hash = LRT_HASH_INIT;
	char why[96] = "";
	struct lrt_record r;
	bool ok = true;
	int ret;

	for (size_t i = 0; i < NO_OS_ARRAY_SIZE(t->ops); i++) {
		const struct op *o = &t->ops[i];
		struct no_os_i2c_desc *d = o->absent ? c->absent : c->tgt;
		int32_t mm;

		if (o->kind == OP_NONE)
			break;

		if (o->kind == OP_WRITE) {
			/* A 1-byte write is a command byte, else the pattern */
			for (uint32_t j = 0; j < o->len; j++)
				tx[j] = o->len == 1 ? LRT_CMD_BLOCK : lrt_pat(j);
			if (!o->absent)
				for (uint32_t j = 0; j < o->len; j++)
					want_hash = lrt_hash(want_hash, tx[j]);
			if (!o->absent)
				want_wr_bytes += o->len;
			ret = no_os_i2c_write(d, tx, o->len, o->stop);
		} else {
			memset(rx, 0xEE, o->len);
			ret = no_os_i2c_read(d, rx, o->len, o->stop);
		}

		if (!ret_ok(o->ret, ret)) {
			snprintf(why, sizeof(why), "op%u ret=%d want %d",
				 (unsigned)i + 1, ret, o->ret);
			ok = false;
			break;
		}

		if (o->kind == OP_READ && !o->ret) {
			mm = pat_mismatch(rx, o->len);
			if (mm >= 0) {
				snprintf(why, sizeof(why),
					 "op%u data@%ld=%02x want %02x%s",
					 (unsigned)i + 1, mm, rx[mm],
					 lrt_pat(mm),
					 mm == 256 && rx[mm] == lrt_pat(0) ?
					 " (split at 256)" : "");
				ok = false;
				break;
			}
		}
	}

	ret = get_record(c, &r);
	if (ret) {
		if (ok)
			snprintf(why, sizeof(why), "record query ret=%d", ret);
		printf("%-4s %-52s FAIL  %s\n", t->id, t->desc, why);
		ctl_recover(c);
		if (!get_record(c, &r))
			c->seq = r.seq;
		return false;
	}

	if (ok && t->txns >= 0) {
		uint32_t got_txns = r.seq - c->seq;

		if (got_txns != (uint32_t)t->txns)
			snprintf(why, sizeof(why), "target saw %lu transactions, want %d",
				 got_txns, t->txns);
		else if (t->txns && (r.rd_match != t->rd_match ||
				     r.wr_match != t->wr_match))
			snprintf(why, sizeof(why), "target rd_match=%lu wr_match=%lu, want %lu/%lu%s",
				 r.rd_match, r.wr_match, t->rd_match, t->wr_match,
				 r.rd_match > t->rd_match ? " (split)" : "");
		else if (t->txns && (r.wr_bytes != want_wr_bytes ||
				     r.wr_hash != want_hash))
			snprintf(why, sizeof(why), "target wr_bytes=%lu hash=%08lx, want %lu/%08lx",
				 r.wr_bytes, r.wr_hash, want_wr_bytes, want_hash);
		else if (t->txns && r.errs)
			snprintf(why, sizeof(why), "target errs=0x%04lx", r.errs);
		ok = !why[0];
	}
	c->seq = r.seq;

	if (ok && t->txns > 0)
		printf("%-4s %-52s PASS  (tgt rd=%lu wr=%lu wb=%lu)\n", t->id,
		       t->desc, r.rd_match, r.wr_match, r.wr_bytes);
	else if (ok)
		printf("%-4s %-52s PASS\n", t->id, t->desc);
	else
		printf("%-4s %-52s FAIL  %s\n", t->id, t->desc, why);

	if (!ok)
		ctl_recover(c);

	return ok;
}

/* The ADP4202 shape over and over: w1 + Sr + read 257 */
static bool run_stress(struct ctx *c)
{
	uint8_t cmd = LRT_CMD_BLOCK;
	struct lrt_record r;
	uint32_t txns = 0;
	int fails = 0, ret;

	for (int i = 0; i < LRT_STRESS_N; i++) {
		memset(rx, 0xEE, 257);
		ret = no_os_i2c_write(c->tgt, &cmd, 1, 0);
		if (!ret)
			ret = no_os_i2c_read(c->tgt, rx, 257, 1);
		if (ret || pat_mismatch(rx, 257) >= 0)
			fails++;
	}

	/* A split read is still one transaction, but shows in the data */
	ret = get_record(c, &r);
	if (!ret) {
		txns = r.seq - c->seq;
		c->seq = r.seq;
	}

	printf("X1   %-52s %s  (%d bad, target saw %lu transactions)\n",
	       "200 x (w1 + Sr + read 257)",
	       !ret && !fails && txns == LRT_STRESS_N ? "PASS" : "FAIL",
	       fails, txns);
	if (!ret && txns != LRT_STRESS_N)
		fails++;
	if (ret || fails)
		ctl_recover(c);

	return !ret && !fails;
}

static int run_suite(struct ctx *c)
{
	struct lrt_record r;
	struct mallinfo before, after;
	int failed = 0, ret;

	ret = ctl_init(c);
	if (ret) {
		printf("\n-- %lu Hz: controller init failed: %d --\n", c->speed,
		       ret);
		return 1;
	}

	printf("\n-- requested %lu Hz, running at %u Hz --\n", c->speed,
	       MXC_I2C_GetFrequency(MXC_I2C_GET_I2C(c->i2c_id)));

	ret = get_record(c, &r);
	if (ret) {
		printf("target not reachable at 0x%02x: %d (check wiring)\n",
		       LRT_TGT_ADDR, ret);
		ctl_remove(c);
		return 1;
	}
	c->seq = r.seq;

	before = mallinfo();
	for (size_t i = 0; i < NO_OS_ARRAY_SIZE(tests); i++)
		failed += !run_test(c, &tests[i]);
	failed += !run_stress(c);

	/* A prologue left allocated would show here */
	after = mallinfo();
	printf("heap in use: %d -> %d bytes %s\n", before.uordblks,
	       after.uordblks, before.uordblks == after.uordblks ?
	       "(no leak)" : "LEAK?");
	if (before.uordblks != after.uordblks)
		failed++;

	ctl_remove(c);

	return failed;
}

int lrt_controller_run(uint32_t i2c_id)
{
	static const uint32_t speeds[] = { 100000, 400000 };
	struct ctx c = { .i2c_id = i2c_id };
	int failed = 0;

	printf("controller: no-OS I2C%lu -> target 0x%02x\n", i2c_id,
	       LRT_TGT_ADDR);

	for (size_t i = 0; i < NO_OS_ARRAY_SIZE(speeds); i++) {
		c.speed = speeds[i];
		failed += run_suite(&c);
	}

	return failed;
}
