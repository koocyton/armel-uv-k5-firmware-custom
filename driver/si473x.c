#include "si473x.h"
#include "audio.h"
#include "settings.h"
#include "bsp/dp32g030/gpio.h"
#include "driver/eeprom.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/system.h"
#include "driver/systick.h"

/* I2C 7-bit: 0x11 (A0 low, default); 0x63 (A0 high). Override with -DSI47XX_I2C_ADDR_7BIT=0x63 if needed. */
#ifndef SI47XX_I2C_ADDR_7BIT
#define SI47XX_I2C_ADDR_7BIT 0x11U
#endif
static const uint8_t SI47XX_I2C_ADDR = (SI47XX_I2C_ADDR_7BIT << 1) | 0U;

#define RST_HIGH GPIO_ClearBit(&GPIOB->DATA, GPIOB_PIN_BK1080)
#define RST_LOW GPIO_SetBit(&GPIOB->DATA, GPIOB_PIN_BK1080)

RSQStatus rsqStatus;
uint16_t divider = 1000;

SI47XX_MODE si4732mode = SI47XX_FM;

static bool isAmFamilyStatic(void);
uint16_t siCurrentFreq = 10210;

/* AM/SSB 天线选择：false=AMI, true=FMI。长按 9 切换，上电时按此值设置 PROP_FM_ANTENNA_INPUT */
#ifdef ENABLE_SI4732_AM_USE_FMI
static bool gAmUseFMI = true;  /* 编译选项默认 FMI */
#else
static bool gAmUseFMI = false; /* 默认 AMI */
#endif

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

void SI47XX_ApplyAmAntennaInput(void) {
  sendProperty(PROP_FM_ANTENNA_INPUT, gAmUseFMI ? 0U : 1U); /* 0=FMI, 1=AMI */
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

/* AN332: RX_HARD_MUTE is command 0x40; ARG1=0, ARG2=0 unmute / 3 mute both */
void SI47XX_Mute(bool mute) {
  waitToSend();
  uint8_t cmd[3] = {0x40, 0x00, mute ? 0x03U : 0x00U};
  SI47XX_WriteBuffer(cmd, 3);
}

/* K5 does not use RDS; enableRDS is no-op to save flash. */
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
    /* AM / LSB / USB / CW: 500 kHz–30 MHz */
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
/* SSB patch: same layout as uv-k5-firmware-custom (patch at end of 256KB EEPROM) */
#define SI473X_EEPROM_SIZE      262144U
#define SI473X_PATCH_SIZE      15832U
#define SI473X_PATCH_EEPROM    (SI473X_EEPROM_SIZE - SI473X_PATCH_SIZE)

/* K5 EEPROM: FM at 0x0E8C (4 bytes); AM at 0x0E68 (2 bytes, kHz) to avoid clash with 0x0E90 */
#define SI473X_EEPROM_FREQ_BASE  0x0E8CU
#define SI473X_EEPROM_AM_KHZ    0x0E68U

static uint32_t Read_FreqSaved(void)
{
    if (isAmFamilyStatic()) {
        uint16_t khz;
        EEPROM_ReadBuffer(SI473X_EEPROM_AM_KHZ, (uint8_t *)&khz, 2);
        if (khz < 500 || khz > 30000)
            khz = 720;
        return (uint32_t)khz * 1000U;
    }
    uint32_t tmpF;
    EEPROM_ReadBuffer(SI473X_EEPROM_FREQ_BASE, (uint8_t *)&tmpF, 4);
    if (!FreqCheck(tmpF))
        tmpF = 10210000;
    return tmpF;
}
/* Call after caller has done reset and RST_HIGH: send POWER_UP without CTS wait, 500ms, then path/volume/RDS/SetFreq. */
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
  SI47XX_SetSeekFmLimits(8750, 10800); /* 87.5–108 MHz */
  enableRDS();
  SI47XX_SetFreq(freq_10k);
}

