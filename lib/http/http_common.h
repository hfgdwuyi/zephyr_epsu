#pragma once

#include <zephyr/net/http/server.h>

/* Common response headers for JSON APIs */
static const struct http_header HTTP_JSON_HEADERS_NO_STORE[] = {
    { .name = "Content-Type", .value = "application/json" },
    { .name = "Cache-Control", .value = "no-store" },
};