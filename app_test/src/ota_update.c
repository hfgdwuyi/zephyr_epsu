#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/http/client.h>
#include <zephyr/sys/reboot.h>

#include <errno.h>
#include <string.h>

#include "ota_update.h"

/*
 * SWAP_USING_OFFSET: the first sector (128KB) of the secondary slot is
 * reserved for swap operations. The image must start at offset 0x20000.
 */
#define IMAGE_OFFSET  0x20000
#define SECTOR_SZ     0x20000

/* MCUboot image header magic */
#define IMAGE_MAGIC 0x96f3b83d

/* MCUboot image header (first 32 bytes) */
struct __attribute__((packed)) mcuboot_hdr {
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

static enum ota_state state = OTA_IDLE;
static int ota_error;
static uint8_t ota_progress;

enum ota_state ota_get_state(void) { return state; }
int ota_get_error(void) { return ota_error; }
uint8_t ota_get_progress(void) { return ota_progress; }

/* Flash write block size for STM32H7 (256-bit / 32-byte alignment) */
#define FLASH_ALIGN 32

/* Per-download context */
struct dl_ctx {
	const struct flash_area *fa;
	uint32_t offset;
	uint32_t max_offset;
	uint8_t  buf[FLASH_ALIGN];
	uint8_t  buf_pos;
};

static int dl_flush(struct dl_ctx *ctx, bool final)
{
	if (ctx->buf_pos == 0) {
		return 0;
	}

	uint8_t aligned[FLASH_ALIGN];
	memcpy(aligned, ctx->buf, ctx->buf_pos);
	memset(aligned + ctx->buf_pos, 0xFF, FLASH_ALIGN - ctx->buf_pos);

	uint32_t wr_addr = ctx->offset - ctx->buf_pos;
	wr_addr &= ~(FLASH_ALIGN - 1);

	int rc = flash_area_write(ctx->fa, wr_addr, aligned, FLASH_ALIGN);
	if (rc != 0) {
		printk("OTA: flash flush err %d at 0x%x\n", rc, wr_addr);
		return rc;
	}

	if (final) {
		ctx->offset = wr_addr + ctx->buf_pos;
	}
	ctx->buf_pos = 0;
	return 0;
}

static int download_cb(struct http_response *rsp, enum http_final_call final,
		       void *user_data)
{
	struct dl_ctx *ctx = (struct dl_ctx *)user_data;

	if (ctx == NULL || rsp == NULL) {
		return -EINVAL;
	}

	if (rsp->body_frag_len > 0 && rsp->body_frag_start != NULL) {
		const uint8_t *data = rsp->body_frag_start;
		uint32_t remaining = (uint32_t)rsp->body_frag_len;

		while (remaining > 0 && ctx->offset < ctx->max_offset) {
			uint32_t space_until_max = ctx->max_offset - ctx->offset;
			uint8_t space = FLASH_ALIGN - ctx->buf_pos;
			uint32_t copy = (remaining < space) ? remaining : space;
			if (copy > space_until_max) {
				copy = space_until_max;
			}
			memcpy(ctx->buf + ctx->buf_pos, data, copy);
			ctx->buf_pos += (uint8_t)copy;
			ctx->offset += copy;
			data += copy;
			remaining -= copy;

			if (ctx->buf_pos == FLASH_ALIGN) {
				uint32_t wr_addr = ctx->offset - FLASH_ALIGN;
				int rc = flash_area_write(ctx->fa, wr_addr,
							  ctx->buf, FLASH_ALIGN);
				if (rc != 0) {
					printk("OTA: flash write err %d at 0x%x\n",
					       rc, wr_addr);
					return rc;
				}
				ctx->buf_pos = 0;
			}
		}

		if (ctx->offset >= ctx->max_offset) {
			printk("OTA: max_offset reached, truncating\n");
		}

		if (rsp->content_length > 0) {
			uint32_t downloaded = ctx->offset - IMAGE_OFFSET;
			ota_progress = (uint8_t)((downloaded * 100) /
						(uint32_t)rsp->content_length);
			if (ota_progress % 20 == 0) {
				printk("OTA: download %u%%\n", ota_progress);
			}
		}
	}

	if (final == HTTP_DATA_FINAL) {
		int rc = dl_flush(ctx, true);
		if (rc != 0) {
			return rc;
		}
	}

	return 0;
}

int ota_update_start(const char *uri)
{
	if (state == OTA_DOWNLOADING) {
		return -EBUSY;
	}

	if (uri == NULL || uri[0] == '\0') {
		state = OTA_FAILED;
		ota_error = -EINVAL;
		return -EINVAL;
	}

	printk("OTA: starting download from %s\n", uri);

	state = OTA_DOWNLOADING;
	ota_progress = 0;
	ota_error = 0;

	const struct flash_area *fa = NULL;
	int rc = flash_area_open(PARTITION_ID(slot1_partition), &fa);
	if (rc != 0) {
		printk("OTA: flash_area_open slot1 err %d\n", rc);
		goto fail;
	}

	rc = flash_area_erase(fa, 0, fa->fa_size);
	if (rc != 0) {
		printk("OTA: flash_area_erase err %d\n", rc);
		flash_area_close(fa);
		goto fail;
	}

	printk("OTA: image starts at slot offset 0x%x, slot size 0x%x\n",
	       IMAGE_OFFSET, fa->fa_size);

	/* ---------- Parse URI ---------- */
	const char *host_start = NULL;
	if (strncmp(uri, "http://", 7) == 0) {
		host_start = uri + 7;
	} else {
		host_start = uri;
	}

	char host[64];
	const char *path_start = strchr(host_start, '/');
	size_t host_len;
	if (path_start != NULL) {
		host_len = (size_t)(path_start - host_start);
	} else {
		host_len = strlen(host_start);
	}
	if (host_len > sizeof(host) - 1) {
		host_len = sizeof(host) - 1;
	}
	memcpy(host, host_start, host_len);
	host[host_len] = '\0';

	const char *path = (path_start != NULL) ? path_start : "/";

	char port_str[8] = "80";
	const char *colon = strchr(host, ':');
	if (colon != NULL) {
		size_t port_len = host_len - (size_t)(colon - host) - 1;
		if (port_len > 0 && port_len < sizeof(port_str)) {
			memcpy(port_str, colon + 1, port_len);
			port_str[port_len] = '\0';
		}
		host[colon - host] = '\0';
	}

	printk("OTA: host=%s port=%s path=%s\n", host, port_str, path);

	/* ---------- Connect ---------- */
	struct zsock_addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
	};
	struct zsock_addrinfo *res = NULL;

