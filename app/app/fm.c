#ifdef ENABLE_FMRADIO

#include <stdint.h>
#include <string.h>

#include "app/action.h"
#include "app/fm.h"
#include "app/generic.h"
#include "audio.h"
#include "driver/bk1080.h"
#include "driver/bk4819.h"
#include "driver/eeprom.h"
#include "driver/py25q16.h"
#define FM_EEPROM_AM_KHZ 0x0E68U
#ifdef ENABLE_FM_SI4732
#include "driver/si473x.h"
#include "driver/st7565.h"
#include "driver/system.h"
#include "ui/fmradio.h"
#endif
#include "driver/gpio.h"
#include "functions.h"
#include "misc.h"
#include "settings.h"
#include "ui/inputbox.h"
#include "ui/ui.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))
#endif

#ifdef ENABLE_FM_SI4732

uint16_t          gFM_Channels[FM_CHANNELS_MAX];
bool              gFmRadioMode;
uint8_t           gFmRadioCountdown_500ms;
volatile uint16_t gFmPlayCountdown_10ms;
volatile int8_t   gFM_ScanState;
bool              gFM_AutoScan;
uint8_t           gFM_ChannelPosition;
bool              gFM_FoundFrequency;
bool              gFM_AutoScan;
uint16_t          gFM_RestoreCountdown_10ms;

/* FM 步进（单位 0.1MHz）：0=0.1, 1=0.5, 2=1.0 */
static uint8_t gFM_StepIndex = 0;
static const uint16_t gFM_Step10_Table[] = { 1, 5, 10 };

uint16_t FM_GetFM_Step10(void)
{
	const uint8_t idx = (gFM_StepIndex < (uint8_t)ARRAY_SIZE(gFM_Step10_Table)) ? gFM_StepIndex : 0;
	return gFM_Step10_Table[idx];
}

#ifdef ENABLE_FM_SI4732
/* AM mode: current frequency in kHz (500–30000, MW+SW). Used when si4732mode == SI47XX_AM. */
static uint16_t gAM_FrequencyKHz = 720;
/* AM 底部选项：短按 M 切换焦点 0=LNA 1=BW 2=STP 3=BFO；短按 * 循环当前子选项（BFO 用上下键） */
static uint8_t gAM_OptionFocus = 0;
/* LNA：0=AGC ON，1..5=AGC OFF + ATT 0,1,5,15,26 dB */
static uint8_t gAM_LnaIndex = 0;
static uint8_t gAM_BW_Index = 2;    /* 0..6 = 0.5,1,1.2,2.2,3,4,5 kHz，默认 1.2 */
static int16_t gAM_BfoHz = 0;       /* BFO 偏置 Hz，单边带/中短波 UI 统调 */
/* Step index 0..4 = 1, 5, 10, 100, 1000 kHz，显示 1K 5K 10K 100K 1000K */
static uint8_t gAM_StepIndex = 0;   /* 默认 1k */
/* 长按 F 刚进入单边带时置位，松键清除；避免同一长按的后续 held 事件立刻触发“退回 AM” */
static bool gFKeyJustEnteredSSB = false;
/* 本次 F 键已触发长按，松键前不再触发短按；松键时清除 */
static bool gFKeyLongPressDone = false;

static const uint16_t gAM_StepKHzTable[] = { 1, 5, 10, 100, 1000 };
#define AM_STEP_COUNT ((unsigned)ARRAY_SIZE(gAM_StepKHzTable))

static void FM_AM_ApplyDefaultBwForMode(void)
{
	/* 默认：CW=1.2k(索引2)，USB/LSB=2.2k(索引3) */
	if (si4732mode == SI47XX_AM) {
		/* 进入 AM：BFO 强制归零（AM 不需要拍频） */
		gAM_BfoHz = 0;
	} else if (si4732mode == SI47XX_CW) {
		gAM_BW_Index = 2;
		/* 进入 CW：BFO 默认归零（与 CEC 一致；拍频由用户手动调整） */
		gAM_BfoHz = 0;
	} else if (si4732mode == SI47XX_USB || si4732mode == SI47XX_LSB) {
		gAM_BW_Index = 3;
	}
}

