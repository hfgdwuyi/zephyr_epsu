/*!
 * Copyright � Siemens Healthcare GmbH 2022, All Rights Reserved
 *
 * Project: Building Block Low End MCU
 *
 * @file
 * @brief Pins and interfaces initialization
 */
/*----------------------------------------------------------------------------*/
#ifndef CRC_CALC_H
#define CRC_CALC_H

#include <stdint.h>
#include <stdbool.h>

int crcInit(void);
/* Reset is mostly a semantic wrapper; driver handles internal state via ctx */
int crcReset(void);
/* Convenience APIs */
int crcCalc8(const void *data, size_t len, uint8_t *out);
int crcCalc32(const void *data, size_t len, uint32_t *out);


#endif
