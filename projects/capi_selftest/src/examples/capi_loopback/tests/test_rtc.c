/***************************************************************************//**
 * @file test_rtc.c
 * @brief CAPI RTC tests.
 *
 * The RTC needs no external wiring. The RTC's clock may be an uncalibrated
 * internal oscillator (see the platform's RTC_PLATFORM_INIT()), so every
 * timing check here is a loose "it works" bound rather than an accuracy
 * measurement.
 *
 * Everything backend-specific comes from parameters.h:
 *
 *   RTC_OPS, RTC_IDENTIFIER	backend under test (suite skipped if unset)
 *   RTC_BAD_IDENTIFIER		an identifier init must reject
 *   RTC_PLATFORM_INIT()	optional clock/pin setup run once
 *   RTC_STATIC_HANDLE()	optional caller-provided handle initializer
 *   RTC_TICK_FREQ_HZ		counter rate after init with tick_freq_hz = 0
 *   RTC_BAD_TICK_FREQ_HZ	optional tick rate init/set_tick_config reject
 *   RTC_HAS_TICK_IRQ		0: TICK irq_* calls must return -ENOTSUP
 *
 *   RTC_HAS_CALIBRATION	set/get_calibration supported; then
 *     RTC_CAL_STEP_PPB		trim resolution (rounded up to whole ppb)
 *     RTC_CAL_MIN_PPB/MAX_PPB	trim range
 *
 *   RTC_NUM_ALARMS		alarm ids are 0..RTC_NUM_ALARMS-1
 *   RTC_ALARM_ID		one-shot alarm channel
 *   RTC_ALARM_EXTRA_TYPE	type behind capi_rtc_alarm_config.extra
 *   RTC_ALARM_SET(cfg, extra, value)
 *				fill an alarm config; for RTC_ALARM_ID the value
 *				is the absolute count the alarm fires at
 *   RTC_ALARM_VALUE(cfg, extra), RTC_ALARM_ENABLED(cfg, extra)
 *				decode the result of capi_rtc_alarm_get()
 *   RTC_ALARM_REQUIRES_EXTRA	alarm_set/get reject a NULL extra
 *   RTC_ALARM_EVENT_EXTRA_IS_ID	alarm callbacks get the alarm id as event_extra
 *
 *   RTC_HAS_PERIODIC_ALARM	self-reloading alarm; then
 *     RTC_PERIODIC_ALARM_ID, RTC_PERIODIC_ALARM_VALUE (RTC_ALARM_SET value),
 *     RTC_PERIODIC_ALARM_PERIOD_MS (the period that value gives)
 *
 *   RTC_HAS_PLATFORM_EVENT	extra event past CAPI_RTC_EVENT_LIMIT that fires
 *				on its own once enabled; then RTC_PLATFORM_EVENT
 *
 *   RTC_HAS_OUTPUT		clock output selected through config.extra and
 *				driven only while the counter runs; then
 *     RTC_OUTPUT_EXTRA_TYPE, RTC_OUTPUT_EXTRA_INIT, RTC_OUTPUT_EXTRA_BAD_INIT,
 *     RTC_OUTPUT_IS_ON()	read the output enable from hardware
 *     RTC_RAW_START()		start the counter behind the driver; 0 on success
 *
 * Copyright (c) 2026 Analog Devices, Inc.
 * SPDX-License-Identifier: BSD-3-Clause
 *******************************************************************************/

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include "capi_time.h"
#include "parameters.h"
#include "test_framework.h"
#include "test_rtc.h"

#ifndef RTC_OPS

/* No RTC backend mapped on this platform: the suite compiles out to a skip. */
int test_rtc(void)
{
	static const struct test_case stub[] = {
		{ "NOT_CONFIGURED", NULL, false },
	};

	return test_framework_run_cases("RTC", stub, 1U);
}

#else /* RTC_OPS defined - full implementation follows */

#include "capi_rtc.h"

#ifndef RTC_PLATFORM_INIT
#define RTC_PLATFORM_INIT()
#endif

#define RTC_MODULE		"RTC"
/* Generous: the internal oscillator can be well off nominal. */
#define RTC_ALARM_TIMEOUT_US	6000000U
#define RTC_POLL_STEP_US	10000U
/* Fine enough to catch a counter tick within 0.1 ms. */
#define RTC_TICK_POLL_US	100U
/* Ends mid-tick, so the sampled count is not read on a rollover. */
#define RTC_RATE_WINDOW_MS	(3000U + 500U / RTC_TICK_FREQ_HZ)
/* Ticks the counter can advance within a 10 ms poll step. */
#define RTC_TICKS_PER_10MS	(RTC_TICK_FREQ_HZ / 100U)
/* One-shot alarms are set ~2 s ahead of the current count. */
#define RTC_ALARM_DELAY_TICKS	(2U * RTC_TICK_FREQ_HZ)

struct rtc_cb_log {
	volatile uint32_t alarm_count;
	volatile uint32_t platform_count;
	volatile uint32_t other_count;
	volatile uint32_t last_event;
	volatile int last_extra;
	void *volatile last_arg;
};

static struct rtc_cb_log rtc_global_log;
static struct rtc_cb_log rtc_alarm_log;

