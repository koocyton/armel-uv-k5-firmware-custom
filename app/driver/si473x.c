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

/* AN332: >= 500 ms after POWER_UP (XOSCEN); K5 uses 500 ms on switch too. */
#define SI47XX_T_XOSC_STABLE_MS         500U
#define SI47XX_T_XOSC_STABLE_SWITCH_MS  500U

/* K5 BK1080_Init: 30 ms RST low + 80 ms high (same for FM/AM/SSB mode changes). */
void SI47XX_HardwareReset(void)
{
    I2C_BusIdle();
    SI4732_RST_PulseMs(30U, 80U);
    I2C_BusIdle();
}

static void SI47XX_HwResetForModeSwitch(void)
{
    SI47XX_HardwareReset();
}

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

static bool waitToSendTimeoutMs(uint16_t timeout_ms)
{
    for (uint16_t i = 0; i < timeout_ms; i++) {
        uint8_t tmp = 0;
        SI47XX_ReadBuffer((uint8_t *)&tmp, 1);
        if (tmp & STATUS_CTS)
            return true;
        SYSTEM_DelayMs(1);
    }
    return false;
}

void waitToSend()
{
    (void)waitToSendTimeoutMs(500U);
}

/* After HW reset chip has no CTS until POWER_UP (same as FirstPowerUp / FM_Start). */
static void SI47XX_SendPowerUp(uint8_t func_byte, uint16_t xosc_stable_ms)
{
    uint8_t cmd[3] = { CMD_POWER_UP, func_byte, OUT_ANALOG };
    I2C_BusIdle();
    SI47XX_WriteBuffer(cmd, 3);
    SYSTEM_DelayMs(xosc_stable_ms);
    waitToSend();
}

static void sendProperty(uint16_t prop, uint16_t parameter)
{
    waitToSend();
    uint8_t tmp[6] = { CMD_SET_PROPERTY, 0, (uint8_t)(prop >> 8), (uint8_t)(prop & 0xff),
                       (uint8_t)(parameter >> 8), (uint8_t)(parameter & 0xff) };
    SI47XX_WriteBuffer(tmp, 6);
    SYSTEM_DelayMs(2);
}

#define FM_TUNE_STC_MS 80U

static void waitForTuneStc(uint16_t timeoutMs)
{
    for (uint16_t t = 0; t < timeoutMs; t++) {
        uint8_t st = 0;
        SI47XX_ReadBuffer(&st, 1);
        if (st & STATUS_STCINT)
            return;
        SYSTEM_DelayMs(1);
    }
}

static void ackTuneStc(void)
{
    uint8_t cmd[2];
    uint8_t respLen;

    if (si4732mode == SI47XX_FM) {
        cmd[0] = CMD_FM_TUNE_STATUS;
        respLen = 7;
    } else if (isAmFamilyStatic()) {
        cmd[0] = CMD_AM_TUNE_STATUS;
        respLen = 7;
    } else {
        return;
    }
    cmd[1] = TUNE_STATUS_ARG1_CLEAR_INT;
    waitToSend();
    SI47XX_WriteBuffer(cmd, 2);
    waitToSend();
    {
        uint8_t resp[7];
        SI47XX_ReadBuffer(resp, respLen);
    }
}

static void SI47XX_ApplyFmAudioProfile(void)
{
#ifndef SI47XX_FM_DEEMPH_75
    sendProperty(PROP_FM_DEEMPHASIS, FLG_DEEMPH_50);
#else
    sendProperty(PROP_FM_DEEMPHASIS, FLG_DEEMPH_75);
#endif
    sendProperty(PROP_FM_CHANNEL_FILTER, 0);
    sendProperty(PROP_FM_MAX_TUNE_ERROR, 20);
    sendProperty(PROP_FM_ANTENNA_INPUT, 0);
    sendProperty(PROP_FM_BLEND_RSSI_STEREO_THRESHOLD, 49);
    sendProperty(PROP_FM_BLEND_RSSI_MONO_THRESHOLD, 30);
    sendProperty(PROP_FM_BLEND_SNR_STEREO_THRESHOLD, 30);
    sendProperty(PROP_FM_BLEND_SNR_MONO_THRESHOLD, 14);
    sendProperty(PROP_FM_SOFT_MUTE_SLOPE, 2);
    sendProperty(PROP_FM_SOFT_MUTE_MAX_ATTENUATION, 0);
    sendProperty(PROP_FM_SOFT_MUTE_SNR_THRESHOLD, 0);
    sendProperty(PROP_FM_HICUT_SNR_HIGH_THRESHOLD, 20);
    sendProperty(PROP_FM_HICUT_SNR_LOW_THRESHOLD, 12);
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
    /* PROP_RX_HARD_MUTE: bit0=RMUTE, bit1=LMUTE (AN332). Was wrongly using CMD_AM_TUNE_FREQ (0x40). */
    sendProperty(PROP_RX_HARD_MUTE, mute ? 0x0003U : 0x0000U);
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

#define SI47XX_SSB_AVC_MAX_GAIN 0x7800U

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
    SYSTEM_DelayMs(SI47XX_T_XOSC_STABLE_MS);
    waitToSend();
#ifdef ENABLE_FM_SI4732_AUDIO_PATH_INVERTED
    AUDIO_AudioPathOn_FM();
#else
    AUDIO_AudioPathOn();
#endif
    setVolume(63);
    SI47XX_ApplyFmAudioProfile();
    SI47XX_SetSeekFmLimits(8750, 10800);
    enableRDS();
    SI47XX_SetFreq(freq_10k);
}

