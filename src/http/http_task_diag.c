/*
 * http_task_diag.c
 *
 * REST endpoints:
 *  - GET  /diag        : read diagnostics snapshot (faults + recent events)
 *  - POST /diag/clear  : request clearing faults via DataModel diag request queue
 *
 * Design intent:
 *  - HTTP layer only reads from DataModel and submits requests.
 *  - Actual mutation (clear) is performed by the diagnostics task consuming the
 *    diagnostics request queue.
 *
 * Notes:
 *  - This implementation uses static buffers. If multiple HTTP clients are enabled
 *    concurrently, consider per-client storage or locking.
 */

#include <zephyr/kernel.h>
#include <zephyr/net/http/server.h>
#include <zephyr/data/json.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>

#include <errno.h>
#include <string.h>

#include "http_common.h"
#include "dm_api.h"
#include "dm_types.h"

LOG_MODULE_DECLARE(net_http_server_sample);

static int diag_get_handler(struct http_client_ctx *client, enum http_transaction_status status,
                const struct http_request_ctx *request_ctx,
                struct http_response_ctx *response_ctx, void *user_data)
{
    static char buf[768];

    ARG_UNUSED(client);
    ARG_UNUSED(request_ctx);
    ARG_UNUSED(user_data);

    if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
        return 0;
    }

    dm_snapshot_t snap;
    dm_get_snapshot(&snap);

    /* Serialize a compact view. For now, include up to last 8 events. */
    const uint16_t cap = DM_DIAG_EVENT_LOG_CAPACITY;
    uint16_t count = snap.diag.event_count;
    if (count > 8) {
        count = 8;
    }

    /* Compute starting index for last 'count' elements */
    uint16_t start;
    if (snap.diag.event_count <= count) {
        start = (uint16_t)((snap.diag.event_head + cap - snap.diag.event_count) % cap);
    } else {
        start = (uint16_t)((snap.diag.event_head + cap - count) % cap);
    }

    int n = snprintk(buf, sizeof(buf),
             "{"
             "\"active_faults\":%u,"
             "\"latched_faults\":%u,"
             "\"dropped_events\":%u,"
             "\"events\":[",
             snap.diag.active_faults,
             snap.diag.latched_faults,
             snap.diag.dropped_events);
    if (n < 0) {
        return n;
    }

    for (uint16_t i = 0; i < count; i++) {
        uint16_t idx = (uint16_t)((start + i) % cap);
        const dm_diag_event_t *e = &snap.diag.events[idx];

        int m = snprintk(buf + n, sizeof(buf) - (size_t)n,
                 "%s{\"code\":%u,\"sev\":%u,\"ts\":%lld,\"aux\":%u}",
                 (i == 0) ? "" : ",",
                 e->code, (uint32_t)e->sev, (long long)e->ts_ms, e->aux);
        if (m < 0) {
            return m;
        }
        n += m;

        if ((size_t)n >= sizeof(buf)) {
            /* Truncate safely */
            break;
        }
    }

    int tail = snprintk(buf + n, sizeof(buf) - (size_t)n, "]}\n");
    if (tail < 0) {
        return tail;
    }
    n += tail;

    response_ctx->status = 200;
    response_ctx->headers = HTTP_JSON_HEADERS_NO_STORE;
    response_ctx->header_count = ARRAY_SIZE(HTTP_JSON_HEADERS_NO_STORE);
    response_ctx->body = buf;
    response_ctx->body_len = (size_t)n;
    response_ctx->final_chunk = true;
    return 0;
}

/* POST /diag/clear body: {"mask":123,"clear_latched":true} */
struct diag_clear_cmd {
    int mask;
    bool clear_latched;
};

static const struct json_obj_descr diag_clear_cmd_descr[] = {
    JSON_OBJ_DESCR_PRIM(struct diag_clear_cmd, mask, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct diag_clear_cmd, clear_latched, JSON_TOK_TRUE),
};

static int diag_clear_post_handler(struct http_client_ctx *client, enum http_transaction_status status,
                   const struct http_request_ctx *request_ctx,
                   struct http_response_ctx *response_ctx, void *user_data)
{
    static uint8_t payload[128];
    static size_t cursor;
    static char reply[128];

