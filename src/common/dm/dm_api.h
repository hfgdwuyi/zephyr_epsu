/*
 * dm_api.h
 *
 * DataModel Manager (DMM) - public API.
 *
 * The DataModel Manager provides the central internal data interface for the PDM
 * software. It stores and manages:
 *  - volatile runtime parameters
 *  - persistent configuration parameters
 *  - activity requests (commands)
 *  - diagnostic status/event information
 *  - update state/progress information
 *
 * All PDM modules shall access shared system information through the DataModel
 * Manager instead of directly accessing each other.
 *
 * Thread-safety:
 *  - Snapshot getters are thread-safe and return a consistent copy.
 *  - Requests are transferred via internal message queues (split by domain to
 *    avoid multi-consumer competition).
 */

#pragma once

#include <zephyr/kernel.h>
#include "dm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------
 * Initialization / snapshots
 * ----------------------------- */

/* Init must be called once at boot from main(). */
int dm_init(void);

/* Snapshot getters (thread-safe). */
void dm_get_snapshot(dm_snapshot_t *out);

/* Convenience getters */
int64_t dm_get_uptime_ms(void);

/* Runtime updates (called by periodic tasks / producers). */
void dm_runtime_set_uptime_ms(int64_t uptime_ms);

/* -----------------------------
 * Requests (domain queues)
 * ----------------------------- */

/*
 * Control requests:
 * - submitted by interface modules (HTTP/CLI/etc.)
 * - consumed by control task thread(s)
 */
int dm_ctrl_req_submit(const dm_request_t *req);
int dm_ctrl_req_receive(dm_request_t *out, k_timeout_t timeout);

/*
 * Update requests:
 * - submitted by interface modules (HTTP/CLI/etc.)
 * - consumed by update task thread(s)
 */
int dm_update_req_submit(const dm_request_t *req);
int dm_update_req_receive(dm_request_t *out, k_timeout_t timeout);

/*
 * Diagnostics requests:
 * - submitted by interface modules (HTTP/CLI/etc.)
 * - consumed by diagnostics task thread(s)
 */
int dm_diag_req_submit(const dm_request_t *req);
int dm_diag_req_receive(dm_request_t *out, k_timeout_t timeout);

/* -----------------------------
 * Diagnostics API
 * ----------------------------- */

/* Set/clear fault bits. If 'latched' is true, sets/clears latched_faults too. */
void dm_diag_set_fault_bits(uint32_t mask, bool active, bool latched);

/* Clear faults; if mask == 0, clear all. */
void dm_diag_clear(uint32_t mask, bool clear_latched);

/* Add a diagnostic event entry into the ring buffer. */
void dm_diag_add_event(uint32_t code, dm_diag_severity_t sev, uint32_t aux);

/* -----------------------------
 * Update API
 * ----------------------------- */

void dm_update_set_state(dm_update_state_t state, int32_t last_error, uint8_t progress);
void dm_update_set_uri(const char *uri);

#ifdef __cplusplus
}
#endif