/*
 * Bootloader swap handlers.
 *
 * Both endpoints trigger a permanent slot swap via MCUboot:
 *   boot_request_upgrade(true) + sys_reboot(SYS_REBOOT_COLD)
 *
 *   POST /api/v1/bootloader     — Main App → Bootloader App
 *   POST /api/v1/bootloader/exit — Bootloader App → Main App
 */
#include <zephyr/kernel.h>
#include <zephyr/net/http/server.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/logging/log.h>

#include <string.h>
#include <stdio.h>

#include "http_common.h"

LOG_MODULE_DECLARE(net_http_server_sample);

/* POST /api/v1/bootloader — swap to Bootloader App */
static int bootloader_handler(struct http_client_ctx *client, enum http_transaction_status status,
                 const struct http_request_ctx *request_ctx,
                 struct http_response_ctx *response_ctx, void *user_data)
{
	static char body[] = "{\"ok\":true,\"msg\":\"swapping to bootloader\"}\n";

	ARG_UNUSED(client);
	ARG_UNUSED(request_ctx);
	ARG_UNUSED(user_data);

	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}

	response_ctx->status = 200;
	response_ctx->headers = HTTP_JSON_HEADERS_NO_STORE;
	response_ctx->header_count = ARRAY_SIZE(HTTP_JSON_HEADERS_NO_STORE);
	response_ctx->body = body;
	response_ctx->body_len = strlen(body);
	response_ctx->final_chunk = true;

	boot_request_upgrade(true);
	k_sleep(K_MSEC(500));
	sys_reboot(SYS_REBOOT_COLD);
	return 0;
}

struct http_resource_detail_dynamic bootloader_resource_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_POST),
	},
	.cb = bootloader_handler,
	.user_data = NULL,
};

/* POST /api/v1/bootloader/exit — swap back to Main App */
static int bootloader_exit_handler(struct http_client_ctx *client, enum http_transaction_status status,
                 const struct http_request_ctx *request_ctx,
                 struct http_response_ctx *response_ctx, void *user_data)
{
	static char body[] = "{\"ok\":true,\"msg\":\"swapping to main app\"}\n";

	ARG_UNUSED(client);
	ARG_UNUSED(request_ctx);
	ARG_UNUSED(user_data);

	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}

	response_ctx->status = 200;
	response_ctx->headers = HTTP_JSON_HEADERS_NO_STORE;
	response_ctx->header_count = ARRAY_SIZE(HTTP_JSON_HEADERS_NO_STORE);
	response_ctx->body = body;
	response_ctx->body_len = strlen(body);
	response_ctx->final_chunk = true;

	boot_request_upgrade(true);
	k_sleep(K_MSEC(500));
	sys_reboot(SYS_REBOOT_COLD);
	return 0;
}

struct http_resource_detail_dynamic bootloader_exit_resource_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_POST),
	},
	.cb = bootloader_exit_handler,
	.user_data = NULL,
};
