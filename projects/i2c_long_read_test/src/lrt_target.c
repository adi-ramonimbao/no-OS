/*
 * Target side of the I2C long-read bench test: MAX32690 I2C2 (P1.7 SDA,
 * P1.8 SCL) at LRT_TGT_ADDR, driven directly through the MSDK.
 *
 * - Reads stream lrt_pat(i), restarting at i = 0 on every read address
 *   match.
 * - Written bytes are drained, counted and hashed.
 * - Each transaction is recorded at its STOP. A 1-byte write of
 *   LRT_CMD_STATS followed by a read returns the last record instead of the
 *   pattern, and is not recorded itself.
 *
 * no-OS defines I2C2_IRQHandler(), so the handler is installed in the RAM
 * vector table instead.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "mxc_device.h"
#include "mxc_delay.h"
#include "gpio.h"
#include "nvic_table.h"
#include "i2c.h"
#include "i2c_reva.h"
#include "mxc_pins.h"
#include "mxc_sys.h"
#include "lrt_common.h"
#include "lrt_target.h"

#define TGT		MXC_I2C2
#define TGT_IRQ		I2C2_IRQn
/* INTFL0 bits 8..14: ARB, TO, ADDR_NACK, DATA, DNR, START, STOP errors */
#define ERR_MASK	0x7F00U

#define QLEN		32

static volatile struct lrt_record cur;
static volatile struct lrt_record last;
static volatile uint32_t seq;
static volatile uint32_t idx;
static volatile bool serve_stats;
static struct lrt_record stats_snap;

static struct lrt_record q[QLEN];
static volatile uint32_t qhead, qtail, qdropped;

static void cur_reset(void)
{
	memset((void *)&cur, 0, sizeof(cur));
	cur.wr_hash = LRT_HASH_INIT;
}

static uint8_t tx_byte(uint32_t i)
{
	if (serve_stats)
		return i < sizeof(stats_snap) ? ((uint8_t *)&stats_snap)[i] : 0xFF;

	return lrt_pat(i);
}

static void fill(void)
{
	uint8_t b[8];
	int n = MXC_I2C_GetTXFIFOAvailable(TGT);

	if (n > (int)sizeof(b))
		n = sizeof(b);
	for (int i = 0; i < n; i++)
		b[i] = tx_byte(idx + i);
	n = MXC_I2C_WriteTXFIFO(TGT, b, n);
	idx += n;
}

static void drain_rx(void)
{
	while (!(TGT->status & MXC_F_I2C_STATUS_RX_EM)) {
		uint8_t b = (uint8_t)TGT->fifo;

		if (!cur.wr_bytes)
			cur.wr_first = b;
		cur.wr_hash = lrt_hash(cur.wr_hash, b);
		cur.wr_bytes++;
	}
}

