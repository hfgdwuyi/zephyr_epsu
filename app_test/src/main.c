#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/net_config.h>
#include <zephyr/data/json.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/reboot.h>

#include <errno.h>
#include <string.h>

#include "ota_update.h"

/* ---------- JSON response headers ---------- */
static const struct http_header json_headers[] = {
	{ .name = "Content-Type",  .value = "application/json"   },
	{ .name = "Cache-Control", .value = "no-store"            },
};

static uint16_t svc_port = 80;

/* ---------- POST /update/start ---------- */
struct start_cmd {
	char uri[160];
};

static const struct json_obj_descr start_cmd_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct start_cmd, uri, JSON_TOK_STRING),
};

static int handle_update_start(struct http_client_ctx *client,
			       enum http_transaction_status status,
			       const struct http_request_ctx *request,
			       struct http_response_ctx *response,
			       void *user_data)
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

	if ((cursor + request->data_len) > sizeof(payload)) {
		cursor = 0;
		int n = snprintk(reply, sizeof(reply),
			"{\"ok\":false,\"error\":\"too_large\"}\n");
		response->status = 413;
		response->headers = json_headers;
		response->header_count = ARRAY_SIZE(json_headers);
		response->body = reply;
		response->body_len = n;
		response->final_chunk = true;
		return 0;
	}

	memcpy(payload + cursor, request->data, request->data_len);
	cursor += request->data_len;

	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}

	/* null-terminate for safety */
	if (cursor < sizeof(payload)) {
		payload[cursor] = '\0';
	}

	/* Manual JSON parsing: extract "uri" value from {"uri":"..."} */
	char uri[160] = {0};
	const char *key = strstr((const char *)payload, "\"uri\"");
	if (key != NULL) {
		const char *val_start = strchr(key + 5, '"');
		if (val_start != NULL) {
			val_start++;
			const char *val_end = strchr(val_start, '"');
			if (val_end != NULL) {
				size_t len = (size_t)(val_end - val_start);
				if (len >= sizeof(uri)) {
					len = sizeof(uri) - 1;
				}
				memcpy(uri, val_start, len);
				uri[len] = '\0';
			}
		}
	}
	cursor = 0;

	printk("OTA parsed uri='%s'\n", uri);

	if (uri[0] == '\0') {
		int n = snprintk(reply, sizeof(reply),
			"{\"ok\":false,\"error\":\"bad_json\"}\n");
		response->status = 400;
		response->headers = json_headers;
		response->header_count = ARRAY_SIZE(json_headers);
		response->body = reply;
		response->body_len = n;
		response->final_chunk = true;
		return 0;
	}

	int rc = ota_update_start(uri);
	if (rc != 0) {
		int n = snprintk(reply, sizeof(reply),
			"{\"ok\":false,\"error\":\"ota_failed\",\"rc\":%d}\n", rc);
		response->status = 500;
		response->headers = json_headers;
		response->header_count = ARRAY_SIZE(json_headers);
		response->body = reply;
		response->body_len = n;
		response->final_chunk = true;
		return 0;
	}

	int n = snprintk(reply, sizeof(reply), "{\"ok\":true}\n");
	response->status = 200;
	response->headers = json_headers;
	response->header_count = ARRAY_SIZE(json_headers);
	response->body = reply;
	response->body_len = n;
	response->final_chunk = true;
	return 0;
}

static struct http_resource_detail_dynamic update_start_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_POST),
	},
	.cb = handle_update_start,
	.user_data = NULL,
};

/* ---------- GET /update/status ---------- */
static int handle_update_status(struct http_client_ctx *client,
				enum http_transaction_status status,
				const struct http_request_ctx *request,
				struct http_response_ctx *response,
				void *user_data)
{
	static char buf[256];

	ARG_UNUSED(client);
	ARG_UNUSED(request);
	ARG_UNUSED(user_data);

	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}

	enum ota_state s = ota_get_state();
	int n = snprintk(buf, sizeof(buf),
		"{\"state\":%d,\"progress\":%u,\"error\":%d}\n",
		(int)s, ota_get_progress(), ota_get_error());

	response->status = 200;
	response->headers = json_headers;
	response->header_count = ARRAY_SIZE(json_headers);
	response->body = buf;
	response->body_len = n;
	response->final_chunk = true;
	return 0;
}

static struct http_resource_detail_dynamic update_status_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET),
	},
	.cb = handle_update_status,
	.user_data = NULL,
};

/* ---------- HTTP Resource registration (must be before SERVICE) ---------- */
HTTP_RESOURCE_DEFINE(update_start, ota_svc, "/update/start", &update_start_detail);
HTTP_RESOURCE_DEFINE(update_status, ota_svc, "/update/status", &update_status_detail);

/* ---------- HTTP Service ---------- */
HTTP_SERVICE_DEFINE(ota_svc, "0.0.0.0", &svc_port, 3, 3, NULL, NULL, NULL);

/* ---------- Main ---------- */
int main(void)
{
	printk("\n===== TestApp Version 1 (OTA) =====\n");
	boot_write_img_confirmed();
	printk("Image confirmed, running...\n");

	/* Init network (static IP 192.0.2.1) */
	printk("Initializing network...\n");
	(void)net_config_init_app(NULL, "Initializing network");
	printk("Network ready\n");

	/* Give network stack time to stabilize before binding */
	k_sleep(K_SECONDS(2));

	/* Start HTTP server */
	int rc = http_server_start();
	if (rc != 0) {
		printk("HTTP server start failed: %d\n", rc);
	} else {
		printk("HTTP server listening on port %u\n", svc_port);
	}

	printk("OTA endpoints: POST /update/start  GET /update/status\n");

	uint32_t count = 0;
	while (1) {
		count++;
		printk("V1 Alive: %u\n", count);
		k_sleep(K_SECONDS(3));
	}
	return 0;
}
