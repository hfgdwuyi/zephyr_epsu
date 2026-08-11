/*!
 * @file at24c04.h
 * @brief 24C04 I2C EEPROM (512 B) — MFD asset information storage
 *
 * Persists the Multi-Function Display (MFD) asset info on the on-board
 * 24C04 EEPROM (Zephyr `atmel,at24` device, DT label `identity_eeprom`).
 * A magic + version + CRC16-CCITT guard lets callers distinguish
 * "empty / never written", "foreign version" and "corrupted" records.
 *
 * Storage layout (offset 0, big-endian-safe fixed field offsets):
 *   +0  uint32_t magic                 "MFD\0"
 *   +4  uint8_t  version               structure version
 *   +5  uint8_t  hw_rev                hardware revision
 *   +6  uint8_t  fw_rev                firmware revision
 *   +7  uint8_t  reserved0
 *   +8  char[16] serial                serial number (NUL padded)
 *   +24 char[16] model                 model / part number (NUL padded)
 *   +40 char[8]  mfg_date              manufacture date "YYYYMMDD"
 *   +48 uint8_t[16] reserved           future expansion
 *   +64 uint16_t crc                   CRC16-CCITT(0xFFFF) of [0..63]
 *
 * NOTE: 24C04 is byte-programmable but bits can only go 1->0; therefore
 * Save() first blanks the record area with 0xFF, then writes the struct.
 */
/*----------------------------------------------------------------------------*/
#ifndef AT24C04_H
#define AT24C04_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

struct device;

#ifdef __cplusplus
extern "C" {
#endif

/* ---- MFD asset record ---- */

#define MFD_ASSET_MAGIC          0x4D464400u   /* "MFD\0" */
#define MFD_ASSET_VERSION        1u

#define MFD_ASSET_SERIAL_LEN     16u
#define MFD_ASSET_MODEL_LEN      16u
#define MFD_ASSET_DATE_LEN       8u            /* YYYYMMDD, no NUL */
#define MFD_ASSET_RESERVED_LEN   16u

typedef struct __attribute__((packed)) {
	uint32_t magic;                          /* MFD_ASSET_MAGIC */
	uint8_t  version;                        /* MFD_ASSET_VERSION */
	uint8_t  hw_rev;                         /* hardware revision */
	uint8_t  fw_rev;                         /* firmware revision */
	uint8_t  reserved0;
	char     serial[MFD_ASSET_SERIAL_LEN];   /* NUL-padded */
	char     model[MFD_ASSET_MODEL_LEN];     /* NUL-padded */
	char     mfg_date[MFD_ASSET_DATE_LEN];   /* "YYYYMMDD" */
	uint8_t  reserved[MFD_ASSET_RESERVED_LEN];
	uint16_t crc;                            /* CRC16-CCITT(0xFFFF) of [0..size-3] */
} mfd_asset_t;

#define MFD_ASSET_SIZE           (sizeof(mfd_asset_t))   /* 66 */
#define MFD_ASSET_OFFSET         0u                     /* record at EEPROM start */

/* ---- Return codes ---- */

enum mfd_asset_rc {
	MFD_ASSET_OK           =  0,
	MFD_ASSET_ERR_DEVICE   = -1,   /* EEPROM device missing / not ready */
	MFD_ASSET_ERR_BAD_MAGIC= -2,   /* area never written / cleared */
	MFD_ASSET_ERR_VERSION  = -3,   /* written by a different layout */
	MFD_ASSET_ERR_CRC      = -4,   /* corrupted record */
	MFD_ASSET_ERR_SIZE     = -5,   /* EEPROM too small for record */
	MFD_ASSET_ERR_RW       = -6,   /* I2C read/write failure */
};

/* ---- API ---- */

/*! Initialize: resolve the EEPROM device (idempotent). Returns MFD_ASSET_OK
 *  or MFD_ASSET_ERR_DEVICE if the node is absent / device not ready. */
int mfdAssetInit(void);

/*! Read + CRC-verify the record. Fills *asset only on MFD_ASSET_OK. */
int mfdAssetLoad(mfd_asset_t *asset);

/*! Fill the record header/checksum and write it to EEPROM.
 *  Caller sets serial/model/mfg_date/hw_rev/fw_rev; magic/version/crc are
 *  handled here. Returns MFD_ASSET_OK or a negative error code. */
int mfdAssetSave(const mfd_asset_t *asset);

/*! Blank the record area (0xFF) — subsequent Load() returns BAD_MAGIC. */
int mfdAssetClear(void);

/*! Quick validity check without exposing the record. */
bool mfdAssetIsValid(void);

/*! Underlying Zephyr EEPROM device, or NULL if not available. */
const struct device *mfdAssetGetDevice(void);

#ifdef __cplusplus
}
#endif

#endif /* AT24C04_H */