void SI47XX_PowerUp() {
  RST_HIGH;

  uint8_t cmd[3] = {CMD_POWER_UP, FLG_XOSCEN | FUNC_FM, OUT_ANALOG};
  if (isAmFamilyStatic()) {
    cmd[1] = FLG_XOSCEN | FUNC_AM;
  }
  waitToSend();
  SI47XX_WriteBuffer(cmd, 3);
  SYSTEM_DelayMs(500);

  AUDIO_AudioPathOn();
  setVolume(63);

  if (si4732mode == SI47XX_FM) {
    enableRDS();
  } else if (isAmFamilyStatic()) {
    /* AM / LSB / USB / CW：按当前天线选择设置（长按 9 可切换 FMI/AMI） */
    SI47XX_ApplyAmAntennaInput();
    SI47XX_SetAutomaticGainControl(1, 0);
    sendProperty(PROP_AM_SOFT_MUTE_MAX_ATTENUATION, 0);
    sendProperty(PROP_AM_AUTOMATIC_VOLUME_CONTROL_MAX_GAIN, 0x7800);
    SI47XX_SetSeekAmLimits(500, 30000); /* 500 kHz–30 MHz */
  }
  SI47XX_SetFreq(Read_FreqSaved() / divider);
}

static void SI47XX_SsbSetup(uint8_t AUDIOBW, uint8_t SBCUTFLT, uint8_t AVC_DIVIDER,
    uint8_t AVCEN, uint8_t SMUTESEL, uint8_t DSP_AFCDIS) {
  uint8_t lo = (AUDIOBW & 0x0F) | ((SBCUTFLT & 0x0F) << 4);
  uint8_t hi = (AVC_DIVIDER & 0x0F) | (AVCEN ? 0x10U : 0) | (SMUTESEL ? 0x20U : 0) | (DSP_AFCDIS ? 0x80U : 0);
  sendProperty(PROP_SSB_MODE, (uint16_t)lo | ((uint16_t)hi << 8));
}

static bool SI47XX_downloadPatch(void) {
  uint8_t buf[248];
  for (uint16_t offset = 0; offset < SI473X_PATCH_SIZE; offset += sizeof(buf)) {
    uint32_t n = SI473X_PATCH_SIZE - offset;
    if (n > sizeof(buf)) n = sizeof(buf);
    EEPROM_ReadBuffer32(SI473X_PATCH_EEPROM + offset, buf, (uint16_t)n);
    for (uint16_t i = 0; i < n; i += 8) {
      waitToSend();
      SI47XX_WriteBuffer(buf + i, 8);
    }
    SYSTEM_DelayMs(1);
  }
  SYSTEM_DelayMs(120);
  return true;
}

static void SI47XX_PatchPowerUp(void) {
  RST_HIGH;
  /* POWER_UP: AM, crystal, patch enable (same as uv-k5-firmware-custom) */
  uint8_t cmd[3] = {CMD_POWER_UP, 0x31, OUT_ANALOG}; /* 0x31 = FLG_XOSCEN | FLG_PATCH | FUNC_AM */
  waitToSend();
  SI47XX_WriteBuffer(cmd, 3);
  SYSTEM_DelayMs(280);

  SI47XX_downloadPatch();
  SI47XX_ApplyAmAntennaInput(); /* 单边带也按当前 FMI/AMI 选择 */
  /* AUDIOBW: 0=1.2k 1=2.2k 2=3k 3=4k；收窄带宽降噪，2.2k 兼顾语音与噪声 */
  SI47XX_SsbSetup(1, 2, 0, 1, 0, 1); /* AUDIOBW=2.2kHz, SBCUTFLT=2, AVC=1, DSP_AFCDIS=1 */

  AUDIO_AudioPathOn();
  setVolume(63);
  SI47XX_SetFreq(Read_FreqSaved() / divider);
  /* 弱信号时软静音减轻底噪；AVC 最大增益略降减少噪声放大 */
  sendProperty(PROP_SSB_SOFT_MUTE_MAX_ATTENUATION, 8);   /* 8dB 衰减 */
  sendProperty(PROP_SSB_SOFT_MUTE_SNR_THRESHOLD, 8);     /* SNR<8dB 时触发 */
  sendProperty(PROP_AM_AUTOMATIC_VOLUME_CONTROL_MAX_GAIN, 0x5000); /* 原 0x7800 */
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
  SYSTICK_DelayUs(10);
  RST_LOW;
}

