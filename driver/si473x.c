#include "si473x.h"
#include <string.h>
#include "audio.h"
#include "settings.h"
#include "driver/eeprom.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/system.h"
#include "driver/systick.h"

#ifndef SI47XX_I2C_ADDR_7BIT
#define SI47XX_I2C_ADDR_7BIT 0x11U
#endif
static const uint8_t SI47XX_I2C_ADDR = (SI47XX_I2C_ADDR_7BIT << 1) | 0U;

#define SI473X_SSB_PATCH_SIZE 15832U
#define SI473X_PATCH_EEPROM   (262144U - SI473X_SSB_PATCH_SIZE)

#define RST_HIGH GPIO_SI4732_RstHigh()
#define RST_LOW  GPIO_SI4732_RstLow()

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

static uint8_t gFmAudioProfile = 1; /* 0=HI-FI, 1=NORM, 2=DX */
static uint8_t gFmBandwidthIndex = 0; /* 0=auto, 1=110k, 2=84k, 3=60k, 4=40k */
static bool gFmUseFMI = true;
static uint16_t gAmAvcMaxGain = 0x5000U;
static uint8_t gAmSoftMuteMaxAttn = 8;

void SI47XX_ReadBuffer(uint8_t *buf, uint8_t size) {
  I2C_Start();
  I2C_Write(SI47XX_I2C_ADDR + 1);
  I2C_ReadBuffer(buf, size);
  I2C_Stop();
}

void SI47XX_WriteBuffer(uint8_t *buf, uint8_t size) {
  I2C_Start();
  I2C_Write(SI47XX_I2C_ADDR);
  I2C_WriteBuffer(buf, size);
  I2C_Stop();
}

void waitToSend() {
  uint8_t tmp = 0;
  SI47XX_ReadBuffer((uint8_t *)&tmp, 1);
  while (!(tmp & STATUS_CTS)) {
    SYSTICK_DelayUs(1);
    SI47XX_ReadBuffer((uint8_t *)&tmp, 1);
  }
}


static void sendProperty(uint16_t prop, uint16_t parameter) {
  waitToSend();
  uint8_t tmp[6] = {CMD_SET_PROPERTY, 0, (uint8_t)(prop >> 8), (uint8_t)(prop & 0xff),
                    (uint8_t)(parameter >> 8), (uint8_t)(parameter & 0xff)};
  SI47XX_WriteBuffer(tmp, 6);
  SYSTEM_DelayMs(2);
}

#define FM_TUNE_STC_MS   80U

static void waitForTuneStc(uint16_t timeoutMs) {
  for (uint16_t t = 0; t < timeoutMs; t++) {
    uint8_t st = 0;
    SI47XX_ReadBuffer(&st, 1);
    if (st & STATUS_STCINT)
      return;
    SYSTEM_DelayMs(1);
  }
}

