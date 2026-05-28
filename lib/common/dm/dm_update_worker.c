/*
 * dm_update_worker.c
 *
 * Update worker thread — consumes dm_update_req_q and performs firmware
 * download, verification, and MCUboot swap requests.
 *
 * SWAP_USING_OFFSET: first sector (128KB) of slot1 is swap scratch.
 * Image payload is written at slot1 offset 0x20000. STM32H7 requires
 * 32-byte flash write alignment.
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

#define IMAGE_MAGIC   0x96f3b83d
#define IMAGE_OFFSET  0x20000   /* skip swap scratch in slot1 */
#define SECTOR_SZ     0x20000   /* 128KB flash sector */
#define FLASH_ALIGN   32        /* STM32H7 minimum write alignment */

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

/* Per-download state passed to HTTP response callback */
struct download_ctx {
    const struct flash_area *fa;
    uint32_t offset;        /* absolute offset within slot1 */
    uint32_t max_offset;    /* slot1 end minus scratch area */
    uint32_t last_progress;
    uint8_t  buf[FLASH_ALIGN];
    uint8_t  buf_pos;
};

static int download_response_cb(struct http_response *rsp,
                                enum http_final_call final_data,
                                void *user_data)
{
    struct download_ctx *ctx = (struct download_ctx *)user_data;

    if (ctx == NULL || rsp == NULL) {
        return -EINVAL;
    }

    if (rsp->body_frag_len == 0 || rsp->body_frag_start == NULL) {
        return 0;
    }

    const uint8_t *src = rsp->body_frag_start;
    uint32_t remaining = (uint32_t)rsp->body_frag_len;

    while (remaining > 0 && ctx->offset < ctx->max_offset) {
        uint32_t space_until_max = ctx->max_offset - ctx->offset;
        uint8_t space = FLASH_ALIGN - ctx->buf_pos;
        uint32_t copy = (remaining < space) ? remaining : space;
        if (copy > space_until_max) {
            copy = space_until_max;
        }

        memcpy(ctx->buf + ctx->buf_pos, src, copy);
        ctx->buf_pos += (uint8_t)copy;
        src += copy;
        remaining -= copy;

        if (ctx->buf_pos == FLASH_ALIGN) {
            int rc = flash_area_write(ctx->fa, ctx->offset,
                                      ctx->buf, FLASH_ALIGN);
            if (rc != 0) {
                LOG_ERR("update_worker: flash_write at 0x%x rc=%d",
                        (unsigned)ctx->offset, rc);
                return rc;
            }
            ctx->offset += FLASH_ALIGN;
            ctx->buf_pos = 0;
        }
    }

    /* Progress update every 10% */
    if (rsp->content_length > 0 && ctx->offset > ctx->last_progress) {
        uint32_t downloaded = ctx->offset - IMAGE_OFFSET;
        uint32_t total = (uint32_t)rsp->content_length;
        uint32_t pct = (downloaded * 100) / total;
        if (pct > ctx->last_progress && pct <= 100) {
            ctx->last_progress = pct;
            dm_update_set_state(DM_UPDATE_STATE_DOWNLOADING, 0, (uint8_t)pct);
            LOG_INF("update_worker: download %u%%", (unsigned)pct);
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

        /* Erase only the image area (skip scratch at IMAGE_OFFSET) */
        uint32_t image_size = fa->fa_size - SECTOR_SZ;
        rc = flash_area_erase(fa, IMAGE_OFFSET, image_size);
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
            .offset = IMAGE_OFFSET,
            .max_offset = fa->fa_size - SECTOR_SZ,
            .last_progress = 0,
            .buf_pos = 0,
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

        rc = http_client_req(-1, &http_req, 30000, &dl_ctx);
        if (rc < 0 && rc != -ECONNRESET) {
            LOG_ERR("update_worker: http_client_req rc=%d", rc);
            flash_area_close(fa);
            dm_update_set_state(DM_UPDATE_STATE_FAILED, rc, 0);
            continue;
        }

        /* Flush any remaining bytes in alignment buffer */
        if (dl_ctx.buf_pos > 0 && dl_ctx.offset < dl_ctx.max_offset) {
            memset(dl_ctx.buf + dl_ctx.buf_pos, 0xFF,
                   FLASH_ALIGN - dl_ctx.buf_pos);
            rc = flash_area_write(fa, dl_ctx.offset,
                                  dl_ctx.buf, FLASH_ALIGN);
            if (rc == 0) {
                dl_ctx.offset += FLASH_ALIGN;
            }
        }

        uint32_t total_bytes = dl_ctx.offset - IMAGE_OFFSET;
        flash_area_close(fa);

        if (total_bytes == 0) {
            LOG_ERR("update_worker: downloaded 0 bytes");
            dm_update_set_state(DM_UPDATE_STATE_FAILED, -ENODATA, 0);
            continue;
        }

        LOG_INF("update_worker: downloaded %u bytes", (unsigned)total_bytes);

        /* -----------------------------
         * Verify MCUboot header at IMAGE_OFFSET within slot1
         * ----------------------------- */
        dm_update_set_state(DM_UPDATE_STATE_VERIFYING, 0, 100);

        rc = flash_area_open(PARTITION_ID(slot1_partition), &fa);
        if (rc != 0) {
            dm_update_set_state(DM_UPDATE_STATE_FAILED, rc, 0);
            continue;
        }

        struct mcuboot_image_header hdr;
        rc = flash_area_read(fa, IMAGE_OFFSET, &hdr, sizeof(hdr));
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