void SI47XX_SwitchMode(SI47XX_MODE mode) {
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
    if (si4732mode == SI47XX_USB)
      cmd[1] = 0x80;
    else
      cmd[1] = 0x40;
    if (freq > 1800)
      cmd[5] = 1;
  } else if (si4732mode == SI47XX_CW) {
    cmd[0] = CMD_AM_TUNE_FREQ;
    size = 6;
    cmd[1] = 0x40; /* LSB for CW */
    if (freq > 1800)
      cmd[5] = 1;
  }

  waitToSend();
  SI47XX_WriteBuffer(cmd, size);
  siCurrentFreq = freq;
  SYSTEM_DelayMs(30);
}

void SI47XX_SetSeekFmLimits(uint16_t bottom, uint16_t top) {
  sendProperty(PROP_FM_SEEK_BAND_BOTTOM, bottom);
  sendProperty(PROP_FM_SEEK_BAND_TOP, top);
}

void SI47XX_SetSeekAmLimits(uint16_t bottom, uint16_t top) {
  sendProperty(PROP_AM_SEEK_BAND_BOTTOM, bottom);
  sendProperty(PROP_AM_SEEK_BAND_TOP, top);
}

/* ATT index 0..4 -> 0, 1, 5, 15, 26 dB (AGCIDX 值). AGC ON 时由芯片自动，OFF 时用此表 */
static const uint8_t am_att_agcidx[] = { 0, 1, 5, 15, 26 };

void SI47XX_SetAMAgcAtt(bool agcOn, uint8_t attIndex) {
  if (agcOn) {
    SI47XX_SetAutomaticGainControl(0, 0);
  } else {
    if (attIndex > 4) attIndex = 4;
    SI47XX_SetAutomaticGainControl(AGC_OVERRIDE_ARG1_DISABLE_AGC, am_att_agcidx[attIndex]);
  }
}

/* 兼容旧 LNA 索引：0=AGC ON, 1..5=ATT 0,1,5,15,26 */
void SI47XX_SetAMLna(uint8_t index) {
  if (index == 0) {
    SI47XX_SetAMAgcAtt(true, 0);
  } else if (index <= 5) {
    SI47XX_SetAMAgcAtt(false, (uint8_t)(index - 1));
  }
}

/* BW index 0..6 -> 0.5,1,1.2,2.2,3,4,5 kHz. AM: FLG_AMCHFLT 6k=0,4k=1,3k=2,2k=3,1k=4,1.8k=5,2.5k=6. SSB: AUDIOBW 4=0.5k,5=1k,0=1.2k,1=2.2k,2=3k,3=4k. */
static const uint8_t am_bw_amchflt[] = { FLG_AMCHFLT_1KHZ, FLG_AMCHFLT_1KHZ, FLG_AMCHFLT_1KHZ8, FLG_AMCHFLT_2KHZ5, FLG_AMCHFLT_3KHZ, FLG_AMCHFLT_4KHZ, FLG_AMCHFLT_6KHZ };
static const uint8_t am_bw_ssb_audiobw[] = { 4, 5, 0, 1, 2, 3, 3 };

void SI47XX_SetAMBandwidth(uint8_t index) {
  if (index > 6) index = 6;
  if (si4732mode == SI47XX_AM) {
    sendProperty(PROP_AM_CHANNEL_FILTER, (uint16_t)am_bw_amchflt[index] | (0U << 8));
  } else if (SI47XX_IsSSB() || si4732mode == SI47XX_CW) {
    SI47XX_SsbSetup(am_bw_ssb_audiobw[index], 2, 0, 1, 0, 1);
  }
}
