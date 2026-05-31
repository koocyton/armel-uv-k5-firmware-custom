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

#include "app/fm.h"
#include "driver/bk1080.h"
#ifdef ENABLE_FM_SI4732
#include "driver/si473x.h"
#endif
#include "driver/st7565.h"
#include "external/printf/printf.h"
#include "font.h"
#include "misc.h"
#include "settings.h"
#include "ui/fmradio.h"
#include "ui/helper.h"
#include "ui/inputbox.h"
#include "ui/ui.h"

#ifdef ENABLE_FM_SI4732
static void UI_FM_DrawStepUnderline_AM(uint8_t startX, uint8_t y, const char *freqStr);
static void UI_FM_DrawStepUnderline_FM(uint8_t startX, uint8_t y, const char *freqStr);
static void UI_FM_DrawSmeter(uint8_t x, uint8_t y, uint16_t units, uint8_t cells);
#else
static void UI_FM_DrawStepUnderline_FM(uint8_t startX, uint8_t y, const char *freqStr);
static void UI_FM_DrawSmeter(uint8_t x, uint8_t y, uint16_t units, uint8_t cells);
#endif

static void UI_FM_DrawSmallStringAt(uint8_t x, uint8_t y, const char *s);

#define UI_FM_IS_FREQ_DIGIT(c) ((c) == '-' || ((c) >= '0' && (c) <= '9'))

#ifdef ENABLE_FM_SI4732
static void UI_FM_DrawStepUnderline_AM(uint8_t startX, uint8_t y, const char *freqStr)
{
	struct DigitPos { uint8_t x1, x2; };
	struct DigitPos digits[10];
	uint8_t nd = 0;

	uint8_t x = startX;
	for (const char *p = freqStr; *p && nd < (uint8_t)(sizeof(digits) / sizeof(digits[0])); p++) {
		const char c = *p;
		if (UI_FM_IS_FREQ_DIGIT(c)) {
			digits[nd].x1 = x;
			digits[nd].x2 = (uint8_t)(x + 12);
			nd++;
			x = (uint8_t)(x + 13);
		} else if (c == '.') {
			x = (uint8_t)(x + 3);
		} else {
			x = (uint8_t)(x + 13);
		}
	}
	if (nd < 2)
		return;

	const uint16_t step = FM_GetAM_StepKHz();
	if (step == 5) {
		const uint8_t right = (uint8_t)(nd - 1);
		const uint8_t left  = (uint8_t)(nd - 2);
		const uint8_t mid = (uint8_t)((digits[left].x2 + digits[right].x1) / 2);
		uint8_t x1 = (mid > 3) ? (uint8_t)(mid - 3) : 0;
		uint8_t x2 = (uint8_t)(mid + 3 + 2);
		UI_DrawLineBuffer(gFrameBuffer, x1, y, x2, y, true);
		UI_DrawLineBuffer(gFrameBuffer, x1, (uint8_t)(y + 1), x2, (uint8_t)(y + 1), true);
		return;
	}

	uint8_t posFromRight = 0;
	if (step == 1) posFromRight = 0;
	else if (step == 10) posFromRight = 1;
	else if (step == 100) posFromRight = 2;
	else if (step == 1000) posFromRight = 3;
	else posFromRight = 0;

	if (posFromRight >= nd)
		posFromRight = (uint8_t)(nd - 1);

	const uint8_t idx = (uint8_t)((nd - 1) - posFromRight);
	uint8_t x1 = (uint8_t)(digits[idx].x1 + 2);
	uint8_t x2 = (uint8_t)(digits[idx].x2 - 2 + 2);
	UI_DrawLineBuffer(gFrameBuffer, x1, y, x2, y, true);
	UI_DrawLineBuffer(gFrameBuffer, x1, (uint8_t)(y + 1), x2, (uint8_t)(y + 1), true);
}
#endif

