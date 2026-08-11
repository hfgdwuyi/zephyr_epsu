/*!
 * @file is25lp064.h
 * @brief IS25LP064 external QSPI NOR flash (64 Mbit / 8 MB) — storage
 *
 * Wraps the Zephyr `st,stm32-qspi-nor` flash driver (DT label `is25lp064`).
 * Provides partitioned access over the Zephyr flash API, with the partition
 * layout defined by the `fixed-partitions` node in application/app.overlay.
 *
 * Firmware design partition map (offset within the QSPI flash, 8 MB total):
 *   [0x000000 .. 0x100000)  slot1      1 MB  — OTA image slot 1
 *   [0x100000 .. 0x500000)  slave-fw   4 MB  — slave PSU firmware staging
 *   [0x500000 .. 0x600000)  storage    1 MB  — general-purpose storage
 *   [0x600000 .. 0x800000)  reserved   2 MB  — reserved / future use
 *
 * NOTE: the NUCLEO-H745 dev board has no external QSPI device — the driver
 * probe fails at boot (SFDP invalid). is25lp064Init() then returns
 * IS25LP064_ERR_DEVICE and all other calls fail fast, which is expected.
 */
/*----------------------------------------------------------------------------*/
#ifndef IS25LP064_H
#define IS25LP064_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

struct device;

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Partitions (mirror of app.overlay fixed-partitions) ---- */

enum is25lp064_partition {
	IS25LP064_PART_SLOT1,       /* image-1    1 MB @ 0x000000 */
	IS25LP064_PART_SLAVE_FW,    /* slave-fw   4 MB @ 0x100000 */
	IS25LP064_PART_STORAGE,     /* storage    1 MB @ 0x500000 */
	IS25LP064_PART_RESERVED,    /* reserved   2 MB @ 0x600000 */
	IS25LP064_PART_COUNT,
};

/* ---- Return codes ---- */

enum is25lp064_rc {
	IS25LP064_OK          = 0,
	IS25LP064_ERR_DEVICE  = -1,   /* QSPI device missing / not ready */
	IS25LP064_ERR_ARG     = -2,   /* invalid partition / offset / len */
	IS25LP064_ERR_ERASE   = -3,   /* flash_erase failed */
	IS25LP064_ERR_WRITE   = -4,   /* flash_write failed */
	IS25LP064_ERR_READ    = -5,   /* flash_read failed */
	IS25LP064_ERR_INFO    = -6,   /* flash_get_geometry failed */
};

/* ---- API ---- */

/*! Initialize: resolve the QSPI device + cache geometry (idempotent). */
int is25lp064Init(void);

/*! Read from a partition. `offset` is relative to the partition start. */
int is25lp064Read(enum is25lp064_partition part, off_t offset,
		  void *buf, size_t len);

/*! Write to a partition (flash_write may impose alignment on `offset`/`len`).
 *  Caller must erase the target region first via is25lp064Erase(). */
int is25lp064Write(enum is25lp064_partition part, off_t offset,
		   const void *buf, size_t len);

/*! Erase an entire partition (sector-aligned to page size). */
int is25lp064Erase(enum is25lp064_partition part);

/*! Erase a region inside a partition (offset/len page-aligned). */
int is25lp064EraseRegion(enum is25lp064_partition part,
			 off_t offset, size_t len);

/*! True if the device is present and ready. */
bool is25lp064IsValid(void);

/*! Flash device, or NULL if not available. */
const struct device *is25lp064GetDevice(void);

/*! Partition offset (within flash) / size, or -1 / 0 on bad partition. */
off_t  is25lp064PartitionOffset(enum is25lp064_partition part);
size_t is25lp064PartitionSize(enum is25lp064_partition part);

/*! Flash write/erase page size (bytes), or 0 if unavailable. */
size_t is25lp064PageSize(void);

#ifdef __cplusplus
}
#endif

#endif /* IS25LP064_H */
