/*
 * http_task_control.c
 *
 * REST endpoint:
 *  - POST /api/v1/control
 *
 * JSON body (LED example):
 *  - {"action":1,"index":0,"on":1,"value":0}
 *
 * Notes:
 *  - Avoids JSON boolean tokens for compatibility with Zephyr versions that do not provide JSON_TOK_BOOL.
 *  - Tolerates UTF-8 BOM at the beginning of the body.
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
#include "bsp_led.h"

LOG_MODULE_DECLARE(net_http_server_sample);

#ifndef HTTP_CTRL_MAX_PAYLOAD
#define HTTP_CTRL_MAX_PAYLOAD 256
#endif

#ifndef HTTP_CTRL_MAX_INDEX
#define HTTP_CTRL_MAX_INDEX 32
#endif

struct control_cmd {
    int action; /* numeric dm_ctrl_action_t */
    int index;
    int on;     /* 0/1 (numeric) */
    int value;  /* optional, defaults to 0 */
};

static const struct json_obj_descr control_cmd_descr[] = {
    JSON_OBJ_DESCR_PRIM(struct control_cmd, action, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct control_cmd, index, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct control_cmd, on, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct control_cmd, value, JSON_TOK_NUMBER),
};

static bool action_valid(int action)
{
    switch ((dm_ctrl_action_t)action) {
    case DM_CTRL_ACT_LED_SET:
    case DM_CTRL_ACT_RELAY_SET:
    case DM_CTRL_ACT_PSU_CH_SET:
    case DM_CTRL_ACT_BUZZER_SET:
        return true;
    default:
        return false;
    }
}

static bool on_valid(int on)
{
    return (on == 0) || (on == 1);
}

static const uint8_t *skip_utf8_bom(const uint8_t *buf, size_t *len)
{
    if (buf == NULL || len == NULL) {
        return buf;
    }

    if (*len >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF) {
        buf += 3;
        *len -= 3;
    }
    return buf;
}

static void dump_prefix_hex(const uint8_t *buf, size_t len)
{
    /* Print first up to 16 bytes for troubleshooting */
    char hex[3 * 16 + 1];
    memset(hex, 0, sizeof(hex));

    size_t n = MIN(len, (size_t)16);
    for (size_t i = 0; i < n; i++) {
        snprintk(&hex[i * 3], 4, "%02X ", buf[i]);
    }
    LOG_ERR("control: payload prefix hex: %s", hex);
}

static int parse_control_post(const uint8_t *buf, size_t len, struct control_cmd *out, int *out_mask)
{
    const int expected = BIT_MASK(ARRAY_SIZE(control_cmd_descr));

    int ret = json_obj_parse((uint8_t *)buf, len,
                control_cmd_descr, ARRAY_SIZE(control_cmd_descr),
                out);

    if (out_mask) {
        *out_mask = ret;
    }

    return (ret == expected) ? 0 : -EINVAL;
}

static int control_post_handler(struct http_client_ctx *client, enum http_transaction_status status,
                const struct http_request_ctx *request_ctx,
                struct http_response_ctx *response_ctx, void *user_data)
{
    /* +1 for '\0' safety terminator */
    static uint8_t payload[HTTP_CTRL_MAX_PAYLOAD + 1];
    static size_t cursor;
    static char reply[160];

    ARG_UNUSED(client);
    ARG_UNUSED(user_data);

    if (status == HTTP_SERVER_TRANSACTION_ABORTED ||
        status == HTTP_SERVER_TRANSACTION_COMPLETE) {
        cursor = 0;
        return 0;
    }

    /* Keep one byte for payload[cursor] = '\0' */
    if ((cursor + request_ctx->data_len) > HTTP_CTRL_MAX_PAYLOAD) {
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

    payload[cursor] = '\0';

    /* Prepare parse buffer (skip UTF-8 BOM if present) */
    size_t parse_len = cursor;
    const uint8_t *parse_buf = skip_utf8_bom(payload, &parse_len);

    struct control_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));

    int mask = 0;
    int prc = parse_control_post(parse_buf, parse_len, &cmd, &mask);
    cursor = 0;

    if (prc != 0) {
        dump_prefix_hex(parse_buf, parse_len);

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

    if (!action_valid(cmd.action)) {
        int n = snprintk(reply, sizeof(reply),
                 "{\"ok\":false,\"error\":\"bad_action\"}\n");
        response_ctx->status = 400;
        response_ctx->headers = HTTP_JSON_HEADERS_NO_STORE;
        response_ctx->header_count = ARRAY_SIZE(HTTP_JSON_HEADERS_NO_STORE);
        response_ctx->body = reply;
        response_ctx->body_len = (size_t)n;
        response_ctx->final_chunk = true;
        return 0;
    }

    if (cmd.index < 0 || cmd.index >= HTTP_CTRL_MAX_INDEX) {
        int n = snprintk(reply, sizeof(reply),
                 "{\"ok\":false,\"error\":\"bad_index\",\"max\":%d}\n",
                 HTTP_CTRL_MAX_INDEX - 1);
        response_ctx->status = 400;
        response_ctx->headers = HTTP_JSON_HEADERS_NO_STORE;
        response_ctx->header_count = ARRAY_SIZE(HTTP_JSON_HEADERS_NO_STORE);
        response_ctx->body = reply;
        response_ctx->body_len = (size_t)n;
        response_ctx->final_chunk = true;
        return 0;
    }

    if (!on_valid(cmd.on)) {
        int n = snprintk(reply, sizeof(reply),
                 "{\"ok\":false,\"error\":\"bad_on\"}\n");
        response_ctx->status = 400;
        response_ctx->headers = HTTP_JSON_HEADERS_NO_STORE;
        response_ctx->header_count = ARRAY_SIZE(HTTP_JSON_HEADERS_NO_STORE);
        response_ctx->body = reply;
        response_ctx->body_len = (size_t)n;
        response_ctx->final_chunk = true;
        return 0;
    }

    dm_request_t req = {
        .id = DM_REQ_CTRL_ACTION,
        .ts_ms = k_uptime_get(),
    };

    req.p.ctrl_action.action = (dm_ctrl_action_t)cmd.action;
    req.p.ctrl_action.index = (uint8_t)cmd.index;
    req.p.ctrl_action.on = (cmd.on != 0);
    req.p.ctrl_action.value = (int32_t)cmd.value;

    if (req.id == DM_REQ_CTRL_ACTION && req.p.ctrl_action.action == DM_CTRL_ACT_LED_SET) {
        uint8_t idx = req.p.ctrl_action.index;
        bool on = req.p.ctrl_action.on;

        if (on) {
            ledSwitchOn(idx);
        } else {
            ledSwitchOff(idx);
        }
    }

    int qrc = dm_ctrl_req_submit(&req);
    if (qrc != 0) {
        int n = snprintk(reply, sizeof(reply),
                 "{\"ok\":false,\"error\":\"queue_full\",\"err\":%d}\n",
                 qrc);
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

struct http_resource_detail_dynamic control_resource_detail = {
    .common = {
        .type = HTTP_RESOURCE_TYPE_DYNAMIC,
        .bitmask_of_supported_http_methods = BIT(HTTP_POST),
    },
    .cb = control_post_handler,
    .user_data = NULL,
};