static void UI_FM_DrawStepUnderline_FM(uint8_t startX, uint8_t y, const char *freqStr)
{
	struct DigitPos { uint8_t x1, x2; };
	struct DigitPos digits[8];
	uint8_t nd = 0;

	uint8_t x = startX;
	for (const char *p = freqStr; *p && nd < (uint8_t)(sizeof(digits) / sizeof(digits[0])); p++) {
		const char c = *p;
		if (UI_FM_IS_FREQ_DIGIT(c)) {
			digits[nd].x1 = x;
			digits[nd].x2 = (uint8_t)(x + 12);
			nd++;
			x = (uint8_t)(x + 13);
		} else if (c == '.') {
			x = (uint8_t)(x + 3);
		} else {
			x = (uint8_t)(x + 13);
		}
	}
	if (nd < 2)
		return;

	const uint16_t step10 = FM_GetFM_Step10();
	if (step10 == 5) {
		const uint8_t right = (uint8_t)(nd - 1);
		const uint8_t left  = (uint8_t)(nd - 2);
		const uint8_t mid = (uint8_t)((digits[left].x2 + digits[right].x1) / 2);
		uint8_t x1 = (mid > 3) ? (uint8_t)(mid - 3) : 0;
		uint8_t x2 = (uint8_t)(mid + 3 + 2);
		UI_DrawLineBuffer(gFrameBuffer, x1, y, x2, y, true);
		UI_DrawLineBuffer(gFrameBuffer, x1, (uint8_t)(y + 1), x2, (uint8_t)(y + 1), true);
		return;
	}

	uint8_t posFromRight = 0;
	if (step10 == 1) posFromRight = 0;
	else if (step10 == 10) posFromRight = 1;
	else posFromRight = 0;

	if (posFromRight >= nd)
		posFromRight = (uint8_t)(nd - 1);

	const uint8_t idx = (uint8_t)((nd - 1) - posFromRight);
	uint8_t x1 = (uint8_t)(digits[idx].x1 + 2);
	uint8_t x2 = (uint8_t)(digits[idx].x2 - 2 + 2);
	UI_DrawLineBuffer(gFrameBuffer, x1, y, x2, y, true);
	UI_DrawLineBuffer(gFrameBuffer, x1, (uint8_t)(y + 1), x2, (uint8_t)(y + 1), true);
}

static void UI_FM_DrawSmeter(uint8_t x, uint8_t y, uint16_t units, uint8_t cells)
{
	const uint8_t bw = 6;
	const uint8_t bh = 7;
	const uint8_t gap = 1;
	if (cells == 0) return;

	const uint16_t totalUnits = (uint16_t)cells * 4U;
	if (units > totalUnits) units = totalUnits;

	uint8_t reached = (uint8_t)((units + 3U) / 4U);
	if (reached > cells) reached = cells;

	char sStr[4] = {'S','0',' ','\0'};
	if (reached == 0) {
		sStr[1] = '0';
	} else if (reached > 9) {
		sStr[1] = '9';
		sStr[2] = '+';
	} else if (reached == 9) {
		sStr[1] = '9';
	} else {
		sStr[1] = (char)('0' + reached);
	}

	UI_FM_DrawSmallStringAt(x, (uint8_t)(y + 0), sStr);
	const uint8_t labelW = (uint8_t)(3U * 6U);
	uint8_t barX = (uint8_t)(x + labelW + (bw + gap));

	for (uint8_t i = 0; i < cells; i++) {
		const uint8_t bx1 = (uint8_t)(barX + i * (bw + gap));
		const uint8_t by1 = y;
		const uint8_t bx2 = (uint8_t)(bx1 + bw - 1);
		const uint8_t by2 = (uint8_t)(y + bh - 1);

		if (i < reached) {
			UI_FillRectangleBuffer(gFrameBuffer, bx1, by1, bx2, by2, true);
		} else {
			for (uint8_t yy = 0; yy < bh; yy++) {
				const bool stripedRow = (yy % 2) == 0;
				for (uint8_t xx = 0; xx < bw; xx++) {
					const bool pix = stripedRow && ((xx % 2) == 0);
					if (pix) UI_DrawPixelBuffer(gFrameBuffer, (uint8_t)(bx1 + xx), (uint8_t)(by1 + yy), true);
				}
			}
		}
	}
}

