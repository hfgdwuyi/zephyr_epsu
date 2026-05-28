/*
 * dm_update_worker.c
 *
 * Update worker thread — consumes dm_update_req_q and performs firmware
 * download, verification, and MCUboot swap requests.
 *
 * Design:
 *  - HTTP GET the firmware binary from the URI in the request.
 *  - Stream-write to slot1 via flash_area API using response callback.
 *  - Verify MCUboot image header magic.
 *  - Request MCUboot to swap on next boot, then reboot.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/http/client.h>
#include <zephyr/sys/reboot.h>

#include <errno.h>
#include <string.h>
#include <stdint.h>

#include "dm_api.h"
#include "dm_types.h"

LOG_MODULE_REGISTER(update_worker, LOG_LEVEL_INF);

/* MCUboot image header magic (little-endian: 0x96f3b83d) */
#define IMAGE_MAGIC 0x96f3b83d

/* MCUboot image header (first 32 bytes of a signed image) */
struct mcuboot_image_header {
    uint32_t ih_magic;
    uint32_t ih_load_addr;
    uint16_t ih_hdr_size;
    uint16_t ih_protect_tlv_size;
    uint32_t ih_img_size;
    uint32_t ih_flags;
    struct {
        uint8_t  major;
        uint8_t  minor;
        uint16_t revision;
        uint32_t build_num;
    } ih_ver;
    uint32_t _pad1;
};

/* Per-download state passed to response callback */
struct download_ctx {
    const struct flash_area *fa;
    uint32_t offset;
    uint32_t last_progress;
};

static int download_response_cb(struct http_response *rsp,
                                enum http_final_call final_data,
                                void *user_data)
{
    struct download_ctx *ctx = (struct download_ctx *)user_data;

    if (ctx == NULL || rsp == NULL) {
        return -EINVAL;
    }

    /* Write body chunk to flash */
    if (rsp->body_frag_len > 0 && rsp->body_frag_start != NULL) {
        int rc = flash_area_write(ctx->fa, ctx->offset,
                                  rsp->body_frag_start,
                                  (uint32_t)rsp->body_frag_len);
        if (rc != 0) {
            LOG_ERR("update_worker: flash_write at 0x%x rc=%d",
                    (unsigned)ctx->offset, rc);
            return rc;
        }

        ctx->offset += (uint32_t)rsp->body_frag_len;

        /* Progress update every 10% */
        if (rsp->content_length > 0) {
            uint32_t pct = (ctx->offset * 100) / (uint32_t)rsp->content_length;
            if (pct > ctx->last_progress && pct <= 100) {
                ctx->last_progress = pct;
                dm_update_set_state(DM_UPDATE_STATE_DOWNLOADING, 0, (uint8_t)pct);
                LOG_INF("update_worker: download %u%%", (unsigned)pct);
            }
        }
    }

    return 0;
}