uint16_t FM_GetAM_StepKHz(void)
{
	return gAM_StepKHzTable[gAM_StepIndex < AM_STEP_COUNT ? gAM_StepIndex : 0];
}

uint8_t FM_GetAM_OptionFocus(void) { return gAM_OptionFocus; }
uint8_t FM_GetAM_LnaIndex(void)   { return gAM_LnaIndex; }
uint8_t FM_GetAM_BW_Index(void)   { return gAM_BW_Index; }
uint8_t FM_GetAM_StepIndex(void)   { return gAM_StepIndex; }
int16_t FM_GetAM_BfoHz(void)      { return gAM_BfoHz; }

static void FM_ApplyAMOptions(void) {
	SI47XX_SetAMLna(gAM_LnaIndex);
	SI47XX_SetAMBandwidth(gAM_BW_Index);
	SI47XX_ApplyRxBfo(gAM_BfoHz);
	if (SI47XX_IsSSB() || si4732mode == SI47XX_CW)
		SI47XX_ApplySsbAudioProfile();
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
	buf[2] = (uint8_t)((uint16_t)gAM_BfoHz & 0xFF);
	buf[3] = (uint8_t)((uint16_t)gAM_BfoHz >> 8);
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
	gAM_BfoHz = (int16_t)((uint16_t)buf[2] | ((uint16_t)buf[3] << 8));
	if (buf[4] <= 5)
		gAM_LnaIndex = buf[4];
	else
		gAM_LnaIndex = 0;
}
#endif

const uint8_t BUTTON_STATE_PRESSED = 1 << 0;
const uint8_t BUTTON_STATE_HELD = 1 << 1;

const uint8_t BUTTON_EVENT_PRESSED = BUTTON_STATE_PRESSED;
const uint8_t BUTTON_EVENT_HELD = BUTTON_STATE_PRESSED | BUTTON_STATE_HELD;
const uint8_t BUTTON_EVENT_SHORT =  0;
const uint8_t BUTTON_EVENT_LONG =  BUTTON_STATE_HELD;


static void Key_FUNC(KEY_Code_t Key, uint8_t state);

/* 30 秒窗口内每 2 秒自动刷新 RSSI（仅用于 DISPLAY_FM 画面） */
static uint16_t gFM_AutoRssiWindow_10ms = 0;
static uint16_t gFM_AutoRssiNext_10ms = 0;

void FM_TimeSlice10ms(void)
{
	/* 30 秒后回到“无操作不刷新”的现有逻辑 */
	if (gFM_AutoRssiWindow_10ms == 0)
		return;

	gFM_AutoRssiWindow_10ms--;
	if (gFM_AutoRssiNext_10ms > 0)
		gFM_AutoRssiNext_10ms--;

	if (gFM_AutoRssiNext_10ms == 0) {
		/* 触发一次重绘，从而更新 RSSI */
		gUpdateDisplay = true;
		gFM_AutoRssiNext_10ms = 2U * 100U;
	}
}

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

#ifdef ENABLE_FEAT_F4HWN_RESUME_STATE
	gEeprom.CURRENT_STATE = 0;
	SETTINGS_WriteCurrentState();
#endif
}

