/* Copyright 2023 Dual Tachyon
 * https://github.com/DualTachyon
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

#ifdef ENABLE_FMRADIO

#include <string.h>

#include "app/action.h"
#include "app/fm.h"
#include "app/generic.h"
#include "audio.h"
#include "bsp/dp32g030/gpio.h"
#include "driver/bk1080.h"
#include "driver/bk4819.h"
#include "driver/eeprom.h"
#define FM_EEPROM_AM_KHZ 0x0E68U
#ifdef ENABLE_FM_SI4732
#include "driver/si473x.h"
#endif
#include "driver/gpio.h"
#include "driver/st7565.h"
#include "driver/system.h"
#include "ui/fmradio.h"
#include "functions.h"
#include "misc.h"
#include "settings.h"
#include "ui/inputbox.h"
#include "ui/ui.h"

#ifndef ARRAY_SIZE
	#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))
#endif

uint16_t          gFM_Channels[20];
bool              gFmRadioMode;
uint8_t           gFmRadioCountdown_500ms;
volatile uint16_t gFmPlayCountdown_10ms;
volatile int8_t   gFM_ScanState;
bool              gFM_AutoScan;
uint8_t           gFM_ChannelPosition;
bool              gFM_FoundFrequency;
bool              gFM_AutoScan;
uint16_t          gFM_RestoreCountdown_10ms;

#ifdef ENABLE_FM_SI4732
/* AM mode: current frequency in kHz (500–30000, MW+SW). Used when si4732mode == SI47XX_AM. */
static uint16_t gAM_FrequencyKHz = 720;
/* AM 底部选项：短按 M 切换焦点 0=LNA 1=BW 2=STP 3=BFO；短按 * 修改当前子选项 */
static uint8_t gAM_OptionFocus = 0;
static int16_t gAM_BFO_Hz = 0;        /* 拍频偏移 ±8000 Hz */
/* LNA：0=AGC ON，1..5 对应 SI47XX_SetAMLna（ATT 0/1/5/15/26 dB） */
static uint8_t gAM_LnaIndex = 0;
static uint8_t gAM_BW_Index = 2;    /* 0..6 = 0.5,1,1.2,2.2,3,4,5 kHz，默认 1.2 */
/* Step index 0..4 = 1, 5, 10, 100, 1000 kHz，显示 1K 5K 10K 100K 1000K */
static uint8_t gAM_StepIndex = 0;   /* 默认 1k */
/* 长按 F 刚进入单边带时置位，松键清除；避免同一长按的后续 held 事件立刻触发“退回 AM” */
static bool gFKeyJustEnteredSSB = false;
/* 本次 F 键已触发长按，松键前不再触发短按；松键时清除 */
static bool gFKeyLongPressDone = false;

static const uint16_t gAM_StepKHzTable[] = { 1, 5, 10, 100, 1000 };
#define AM_STEP_COUNT ((unsigned)ARRAY_SIZE(gAM_StepKHzTable))

uint16_t FM_GetAM_StepKHz(void)
{
	return gAM_StepKHzTable[gAM_StepIndex < AM_STEP_COUNT ? gAM_StepIndex : 0];
}

uint8_t FM_GetAM_OptionFocus(void) { return gAM_OptionFocus; }
uint8_t FM_GetAM_LnaIndex(void)    { return gAM_LnaIndex; }
uint8_t FM_GetAM_BW_Index(void)   { return gAM_BW_Index; }
uint8_t FM_GetAM_StepIndex(void)   { return gAM_StepIndex; }
int16_t FM_GetAM_BFO_Hz(void)      { return gAM_BFO_Hz; }

static void FM_ApplyAMOptions(void) {
	SI47XX_SetAMLna(gAM_LnaIndex);
	SI47XX_SetAMBandwidth(gAM_BW_Index);
	SI47XX_SetBFO(gAM_BFO_Hz);
}

bool FM_IsAMMode(void)
{
	return SI47XX_IsAMFamily();
}

static void FM_SaveAMFreqToEeprom(void)
{
	uint8_t buf[8];
	EEPROM_ReadBuffer(FM_EEPROM_AM_KHZ, buf, 8);
	buf[0] = (uint8_t)(gAM_FrequencyKHz & 0xFF);
	buf[1] = (uint8_t)(gAM_FrequencyKHz >> 8);
	buf[2] = (uint8_t)((uint16_t)gAM_BFO_Hz & 0xFF);
	buf[3] = (uint8_t)(((uint16_t)gAM_BFO_Hz >> 8) & 0xFF);
	buf[4] = gAM_LnaIndex;
	EEPROM_WriteBuffer(FM_EEPROM_AM_KHZ, buf);
}

