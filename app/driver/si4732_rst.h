/* Si4732 RST: active-low.
 * Default K1 LQFP48: PA15 (pin 38).
 * ENABLE_SI4732_RST_ON_PA14: PA14 (pin 37, SWCLK) — build with ENABLE_SWD=OFF.
 * Or override with -DSI4732_RST_PIN=GPIO_MAKE_PIN(...). */
#ifndef DRIVER_SI4732_RST_H
#define DRIVER_SI4732_RST_H

#include <stdint.h>

void SI4732_RST_ConfigurePin(void);
void SI4732_RST_HoldAssert(void);
void SI4732_RST_HoldRelease(void);
void SI4732_RST_PulseMs(uint16_t low_ms, uint16_t high_ms);

#define SI47XX_RST_ASSERT()  SI4732_RST_HoldAssert()
#define SI47XX_RST_RELEASE() SI4732_RST_HoldRelease()

#endif