void FM_EraseChannels(void)
{
	uint8_t      Template[8];
	memset(Template, 0xFF, sizeof(Template));

	for (unsigned i = 0; i < 5; i++)
		EEPROM_WriteBuffer(0x0E40 + (i * 8), Template);

	memset(gFM_Channels, 0xFF, sizeof(gFM_Channels));
}

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
		Frequency += Step;
		if (Frequency < BK1080_GetFreqLoLimit(gEeprom.FM_Band))
			Frequency = BK1080_GetFreqHiLimit(gEeprom.FM_Band);
		else if (Frequency > BK1080_GetFreqHiLimit(gEeprom.FM_Band))
			Frequency = BK1080_GetFreqLoLimit(gEeprom.FM_Band);

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
#endif
		INPUTBOX_Append(Key);

		gRequestDisplayScreen = DISPLAY_FM;

		if (State == STATE_FREQ_MODE) {
#ifdef ENABLE_FM_SI4732
			/* AM: 3–5 digits; commit when 5 digits or EXIT. No FM first-digit rule. */
			if (SI47XX_IsAMFamily()) {
				/* 输入首位规则：
				 * - 若首位 > 2：自动补前导 0 → 显示 0X.---，等待剩余 3 位（总共 5 位）
				 * - 若首位 <= 2：保持原样 → 显示 X-.---，等待剩余 4 位
				 */
				if (gInputBoxIndex == 1 && gInputBox[0] > 2) {
					gInputBox[1] = gInputBox[0];
					gInputBox[0] = 0;
					gInputBoxIndex = 2;
				}

				if (gInputBoxIndex > 4) {
					gAM_FrequencyKHz = FM_AM_ParseInputFreq();
					FM_SaveAMFreqToEeprom();
					gInputBoxIndex = 0;
					SI47XX_SetFreq(gAM_FrequencyKHz);
					FM_ApplyAMOptions();
					gUpdateStatus = true;
				}
				return;
			}
#endif
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
			else if (Channel < FM_CHANNELS_MAX) {
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
					FM_AM_ApplyDefaultBwForMode();
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
				/* 禁用 FM 搜索/扫描：仅在扫描中允许“停止”，否则提示不可用 */
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
			/* AM: 仅在 5 位输入完成时提交，否则当作退格 */
			if (SI47XX_IsAMFamily() && gInputBoxIndex == 5) {
				gAM_FrequencyKHz = FM_AM_ParseInputFreq();
				FM_SaveAMFreqToEeprom();
				gInputBoxIndex = 0;
				SI47XX_SetFreq(gAM_FrequencyKHz);
				FM_ApplyAMOptions();
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
	if (SI47XX_IsAMFamily()) {
		uint16_t step = FM_GetAM_StepKHz();
		int32_t next = (int32_t)gAM_FrequencyKHz + (int32_t)Step * (int32_t)step;
		if (next < 500) next = 30000;
		else if (next > 30000) next = 500;
		gAM_FrequencyKHz = (uint16_t)next;
		FM_SaveAMFreqToEeprom();
		SI47XX_SetFreq(gAM_FrequencyKHz);
		FM_ApplyAMOptions();
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
		const uint16_t step10 = FM_GetFM_Step10();
		uint16_t Frequency = (uint16_t)(gEeprom.FM_SelectedFrequency + Step * (int16_t)step10);

		if (Frequency < BK1080_GetFreqLoLimit(gEeprom.FM_Band))
			Frequency = BK1080_GetFreqHiLimit(gEeprom.FM_Band);
		else if (Frequency > BK1080_GetFreqHiLimit(gEeprom.FM_Band))
			Frequency = BK1080_GetFreqLoLimit(gEeprom.FM_Band);

		gEeprom.FM_FrequencyPlaying  = Frequency;
		gEeprom.FM_SelectedFrequency = gEeprom.FM_FrequencyPlaying;
	}

	gRequestSaveFM = true;

Bail:
	BK1080_SetFrequency(gEeprom.FM_FrequencyPlaying, gEeprom.FM_Band/*, gEeprom.FM_Space*/);

	gRequestDisplayScreen = DISPLAY_FM;
}

#ifdef ENABLE_FM_SI4732
static void FM_AM_AdjustFocusedOption(int8_t step)
{
	switch (gAM_OptionFocus) {
	case 0: /* LNA */
		if (step > 0) gAM_LnaIndex = (uint8_t)((gAM_LnaIndex + 1) % 6);
		else gAM_LnaIndex = (uint8_t)((gAM_LnaIndex + 5) % 6);
		FM_ApplyAMOptions();
		FM_SaveAMFreqToEeprom();
		break;
	case 1: /* BW */
		if (step > 0) gAM_BW_Index = (uint8_t)((gAM_BW_Index + 1) % 7);
		else gAM_BW_Index = (uint8_t)((gAM_BW_Index + 6) % 7);
		FM_ApplyAMOptions();
		FM_SaveAMFreqToEeprom();
		break;
	case 2: /* STP */
		if (step > 0) gAM_StepIndex = (uint8_t)((gAM_StepIndex + 1) % AM_STEP_COUNT);
		else gAM_StepIndex = (uint8_t)((gAM_StepIndex + AM_STEP_COUNT - 1) % AM_STEP_COUNT);
		/* STP 本身不需下发到芯片，只影响调谐步进 */
		FM_SaveAMFreqToEeprom();
		break;
	case 3: /* BFO */
	default:
		/* BFO 步长：5 Hz */
		gAM_BfoHz += (int16_t)(step * 5);
		SI47XX_ApplyRxBfo(gAM_BfoHz);
		FM_SaveAMFreqToEeprom();
		break;
	}
	gRequestDisplayScreen = DISPLAY_FM;
	gUpdateStatus = true;
}
#endif

void FM_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{
	uint8_t state = bKeyPressed + 2 * bKeyHeld;

	if (bKeyPressed) {
		gFM_AutoRssiWindow_10ms = 30U * 100U;
		gFM_AutoRssiNext_10ms = 2U * 100U;
	}

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
				/* * 键始终调整 STP（循环） */
				gAM_StepIndex = (uint8_t)((gAM_StepIndex + 1) % AM_STEP_COUNT);
				FM_SaveAMFreqToEeprom();
				gUpdateStatus = true;
				gRequestDisplayScreen = DISPLAY_FM;
				gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
				break;
			}
#endif
			/* FM：* 键循环 STP（0.1/0.5/1.0MHz） */
			if (gInputBoxIndex == 0 && state == BUTTON_EVENT_SHORT && gFM_ScanState == FM_SCAN_OFF) {
				gFM_StepIndex = (uint8_t)((gFM_StepIndex + 1) % (uint8_t)ARRAY_SIZE(gFM_Step10_Table));
				gUpdateStatus = true;
				gRequestDisplayScreen = DISPLAY_FM;
				gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
				break;
			}
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
		case KEY_SIDE1:
		case KEY_SIDE2:
#ifdef ENABLE_FM_SI4732
			/* AM/SSB：侧键用于调整当前焦点（LNA/BW/STP/BFO）。 */
			if (SI47XX_IsAMFamily()) {
				if (gInputBoxIndex == 0 && (state == BUTTON_EVENT_SHORT || state == BUTTON_EVENT_HELD)) {
					const int8_t step = (Key == KEY_SIDE1) ? 1 : -1;
					FM_AM_AdjustFocusedOption(step);
					gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
					break;
				}
				if (!bKeyHeld && bKeyPressed)
					gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
				break;
			}
#endif
			if (!bKeyHeld && bKeyPressed)
				gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
			break;
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
						FM_AM_ApplyDefaultBwForMode();
						FM_SaveAMFreqToEeprom();
						SI47XX_SetFreq(gAM_FrequencyKHz);
						FM_ApplyAMOptions();
						gUpdateStatus = true;
					} else if (si4732mode == SI47XX_AM) {
						SI47XX_SwitchMode(SI47XX_FM);
						SI47XX_SetFreq((uint16_t)((unsigned long)gEeprom.FM_FrequencyPlaying * 10U));
						gUpdateStatus = true;
					} else if (si4732mode == SI47XX_LSB || si4732mode == SI47XX_USB || si4732mode == SI47XX_CW) {
						SI47XX_MODE next = (si4732mode == SI47XX_USB) ? SI47XX_LSB :
							(si4732mode == SI47XX_LSB) ? SI47XX_CW : SI47XX_USB;
						SI47XX_SwitchMode(next);
						FM_AM_ApplyDefaultBwForMode();
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
						FM_AM_ApplyDefaultBwForMode();
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

		if (gFM_ChannelPosition < FM_CHANNELS_MAX)
			gFM_Channels[gFM_ChannelPosition++] = gEeprom.FM_FrequencyPlaying;

		if (gFM_ChannelPosition >= FM_CHANNELS_MAX) {
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

#ifdef ENABLE_FM_SI4732
	AUDIO_AudioPathOff_FM();
	BK4819_SetAF(BK4819_AF_MUTE); /* only Si4732 drives audio; kk uses BK4819_Disable() in SI_init */
	BK1080_Init(gEeprom.FM_FrequencyPlaying, gEeprom.FM_Band/*, gEeprom.FM_Space*/);
	/* Audio path is switched on inside Si4732 init; set speaker flag before delay
	 * so nothing turns path off during the next 100ms (e.g. AUDIO_PlayQueuedVoice). */
	gEnableSpeaker = true;
	SYSTEM_DelayMs(100);
	AUDIO_AudioPathOn_FM(); /* ensure path to Si4732 before unmute */
	BK1080_Mute(false);
#else
	BK1080_Init(gEeprom.FM_FrequencyPlaying, gEeprom.FM_Band/*, gEeprom.FM_Space*/);
	AUDIO_AudioPathOn_FM();
	gEnableSpeaker       = true;
#endif

	gUpdateStatus = true;

#ifdef ENABLE_FEAT_F4HWN_RESUME_STATE
	gEeprom.CURRENT_STATE = 3;
	SETTINGS_WriteCurrentState();
#endif
}
#else

uint16_t          gFM_Channels[FM_CHANNELS_MAX];
bool              gFmRadioMode;
uint8_t           gFmRadioCountdown_500ms;
volatile uint16_t gFmPlayCountdown_10ms;
volatile int8_t   gFM_ScanState;
bool              gFM_AutoScan;
uint8_t           gFM_ChannelPosition;
bool              gFM_FoundFrequency;
bool              gFM_AutoScan;
uint16_t          gFM_RestoreCountdown_10ms;



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
    gFmRadioMode              = false;
    gFM_ScanState             = FM_SCAN_OFF;
    gFM_RestoreCountdown_10ms = 0;

    AUDIO_AudioPathOff();
    gEnableSpeaker = false;

    BK1080_Init0();

    // Enable relevant LNA based on VFO frequency
    BK4819_PickRXFilterPathBasedOnFrequency(gRxVfo->freq_config_RX.Frequency);


    gUpdateStatus  = true;

    #ifdef ENABLE_FEAT_F4HWN_RESUME_STATE
        gEeprom.CURRENT_STATE = 0;
        SETTINGS_WriteCurrentState();
    #endif
}

void FM_EraseChannels(void)
{
    //PY25Q16_SectorErase(0x003000);
    
    uint8_t clearBuf[128];
    memset(clearBuf, 0xFF, sizeof(clearBuf));
    PY25Q16_WriteBuffer(0x00A028, clearBuf, sizeof(clearBuf), false);

    memset(gFM_Channels, 0xFF, sizeof(gFM_Channels));
}

uint16_t FM_WrapFrequency(uint16_t Frequency) {
    const uint16_t freqLoLimit = BK1080_GetFreqLoLimit(gEeprom.FM_Band);
    const uint16_t freqHiLimit = BK1080_GetFreqHiLimit(gEeprom.FM_Band);

    if (Frequency < freqLoLimit)
        return freqHiLimit;
    else if (Frequency > freqHiLimit)
        return freqLoLimit;

    return Frequency;
}

void FM_Tune(uint16_t Frequency, int8_t Step, bool bFlag)
{
    AUDIO_AudioPathOff();

    gEnableSpeaker = false;

    gFmPlayCountdown_10ms = (gFM_ScanState == FM_SCAN_OFF) ? fm_play_countdown_noscan_10ms : fm_play_countdown_scan_10ms;

    gScheduleFM                 = false;
    gFM_FoundFrequency          = false;
    gAskToSave                  = false;
    gAskToDelete                = false;
    gEeprom.FM_FrequencyPlaying = Frequency;

    if (!bFlag) {
        Frequency += Step;
        Frequency = FM_WrapFrequency(Frequency);

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

    AUDIO_AudioPathOn();

    gEnableSpeaker   = true;
}

int FM_CheckFrequencyLock(uint16_t Frequency, uint16_t LowerLimit)
{
    const uint16_t Test2     = BK1080_ReadRegister(BK1080_REG_07);
    const uint16_t Deviation = BK1080_REG_07_GET_FREQD(Test2);

    // Helper macro to update globals and return
    #define RETURN(val) \
        do { \
            BK1080_FrequencyDeviation = Deviation; \
            BK1080_BaseFrequency      = Frequency; \
            return (val); \
        } while (0)

    if (BK1080_REG_07_GET_SNR(Test2) <= 2)
        RETURN(-1);

    const uint16_t Status = BK1080_ReadRegister(BK1080_REG_10);
    if ((Status & BK1080_REG_10_MASK_AFCRL) != BK1080_REG_10_AFCRL_NOT_RAILED ||
        BK1080_REG_10_GET_RSSI(Status) < 10)
        RETURN(-1);

    if (Deviation >= 280 && Deviation <= 3815)
        RETURN(-1);

    // Scanning upward: previous deviation was negative (bit 11 set) or near zero
    if (Frequency > LowerLimit && (Frequency - BK1080_BaseFrequency) == 1) {
        if (BK1080_FrequencyDeviation & 0x800 || BK1080_FrequencyDeviation < 20)
            RETURN(-1);
    }

    // Scanning downward: previous deviation was positive or saturated high
    if (Frequency >= LowerLimit && (BK1080_BaseFrequency - Frequency) == 1) {
        if ((BK1080_FrequencyDeviation & 0x800) == 0 || BK1080_FrequencyDeviation > 4075)
            RETURN(-1);
    }

    #undef RETURN

    BK1080_FrequencyDeviation = Deviation;
    BK1080_BaseFrequency      = Frequency;
    return 0;
}

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

        INPUTBOX_Append(Key);
        gKeyInputCountdown = key_input_timeout_500ms;

        gRequestDisplayScreen = DISPLAY_FM;

        if (State == STATE_FREQ_MODE) {
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
                gKeyInputCountdown = 1;

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
        }
        else if (gInputBoxIndex == 2) {
            uint8_t Channel;

            gInputBoxIndex = 0;
            gKeyInputCountdown = 1;
            
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
            else if (Channel < FM_CHANNELS_MAX) {
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
        bool autoScan = gWasFKeyPressed || (state == BUTTON_EVENT_HELD);

        gBeepToPlay           = BEEP_1KHZ_60MS_OPTIONAL;
        HideFKeyIcon();
        gRequestDisplayScreen = DISPLAY_FM;

        switch (Key) {
            case KEY_0:
                ACTION_FM();
                break;

            case KEY_1:
                gEeprom.FM_Band++;
                gRequestSaveFM = true;
                break;

            // case KEY_2:
            //  gEeprom.FM_Space = (gEeprom.FM_Space + 1) % 3;
            //  gRequestSaveFM = true;
            //  break;

            case KEY_3:
                gEeprom.FM_IsMrMode = !gEeprom.FM_IsMrMode;

                if (!FM_ConfigureChannelState()) {
                    BK1080_SetFrequency(gEeprom.FM_FrequencyPlaying, gEeprom.FM_Band/*, gEeprom.FM_Space*/);
                    gRequestSaveFM = true;
                }
                else
                    gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
                break;

            case KEY_8:
                ACTION_BackLightOnDemand();
                break;

            case KEY_9:
                ACTION_BackLight();
                break;

            case KEY_STAR:
                ACTION_Scan(autoScan);
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
            gInputBox[--gInputBoxIndex] = 10;
            gKeyInputCountdown = key_input_timeout_500ms;

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
    if (state == BUTTON_EVENT_HELD) {
        ACTION_Handle(KEY_MENU, true, true);
        return;
    }
    else if (state != BUTTON_EVENT_SHORT) {
        return;
    }

    gRequestDisplayScreen = DISPLAY_FM;
    gBeepToPlay           = BEEP_1KHZ_60MS_OPTIONAL;

    HideFKeyIcon();

    if (gFM_ScanState == FM_SCAN_OFF) {
        if (gInputBoxIndex) {
            gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
            return;
        }

        if (!gEeprom.FM_IsMrMode) {
            if (gAskToSave) {
                gFM_Channels[gFM_ChannelPosition] = gEeprom.FM_FrequencyPlaying;
                gRequestSaveFM = true;
            }
            gAskToSave = !gAskToSave;
        }
        else {
            if (gAskToDelete) {
                gFM_Channels[gEeprom.FM_SelectedChannel] = 0xFFFF;

                FM_ConfigureChannelState();
                BK1080_SetFrequency(gEeprom.FM_FrequencyPlaying, gEeprom.FM_Band/*, gEeprom.FM_Space*/);

                gRequestSaveFM = true;
            }
            gAskToDelete = !gAskToDelete;
        }
    }
    else {
        if (gFM_AutoScan || !gFM_FoundFrequency) {
            gBeepToPlay    = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
            gInputBoxIndex = 0;
            return;
        }

        if (gAskToSave) {
            gFM_Channels[gFM_ChannelPosition] = gEeprom.FM_FrequencyPlaying;
            gRequestSaveFM = true;
        }
        gAskToSave = !gAskToSave;
    }
}

static void Key_UP_DOWN(uint8_t state, int8_t Step)
{
    HideFKeyIcon();

    if (state == BUTTON_EVENT_PRESSED) {
        if (gInputBoxIndex) {
            gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
            return;
        }

        gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
    } else if (gInputBoxIndex || state!=BUTTON_EVENT_HELD) {
        return;
    }

    if (!gEeprom.SET_NAV) {
        Step = -Step;
    }

    if (gAskToSave) {
        gRequestDisplayScreen = DISPLAY_FM;
        gFM_ChannelPosition   = NUMBER_AddWithWraparound(gFM_ChannelPosition, Step, 0, FM_CHANNELS_MAX - 1);
        return;
    }

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
        uint16_t Frequency = gEeprom.FM_SelectedFrequency + Step;

        Frequency = FM_WrapFrequency(Frequency);

        gEeprom.FM_FrequencyPlaying  = Frequency;
        gEeprom.FM_SelectedFrequency = gEeprom.FM_FrequencyPlaying;
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
        case KEY_0...KEY_9:
            Key_DIGITS(Key, state);
            break;
        case KEY_STAR:
            Key_FUNC(Key, state);
            break;
        case KEY_MENU:
            Key_MENU(state);
            break;
        case KEY_UP:
        case KEY_DOWN:
            Key_UP_DOWN(state, Key == KEY_UP ? 1 : -1);
            break;
        case KEY_EXIT:
            Key_EXIT(state);
            break;
        case KEY_F:
            GENERIC_Key_F(bKeyPressed, bKeyHeld);
            break;
        case KEY_PTT:
            GENERIC_Key_PTT(bKeyPressed);
            break;
        case KEY_SIDE1:
        case KEY_SIDE2:
            if (state != BUTTON_EVENT_PRESSED) {
                gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
                HideFKeyIcon();
            }
            break;
        default:
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

            AUDIO_AudioPathOn();
            gEnableSpeaker = true;

            GUI_SelectNextDisplay(DISPLAY_FM);
            return;
        }

        if (gFM_ChannelPosition < FM_CHANNELS_MAX)
            gFM_Channels[gFM_ChannelPosition++] = gEeprom.FM_FrequencyPlaying;

        if (gFM_ChannelPosition >= FM_CHANNELS_MAX) {
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

void FM_Start(void)
{
    gDualWatchActive          = false;
    gFmRadioMode              = true;
    gFM_ScanState             = FM_SCAN_OFF;
    gFM_RestoreCountdown_10ms = 0;

    BK1080_Init(gEeprom.FM_FrequencyPlaying, gEeprom.FM_Band/*, gEeprom.FM_Space*/);
    // Disable UHF LNA, enable VHF LNA
    BK4819_PickRXFilterPathBasedOnFrequency(10320000); // 103.2 MHz < 280 MHz

    AUDIO_AudioPathOn();

    gEnableSpeaker       = true;
    gUpdateStatus        = true;

    #ifdef ENABLE_FEAT_F4HWN_RESUME_STATE
        gEeprom.CURRENT_STATE = 3;
        SETTINGS_WriteCurrentState();
    #endif
}

#endif

#endif