void FM_LoadAMFrequencyFromEeprom(void)
{
	uint8_t buf[8];
	EEPROM_ReadBuffer(FM_EEPROM_AM_KHZ, buf, 8);
	uint16_t khz = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
	if (khz >= 500 && khz <= 30000)
		gAM_FrequencyKHz = khz;
	int16_t bfo = (int16_t)((uint16_t)buf[2] | ((uint16_t)buf[3] << 8));
	if (bfo >= -8000 && bfo <= 8000)
		gAM_BFO_Hz = bfo;
	if (buf[4] <= 5)
		gAM_LnaIndex = buf[4];
}
#endif

const uint8_t BUTTON_STATE_PRESSED = 1 << 0;
const uint8_t BUTTON_STATE_HELD = 1 << 1;

const uint8_t BUTTON_EVENT_PRESSED = BUTTON_STATE_PRESSED;
const uint8_t BUTTON_EVENT_HELD = BUTTON_STATE_PRESSED | BUTTON_STATE_HELD;
const uint8_t BUTTON_EVENT_SHORT =  0;
const uint8_t BUTTON_EVENT_LONG =  BUTTON_STATE_HELD;


static void Key_FUNC(KEY_Code_t Key, uint8_t state);

bool FM_CheckValidChannel(uint8_t Channel)
{
	return  Channel < ARRAY_SIZE(gFM_Channels) && 
			gFM_Channels[Channel] >= BK1080_GetFreqLoLimit(gEeprom.FM_Band) && 
			gFM_Channels[Channel] < BK1080_GetFreqHiLimit(gEeprom.FM_Band);
}

uint8_t FM_FindNextChannel(uint8_t Channel, uint8_t Direction)
{
	for (unsigned i = 0; i < ARRAY_SIZE(gFM_Channels); i++) {
		if (Channel == 0xFF)
			Channel = ARRAY_SIZE(gFM_Channels) - 1;
		else if (Channel >= ARRAY_SIZE(gFM_Channels))
			Channel = 0;
		if (FM_CheckValidChannel(Channel))
			return Channel;
		Channel += Direction;
	}

	return 0xFF;
}

int FM_ConfigureChannelState(void)
{
	gEeprom.FM_FrequencyPlaying = gEeprom.FM_SelectedFrequency;

	if (gEeprom.FM_IsMrMode) {
		const uint8_t Channel = FM_FindNextChannel(gEeprom.FM_SelectedChannel, FM_CHANNEL_UP);
		if (Channel == 0xFF) {
			gEeprom.FM_IsMrMode = false;
			return -1;
		}
		gEeprom.FM_SelectedChannel  = Channel;
		gEeprom.FM_FrequencyPlaying = gFM_Channels[Channel];
	}

	return 0;
}

void FM_TurnOff(void)
{
#ifdef ENABLE_FM_SI4732
	/* Persist AM frequency so next boot / switch-to-AM restores it */
	if (SI47XX_IsAMFamily())
		FM_SaveAMFreqToEeprom();
#endif
	gFmRadioMode              = false;
	gFM_ScanState             = FM_SCAN_OFF;
	gFM_RestoreCountdown_10ms = 0;

	AUDIO_AudioPathOff_FM();
	gEnableSpeaker = false;

	BK1080_Init0();

	gUpdateStatus  = true;
}

void FM_EraseChannels(void)
{
	uint8_t      Template[8];
	memset(Template, 0xFF, sizeof(Template));

	for (unsigned i = 0; i < 5; i++)
		EEPROM_WriteBuffer(0x0E40 + (i * 8), Template);

	memset(gFM_Channels, 0xFF, sizeof(gFM_Channels));
}

#ifdef ENABLE_FM_SI4732
/* FM：存储为 10 kHz 整数（显示 MHz = 值/100）。手调按 0.05 MHz 网格：
 * 未在网格上时：上调 → 向上取整到 0.05；下调 → 向下取整到 0.05。
 * 已在网格上：上调 +0.05，下调 -0.05。（例：100.04 下→100.00→99.95；上→100.05→100.10） */
static uint16_t FM_NextOn05MhzGrid(uint16_t cur, int8_t stepDir)
{
	const uint16_t lo = BK1080_GetFreqLoLimit(gEeprom.FM_Band);
	const uint16_t hi = BK1080_GetFreqHiLimit(gEeprom.FM_Band);
	int32_t n;

	if (stepDir > 0) {
		if ((cur % 5u) == 0u)
			n = (int32_t)cur + 5;
		else
			n = (int32_t)((cur / 5u + 1u) * 5u);
	} else {
		if ((cur % 5u) == 0u)
			n = (int32_t)cur - 5;
		else
			n = (int32_t)((cur / 5u) * 5u);
	}

	if (n < (int32_t)lo)
		n = (int32_t)hi;
	else if (n > (int32_t)hi)
		n = (int32_t)lo;

	return (uint16_t)n;
}
#endif