static void lrt_target_isr(void)
{
	uint32_t f0 = TGT->intfl0;

	if (f0 & MXC_F_I2C_INTFL0_ADDR_MATCH) {
		/* Like the MSDK: CTRL.READ tells a read from a write */
		if (TGT->ctrl & MXC_F_I2C_CTRL_READ) {
			drain_rx();
			cur.rd_match++;
			idx = 0;
			serve_stats = cur.wr_bytes == 1 &&
				      cur.wr_first == LRT_CMD_STATS;
			if (serve_stats)
				stats_snap = *(struct lrt_record *)&last;
			TGT->intfl0 = MXC_F_I2C_INTFL0_ADDR_MATCH |
				      MXC_F_I2C_INTFL0_RD_ADDR_MATCH |
				      MXC_F_I2C_INTFL0_TX_LOCKOUT;
			fill();
			TGT->inten0 |= MXC_F_I2C_INTEN0_TX_THD;
		} else {
			cur.wr_match++;
			TGT->intfl0 = MXC_F_I2C_INTFL0_ADDR_MATCH |
				      MXC_F_I2C_INTFL0_WR_ADDR_MATCH |
				      MXC_F_I2C_INTFL0_RD_ADDR_MATCH;
		}
	}

	if (f0 & MXC_F_I2C_INTFL0_RX_THD) {
		drain_rx();
		TGT->intfl0 = MXC_F_I2C_INTFL0_RX_THD;
	}

	if (f0 & MXC_F_I2C_INTFL0_TX_LOCKOUT)
		TGT->intfl0 = MXC_F_I2C_INTFL0_TX_LOCKOUT;

	if ((TGT->inten0 & MXC_F_I2C_INTEN0_TX_THD) &&
	    (TGT->intfl0 & MXC_F_I2C_INTFL0_TX_THD)) {
		fill();
		TGT->intfl0 = MXC_F_I2C_INTFL0_TX_THD;
	}

	if (f0 & MXC_F_I2C_INTFL0_DONE) {
		cur.done++;
		TGT->intfl0 = MXC_F_I2C_INTFL0_DONE;
	}

	if (f0 & ERR_MASK) {
		cur.errs |= f0 & ERR_MASK;
		TGT->intfl0 = f0 & ERR_MASK;
	}

	if (f0 & MXC_F_I2C_INTFL0_STOP) {
		drain_rx();
		TGT->inten0 &= ~MXC_F_I2C_INTEN0_TX_THD;
		TGT->intfl0 = MXC_F_I2C_INTFL0_STOP | MXC_F_I2C_INTFL0_TX_THD;
		/* The STOP flag is set for any STOP on the bus, addressed or not */
		if (!serve_stats && (cur.rd_match || cur.wr_match)) {
			cur.seq = ++seq;
			last = cur;
			if (qhead - qtail < QLEN) {
				q[qhead % QLEN] = *(struct lrt_record *)&cur;
				qhead++;
			} else {
				qdropped++;
			}
		}
		serve_stats = false;
		cur_reset();
	}
}

static void cfg_in(mxc_gpio_regs_t *port, uint32_t mask, mxc_gpio_pad_t pad)
{
	mxc_gpio_cfg_t c = { port, mask, MXC_GPIO_FUNC_IN, pad,
			     MXC_GPIO_VSSEL_VDDIO, MXC_GPIO_DRVSTR_0 };

	MXC_GPIO_Config(&c);
}

static void cfg_out_low(mxc_gpio_regs_t *port, uint32_t mask)
{
	mxc_gpio_cfg_t c = { port, mask, MXC_GPIO_FUNC_OUT, MXC_GPIO_PAD_NONE,
			     MXC_GPIO_VSSEL_VDDIO, MXC_GPIO_DRVSTR_0 };

	MXC_GPIO_Config(&c);
	MXC_GPIO_OutClr(port, mask);
}

static int pin_in(mxc_gpio_regs_t *port, uint32_t mask)
{
	return !!MXC_GPIO_InGet(port, mask);
}

#define P27 MXC_GPIO2, MXC_GPIO_PIN_7
#define P28 MXC_GPIO2, MXC_GPIO_PIN_8
#define P17 MXC_GPIO1, MXC_GPIO_PIN_7
#define P18 MXC_GPIO1, MXC_GPIO_PIN_8

