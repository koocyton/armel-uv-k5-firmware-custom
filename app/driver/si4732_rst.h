/* Si4732 RST: active-low.
 * Default K1 LQFP48: PA15 (pin 38).
 * ENABLE_SI4732_RST_ON_PA14: PA14 (pin 37, SWCLK) — build with ENABLE_SWD=OFF.
 * Or override with -DSI4732_RST_PIN=GPIO_MAKE_PIN(...). */
#ifndef DRIVER_SI4732_RST_H
#define DRIVER_SI4732_RST_H

#include "driver/gpio.h"
#include "py32f071_ll_gpio.h"

#if defined(ENABLE_SI4732_RST_ON_PA14)
#define SI4732_RST_PIN GPIO_MAKE_PIN(GPIOA, LL_GPIO_PIN_14)
#elif !defined(SI4732_RST_PIN)
#define SI4732_RST_PIN GPIO_MAKE_PIN(GPIOA, LL_GPIO_PIN_15)
#endif

#define SI47XX_RST_ASSERT   GPIO_ResetOutputPin(SI4732_RST_PIN) /* RST low  = chip in reset */
#define SI47XX_RST_RELEASE  GPIO_SetOutputPin(SI4732_RST_PIN)    /* RST high = chip running */

#endif