void FM_Tune(uint16_t Frequency, int8_t Step, bool bFlag)
{
	AUDIO_AudioPathOff_FM();

	gEnableSpeaker = false;

	gFmPlayCountdown_10ms = (gFM_ScanState == FM_SCAN_OFF) ? fm_play_countdown_noscan_10ms : fm_play_countdown_scan_10ms;

	gScheduleFM                 = false;
	gFM_FoundFrequency          = false;
	gAskToSave                  = false;
	gAskToDelete                = false;
	gEeprom.FM_FrequencyPlaying = Frequency;

	if (!bFlag) {
#ifdef ENABLE_FM_SI4732
		Frequency = FM_NextOn05MhzGrid(Frequency, Step);
#else
		Frequency += Step;
		if (Frequency < BK1080_GetFreqLoLimit(gEeprom.FM_Band))
			Frequency = BK1080_GetFreqHiLimit(gEeprom.FM_Band);
		else if (Frequency > BK1080_GetFreqHiLimit(gEeprom.FM_Band))
			Frequency = BK1080_GetFreqLoLimit(gEeprom.FM_Band);
#endif
		gEeprom.FM_FrequencyPlaying = Frequency;
	}

	gFM_ScanState = Step;

	BK1080_SetFrequency(gEeprom.FM_FrequencyPlaying, gEeprom.FM_Band/*, gEeprom.FM_Space*/);
}

void FM_PlayAndUpdate(void)
{
	gFM_ScanState = FM_SCAN_OFF;

	if (gFM_AutoScan) {
		gEeprom.FM_IsMrMode        = true;
		gEeprom.FM_SelectedChannel = 0;
	}

	FM_ConfigureChannelState();
	BK1080_SetFrequency(gEeprom.FM_FrequencyPlaying, gEeprom.FM_Band/*, gEeprom.FM_Space*/);
	SETTINGS_SaveFM();

	gFmPlayCountdown_10ms = 0;
	gScheduleFM           = false;
	gAskToSave            = false;

	AUDIO_AudioPathOn_FM();
	BK1080_Mute(false);

	gEnableSpeaker   = true;
}

int FM_CheckFrequencyLock(uint16_t Frequency, uint16_t LowerLimit)
{
	int ret = -1;

	const uint16_t Test2 = BK1080_ReadRegister(BK1080_REG_07);

	// This is supposed to be a signed value, but above function is unsigned
	const uint16_t Deviation = BK1080_REG_07_GET_FREQD(Test2);

	if (BK1080_REG_07_GET_SNR(Test2) <= 2) {
		goto Bail;
	}

	const uint16_t Status = BK1080_ReadRegister(BK1080_REG_10);

	if ((Status & BK1080_REG_10_MASK_AFCRL) != BK1080_REG_10_AFCRL_NOT_RAILED || BK1080_REG_10_GET_RSSI(Status) < 10) {
		goto Bail;
	}

	//if (Deviation > -281 && Deviation < 280)
	if (Deviation >= 280 && Deviation <= 3815) {
		goto Bail;
	}

	// not BLE(less than or equal)
	if (Frequency > LowerLimit && (Frequency - BK1080_BaseFrequency) == 1) {
		if (BK1080_FrequencyDeviation & 0x800 || (BK1080_FrequencyDeviation < 20))
			goto Bail;
	}

	// not BLT(less than)

	if (Frequency >= LowerLimit && (BK1080_BaseFrequency - Frequency) == 1) {
		if ((BK1080_FrequencyDeviation & 0x800) == 0 || (BK1080_FrequencyDeviation > 4075))
			goto Bail;
	}

	ret = 0;

Bail:
	BK1080_FrequencyDeviation = Deviation;
	BK1080_BaseFrequency      = Frequency;

	return ret;
}

#ifdef ENABLE_FM_SI4732
/* Parse AM frequency from current input digits (3–5 digits), clamp to 500–30000. */
static uint16_t FM_AM_ParseInputFreq(void)
{
	uint32_t v = 0;
	for (uint8_t i = 0; i < gInputBoxIndex && i < 5; i++) {
		uint8_t d = (uint8_t)gInputBox[i];
		if (d > 9) break;
		v = v * 10 + d;
	}
	if (v < 500) v = 500;
	if (v > 30000) v = 30000;
	return (uint16_t)v;
}
#endif

