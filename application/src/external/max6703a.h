/*!
 * @file max6703a.h
 * @brief External watchdog MAX6703A (WDI) — application layer
 *
 * The MAX6703A is supervised by toggling the DOUT_WDI pin within its
 * 1.6 s timeout. Application-level feed task lives here (not in BSP).
 */
/*----------------------------------------------------------------------------*/
#ifndef MAX6703A_H
#define MAX6703A_H

#ifdef __cplusplus
extern "C" {
#endif

void max6703aInit(void);
void max6703aFeed(void);

#ifdef __cplusplus
}
#endif

#endif /* MAX6703A_H */