static void ackTuneStc(void) {
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

static void SI47XX_ApplyFmAudioProfile(void) {
#ifndef SI47XX_FM_DEEMPH_75
  sendProperty(PROP_FM_DEEMPHASIS, FLG_DEEMPH_50);
#else
  sendProperty(PROP_FM_DEEMPHASIS, FLG_DEEMPH_75);
#endif
  static const uint8_t fmChannelFilter[] = { 0, 1, 2, 3, 4 };
  sendProperty(PROP_FM_CHANNEL_FILTER,
               fmChannelFilter[gFmBandwidthIndex < (uint8_t)(sizeof(fmChannelFilter) / sizeof(fmChannelFilter[0])) ? gFmBandwidthIndex : 0U]);
  sendProperty(PROP_FM_MAX_TUNE_ERROR, 20);
  sendProperty(PROP_FM_ANTENNA_INPUT, gFmUseFMI ? 0U : 1U);
  if (gFmAudioProfile == 0U) {
    sendProperty(PROP_FM_BLEND_RSSI_STEREO_THRESHOLD, 38);
    sendProperty(PROP_FM_BLEND_RSSI_MONO_THRESHOLD, 24);
    sendProperty(PROP_FM_BLEND_SNR_STEREO_THRESHOLD, 22);
    sendProperty(PROP_FM_BLEND_SNR_MONO_THRESHOLD, 10);
    sendProperty(PROP_FM_SOFT_MUTE_SLOPE, 1);
    sendProperty(PROP_FM_SOFT_MUTE_MAX_ATTENUATION, 4);
    sendProperty(PROP_FM_SOFT_MUTE_SNR_THRESHOLD, 4);
    sendProperty(PROP_FM_HICUT_SNR_HIGH_THRESHOLD, 28);
    sendProperty(PROP_FM_HICUT_SNR_LOW_THRESHOLD, 16);
  } else if (gFmAudioProfile == 2U) {
    sendProperty(PROP_FM_BLEND_RSSI_STEREO_THRESHOLD, 58);
    sendProperty(PROP_FM_BLEND_RSSI_MONO_THRESHOLD, 34);
    sendProperty(PROP_FM_BLEND_SNR_STEREO_THRESHOLD, 36);
    sendProperty(PROP_FM_BLEND_SNR_MONO_THRESHOLD, 18);
    sendProperty(PROP_FM_SOFT_MUTE_SLOPE, 2);
    sendProperty(PROP_FM_SOFT_MUTE_MAX_ATTENUATION, 6);
    sendProperty(PROP_FM_SOFT_MUTE_SNR_THRESHOLD, 5);
    sendProperty(PROP_FM_HICUT_SNR_HIGH_THRESHOLD, 16);
    sendProperty(PROP_FM_HICUT_SNR_LOW_THRESHOLD, 8);
  } else {
    sendProperty(PROP_FM_BLEND_RSSI_STEREO_THRESHOLD, 49);
    sendProperty(PROP_FM_BLEND_RSSI_MONO_THRESHOLD, 30);
    sendProperty(PROP_FM_BLEND_SNR_STEREO_THRESHOLD, 30);
    sendProperty(PROP_FM_BLEND_SNR_MONO_THRESHOLD, 14);
    sendProperty(PROP_FM_SOFT_MUTE_SLOPE, 2);
    sendProperty(PROP_FM_SOFT_MUTE_MAX_ATTENUATION, 6);
    sendProperty(PROP_FM_SOFT_MUTE_SNR_THRESHOLD, 6);
    sendProperty(PROP_FM_HICUT_SNR_HIGH_THRESHOLD, 20);
    sendProperty(PROP_FM_HICUT_SNR_LOW_THRESHOLD, 12);
  }
}

void SI47XX_SetFmAudioControls(uint8_t profile, uint8_t bandwidthIndex, bool useFMI) {
  if (profile > 2U)
    profile = 1U;
  if (bandwidthIndex > 4U)
    bandwidthIndex = 0U;
  gFmAudioProfile = profile;
  gFmBandwidthIndex = bandwidthIndex;
  gFmUseFMI = useFMI;
}

void SI47XX_ApplyFmAudioControls(void) {
  if (si4732mode == SI47XX_FM)
    SI47XX_ApplyFmAudioProfile();
}

void SI47XX_ApplyAmAntennaInput(void) {
  sendProperty(PROP_FM_ANTENNA_INPUT, gAmUseFMI ? 0U : 1U);
}

void SI47XX_ToggleAmAntennaFMI(void) {
  gAmUseFMI = !gAmUseFMI;
  SI47XX_ApplyAmAntennaInput();
}

bool SI47XX_GetAmAntennaFMI(void) {
  return gAmUseFMI;
}

void RSQ_GET() {
  uint8_t cmd[2] = {CMD_FM_RSQ_STATUS, 0x01};
  if (isAmFamilyStatic()) {
    cmd[0] = CMD_AM_RSQ_STATUS;
  }
  waitToSend();
  SI47XX_WriteBuffer(cmd, 2);
  SI47XX_ReadBuffer(rsqStatus.raw, si4732mode == SI47XX_FM ? 8 : 6);
}

void setVolume(uint8_t volume) {
  if (volume > 63)
    volume = 63;
  sendProperty(PROP_RX_VOLUME, volume);
}

void SI47XX_Mute(bool mute) {
  /* 0x40 是 AM_TUNE_FREQ，不是静音命令；须写 PROP_RX_HARD_MUTE（与 PU2CLR setAudioMute 一致） */
  sendProperty(PROP_RX_HARD_MUTE, mute ? 3U : 0U);
}

static void enableRDS(void) {
  (void)0;
}

void SI47XX_SetAutomaticGainControl(uint8_t AGCDIS, uint8_t AGCIDX) {
  uint8_t cmd = si4732mode == SI47XX_FM ? CMD_FM_AGC_OVERRIDE : CMD_AM_AGC_OVERRIDE;
  waitToSend();
  uint8_t cmd2[3] = {cmd, (uint8_t)(AGCDIS & 1U), AGCIDX};
  SI47XX_WriteBuffer(cmd2, 3);
}


static bool FreqCheck(uint32_t f) {
    if (si4732mode == SI47XX_FM)
        return f >= 6400000 && f <= 10800000;
    return f >= 500000 && f <= 30000000;
}

static bool isAmFamilyStatic(void) {
    return si4732mode != SI47XX_FM;
}

bool SI47XX_IsAMFamily(void) {
    return isAmFamilyStatic();
}

bool SI47XX_IsSSB(void) {
    return si4732mode == SI47XX_LSB || si4732mode == SI47XX_USB;
}

#define SI473X_EEPROM_FREQ_BASE  0x0E8CU
#define SI473X_EEPROM_AM_KHZ    0x0E68U

#define SI47XX_SSB_AVC_MAX_GAIN  0x7800U

static uint16_t gSsbAvcMaxGain = SI47XX_SSB_AVC_MAX_GAIN;
static uint8_t gSsbSoftMuteMaxAttn = 0;

static uint16_t Read_AmFreqKHzSaved(void)
{
    uint16_t khz;
#if defined(ENABLE_FMRADIO) && defined(ENABLE_FM_SI4732)
    uint8_t buf[2];
    EEPROM_ReadBuffer(SI473X_EEPROM_AM_KHZ, buf, 2);
    khz = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
#else
    I2C_EEPROM_ReadBuffer(SI473X_EEPROM_AM_KHZ, (uint8_t *)&khz, 2);
#endif
    if (khz < 500 || khz > 30000)
        khz = 720;
    return khz;
}

static uint32_t Read_FreqSaved(void)
{
    if (isAmFamilyStatic()) {
        return (uint32_t)Read_AmFreqKHzSaved() * 1000U;
    }
    uint32_t tmpF;
    EEPROM_ReadBuffer(SI473X_EEPROM_FREQ_BASE, (uint8_t *)&tmpF, 4);
    if (!FreqCheck(tmpF))
        tmpF = 10210000;
    return tmpF;
}

/* RST is active-low; hold low 30 ms then release high 80 ms before I2C (matches BK1080_Init). */
void SI47XX_HardwareReset(void)
{
  RST_LOW;
  SYSTEM_DelayMs(30);
  RST_HIGH;
  SYSTEM_DelayMs(80);
}

void SI47XX_FirstPowerUp(uint16_t freq_10k) {
  uint8_t cmd[3] = {CMD_POWER_UP, FLG_XOSCEN | FUNC_FM, OUT_ANALOG};
  SI47XX_WriteBuffer(cmd, 3);
  SYSTEM_DelayMs(500);
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

void SI47XX_PowerUp() {
  SI47XX_HardwareReset();

  uint8_t cmd[3] = {CMD_POWER_UP, FLG_XOSCEN | FUNC_FM, OUT_ANALOG};
  if (isAmFamilyStatic()) {
    cmd[1] = FLG_XOSCEN | FUNC_AM;
  }
  waitToSend();
  SI47XX_WriteBuffer(cmd, 3);
  SYSTEM_DelayMs(500);

  AUDIO_AudioPathOn_FM();
  setVolume(63);

  if (si4732mode == SI47XX_FM) {
    SI47XX_ApplyFmAudioProfile();
    enableRDS();
  } else if (si4732mode == SI47XX_AM) {
    SI47XX_ApplyAmAntennaInput();
    SI47XX_ApplyAmAudioProfile(2);
    SI47XX_SetSeekAmLimits(500, 30000);
  }
  SI47XX_SetFreq(Read_FreqSaved() / divider);
}

static uint8_t gSsbAudiobw = 1;

static uint8_t SI47XX_SsbSidebandCutoff(uint8_t audiobw) {
  return (audiobw == 0U || audiobw == 4U || audiobw == 5U) ? 0U : 1U;
}

static void SI47XX_SsbSetup(uint8_t AUDIOBW, uint8_t SBCUTFLT, uint8_t AVC_DIVIDER,
    uint8_t AVCEN, uint8_t SMUTESEL, uint8_t DSP_AFCDIS) {
  uint8_t lo = (AUDIOBW & 0x0F) | ((SBCUTFLT & 0x0F) << 4);
  uint8_t hi = (AVC_DIVIDER & 0x0F) | (AVCEN ? 0x10U : 0) | (SMUTESEL ? 0x20U : 0) | (DSP_AFCDIS ? 0x80U : 0);
  sendProperty(PROP_SSB_MODE, (uint16_t)lo | ((uint16_t)hi << 8));
}

static void SI47XX_SsbRunPowerUp(void) {
  uint8_t cmd[3] = {CMD_POWER_UP, FLG_XOSCEN | FUNC_AM, OUT_ANALOG};
  waitToSend();
  SI47XX_WriteBuffer(cmd, 3);
  SYSTEM_DelayMs(500);
  waitToSend();
}

static bool SI47XX_downloadPatch(void) {
  const uint16_t patchSize = SI473X_SSB_PATCH_SIZE;
  uint8_t chunk[8];
  for (uint16_t offset = 0; offset < patchSize; offset += 8) {
    I2C_EEPROM_ReadBuffer32(SI473X_PATCH_EEPROM + offset, chunk, 8);
    waitToSend();
    SI47XX_WriteBuffer(chunk, 8);
    /* PU2CLR approach 4：每 8 字节后读状态，仅 CTS(0x80) 为成功 */
    waitToSend();
    {
      uint8_t st = 0;
      SI47XX_ReadBuffer(&st, 1);
      if (st != 0x80U)
        return false;
    }
  }
  SYSTEM_DelayMs(25);
  return true;
}

static void SI47XX_PatchPowerUp(void) {
  for (uint8_t attempt = 0; attempt < 2; attempt++) {
    SI47XX_HardwareReset();
    uint8_t cmd[3] = {CMD_POWER_UP, 0x31, OUT_ANALOG};
    SI47XX_WriteBuffer(cmd, 3);
    SYSTEM_DelayMs(500);
    waitToSend();

    if (!SI47XX_downloadPatch())
      continue;
    SYSTEM_DelayMs(50);

    // 先完成第二次 power up
    SI47XX_SsbRunPowerUp();
    waitToSend();

    gSsbAudiobw = 1;
    SI47XX_SsbSetup(gSsbAudiobw, SI47XX_SsbSidebandCutoff(gSsbAudiobw), 0, 1, 0, 1);

    SI47XX_ApplyAmAntennaInput();
    SI47XX_Mute(true);
    AUDIO_AudioPathOn_FM();
    SI47XX_SetSeekAmLimits(500, 30000);
    /* 调谐与出声由 fm.c 在静音下一次性完成，避免与 PatchPowerUp 重复咔哒 */
    return;
  }
}

void SI47XX_PowerDown() {
#ifdef ENABLE_FM_SI4732_AUDIO_PATH_INVERTED
  AUDIO_AudioPathOff_FM();
#else
  AUDIO_AudioPathOff();
#endif
  uint8_t cmd[1] = {CMD_POWER_DOWN};

  waitToSend();
  SI47XX_WriteBuffer(cmd, 1);
  SYSTICK_DelayUs(2500);
  /* 与 PU2CLR 一致：软件 POWER_DOWN 即可，RST 由后续 HardwareReset 统一处理 */
}

void SI47XX_SwitchMode(SI47XX_MODE mode) {
  if (si4732mode == mode)
      return;
  bool wasSSB = SI47XX_IsSSB() || (si4732mode == SI47XX_CW);
  si4732mode = mode;
  if (mode == SI47XX_LSB || mode == SI47XX_USB || mode == SI47XX_CW) {
      if (!wasSSB) {
          SI47XX_Mute(true);          // ← 新增：先静音再断电
          SI47XX_PowerDown();
          SI47XX_PatchPowerUp();
      }
  } else {
      SI47XX_Mute(true);              // ← 新增：AM/FM 切换也先静音
      SI47XX_PowerDown();
      SI47XX_PowerUp();
  }
}

void SI47XX_SetFreq(uint16_t freq) {
  uint8_t hb = (freq >> 8) & 0xFF;
  uint8_t lb = freq & 0xFF;
  uint8_t size = 4;
  uint8_t cmd[6] = {CMD_FM_TUNE_FREQ, 0x01, hb, lb, 0, 0};

  if (si4732mode == SI47XX_AM) {
    cmd[0] = CMD_AM_TUNE_FREQ;
    size = 5;
    if (freq > 1800)
      cmd[5] = 1;
  } else if (SI47XX_IsSSB()) {
    cmd[0] = CMD_AM_TUNE_FREQ;
    size = 6;
    /* AN332 ARG1 USBLSB[7:6]：USB=0x80，LSB=0x40（与 K5 一致，不用 FAST 避免切换时多一次 STC 杂音） */
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

void SI47XX_SetSeekFmLimits(uint16_t bottom, uint16_t top) {
  sendProperty(PROP_FM_SEEK_BAND_BOTTOM, bottom);
  sendProperty(PROP_FM_SEEK_BAND_TOP, top);
}

void SI47XX_SetSeekAmLimits(uint16_t bottom, uint16_t top) {
  sendProperty(PROP_AM_SEEK_BAND_BOTTOM, bottom);
  sendProperty(PROP_AM_SEEK_BAND_TOP, top);
}

static uint8_t SI47XX_ReadTuneStatus(uint8_t arg, uint8_t *resp) {
  uint8_t cmd[2];

  cmd[0] = (si4732mode == SI47XX_FM) ? CMD_FM_TUNE_STATUS : CMD_AM_TUNE_STATUS;
  cmd[1] = arg;
  waitToSend();
  SI47XX_WriteBuffer(cmd, 2);
  waitToSend();
  SI47XX_ReadBuffer(resp, 7);
  siCurrentFreq = (uint16_t)(((uint16_t)resp[2] << 8) | resp[3]);
  return resp[0];
}

void SI47XX_SeekStartUp(uint16_t spacing) {
  uint8_t cmd[2];

  if (si4732mode == SI47XX_FM) {
    /* FM spacing is expressed in 10 kHz units. */
    SI47XX_SetSeekFmLimits(8750, 10800);
    sendProperty(PROP_FM_SEEK_FREQ_SPACING, spacing);
    sendProperty(PROP_FM_SEEK_TUNE_RSSI_THRESHOLD, 12);
    sendProperty(PROP_FM_SEEK_TUNE_SNR_THRESHOLD, 4);
    cmd[0] = CMD_FM_SEEK_START;
  } else if (si4732mode == SI47XX_AM) {
    /* AM spacing is expressed in kHz. */
    SI47XX_SetSeekAmLimits(500, 30000);
    sendProperty(PROP_AM_SEEK_FREQ_SPACING, spacing);
    sendProperty(PROP_AM_SEEK_TUNE_RSSI_THRESHOLD, 10);
    sendProperty(PROP_AM_SEEK_TUNE_SNR_THRESHOLD, 3);
    cmd[0] = CMD_AM_SEEK_START;
  } else {
    return;
  }

  /* WRAP makes a seek crossing the upper band edge continue at the bottom. */
  cmd[1] = SEEK_START_ARG1_SEEK_UP | SEEK_START_ARG1_WRAP;
  waitToSend();
  SI47XX_WriteBuffer(cmd, 2);
}

bool SI47XX_SeekPoll(uint16_t *frequency, bool *valid) {
  uint8_t resp[7];
  const uint8_t status = SI47XX_ReadTuneStatus(0, resp);
  const bool complete = (status & STATUS_STCINT) != 0;

  if (frequency != NULL)
    *frequency = siCurrentFreq;
  if (valid != NULL)
    *valid = (resp[1] & FIELD_TUNE_STATUS_RESP1_VALID) != 0;

  if (complete)
    SI47XX_ReadTuneStatus(TUNE_STATUS_ARG1_CLEAR_INT, resp);
  return complete;
}

uint16_t SI47XX_SeekCancel(void) {
  uint8_t resp[7];
  SI47XX_ReadTuneStatus(TUNE_STATUS_ARG1_CANCEL_SEEK | TUNE_STATUS_ARG1_CLEAR_INT, resp);
  return siCurrentFreq;
}

static const uint8_t am_att_agcidx[] = { 0, 1, 5, 15, 26 };

void SI47XX_SetAMAgcAtt(bool agcOn, uint8_t attIndex) {
  if (agcOn) {
    SI47XX_SetAutomaticGainControl(0, 0);
  } else {
    if (attIndex > 4) attIndex = 4;
    SI47XX_SetAutomaticGainControl(AGC_OVERRIDE_ARG1_DISABLE_AGC, am_att_agcidx[attIndex]);
  }
}

void SI47XX_SetSsbAudioControls(uint16_t avcMaxGain, uint8_t softMuteMaxAttn) {
  gSsbAvcMaxGain = avcMaxGain;
  gSsbSoftMuteMaxAttn = softMuteMaxAttn;
}

void SI47XX_SetAmAudioControls(uint16_t avcMaxGain, uint8_t softMuteMaxAttn) {
  gAmAvcMaxGain = avcMaxGain;
  gAmSoftMuteMaxAttn = softMuteMaxAttn;
}

void SI47XX_SetAMLna(uint8_t index) {
  if (index == 0) {
    SI47XX_SetAMAgcAtt(true, 0);
  } else if (index <= 5) {
    SI47XX_SetAMAgcAtt(false, (uint8_t)(index - 1));
  }
}

void SI47XX_ApplyRxBfo(int16_t hz) {
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

#define AM_CHANNEL_FILTER_AMPLFLT  (1U << 8)

void SI47XX_SetAMBandwidth(uint8_t index) {
  if (index > 6) index = 6;
  if (si4732mode == SI47XX_AM) {
    sendProperty(PROP_AM_CHANNEL_FILTER,
                 (uint16_t)am_bw_amchflt[index] | AM_CHANNEL_FILTER_AMPLFLT);
  } else if (SI47XX_IsSSB() || si4732mode == SI47XX_CW) {
    gSsbAudiobw = am_bw_ssb_audiobw[index];
    SI47XX_SsbSetup(gSsbAudiobw, SI47XX_SsbSidebandCutoff(gSsbAudiobw), 0, 1, 0, 1);
  }
}

void SI47XX_ApplySsbAudioProfile(void) {
  if (!SI47XX_IsSSB() && si4732mode != SI47XX_CW)
    return;
  setVolume(63);
  SI47XX_Mute(false);
  sendProperty(PROP_AM_AUTOMATIC_VOLUME_CONTROL_MAX_GAIN, gSsbAvcMaxGain);
  /* 0 = disable soft mute. SSB weak signals sound cleaner without the default 8 dB dip. */
  sendProperty(PROP_SSB_SOFT_MUTE_MAX_ATTENUATION, gSsbSoftMuteMaxAttn);
  sendProperty(PROP_SSB_SOFT_MUTE_SNR_THRESHOLD, 8);
}

void SI47XX_ApplyAmAudioProfile(uint8_t bwIndex) {
  if (si4732mode != SI47XX_AM)
    return;
  if (bwIndex > 6)
    bwIndex = 6;
  setVolume(63);
  SI47XX_Mute(false);
#ifndef SI47XX_FM_DEEMPH_75
  sendProperty(PROP_AM_DEEMPHASIS, FLG_DEEMPH_50);
#else
  sendProperty(PROP_AM_DEEMPHASIS, FLG_DEEMPH_75);
#endif
  sendProperty(PROP_AM_SOFT_MUTE_SLOPE, 2);
  sendProperty(PROP_AM_SOFT_MUTE_MAX_ATTENUATION, gAmSoftMuteMaxAttn);
  sendProperty(PROP_AM_SOFT_MUTE_SNR_THRESHOLD, 8);
  sendProperty(PROP_AM_AUTOMATIC_VOLUME_CONTROL_MAX_GAIN, gAmAvcMaxGain);
  SI47XX_SetAutomaticGainControl(0, 0);
  SI47XX_SetAMBandwidth(bwIndex);
}
