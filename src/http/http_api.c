#include "http_api.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>
#include <zephyr/data/json.h>
#include <zephyr/sys/util.h>

#include <string.h>
#include <errno.h>
#include <inttypes.h>

#include "bsp_led.h" 

LOG_MODULE_REGISTER(net_http_server_sample, LOG_LEVEL_DBG);

struct led_command {
    int led_num;
    bool led_state;
};

static const struct json_obj_descr led_command_descr[] = {
    JSON_OBJ_DESCR_PRIM(struct led_command, led_num, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct led_command, led_state, JSON_TOK_TRUE),
};

/* ---- Handlers ---- */

static int echo_handler(struct http_client_ctx *client, enum http_transaction_status status,
            const struct http_request_ctx *request_ctx,
            struct http_response_ctx *response_ctx, void *user_data)
{
#define MAX_TEMP_PRINT_LEN 32
    static char print_str[MAX_TEMP_PRINT_LEN];
    enum http_method method = client->method;
    static size_t processed;

    ARG_UNUSED(user_data);

    if (status == HTTP_SERVER_TRANSACTION_ABORTED ||
        status == HTTP_SERVER_TRANSACTION_COMPLETE) {
        if (status == HTTP_SERVER_TRANSACTION_ABORTED) {
            LOG_DBG("Transaction aborted after %zd bytes.", processed);
        }
        processed = 0;
        return 0;
    }

    __ASSERT_NO_MSG(request_ctx->data != NULL);

    processed += request_ctx->data_len;

    snprintf(print_str, sizeof(print_str), "%s received (%zd bytes)", http_method_str(method),
         request_ctx->data_len);
    LOG_HEXDUMP_DBG(request_ctx->data, request_ctx->data_len, print_str);

    if (status == HTTP_SERVER_REQUEST_DATA_FINAL) {
        LOG_DBG("All data received (%zd bytes).", processed);
        processed = 0;
    }

    response_ctx->body = request_ctx->data;
    response_ctx->body_len = request_ctx->data_len;
    response_ctx->final_chunk = (status == HTTP_SERVER_REQUEST_DATA_FINAL);
    return 0;
}

static int uptime_handler(struct http_client_ctx *client, enum http_transaction_status status,
              const struct http_request_ctx *request_ctx,
              struct http_response_ctx *response_ctx, void *user_data)
{
    static char uptime_buf[32];

    static const struct http_header uptime_headers[] = {
        { .name = "Content-Type", .value = "application/json" },
        { .name = "Cache-Control", .value = "no-store" },
    };

    ARG_UNUSED(client);
    ARG_UNUSED(request_ctx);
    ARG_UNUSED(user_data);

    if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
        return 0;
    }

    int ret = snprintk(uptime_buf, sizeof(uptime_buf), "%" PRId64, k_uptime_get());
    if (ret < 0) {
        return ret;
    }

    response_ctx->status = 200;
    response_ctx->headers = uptime_headers;
    response_ctx->header_count = ARRAY_SIZE(uptime_headers);
    response_ctx->body = uptime_buf;
    response_ctx->body_len = (size_t)ret;
    response_ctx->final_chunk = true;
    return 0;
}

static int heartbeat_handler(struct http_client_ctx *client, enum http_transaction_status status,
                 const struct http_request_ctx *request_ctx,
                 struct http_response_ctx *response_ctx, void *user_data)
{
    static char hb_buf[160];

    static const struct http_header hb_headers[] = {
        { .name = "Content-Type", .value = "application/json" },
        { .name = "Cache-Control", .value = "no-store" },
    };

    ARG_UNUSED(client);
    ARG_UNUSED(request_ctx);
    ARG_UNUSED(user_data);

    if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
        return 0;
    }

    int len = snprintk(hb_buf, sizeof(hb_buf),
               "{"
               "\"deviceId\":\"%s\","
               "\"state\":\"%s\","
               "\"uptimeSec\":%" PRId64 ","
               "\"lastResetReason\":\"%s\""
               "}\n",
               "pdm01", "operational", k_uptime_get() / 1000, "power_on");
    if (len < 0) {
        return len;
    }

    response_ctx->status = 200;
    response_ctx->headers = hb_headers;
    response_ctx->header_count = ARRAY_SIZE(hb_headers);
    response_ctx->body = hb_buf;
    response_ctx->body_len = (size_t)len;
    response_ctx->final_chunk = true;
    return 0;
}