static void Key_DIGITS(KEY_Code_t Key, uint8_t state)
{
	enum { STATE_FREQ_MODE, STATE_MR_MODE, STATE_SAVE };

	if (state == BUTTON_EVENT_SHORT && !gWasFKeyPressed) {
		uint8_t State;

		if (gAskToDelete) {
			gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
			return;
		}

		if (gAskToSave) {
			State = STATE_SAVE;
		}
		else {
			if (gFM_ScanState != FM_SCAN_OFF) {
				gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
				return;
			}

			State = gEeprom.FM_IsMrMode ? STATE_MR_MODE : STATE_FREQ_MODE;
		}

#ifdef ENABLE_FM_SI4732
		if (State == STATE_FREQ_MODE && SI47XX_IsAMFamily() && gInputBoxIndex >= 5) {
			gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
			gRequestDisplayScreen = DISPLAY_FM;
			return;
		}
		if (State == STATE_FREQ_MODE && !SI47XX_IsAMFamily() && gInputBoxIndex >= 5) {
			gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
			gRequestDisplayScreen = DISPLAY_FM;
			return;
		}
#endif
		INPUTBOX_Append(Key);

		gRequestDisplayScreen = DISPLAY_FM;

		if (State == STATE_FREQ_MODE) {
#ifdef ENABLE_FM_SI4732
			/* AM: 3–5 digits; commit when 5 digits or EXIT. No FM first-digit rule. */
			if (SI47XX_IsAMFamily()) {
				if (gInputBoxIndex > 4) {
					gAM_FrequencyKHz = FM_AM_ParseInputFreq();
					FM_SaveAMFreqToEeprom();
					gInputBoxIndex = 0;
					SI47XX_SetFreq(gAM_FrequencyKHz);
					gUpdateStatus = true;
				}
				return;
			}
			/* FM (Si4732)：5 位数字 = XXX.XX MHz（10 kHz 单位存 EEPROM） */
			/* FM: first digit must be 0 or 1 (87–108) */
			if (gInputBoxIndex == 1) {
				if (gInputBox[0] > 1) {
					gInputBox[1] = gInputBox[0];
					gInputBox[0] = 0;
					gInputBoxIndex = 2;
				}
			}
			else if (gInputBoxIndex > 4) {
				uint32_t Frequency;

				gInputBoxIndex = 0;
				Frequency = StrToUL(INPUTBOX_GetAscii());

				if (Frequency < BK1080_GetFreqLoLimit(gEeprom.FM_Band) || BK1080_GetFreqHiLimit(gEeprom.FM_Band) < Frequency) {
					gBeepToPlay           = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
					gRequestDisplayScreen = DISPLAY_FM;
					return;
				}

				gEeprom.FM_SelectedFrequency = (uint16_t)Frequency;
#ifdef ENABLE_VOICE
				gAnotherVoiceID = (VOICE_ID_t)Key;
#endif
				gEeprom.FM_FrequencyPlaying = gEeprom.FM_SelectedFrequency;
				BK1080_SetFrequency(gEeprom.FM_FrequencyPlaying, gEeprom.FM_Band/*, gEeprom.FM_Space*/);
				gRequestSaveFM = true;
				return;
			}
#else
			/* FM: first digit must be 0 or 1 (87–108) */
			if (gInputBoxIndex == 1) {
				if (gInputBox[0] > 1) {
					gInputBox[1] = gInputBox[0];
					gInputBox[0] = 0;
					gInputBoxIndex = 2;
				}
			}
			else if (gInputBoxIndex > 3) {
				uint32_t Frequency;

				gInputBoxIndex = 0;
				Frequency = StrToUL(INPUTBOX_GetAscii());

				if (Frequency < BK1080_GetFreqLoLimit(gEeprom.FM_Band) || BK1080_GetFreqHiLimit(gEeprom.FM_Band) < Frequency) {
					gBeepToPlay           = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
					gRequestDisplayScreen = DISPLAY_FM;
					return;
				}

				gEeprom.FM_SelectedFrequency = (uint16_t)Frequency;
#ifdef ENABLE_VOICE
				gAnotherVoiceID = (VOICE_ID_t)Key;
#endif
				gEeprom.FM_FrequencyPlaying = gEeprom.FM_SelectedFrequency;
				BK1080_SetFrequency(gEeprom.FM_FrequencyPlaying, gEeprom.FM_Band/*, gEeprom.FM_Space*/);
				gRequestSaveFM = true;
				return;
			}
#endif
		}
		else if (gInputBoxIndex == 2) {
			uint8_t Channel;

			gInputBoxIndex = 0;
			Channel = ((gInputBox[0] * 10) + gInputBox[1]) - 1;

			if (State == STATE_MR_MODE) {
				if (FM_CheckValidChannel(Channel)) {
#ifdef ENABLE_VOICE
					gAnotherVoiceID = (VOICE_ID_t)Key;
#endif
					gEeprom.FM_SelectedChannel = Channel;
					gEeprom.FM_FrequencyPlaying = gFM_Channels[Channel];
					BK1080_SetFrequency(gEeprom.FM_FrequencyPlaying, gEeprom.FM_Band/*, gEeprom.FM_Space*/);
					gRequestSaveFM = true;
					return;
				}
			}
			else if (Channel < 20) {
#ifdef ENABLE_VOICE
				gAnotherVoiceID = (VOICE_ID_t)Key;
#endif
				gRequestDisplayScreen = DISPLAY_FM;
				gInputBoxIndex = 0;
				gFM_ChannelPosition = Channel;
				return;
			}

			gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
			return;
		}

#ifdef ENABLE_VOICE
		gAnotherVoiceID = (VOICE_ID_t)Key;
#endif
	}
	else
		Key_FUNC(Key, state);
}