static void rtc_log_event(struct rtc_cb_log *log, uint32_t event, void *arg,
			  int event_extra)
{
	log->last_event = event;
	log->last_extra = event_extra;
	log->last_arg = arg;

	if (event == CAPI_RTC_EVENT_ALARM)
		log->alarm_count++;
#if RTC_HAS_PLATFORM_EVENT
	else if (event == RTC_PLATFORM_EVENT)
		log->platform_count++;
#endif
	else
		log->other_count++;
}

static void rtc_global_cb(uint32_t event, void *arg, int event_extra)
{
	rtc_log_event(&rtc_global_log, event, arg, event_extra);
}

static void rtc_alarm_cb(uint32_t event, void *arg, int event_extra)
{
	rtc_log_event(&rtc_alarm_log, event, arg, event_extra);
}

static void rtc_reset_logs(void)
{
	rtc_global_log = (struct rtc_cb_log) { 0 };
	rtc_alarm_log = (struct rtc_cb_log) { 0 };
}

/**
 * @brief Initialize the RTC. *rtc must be NULL (allocate) or a static handle.
 */
static int rtc_init(struct capi_rtc_handle **rtc, uint32_t initial_count,
		    uint32_t flags, capi_rtc_event_callback callback, void *arg)
{
	struct capi_rtc_config config = {
		.ops = RTC_OPS,
		.identifier = RTC_IDENTIFIER,
		.initial_count = initial_count,
		.flags = flags,
		.callback = callback,
		.arg = arg,
	};

	return capi_rtc_init(rtc, &config);
}

static uint32_t rtc_count(struct capi_rtc_handle *rtc)
{
	uint32_t count = 0U;

	(void)capi_rtc_get_count(rtc, &count);

	return count;
}

static uint32_t rtc_status(struct capi_rtc_handle *rtc)
{
	uint32_t status = 0U;

	(void)capi_rtc_get_status(rtc, &status);

	return status;
}

static bool rtc_alarm_pending(struct capi_rtc_handle *rtc)
{
	bool pending = false;

	if (capi_rtc_is_irq_pending(rtc, CAPI_RTC_EVENT_ALARM, &pending))
		return false;

	return pending;
}

/*
 * deinit disables every alarm/IRQ, stops the counter and frees the handle.
 * The trim register survives deinit, so put it back to 0 as well.
 */
#define CLEANUP \
	do { \
		if (rtc != NULL) { \
			(void)capi_rtc_set_calibration(rtc, 0); \
			(void)capi_rtc_deinit(rtc); \
			rtc = NULL; \
		} \
	} while (0)

/**
 * @brief Argument validation that does not touch the hardware.
 */
static int rtc_param_checks(void)
{
	struct capi_rtc_handle *rtc = NULL;
	struct capi_rtc_config config = {
		.ops = RTC_OPS,
		.identifier = RTC_BAD_IDENTIFIER,
	};

	TEST_SECTION("PARAMS");

	TEST_ASSERT_EQ(capi_rtc_init(NULL, &config), -EINVAL, "INIT_NULL_HANDLE");
	TEST_ASSERT_EQ(capi_rtc_init(&rtc, NULL), -EINVAL, "INIT_NULL_CONFIG");
	TEST_ASSERT_EQ(capi_rtc_init(&rtc, &config), -EINVAL,
		       "INIT_BAD_IDENTIFIER");

#ifdef RTC_BAD_TICK_FREQ_HZ
	config.identifier = RTC_IDENTIFIER;
	config.tick.tick_freq_hz = RTC_BAD_TICK_FREQ_HZ;
	TEST_ASSERT_EQ(capi_rtc_init(&rtc, &config), -EINVAL,
		       "INIT_BAD_TICK_FREQ");
#endif
	TEST_ASSERT(rtc == NULL, "HANDLE_UNTOUCHED");

	TEST_ASSERT_EQ(capi_rtc_deinit(NULL), -EINVAL, "DEINIT_NULL");

	return 0;
}

/**
 * @brief Init/deinit lifetime, singleton guard, static handle and tick config.
 */
static int rtc_init_deinit(void)
{
	struct capi_rtc_handle *rtc = NULL;
	struct capi_rtc_handle *rtc2 = NULL;
#ifdef RTC_STATIC_HANDLE
	struct capi_rtc_handle *static_rtc = RTC_STATIC_HANDLE();
#endif
	struct capi_rtc_tick_config tick = { 0 };
	uint32_t count = 0U;
	uint32_t freq = 0U;

	TEST_SECTION("INIT_DEINIT");

	TEST_ASSERT_EQ_OR_CLEANUP(rtc_init(&rtc, 1000U, 0U, NULL, NULL), 0,
				  "INIT");
	TEST_ASSERT_OR_CLEANUP(rtc != NULL, "HANDLE_ALLOCATED");
	TEST_ASSERT_OR_CLEANUP(rtc->init_allocated, "INIT_ALLOCATED_FLAG");

	TEST_ASSERT_EQ_OR_CLEANUP(rtc_init(&rtc2, 0U, 0U, NULL, NULL), -EBUSY,
				  "INIT_TWICE_BUSY");

	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_get_count(rtc, &count), 0, "GET_COUNT");
	TEST_ASSERT_EQ_OR_CLEANUP(count, 1000U, "INITIAL_COUNT");
	TEST_ASSERT_EQ_OR_CLEANUP(rtc_status(rtc), 0U, "STATUS_IDLE");

	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_get_tick_freq(rtc, &freq), 0,
				  "GET_TICK_FREQ");
	TEST_ASSERT_EQ_OR_CLEANUP(freq, RTC_TICK_FREQ_HZ, "TICK_FREQ_VALUE");
	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_get_tick_config(rtc, &tick), 0,
				  "GET_TICK_CONFIG");
	TEST_ASSERT_EQ_OR_CLEANUP(tick.tick_freq_hz, RTC_TICK_FREQ_HZ,
				  "TICK_CONFIG_VALUE");

	tick.tick_freq_hz = 0U;
	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_set_tick_config(rtc, &tick), 0,
				  "SET_TICK_DEFAULT");
	tick.tick_freq_hz = RTC_TICK_FREQ_HZ;
	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_set_tick_config(rtc, &tick), 0,
				  "SET_TICK_NATIVE");
