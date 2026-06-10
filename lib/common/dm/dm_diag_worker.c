/*
 * dm_diag_worker.c
 *
 * Diagnostics domain worker.
 *
 * This file consumes diagnostics requests from the DataModel queue and applies
 * changes through the public dm_* diagnostics API.
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>

#include <stdbool.h>
#include <stdint.h>

#include "dm_api.h"
#include "dm_types.h"

LOG_MODULE_REGISTER(dm_diag_worker, LOG_LEVEL_INF);

#define DM_DIAG_WORKER_STACK_SZ      1536
#define DM_DIAG_WORKER_PRIO_PREEMPT  3

static void dm_diag_worker(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    LOG_INF("diag_worker: started (tid=%p)", k_current_get());

    for (;;) {
        dm_request_t req;
        int rc = dm_diag_req_receive(&req, K_FOREVER);

        if (rc != 0) {
            LOG_ERR("diag_worker: receive rc=%d", rc);
            continue;
        }

        LOG_DBG("diag_worker: dequeued id=%d", (int)req.id);

        if (req.id != DM_REQ_DIAG_CLEAR) {
            LOG_WRN("diag_worker: unexpected req.id=%d", (int)req.id);
            continue;
        }

        {
            uint32_t mask = (uint32_t)req.p.diag_clear.mask;
            bool clear_latched = req.p.diag_clear.clear_latched ? true : false;

            LOG_DBG("diag_worker: executing DIAG_CLEAR mask=0x%08x clear_latched=%u",
                (unsigned int)mask, (unsigned int)clear_latched);

            dm_diag_clear(mask, clear_latched);
        }
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