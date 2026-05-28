#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include <stdint.h>

/* OTA update states */
enum ota_state {
	OTA_IDLE = 0,
	OTA_DOWNLOADING,
	OTA_VERIFYING,
	OTA_APPLYING,
	OTA_REBOOT_PENDING,
	OTA_FAILED,
};

/* Start firmware download from URI and write to slot1 */
int ota_update_start(const char *uri);

/* Get current OTA state, error code, and progress percentage */
enum ota_state ota_get_state(void);
int ota_get_error(void);
uint8_t ota_get_progress(void);

#endif /* OTA_UPDATE_H */