static int parse_led_post(const uint8_t *buf, size_t len, struct led_command *out)
{
    const int expected = BIT_MASK(ARRAY_SIZE(led_command_descr));
    int ret = json_obj_parse((uint8_t *)buf, len,
                led_command_descr, ARRAY_SIZE(led_command_descr),
                out);
    if (ret != expected) {
        return -EINVAL;
    }
    return 0;
}

static int led_handler(struct http_client_ctx *client, enum http_transaction_status status,
               const struct http_request_ctx *request_ctx,
               struct http_response_ctx *response_ctx, void *user_data)
{
    static uint8_t payload[128];
    static size_t cursor;

    static const struct http_header headers[] = {
        { .name = "Content-Type", .value = "application/json" },
        { .name = "Cache-Control", .value = "no-store" },
    };

    static char reply[96];

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
        response_ctx->headers = headers;
        response_ctx->header_count = ARRAY_SIZE(headers);
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

    struct led_command cmd;
    int pret = parse_led_post(payload, cursor, &cmd);
    cursor = 0;

    if (pret != 0) {
        int n = snprintk(reply, sizeof(reply),
                 "{\"ok\":false,\"error\":\"bad_json\"}\n");
        response_ctx->status = 400;
        response_ctx->headers = headers;
        response_ctx->header_count = ARRAY_SIZE(headers);
        response_ctx->body = reply;
        response_ctx->body_len = (size_t)n;
        response_ctx->final_chunk = true;
        return 0;
    }

    if (cmd.led_state) {
        ledSwitchOn((uint8_t)cmd.led_num);
    } else {
        ledSwitchOff((uint8_t)cmd.led_num);
    }

    int n = snprintk(reply, sizeof(reply), "{\"ok\":true}\n");
    response_ctx->status = 200;
    response_ctx->headers = headers;
    response_ctx->header_count = ARRAY_SIZE(headers);
    response_ctx->body = reply;
    response_ctx->body_len = (size_t)n;
    response_ctx->final_chunk = true;

    return 0;
}

/* ---- Resource details ---- */

static struct http_resource_detail_dynamic echo_resource_detail = {
    .common = {
        .type = HTTP_RESOURCE_TYPE_DYNAMIC,
        .bitmask_of_supported_http_methods = BIT(HTTP_GET) | BIT(HTTP_POST),
    },
    .cb = echo_handler,
    .user_data = NULL,
};

static struct http_resource_detail_dynamic uptime_resource_detail = {
    .common = {
        .type = HTTP_RESOURCE_TYPE_DYNAMIC,
        .bitmask_of_supported_http_methods = BIT(HTTP_GET),
    },
    .cb = uptime_handler,
    .user_data = NULL,
};

static struct http_resource_detail_dynamic heartbeat_resource_detail = {
    .common = {
        .type = HTTP_RESOURCE_TYPE_DYNAMIC,
        .bitmask_of_supported_http_methods = BIT(HTTP_GET),
    },
    .cb = heartbeat_handler,
    .user_data = NULL,
};

static struct http_resource_detail_dynamic led_resource_detail = {
    .common = {
        .type = HTTP_RESOURCE_TYPE_DYNAMIC,
        .bitmask_of_supported_http_methods = BIT(HTTP_POST),
    },
    .cb = led_handler,
    .user_data = NULL,
};

/* ---- Service + resources (must stay in same translation unit as details/handlers) ---- */

static uint16_t test_http_service_port = 80;

HTTP_SERVICE_DEFINE(test_http_service, NULL, &test_http_service_port,
            CONFIG_HTTP_SERVER_MAX_CLIENTS, 10, NULL, NULL, NULL);

HTTP_RESOURCE_DEFINE(echo_resource, test_http_service, "/dynamic", &echo_resource_detail);
HTTP_RESOURCE_DEFINE(uptime_resource, test_http_service, "/uptime", &uptime_resource_detail);
HTTP_RESOURCE_DEFINE(led_resource, test_http_service, "/led", &led_resource_detail);
HTTP_RESOURCE_DEFINE(heartbeat_resource, test_http_service,
             "/api/v1/status/heartbeat", &heartbeat_resource_detail);

void http_api_start(void)
{
    http_server_start();
}