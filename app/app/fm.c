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
#include "driver/bk1080.h"
#include "driver/bk4819.h"
#include "driver/py25q16.h"
#include "driver/gpio.h"
#include "functions.h"
#include "misc.h"
#include "settings.h"
#include "ui/inputbox.h"
#include "ui/ui.h"

#ifdef ENABLE_FM_SI4732
#include <stdint.h>
#include "driver/si473x.h"
#include "driver/si4732_storage.h"
#include "driver/st7565.h"
#include "driver/system.h"
#include "ui/fmradio.h"
#endif

uint16_t          gFM_Channels[FM_CHANNELS_MAX];
bool              gFmRadioMode;
uint8_t           gFmRadioCountdown_500ms;
volatile uint16_t gFmPlayCountdown_10ms;
volatile int8_t   gFM_ScanState;
bool              gFM_AutoScan;
uint8_t           gFM_ChannelPosition;
bool              gFM_FoundFrequency;
uint16_t          gFM_RestoreCountdown_10ms;

/* FM 步进（单位 0.1MHz）：0=0.1, 1=0.5, 2=1.0 */
static uint8_t gFM_StepIndex = 0;
static const uint16_t gFM_Step10_Table[] = { 1, 5, 10 };

uint16_t FM_GetFM_Step10(void)
{
    const uint8_t idx = (gFM_StepIndex < (uint8_t)ARRAY_SIZE(gFM_Step10_Table)) ? gFM_StepIndex : 0;
    return gFM_Step10_Table[idx];
}

static uint16_t gFM_AutoRssiWindow_10ms = 0;
static uint16_t gFM_AutoRssiNext_10ms = 0;

static void FM_BeginAutoRssiRefresh(void)
{
    gFM_AutoRssiWindow_10ms = 30U * 100U;
    gFM_AutoRssiNext_10ms = 2U * 100U;
}

void FM_TimeSlice10ms(void)
{
    if (gFM_AutoRssiWindow_10ms == 0)
        return;

    gFM_AutoRssiWindow_10ms--;
    if (gFM_AutoRssiNext_10ms > 0)
        gFM_AutoRssiNext_10ms--;

    if (gFM_AutoRssiNext_10ms == 0) {
        gUpdateDisplay = true;
        gFM_AutoRssiNext_10ms = 2U * 100U;
    }
}

const uint8_t BUTTON_STATE_PRESSED = 1 << 0;
const uint8_t BUTTON_STATE_HELD = 1 << 1;

const uint8_t BUTTON_EVENT_PRESSED = BUTTON_STATE_PRESSED;
const uint8_t BUTTON_EVENT_HELD = BUTTON_STATE_PRESSED | BUTTON_STATE_HELD;
const uint8_t BUTTON_EVENT_SHORT =  0;
const uint8_t BUTTON_EVENT_LONG =  BUTTON_STATE_HELD;


#ifdef ENABLE_FM_SI4732
static uint16_t gAM_FrequencyKHz = 720;
static uint8_t  gAM_OptionFocus  = 0;
static uint8_t  gAM_LnaIndex     = 0;
static uint8_t  gAM_BW_Index     = 2;
static int16_t  gAM_BfoHz        = 0;
static uint8_t  gAM_StepIndex    = 0;
static bool     gFKeyJustEnteredSSB = false;
static bool     gFKeyLongPressDone  = false;

static const uint16_t gAM_StepKHzTable[] = { 1, 5, 10, 100, 1000 };
#define AM_STEP_COUNT ((unsigned)ARRAY_SIZE(gAM_StepKHzTable))

static void FM_AM_ApplyDefaultBwForMode(void)
{
    if (si4732mode == SI47XX_AM) {
        gAM_BfoHz = 0;
    } else if (si4732mode == SI47XX_CW) {
        gAM_BW_Index = 2;
        gAM_BfoHz = 0;
    } else if (si4732mode == SI47XX_USB || si4732mode == SI47XX_LSB) {
        gAM_BW_Index = 3;
    }
}

uint16_t FM_GetAM_StepKHz(void)
{
    return gAM_StepKHzTable[gAM_StepIndex < AM_STEP_COUNT ? gAM_StepIndex : 0];
}

