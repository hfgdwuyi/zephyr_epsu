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
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <errno.h>
#include <string.h>
#include <stdint.h>

#include "dm_api.h"
#include "dm_types.h"

LOG_MODULE_REGISTER(dm_core, LOG_LEVEL_INF);

static struct k_mutex g_dm_lock;
static dm_snapshot_t g_dm;

/* Domain request queues (avoid multi-consumer competition on a single queue) */
K_MSGQ_DEFINE(g_dm_ctrl_req_q,   sizeof(dm_request_t), 16, 8);
K_MSGQ_DEFINE(g_dm_diag_req_q,   sizeof(dm_request_t),  8, 8);
K_MSGQ_DEFINE(g_dm_update_req_q, sizeof(dm_request_t),  4, 8);

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

    LOG_DBG("dm_ctrl_req_submit: put id=%d action=%d idx=%u on=%u val=%d",
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
    int rc = k_msgq_put(&g_dm_update_req_q, req, K_NO_WAIT);
    LOG_DBG("dm_update_req_submit: id=%d rc=%d", (int)req->id, rc);
    return rc;
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
