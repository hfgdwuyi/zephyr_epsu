/*!
 * @file tmp75.h
 * @brief TMP75 I2C 温度传感器驱动（TI，12-bit 分辨率）
 *
 * 硬件连接：
 *   I2C1 总线，SCL = PB8 / SDA = PB9（与 24C04 EEPROM 共用同一总线）
 *   7 位从地址 0x48（0b1001000，即 A2:A0 = 000）
 *
 * 工作方式：
 *   - 上电默认 9-bit 分辨率；tmp75Init() 改写配置寄存器为 12-bit 连续转换
 *   - 温度寄存器 0x00 为 16-bit（MSB 先），12-bit 数据左对齐
 *     原始值 LSB 权重 = 1/256 °C → 毫摄氏度 = raw * 1000 / 256
 *
 * 典型用法：
 *   tmp75Init();
 *   int32_t milliDegC;
 *   if (tmp75Read(&milliDegC) == 0) { ... }
 */
/*----------------------------------------------------------------------------*/
#ifndef TMP75_H
#define TMP75_H

#include <stdbool.h>
#include <stdint.h>

/* ---- I2C 从地址与寄存器指针 ---- */
#define TMP75_I2C_ADDR      0x48u   /* 7-bit: 0b1001000 */
#define TMP75_REG_TEMP      0x00u   /* 温度（只读，16-bit） */
#define TMP75_REG_CONFIG    0x01u   /* 配置（读/写，16-bit） */
#define TMP75_REG_T_LOW     0x02u   /* 低温阈值 */
#define TMP75_REG_T_HIGH    0x03u   /* 高温阈值 */

/* ---- 12-bit + 连续转换（bits[6:5] = 11） ---- */
#define TMP75_CONFIG_12BIT  0x60u

/*!
 * @brief 初始化 TMP75：检查 I2C1、写配置寄存器、试读一次温度
 * @return 0 成功；负值为 errno（I2C 错误或 -ENODEV）
 */
int tmp75Init(void);

/*! @brief 初始化是否成功（可作为"器件是否存在"的判断） */
bool tmp75IsReady(void);

/*!
 * @brief 读取原始温度寄存器（12-bit 左对齐，有符号）
 * @param raw 输出原始值
 * @return 0 成功；负值为 I2C 错误
 */
int tmp75ReadRaw(int16_t *raw);

/*!
 * @brief 读取温度（毫摄氏度，避免浮点）
 * @param milliDegC 输出温度，如 25375 表示 25.375 °C
 * @return 0 成功；负值为 I2C 错误
 */
int tmp75Read(int32_t *milliDegC);

/*! @brief 累计 I2C 读失败次数（诊断用） */
uint32_t tmp75ErrorCount(void);

#endif /* TMP75_H */
