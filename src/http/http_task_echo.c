#include <zephyr/kernel.h>
#include <zephyr/net/http/server.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(net_http_server_sample);

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

    snprintf(print_str, sizeof(print_str), "%s received (%zd bytes)",
         http_method_str(method), request_ctx->data_len);
    LOG_HEXDUMP_DBG(request_ctx->data, request_ctx->data_len, print_str);

    if (status == HTTP_SERVER_REQUEST_DATA_FINAL) {
        LOG_DBG("All data received (%zd bytes).", processed);
        processed = 0;
    }

    response_ctx->body = request_ctx->data;
    response_ctx->body_len = request_ctx->data_len;
    response_ctx->final_chunk = (status == HTTP_SERVER_REQUEST_DATA_FINAL);
    if (status == HTTP_SERVER_REQUEST_DATA_FINAL) {
        static const struct http_header echo_headers[] = {
            { .name = "Content-Type", .value = "application/octet-stream" },
        };
        response_ctx->headers = echo_headers;
        response_ctx->header_count = ARRAY_SIZE(echo_headers);
    }
    return 0;
}

struct http_resource_detail_dynamic echo_resource_detail = {
    .common = {
        .type = HTTP_RESOURCE_TYPE_DYNAMIC,
        .bitmask_of_supported_http_methods = BIT(HTTP_GET) | BIT(HTTP_POST),
    },
    .cb = echo_handler,
    .user_data = NULL,
};