	rc = zsock_getaddrinfo(host, port_str, &hints, &res);
	if (rc != 0 || res == NULL) {
		printk("OTA: getaddrinfo err %d\n", rc);
		flash_area_close(fa);
		goto fail;
	}

	int sock = zsock_socket(res->ai_family, res->ai_socktype,
				res->ai_protocol);
	if (sock < 0) {
		printk("OTA: socket err %d\n", sock);
		zsock_freeaddrinfo(res);
		flash_area_close(fa);
		goto fail;
	}

	rc = zsock_connect(sock, res->ai_addr, res->ai_addrlen);
	zsock_freeaddrinfo(res);
	if (rc < 0) {
		printk("OTA: connect err %d\n", rc);
		zsock_close(sock);
		flash_area_close(fa);
		goto fail;
	}

	printk("OTA: connected to %s:%s\n", host, port_str);

	/* ---------- HTTP GET ---------- */
	struct dl_ctx dl_ctx = {
		.fa = fa,
		.offset = IMAGE_OFFSET,
		.max_offset = fa->fa_size - SECTOR_SZ,
		.buf_pos = 0,
	};

	static uint8_t recv_buf[4096];
	struct http_request req;
	memset(&req, 0, sizeof(req));
	req.method = HTTP_GET;
	req.url = path;
	req.host = host;
	req.port = port_str;
	req.protocol = "HTTP/1.1";
	req.recv_buf = recv_buf;
	req.recv_buf_len = sizeof(recv_buf);
	req.response = download_cb;

	rc = http_client_req(sock, &req, 30000, &dl_ctx);
	zsock_close(sock);
	if (rc < 0 && rc != -ECONNRESET) {
		printk("OTA: http_client_req err %d\n", rc);
		flash_area_close(fa);
		goto fail;
	}
	uint32_t total = dl_ctx.offset;
	flash_area_close(fa);

	if (total == IMAGE_OFFSET) {
		printk("OTA: downloaded 0 bytes\n");
		ota_error = -ENODATA;
		goto fail;
	}

	printk("OTA: downloaded %u bytes (image %u bytes)\n",
	       total, total - IMAGE_OFFSET);

	/* ---------- Verify MCUboot header ---------- */
	state = OTA_VERIFYING;
	rc = flash_area_open(PARTITION_ID(slot1_partition), &fa);
	if (rc != 0) {
		printk("OTA: re-open slot1 for verify err %d\n", rc);
		goto fail;
	}

	struct mcuboot_hdr hdr;
	rc = flash_area_read(fa, IMAGE_OFFSET, &hdr, sizeof(hdr));
	flash_area_close(fa);

	if (rc != 0) {
		printk("OTA: read header err %d\n", rc);
		goto fail;
	}

	if (hdr.ih_magic != IMAGE_MAGIC) {
		printk("OTA: bad magic 0x%08x (expected 0x%08x)\n",
		       hdr.ih_magic, IMAGE_MAGIC);
		ota_error = -EBADMSG;
		goto fail;
	}

	printk("OTA: image v%u.%u.%u size=%u\n",
	       hdr.ih_ver.major, hdr.ih_ver.minor,
	       hdr.ih_ver.revision, hdr.ih_img_size);

