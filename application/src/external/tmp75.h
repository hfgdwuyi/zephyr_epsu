/*!
 * @file tmp75.h
 * @brief TMP75 I2C temperature sensor driver (TI, 12-bit)
 *
 * Hardware:
 *   I2C1, SCL = PB8 / SDA = PB9 (shared with the 24C04 EEPROM)
 *   7-bit slave address 0x48 (A2:A0 = 000)
 *
 * Behaviour:
 *   - 9-bit after power-up; tmp75Init() sets 12-bit continuous conversion
 *   - temperature register 0x00 is 16-bit MSB-first, 12-bit left aligned;
 *     LSB weight 1/256 degC -> milli-degC = raw * 1000 / 256
 *
 * Typical usage:
 *   tmp75Init();
 *   int32_t milliDegC;
 *   if (tmp75Read(&milliDegC) == 0) { ... }
 */
/*----------------------------------------------------------------------------*/
#ifndef TMP75_H
#define TMP75_H

/* Standard library */
#include <stdbool.h>
#include <stdint.h>

/* ---- I2C address and register pointers ---- */
#define TMP75_I2C_ADDR      0x48u   /* 7-bit: 0b1001000 */
#define TMP75_REG_TEMP      0x00u   /* temperature (RO, 16-bit) */
#define TMP75_REG_CONFIG    0x01u   /* configuration (RW, 16-bit) */
#define TMP75_REG_T_LOW     0x02u   /* low limit */
#define TMP75_REG_T_HIGH    0x03u   /* high limit */

/* ---- 12-bit, continuous conversion (bits[6:5] = 11) ---- */
#define TMP75_CONFIG_12BIT  0x60u

/*!
 * @brief Init TMP75: check I2C1, write the config register, read once
 * @return 0 on success, negative errno on failure (-ENODEV if absent)
 */
int tmp75Init(void);

/*! @brief True if init succeeded (can be used as a presence check) */
bool tmp75IsReady(void);

/*!
 * @brief Read the raw temperature register (12-bit, left aligned, signed)
 * @param raw output raw value
 * @return 0 on success, negative I2C error otherwise
 */
int tmp75ReadRaw(int16_t *raw);

/*!
 * @brief Read the temperature in milli-degC (no floating point)
 * @param milliDegC output, e.g. 25375 means 25.375 degC
 * @return 0 on success, negative I2C error otherwise
 */
int tmp75Read(int32_t *milliDegC);

/*! @brief Accumulated I2C read failures (diagnostics) */
uint32_t tmp75ErrorCount(void);

#endif /* TMP75_H */
