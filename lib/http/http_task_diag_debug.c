/*
 * http_task_diag_debug.c
 *
 * DEBUG endpoint:
 *  - POST /api/v1/diag/inject : inject one diagnostic event and optionally set fault bits
 *
 * Body example:
 *  {"code":4660,"sev":1,"aux":0,"set_active_mask":1,"set_latched_mask":2}
 */

#include <zephyr/kernel.h>
#include <zephyr/net/http/server.h>
#include <zephyr/data/json.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>

#include <errno.h>
#include <string.h>
#include <stdint.h>

#include "http_common.h"
#include "dm_api.h"
#include "dm_types.h"

LOG_MODULE_DECLARE(net_http_server_sample);

#ifndef HTTP_DIAG_DEBUG_MAX_PAYLOAD
#define HTTP_DIAG_DEBUG_MAX_PAYLOAD 128
#endif

struct diag_inject_cmd {
    int code;
    int sev;
    int aux;
    int set_active_mask;
    int set_latched_mask;
};

static const struct json_obj_descr diag_inject_cmd_descr[] = {
    JSON_OBJ_DESCR_PRIM(struct diag_inject_cmd, code, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct diag_inject_cmd, sev, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct diag_inject_cmd, aux, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct diag_inject_cmd, set_active_mask, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct diag_inject_cmd, set_latched_mask, JSON_TOK_NUMBER),
};

static int diag_inject_post_handler(struct http_client_ctx *client,
                    enum http_transaction_status status,
                    const struct http_request_ctx *request_ctx,
                    struct http_response_ctx *response_ctx,
                    void *user_data)
{
    static uint8_t payload[HTTP_DIAG_DEBUG_MAX_PAYLOAD];
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

    struct diag_inject_cmd cmd = {0};
    const int expected = BIT_MASK(ARRAY_SIZE(diag_inject_cmd_descr));
    int ret = json_obj_parse(payload, cursor,
                diag_inject_cmd_descr, ARRAY_SIZE(diag_inject_cmd_descr),
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

    /* Inject one event */
    dm_diag_add_event((uint32_t)cmd.code, (dm_diag_severity_t)cmd.sev, (uint32_t)cmd.aux);

    /* Optionally set fault bits - these APIs mutate DM immediately (no queue). */
    if (cmd.set_active_mask != 0) {
        dm_diag_set_fault_bits((uint32_t)cmd.set_active_mask, true, false);
    }
    if (cmd.set_latched_mask != 0) {
        dm_diag_set_fault_bits((uint32_t)cmd.set_latched_mask, true, true);
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

struct http_resource_detail_dynamic diag_inject_resource_detail = {
    .common = {
        .type = HTTP_RESOURCE_TYPE_DYNAMIC,
        .bitmask_of_supported_http_methods = BIT(HTTP_POST),
    },
    .cb = diag_inject_post_handler,
    .user_data = NULL,
};