uint8_t FM_GetAM_OptionFocus(void)
{
    return gAM_OptionFocus;
}
uint8_t FM_GetAM_LnaIndex(void)
{
    return gAM_LnaIndex;
}
uint8_t FM_GetAM_BW_Index(void)
{
    return gAM_BW_Index;
}
uint8_t FM_GetAM_StepIndex(void)
{
    return gAM_StepIndex;
}
int16_t FM_GetAM_BfoHz(void)
{
    return gAM_BfoHz;
}

static void FM_ApplyAMOptions(void)
{
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
    PY25Q16_ReadBuffer(FM_PY_SI4732_AM_EXT_ADDR, buf, 8);
    buf[0] = (uint8_t)(gAM_FrequencyKHz & 0xFF);
    buf[1] = (uint8_t)(gAM_FrequencyKHz >> 8);
    buf[2] = (uint8_t)((uint16_t)gAM_BfoHz & 0xFF);
    buf[3] = (uint8_t)((uint16_t)gAM_BfoHz >> 8);
    buf[4] = gAM_LnaIndex;
    PY25Q16_WriteBuffer(FM_PY_SI4732_AM_EXT_ADDR, buf, 8, false);
}

void FM_LoadAMFrequencyFromEeprom(void)
{
    uint8_t buf[8];
    PY25Q16_ReadBuffer(FM_PY_SI4732_AM_EXT_ADDR, buf, 8);
    uint16_t khz = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
    if (khz >= 500 && khz <= 30000)
        gAM_FrequencyKHz = khz;
    gAM_BfoHz = (int16_t)((uint16_t)buf[2] | ((uint16_t)buf[3] << 8));
    if (buf[4] <= 5)
        gAM_LnaIndex = buf[4];
    else
        gAM_LnaIndex = 0;
}

static uint16_t FM_AM_ParseInputFreq(void)
{
    uint32_t v = 0;
    for (uint8_t i = 0; i < gInputBoxIndex && i < 5; i++) {
        uint8_t d = (uint8_t)gInputBox[i];
        if (d > 9)
            break;
        v = v * 10 + d;
    }
    if (v < 500)
        v = 500;
    if (v > 30000)
        v = 30000;
    return (uint16_t)v;
}
#endif


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

#ifdef ENABLE_FM_SI4732
    if (SI47XX_IsAMFamily())
        FM_SaveAMFreqToEeprom();
    AUDIO_AudioPathOff_FM();
#else
    AUDIO_AudioPathOff();
#endif
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
#ifdef ENABLE_FM_SI4732
    AUDIO_AudioPathOff_FM();
#else
    AUDIO_AudioPathOff();
#endif

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

