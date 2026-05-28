/*
 * http_routes.c
 *
 * HTTP module - service and REST resource registration.
 *
 * This file defines:
 *  - the HTTP service instance (port, max clients, backlog)
 *  - the URI -> handler resource mapping
 *
 * Handler implementations live in separate "task" files (http_task_*.c).
 */

#include <zephyr/net/http/service.h>
#include <zephyr/net/http/server.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(net_http_server_sample, LOG_LEVEL_DBG);

/* Resource details are owned by each task module */
extern struct http_resource_detail_dynamic uptime_resource_detail;
extern struct http_resource_detail_dynamic heartbeat_resource_detail;

extern struct http_resource_detail_dynamic control_resource_detail;

extern struct http_resource_detail_dynamic diag_resource_detail;
extern struct http_resource_detail_dynamic diag_clear_resource_detail;
#ifdef CONFIG_HTTP_DIAG_DEBUG
extern struct http_resource_detail_dynamic diag_inject_resource_detail;
#endif

extern struct http_resource_detail_dynamic update_start_resource_detail;
extern struct http_resource_detail_dynamic update_status_resource_detail;

/* Service must match the name used in sections-rom.ld */
static uint16_t test_http_service_port = 80;

HTTP_SERVICE_DEFINE(test_http_service, NULL, &test_http_service_port,
            CONFIG_HTTP_SERVER_MAX_CLIENTS, 10, NULL, NULL, NULL);

/* Status */
HTTP_RESOURCE_DEFINE(uptime_resource, test_http_service,
             "/api/v1/status/uptime", &uptime_resource_detail);
HTTP_RESOURCE_DEFINE(heartbeat_resource, test_http_service,
             "/api/v1/status/heartbeat", &heartbeat_resource_detail);

/* Unified control */
HTTP_RESOURCE_DEFINE(control_resource, test_http_service,
             "/api/v1/control", &control_resource_detail);

/* Diagnostics */
HTTP_RESOURCE_DEFINE(diag_resource, test_http_service, "/api/v1/diag/status", &diag_resource_detail);
HTTP_RESOURCE_DEFINE(diag_clear_resource, test_http_service, "/api/v1/diag/clear", &diag_clear_resource_detail);
#ifdef CONFIG_HTTP_DIAG_DEBUG
HTTP_RESOURCE_DEFINE(diag_inject_resource, test_http_service, "/api/v1/diag/inject",
             &diag_inject_resource_detail);
#endif

/* Update */
HTTP_RESOURCE_DEFINE(update_start_resource, test_http_service, "/update/start", &update_start_resource_detail);
HTTP_RESOURCE_DEFINE(update_status_resource, test_http_service, "/update/status", &update_status_resource_detail);