/*
 * bsp_ain.h — Analog Input (Zephyr port) — cios-zhong
 */

#ifndef BSP_AIN_H
#define BSP_AIN_H

#include <stdint.h>
#include <zephyr/devicetree.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Channel count derives from the io-channels array in app.overlay. */
#define BSP_AIN_IO_CHANNELS_NODE DT_PATH(zephyr_user)
#define BSP_AIN_NUMBER           DT_PROP_LEN(BSP_AIN_IO_CHANNELS_NODE, io_channels)

/* AIN logical channels (matches app.overlay io-channels order) */

enum {
	AIN_ADC_12V,            /*  0: PH2   — 12V monitor, direct          */
	AIN_ADC_PDC7,           /*  1: PF11  — PSU output, 47k:4.7k divider */
	AIN_ADC_PDC6,           /*  2: PF12  — PSU output, 47k:4.7k divider */
	AIN_ADC_PDC5,           /*  3: PF13  — PSU output, 47k:4.7k divider */
	AIN_ADC_5V0,            /*  4: PF14  — 5V monitor, direct            */
	AIN_ADC_TEMP1,          /*  5: PA3   — NTC sensor 1 (no divider)    */
	AIN_ADC_TEMP2,          /*  6: PA4   — NTC sensor 2 (no divider)    */
	AIN_ADC_PDC0,           /*  7: PA6   — PSU output, 47k:4.7k divider */
	AIN_ADC_PDC4,           /*  8: PA0_C — PSU output, 47k:4.7k divider */
	AIN_ADC_PDC2,           /*  9: PB0   — PSU output, 47k:4.7k divider */
	AIN_ADC_PDC3,           /* 10: PB1   — PSU output, 47k:4.7k divider */
	AIN_ADC_3V3,            /* 11: PC0   — 3.3V monitor, direct         */
	AIN_ADC_PDC1,           /* 12: PC2   — PSU output, 47k:4.7k divider */
	AIN_ADC_VIN,            /* 13: PC2_C — mains AC, direct ADC3_INP0   */
	AIN_ADC_PDC0_ALT,       /* 14: PC3_C — second PDC0, 47k:4.7k divider */
};

/* ==================== API ==================== */

void     bspAinInit(void);
void     bspAinPoll(void);

/* Raw ADC value (12-bit, 0–4095) */
uint32_t bspAinGetRawValue(uint8_t channel);

const char *bspAinGetName(uint8_t channel);

#ifdef __cplusplus
}
#endif

#endif /* BSP_AIN_H */
