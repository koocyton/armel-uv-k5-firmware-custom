/* Si4732 reset: active-low on pin. Idle = released (high). */
#ifndef DRIVER_SI4732_RST_H
#define DRIVER_SI4732_RST_H

#include "driver/gpio.h"
#include "py32f071_ll_gpio.h"

#ifndef SI4732_RST_PIN
#define SI4732_RST_PIN GPIO_MAKE_PIN(GPIOF, LL_GPIO_PIN_4)
#endif

/* Match k5 naming: RST_HIGH in original code = assert reset (pin low). */
#define SI47XX_RST_ASSERT   GPIO_ResetOutputPin(SI4732_RST_PIN)
#define SI47XX_RST_RELEASE  GPIO_SetOutputPin(SI4732_RST_PIN)

#endif