static void SI47XX_AmBootAfterReset(void)
{
    SI47XX_SendPowerUp(FLG_XOSCEN | FUNC_AM, SI47XX_T_XOSC_STABLE_SWITCH_MS);

    AUDIO_AudioPathOn_FM();
    setVolume(63);
    SI47XX_ApplyAmAntennaInput();
    SI47XX_ApplyAmAudioProfile(2);
    SI47XX_SetSeekAmLimits(500, 30000);
    SI47XX_SetFreq(Read_FreqSaved() / divider);
}

void SI47XX_PowerUp()
{
    SI47XX_HwResetForModeSwitch();
    if (si4732mode == SI47XX_FM) {
        SI47XX_FirstPowerUp((uint16_t)(Read_FreqSaved() / divider));
    } else {
        SI47XX_AmBootAfterReset();
    }
}

static uint8_t SI47XX_SsbSidebandCutoff(uint8_t audiobw)
{
    return (audiobw == 0U || audiobw == 4U || audiobw == 5U) ? 0U : 1U;
}

static void SI47XX_SsbSetup(uint8_t AUDIOBW, uint8_t SBCUTFLT, uint8_t AVC_DIVIDER,
                            uint8_t AVCEN, uint8_t SMUTESEL, uint8_t DSP_AFCDIS)
{
    uint8_t lo = (AUDIOBW & 0x0F) | ((SBCUTFLT & 0x0F) << 4);
    uint8_t hi = (AVC_DIVIDER & 0x0F) | (AVCEN ? 0x10U : 0) | (SMUTESEL ? 0x20U : 0) | (DSP_AFCDIS ? 0x80U : 0);
    sendProperty(PROP_SSB_MODE, (uint16_t)lo | ((uint16_t)hi << 8));
}

static void SI47XX_SsbRunPowerUp(void)
{
    uint8_t cmd[3] = { CMD_POWER_UP, FLG_XOSCEN | FUNC_AM, OUT_ANALOG };
    waitToSend();
    SI47XX_WriteBuffer(cmd, 3);
    SYSTEM_DelayMs(SI47XX_T_XOSC_STABLE_SWITCH_MS);
    waitToSend();
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
    SI47XX_SendPowerUp(0x31U, SI47XX_T_XOSC_STABLE_SWITCH_MS);

    SI47XX_downloadPatch();
    SYSTEM_DelayMs(50);
    SI47XX_SsbRunPowerUp();

    SI47XX_ApplyAmAntennaInput();
    SI47XX_SsbSetup(1, SI47XX_SsbSidebandCutoff(1), 0, 1, 0, 1);

    AUDIO_AudioPathOn_FM();
    SI47XX_SetSeekAmLimits(500, 30000);
    SI47XX_SetFreq(Read_FreqSaved() / divider);
    {
        uint8_t bfoBuf[4];
        PY25Q16_ReadBuffer(FM_PY_SI4732_AM_EXT_ADDR, bfoBuf, 4);
        SI47XX_ApplyRxBfo((int16_t)((uint16_t)bfoBuf[2] | ((uint16_t)bfoBuf[3] << 8)));
    }
    SI47XX_SetAutomaticGainControl(0, 0);
    SI47XX_ApplySsbAudioProfile();
}

void SI47XX_PowerDown()
{
#ifdef ENABLE_FM_SI4732_AUDIO_PATH_INVERTED
    AUDIO_AudioPathOff_FM();
#else
    AUDIO_AudioPathOff();
#endif
    uint8_t cmd[1] = { CMD_POWER_DOWN };

    if (waitToSendTimeoutMs(100U))
        SI47XX_WriteBuffer(cmd, 1);
    SYSTICK_DelayUs(10);
    I2C_BusIdle();
}

