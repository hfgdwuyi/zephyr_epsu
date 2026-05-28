/*
 * http_task_update.c
 *
 * REST endpoints:
 *  - POST /update/start   : submit an update start request (URI) to update domain queue
 *  - GET  /update/status  : read current update state/progress from DataModel snapshot
 *
 * Design intent:
 *  - HTTP layer only reads from DataModel and submits requests.
 *  - Update execution is performed by update_task.c consuming the update request queue.
 *
 * Notes:
 *  - Uses static payload/reply buffers. If multiple HTTP clients are enabled concurrently,
 *    consider per-client storage or locking.
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

/* POST /update/start: {"uri":"http://..."} */
struct update_start_cmd {
    char uri[160];
};

static const struct json_obj_descr update_start_cmd_descr[] = {
    JSON_OBJ_DESCR_PRIM(struct update_start_cmd, uri, JSON_TOK_STRING),
};

static int update_start_handler(struct http_client_ctx *client, enum http_transaction_status status,
                const struct http_request_ctx *request_ctx,
                struct http_response_ctx *response_ctx, void *user_data)
{
    static uint8_t payload[256];
    static size_t cursor;
    static char reply[160];

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

    struct update_start_cmd cmd = {0};
    const int expected = BIT_MASK(ARRAY_SIZE(update_start_cmd_descr));
    int ret = json_obj_parse(payload, cursor,
                update_start_cmd_descr, ARRAY_SIZE(update_start_cmd_descr),
                &cmd);
    cursor = 0;

    if (ret != expected || cmd.uri[0] == '\0') {
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
        .id = DM_REQ_UPDATE_START,
        .ts_ms = k_uptime_get(),
    };
    strncpy(req.p.update_start.uri, cmd.uri, sizeof(req.p.update_start.uri) - 1);
    req.p.update_start.uri[sizeof(req.p.update_start.uri) - 1] = '\0';

    /* Submit to update domain queue; worker thread handles the rest */
    dm_update_set_uri(cmd.uri);
    dm_update_set_state(DM_UPDATE_STATE_REQUESTED, 0, 0);
    (void)dm_update_req_submit(&req);

    int n = snprintk(reply, sizeof(reply), "{\"ok\":true}\n");
    response_ctx->status = 200;
    response_ctx->headers = HTTP_JSON_HEADERS_NO_STORE;
    response_ctx->header_count = ARRAY_SIZE(HTTP_JSON_HEADERS_NO_STORE);
    response_ctx->body = reply;
    response_ctx->body_len = (size_t)n;
    response_ctx->final_chunk = true;
    return 0;
}

static int update_status_handler(struct http_client_ctx *client, enum http_transaction_status status,
                 const struct http_request_ctx *request_ctx,
                 struct http_response_ctx *response_ctx, void *user_data)
{
    static char buf[420];

    ARG_UNUSED(client);
    ARG_UNUSED(request_ctx);
    ARG_UNUSED(user_data);

    if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
        return 0;
    }

    dm_snapshot_t snap;
    dm_get_snapshot(&snap);

    /* Minimal JSON without advanced escaping (URI should be plain). */
    int n = snprintk(buf, sizeof(buf),
             "{"
             "\"state\":%u,"
             "\"progress\":%u,"
             "\"last_error\":%d,"
             "\"uri\":\"%s\","
             "\"last_change_ts\":%lld"
             "}\n",
             (uint32_t)snap.update.state,
             (uint32_t)snap.update.progress_percent,
             (int)snap.update.last_error,
             snap.update.package_uri,
             (long long)snap.update.last_change_ts_ms);
    if (n < 0) {
        return n;
    }

    response_ctx->status = 200;
    response_ctx->headers = HTTP_JSON_HEADERS_NO_STORE;
    response_ctx->header_count = ARRAY_SIZE(HTTP_JSON_HEADERS_NO_STORE);
    response_ctx->body = buf;
    response_ctx->body_len = (size_t)n;
    response_ctx->final_chunk = true;
    return 0;
}

struct http_resource_detail_dynamic update_start_resource_detail = {
    .common = {
        .type = HTTP_RESOURCE_TYPE_DYNAMIC,
        .bitmask_of_supported_http_methods = BIT(HTTP_POST),
    },
    .cb = update_start_handler,
    .user_data = NULL,
};

struct http_resource_detail_dynamic update_status_resource_detail = {
    .common = {
        .type = HTTP_RESOURCE_TYPE_DYNAMIC,
        .bitmask_of_supported_http_methods = BIT(HTTP_GET),
    },
    .cb = update_status_handler,
    .user_data = NULL,
};