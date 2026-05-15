#include <zephyr/kernel.h>
#include <zephyr/net/http/server.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>

#include <inttypes.h>

#include "http_common.h"

LOG_MODULE_DECLARE(net_http_server_sample);

static int uptime_handler(struct http_client_ctx *client, enum http_transaction_status status,
              const struct http_request_ctx *request_ctx,
              struct http_response_ctx *response_ctx, void *user_data)
{
    static char uptime_buf[32];

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
    response_ctx->headers = HTTP_JSON_HEADERS_NO_STORE;
    response_ctx->header_count = ARRAY_SIZE(HTTP_JSON_HEADERS_NO_STORE);
    response_ctx->body = uptime_buf;
    response_ctx->body_len = (size_t)ret;
    response_ctx->final_chunk = true;
    return 0;
}

struct http_resource_detail_dynamic uptime_resource_detail = {
    .common = {
        .type = HTTP_RESOURCE_TYPE_DYNAMIC,
        .bitmask_of_supported_http_methods = BIT(HTTP_GET),
    },
    .cb = uptime_handler,
    .user_data = NULL,
};