static void UI_FM_DrawSmallStringAt(uint8_t x, uint8_t y, const char *s)
{
	if (!s || !*s) return;
	if (x >= 128) return;
	if (y >= (FRAME_LINES * 8)) return;

	const uint8_t row = (uint8_t)(y / 8);
	const uint8_t shift = (uint8_t)(y % 8);

	uint8_t cx = x;
	for (const char *p = s; *p; p++) {
		const char c = *p;
		if (c <= ' ' || c >= 127) {
			cx = (uint8_t)(cx + 7);
			continue;
		}
		const uint8_t idx = (uint8_t)(c - ' ' - 1);
		for (uint8_t col = 0; col < 6; col++) {
			if (cx >= 128) break;
			const uint8_t b = gFontSmall[idx][col];
			if (shift == 0) {
				gFrameBuffer[row][cx] |= b;
			} else {
				gFrameBuffer[row][cx] |= (uint8_t)(b << shift);
				if (row + 1 < FRAME_LINES) {
					gFrameBuffer[row + 1][cx] |= (uint8_t)(b >> (8 - shift));
				}
			}
			cx++;
		}
		cx = (uint8_t)(cx + 1);
	}
}

#ifdef ENABLE_FM_SI4732
static uint16_t UI_FM_MapRssiToUnits(uint8_t rssi, uint16_t fullScale, uint8_t cells)
{
	const uint16_t totalUnits = (uint16_t)cells * 4U;
	if (fullScale == 0) return 0;
	if (rssi > fullScale) rssi = (uint8_t)fullScale;
	return (uint16_t)rssi * totalUnits / fullScale;
}

static void UI_FM_DrawWaitPopup(void)
{
	const int16_t boxWidth = (int16_t)(LCD_WIDTH / 2);
	const int16_t x0 = (int16_t)(boxWidth / 2.2);
	const int16_t boxHeight = (int16_t)(boxWidth / 3);
	const int16_t y0 = (int16_t)((boxHeight * 3) / 4);
	static const char msg[] = "WAIT";
	const uint8_t textW = (uint8_t)(sizeof(msg) - 1U) * 7U - 1U;

	for (int16_t y = 0; y < boxHeight; y++) {
		for (int16_t x = 0; x < boxWidth; x++) {
			const bool onLeft   = (x == 1 || x == 2);
			const bool onRight  = (x == boxWidth - 2 || x == boxWidth - 3);
			const bool onTop    = (y == 1 || y == 2);
			const bool onBottom = (y == boxHeight - 2 || y == boxHeight - 3);
			const bool black = (onTop && x >= 3 && x <= boxWidth - 3) ||
			                   (onBottom && x >= 3 && x <= boxWidth - 3) ||
			                   (onLeft && y >= 3 && y <= boxHeight - 3) ||
			                   (onRight && y >= 3 && y <= boxHeight - 3) ||
			                   ((onLeft || onRight) && (onTop || onBottom));
			UI_DrawPixelBuffer(gFrameBuffer, (uint8_t)(x0 + x), (uint8_t)(y0 + y), black);
		}
	}

	const uint8_t textX = (uint8_t)(x0 + (boxWidth - textW) / 2);
	const uint8_t textY = (uint8_t)(y0 + (boxHeight - 8) / 2);
	UI_FM_DrawSmallStringAt(textX, textY, msg);
}

void UI_DisplayFmWait(void)
{
	UI_FM_DrawWaitPopup();
}
#endif