	/* ---------- Write MCUboot trailer ----------
	 * The signed image may be padded to fill the entire slot, which
	 * overwrites the trailer area with non-0xFF data. Re-erase the
	 * last 128KB sector before writing trailer.
	 *
	 * Trailer layout (SWAP_USING_OFFSET, 32-byte alignment):
	 *   fa_size - 16:        magic (16 bytes)
	 *   ALIGN_DOWN(magic-32): image_ok
	 *   image_ok - 32:       copy_done
	 *   copy_done - 32:      swap_info
	 */
	state = OTA_APPLYING;

	#define BOOT_MAGIC_SZ       16
	#define BOOT_MAX_ALIGN      32
	#define BOOT_SWAP_TYPE_TEST 0x01

	/* MCUboot trailer magic for BOOT_MAX_ALIGN=32.
	 * struct { uint16_t align; uint8_t magic[14]; } = {32, {0x2d,0xe1,...}}
	 * On little-endian ARM: 0x20,0x00 then 14 magic bytes.
	 */
	static const uint8_t boot_img_magic[16] = {
		0x20, 0x00, 0x2d, 0xe1, 0x5d, 0x29, 0x41, 0x0b,
		0x8d, 0x77, 0x67, 0x9c, 0x11, 0x0f, 0x1f, 0x8a
	};

	rc = flash_area_open(PARTITION_ID(slot1_partition), &fa);
	if (rc != 0) {
		printk("OTA: open slot1 for trailer err %d\n", rc);
		goto fail;
	}

	uint32_t fa_sz = fa->fa_size;

	/* Erase last 128KB sector to clear overwritten trailer area */
	uint32_t trailer_erase_off = fa_sz - SECTOR_SZ;
	rc = flash_area_erase(fa, trailer_erase_off, SECTOR_SZ);
	if (rc != 0) {
		printk("OTA: trailer sector erase err %d at 0x%x\n",
		       rc, trailer_erase_off);
		flash_area_close(fa);
		goto fail;
	}

	uint32_t magic_off = fa_sz - BOOT_MAGIC_SZ;
	uint32_t swap_info_off = ((magic_off - BOOT_MAX_ALIGN) / BOOT_MAX_ALIGN)
				 * BOOT_MAX_ALIGN; /* image_ok */
	swap_info_off -= BOOT_MAX_ALIGN; /* copy_done */
	swap_info_off -= BOOT_MAX_ALIGN; /* swap_info */
	uint32_t magic_pad_off = (magic_off / BOOT_MAX_ALIGN) * BOOT_MAX_ALIGN;

	printk("OTA: trailer magic@0x%x swap@0x%x\n", magic_off, swap_info_off);

	/* Write magic (32-byte aligned block, magic at end) */
	uint8_t magic_buf[32];
	memset(magic_buf, 0xFF, sizeof(magic_buf));
	memcpy(magic_buf + (magic_off - magic_pad_off), boot_img_magic, 16);

	rc = flash_area_write(fa, magic_pad_off, magic_buf, 32);
	if (rc != 0) {
		printk("OTA: write magic err %d at 0x%x\n", rc, magic_pad_off);
		flash_area_close(fa);
		goto fail;
	}

	/* Write swap_info = TEST (1 byte padded to 32) */
	uint8_t swap_buf[32];
	memset(swap_buf, 0xFF, sizeof(swap_buf));
	swap_buf[0] = BOOT_SWAP_TYPE_TEST;

	rc = flash_area_write(fa, swap_info_off, swap_buf, 32);
	if (rc != 0) {
		printk("OTA: write swap_info err %d at 0x%x\n", rc, swap_info_off);
		flash_area_close(fa);
		goto fail;
	}

	/* Write swap_size = 0 to ensure swap starts from offset 0.
	 * swap_size_off = swap_info_off - 2 * BOOT_MAX_ALIGN (SWAP_USING_OFFSET)
	 */
	uint32_t swap_size_off = swap_info_off - 2 * BOOT_MAX_ALIGN;
	uint8_t sz_buf[32];
	memset(sz_buf, 0xFF, sizeof(sz_buf));
	uint32_t zero = 0;
	memcpy(sz_buf, &zero, sizeof(zero));

	rc = flash_area_write(fa, swap_size_off, sz_buf, 32);
	flash_area_close(fa);
	if (rc != 0) {
		printk("OTA: write swap_size err %d at 0x%x\n", rc, swap_size_off);
		goto fail;
	}

	printk("OTA: trailer written\n");

	/* ---------- Reboot ---------- */
	state = OTA_REBOOT_PENDING;
	printk("OTA: upgrade requested, rebooting in 2s...\n");
	k_sleep(K_SECONDS(2));
	sys_reboot(SYS_REBOOT_COLD);

	return 0;

fail:
	state = OTA_FAILED;
	if (ota_error == 0) {
		ota_error = rc;
	}
	return ota_error;
}