#ifdef RTC_BAD_TICK_FREQ_HZ
	tick.tick_freq_hz = RTC_BAD_TICK_FREQ_HZ;
	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_set_tick_config(rtc, &tick), -EINVAL,
				  "SET_TICK_BAD");
#endif

	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_deinit(rtc), 0, "DEINIT");
	rtc = NULL;

#ifdef RTC_STATIC_HANDLE

	/* Caller-provided handle: init must use it and not mark it allocated. */
	rtc = static_rtc;
	TEST_ASSERT_EQ_OR_CLEANUP(rtc_init(&rtc, 0U, 0U, NULL, NULL), 0,
				  "INIT_STATIC");
	TEST_ASSERT_OR_CLEANUP(rtc == static_rtc, "STATIC_HANDLE_KEPT");
	TEST_ASSERT_OR_CLEANUP(!rtc->init_allocated, "STATIC_NOT_ALLOCATED");
	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_deinit(rtc), 0, "DEINIT_STATIC");
	rtc = NULL;

	/* The static handle stays valid memory, so a second deinit is safe. */
	TEST_ASSERT_EQ(capi_rtc_deinit(static_rtc), -EINVAL, "DEINIT_TWICE");
#endif

	return 0;
}

/**
 * @brief Start/stop and a loose tick-rate check against the CAPI uptime clock.
 */
static int rtc_count_runs(void)
{
	struct capi_rtc_handle *rtc = NULL;
	uint64_t up0 = 0U;
	uint64_t up1 = 0U;
	uint64_t expected;
	uint32_t start = 0U;
	uint32_t count0 = 0U;
	uint32_t count1 = 0U;

	TEST_SECTION("COUNT_RUNS");

	TEST_ASSERT_EQ_OR_CLEANUP(rtc_init(&rtc, 0U, 0U, NULL, NULL), 0, "INIT");
	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_start(rtc), 0, "START");
	TEST_ASSERT_OR_CLEANUP(rtc_status(rtc) & CAPI_RTC_STATUS_RUNNING,
			       "STATUS_RUNNING");
	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_get_count(rtc, &start), 0,
				  "GET_COUNT_1");

	/*
	 * Start the window on a counter tick, so the count delta is not
	 * off by one. No framework output between the two samples.
	 */
	TEST_WAIT_UNTIL(rtc_count(rtc) != start, RTC_ALARM_TIMEOUT_US,
			RTC_TICK_POLL_US);
	(void)capi_uptime(&up0);
	count0 = rtc_count(rtc);
	capi_wait_ms(RTC_RATE_WINDOW_MS);
	count1 = rtc_count(rtc);
	(void)capi_uptime(&up1);

	TEST_ASSERT_OR_CLEANUP(count0 != start, "COUNT_ADVANCED");

	expected = (up1 - up0) * RTC_TICK_FREQ_HZ / 1000000U;
	TEST_VALUE("RTC_TICKS", count1 - count0);
	TEST_VALUE("EXPECTED_TICKS", expected);
	TEST_ASSERT_RANGE_OR_CLEANUP(count1 - count0, expected / 2U,
				     expected * 3U / 2U, "RATE_LOOSE");

	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_stop(rtc), 0, "STOP");
	TEST_ASSERT_OR_CLEANUP(!(rtc_status(rtc) & CAPI_RTC_STATUS_RUNNING),
			       "STATUS_STOPPED");

	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_get_count(rtc, &count0), 0, "GET_COUNT_2");
	capi_wait_ms(1500U);
	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_get_count(rtc, &count1), 0, "GET_COUNT_3");
	TEST_ASSERT_EQ_OR_CLEANUP(count1, count0, "FROZEN_WHEN_STOPPED");

	CLEANUP;

	return 0;
}

/**
 * @brief set_count while stopped and running, and 32-bit wraparound.
 */
static int rtc_set_count(void)
{
	struct capi_rtc_handle *rtc = NULL;
	uint32_t count = 0U;

	TEST_SECTION("SET_COUNT");

	TEST_ASSERT_EQ_OR_CLEANUP(rtc_init(&rtc, 0U, 0U, NULL, NULL), 0, "INIT");

	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_set_count(rtc, 42U), 0, "SET_STOPPED");
	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_get_count(rtc, &count), 0, "GET_COUNT_1");
	TEST_ASSERT_EQ_OR_CLEANUP(count, 42U, "VALUE_STOPPED");
	TEST_ASSERT_OR_CLEANUP(!(rtc_status(rtc) & CAPI_RTC_STATUS_RUNNING),
			       "STAYS_STOPPED");

	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_start(rtc), 0, "START");
	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_set_count(rtc, 5000U), 0,
				  "SET_RUNNING");
	TEST_ASSERT_OR_CLEANUP(rtc_status(rtc) & CAPI_RTC_STATUS_RUNNING,
			       "STAYS_RUNNING");
	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_get_count(rtc, &count), 0, "GET_COUNT_2");
	TEST_ASSERT_RANGE_OR_CLEANUP(count, 5000U, 5001U + RTC_TICKS_PER_10MS,
				     "VALUE_RUNNING");

	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_set_count(rtc, UINT32_MAX), 0,
				  "SET_MAX");
	capi_wait_ms(1500U);
	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_get_count(rtc, &count), 0, "GET_COUNT_3");
	TEST_ASSERT_LTE_OR_CLEANUP(count, 2U * RTC_TICK_FREQ_HZ, "WRAPPED");

	CLEANUP;

	return 0;
}

