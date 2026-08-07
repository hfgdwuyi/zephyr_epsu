/*
 * ain.c — AIN application task: circuit-config voltage scaling
 */

/* BSP */
#include "bsp_ain.h"

/* Application */
#include "ain.h"

void ainPoll(void)
{
	bspAinPoll();
}

uint32_t ainReadMv(uint8_t channel)
{
	uint32_t raw = bspAinGetRawValue(channel);
	return (raw * 3300U) / 4095U;
}

uint32_t ainReadDivMv(uint8_t channel, uint32_t rHigh, uint32_t rLow)
{
	uint32_t vAdc = ainReadMv(channel);
	return (vAdc * (rHigh + rLow)) / rLow;
}

/*
 * Mains AC voltage at AIN_ADC_VIN (PC2_C, ADC3_INP0 — direct channel).
 * Vadc = 1.65 + (0.4 x 1.414 x Vin x 50.2 / (750x4 + 500 + 75)) x 3.9 / 6.49
 *        denominator = 3000 + 575 = 3575
 *   => Vadc = 1.65 + Vin x (0.4 x 1.414 x 50.2 / 3575 x 3.9 / 6.49)
 *   => Vadc = 1.65 + Vin x 0.004773
 *   => Vin  = (Vadc - 1.65) / 0.004773
 * ADC output range: 0.568 V .. 2.732 V.
 * All in mV.  Returns 0 if Vadc < 1.65 V.
 */
uint32_t ainReadVinMv(void)
{
	int32_t vAdc = (int32_t)ainReadMv(AIN_ADC_VIN);
	int32_t delta = vAdc - 1650;
	if (delta <= 0) {
		return 0;
	}
	return (uint32_t)(delta * 20953U / 100U);
}