static void Key_FUNC(KEY_Code_t Key, uint8_t state)
{
	if (state == BUTTON_EVENT_SHORT || state == BUTTON_EVENT_HELD) {
		gBeepToPlay           = BEEP_1KHZ_60MS_OPTIONAL;
		gWasFKeyPressed       = false;
		gUpdateStatus         = true;
		gRequestDisplayScreen = DISPLAY_FM;

		switch (Key) {
			case KEY_0:
				/* 仅短按 0 退出 Radio；长按 0 与其它数字键一致，无动作 */
				if (state == BUTTON_EVENT_SHORT)
					ACTION_FM();
				break;

			case KEY_1:
				gEeprom.FM_Band++;
				gRequestSaveFM = true;
				break;

			// case KEY_2:
			// 	gEeprom.FM_Space = (gEeprom.FM_Space + 1) % 3;
			// 	gRequestSaveFM = true;
			// 	break;

			case KEY_3:
#ifdef ENABLE_FM_SI4732
				/* In AM band: cycle AM → LSB → USB → CW → AM; driver loads patch when entering LSB/USB */
				if (SI47XX_IsAMFamily()) {
					SI47XX_MODE next = (si4732mode == SI47XX_AM) ? SI47XX_LSB :
						(si4732mode == SI47XX_LSB) ? SI47XX_USB :
						(si4732mode == SI47XX_USB) ? SI47XX_CW : SI47XX_AM;
					SI47XX_SwitchMode(next);
					SI47XX_SetFreq(gAM_FrequencyKHz);
					FM_ApplyAMOptions();
					gUpdateStatus = true;
					break;
				}
#endif
				gEeprom.FM_IsMrMode = !gEeprom.FM_IsMrMode;
				if (!FM_ConfigureChannelState()) {
					BK1080_SetFrequency(gEeprom.FM_FrequencyPlaying, gEeprom.FM_Band/*, gEeprom.FM_Space*/);
					gRequestSaveFM = true;
				}
				else
					gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
				break;

			case KEY_STAR:
				// 禁用 FM 搜索/扫描：仅在扫描中允许“停止”，否则提示不可用
				if (gFM_ScanState != FM_SCAN_OFF) {
					FM_PlayAndUpdate();
				} else {
					gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
				}
				break;

			default:
				gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
				break;
		}
	}
}

static void Key_EXIT(uint8_t state)
{
	if (state != BUTTON_EVENT_SHORT)
		return;

	gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;

	if (gFM_ScanState == FM_SCAN_OFF) {
		if (gInputBoxIndex == 0) {
			if (!gAskToSave && !gAskToDelete) {
				ACTION_FM();
				return;
			}

			gAskToSave   = false;
			gAskToDelete = false;
		}
		else {
#ifdef ENABLE_FM_SI4732
			/* AM: EXIT with 3–5 digits commits frequency */
			if (SI47XX_IsAMFamily() && gInputBoxIndex >= 3) {
				gAM_FrequencyKHz = FM_AM_ParseInputFreq();
				FM_SaveAMFreqToEeprom();
				gInputBoxIndex = 0;
				SI47XX_SetFreq(gAM_FrequencyKHz);
				gUpdateStatus = true;
				gRequestDisplayScreen = DISPLAY_FM;
				return;
			}
#endif
			gInputBox[--gInputBoxIndex] = 10;

			if (gInputBoxIndex) {
				if (gInputBoxIndex != 1) {
					gRequestDisplayScreen = DISPLAY_FM;
					return;
				}

				if (gInputBox[0] != 0) {
					gRequestDisplayScreen = DISPLAY_FM;
					return;
				}
			}
			gInputBoxIndex = 0;
		}

#ifdef ENABLE_VOICE
		gAnotherVoiceID = VOICE_ID_CANCEL;
#endif
	}
	else {
		FM_PlayAndUpdate();
#ifdef ENABLE_VOICE
		gAnotherVoiceID = VOICE_ID_SCANNING_STOP;
#endif
	}

	gRequestDisplayScreen = DISPLAY_FM;
}

