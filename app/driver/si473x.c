#include "si473x.h"
#include "audio.h"
#include "driver/si4732_ext_flash.h"
#include "driver/si4732_rst.h"
#include "driver/si4732_storage.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/py25q16.h"
#include "driver/system.h"
#include "driver/systick.h"

#ifndef SI47XX_I2C_ADDR_7BIT
#define SI47XX_I2C_ADDR_7BIT 0x11U
#endif
static const uint8_t SI47XX_I2C_ADDR = (SI47XX_I2C_ADDR_7BIT << 1) | 0U;

RSQStatus rsqStatus;
uint16_t divider = 1000;

SI47XX_MODE si4732mode = SI47XX_FM;

static bool isAmFamilyStatic(void);
uint16_t siCurrentFreq = 10210;

#ifdef ENABLE_SI4732_AM_USE_FMI
static bool gAmUseFMI = true;
#else
static bool gAmUseFMI = false;
#endif

void SI47XX_ReadBuffer(uint8_t *buf, uint8_t size)
{
    I2C_Start();
    I2C_Write(SI47XX_I2C_ADDR + 1);
    I2C_ReadBuffer(buf, size);
    I2C_Stop();
}

void SI47XX_WriteBuffer(uint8_t *buf, uint8_t size)
{
    I2C_Start();
    I2C_Write(SI47XX_I2C_ADDR);
    I2C_WriteBuffer(buf, size);
    I2C_Stop();
}

void waitToSend()
{
    uint8_t tmp = 0;
    SI47XX_ReadBuffer((uint8_t *)&tmp, 1);
    while (!(tmp & STATUS_CTS)) {
        SYSTICK_DelayUs(1);
        SI47XX_ReadBuffer((uint8_t *)&tmp, 1);
    }
}

static void sendProperty(uint16_t prop, uint16_t parameter)
{
    waitToSend();
    uint8_t tmp[6] = { CMD_SET_PROPERTY, 0, (uint8_t)(prop >> 8), (uint8_t)(prop & 0xff),
                       (uint8_t)(parameter >> 8), (uint8_t)(parameter & 0xff) };
    SI47XX_WriteBuffer(tmp, 6);
    SYSTEM_DelayMs(2);
}

void SI47XX_ApplyAmAntennaInput(void)
{
    sendProperty(PROP_FM_ANTENNA_INPUT, gAmUseFMI ? 0U : 1U);
}

void SI47XX_ToggleAmAntennaFMI(void)
{
    gAmUseFMI = !gAmUseFMI;
    SI47XX_ApplyAmAntennaInput();
}

bool SI47XX_GetAmAntennaFMI(void)
{
    return gAmUseFMI;
}

void RSQ_GET()
{
    uint8_t cmd[2] = { CMD_FM_RSQ_STATUS, 0x01 };
    if (isAmFamilyStatic()) {
        cmd[0] = CMD_AM_RSQ_STATUS;
    }
    waitToSend();
    SI47XX_WriteBuffer(cmd, 2);
    SI47XX_ReadBuffer(rsqStatus.raw, si4732mode == SI47XX_FM ? 8 : 6);
}

void setVolume(uint8_t volume)
{
    if (volume > 63)
        volume = 63;
    sendProperty(PROP_RX_VOLUME, volume);
}

void SI47XX_Mute(bool mute)
{
    waitToSend();
    uint8_t cmd[3] = { 0x40, 0x00, mute ? 0x03U : 0x00U };
    SI47XX_WriteBuffer(cmd, 3);
}

static void enableRDS(void)
{
    (void)0;
}

void SI47XX_SetAutomaticGainControl(uint8_t AGCDIS, uint8_t AGCIDX)
{
    uint8_t cmd = si4732mode == SI47XX_FM ? CMD_FM_AGC_OVERRIDE : CMD_AM_AGC_OVERRIDE;
    waitToSend();
    uint8_t cmd2[3] = { cmd, (uint8_t)(AGCDIS & 1U), AGCIDX };
    SI47XX_WriteBuffer(cmd2, 3);
}

static bool FreqCheck(uint32_t f)
{
    if (si4732mode == SI47XX_FM)
        return f >= 6400000 && f <= 10800000;
    return f >= 500000 && f <= 30000000;
}

static bool isAmFamilyStatic(void)
{
    return si4732mode != SI47XX_FM;
}

bool SI47XX_IsAMFamily(void)
{
    return isAmFamilyStatic();
}

bool SI47XX_IsSSB(void)
{
    return si4732mode == SI47XX_LSB || si4732mode == SI47XX_USB;
}

#define SI473X_PATCH_SIZE   15832U
#define SI473X_PATCH_EEPROM (SI4732_VIRT_EEPROM_SIZE - SI473X_PATCH_SIZE)