/**
 * @brief CAPI_RTC_INIT_FLAG_PRESERVE_COUNT keeps the hardware counter.
 */
static int rtc_preserve_count(void)
{
	struct capi_rtc_handle *rtc = NULL;
	uint32_t count = 0U;

	TEST_SECTION("PRESERVE_COUNT");

	TEST_ASSERT_EQ_OR_CLEANUP(rtc_init(&rtc, 777U, 0U, NULL, NULL), 0,
				  "INIT");
	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_deinit(rtc), 0, "DEINIT");
	rtc = NULL;

	TEST_ASSERT_EQ_OR_CLEANUP(rtc_init(&rtc, 0U,
					   CAPI_RTC_INIT_FLAG_PRESERVE_COUNT,
					   NULL, NULL), 0, "INIT_PRESERVE");
	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_get_count(rtc, &count), 0, "GET_COUNT");
	TEST_ASSERT_EQ_OR_CLEANUP(count, 777U, "COUNT_PRESERVED");

	/* And without the flag the counter is reloaded. */
	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_deinit(rtc), 0, "DEINIT_2");
	rtc = NULL;
	TEST_ASSERT_EQ_OR_CLEANUP(rtc_init(&rtc, 5U, 0U, NULL, NULL), 0,
				  "INIT_RELOAD");
	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_get_count(rtc, &count), 0, "GET_COUNT_2");
	TEST_ASSERT_EQ_OR_CLEANUP(count, 5U, "COUNT_RELOADED");

	CLEANUP;

	return 0;
}

#if RTC_HAS_CALIBRATION
/**
 * @brief Calibration round-trip, rounding to the trim step and range limits.
 */
static int rtc_calibration(void)
{
	static const struct {
		const char *name;
		int32_t set_ppb;
		int ret;
	} cases[] = {
		{ "CAL_0", 0, 0 },
		{ "CAL_5_STEPS", 5 * RTC_CAL_STEP_PPB, 0 },
		{ "CAL_ROUND_DOWN", RTC_CAL_STEP_PPB * 3 / 2 - 1, 0 },
		{ "CAL_ROUND_UP", RTC_CAL_STEP_PPB * 3 / 2, 0 },
		{ "CAL_NEG_ROUND_DOWN", -(RTC_CAL_STEP_PPB * 3 / 2 - 1), 0 },
		{ "CAL_NEG_ROUND_UP", -(RTC_CAL_STEP_PPB * 3 / 2), 0 },
		{ "CAL_MAX", RTC_CAL_MAX_PPB, 0 },
		{ "CAL_MAX_ROUNDED", RTC_CAL_MAX_PPB + RTC_CAL_STEP_PPB / 2 - 1, 0 },
		{ "CAL_MIN", RTC_CAL_MIN_PPB, 0 },
		{ "CAL_OVER_MAX", RTC_CAL_MAX_PPB + RTC_CAL_STEP_PPB, -ERANGE },
		{ "CAL_UNDER_MIN", RTC_CAL_MIN_PPB - RTC_CAL_STEP_PPB, -ERANGE },
	};
	struct capi_rtc_handle *rtc = NULL;
	int32_t expected = 0;
	int32_t ppb = 0;

	TEST_SECTION("CALIBRATION");

	TEST_ASSERT_EQ_OR_CLEANUP(rtc_init(&rtc, 0U, 0U, NULL, NULL), 0, "INIT");

	/*
	 * An accepted trim must read back within half a step of the request.
	 * Each rejected (-ERANGE) case must leave the previous trim in place,
	 * so they follow CAL_MIN and expect its value back.
	 */
	for (unsigned int i = 0U; i < sizeof(cases) / sizeof(cases[0]); i++) {
		TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_set_calibration(rtc,
					  cases[i].set_ppb), cases[i].ret,
					  cases[i].name);
		TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_get_calibration(rtc, &ppb), 0,
					  "GET_CALIBRATION");
		if (cases[i].ret == 0) {
			expected = cases[i].set_ppb;
			TEST_ASSERT_RANGE_OR_CLEANUP(ppb,
					expected - RTC_CAL_STEP_PPB / 2,
					expected + RTC_CAL_STEP_PPB / 2,
					cases[i].name);
		} else {
			TEST_ASSERT_EQ_OR_CLEANUP(ppb, expected, cases[i].name);
		}
		expected = ppb;
	}

	CLEANUP;

	return 0;
}
#else
#define rtc_calibration	NULL
#endif /* RTC_HAS_CALIBRATION */

/**
 * @brief Alarm/event argument validation on an initialized RTC.
 */