static void update_worker_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    printk("update_worker: started\n");

    dm_request_t req;
    static uint8_t recv_buf[1024];

    for (;;) {
        int rc = dm_update_req_receive(&req, K_FOREVER);
        if (rc != 0) {
            LOG_ERR("update_worker: receive rc=%d", rc);
            continue;
        }

        LOG_INF("update_worker: dequeued id=%d", (int)req.id);

        if (req.id != DM_REQ_UPDATE_START) {
            LOG_WRN("update_worker: unexpected req.id=%d", (int)req.id);
            continue;
        }

        const char *uri = req.p.update_start.uri;
        if (uri[0] == '\0') {
            LOG_WRN("update_worker: empty URI, skipping");
            dm_update_set_state(DM_UPDATE_STATE_FAILED, -EINVAL, 0);
            continue;
        }

        LOG_INF("update_worker: downloading from %s", uri);

        /* -----------------------------
         * Open slot1 for writing
         * ----------------------------- */
        dm_update_set_state(DM_UPDATE_STATE_DOWNLOADING, 0, 0);

        const struct flash_area *fa = NULL;
        rc = flash_area_open(PARTITION_ID(slot1_partition), &fa);
        if (rc != 0) {
            LOG_ERR("update_worker: flash_area_open slot1 rc=%d", rc);
            dm_update_set_state(DM_UPDATE_STATE_FAILED, rc, 0);
            continue;
        }

        rc = flash_area_erase(fa, 0, fa->fa_size);
        if (rc != 0) {
            LOG_ERR("update_worker: flash_area_erase rc=%d", rc);
            flash_area_close(fa);
            dm_update_set_state(DM_UPDATE_STATE_FAILED, rc, 0);
            continue;
        }

        /* -----------------------------
         * Parse URI: extract host and path
         * ----------------------------- */
        const char *host_begin = NULL;
        const char *path_begin = NULL;
        char host[64] = {0};

        if (strncmp(uri, "http://", 7) == 0) {
            host_begin = uri + 7;
        } else {
            host_begin = uri;
        }

        if (host_begin == NULL || host_begin[0] == '\0') {
            flash_area_close(fa);
            dm_update_set_state(DM_UPDATE_STATE_FAILED, -EINVAL, 0);
            continue;
        }

        path_begin = strchr(host_begin, '/');
        size_t host_len;
        if (path_begin != NULL) {
            host_len = (size_t)(path_begin - host_begin);
        } else {
            host_len = strlen(host_begin);
        }
        if (host_len > sizeof(host) - 1) {
            host_len = sizeof(host) - 1;
        }
        memcpy(host, host_begin, host_len);
        host[host_len] = '\0';

        const char *path = (path_begin != NULL) ? path_begin : "/";

        LOG_INF("update_worker: host=%s path=%s", host, path);

        /* -----------------------------
         * HTTP GET request with streaming callback
         * ----------------------------- */
        struct download_ctx dl_ctx = {
            .fa = fa,
            .offset = 0,
            .last_progress = 0,
        };

        struct http_request http_req;
        memset(&http_req, 0, sizeof(http_req));

        http_req.method = HTTP_GET;
        http_req.url = path;
        http_req.host = host;
        http_req.protocol = "HTTP/1.1";
        http_req.recv_buf = recv_buf;
        http_req.recv_buf_len = sizeof(recv_buf);
        http_req.response = download_response_cb;

        int sock = -1;
        rc = http_client_req(-1, &http_req, 30000, &dl_ctx);
        if (rc < 0 && rc != -ECONNRESET) {
            /* ECONNRESET is expected when server closes after sending all data */
            LOG_ERR("update_worker: http_client_req rc=%d", rc);
            flash_area_close(fa);
            dm_update_set_state(DM_UPDATE_STATE_FAILED, rc, 0);
            continue;
        }

        uint32_t total_bytes = dl_ctx.offset;
        flash_area_close(fa);

        if (total_bytes == 0) {
            LOG_ERR("update_worker: downloaded 0 bytes");
            dm_update_set_state(DM_UPDATE_STATE_FAILED, -ENODATA, 0);
            continue;
        }

        LOG_INF("update_worker: downloaded %u bytes", (unsigned)total_bytes);

        /* -----------------------------
         * Verify MCUboot header
         * ----------------------------- */
        dm_update_set_state(DM_UPDATE_STATE_VERIFYING, 0, 100);

        rc = flash_area_open(PARTITION_ID(slot1_partition), &fa);
        if (rc != 0) {
            dm_update_set_state(DM_UPDATE_STATE_FAILED, rc, 0);
            continue;
        }

        struct mcuboot_image_header hdr;
        rc = flash_area_read(fa, 0, &hdr, sizeof(hdr));
        flash_area_close(fa);

        if (rc != 0) {
            LOG_ERR("update_worker: flash_area_read header rc=%d", rc);
            dm_update_set_state(DM_UPDATE_STATE_FAILED, rc, 0);
            continue;
        }

        if (hdr.ih_magic != IMAGE_MAGIC) {
            LOG_ERR("update_worker: bad magic 0x%08x (expected 0x%08x)",
                    (unsigned)hdr.ih_magic, IMAGE_MAGIC);
            dm_update_set_state(DM_UPDATE_STATE_FAILED, -EBADMSG, 0);
            continue;
        }

        LOG_INF("update_worker: image verified v%u.%u.%u size=%u",
                hdr.ih_ver.major, hdr.ih_ver.minor,
                (unsigned)hdr.ih_ver.revision,
                (unsigned)hdr.ih_img_size);

        /* -----------------------------
         * Request upgrade and reboot
         * ----------------------------- */
        dm_update_set_state(DM_UPDATE_STATE_APPLYING, 0, 100);

        rc = boot_request_upgrade(false);
        if (rc != 0) {
            LOG_ERR("update_worker: boot_request_upgrade rc=%d", rc);
            dm_update_set_state(DM_UPDATE_STATE_FAILED, rc, 0);
            continue;
        }

        dm_update_set_state(DM_UPDATE_STATE_REBOOT_PENDING, 0, 100);
        LOG_INF("update_worker: rebooting...");
        k_sleep(K_MSEC(500));

        sys_reboot(SYS_REBOOT_COLD);

        /* Should not reach here */
        dm_update_set_state(DM_UPDATE_STATE_FAILED, -ETIMEDOUT, 0);
    }
}

#define DM_UPDATE_WORKER_STACK_SZ      4096
#define DM_UPDATE_WORKER_PRIO_PREEMPT  4

K_THREAD_STACK_DEFINE(g_dm_update_worker_stack, DM_UPDATE_WORKER_STACK_SZ);
static struct k_thread g_dm_update_worker_thread;

int dm_update_worker_start(void)
{
    k_tid_t tid = k_thread_create(&g_dm_update_worker_thread,
                      g_dm_update_worker_stack,
                      K_THREAD_STACK_SIZEOF(g_dm_update_worker_stack),
                      update_worker_thread,
                      NULL, NULL, NULL,
                      DM_UPDATE_WORKER_PRIO_PREEMPT, 0, K_NO_WAIT);
    k_thread_name_set(tid, "dm_update_worker");
    return 0;
}