static uint32_t Read_FreqSaved(void)
{
    if (isAmFamilyStatic()) {
        uint8_t buf[2];
        PY25Q16_ReadBuffer(FM_PY_SI4732_AM_EXT_ADDR, buf, 2);
        uint16_t khz = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
        if (khz < 500 || khz > 30000)
            khz = 720;
        return (uint32_t)khz * 1000U;
    }
    uint32_t tmpF;
    PY25Q16_ReadBuffer(FM_PY_FMCFG_ADDR + 4U, (uint8_t *)&tmpF, 4);
    if (!FreqCheck(tmpF))
        tmpF = 10210000;
    return tmpF;
}

void SI47XX_FirstPowerUp(uint16_t freq_10k)
{
    uint8_t cmd[3] = { CMD_POWER_UP, FLG_XOSCEN | FUNC_FM, OUT_ANALOG };
    SI47XX_WriteBuffer(cmd, 3);
    SYSTEM_DelayMs(500);
    waitToSend();
#ifdef ENABLE_FM_SI4732_AUDIO_PATH_INVERTED
    AUDIO_AudioPathOn_FM();
#else
    AUDIO_AudioPathOn();
#endif
    setVolume(63);
    SI47XX_SetSeekFmLimits(8750, 10800);
    enableRDS();
    SI47XX_SetFreq(freq_10k);
}

void SI47XX_PowerUp()
{
    SI47XX_RST_ASSERT;

    uint8_t cmd[3] = { CMD_POWER_UP, FLG_XOSCEN | FUNC_FM, OUT_ANALOG };
    if (isAmFamilyStatic()) {
        cmd[1] = FLG_XOSCEN | FUNC_AM;
    }
    waitToSend();
    SI47XX_WriteBuffer(cmd, 3);
    SYSTEM_DelayMs(500);

    AUDIO_AudioPathOn_FM();
    setVolume(63);

    if (si4732mode == SI47XX_FM) {
        enableRDS();
    } else if (isAmFamilyStatic()) {
        SI47XX_ApplyAmAntennaInput();
        SI47XX_SetAutomaticGainControl(1, 0);
        sendProperty(PROP_AM_SOFT_MUTE_MAX_ATTENUATION, 0);
        sendProperty(PROP_AM_AUTOMATIC_VOLUME_CONTROL_MAX_GAIN, 0x7800);
        SI47XX_SetSeekAmLimits(500, 30000);
    }
    SI47XX_SetFreq(Read_FreqSaved() / divider);
}

static void SI47XX_SsbSetup(uint8_t AUDIOBW, uint8_t SBCUTFLT, uint8_t AVC_DIVIDER,
                            uint8_t AVCEN, uint8_t SMUTESEL, uint8_t DSP_AFCDIS)
{
    uint8_t lo = (AUDIOBW & 0x0F) | ((SBCUTFLT & 0x0F) << 4);
    uint8_t hi = (AVC_DIVIDER & 0x0F) | (AVCEN ? 0x10U : 0) | (SMUTESEL ? 0x20U : 0) | (DSP_AFCDIS ? 0x80U : 0);
    sendProperty(PROP_SSB_MODE, (uint16_t)lo | ((uint16_t)hi << 8));
}

static bool SI47XX_downloadPatch(void)
{
    uint8_t buf[248];
    for (uint16_t offset = 0; offset < SI473X_PATCH_SIZE; offset += sizeof(buf)) {
        uint32_t n = SI473X_PATCH_SIZE - offset;
        if (n > sizeof(buf))
            n = sizeof(buf);
        SI4732_VirtEeprom_Read32(SI473X_PATCH_EEPROM + offset, buf, (uint16_t)n);
        for (uint16_t i = 0; i < n; i += 8) {
            waitToSend();
            SI47XX_WriteBuffer(buf + i, 8);
        }
        SYSTEM_DelayMs(1);
    }
    SYSTEM_DelayMs(120);
    return true;
}

static void SI47XX_PatchPowerUp(void)
{
    SI47XX_RST_ASSERT;
    uint8_t cmd[3] = { CMD_POWER_UP, 0x31, OUT_ANALOG };
    waitToSend();
    SI47XX_WriteBuffer(cmd, 3);
    SYSTEM_DelayMs(280);

    SI47XX_downloadPatch();
    SI47XX_ApplyAmAntennaInput();
    SI47XX_SsbSetup(1, 2, 0, 1, 0, 1);

    AUDIO_AudioPathOn_FM();
    setVolume(63);
    SI47XX_SetFreq(Read_FreqSaved() / divider);
    sendProperty(PROP_SSB_SOFT_MUTE_MAX_ATTENUATION, 8);
    sendProperty(PROP_SSB_SOFT_MUTE_SNR_THRESHOLD, 8);
    sendProperty(PROP_AM_AUTOMATIC_VOLUME_CONTROL_MAX_GAIN, 0x5000);
}