static void Key_MENU(uint8_t state)
{
	if (state != BUTTON_EVENT_SHORT)
		return;

	// 禁用 FM 保存/删除功能（SAVE?/DEL?）
	gAskToSave = false;
	gAskToDelete = false;
	gRequestDisplayScreen = DISPLAY_FM;
	gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
}

static void Key_UP_DOWN(uint8_t state, int8_t Step)
{
	if (state == BUTTON_EVENT_PRESSED) {
		if (gInputBoxIndex) {
			gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
			return;
		}

		gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
	} else if (gInputBoxIndex || state!=BUTTON_EVENT_HELD) {
		return;
	}

	if (gAskToSave) {
		gRequestDisplayScreen = DISPLAY_FM;
		gFM_ChannelPosition   = NUMBER_AddWithWraparound(gFM_ChannelPosition, Step, 0, 19);
		return;
	}

#ifdef ENABLE_FM_SI4732
	/* AM mode: step 1/5/10/50/100/1000 kHz (STAR 改子选项), 500–30000 kHz；焦点 BFO 时改拍频 */
	if (SI47XX_IsAMFamily()) {
		if (gAM_OptionFocus == 3) {
			const int step = (state == BUTTON_EVENT_HELD) ? (Step * 10) : (Step * 5);
			int32_t b = (int32_t)gAM_BFO_Hz + (int32_t)step;
			/* ±5 步进会跳过 0：若从正/负跨过零点，先落到 0（否则如 -1+5=4 永远调不到 0） */
			if (gAM_BFO_Hz != 0 && b != 0 &&
			    ((gAM_BFO_Hz < 0 && b > 0) || (gAM_BFO_Hz > 0 && b < 0)))
				b = 0;
			if (b < -8000) b = -8000;
			else if (b > 8000) b = 8000;
			gAM_BFO_Hz = (int16_t)b;
			FM_SaveAMFreqToEeprom();
			SI47XX_SetBFO(gAM_BFO_Hz);
			gRequestDisplayScreen = DISPLAY_FM;
			gUpdateStatus = true;
			return;
		}
		uint16_t step = FM_GetAM_StepKHz();
		int32_t next = (int32_t)gAM_FrequencyKHz + (int32_t)Step * (int32_t)step;
		if (next < 500) next = 30000;
		else if (next > 30000) next = 500;
		gAM_FrequencyKHz = (uint16_t)next;
		FM_SaveAMFreqToEeprom();
		SI47XX_SetFreq(gAM_FrequencyKHz);
		gRequestDisplayScreen = DISPLAY_FM;
		gUpdateStatus = true;
		return;
	}
#endif

	if (gFM_ScanState != FM_SCAN_OFF) {
		if (gFM_AutoScan) {
			gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
			return;
		}

		FM_Tune(gEeprom.FM_FrequencyPlaying, Step, false);
		gRequestDisplayScreen = DISPLAY_FM;
		return;
	}

	if (gEeprom.FM_IsMrMode) {
		const uint8_t Channel = FM_FindNextChannel(gEeprom.FM_SelectedChannel + Step, Step);
		if (Channel == 0xFF || gEeprom.FM_SelectedChannel == Channel)
			goto Bail;

		gEeprom.FM_SelectedChannel  = Channel;
		gEeprom.FM_FrequencyPlaying = gFM_Channels[Channel];
	}
	else {
#ifdef ENABLE_FM_SI4732
		gEeprom.FM_SelectedFrequency = FM_NextOn05MhzGrid(gEeprom.FM_SelectedFrequency, Step);
#else
		uint16_t Frequency = gEeprom.FM_SelectedFrequency + Step;

		if (Frequency < BK1080_GetFreqLoLimit(gEeprom.FM_Band))
			Frequency = BK1080_GetFreqHiLimit(gEeprom.FM_Band);
		else if (Frequency > BK1080_GetFreqHiLimit(gEeprom.FM_Band))
			Frequency = BK1080_GetFreqLoLimit(gEeprom.FM_Band);
		gEeprom.FM_SelectedFrequency = Frequency;
#endif
		gEeprom.FM_FrequencyPlaying  = gEeprom.FM_SelectedFrequency;
	}

	gRequestSaveFM = true;

Bail:
	BK1080_SetFrequency(gEeprom.FM_FrequencyPlaying, gEeprom.FM_Band/*, gEeprom.FM_Space*/);

	gRequestDisplayScreen = DISPLAY_FM;
}

