/* BK1080-compatible adapter for Si4732 using driver/si473x (FM only). */

#include "driver/bk1080.h"
#include "driver/bk1080-regs.h"
#include "driver/gpio.h"
#include "driver/si4732_rst.h"
#include "driver/si473x.h"
#include "driver/system.h"

uint16_t BK1080_BaseFrequency;
uint16_t BK1080_FrequencyDeviation;

void BK1080_Init0(void)
{
	SI47XX_RST_ASSERT;
	si4732mode = SI47XX_FM;
}

void BK1080_Init(uint16_t freq, uint8_t band)
{
	(void)band;
	if (freq) {
		SI47XX_HardwareReset();
		si4732mode = SI47XX_FM;
		SI47XX_FirstPowerUp((uint16_t)((unsigned long)freq * 10U));
	} else {
		SI47XX_PowerDown();
		SI47XX_RST_ASSERT;
	}
}

void BK1080_SetFrequency(uint16_t frequency, uint8_t band)
{
	(void)band;
	SI47XX_SetFreq((uint16_t)((unsigned long)frequency * 10U));
}

void BK1080_Mute(bool Mute)
{
	SI47XX_Mute(Mute);
}

void BK1080_GetFrequencyDeviation(uint16_t Frequency)
{
	BK1080_BaseFrequency = Frequency;
	RSQ_GET();
	BK1080_FrequencyDeviation = (uint16_t)rsqStatus.resp.SNR * 16U;
}

uint16_t BK1080_ReadRegister(BK1080_Register_t Register)
{
	RSQ_GET();
	if (Register == BK1080_REG_07)
		return (uint16_t)(rsqStatus.resp.SNR & 0x0FU) << BK1080_REG_07_SHIFT_SNR;
	if (Register == BK1080_REG_10)
		return (uint16_t)(BK1080_REG_10_AFCRL_NOT_RAILED | (rsqStatus.resp.RSSI & 0xFFU));
	return 0;
}

void BK1080_WriteRegister(BK1080_Register_t Register, uint16_t Value)
{
	(void)Register;
	(void)Value;
}

uint16_t BK1080_GetFreqLoLimit(uint8_t band)
{
	(void)band;
	return SI47XX_IsAMFamily() ? 500 : 875;
}

uint16_t BK1080_GetFreqHiLimit(uint8_t band)
{
	(void)band;
	return SI47XX_IsAMFamily() ? 30000 : 1080;
}
