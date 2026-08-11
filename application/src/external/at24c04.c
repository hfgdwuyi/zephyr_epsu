/*!
 * @file at24c04.c
 * @brief 24C04 I2C EEPROM (512 B) — MFD asset information storage
 */
/*----------------------------------------------------------------------------*/

/* C standard library */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Zephyr */
#include <zephyr/device.h>
#include <zephyr/drivers/eeprom.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/util.h>

/* Application */
#include "at24c04.h"

/* Devicetree wiring: 24C04 node from application/app.overlay.
 * Guarded so the module compiles even if the node is removed later. */
#if DT_NODE_EXISTS(DT_NODELABEL(identity_eeprom))
#define MFD_ASSET_DEV_AVAILABLE 1
#else
#define MFD_ASSET_DEV_AVAILABLE 0
#endif

#if MFD_ASSET_DEV_AVAILABLE
BUILD_ASSERT(MFD_ASSET_SIZE == 66u, "mfd_asset_t layout changed; update this assert");
BUILD_ASSERT(MFD_ASSET_SIZE >= 4u, "mfd_asset_t too small");
#endif

/* ---- CRC helpers ---- */

static uint16_t mfdAssetCrc(const mfd_asset_t *asset)
{
	/* CRC covers every byte up to (but not including) the crc field. */
	return crc16_ccitt(0xFFFF, (const uint8_t *)asset, MFD_ASSET_SIZE - sizeof(uint16_t));
}

/* ---- Device access ---- */

#if MFD_ASSET_DEV_AVAILABLE
static const struct device *mfdAssetGetDev(void)
{
	static const struct device *dev;

	if (dev == NULL) {
		dev = DEVICE_DT_GET(DT_NODELABEL(identity_eeprom));
	}

	return dev;
}
#endif /* MFD_ASSET_DEV_AVAILABLE */

/*----------------------------------------------------------------------------*/
/*! @brief Initialize: resolve the EEPROM device (idempotent) */
/*----------------------------------------------------------------------------*/
int mfdAssetInit(void)
{
#if MFD_ASSET_DEV_AVAILABLE
	const struct device *dev = mfdAssetGetDev();

	if (dev == NULL || !device_is_ready(dev)) {
		return MFD_ASSET_ERR_DEVICE;
	}

	return MFD_ASSET_OK;
#else
	return MFD_ASSET_ERR_DEVICE;
#endif
}

/*----------------------------------------------------------------------------*/
/*! @brief Read + CRC-verify the record */
/*----------------------------------------------------------------------------*/
int mfdAssetLoad(mfd_asset_t *asset)
{
#if MFD_ASSET_DEV_AVAILABLE
	const struct device *dev = mfdAssetGetDev();
	uint16_t stored_crc;
	int err;

	if (asset == NULL) {
		return MFD_ASSET_ERR_RW;
	}
	if (dev == NULL || !device_is_ready(dev)) {
		return MFD_ASSET_ERR_DEVICE;
	}
	if (eeprom_get_size(dev) < MFD_ASSET_SIZE) {
		return MFD_ASSET_ERR_SIZE;
	}

	err = eeprom_read(dev, MFD_ASSET_OFFSET, asset, MFD_ASSET_SIZE);
	if (err != 0) {
		return MFD_ASSET_ERR_RW;
	}

	if (asset->magic != MFD_ASSET_MAGIC) {
		return MFD_ASSET_ERR_BAD_MAGIC;
	}
	if (asset->version != MFD_ASSET_VERSION) {
		return MFD_ASSET_ERR_VERSION;
	}

	/* Verify checksum before trusting any field. */
	stored_crc = asset->crc;
	asset->crc = 0;
	if (stored_crc != mfdAssetCrc(asset)) {
		asset->crc = stored_crc;   /* leave caller's struct intact */
		return MFD_ASSET_ERR_CRC;
	}

	return MFD_ASSET_OK;
#else
	return MFD_ASSET_ERR_DEVICE;
#endif
}

/*----------------------------------------------------------------------------*/
/*! @brief Write the record (blanks area with 0xFF, then stores struct) */
/*----------------------------------------------------------------------------*/
int mfdAssetSave(const mfd_asset_t *asset)
{
#if MFD_ASSET_DEV_AVAILABLE
	const struct device *dev = mfdAssetGetDev();
	mfd_asset_t buf;
	uint8_t blank[MFD_ASSET_SIZE];
	int err;

	if (asset == NULL) {
		return MFD_ASSET_ERR_RW;
	}
	if (dev == NULL || !device_is_ready(dev)) {
		return MFD_ASSET_ERR_DEVICE;
	}
	if (eeprom_get_size(dev) < MFD_ASSET_SIZE) {
		return MFD_ASSET_ERR_SIZE;
	}

	memcpy(&buf, asset, sizeof(buf));
	buf.magic   = MFD_ASSET_MAGIC;
	buf.version = MFD_ASSET_VERSION;
	buf.crc     = 0;
	buf.crc     = mfdAssetCrc(&buf);

	/* 24C04 bits can only go 1->0: blank the area so a shorter/moved
	 * record never leaves stale bits behind. */
	memset(blank, 0xFF, sizeof(blank));
	err = eeprom_write(dev, MFD_ASSET_OFFSET, blank, sizeof(blank));
	if (err != 0) {
		return MFD_ASSET_ERR_RW;
	}

	err = eeprom_write(dev, MFD_ASSET_OFFSET, &buf, sizeof(buf));
	if (err != 0) {
		return MFD_ASSET_ERR_RW;
	}

	return MFD_ASSET_OK;
#else
	return MFD_ASSET_ERR_DEVICE;
#endif
}

/*----------------------------------------------------------------------------*/
/*! @brief Blank the record area (0xFF) */
/*----------------------------------------------------------------------------*/
int mfdAssetClear(void)
{
#if MFD_ASSET_DEV_AVAILABLE
	const struct device *dev = mfdAssetGetDev();
	uint8_t blank[MFD_ASSET_SIZE];
	int err;

	if (dev == NULL || !device_is_ready(dev)) {
		return MFD_ASSET_ERR_DEVICE;
	}
	if (eeprom_get_size(dev) < MFD_ASSET_SIZE) {
		return MFD_ASSET_ERR_SIZE;
	}

	memset(blank, 0xFF, sizeof(blank));
	err = eeprom_write(dev, MFD_ASSET_OFFSET, blank, sizeof(blank));
	if (err != 0) {
		return MFD_ASSET_ERR_RW;
	}

	return MFD_ASSET_OK;
#else
	return MFD_ASSET_ERR_DEVICE;
#endif
}

/*----------------------------------------------------------------------------*/
/*! @brief Quick validity check */
/*----------------------------------------------------------------------------*/
bool mfdAssetIsValid(void)
{
	mfd_asset_t asset;

	return (mfdAssetLoad(&asset) == MFD_ASSET_OK);
}

/*----------------------------------------------------------------------------*/
/*! @brief Underlying Zephyr EEPROM device */
/*----------------------------------------------------------------------------*/
const struct device *mfdAssetGetDevice(void)
{
#if MFD_ASSET_DEV_AVAILABLE
	return mfdAssetGetDev();
#else
	return NULL;
#endif
}