void SI47XX_PowerDown()
{
#ifdef ENABLE_FM_SI4732_AUDIO_PATH_INVERTED
    AUDIO_AudioPathOff_FM();
#else
    AUDIO_AudioPathOff();
#endif
    uint8_t cmd[1] = { CMD_POWER_DOWN };

    waitToSend();
    SI47XX_WriteBuffer(cmd, 1);
    SYSTICK_DelayUs(10);
    SI47XX_RST_RELEASE;
}

void SI47XX_SwitchMode(SI47XX_MODE mode)
{
    if (si4732mode == mode)
        return;
    bool wasSSB = SI47XX_IsSSB() || (si4732mode == SI47XX_CW);
    si4732mode = mode;
    if (mode == SI47XX_LSB || mode == SI47XX_USB || mode == SI47XX_CW) {
        if (!wasSSB) {
            SI47XX_PowerDown();
            SI47XX_PatchPowerUp();
        }
    } else {
        SI47XX_PowerDown();
        SI47XX_PowerUp();
    }
}

void SI47XX_SetFreq(uint16_t freq)
{
    uint8_t hb = (freq >> 8) & 0xFF;
    uint8_t lb = freq & 0xFF;
    uint8_t size = 4;
    uint8_t cmd[6] = { CMD_FM_TUNE_FREQ, 0x01, hb, lb, 0, 0 };

    if (si4732mode == SI47XX_AM) {
        cmd[0] = CMD_AM_TUNE_FREQ;
        size = 5;
        if (freq > 1800)
            cmd[5] = 1;
    } else if (SI47XX_IsSSB()) {
        cmd[0] = CMD_AM_TUNE_FREQ;
        size = 6;
        if (si4732mode == SI47XX_USB)
            cmd[1] = 0x80;
        else
            cmd[1] = 0x40;
        if (freq > 1800)
            cmd[5] = 1;
    } else if (si4732mode == SI47XX_CW) {
        cmd[0] = CMD_AM_TUNE_FREQ;
        size = 6;
        cmd[1] = 0x40;
        if (freq > 1800)
            cmd[5] = 1;
    }

    waitToSend();
    SI47XX_WriteBuffer(cmd, size);
    siCurrentFreq = freq;
    SYSTEM_DelayMs(30);
}

void SI47XX_SetSeekFmLimits(uint16_t bottom, uint16_t top)
{
    sendProperty(PROP_FM_SEEK_BAND_BOTTOM, bottom);
    sendProperty(PROP_FM_SEEK_BAND_TOP, top);
}

void SI47XX_SetSeekAmLimits(uint16_t bottom, uint16_t top)
{
    sendProperty(PROP_AM_SEEK_BAND_BOTTOM, bottom);
    sendProperty(PROP_AM_SEEK_BAND_TOP, top);
}

static const uint8_t am_att_agcidx[] = { 0, 1, 5, 15, 26 };

void SI47XX_SetAMAgcAtt(bool agcOn, uint8_t attIndex)
{
    if (agcOn) {
        SI47XX_SetAutomaticGainControl(0, 0);
    } else {
        if (attIndex > 4)
            attIndex = 4;
        SI47XX_SetAutomaticGainControl(AGC_OVERRIDE_ARG1_DISABLE_AGC, am_att_agcidx[attIndex]);
    }
}

void SI47XX_SetAMLna(uint8_t index)
{
    if (index == 0) {
        SI47XX_SetAMAgcAtt(true, 0);
    } else if (index <= 5) {
        SI47XX_SetAMAgcAtt(false, (uint8_t)(index - 1));
    }
}

void SI47XX_ApplyRxBfo(int16_t hz)
{
    if (!SI47XX_IsAMFamily())
        return;
    sendProperty(PROP_SSB_BFO, (uint16_t)hz);
}

static const uint8_t am_bw_amchflt[] = { FLG_AMCHFLT_1KHZ, FLG_AMCHFLT_1KHZ, FLG_AMCHFLT_1KHZ8, FLG_AMCHFLT_2KHZ5,
                                         FLG_AMCHFLT_3KHZ, FLG_AMCHFLT_4KHZ, FLG_AMCHFLT_6KHZ };
static const uint8_t am_bw_ssb_audiobw[] = { 4, 5, 0, 1, 2, 3, 3 };

void SI47XX_SetAMBandwidth(uint8_t index)
{
    if (index > 6)
        index = 6;
    if (si4732mode == SI47XX_AM) {
        sendProperty(PROP_AM_CHANNEL_FILTER, (uint16_t)am_bw_amchflt[index] | (0U << 8));
    } else if (SI47XX_IsSSB() || si4732mode == SI47XX_CW) {
        SI47XX_SsbSetup(am_bw_ssb_audiobw[index], 2, 0, 1, 0, 1);
    }
}
