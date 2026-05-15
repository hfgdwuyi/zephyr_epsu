/*
 * dm_core.c
 *
 * DataModel Manager (DMM) - core implementation.
 *
 * This module owns the global shared data model instance and provides:
 *  - thread-safe snapshot access to shared data
 *  - request queues for activity/control/diagnostics/update requests
 *  - setters for volatile runtime values (updated by periodic tasks)
 *  - diagnostics fault/event management
 *  - update state/progress management
 *
 * Design intent:
 *  - Interface modules (e.g., HTTP) submit requests via dm_*_req_submit().
 *  - Domain task modules consume requests via dm_*_req_receive() and perform actions.
 *  - Shared runtime/status values are read via dm_get_snapshot() or convenience getters.
 *
 * NOTE (important for this project):
 *  - If no domain task consumes dm_ctrl_req_receive(), control actions will never be executed.
 *  - This file includes an internal control worker thread that consumes g_dm_ctrl_req_q
 *    and performs minimal actions (currently LED set) so that HTTP control works end-to-end.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

#include <errno.h>
#include <string.h>
#include <stdint.h>

#include "dm_api.h"
#include "dm_types.h"
#include "bsp_led.h"

LOG_MODULE_REGISTER(dm_core, LOG_LEVEL_INF);

static struct k_mutex g_dm_lock;
static dm_snapshot_t g_dm;

/* Domain request queues (avoid multi-consumer competition on a single queue) */
K_MSGQ_DEFINE(g_dm_ctrl_req_q,   sizeof(dm_request_t), 16, 4);
K_MSGQ_DEFINE(g_dm_diag_req_q,   sizeof(dm_request_t),  8, 4);
K_MSGQ_DEFINE(g_dm_update_req_q, sizeof(dm_request_t),  4, 4);

/* -----------------------------
 * Internal control worker
 * ----------------------------- */

static void dm_ctrl_worker(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    printk("dm_core: ctrl_worker started\r\n");

    dm_request_t req;

    for (;;) {
        int rc = k_msgq_get(&g_dm_ctrl_req_q, &req, K_FOREVER);
        if (rc != 0) {
            LOG_ERR("ctrl_worker: k_msgq_get rc=%d", rc);
            continue;
        }

        LOG_INF("ctrl_worker: dequeued id=%d action=%d idx=%u on=%u val=%d",
            (int)req.id,
            (int)req.p.ctrl_action.action,
            (unsigned int)req.p.ctrl_action.index,
            (unsigned int)req.p.ctrl_action.on,
            (int)req.p.ctrl_action.value);

        if (req.id != DM_REQ_CTRL_ACTION) {
            LOG_WRN("ctrl_worker: unexpected req.id=%d", (int)req.id);
            continue;
        }

        switch (req.p.ctrl_action.action) {
        case DM_CTRL_ACT_LED_SET: {
            uint8_t idx = req.p.ctrl_action.index;
            bool on = req.p.ctrl_action.on;

            if (on) {
                ledSwitchOn(idx);
            } else {
                ledSwitchOff(idx);
            }

            break;
        }

        case DM_CTRL_ACT_RELAY_SET:
            LOG_WRN("ctrl_worker: RELAY_SET not implemented idx=%u on=%u",
                (unsigned int)req.p.ctrl_action.index,
                (unsigned int)req.p.ctrl_action.on);
            break;

        case DM_CTRL_ACT_PSU_CH_SET:
            LOG_WRN("ctrl_worker: PSU_CH_SET not implemented idx=%u on=%u value=%d",
                (unsigned int)req.p.ctrl_action.index,
                (unsigned int)req.p.ctrl_action.on,
                (int)req.p.ctrl_action.value);
            break;

        case DM_CTRL_ACT_BUZZER_SET:
            LOG_WRN("ctrl_worker: BUZZER_SET not implemented idx=%u on=%u value=%d",
                (unsigned int)req.p.ctrl_action.index,
                (unsigned int)req.p.ctrl_action.on,
                (int)req.p.ctrl_action.value);
            break;

        default:
            LOG_WRN("ctrl_worker: unsupported action=%d", (int)req.p.ctrl_action.action);
            break;
        }
    }
}

/*
 * IMPORTANT CHANGE:
 * Replace K_THREAD_DEFINE() with an explicit k_thread_create() started via SYS_INIT.
 * This avoids priority/cooperative starvation and makes startup deterministic.
 */

#define DM_CTRL_WORKER_STACK_SZ      2048
#define DM_CTRL_WORKER_PRIO_PREEMPT  2  /* smaller number = higher prio (preemptive) */

K_THREAD_STACK_DEFINE(g_dm_ctrl_worker_stack, DM_CTRL_WORKER_STACK_SZ);
static struct k_thread g_dm_ctrl_worker_thread;

