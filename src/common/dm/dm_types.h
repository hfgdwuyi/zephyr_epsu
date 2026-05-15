/*
 * dm_types.h
 *
 * DataModel Manager (DMM) - public data types.
 *
 * This header defines the shared data structures owned by the DataModel Manager.
 * Modules must not share state by directly accessing each other. Instead, they
 * exchange information through the DataModel Manager using these types and the
 * APIs provided by dm_api.h.
 *
 * Data categories covered:
 *  - Volatile runtime parameters (reset on reboot)
 *  - Persistent configuration parameters (stored in non-volatile memory)
 *  - Activity requests (commands submitted by interface modules)
 *  - Diagnostic status/event information (faults, event log)
 *  - Update state/progress information
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------
 * Data domains
 * ----------------------------- */

typedef struct {
    int64_t uptime_ms;
} dm_runtime_t;

typedef struct {
    /* Reserved for persistent settings (flash/NVS/settings). */
    uint32_t schema_version;
} dm_config_t;

/* ---- Diagnostics ---- */

typedef enum {
    DM_DIAG_SEV_DEBUG = 0,
    DM_DIAG_SEV_INFO  = 1,
    DM_DIAG_SEV_WARN  = 2,
    DM_DIAG_SEV_ERROR = 3,
} dm_diag_severity_t;

typedef struct {
    uint32_t code;
    dm_diag_severity_t sev;
    int64_t ts_ms;
    uint32_t aux; /* optional: additional numeric data */
} dm_diag_event_t;

#define DM_DIAG_EVENT_LOG_CAPACITY 32

typedef struct {
    /* Fault status (bitfields or numeric codes) */
    uint32_t active_faults;
    uint32_t latched_faults;

    /* Event log (ring buffer) */
    dm_diag_event_t events[DM_DIAG_EVENT_LOG_CAPACITY];
    uint16_t event_head;  /* next write index */
    uint16_t event_count; /* number of valid events, <= CAPACITY */
    uint32_t dropped_events;
} dm_diag_t;

/* ---- Update ---- */

typedef enum {
    DM_UPDATE_STATE_IDLE = 0,
    DM_UPDATE_STATE_REQUESTED,
    DM_UPDATE_STATE_DOWNLOADING,
    DM_UPDATE_STATE_VERIFYING,
    DM_UPDATE_STATE_APPLYING,
    DM_UPDATE_STATE_REBOOT_PENDING,
    DM_UPDATE_STATE_DONE,
    DM_UPDATE_STATE_FAILED,
} dm_update_state_t;

typedef struct {
    dm_update_state_t state;
    int32_t last_error;       /* negative errno-style or app-defined */
    uint8_t progress_percent; /* 0..100 */
    char package_uri[160];    /* e.g., http(s)://... */
    int64_t last_change_ts_ms;
} dm_update_t;

/* ---- Communication/info ---- */

typedef struct {
    char device_id[16];
    char state[16];
    char last_reset_reason[32];
} dm_comm_t;

typedef struct {
    dm_runtime_t runtime;
    dm_config_t  config;
    dm_diag_t    diag;
    dm_update_t  update;
    dm_comm_t    comm;
} dm_snapshot_t;

/* -----------------------------
 * Requests (commands)
 * ----------------------------- */

typedef enum {
    DM_REQ_NONE = 0,

    /* Control (generic) */
    DM_REQ_CTRL_ACTION = 2,

    /* Diagnostics */
    DM_REQ_DIAG_CLEAR = 10,

    /* Update */
    DM_REQ_UPDATE_START = 20,
} dm_req_id_t;

/* Generic control actions */
typedef enum {
    DM_CTRL_ACT_NONE = 0,
    DM_CTRL_ACT_LED_SET = 1,
    DM_CTRL_ACT_RELAY_SET = 2,     /* reserved */
    DM_CTRL_ACT_PSU_CH_SET = 3,    /* reserved */
    DM_CTRL_ACT_BUZZER_SET = 4,    /* reserved */
} dm_ctrl_action_t;

typedef struct {
    dm_ctrl_action_t action;

    /* Common addressing: "which peripheral" */
    uint8_t index;

    /* Common payload (keep minimal; extend later) */
    bool on;

    /* Optional numeric value (PWM/level/etc.). 0 if unused */
    int32_t value;
} dm_req_ctrl_action_t;

typedef struct {
    /* If mask == 0 => clear all */
    uint32_t mask;
    bool clear_latched;
} dm_req_diag_clear_t;

typedef struct {
    char uri[160];
} dm_req_update_start_t;

typedef struct {
    dm_req_id_t id;
    int64_t ts_ms;
    union {
        dm_req_ctrl_action_t   ctrl_action;
        dm_req_diag_clear_t    diag_clear;
        dm_req_update_start_t  update_start;
    } p;
} dm_request_t;

#ifdef __cplusplus
}
#endif