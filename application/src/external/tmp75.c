/*!
 * @file tmp75.c
 * @brief TMP75 I2C temperature sensor driver (see tmp75.h for hardware)
 */
/*----------------------------------------------------------------------------*/

/* Standard library */
#include <errno.h>

/* Zephyr */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/printk.h>

/* Application */
#include "tmp75.h"

/* TMP75 on I2C1 (PB8/PB9), shared with the identity EEPROM */
#define TMP75_BUS_NODE  DT_NODELABEL(i2c1)

/* I2C transfer timeout (ms) so a stuck bus does not block the thread */
#define TMP75_XFER_TIMEOUT_MS  50

static const struct device *tmp75_bus;
static bool     tmp75_ready;
static uint32_t tmp75_errors;

int tmp75Init(void)
{
#if DT_NODE_HAS_STATUS(TMP75_BUS_NODE, okay)
	tmp75_bus = DEVICE_DT_GET(TMP75_BUS_NODE);
#else
	tmp75_bus = NULL;
#endif

	if (tmp75_bus == NULL || !device_is_ready(tmp75_bus)) {
		printk("TMP75: I2C1 not ready\n");
		tmp75_ready = false;
		return -ENODEV;
	}

	/* Config register is 16-bit MSB-first; 12-bit + continuous -> 0x60 */
	uint8_t cfg[2] = { 0x00u, TMP75_CONFIG_12BIT };
	int rc = i2c_burst_write(tmp75_bus, TMP75_I2C_ADDR,
				 TMP75_REG_CONFIG, cfg, sizeof(cfg));
	if (rc != 0) {
		printk("TMP75: config write failed (rc=%d)\n", rc);
		tmp75_ready = false;
		return rc;
	}

	/* Read once to confirm the device is present. Keep the wait short: a 40 ms
	 * blocking call could hit the WWDG window (see bsp_wtdg.c). The first value
	 * may come from the previous conversion, which is normal. */
	k_msleep(5);

	int32_t milliDegC = 0;
	rc = tmp75Read(&milliDegC);
	if (rc != 0) {
		printk("TMP75: probe read failed (rc=%d)\n", rc);
		tmp75_ready = false;
		return rc;
	}

	tmp75_ready = true;
	printk("TMP75: ready @0x%02X, %d.%03d C\n", TMP75_I2C_ADDR,
	       (int)(milliDegC / 1000), (int)(milliDegC % 1000 < 0 ?
					     -(milliDegC % 1000) : milliDegC % 1000));
	return 0;
}

bool tmp75IsReady(void)
{
	return tmp75_ready;
}

int tmp75ReadRaw(int16_t *raw)
{
	uint8_t buf[2];

	if (raw == NULL) {
		return -EINVAL;
	}
	if (tmp75_bus == NULL || !device_is_ready(tmp75_bus)) {
		return -ENODEV;
	}

	int rc = i2c_burst_read(tmp75_bus, TMP75_I2C_ADDR,
				TMP75_REG_TEMP, buf, sizeof(buf));
	if (rc != 0) {
		tmp75_errors++;
		return rc;
	}

	*raw = (int16_t)(((uint16_t)buf[0] << 8) | (uint16_t)buf[1]);
	return 0;
}

int tmp75Read(int32_t *milliDegC)
{
	int16_t raw;
	int rc;

	if (milliDegC == NULL) {
		return -EINVAL;
	}

	rc = tmp75ReadRaw(&raw);
	if (rc != 0) {
		return rc;
	}

	/* raw is 12-bit left aligned: LSB = 1/256 degC -> raw * 1000 / 256 */
	*milliDegC = ((int32_t)raw * 1000) / 256;
	return 0;
}

uint32_t tmp75ErrorCount(void)
{
	return tmp75_errors;
}