static int dm_ctrl_worker_start(void)
{
    k_tid_t tid = k_thread_create(&g_dm_ctrl_worker_thread,
                      g_dm_ctrl_worker_stack,
                      K_THREAD_STACK_SIZEOF(g_dm_ctrl_worker_stack),
                      dm_ctrl_worker,
                      NULL, NULL, NULL,
                      DM_CTRL_WORKER_PRIO_PREEMPT,
                      0,
                      K_NO_WAIT);

    k_thread_name_set(tid, "dm_ctrl_worker");
    return 0;
}

SYS_INIT(dm_ctrl_worker_start, APPLICATION, 50);

/* -----------------------------
 * Diagnostics internals
 * ----------------------------- */

static void diag_ring_push_unlocked(uint32_t code, dm_diag_severity_t sev, uint32_t aux)
{
    dm_diag_t *d = &g_dm.diag;

    dm_diag_event_t *e = &d->events[d->event_head];
    e->code = code;
    e->sev = sev;
    e->aux = aux;
    e->ts_ms = k_uptime_get();

    d->event_head = (uint16_t)((d->event_head + 1U) % DM_DIAG_EVENT_LOG_CAPACITY);

    if (d->event_count < DM_DIAG_EVENT_LOG_CAPACITY) {
        d->event_count++;
    } else {
        d->dropped_events++;
    }
}

int dm_init(void)
{
    k_mutex_init(&g_dm_lock);

    /* Ensure LED driver is initialized so control actions can take effect */
    ledInit();

    k_mutex_lock(&g_dm_lock, K_FOREVER);
    memset(&g_dm, 0, sizeof(g_dm));

    g_dm.config.schema_version = 1;

    /* Defaults for comm/info */
    strncpy(g_dm.comm.device_id, "pdm01", sizeof(g_dm.comm.device_id) - 1);
    strncpy(g_dm.comm.state, "operational", sizeof(g_dm.comm.state) - 1);
    strncpy(g_dm.comm.last_reset_reason, "power_on", sizeof(g_dm.comm.last_reset_reason) - 1);

    /* Update defaults */
    g_dm.update.state = DM_UPDATE_STATE_IDLE;
    g_dm.update.last_error = 0;
    g_dm.update.progress_percent = 0;
    g_dm.update.last_change_ts_ms = k_uptime_get();

    diag_ring_push_unlocked(0x1000, DM_DIAG_SEV_INFO, 0); /* "boot" marker */

    k_mutex_unlock(&g_dm_lock);

    return 0;
}

/* -----------------------------
 * Snapshots / runtime
 * ----------------------------- */

void dm_get_snapshot(dm_snapshot_t *out)
{
    if (out == NULL) {
        return;
    }

    k_mutex_lock(&g_dm_lock, K_FOREVER);
    *out = g_dm;
    k_mutex_unlock(&g_dm_lock);
}

int64_t dm_get_uptime_ms(void)
{
    int64_t v;

    k_mutex_lock(&g_dm_lock, K_FOREVER);
    v = g_dm.runtime.uptime_ms;
    k_mutex_unlock(&g_dm_lock);

    return v;
}

void dm_runtime_set_uptime_ms(int64_t uptime_ms)
{
    k_mutex_lock(&g_dm_lock, K_FOREVER);
    g_dm.runtime.uptime_ms = uptime_ms;
    k_mutex_unlock(&g_dm_lock);
}

/* -----------------------------
 * Requests (domain queues)
 * ----------------------------- */

int dm_ctrl_req_submit(const dm_request_t *req)
{
    if (req == NULL) {
        return -EINVAL;
    }

    LOG_INF("dm_ctrl_req_submit: put id=%d action=%d idx=%u on=%u val=%d",
        (int)req->id,
        (int)req->p.ctrl_action.action,
        (unsigned int)req->p.ctrl_action.index,
        (unsigned int)req->p.ctrl_action.on,
        (int)req->p.ctrl_action.value);

    return k_msgq_put(&g_dm_ctrl_req_q, req, K_NO_WAIT);
}

int dm_ctrl_req_receive(dm_request_t *out, k_timeout_t timeout)
{
    if (out == NULL) {
        return -EINVAL;
    }
    return k_msgq_get(&g_dm_ctrl_req_q, out, timeout);
}

int dm_diag_req_submit(const dm_request_t *req)
{
    if (req == NULL) {
        return -EINVAL;
    }
    return k_msgq_put(&g_dm_diag_req_q, req, K_NO_WAIT);
}

int dm_diag_req_receive(dm_request_t *out, k_timeout_t timeout)
{
    if (out == NULL) {
        return -EINVAL;
    }
    return k_msgq_get(&g_dm_diag_req_q, out, timeout);
}

int dm_update_req_submit(const dm_request_t *req)
{
    if (req == NULL) {
        return -EINVAL;
    }
    return k_msgq_put(&g_dm_update_req_q, req, K_NO_WAIT);
}