void FM_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{
	uint8_t state = bKeyPressed + 2 * bKeyHeld;

	switch (Key) {
		case KEY_0:
			Key_DIGITS(Key, state);
			break;
		case KEY_1: case KEY_2: case KEY_3: case KEY_4: case KEY_5: case KEY_6: case KEY_7: case KEY_8: case KEY_9:
			Key_DIGITS(Key, state);
			break;
		case KEY_STAR:
#ifdef ENABLE_FM_SI4732
			if (SI47XX_IsAMFamily() && gInputBoxIndex == 0 && state == BUTTON_EVENT_SHORT) {
				/* 短按 * 修改当前焦点对应的子选项 */
				switch (gAM_OptionFocus) {
				case 0:
					gAM_LnaIndex = (uint8_t)((gAM_LnaIndex + 1) % 6);
					SI47XX_SetAMLna(gAM_LnaIndex);
					FM_SaveAMFreqToEeprom();
					break;
				case 1:
					gAM_BW_Index = (gAM_BW_Index + 1) % 7;
					SI47XX_SetAMBandwidth(gAM_BW_Index);
					break;
				case 2:
					gAM_StepIndex = (gAM_StepIndex + 1) % AM_STEP_COUNT;
					break;
				case 3:
					gAM_BFO_Hz = 0;
					SI47XX_SetBFO(0);
					FM_SaveAMFreqToEeprom();
					break;
				}
				gUpdateStatus = true;
				gRequestDisplayScreen = DISPLAY_FM;
				gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
				break;
			}
#endif
			Key_FUNC(Key, state);
			break;
		case KEY_MENU:
#ifdef ENABLE_FM_SI4732
			if (SI47XX_IsAMFamily()) {
				/* 中短波下：短按 M 在 LNA/BW/STP/BFO 间切换，长按 M 无效 */
				if (bKeyPressed && !bKeyHeld) {
					gAM_OptionFocus = (gAM_OptionFocus + 1) % 4;
					gRequestDisplayScreen = DISPLAY_FM;
					gUpdateStatus = true;
					gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
					break;
				}
				if (bKeyHeld) {
					break;
				}
				if (!bKeyPressed) {
					break;
				}
			}
#endif
			Key_MENU(state);
			break;
		case KEY_UP:
			Key_UP_DOWN(state, 1);
			break;
		case KEY_DOWN:
			Key_UP_DOWN(state, -1);
			break;;
		case KEY_EXIT:
			Key_EXIT(state);
			break;
		case KEY_F:
#ifdef ENABLE_FM_SI4732
			if (!bKeyPressed) {
				/* 松键：若本次未触发长按，则视为短按（按下立即松开） */
				if (!gFKeyLongPressDone) {
					if (si4732mode == SI47XX_FM) {
						SI47XX_SwitchMode(SI47XX_AM);
						if (gAM_FrequencyKHz < 500) gAM_FrequencyKHz = 500;
						if (gAM_FrequencyKHz > 30000) gAM_FrequencyKHz = 30000;
						FM_SaveAMFreqToEeprom();
						SI47XX_SetFreq(gAM_FrequencyKHz);
						FM_ApplyAMOptions();
						gUpdateStatus = true;
					} else if (si4732mode == SI47XX_AM) {
						SI47XX_SwitchMode(SI47XX_FM);
						SI47XX_SetFreq(gEeprom.FM_FrequencyPlaying);
						gUpdateStatus = true;
					} else if (si4732mode == SI47XX_LSB || si4732mode == SI47XX_USB || si4732mode == SI47XX_CW) {
						SI47XX_MODE next = (si4732mode == SI47XX_USB) ? SI47XX_LSB :
							(si4732mode == SI47XX_LSB) ? SI47XX_CW : SI47XX_USB;
						SI47XX_SwitchMode(next);
						SI47XX_SetFreq(gAM_FrequencyKHz);
						FM_ApplyAMOptions();
						gUpdateStatus = true;
					}
					gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
				}
				gFKeyJustEnteredSSB = false;
				gFKeyLongPressDone = false;
			} else if (bKeyHeld) {
				/* 长按：达到长按标准后只触发一次，同一按下期间不再重复 */
				if (!gFKeyLongPressDone) {
					if (si4732mode == SI47XX_LSB || si4732mode == SI47XX_USB || si4732mode == SI47XX_CW) {
						if (!gFKeyJustEnteredSSB) {
							SI47XX_SwitchMode(SI47XX_AM);
							SI47XX_SetFreq(gAM_FrequencyKHz);
							FM_ApplyAMOptions();
							gUpdateStatus = true;
							gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
						}
					} else if (si4732mode == SI47XX_FM || si4732mode == SI47XX_AM) {
						if (gAM_FrequencyKHz < 500) gAM_FrequencyKHz = 500;
						if (gAM_FrequencyKHz > 30000) gAM_FrequencyKHz = 30000;
						FM_SaveAMFreqToEeprom();
						UI_DisplayFM();
						UI_DisplayFmWait();
						ST7565_BlitFullScreen();
						SI47XX_SwitchMode(SI47XX_USB);
						SI47XX_SetFreq(gAM_FrequencyKHz);
						FM_ApplyAMOptions();
						gUpdateStatus = true;
						gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
						gFKeyJustEnteredSSB = true;
					} else {
						GENERIC_Key_F(bKeyPressed, bKeyHeld);
					}
					gFKeyLongPressDone = true;
				}
			}
			/* 仅按下未达长按时不处理，等松键再判短按 */
			break;
#else
			GENERIC_Key_F(bKeyPressed, bKeyHeld);
			break;
#endif
		case KEY_PTT:
			/* In Radio mode, PTT = EXIT (e.g. exit FM screen, clear input) */
			if (bKeyPressed)
				Key_EXIT(BUTTON_EVENT_SHORT);
			break;
		default:
			if (!bKeyHeld && bKeyPressed)
				gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
			break;
	}
}

