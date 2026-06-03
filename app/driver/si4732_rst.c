#include "driver/si4732_rst.h"
#include "driver/gpio.h"
#include "driver/system.h"
#include "py32f071_ll_bus.h"
#include "py32f071_ll_gpio.h"

#if defined(ENABLE_SI4732_RST_ON_PA14)

void SI4732_RST_ConfigurePin(void)
{
    LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOA);
    LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_14, LL_GPIO_MODE_OUTPUT);
    LL_GPIO_SetPinOutputType(GPIOA, LL_GPIO_PIN_14, LL_GPIO_OUTPUT_PUSHPULL);
    LL_GPIO_SetPinPull(GPIOA, LL_GPIO_PIN_14, LL_GPIO_PULL_NO);
    LL_GPIO_SetPinSpeed(GPIOA, LL_GPIO_PIN_14, LL_GPIO_SPEED_FREQ_VERY_HIGH);
}

void SI4732_RST_HoldAssert(void)
{
    SI4732_RST_ConfigurePin();
    LL_GPIO_ResetOutputPin(GPIOA, LL_GPIO_PIN_14);
}

void SI4732_RST_HoldRelease(void)
{
    SI4732_RST_ConfigurePin();
    LL_GPIO_SetOutputPin(GPIOA, LL_GPIO_PIN_14);
}

#else

#ifndef SI4732_RST_PIN
#define SI4732_RST_PIN GPIO_MAKE_PIN(GPIOA, LL_GPIO_PIN_15)
#endif

static void SI4732_RST_EnsureGpioOutput(void)
{
    const uint32_t pin = GPIO_PIN_MASK(SI4732_RST_PIN);
    GPIO_TypeDef *const port = GPIO_PORT(SI4732_RST_PIN);

    if (port == GPIOA)
        LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOA);
    else if (port == GPIOB)
        LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOB);
    else if (port == GPIOC)
        LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOC);
    else if (port == GPIOF)
        LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOF);

    LL_GPIO_SetPinMode(port, pin, LL_GPIO_MODE_OUTPUT);
    LL_GPIO_SetPinOutputType(port, pin, LL_GPIO_OUTPUT_PUSHPULL);
    LL_GPIO_SetPinPull(port, pin, LL_GPIO_PULL_NO);
    LL_GPIO_SetPinSpeed(port, pin, LL_GPIO_SPEED_FREQ_VERY_HIGH);
}

void SI4732_RST_ConfigurePin(void)
{
    SI4732_RST_EnsureGpioOutput();
}

void SI4732_RST_HoldAssert(void)
{
    SI4732_RST_EnsureGpioOutput();
    GPIO_ResetOutputPin(SI4732_RST_PIN);
}

void SI4732_RST_HoldRelease(void)
{
    SI4732_RST_EnsureGpioOutput();
    GPIO_SetOutputPin(SI4732_RST_PIN);
}

#endif

void SI4732_RST_PulseMs(uint16_t low_ms, uint16_t high_ms)
{
    SI4732_RST_HoldAssert();
    if (low_ms != 0U)
        SYSTEM_DelayMs(low_ms);
    SI4732_RST_HoldRelease();
    if (high_ms != 0U)
        SYSTEM_DelayMs(high_ms);
}