static int rtc_alarm_args(void)
{
	struct capi_rtc_handle *rtc = NULL;
	RTC_ALARM_EXTRA_TYPE extra = { 0 };
	struct capi_rtc_alarm_config alarm = { .extra = &extra };
#if RTC_ALARM_REQUIRES_EXTRA
	struct capi_rtc_alarm_config alarm_no_extra = { .extra = NULL };
#endif
#if !RTC_HAS_TICK_IRQ
	bool pending = false;
#endif

	TEST_SECTION("ALARM_ARGS");

	TEST_ASSERT_EQ_OR_CLEANUP(rtc_init(&rtc, 0U, 0U, NULL, NULL), 0, "INIT");

	RTC_ALARM_SET(alarm, extra, 10U);
	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_alarm_set(rtc, RTC_NUM_ALARMS,
				  &alarm), -EINVAL, "SET_BAD_ID");
#if RTC_ALARM_REQUIRES_EXTRA
	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_alarm_set(rtc, RTC_ALARM_ID,
				  &alarm_no_extra), -EINVAL, "SET_NULL_EXTRA");
#endif
#if RTC_HAS_PERIODIC_ALARM
	/* A zero period would fire continuously. */
	RTC_ALARM_SET(alarm, extra, 0U);
	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_alarm_set(rtc, RTC_PERIODIC_ALARM_ID,
				  &alarm), -EINVAL, "SET_PERIODIC_ZERO");
#endif

	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_alarm_get(rtc, RTC_NUM_ALARMS,
				  &alarm), -EINVAL, "GET_BAD_ID");
#if RTC_ALARM_REQUIRES_EXTRA
	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_alarm_get(rtc, RTC_ALARM_ID,
				  &alarm_no_extra), -EINVAL, "GET_NULL_EXTRA");
#endif
	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_alarm_disable(rtc, RTC_NUM_ALARMS),
				  -EINVAL, "DISABLE_BAD_ID");
	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_alarm_set_callback(rtc,
				  RTC_NUM_ALARMS, rtc_alarm_cb, NULL), -EINVAL,
				  "SET_CALLBACK_BAD_ID");

#if !RTC_HAS_TICK_IRQ
	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_irq_enable(rtc, CAPI_RTC_EVENT_TICK),
				  -ENOTSUP, "IRQ_ENABLE_TICK_NOTSUP");
	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_irq_disable(rtc, CAPI_RTC_EVENT_TICK),
				  -ENOTSUP, "IRQ_DISABLE_TICK_NOTSUP");
	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_is_irq_pending(rtc,
				  CAPI_RTC_EVENT_TICK, &pending), -ENOTSUP,
				  "IS_PENDING_TICK_NOTSUP");
#endif

	CLEANUP;

	return 0;
}

/**
 * @brief One-shot alarm with no callbacks: flag latches, status reflects it.
 */
static int rtc_alarm_poll(void)
{
	const uint32_t at = 100U + RTC_ALARM_DELAY_TICKS;
	struct capi_rtc_handle *rtc = NULL;
	RTC_ALARM_EXTRA_TYPE extra = { 0 };
	struct capi_rtc_alarm_config alarm = { .extra = &extra };
	RTC_ALARM_EXTRA_TYPE get_extra = { 0 };
	struct capi_rtc_alarm_config get_alarm = { .extra = &get_extra };
	uint32_t count = 0U;
	bool fired;

	TEST_SECTION("ALARM_POLL");

	TEST_ASSERT_EQ_OR_CLEANUP(rtc_init(&rtc, 100U, 0U, NULL, NULL), 0,
				  "INIT");
	RTC_ALARM_SET(alarm, extra, at);
	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_alarm_set(rtc, RTC_ALARM_ID, &alarm),
				  0, "ALARM_SET");
	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_alarm_get(rtc, RTC_ALARM_ID,
				  &get_alarm), 0, "ALARM_GET");
	TEST_ASSERT_EQ_OR_CLEANUP(RTC_ALARM_VALUE(get_alarm, get_extra), at,
				  "ALARM_GET_VALUE");
	TEST_ASSERT_OR_CLEANUP(RTC_ALARM_ENABLED(get_alarm, get_extra),
			       "ALARM_GET_ENABLED");
	TEST_ASSERT_OR_CLEANUP(rtc_status(rtc) & CAPI_RTC_STATUS_ALARM_ACTIVE,
			       "STATUS_ACTIVE");
	TEST_ASSERT_OR_CLEANUP(!rtc_alarm_pending(rtc), "NOT_PENDING_YET");

	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_start(rtc), 0, "START");
	TEST_WAIT_UNTIL(rtc_alarm_pending(rtc), RTC_ALARM_TIMEOUT_US,
			RTC_POLL_STEP_US);
	fired = rtc_alarm_pending(rtc);
	(void)capi_rtc_get_count(rtc, &count);

	TEST_ASSERT_OR_CLEANUP(fired, "ALARM_FIRED");
	TEST_ASSERT_RANGE_OR_CLEANUP(count, at, at + 1U + RTC_TICKS_PER_10MS,
				     "FIRED_AT_COUNT");
	TEST_ASSERT_OR_CLEANUP(rtc_status(rtc) & CAPI_RTC_STATUS_ALARM_PENDING,
			       "STATUS_PENDING");

	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_clear_status(rtc,
				  CAPI_RTC_STATUS_ALARM_PENDING), 0,
				  "CLEAR_STATUS");
	TEST_ASSERT_OR_CLEANUP(!rtc_alarm_pending(rtc), "PENDING_CLEARED");

	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_alarm_disable(rtc, RTC_ALARM_ID), 0,
				  "ALARM_DISABLE");
	TEST_ASSERT_OR_CLEANUP(!(rtc_status(rtc) & CAPI_RTC_STATUS_ALARM_ACTIVE),
			       "STATUS_INACTIVE");
	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_alarm_get(rtc, RTC_ALARM_ID,
				  &get_alarm), 0, "ALARM_GET_2");
	TEST_ASSERT_OR_CLEANUP(!RTC_ALARM_ENABLED(get_alarm, get_extra),
			       "ALARM_GET_DISABLED");

	CLEANUP;

	return 0;
}

