#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <zephyr/sys/atomic.h>

#include "timing.h"

static atomic_t callback_count;
static struct k_sem callback_sem;

static void test_timer_callback(void)
{
    atomic_inc(&callback_count);
    k_sem_give(&callback_sem);
}

static void reset_callback_state(void)
{
    atomic_set(&callback_count, 0);
    k_sem_reset(&callback_sem);
}

static void timing_test_before(void *fixture)
{
    ARG_UNUSED(fixture);
    k_sem_init(&callback_sem, 0, 16);
    reset_callback_state();
}

ZTEST(timing_suite, test_timing_null_and_delay_semantics)
{
    zassert_true(timingCheckTimeout(NULL), "NULL timer should be treated as expired");

    int64_t start = k_uptime_get();
    timingDelay_ms(5U);
    zassert_true((k_uptime_get() - start) >= 5, "timingDelay_ms should wait at least requested time");
}

ZTEST(timing_suite, test_timing_one_shot_timer_expires)
{
    timingTimer timer = {0};

    zassert_true(timingCheckTimeout(&timer), "new timer should start expired/not active");

    timingAddTimer(&timer, TIMING_TIMER_ONE_SHOT, 20U, test_timer_callback);
    zassert_false(timingCheckTimeout(&timer), "one-shot timer should be active before expiry");

    zassert_equal(k_sem_take(&callback_sem, K_MSEC(200)), 0, "one-shot callback not observed");
    zassert_true(timingCheckTimeout(&timer), "one-shot timer should be expired after callback");
    zassert_equal(atomic_get(&callback_count), 1, "one-shot callback should run once");
}

ZTEST(timing_suite, test_timing_remove_timer_stops_callbacks)
{
    timingTimer timer = {0};

    timingAddTimer(&timer, TIMING_TIMER_ONE_SHOT, 50U, test_timer_callback);
    timingRemoveTimer(&timer);

    zassert_true(timingCheckTimeout(&timer), "removed timer should be marked expired");
    zassert_not_equal(k_sem_take(&callback_sem, K_MSEC(100)), 0, "removed timer should not fire callback");
    zassert_equal(atomic_get(&callback_count), 0, "removed timer callback count mismatch");
}

ZTEST(timing_suite, test_timing_cyclic_timer_repeats_until_removed)
{
    timingTimer timer = {0};

    timingAddTimer(&timer, TIMING_TIMER_CYCLIC, 20U, test_timer_callback);

    zassert_equal(k_sem_take(&callback_sem, K_MSEC(200)), 0, "first cyclic callback missing");
    zassert_equal(k_sem_take(&callback_sem, K_MSEC(200)), 0, "second cyclic callback missing");
    zassert_false(timingCheckTimeout(&timer), "cyclic timer should remain active before removal");

    timingRemoveTimer(&timer);
    zassert_true(timingCheckTimeout(&timer), "cyclic timer should be expired after removal");
    zassert_true(atomic_get(&callback_count) >= 2, "cyclic timer should run multiple callbacks");
}

ZTEST_SUITE(timing_suite, NULL, NULL, timing_test_before, NULL, NULL);