void SI47XX_SwitchMode(SI47XX_MODE mode)
{
    if (si4732mode == mode)
        return;
    const bool wasSSB = SI47XX_IsSSB() || (si4732mode == SI47XX_CW);
    si4732mode = mode;

#ifdef ENABLE_FM_SI4732_AUDIO_PATH_INVERTED
    AUDIO_AudioPathOff_FM();
#else
    AUDIO_AudioPathOff();
#endif

    if (mode == SI47XX_LSB || mode == SI47XX_USB || mode == SI47XX_CW) {
        if (!wasSSB) {
            SI47XX_PowerDown();
            SI47XX_HwResetForModeSwitch();
            SI47XX_PatchPowerUp();
        }
        /* wasSSB: LSB/USB/CW — no HW reset; fm.c calls SetFreq + FM_ApplyAMOptions */
    } else {
        SI47XX_PowerDown();
        SI47XX_HwResetForModeSwitch();
        if (mode == SI47XX_FM)
            SI47XX_FirstPowerUp((uint16_t)(Read_FreqSaved() / divider));
        else
            SI47XX_AmBootAfterReset();
    }

    SI4732_RST_HoldRelease();
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
        cmd[1] = (si4732mode == SI47XX_USB) ? 0x80U : 0x40U;
        if (freq > 1800) {
            cmd[4] = 0;
            cmd[5] = 1;
        }
    } else if (si4732mode == SI47XX_CW) {
        cmd[0] = CMD_AM_TUNE_FREQ;
        size = 6;
        cmd[1] = 0x80U;
        if (freq > 1800)
            cmd[5] = 1;
    }

    waitToSend();
    SI47XX_WriteBuffer(cmd, size);
    siCurrentFreq = freq;
    waitForTuneStc(FM_TUNE_STC_MS);
    ackTuneStc();
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

static const uint8_t am_bw_amchflt[] = {
    FLG_AMCHFLT_1KHZ,
    FLG_AMCHFLT_1KHZ,
    FLG_AMCHFLT_1KHZ8,
    FLG_AMCHFLT_2KHZ5,
    FLG_AMCHFLT_3KHZ,
    FLG_AMCHFLT_4KHZ,
    FLG_AMCHFLT_6KHZ,
};
static const uint8_t am_bw_ssb_audiobw[] = { 4, 5, 0, 1, 2, 3, 3 };

#define AM_CHANNEL_FILTER_AMPLFLT (1U << 8)

void SI47XX_SetAMBandwidth(uint8_t index)
{
    if (index > 6)
        index = 6;
    if (si4732mode == SI47XX_AM) {
        sendProperty(PROP_AM_CHANNEL_FILTER, (uint16_t)am_bw_amchflt[index] | AM_CHANNEL_FILTER_AMPLFLT);
    } else if (SI47XX_IsSSB() || si4732mode == SI47XX_CW) {
        uint8_t abw = am_bw_ssb_audiobw[index];
        SI47XX_SsbSetup(abw, SI47XX_SsbSidebandCutoff(abw), 0, 1, 0, 1);
    }
}

void SI47XX_ApplySsbAudioProfile(void)
{
    if (!SI47XX_IsSSB() && si4732mode != SI47XX_CW)
        return;
    setVolume(63);
    SI47XX_Mute(false);
    SI47XX_SetAutomaticGainControl(0, 0);
    sendProperty(PROP_AM_AUTOMATIC_VOLUME_CONTROL_MAX_GAIN, SI47XX_SSB_AVC_MAX_GAIN);
    sendProperty(PROP_SSB_SOFT_MUTE_MAX_ATTENUATION, 8);
    sendProperty(PROP_SSB_SOFT_MUTE_SNR_THRESHOLD, 8);
}

void SI47XX_ApplyAmAudioProfile(uint8_t bwIndex)
{
    if (si4732mode != SI47XX_AM)
        return;
    if (bwIndex > 6)
        bwIndex = 6;
#ifndef SI47XX_FM_DEEMPH_75
    sendProperty(PROP_AM_DEEMPHASIS, FLG_DEEMPH_50);
#else
    sendProperty(PROP_AM_DEEMPHASIS, FLG_DEEMPH_75);
#endif
    sendProperty(PROP_AM_SOFT_MUTE_SLOPE, 2);
    sendProperty(PROP_AM_SOFT_MUTE_MAX_ATTENUATION, 8);
    sendProperty(PROP_AM_SOFT_MUTE_SNR_THRESHOLD, 8);
    sendProperty(PROP_AM_AUTOMATIC_VOLUME_CONTROL_MAX_GAIN, 0x5000);
    SI47XX_SetAutomaticGainControl(0, 0);
    SI47XX_SetAMBandwidth(bwIndex);
}
