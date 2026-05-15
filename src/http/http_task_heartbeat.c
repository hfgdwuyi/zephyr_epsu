#include <zephyr/kernel.h>
#include <zephyr/net/http/server.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>

#include <string.h>

#include "http_common.h"

LOG_MODULE_DECLARE(net_http_server_sample);

static int heartbeat_handler(struct http_client_ctx *client, enum http_transaction_status status,
                 const struct http_request_ctx *request_ctx,
                 struct http_response_ctx *response_ctx, void *user_data)
{
    static const char body[] = "{}\n";

    ARG_UNUSED(client);
    ARG_UNUSED(request_ctx);
    ARG_UNUSED(user_data);

    if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
        return 0;
    }

    /* Minimal JSON reply (client may ignore it). */
    response_ctx->status = 200;
    response_ctx->headers = HTTP_JSON_HEADERS_NO_STORE;
    response_ctx->header_count = ARRAY_SIZE(HTTP_JSON_HEADERS_NO_STORE);
    response_ctx->body = body;
    response_ctx->body_len = strlen(body);
    response_ctx->final_chunk = true;
    return 0;
}

struct http_resource_detail_dynamic heartbeat_resource_detail = {
    .common = {
        .type = HTTP_RESOURCE_TYPE_DYNAMIC,
        .bitmask_of_supported_http_methods = BIT(HTTP_GET),
    },
    .cb = heartbeat_handler,
    .user_data = NULL,
};