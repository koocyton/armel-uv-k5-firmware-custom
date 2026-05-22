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
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifdef ENABLE_FMRADIO

#include <string.h>

#include "app/fm.h"
#include "driver/bk1080.h"
#ifdef ENABLE_FM_SI4732
#include "driver/si473x.h"
#endif
#include "driver/st7565.h"
#include "external/printf/printf.h"
#include "misc.h"
#include "settings.h"
#include "ui/fmradio.h"
#include "ui/helper.h"
#include "ui/inputbox.h"
#include "ui/ui.h"

#ifdef ENABLE_FM_SI4732
void UI_DisplayFmWait(void)
{
    const int16_t x1 = 16, y1 = 15, x2 = 111, y2 = 34;
    UI_FillRectangleBuffer(gFrameBuffer, x1, y1, x2, y2, true);
    UI_PrintStringInverted("waiting", (uint8_t)x1, (uint8_t)(x2 + 1), 2, 8);
}
#endif

void UI_DisplayFM(void)
{
    char        String[16]  = { 0 };
    char       *pPrintStr   = String;
    UI_DisplayClear();

#ifdef ENABLE_FEAT_F4HWN
    UI_DisplayUnlockKeyboard(5);
#endif

#ifndef ENABLE_FM_SI4732
    UI_PrintString("FM", 2, 0, 0, 8);

    sprintf(String, "%d%s-%dM",
            BK1080_GetFreqLoLimit(gEeprom.FM_Band) / 10,
            gEeprom.FM_Band == 0 ? ".5" : "",
            BK1080_GetFreqHiLimit(gEeprom.FM_Band) / 10);

    UI_PrintStringSmallNormal(String, 1, 0, 6);

    if (gAskToSave) {
        pPrintStr = "SAVE?";
    } else if (gAskToDelete) {
        pPrintStr = "DEL?";
    } else if (gFM_ScanState == FM_SCAN_OFF) {
        if (gEeprom.FM_IsMrMode) {
            sprintf(String, "MR(CH%02u)", gEeprom.FM_SelectedChannel + 1);
            pPrintStr = String;
        } else {
            pPrintStr = "VFO";
            for (unsigned int i = 0; i < FM_CHANNELS_MAX; i++) {
                if (gEeprom.FM_FrequencyPlaying == gFM_Channels[i]) {
                    sprintf(String, "VFO(CH%02u)", i + 1);
                    pPrintStr = String;
                    break;
                }
            }
        }
    } else if (gFM_AutoScan) {
        sprintf(String, "A-SCAN(%u)", gFM_ChannelPosition);
        pPrintStr = String;
    } else {
        pPrintStr = "M-SCAN";
    }

    UI_PrintString(pPrintStr, 0, 127, 3, 10);

    memset(String, 0, sizeof(String));
    if (gAskToSave || (gEeprom.FM_IsMrMode && gInputBoxIndex > 0)) {
        UI_GenerateChannelString(String, gFM_ChannelPosition);
    } else if (gAskToDelete) {
        sprintf(String, "CH-%02u", gEeprom.FM_SelectedChannel + 1);
    } else {
        if (gInputBoxIndex == 0) {
            sprintf(String, "%3d.%d", gEeprom.FM_FrequencyPlaying / 10, gEeprom.FM_FrequencyPlaying % 10);
        } else {
            const char *ascii = INPUTBOX_GetAscii();
            sprintf(String, "%.3s.%.1s", ascii, ascii + 3);
        }

        UI_DisplayFrequency(String, 36, 1, gInputBoxIndex == 0);
        ST7565_BlitFullScreen();
        return;
    }

    UI_PrintString(String, 0, 127, 1, 10);

#else /* ENABLE_FM_SI4732 */
    if (SI47XX_IsAMFamily()) {
        const char *mod = (si4732mode == SI47XX_AM) ? "AM" : (si4732mode == SI47XX_LSB) ? "LSB" : (si4732mode == SI47XX_USB) ? "USB" : "CW";
        UI_PrintString(mod, 2, 0, 0, 8);
        UI_DrawLineBuffer(gFrameBuffer, 0, 38, 127, 38, true);
        {
            const uint8_t focus = FM_GetAM_OptionFocus();
            UI_PrintStringSmallNormal("LNA", 0, 0, 5);
            UI_PrintStringSmallNormal("BW", 32, 0, 5);
            UI_PrintStringSmallNormal("STP", 64, 0, 5);
            UI_PrintStringSmallNormal("BFO", 96, 0, 5);
            if (focus == 0)
                UI_InvertRectangleBuffer(gFrameBuffer, 0, 39, 29, 47);
            else if (focus == 1)
                UI_InvertRectangleBuffer(gFrameBuffer, 32, 39, 61, 47);
            else if (focus == 2)
                UI_InvertRectangleBuffer(gFrameBuffer, 64, 39, 93, 47);
            else
                UI_InvertRectangleBuffer(gFrameBuffer, 96, 39, 125, 47);
        }
        {
            char valStr[16];
            const uint8_t focus = FM_GetAM_OptionFocus();
            const uint8_t lna   = FM_GetAM_LnaIndex();
            const uint8_t bw    = FM_GetAM_BW_Index();
            static const char *const lnaVals[] = { "AGC ON", "ATT 0", "ATT 1", "ATT 5", "ATT 15", "ATT 26" };
            static const char *const stpVals[] = { "1K", "5K", "10K", "100K", "1000K" };
            static const char *const bwVals[]  = { "0.5", "1.0", "1.2", "2.2", "3.0", "4.0", "5.0" };
            if (focus == 0)
                sprintf(valStr, "%s", lna < 6u ? lnaVals[lna] : "AGC ON");
            else if (focus == 1)
                sprintf(valStr, "%sk", bw < 7u ? bwVals[bw] : "1.2");
            else if (focus == 2)
                sprintf(valStr, "%s", stpVals[FM_GetAM_StepIndex() < 5u ? FM_GetAM_StepIndex() : 2]);
            else
                sprintf(valStr, "%dHz", (int)FM_GetAM_BfoHz());
            UI_PrintStringSmallNormal(valStr, 0, 0, 6);
        }
        if (gInputBoxIndex == 0) {
            RSQ_GET();
            sprintf(String, "%u/%u", (unsigned)rsqStatus.resp.RSSI, (unsigned)rsqStatus.resp.SNR);
            UI_PrintStringSmallNormal(String, 88, 0, 6);
        }
    } else {
        UI_PrintString("FM", 2, 0, 0, 8);
        sprintf(String, "%d%s-%dM",
                BK1080_GetFreqLoLimit(gEeprom.FM_Band) / 10,
                gEeprom.FM_Band == 0 ? ".5" : "",
                BK1080_GetFreqHiLimit(gEeprom.FM_Band) / 10);
        UI_PrintStringSmallNormal(String, 1, 0, 6);
    }

    if (!SI47XX_IsAMFamily() && gInputBoxIndex == 0) {
        RSQ_GET();
        sprintf(String, "%u/%u", (unsigned)rsqStatus.resp.RSSI, (unsigned)rsqStatus.resp.SNR);
        UI_PrintStringSmallNormal(String, 88, 0, 6);
    }

    if (gFM_ScanState == FM_SCAN_OFF) {
        if (SI47XX_IsAMFamily()) {
            pPrintStr = "";
        } else {
            if (gEeprom.FM_IsMrMode) {
                sprintf(String, "MR(CH%02u)", gEeprom.FM_SelectedChannel + 1);
                pPrintStr = String;
            } else {
                pPrintStr = "";
            }
        }
    } else {
        pPrintStr = "SCAN";
    }

    UI_PrintString(pPrintStr, 0, 127, 3, 10);

    memset(String, 0, sizeof(String));
    if (gAskToSave || (gEeprom.FM_IsMrMode && gInputBoxIndex > 0)) {
        UI_GenerateChannelString(String, gFM_ChannelPosition);
    } else if (gAskToDelete) {
        sprintf(String, "CH-%02u", gEeprom.FM_SelectedChannel + 1);
    } else {
        if (gInputBoxIndex == 0) {
            if (SI47XX_IsAMFamily())
                sprintf(String, "%u.%03u", (unsigned)(siCurrentFreq / 1000), (unsigned)(siCurrentFreq % 1000));
            else
                sprintf(String, "%3d.%d", gEeprom.FM_FrequencyPlaying / 10, gEeprom.FM_FrequencyPlaying % 10);
        } else {
            const char *ascii = INPUTBOX_GetAscii();
            if (SI47XX_IsAMFamily())
                sprintf(String, "%.*sk", (int)gInputBoxIndex, ascii);
            else
                sprintf(String, "%.3s.%.1s", ascii, ascii + 3);
        }

        UI_DisplayFrequency(String, 36, 1, gInputBoxIndex == 0);
        ST7565_BlitFullScreen();
        return;
    }

    UI_PrintString(String, 0, 127, 1, 10);
#endif /* ENABLE_FM_SI4732 */

    ST7565_BlitFullScreen();
}

#endif
