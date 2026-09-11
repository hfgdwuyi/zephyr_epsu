/*!
 * @file tmp75.c
 * @brief TMP75 I2C 温度传感器驱动实现（见 tmp75.h 的硬件说明）
 */
/*----------------------------------------------------------------------------*/

/* Zephyr */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/printk.h>
#include <errno.h>

/* 本模块 */
#include "tmp75.h"

/* TMP75 挂在 I2C1（SCL=PB8 / SDA=PB9），与 identity EEPROM 共用总线 */
#define TMP75_BUS_NODE  DT_NODELABEL(i2c1)

/* 单次 I2C 传输超时（ms）——总线被其他从机拉低时不至于卡死线程 */
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

	/* 配置寄存器为 16-bit，MSB 先：{高字节, 低字节}
	 * 12-bit 分辨率 + 连续转换 = bits[6:5] = 11 → 低字节 0x60 */
	uint8_t cfg[2] = { 0x00u, TMP75_CONFIG_12BIT };
	int rc = i2c_burst_write(tmp75_bus, TMP75_I2C_ADDR,
				 TMP75_REG_CONFIG, cfg, sizeof(cfg));
	if (rc != 0) {
		printk("TMP75: config write failed (rc=%d)\n", rc);
		tmp75_ready = false;
		return rc;
	}

	/* 试读一次，确认器件在线。
	 * 注：这里只用很短的等待——40 ms 级的阻塞会拖长启动流程，
	 * 可能撞上 WWDG 窗口（见 bsp_wtdg.c 的 WDT_MAX_WINDOW 说明）。
	 * 首次读到的可能是上一次转换结果，属正常。 */
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

	/* raw 为 12-bit 左对齐数据：每 LSB = 1/256 °C
	 * → 毫摄氏度 = raw * 1000 / 256（整数运算，无浮点） */
	*milliDegC = ((int32_t)raw * 1000) / 256;
	return 0;
}

uint32_t tmp75ErrorCount(void)
{
	return tmp75_errors;
}
