/*!
 * @file is25lp064.c
 * @brief IS25LP064 external QSPI NOR flash (64 Mbit / 8 MB) — storage
 */
/*----------------------------------------------------------------------------*/

/* C standard library */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* Zephyr */
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/sys/util.h>
#include <zephyr/storage/flash_map.h>

/* Application */
#include "is25lp064.h"

/* Devicetree wiring: IS25LP064 node from application/app.overlay.
 * Guarded so the module compiles even if the node is removed later. */
#if DT_NODE_EXISTS(DT_NODELABEL(is25lp064))
#define IS25LP064_DEV_AVAILABLE 1
#else
#define IS25LP064_DEV_AVAILABLE 0
#endif

/* Partition DT labels — must match app.overlay fixed-partitions order. */
#define IS25LP064_PART_NODE(id) DT_NODELABEL(id)
#define IS25LP064_PART_OFF(id)  DT_REG_ADDR(DT_NODELABEL(id))
#define IS25LP064_PART_SIZE(id) DT_REG_SIZE(DT_NODELABEL(id))

static const struct is25lp064_part_info {
	off_t  offset;   /* offset within flash */
	size_t size;     /* bytes */
} part_info[IS25LP064_PART_COUNT] = {
	[IS25LP064_PART_SLOT1]     = { IS25LP064_PART_OFF(qspi_slot1_partition),
				      IS25LP064_PART_SIZE(qspi_slot1_partition) },
	[IS25LP064_PART_SLAVE_FW] = { IS25LP064_PART_OFF(slave_fw_staging),
				      IS25LP064_PART_SIZE(slave_fw_staging) },
	[IS25LP064_PART_STORAGE]  = { IS25LP064_PART_OFF(storage_partition),
				      IS25LP064_PART_SIZE(storage_partition) },
	[IS25LP064_PART_RESERVED] = { IS25LP064_PART_OFF(reserved_partition),
				      IS25LP064_PART_SIZE(reserved_partition) },
};

static struct {
	const struct device *dev;
	size_t page_size;
} is25lp064;

/*----------------------------------------------------------------------------*/
/*! @brief Initialize: resolve device + cache geometry (idempotent) */
/*----------------------------------------------------------------------------*/
int is25lp064Init(void)
{
#if IS25LP064_DEV_AVAILABLE
	const struct flash_parameters *fp;

	if (is25lp064.dev != NULL) {
		return IS25LP064_OK;
	}

	is25lp064.dev = DEVICE_DT_GET(DT_NODELABEL(is25lp064));
	if (!device_is_ready(is25lp064.dev)) {
		is25lp064.dev = NULL;
		return IS25LP064_ERR_DEVICE;
	}

	fp = flash_get_parameters(is25lp064.dev);
	if (fp == NULL) {
		return IS25LP064_ERR_INFO;
	}
	is25lp064.page_size = fp->write_block_size;

	return IS25LP064_OK;
#else
	return IS25LP064_ERR_DEVICE;
#endif
}

/*----------------------------------------------------------------------------*/
/*! @brief Validate a partition + access range */
/*----------------------------------------------------------------------------*/
static int is25lp064PartRange(enum is25lp064_partition part, off_t offset,
			      size_t len, off_t *abs_off)
{
	if (part < 0 || part >= IS25LP064_PART_COUNT) {
		return IS25LP064_ERR_ARG;
	}
	if (offset < 0 || len > part_info[part].size ||
	    (size_t)offset > part_info[part].size - len) {
		return IS25LP064_ERR_ARG;
	}
	*abs_off = part_info[part].offset + offset;
	return IS25LP064_OK;
}