/* GPIO-mode check of the pull-ups and, in loopback, the two wires */
static void wiring_check(void)
{
	cfg_in(P17, MXC_GPIO_PAD_NONE);
	cfg_in(P18, MXC_GPIO_PAD_NONE);
#if LRT_CONTROLLER
	cfg_in(P27, MXC_GPIO_PAD_NONE);
	cfg_in(P28, MXC_GPIO_PAD_NONE);
#endif
	MXC_Delay(MXC_DELAY_MSEC(2));
	/* 1 = an external pull-up is present */
	printf("idle, no internal pulls: P1.7(SDA2)=%d P1.8(SCL2)=%d",
	       pin_in(P17), pin_in(P18));
#if LRT_CONTROLLER
	printf(" P2.7(SDA0)=%d P2.8(SCL0)=%d\n", pin_in(P27), pin_in(P28));

	/* Drive one I2C0 pin low; the others have internal pull-ups */
	cfg_in(P17, MXC_GPIO_PAD_PULL_UP);
	cfg_in(P18, MXC_GPIO_PAD_PULL_UP);
	cfg_in(P28, MXC_GPIO_PAD_PULL_UP);
	cfg_out_low(P27);
	MXC_Delay(MXC_DELAY_MSEC(2));
	printf("P2.7 driven low: P1.7=%d P1.8=%d P2.8=%d (want 0 1 1)\n",
	       pin_in(P17), pin_in(P18), pin_in(P28));

	cfg_in(P27, MXC_GPIO_PAD_PULL_UP);
	cfg_out_low(P28);
	MXC_Delay(MXC_DELAY_MSEC(2));
	printf("P2.8 driven low: P1.7=%d P1.8=%d P2.7=%d (want 1 0 1)\n",
	       pin_in(P17), pin_in(P18), pin_in(P27));

	cfg_in(P27, MXC_GPIO_PAD_NONE);
	cfg_in(P28, MXC_GPIO_PAD_NONE);
	cfg_in(P17, MXC_GPIO_PAD_NONE);
	cfg_in(P18, MXC_GPIO_PAD_NONE);
#else
	printf("\n");
#endif
}

int lrt_target_init(void)
{
	int ret;

	cur_reset();
	wiring_check();

	/*
	 * Like the no-OS MAX32690 driver, mux only the wired pin set:
	 * MXC_I2C_Init() also muxes P0.13/P0.14, and with both sets live
	 * (A2 errata #16) bus recovery reads SCL back high and fails.
	 */
	MXC_I2C_Shutdown(TGT);
	MXC_SYS_ClockEnable(MXC_SYS_PERIPH_CLOCK_I2C2);
	MXC_GPIO_Config(&gpio_cfg_i2c2);
	ret = MXC_I2C_RevA_Init((mxc_i2c_reva_regs_t *)TGT, 0, LRT_TGT_ADDR);
	if (ret) {
		printf("MXC_I2C_RevA_Init(I2C2) = %d, ctrl=0x%08lx (SCL=%lu SDA=%lu)\n",
		       ret, TGT->ctrl, (TGT->ctrl >> 8) & 1,
		       (TGT->ctrl >> 9) & 1);
		return ret;
	}

	MXC_I2C_SetFrequency(TGT, 400000);
	MXC_I2C_SetTXThreshold(TGT, 2);
	MXC_I2C_SetRXThreshold(TGT, 1);
	TGT->intfl0 = TGT->intfl0;
	TGT->inten0 = MXC_F_I2C_INTEN0_ADDR_MATCH | MXC_F_I2C_INTEN0_DONE |
		      MXC_F_I2C_INTEN0_STOP | MXC_F_I2C_INTEN0_TX_LOCKOUT |
		      MXC_F_I2C_INTEN0_RX_THD | ERR_MASK;
	MXC_NVIC_SetVector(TGT_IRQ, lrt_target_isr);
	NVIC_EnableIRQ(TGT_IRQ);

	return 0;
}

void lrt_target_print_log(void)
{
	static uint32_t reported;

	/* The stress test outruns the console; its gaps show in seq */
	if (qdropped != reported) {
		printf("(%lu log lines dropped)\n", qdropped - reported);
		reported = qdropped;
	}

	while (qtail != qhead) {
		struct lrt_record x = q[qtail % QLEN];

		qtail++;
		printf("xact %4lu: rd_match=%lu wr_match=%lu wr_bytes=%lu",
		       x.seq, x.rd_match, x.wr_match, x.wr_bytes);
		if (x.wr_bytes)
			printf(" (first=0x%02lx hash=0x%08lx)", x.wr_first,
			       x.wr_hash);
		printf(" done=%lu", x.done);
		if (x.errs)
			printf(" errs=0x%04lx", x.errs);
		printf("%s\n", x.rd_match > 1 ? "  <-- 2+ reads" : "");
	}
}