/**
 * @brief One-shot alarm delivered through the global (init) callback.
 */
static int rtc_alarm_global_cb(void)
{
	struct capi_rtc_handle *rtc = NULL;
	RTC_ALARM_EXTRA_TYPE extra = { 0 };
	struct capi_rtc_alarm_config alarm = { .extra = &extra };

	TEST_SECTION("ALARM_GLOBAL_CB");

	rtc_reset_logs();

	TEST_ASSERT_EQ_OR_CLEANUP(rtc_init(&rtc, 200U, 0U, rtc_global_cb,
					   &rtc_global_log), 0, "INIT");
	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_irq_enable(rtc, CAPI_RTC_EVENT_ALARM),
				  0, "IRQ_ENABLE_ALARM");
	RTC_ALARM_SET(alarm, extra, 200U + RTC_ALARM_DELAY_TICKS);
	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_alarm_set(rtc, RTC_ALARM_ID, &alarm),
				  0, "ALARM_SET");
	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_start(rtc), 0, "START");

	TEST_WAIT_UNTIL(rtc_global_log.alarm_count != 0U, RTC_ALARM_TIMEOUT_US,
			RTC_POLL_STEP_US);
	/* A one-shot alarm must not keep firing. */
	capi_wait_ms(200U);

	TEST_ASSERT_EQ_OR_CLEANUP(rtc_global_log.alarm_count, 1U,
				  "CALLBACK_FIRED_ONCE");
	TEST_ASSERT_EQ_OR_CLEANUP(rtc_global_log.last_event,
				  CAPI_RTC_EVENT_ALARM, "CALLBACK_EVENT");
#if RTC_ALARM_EVENT_EXTRA_IS_ID
	TEST_ASSERT_EQ_OR_CLEANUP(rtc_global_log.last_extra, RTC_ALARM_ID,
				  "CALLBACK_EXTRA_ID");
#endif
	TEST_ASSERT_OR_CLEANUP(rtc_global_log.last_arg == &rtc_global_log,
			       "CALLBACK_ARG");
	TEST_ASSERT_OR_CLEANUP(!rtc_alarm_pending(rtc), "ISR_CLEARED_FLAG");

	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_irq_disable(rtc, CAPI_RTC_EVENT_ALARM),
				  0, "IRQ_DISABLE_ALARM");

	CLEANUP;

	return 0;
}

#if RTC_HAS_PERIODIC_ALARM
/**
 * @brief Periodic alarm, per-alarm callback precedence over the global
 *	  callback, and fallback to the global one once removed.
 */
static int rtc_alarm_periodic_cb(void)
{
	/* Wide window for an uncalibrated oscillator. */
	const uint32_t nominal = 1000U / RTC_PERIODIC_ALARM_PERIOD_MS;
	struct capi_rtc_handle *rtc = NULL;
	RTC_ALARM_EXTRA_TYPE extra = { 0 };
	struct capi_rtc_alarm_config alarm = { .extra = &extra };
	RTC_ALARM_EXTRA_TYPE get_extra = { 0 };
	struct capi_rtc_alarm_config get_alarm = { .extra = &get_extra };
	uint32_t snapshot;

	TEST_SECTION("ALARM_PERIODIC_CB");

	rtc_reset_logs();

	TEST_ASSERT_EQ_OR_CLEANUP(rtc_init(&rtc, 0U, 0U, rtc_global_cb,
					   &rtc_global_log), 0, "INIT");
	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_irq_enable(rtc, CAPI_RTC_EVENT_ALARM),
				  0, "IRQ_ENABLE_ALARM");
	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_alarm_set_callback(rtc,
				  RTC_PERIODIC_ALARM_ID, rtc_alarm_cb,
				  &rtc_alarm_log), 0, "SET_ALARM_CALLBACK");
	RTC_ALARM_SET(alarm, extra, RTC_PERIODIC_ALARM_VALUE);
	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_alarm_set(rtc, RTC_PERIODIC_ALARM_ID,
				  &alarm), 0, "ALARM_SET");
	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_alarm_get(rtc, RTC_PERIODIC_ALARM_ID,
				  &get_alarm), 0, "ALARM_GET");
	TEST_ASSERT_EQ_OR_CLEANUP(RTC_ALARM_VALUE(get_alarm, get_extra),
				  RTC_PERIODIC_ALARM_VALUE, "ALARM_GET_VALUE");
	TEST_ASSERT_OR_CLEANUP(RTC_ALARM_ENABLED(get_alarm, get_extra),
			       "ALARM_GET_ENABLED");

	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_start(rtc), 0, "START");
	capi_wait_ms(1000U);
	snapshot = rtc_alarm_log.alarm_count;

	TEST_VALUE("PERIODIC_ALARMS_IN_1S", snapshot);
	TEST_ASSERT_RANGE_OR_CLEANUP(snapshot, nominal / 3U, nominal * 3U,
				     "PERIODIC");
	TEST_ASSERT_EQ_OR_CLEANUP(rtc_alarm_log.last_event, CAPI_RTC_EVENT_ALARM,
				  "CALLBACK_EVENT");
