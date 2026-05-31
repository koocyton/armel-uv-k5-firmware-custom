/* Si4732 reset: active-low. Idle = released (pin high). K1: PF4; K5: PB15. */
#ifndef DRIVER_SI4732_RST_H
#define DRIVER_SI4732_RST_H

#include "driver/gpio.h"
#include "py32f071_ll_gpio.h"

#ifndef SI4732_RST_PIN
#define SI4732_RST_PIN GPIO_MAKE_PIN(GPIOF, LL_GPIO_PIN_4)
#endif

#define SI47XX_RST_ASSERT   GPIO_ResetOutputPin(SI4732_RST_PIN)  /* pin low  = chip active (k5 RST_HIGH) */
#define SI47XX_RST_RELEASE  GPIO_SetOutputPin(SI4732_RST_PIN)    /* pin high = chip in reset (k5 RST_LOW) */

#endif
