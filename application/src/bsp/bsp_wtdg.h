/*!
 * Copyright © Siemens Healthcare GmbH 2022, All Rights Reserved
 *
 * Project: Building Block Low End MCU
 *
 * @file
 * @brief    Header file for bsp_wtdg.c.
 */
/*----------------------------------------------------------------------------*/

#ifndef BSP_WTDG_H
#define BSP_WTDG_H

/* Internal watchdog (MCU WWDG) */
void bspWtdgInit(void);
void bspWtdgFeed(void);
void bspWtdgStop(void);


#endif

//--------------------------------- End Of File -------------------------------/