#if RTC_ALARM_EVENT_EXTRA_IS_ID
	TEST_ASSERT_EQ_OR_CLEANUP(rtc_alarm_log.last_extra,
				  RTC_PERIODIC_ALARM_ID, "CALLBACK_EXTRA_ID");
#endif
	TEST_ASSERT_OR_CLEANUP(rtc_alarm_log.last_arg == &rtc_alarm_log,
			       "CALLBACK_ARG");
	TEST_ASSERT_EQ_OR_CLEANUP(rtc_global_log.alarm_count, 0U,
				  "PER_ALARM_PRECEDENCE");

	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_alarm_disable(rtc,
				  RTC_PERIODIC_ALARM_ID), 0, "ALARM_DISABLE");
	snapshot = rtc_alarm_log.alarm_count;
	capi_wait_ms(3U * RTC_PERIODIC_ALARM_PERIOD_MS);
	TEST_ASSERT_EQ_OR_CLEANUP(rtc_alarm_log.alarm_count, snapshot,
				  "STOPPED_AFTER_DISABLE");

	/* Drop the per-alarm callback; the global callback takes over. */
	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_alarm_set_callback(rtc,
				  RTC_PERIODIC_ALARM_ID, NULL, NULL), 0,
				  "CLEAR_ALARM_CALLBACK");
	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_alarm_set(rtc, RTC_PERIODIC_ALARM_ID,
				  &alarm), 0, "ALARM_SET_2");
	TEST_WAIT_UNTIL(rtc_global_log.alarm_count != 0U, RTC_ALARM_TIMEOUT_US,
			RTC_POLL_STEP_US);
	TEST_ASSERT_GT_OR_CLEANUP(rtc_global_log.alarm_count, 0U,
				  "GLOBAL_FALLBACK");
#if RTC_ALARM_EVENT_EXTRA_IS_ID
	TEST_ASSERT_EQ_OR_CLEANUP(rtc_global_log.last_extra,
				  RTC_PERIODIC_ALARM_ID, "GLOBAL_EXTRA_ID");
#endif

	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_alarm_disable(rtc,
				  RTC_PERIODIC_ALARM_ID), 0, "ALARM_DISABLE_2");
	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_irq_disable(rtc, CAPI_RTC_EVENT_ALARM),
				  0, "IRQ_DISABLE_ALARM");

	CLEANUP;

	return 0;
}
#else
#define rtc_alarm_periodic_cb	NULL
#endif /* RTC_HAS_PERIODIC_ALARM */

#if RTC_HAS_PLATFORM_EVENT
/**
 * @brief Platform-specific event through the global callback (registered
 *	  after init, to exercise register_callback).
 */
static int rtc_platform_event(void)
{
	struct capi_rtc_handle *rtc = NULL;
	uint32_t snapshot;

	TEST_SECTION("PLATFORM_EVENT");

	rtc_reset_logs();

	TEST_ASSERT_EQ_OR_CLEANUP(rtc_init(&rtc, 0U, 0U, NULL, NULL), 0, "INIT");
	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_register_callback(rtc, rtc_global_cb,
				  &rtc_global_log), 0, "REGISTER_CALLBACK");
	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_start(rtc), 0, "START");
	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_irq_enable(rtc, RTC_PLATFORM_EVENT),
				  0, "IRQ_ENABLE_EVENT");

	TEST_WAIT_UNTIL(rtc_global_log.platform_count != 0U, 500000U, 1000U);

	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_irq_disable(rtc, RTC_PLATFORM_EVENT),
				  0, "IRQ_DISABLE_EVENT");
	TEST_VALUE("PLATFORM_EVENTS", rtc_global_log.platform_count);
	TEST_ASSERT_GT_OR_CLEANUP(rtc_global_log.platform_count, 0U,
				  "EVENT_FIRED");
	TEST_ASSERT_OR_CLEANUP(rtc_global_log.last_arg == &rtc_global_log,
			       "CALLBACK_ARG");

	snapshot = rtc_global_log.platform_count;
	capi_wait_ms(50U);
	TEST_ASSERT_EQ_OR_CLEANUP(rtc_global_log.platform_count, snapshot,
				  "EVENT_STOPPED");
	TEST_ASSERT_EQ_OR_CLEANUP(rtc_global_log.other_count, 0U,
				  "NO_UNEXPECTED_EVENTS");

	CLEANUP;

	return 0;
}
#else
#define rtc_platform_event	NULL
#endif /* RTC_HAS_PLATFORM_EVENT */

/**
 * @brief Wakeup API smoke test.
 */
static int rtc_misc_api(void)
{
	struct capi_rtc_handle *rtc = NULL;

	TEST_SECTION("MISC_API");

	TEST_ASSERT_EQ_OR_CLEANUP(rtc_init(&rtc, 0U, 0U, NULL, NULL), 0, "INIT");

	TEST_ASSERT_OK_OR_NOTSUP_OR_CLEANUP(capi_rtc_wakeup_enable(rtc),
					    "WAKEUP_ENABLE");
	TEST_ASSERT_OK_OR_NOTSUP_OR_CLEANUP(capi_rtc_wakeup_disable(rtc),
					    "WAKEUP_DISABLE");

	CLEANUP;

	return 0;
}