void FM_Play(void)
{
	if (!FM_CheckFrequencyLock(gEeprom.FM_FrequencyPlaying, BK1080_GetFreqLoLimit(gEeprom.FM_Band))) {
		if (!gFM_AutoScan) {
			gFmPlayCountdown_10ms = 0;
			gFM_FoundFrequency    = true;

			if (!gEeprom.FM_IsMrMode)
				gEeprom.FM_SelectedFrequency = gEeprom.FM_FrequencyPlaying;

			AUDIO_AudioPathOn_FM();
			gEnableSpeaker = true;

			GUI_SelectNextDisplay(DISPLAY_FM);
			return;
		}

		if (gFM_ChannelPosition < 20)
			gFM_Channels[gFM_ChannelPosition++] = gEeprom.FM_FrequencyPlaying;

		if (gFM_ChannelPosition >= 20) {
			FM_PlayAndUpdate();
			GUI_SelectNextDisplay(DISPLAY_FM);
			return;
		}
	}

	if (gFM_AutoScan && gEeprom.FM_FrequencyPlaying >= BK1080_GetFreqHiLimit(1))
		FM_PlayAndUpdate();
	else
		FM_Tune(gEeprom.FM_FrequencyPlaying, gFM_ScanState, false);

	GUI_SelectNextDisplay(DISPLAY_FM);
}

/* Si4732 audio vs kk:
 * - kk (SI screen): SI_init() → BK4819_Disable(), SI47XX_PowerUp(); PowerUp does
 *   AUDIO_AudioPathOn() after 500ms then setVolume(63), no mute, then SetFreq.
 *   kk does not set gEnableSpeaker in SI_init; path stays on until PowerDown.
 * - kk defaults to AM (band list / last mode); this project is FM-only, default FM.
 * - Here: AudioPathOff, BK4819_SetAF(MUTE), Init (path on inside + volume, no mute), then
 *   gEnableSpeaker, delay, AudioPathOn(), BK1080_Mute(false). */
void FM_Start(void)
{
	gDualWatchActive 		  = false;
	gFmRadioMode              = true;
	gFM_ScanState             = FM_SCAN_OFF;
	gFM_RestoreCountdown_10ms = 0;

	AUDIO_AudioPathOff_FM();
	BK4819_SetAF(BK4819_AF_MUTE); /* only Si4732 drives audio; kk uses BK4819_Disable() in SI_init */
	BK1080_Init(gEeprom.FM_FrequencyPlaying, gEeprom.FM_Band/*, gEeprom.FM_Space*/);
	/* Audio path is switched on inside Si4732 init; set speaker flag before delay
	 * so nothing turns path off during the next 100ms (e.g. AUDIO_PlayQueuedVoice). */
	gEnableSpeaker = true;
	SYSTEM_DelayMs(100);
	AUDIO_AudioPathOn_FM(); /* ensure path to Si4732 before unmute */
	BK1080_Mute(false);

	gUpdateStatus = true;
}

#endif
