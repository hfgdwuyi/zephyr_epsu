/*
 * Bootloader swap handlers.
 *
 * Both endpoints trigger an OTA download of the target firmware via the
 * dm_update mechanism.  The update worker downloads the image into
 * slot1 at IMAGE_OFFSET (0x20000), verifies it, and requests an MCUboot
 * permanent upgrade followed by a reboot.
 *
 *   POST /api/v1/bootloader     — Main App → Bootloader App
 *     body: {"uri": "http://<host>/bootloader.signed.bin"}
 *
 *   POST /api/v1/bootloader/exit — Bootloader App → Main App
 *     body: {"uri": "http://<host>/application.signed.bin"}
 */
#include <zephyr/kernel.h>
#include <zephyr/net/http/server.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <zephyr/sys/printk.h>

#include "http_common.h"
#include "dm_api.h"
#include "dm_types.h"

LOG_MODULE_DECLARE(net_http_server_sample);

/* Helper: parse "uri" value from a JSON body like {"uri": "http://..."} */
static bool parse_uri_from_json(const uint8_t *data, size_t len,
				char *uri_out, size_t uri_sz)
{
	const char *key = strstr((const char *)data, "\"uri\"");
	if (key == NULL) {
		return false;
	}

	const char *val_start = strchr(key + 5, '"');
	if (val_start == NULL) {
		return false;
	}
	val_start++; /* skip the opening quote */

	const char *val_end = strchr(val_start, '"');
	if (val_end == NULL) {
		return false;
	}

	size_t uri_len = (size_t)(val_end - val_start);
	if (uri_len >= uri_sz) {
		uri_len = uri_sz - 1;
	}
	memcpy(uri_out, val_start, uri_len);
	uri_out[uri_len] = '\0';
	return (uri_len > 0);
}

/*
 * Generic handler: parse URI from JSON body, submit OTA update request.
 * The update worker does the rest (download → verify → upgrade → reboot).
 */
static int bootloader_swap_handler(struct http_client_ctx *client,
				   enum http_transaction_status status,
				   const struct http_request_ctx *request_ctx,
				   struct http_response_ctx *response_ctx,
				   void *user_data,
				   const char *action_name)
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

	/* Accumulate body */
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

	/* Parse URI */
	char uri[160] = {0};
	bool ok = parse_uri_from_json(payload, cursor, uri, sizeof(uri));
	cursor = 0;

	if (!ok) {
		printk("BOOTLOADER: %s — bad or missing uri\n", action_name);
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

	printk("BOOTLOADER: %s — downloading from %s\n", action_name, uri);

	/* Submit OTA update request */
	dm_request_t req = {
		.id = DM_REQ_UPDATE_START,
		.ts_ms = k_uptime_get(),
	};
	strncpy(req.p.update_start.uri, uri, sizeof(req.p.update_start.uri) - 1);
	req.p.update_start.uri[sizeof(req.p.update_start.uri) - 1] = '\0';

	dm_update_set_uri(uri);
	dm_update_set_state(DM_UPDATE_STATE_REQUESTED, 0, 0);
	(void)dm_update_req_submit(&req);

	int n = snprintk(reply, sizeof(reply),
			 "{\"ok\":true,\"msg\":\"ota started\"}\n");
	response_ctx->status = 200;
	response_ctx->headers = HTTP_JSON_HEADERS_NO_STORE;
	response_ctx->header_count = ARRAY_SIZE(HTTP_JSON_HEADERS_NO_STORE);
	response_ctx->body = reply;
	response_ctx->body_len = (size_t)n;
	response_ctx->final_chunk = true;
	return 0;
}

/* POST /api/v1/bootloader — swap to Bootloader App */
static int bootloader_handler(struct http_client_ctx *client,
			      enum http_transaction_status status,
			      const struct http_request_ctx *request_ctx,
			      struct http_response_ctx *response_ctx,
			      void *user_data)
{
	return bootloader_swap_handler(client, status, request_ctx,
				       response_ctx, user_data, "bootloader");
}

/* POST /api/v1/bootloader/exit — swap back to Main App */
static int bootloader_exit_handler(struct http_client_ctx *client,
				   enum http_transaction_status status,
				   const struct http_request_ctx *request_ctx,
				   struct http_response_ctx *response_ctx,
				   void *user_data)
{
	return bootloader_swap_handler(client, status, request_ctx,
				       response_ctx, user_data, "exit");
}

struct http_resource_detail_dynamic bootloader_resource_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_POST),
	},
	.cb = bootloader_handler,
	.user_data = NULL,
};

struct http_resource_detail_dynamic bootloader_exit_resource_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_POST),
	},
	.cb = bootloader_exit_handler,
	.user_data = NULL,
};