#if RTC_HAS_OUTPUT
/**
 * @brief Clock output selected at init: driven only while the counter runs.
 */
static int rtc_output(void)
{
	struct capi_rtc_handle *rtc = NULL;
	RTC_OUTPUT_EXTRA_TYPE bad_extra = RTC_OUTPUT_EXTRA_BAD_INIT;
	RTC_OUTPUT_EXTRA_TYPE extra = RTC_OUTPUT_EXTRA_INIT;
	struct capi_rtc_config config = {
		.ops = RTC_OPS,
		.identifier = RTC_IDENTIFIER,
		.extra = &bad_extra,
	};

	TEST_SECTION("OUTPUT");

	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_init(&rtc, &config), -EINVAL,
				  "INIT_BAD_EXTRA");
	TEST_ASSERT_OR_CLEANUP(rtc == NULL, "HANDLE_UNTOUCHED");

	/* Deferred to start: init must neither drive it nor start the counter. */
	config.extra = &extra;
	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_init(&rtc, &config), 0, "INIT");
	TEST_ASSERT_OR_CLEANUP(!RTC_OUTPUT_IS_ON(), "OFF_AFTER_INIT");
	TEST_ASSERT_OR_CLEANUP(!(rtc_status(rtc) & CAPI_RTC_STATUS_RUNNING),
			       "STOPPED_AFTER_INIT");

	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_start(rtc), 0, "START");
	TEST_ASSERT_OR_CLEANUP(RTC_OUTPUT_IS_ON(), "ON_AFTER_START");
	TEST_ASSERT_OR_CLEANUP(rtc_status(rtc) & CAPI_RTC_STATUS_RUNNING,
			       "RUNNING_AFTER_START");

	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_stop(rtc), 0, "STOP");
	TEST_ASSERT_OR_CLEANUP(!RTC_OUTPUT_IS_ON(), "OFF_AFTER_STOP");

	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_start(rtc), 0, "RESTART");
	TEST_ASSERT_OR_CLEANUP(RTC_OUTPUT_IS_ON(), "ON_AFTER_RESTART");

	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_deinit(rtc), 0, "DEINIT");
	rtc = NULL;
	TEST_ASSERT_OR_CLEANUP(!RTC_OUTPUT_IS_ON(), "OFF_AFTER_DEINIT");

	/*
	 * Counter left running outside the driver (e.g. by a bootloader):
	 * a preserving init drives the output right away.
	 */
	TEST_ASSERT_EQ_OR_CLEANUP(RTC_RAW_START(), 0, "RAW_START");
	config.flags = CAPI_RTC_INIT_FLAG_PRESERVE_COUNT;
	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_init(&rtc, &config), 0,
				  "INIT_PRESERVE_RUNNING");
	TEST_ASSERT_OR_CLEANUP(RTC_OUTPUT_IS_ON(), "ON_AFTER_PRESERVE");
	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_deinit(rtc), 0, "DEINIT_2");
	rtc = NULL;

	/* No extra: default is off, also once started. */
	config.flags = 0U;
	config.extra = NULL;
	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_init(&rtc, &config), 0,
				  "INIT_DEFAULT");
	TEST_ASSERT_EQ_OR_CLEANUP(capi_rtc_start(rtc), 0, "START_DEFAULT");
	TEST_ASSERT_OR_CLEANUP(!RTC_OUTPUT_IS_ON(), "OFF_BY_DEFAULT");

	CLEANUP;

	return 0;
}
#else
#define rtc_output	NULL
#endif /* RTC_HAS_OUTPUT */

#undef CLEANUP

/* Capability-gated entries are reported as skipped when their flag is 0. */
static const struct test_case rtc_subtests[] = {
	{ "PARAMS", rtc_param_checks, false },
	{ "INIT_DEINIT", rtc_init_deinit, false },
	{ "COUNT_RUNS", rtc_count_runs, false },
	{ "SET_COUNT", rtc_set_count, false },
	{ "PRESERVE_COUNT", rtc_preserve_count, false },
	{ "CALIBRATION", rtc_calibration, !RTC_HAS_CALIBRATION },
	{ "ALARM_ARGS", rtc_alarm_args, false },
	{ "ALARM_POLL", rtc_alarm_poll, false },
	{ "ALARM_GLOBAL_CB", rtc_alarm_global_cb, false },
	{ "ALARM_PERIODIC_CB", rtc_alarm_periodic_cb, !RTC_HAS_PERIODIC_ALARM },
	{ "PLATFORM_EVENT", rtc_platform_event, !RTC_HAS_PLATFORM_EVENT },
	{ "MISC_API", rtc_misc_api, false },
	{ "OUTPUT", rtc_output, !RTC_HAS_OUTPUT },
};

/**
 * @brief Run all RTC test cases.
 * @return 0 if all executed cases passed, first non-zero test error otherwise.
 */
int test_rtc(void)
{
	RTC_PLATFORM_INIT();

	return test_framework_run_cases(RTC_MODULE, rtc_subtests,
					sizeof(rtc_subtests) /
					sizeof(rtc_subtests[0]));
}

#endif /* RTC_OPS */
