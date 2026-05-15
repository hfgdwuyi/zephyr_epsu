#include "http_api.h"

#include <zephyr/net/http/server.h>

void http_api_start(void)
{
    http_server_start();
}