/*
 * dm_ctrl_worker.c
 *
 * Control domain worker.
 *
 * This file consumes control requests from the DataModel queue and performs
 * side effects on BSP drivers. It is intentionally separated from dm_core so
 * that the DataModel layer remains focused on state ownership and request
 * transport.
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

#include <stdbool.h>
#include <stdint.h>

#include "dm_api.h"
#include "dm_types.h"
#include "bsp_led.h"

LOG_MODULE_REGISTER(dm_ctrl_worker, LOG_LEVEL_INF);

#define DM_CTRL_WORKER_STACK_SZ      2048
#define DM_CTRL_WORKER_PRIO_PREEMPT  2

static void dm_ctrl_worker(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    printk("dm_core: ctrl_worker started\r\n");

    for (;;) {
        dm_request_t req;
        int rc = dm_ctrl_req_receive(&req, K_FOREVER);

        if (rc != 0) {
            LOG_ERR("ctrl_worker: receive rc=%d", rc);
            continue;
        }

        LOG_DBG("ctrl_worker: dequeued id=%d action=%d idx=%u on=%u val=%d",
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

K_THREAD_STACK_DEFINE(g_dm_ctrl_worker_stack, DM_CTRL_WORKER_STACK_SZ);
static struct k_thread g_dm_ctrl_worker_thread;

static int dm_ctrl_worker_start(void)
{
    ledInit();

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