void FM_AudioPathOn(void) {
    BACKLIGHT_TurnOn();
    AUDIO_AudioPathOn();
    gEnableSpeaker = true;
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

#ifdef ENABLE_FM_SI4732
    AUDIO_AudioPathOn_FM();
    BK1080_Mute(false);
    gEnableSpeaker = true;
#else
    FM_AudioPathOn();
#endif
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
#ifdef ENABLE_FM_SI4732
            if (SI47XX_IsAMFamily() && gInputBoxIndex >= 5) {
                gBeepToPlay           = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
                gRequestDisplayScreen = DISPLAY_FM;
                return;
            }
#endif
            if (gInputBoxIndex == 1) {
#ifdef ENABLE_FM_SI4732
                if (SI47XX_IsAMFamily()) {
                    if (gInputBox[0] > 2) {
                        gInputBox[1] = gInputBox[0];
                        gInputBox[0] = 0;
                        gInputBoxIndex = 2;
                    }
                } else if (gInputBox[0] > 1) {
                    gInputBox[1] = gInputBox[0];
                    gInputBox[0] = 0;
                    gInputBoxIndex = 2;
                }
#else
                if (gInputBox[0] > 1) {
                    gInputBox[1] = gInputBox[0];
                    gInputBox[0] = 0;
                    gInputBoxIndex = 2;
                }
#endif
            }
#ifdef ENABLE_FM_SI4732
            else if (SI47XX_IsAMFamily() && gInputBoxIndex > 4) {
                gAM_FrequencyKHz = FM_AM_ParseInputFreq();
                FM_SaveAMFreqToEeprom();
                gInputBoxIndex = 0;
                SI47XX_SetFreq(gAM_FrequencyKHz);
                FM_ApplyAMOptions();
                gUpdateStatus = true;
                return;
            }
#endif
            else if (gInputBoxIndex > 3) {
#ifndef ENABLE_FM_SI4732
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
#else
                if (!SI47XX_IsAMFamily()) {
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
#endif
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

#ifdef ENABLE_FM_SI4732
        (void)autoScan;
        gWasFKeyPressed       = false;
        switch (Key) {
            case KEY_0:
                if (state == BUTTON_EVENT_SHORT)
                    ACTION_FM();
                break;

            case KEY_1:
                gEeprom.FM_Band++;
                gRequestSaveFM = true;
                break;

            case KEY_3:
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
                gEeprom.FM_IsMrMode = !gEeprom.FM_IsMrMode;
                if (!FM_ConfigureChannelState()) {
                    BK1080_SetFrequency(gEeprom.FM_FrequencyPlaying, gEeprom.FM_Band/*, gEeprom.FM_Space*/);
                    gRequestSaveFM = true;
                } else
                    gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
                break;

            case KEY_STAR:
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
#else
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
#endif
    }
}

static void Key_EXIT(uint8_t state)
{
    if (gInputBoxIndex) {
        if (state != BUTTON_EVENT_SHORT)
            return;
    } 
    else {
        if (state != BUTTON_EVENT_PRESSED)
            return;
    }

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
#ifdef ENABLE_FM_SI4732
    if (state != BUTTON_EVENT_SHORT)
        return;

    gAskToSave   = false;
    gAskToDelete = false;
    gRequestDisplayScreen = DISPLAY_FM;
    gBeepToPlay           = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
#else
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
#endif
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
        const uint16_t step10 = FM_GetFM_Step10();
        uint16_t Frequency = (uint16_t)(gEeprom.FM_SelectedFrequency + Step * (int16_t)step10);

        Frequency = FM_WrapFrequency(Frequency);

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
    case 0:
        if (step > 0) gAM_LnaIndex = (uint8_t)((gAM_LnaIndex + 1) % 6);
        else gAM_LnaIndex = (uint8_t)((gAM_LnaIndex + 5) % 6);
        FM_ApplyAMOptions();
        FM_SaveAMFreqToEeprom();
        break;
    case 1:
        if (step > 0) gAM_BW_Index = (uint8_t)((gAM_BW_Index + 1) % 7);
        else gAM_BW_Index = (uint8_t)((gAM_BW_Index + 6) % 7);
        FM_ApplyAMOptions();
        FM_SaveAMFreqToEeprom();
        break;
    case 2:
        if (step > 0) gAM_StepIndex = (uint8_t)((gAM_StepIndex + 1) % AM_STEP_COUNT);
        else gAM_StepIndex = (uint8_t)((gAM_StepIndex + AM_STEP_COUNT - 1) % AM_STEP_COUNT);
        FM_SaveAMFreqToEeprom();
        break;
    case 3:
    default:
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
        FM_BeginAutoRssiRefresh();
    }

    switch (Key) {
        case KEY_0...KEY_9:
            Key_DIGITS(Key, state);
            break;
        case KEY_STAR:
#ifdef ENABLE_FM_SI4732
            if (SI47XX_IsAMFamily() && gInputBoxIndex == 0 && state == BUTTON_EVENT_SHORT) {
                gAM_StepIndex = (uint8_t)((gAM_StepIndex + 1) % AM_STEP_COUNT);
                FM_SaveAMFreqToEeprom();
                gUpdateStatus = true;
                gRequestDisplayScreen = DISPLAY_FM;
                gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
                break;
            }
            if (gInputBoxIndex == 0 && state == BUTTON_EVENT_SHORT && gFM_ScanState == FM_SCAN_OFF) {
                gFM_StepIndex = (uint8_t)((gFM_StepIndex + 1) % (uint8_t)ARRAY_SIZE(gFM_Step10_Table));
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
                if (bKeyPressed && !bKeyHeld) {
                    gAM_OptionFocus = (gAM_OptionFocus + 1) % 4;
                    gRequestDisplayScreen = DISPLAY_FM;
                    gUpdateStatus         = true;
                    gBeepToPlay           = BEEP_1KHZ_60MS_OPTIONAL;
                    break;
                }
                if (bKeyHeld || !bKeyPressed)
                    break;
            }
#endif
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
#ifdef ENABLE_FM_SI4732
            if (!bKeyPressed) {
                if (!gFKeyLongPressDone) {
                    if (si4732mode == SI47XX_FM) {
                        SI47XX_SwitchMode(SI47XX_AM);
                        if (gAM_FrequencyKHz < 500)
                            gAM_FrequencyKHz = 500;
                        if (gAM_FrequencyKHz > 30000)
                            gAM_FrequencyKHz = 30000;
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
                gFKeyLongPressDone  = false;
            } else if (bKeyHeld) {
                if (!gFKeyLongPressDone) {
                    if (si4732mode == SI47XX_LSB || si4732mode == SI47XX_USB || si4732mode == SI47XX_CW) {
                        if (!gFKeyJustEnteredSSB) {
                            SI47XX_SwitchMode(SI47XX_AM);
                            SI47XX_SetFreq(gAM_FrequencyKHz);
                            FM_ApplyAMOptions();
                            gUpdateStatus = true;
                            gBeepToPlay   = BEEP_1KHZ_60MS_OPTIONAL;
                        }
                    } else if (si4732mode == SI47XX_FM || si4732mode == SI47XX_AM) {
                        if (gAM_FrequencyKHz < 500)
                            gAM_FrequencyKHz = 500;
                        if (gAM_FrequencyKHz > 30000)
                            gAM_FrequencyKHz = 30000;
                        FM_SaveAMFreqToEeprom();
                        UI_DisplayFM();
                        UI_DisplayFmWait();
                        ST7565_BlitFullScreen();
                        SI47XX_SwitchMode(SI47XX_USB);
                        FM_AM_ApplyDefaultBwForMode();
                        SI47XX_SetFreq(gAM_FrequencyKHz);
                        FM_ApplyAMOptions();
                        gUpdateStatus         = true;
                        gBeepToPlay           = BEEP_1KHZ_60MS_OPTIONAL;
                        gFKeyJustEnteredSSB   = true;
                    } else {
                        GENERIC_Key_F(bKeyPressed, bKeyHeld);
                    }
                    gFKeyLongPressDone = true;
                }
            }
            break;
#else
            GENERIC_Key_F(bKeyPressed, bKeyHeld);
            break;
#endif
        case KEY_PTT:
#ifdef ENABLE_FM_SI4732
            if (bKeyPressed)
                Key_EXIT(BUTTON_EVENT_SHORT);
#else
            GENERIC_Key_PTT(bKeyPressed);
#endif
            break;
        case KEY_SIDE1:
        case KEY_SIDE2:
#ifdef ENABLE_FM_SI4732
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

#ifdef ENABLE_FM_SI4732
            AUDIO_AudioPathOn_FM();
            gEnableSpeaker = true;
#else
            FM_AudioPathOn();
#endif

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

#ifdef ENABLE_FM_SI4732
    AUDIO_AudioPathOff_FM();
    BK4819_SetAF(BK4819_AF_MUTE);
    BK1080_Init(gEeprom.FM_FrequencyPlaying, gEeprom.FM_Band/*, gEeprom.FM_Space*/);
    gEnableSpeaker = true;
    SYSTEM_DelayMs(100);
    AUDIO_AudioPathOn_FM();
    BK1080_Mute(false);
#else
    BK1080_Init(gEeprom.FM_FrequencyPlaying, gEeprom.FM_Band/*, gEeprom.FM_Space*/);
    BK4819_PickRXFilterPathBasedOnFrequency(10320000);
    FM_AudioPathOn();
#endif

    gUpdateStatus        = true;
    gUpdateDisplay       = true;
    FM_BeginAutoRssiRefresh();

    #ifdef ENABLE_FEAT_F4HWN_RESUME_STATE
        gEeprom.CURRENT_STATE = 3;
        SETTINGS_WriteCurrentState();
    #endif
}

#endif