void UI_DisplayFM(void)
{
	char String[16] = {0};
	char *pPrintStr = String;
	UI_DisplayClear();

#ifdef ENABLE_FEAT_F4HWN
	UI_DisplayUnlockKeyboard(5);
#endif

#ifdef ENABLE_FM_SI4732
	if (SI47XX_IsAMFamily()) {
		const char *mod = (si4732mode == SI47XX_AM) ? "AM" : (si4732mode == SI47XX_LSB) ? "LSB" : (si4732mode == SI47XX_USB) ? "USB" : "CW";
		UI_PrintString(mod, 2, 0, 0, 8);
		if (gInputBoxIndex == 0) {
			RSQ_GET();
			uint16_t full_scale;
			switch (si4732mode) {
			case SI47XX_FM:
				full_scale = 70;
				break;
			case SI47XX_LSB:
			case SI47XX_USB:
			case SI47XX_CW:
				full_scale = 35;
				break;
			default:
				full_scale = 45;
				break;
			}
			uint8_t meterRssi = rsqStatus.resp.RSSI;
			if (si4732mode == SI47XX_LSB || si4732mode == SI47XX_USB || si4732mode == SI47XX_CW) {
				meterRssi = (uint8_t)(meterRssi / 2U);
			}
			const uint16_t units = UI_FM_MapRssiToUnits(meterRssi, full_scale, 15);
			UI_FM_DrawSmeter(0, 29, units, 15);
		}
		{
			const uint8_t focus = FM_GetAM_OptionFocus();
			#define AM_OPT_X0  0
			#define AM_OPT_X1 32
			#define AM_OPT_X2 64
			#define AM_OPT_X3 96
			#define AM_OPT_W  30
			UI_FM_DrawSmallStringAt((uint8_t)(AM_OPT_X0 + 5), 39, "LNA");
			UI_FM_DrawSmallStringAt((uint8_t)(AM_OPT_X1 + 8), 39, "BW");
			UI_FM_DrawSmallStringAt((uint8_t)(AM_OPT_X2 + 5), 39, "STP");
			UI_FM_DrawSmallStringAt((uint8_t)(AM_OPT_X3 + 5), 39, "BFO");
			if (focus == 0) UI_InvertRectangleBuffer(gFrameBuffer, AM_OPT_X0, 38, AM_OPT_X0 + AM_OPT_W - 1, 46);
			else if (focus == 1) UI_InvertRectangleBuffer(gFrameBuffer, AM_OPT_X1, 38, AM_OPT_X1 + AM_OPT_W - 1, 46);
			else if (focus == 2) UI_InvertRectangleBuffer(gFrameBuffer, AM_OPT_X2, 38, AM_OPT_X2 + AM_OPT_W - 1, 46);
			else UI_InvertRectangleBuffer(gFrameBuffer, AM_OPT_X3, 38, AM_OPT_X3 + AM_OPT_W - 1, 46);
			#undef AM_OPT_X0
			#undef AM_OPT_X1
			#undef AM_OPT_X2
			#undef AM_OPT_X3
			#undef AM_OPT_W
		}
		{
			char valStr[16];
			const uint8_t focus = FM_GetAM_OptionFocus();
			const uint8_t lna = FM_GetAM_LnaIndex();
			const uint8_t bw = FM_GetAM_BW_Index();
			static const char * const lnaVals[] = { "AGC ON", "ATT 0", "ATT 1", "ATT 5", "ATT 15", "ATT 26" };
			static const char * const stpVals[] = { "1K", "5K", "10K", "100K", "1000K" };
			static const char * const bwVals[] = { "0.5", "1.0", "1.2", "2.2", "3.0", "4.0", "5.0" };
			if (focus == 0) sprintf(valStr, "%s", lna < 6u ? lnaVals[lna] : "AGC ON");
			else if (focus == 1) sprintf(valStr, "%sk", bw < 7u ? bwVals[bw] : "1.2");
			else if (focus == 2) sprintf(valStr, "%s", stpVals[FM_GetAM_StepIndex() < 5u ? FM_GetAM_StepIndex() : 2]);
			else sprintf(valStr, "%dHz", (int)FM_GetAM_BfoHz());
			UI_PrintStringSmallNormal(valStr, 0, 0, 6);
		}
		if (gInputBoxIndex == 0) {
			RSQ_GET();
			sprintf(String, "%u/%u", (unsigned)rsqStatus.resp.RSSI, (unsigned)rsqStatus.resp.SNR);
			UI_PrintStringSmallNormal(String, 88, 0, 6);
		}
	} else
#endif
	{
		UI_PrintString("FM", 2, 0, 0, 8);
		sprintf(String, "%d%s-%dM",
			BK1080_GetFreqLoLimit(gEeprom.FM_Band)/10,
			gEeprom.FM_Band == 0 ? ".5" : "",
			BK1080_GetFreqHiLimit(gEeprom.FM_Band)/10
		);
		UI_PrintStringSmallNormal(String, 1, 0, 6);

		if (gInputBoxIndex == 0) {
			const uint16_t st = BK1080_ReadRegister(BK1080_REG_10);
			const uint16_t units = (uint16_t)(uint8_t)BK1080_REG_10_GET_RSSI(st) * (15U * 4U) / 70U;
			UI_FM_DrawSmeter(0, 29, units, 15);
		}
	}

#ifdef ENABLE_FM_SI4732
	if (!SI47XX_IsAMFamily() && gInputBoxIndex == 0) {
		RSQ_GET();
		sprintf(String, "%u/%u", (unsigned)rsqStatus.resp.RSSI, (unsigned)rsqStatus.resp.SNR);
		UI_PrintStringSmallNormal(String, 88, 0, 6);
	}
#endif

	if (gFM_ScanState == FM_SCAN_OFF) {
#ifdef ENABLE_FM_SI4732
		if (SI47XX_IsAMFamily()) {
			pPrintStr = "";
		} else
#endif
		{
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
#ifdef ENABLE_FM_SI4732
			if (SI47XX_IsAMFamily()) {
				const unsigned mhz = (unsigned)(siCurrentFreq / 1000);
				const unsigned khz3 = (unsigned)(siCurrentFreq % 1000);
				if (mhz < 10u)
					sprintf(String, "-%u.%03u", mhz, khz3);
				else
					sprintf(String, "%u.%03u", mhz, khz3);
			} else
#endif
			{
				const uint16_t freq10 = gEeprom.FM_FrequencyPlaying;
				if (freq10 < 1000u)
					sprintf(String, "-%02d.%d", freq10 / 10u, freq10 % 10u);
				else
					sprintf(String, "%3d.%d", freq10 / 10u, freq10 % 10u);
			}
		} else {
			const char * ascii = INPUTBOX_GetAscii();
#ifdef ENABLE_FM_SI4732
			if (SI47XX_IsAMFamily()) {
				char d[5] = {'-','-','-','-','-'};
				for (uint8_t i = 0; i < 5 && i < gInputBoxIndex; i++) {
					const uint8_t v = gInputBox[i];
					if (v <= 9)
						d[i] = (char)('0' + v);
				}
				sprintf(String, "%c%c.%c%c%c", d[0], d[1], d[2], d[3], d[4]);
			}
			else
#endif
				sprintf(String, "%.3s.%.1s", ascii, ascii + 3);
		}

#ifdef ENABLE_FM_SI4732
		const bool centerFreq = (gInputBoxIndex == 0) && SI47XX_IsAMFamily();
#else
		const bool centerFreq = false;
#endif
		UI_DisplayFrequency(String, 36, 1, centerFreq);

#ifdef ENABLE_FM_SI4732
		if (SI47XX_IsAMFamily() && gInputBoxIndex == 0) {
			UI_FM_DrawStepUnderline_AM(36, 23, String);
		} else if (!SI47XX_IsAMFamily() && gInputBoxIndex == 0) {
			UI_FM_DrawStepUnderline_FM(36, 23, String);
		}
#else
		if (gInputBoxIndex == 0) {
			UI_FM_DrawStepUnderline_FM(36, 23, String);
		}
#endif
		ST7565_BlitFullScreen();
		return;
	}

	UI_PrintString(String, 0, 127, 1, 10);

	ST7565_BlitFullScreen();
}

#endif