    ARG_UNUSED(client);
    ARG_UNUSED(user_data);

    if (status == HTTP_SERVER_TRANSACTION_ABORTED ||
        status == HTTP_SERVER_TRANSACTION_COMPLETE) {
        cursor = 0;
        return 0;
    }

    if ((cursor + request_ctx->data_len) > sizeof(payload)) {
        cursor = 0;
        int n = snprintk(reply, sizeof(reply),
                 "{\"ok\":false,\"error\":\"payload_too_large\"}\n");
        response_ctx->status = 413;
        response_ctx->headers = HTTP_JSON_HEADERS_NO_STORE;
        response_ctx->header_count = ARRAY_SIZE(HTTP_JSON_HEADERS_NO_STORE);
        response_ctx->body = reply;
        response_ctx->body_len = (size_t)n;
        response_ctx->final_chunk = true;
        return 0;
    }

    memcpy(payload + cursor, request_ctx->data, request_ctx->data_len);
    cursor += request_ctx->data_len;

    if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
        return 0;
    }

    struct diag_clear_cmd cmd = {0};
    const int expected = BIT_MASK(ARRAY_SIZE(diag_clear_cmd_descr));
    int ret = json_obj_parse(payload, cursor,
                diag_clear_cmd_descr, ARRAY_SIZE(diag_clear_cmd_descr),
                &cmd);
    cursor = 0;

    if (ret != expected) {
        int n = snprintk(reply, sizeof(reply),
                 "{\"ok\":false,\"error\":\"bad_json\"}\n");
        response_ctx->status = 400;
        response_ctx->headers = HTTP_JSON_HEADERS_NO_STORE;
        response_ctx->header_count = ARRAY_SIZE(HTTP_JSON_HEADERS_NO_STORE);
        response_ctx->body = reply;
        response_ctx->body_len = (size_t)n;
        response_ctx->final_chunk = true;
        return 0;
    }

    dm_request_t req = {
        .id = DM_REQ_DIAG_CLEAR,
        .ts_ms = k_uptime_get(),
    };
    req.p.diag_clear.mask = (uint32_t)cmd.mask;
    req.p.diag_clear.clear_latched = cmd.clear_latched;

    /* Submit to diagnostics domain queue */
    int qrc = dm_diag_req_submit(&req);
    if (qrc != 0) {
        int n = snprintk(reply, sizeof(reply),
                 "{\"ok\":false,\"error\":\"queue_full\",\"err\":%d}\n", qrc);
        response_ctx->status = 503;
        response_ctx->headers = HTTP_JSON_HEADERS_NO_STORE;
        response_ctx->header_count = ARRAY_SIZE(HTTP_JSON_HEADERS_NO_STORE);
        response_ctx->body = reply;
        response_ctx->body_len = (size_t)n;
        response_ctx->final_chunk = true;
        return 0;
    }

    int n = snprintk(reply, sizeof(reply), "{\"ok\":true}\n");
    response_ctx->status = 200;
    response_ctx->headers = HTTP_JSON_HEADERS_NO_STORE;
    response_ctx->header_count = ARRAY_SIZE(HTTP_JSON_HEADERS_NO_STORE);
    response_ctx->body = reply;
    response_ctx->body_len = (size_t)n;
    response_ctx->final_chunk = true;
    return 0;
}

struct http_resource_detail_dynamic diag_resource_detail = {
    .common = {
        .type = HTTP_RESOURCE_TYPE_DYNAMIC,
        .bitmask_of_supported_http_methods = BIT(HTTP_GET),
    },
    .cb = diag_get_handler,
    .user_data = NULL,
};

struct http_resource_detail_dynamic diag_clear_resource_detail = {
    .common = {
        .type = HTTP_RESOURCE_TYPE_DYNAMIC,
        .bitmask_of_supported_http_methods = BIT(HTTP_POST),
    },
    .cb = diag_clear_post_handler,
    .user_data = NULL,
};