int dm_update_req_receive(dm_request_t *out, k_timeout_t timeout)
{
    if (out == NULL) {
        return -EINVAL;
    }
    return k_msgq_get(&g_dm_update_req_q, out, timeout);
}

/* -----------------------------
 * Diagnostics
 * ----------------------------- */

 // ...existing code...
void dm_diag_clear(uint32_t mask, bool clear_latched);
/* -----------------------------
 * Internal diagnostics worker
 * ----------------------------- */

#define DM_DIAG_WORKER_STACK_SZ      1536
#define DM_DIAG_WORKER_PRIO_PREEMPT  3

static void dm_diag_worker(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    LOG_INF("diag_worker: started (tid=%p)", k_current_get());

    dm_request_t req;

    for (;;) {
        int rc = k_msgq_get(&g_dm_diag_req_q, &req, K_FOREVER);
        if (rc != 0) {
            LOG_ERR("diag_worker: k_msgq_get rc=%d", rc);
            continue;
        }

        LOG_INF("diag_worker: dequeued id=%d", (int)req.id);

        if (req.id != DM_REQ_DIAG_CLEAR) {
            LOG_WRN("diag_worker: unexpected req.id=%d", (int)req.id);
            continue;
        }

        /* TODO: adjust field names to match your dm_request_t definition */
        uint32_t mask = (uint32_t)req.p.diag_clear.mask;
        bool clear_latched = req.p.diag_clear.clear_latched ? true : false;

        LOG_INF("diag_worker: executing DIAG_CLEAR mask=0x%08x clear_latched=%u",
            (unsigned int)mask, (unsigned int)clear_latched);

        dm_diag_clear(mask, clear_latched);
    }
}

K_THREAD_STACK_DEFINE(g_dm_diag_worker_stack, DM_DIAG_WORKER_STACK_SZ);
static struct k_thread g_dm_diag_worker_thread;

static int dm_diag_worker_start(void)
{
    k_tid_t tid = k_thread_create(&g_dm_diag_worker_thread,
                      g_dm_diag_worker_stack,
                      K_THREAD_STACK_SIZEOF(g_dm_diag_worker_stack),
                      dm_diag_worker,
                      NULL, NULL, NULL,
                      DM_DIAG_WORKER_PRIO_PREEMPT, 0, K_NO_WAIT);
    k_thread_name_set(tid, "dm_diag_worker");
    return 0;
}

SYS_INIT(dm_diag_worker_start, APPLICATION, 55);

// ...existing code...


void dm_diag_set_fault_bits(uint32_t mask, bool active, bool latched)
{
    k_mutex_lock(&g_dm_lock, K_FOREVER);

    if (active) {
        g_dm.diag.active_faults |= mask;
        if (latched) {
            g_dm.diag.latched_faults |= mask;
        }
    } else {
        g_dm.diag.active_faults &= ~mask;
        if (latched) {
            g_dm.diag.latched_faults &= ~mask;
        }
    }

    k_mutex_unlock(&g_dm_lock);
}

void dm_diag_clear(uint32_t mask, bool clear_latched)
{
    k_mutex_lock(&g_dm_lock, K_FOREVER);

    if (mask == 0U) {
        g_dm.diag.active_faults = 0U;
        if (clear_latched) {
            g_dm.diag.latched_faults = 0U;
        }
    } else {
        g_dm.diag.active_faults &= ~mask;
        if (clear_latched) {
            g_dm.diag.latched_faults &= ~mask;
        }
    }

    k_mutex_unlock(&g_dm_lock);
}

void dm_diag_add_event(uint32_t code, dm_diag_severity_t sev, uint32_t aux)
{
    k_mutex_lock(&g_dm_lock, K_FOREVER);
    diag_ring_push_unlocked(code, sev, aux);
    k_mutex_unlock(&g_dm_lock);
}

/* -----------------------------
 * Update
 * ----------------------------- */

void dm_update_set_state(dm_update_state_t state, int32_t last_error, uint8_t progress)
{
    k_mutex_lock(&g_dm_lock, K_FOREVER);

    g_dm.update.state = state;
    g_dm.update.last_error = last_error;
    g_dm.update.progress_percent = progress;
    g_dm.update.last_change_ts_ms = k_uptime_get();

    k_mutex_unlock(&g_dm_lock);
}

void dm_update_set_uri(const char *uri)
{
    if (uri == NULL) {
        return;
    }

    k_mutex_lock(&g_dm_lock, K_FOREVER);
    strncpy(g_dm.update.package_uri, uri, sizeof(g_dm.update.package_uri) - 1);
    g_dm.update.package_uri[sizeof(g_dm.update.package_uri) - 1] = '\0';
    k_mutex_unlock(&g_dm_lock);
}