/*
 * Zephyr port of timing module (originally HAL/SysTick based).
 * - time unit: milliseconds
 * - timingAdd/Remove not called from ISR (assumed by design)
 * - callback must not sleep and must not add/remove timers (assumed by design)
 */

#include "timing.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

static void timing_work_handler(struct k_work *work);
static void timing_timer_expiry(struct k_timer *timer);

static void timing_init_if_needed(timingTimer *t)
{
    if (t == NULL) {
        return;
    }

    /* Thread-safe enough for your constraints (no ISR use, typical init once). */
    if (atomic_cas(&t->z_initialized, 0, 1)) {
        atomic_set(&t->z_expired, 1); /* default: not active => "expired" */
        k_work_init(&t->z_work, timing_work_handler);
        k_timer_init(&t->z_timer, timing_timer_expiry, NULL);
        k_timer_user_data_set(&t->z_timer, t);
    }
}

void timingRemoveTimer(const timingTimer *pTimer)
{
    if (pTimer == NULL) {
        return;
    }

    timingTimer *t = (timingTimer *)pTimer;
    timing_init_if_needed(t);

    k_timer_stop(&t->z_timer);

    /* Keep original semantics: "not set or expired" => true */
    atomic_set(&t->z_expired, 1);

    /* Optionally clear callback to avoid accidental use */
    t->callback = NULL;
    t->timeout  = 0U;
    t->counter  = 0U;
    t->type     = TIMING_TIMER_ONE_SHOT;
}

void timingAddTimer(timingTimer *pTimer, uint8_t type, uint32_t timeout, timingCallback callback)
{
    if (pTimer == NULL) {
        return;
    }

    timingTimer *t = pTimer;
    timing_init_if_needed(t);

    k_timer_stop(&t->z_timer);

    t->timeout  = timeout;
    t->counter  = timeout;   /* keep legacy field coherent */
    t->callback = callback;
    t->type     = type;

    atomic_set(&t->z_expired, 0);

    if (timeout == 0U) {
        atomic_set(&t->z_expired, 1);
        (void)k_work_submit(&t->z_work);
        return;
    }

    if (type == TIMING_TIMER_CYCLIC) {
        k_timer_start(&t->z_timer, K_MSEC(timeout), K_MSEC(timeout));
    } else {
        /* delay / one-shot treated as oneshot */
        k_timer_start(&t->z_timer, K_MSEC(timeout), K_NO_WAIT);
    }
}

bool timingCheckTimeout(const timingTimer *pTimer)
{
    if (pTimer == NULL) {
        return true;
    }

    timingTimer *t = (timingTimer *)pTimer;
    timing_init_if_needed(t);

    return atomic_get(&t->z_expired) != 0;
}

void timingDelay_ms(uint32_t delay_ms)
{
    if (delay_ms == 0U) {
        return;
    }
    k_msleep(delay_ms);
}

/* Compatibility no-ops */
void timingExecute(void)
{
}

void timingTick(void)
{
}

void timingInit(void)
{
    /* Optional: keep for API compatibility */
}

/* --- internal handlers --- */

static void timing_timer_expiry(struct k_timer *timer)
{
    timingTimer *t = (timingTimer *)k_timer_user_data_get(timer);
    if (t == NULL) {
        return;
    }

    /* For oneshot, mark expired. For cyclic, keep expired as 0. */
    if (t->type != TIMING_TIMER_CYCLIC) {
        atomic_set(&t->z_expired, 1);
    }

    (void)k_work_submit(&t->z_work);
}

static void timing_work_handler(struct k_work *work)
{
    timingTimer *t = CONTAINER_OF(work, timingTimer, z_work);
    if (t->callback != NULL) {
        t->callback();
    }
}