/*----------------------------------------------------------------------------*/
/*! @brief Read from a partition (offset relative to partition start) */
/*----------------------------------------------------------------------------*/
int is25lp064Read(enum is25lp064_partition part, off_t offset,
		  void *buf, size_t len)
{
#if IS25LP064_DEV_AVAILABLE
	off_t abs_off;
	int err;

	if (buf == NULL && len != 0) {
		return IS25LP064_ERR_ARG;
	}
	if (is25lp064.dev == NULL) {
		return IS25LP064_ERR_DEVICE;
	}
	err = is25lp064PartRange(part, offset, len, &abs_off);
	if (err != 0) {
		return err;
	}

	err = flash_read(is25lp064.dev, abs_off, buf, len);
	return (err == 0) ? IS25LP064_OK : IS25LP064_ERR_READ;
#else
	return IS25LP064_ERR_DEVICE;
#endif
}

/*----------------------------------------------------------------------------*/
/*! @brief Write to a partition (caller erases first) */
/*----------------------------------------------------------------------------*/
int is25lp064Write(enum is25lp064_partition part, off_t offset,
		   const void *buf, size_t len)
{
#if IS25LP064_DEV_AVAILABLE
	off_t abs_off;
	int err;

	if (buf == NULL && len != 0) {
		return IS25LP064_ERR_ARG;
	}
	if (is25lp064.dev == NULL) {
		return IS25LP064_ERR_DEVICE;
	}
	err = is25lp064PartRange(part, offset, len, &abs_off);
	if (err != 0) {
		return err;
	}

	err = flash_write(is25lp064.dev, abs_off, buf, len);
	return (err == 0) ? IS25LP064_OK : IS25LP064_ERR_WRITE;
#else
	return IS25LP064_ERR_DEVICE;
#endif
}

/*----------------------------------------------------------------------------*/
/*! @brief Erase an entire partition */
/*----------------------------------------------------------------------------*/
int is25lp064Erase(enum is25lp064_partition part)
{
	return is25lp064EraseRegion(part, 0, part_info[part].size);
}

/*----------------------------------------------------------------------------*/
/*! @brief Erase a region inside a partition (offset/len page-aligned) */
/*----------------------------------------------------------------------------*/
int is25lp064EraseRegion(enum is25lp064_partition part, off_t offset, size_t len)
{
#if IS25LP064_DEV_AVAILABLE
	off_t abs_off;
	int err;

	if (is25lp064.dev == NULL) {
		return IS25LP064_ERR_DEVICE;
	}
	if (is25lp064.page_size == 0) {
		return IS25LP064_ERR_INFO;
	}

	err = is25lp064PartRange(part, offset, len, &abs_off);
	if (err != 0) {
		return err;
	}
	if (offset % is25lp064.page_size != 0 ||
	    len % is25lp064.page_size != 0) {
		return IS25LP064_ERR_ARG;
	}

	err = flash_erase(is25lp064.dev, abs_off, len);
	return (err == 0) ? IS25LP064_OK : IS25LP064_ERR_ERASE;
#else
	return IS25LP064_ERR_DEVICE;
#endif
}

/*----------------------------------------------------------------------------*/
/*! @brief True if the device is present and ready */
/*----------------------------------------------------------------------------*/
bool is25lp064IsValid(void)
{
	return (is25lp064.dev != NULL);
}

/*----------------------------------------------------------------------------*/
/*! @brief Flash device */
/*----------------------------------------------------------------------------*/
const struct device *is25lp064GetDevice(void)
{
	return is25lp064.dev;
}

/*----------------------------------------------------------------------------*/
/*! @brief Partition offset within flash */
/*----------------------------------------------------------------------------*/
off_t is25lp064PartitionOffset(enum is25lp064_partition part)
{
	if (part < 0 || part >= IS25LP064_PART_COUNT) {
		return -1;
	}
	return part_info[part].offset;
}

/*----------------------------------------------------------------------------*/
/*! @brief Partition size */
/*----------------------------------------------------------------------------*/
size_t is25lp064PartitionSize(enum is25lp064_partition part)
{
	if (part < 0 || part >= IS25LP064_PART_COUNT) {
		return 0;
	}
	return part_info[part].size;
}

/*----------------------------------------------------------------------------*/
/*! @brief Flash write/erase page size */
/*----------------------------------------------------------------------------*/
size_t is25lp064PageSize(void)
{
	return is25lp064.page_size;
}
