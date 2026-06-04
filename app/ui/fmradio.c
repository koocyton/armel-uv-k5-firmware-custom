#ifdef ENABLE_FMRADIO

#ifdef ENABLE_FM_SI4732

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

#ifdef ENABLE_FM_SI4732
#define UI_FM_IS_FREQ_DIGIT(c) ((c) == '-' || ((c) >= '0' && (c) <= '9'))

static void UI_FM_DrawStepUnderline_AM(uint8_t startX, uint8_t y, const char *freqStr)
{
	/* freqStr："%u.%03u" 或 "D%u.%03u"（<10 MHz 首位虚线 0）；按 STP 标步进位 */
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
		/* 5k：下划线放在最后两位数字的“中间” */
		const uint8_t right = (uint8_t)(nd - 1);
		const uint8_t left  = (uint8_t)(nd - 2);
		const uint8_t mid = (uint8_t)((digits[left].x2 + digits[right].x1) / 2);
		uint8_t x1 = (mid > 3) ? (uint8_t)(mid - 3) : 0;
		uint8_t x2 = (uint8_t)(mid + 3 + 2); /* 右边增加 2px */
		UI_DrawLineBuffer(gFrameBuffer, x1, y, x2, y, true);
		UI_DrawLineBuffer(gFrameBuffer, x1, (uint8_t)(y + 1), x2, (uint8_t)(y + 1), true); /* 2px 高 */
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
	uint8_t x2 = (uint8_t)(digits[idx].x2 - 2 + 2); /* 右边增加 2px */
	UI_DrawLineBuffer(gFrameBuffer, x1, y, x2, y, true);
	UI_DrawLineBuffer(gFrameBuffer, x1, (uint8_t)(y + 1), x2, (uint8_t)(y + 1), true);
}
#endif

static void UI_FM_DrawStepUnderline_FM(uint8_t startX, uint8_t y, const char *freqStr)
{
	/* FM 频率："%3d.%d" 或 "D%02d.%d"（<100 MHz 百位虚线 0）；按 STP 标下划线 */
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
	/* S数字 + 15 格；每格 4 单位；格子 6x7，间隔 1px；未到达格子用虚格纹理 */
	const uint8_t bw = 6;
	const uint8_t bh = 7;
	const uint8_t gap = 1;
	if (cells == 0) return;

	const uint16_t totalUnits = (uint16_t)cells * 4U;
	if (units > totalUnits) units = totalUnits;

	/* 达到的“格子数”：有任何强度就算到达该格子 */
	uint8_t reached = (uint8_t)((units + 3U) / 4U);
	if (reached > cells) reached = cells;

	/* 显示 S 数字：固定 3 字符宽度（例如 "S9 " / "S9+"），避免 S9↔S9+ 推动条形位置 */
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

	/* S 数字在当前位置下移 1px */
	UI_FM_DrawSmallStringAt(x, (uint8_t)(y + 0), sStr);
	const uint8_t labelW = (uint8_t)(3U * 6U); /* 固定宽度：3 字符，每字符 6px + 1px间距在绘制函数里处理 */
	uint8_t barX = (uint8_t)(x + labelW + (bw + gap)); /* 间隔一个格子的宽度 */

	for (uint8_t i = 0; i < cells; i++) {
		const uint8_t bx1 = (uint8_t)(barX + i * (bw + gap));
		const uint8_t by1 = y;
		const uint8_t bx2 = (uint8_t)(bx1 + bw - 1);
		const uint8_t by2 = (uint8_t)(y + bh - 1);

		if (i < reached) {
			/* 有信号：实心 */
			UI_FillRectangleBuffer(gFrameBuffer, bx1, by1, bx2, by2, true);
		} else {
			/* 无信号：虚格（101010 / 000000 交替行） */
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
	/* 使用 gFontSmall(6x8) 在任意像素 y 绘制（支持跨两行 framebuffer） */
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
		cx = (uint8_t)(cx + 1); /* 字符间距 1px */
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
#endif

#ifdef ENABLE_FM_SI4732
/* 在任意像素 y 处绘制 gFontBig(7x16) 字符串，支持跨三行 framebuffer */
static void UI_FM_DrawBigStringAt(uint8_t x, uint8_t y, const char *s)
{
	if (!s || !*s) return;
	if (y >= (FRAME_LINES * 8)) return;

	const uint8_t row = (uint8_t)(y / 8);
	const uint8_t shift = (uint8_t)(y % 8);

	uint8_t cx = x;
	for (const char *p = s; *p; p++) {
		const char c = *p;
		if (c <= ' ' || c >= 127) {
			cx = (uint8_t)(cx + 8);
			continue;
		}
		const uint8_t idx = (uint8_t)(c - ' ' - 1);
		for (uint8_t col = 0; col < 7; col++) {
			if (cx >= 128) break;
			const uint16_t bits = (uint16_t)gFontBig[idx][col] |
			                      ((uint16_t)gFontBig[idx][col + 7] << 8);
			const uint32_t v = (uint32_t)bits << shift;
			if (row < FRAME_LINES)        gFrameBuffer[row][cx]        |= (uint8_t)(v & 0xFF);
			if (row + 1 < FRAME_LINES)    gFrameBuffer[row + 1][cx]    |= (uint8_t)((v >> 8) & 0xFF);
			if (row + 2 < FRAME_LINES)    gFrameBuffer[row + 2][cx]    |= (uint8_t)((v >> 16) & 0xFF);
			cx++;
		}
		cx = (uint8_t)(cx + 1); /* 字符间距 1px */
	}
}

static void UI_FM_DrawWaitPopup(void)
{
	const int16_t boxWidth = (int16_t)(LCD_WIDTH / 2);
	const int16_t x0 = (int16_t)(boxWidth / 2.2);
	const int16_t boxHeight = (int16_t)(boxWidth / 3); /* 原高度的一半 */
	/* 高度减半后下移 (原高-新高)/2，方框垂直中心与减半前一致 */
	const int16_t y0 = (int16_t)((boxHeight * 3) / 4);
	static const char msg[] = "WAIT";
	/* gFontBig：7px 字宽 + 1px 间距 */
	const uint8_t textW = (uint8_t)(sizeof(msg) - 1U) * 8U - 1U;

	/* 虚点背板：稀疏点阵填充（约 1/8 密度，偶数行错位）
	 * y%4==0 行点亮 x%4==0；y%4==2 行点亮 x%4==2；奇数行全空 */
	for (int16_t y = 0; y < boxHeight; y++) {
		for (int16_t x = 0; x < boxWidth; x++) {
			const bool dot = ((y % 2) == 0) && ((x % 4) == (y % 4));
			UI_DrawPixelBuffer(gFrameBuffer, (uint8_t)(x0 + x), (uint8_t)(y0 + y), dot);
		}
	}

	const uint8_t textX = (uint8_t)(x0 + (boxWidth - textW) / 2);
	const uint8_t textY = (uint8_t)(y0 + (boxHeight - 16) / 2);
	/* WAIT 以实心黑字绘制在虚点背板上，与稀疏点阵形成对比 */
	UI_FM_DrawBigStringAt(textX, textY, msg);
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

#ifdef ENABLE_FM_SI4732
	if (SI47XX_IsAMFamily()) {
		const char *mod = (si4732mode == SI47XX_AM) ? "AM" : (si4732mode == SI47XX_LSB) ? "LSB" : (si4732mode == SI47XX_USB) ? "USB" : "CW";
		/* 左上角第 0 行：模式标签，8px 大字体（font.c gFontBig） */
		UI_PrintString(mod, 2, 0, 0, 8);
		/* RSSI 强度条（方块）：在频率下方、分割线之上，左对齐 */
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
			default: /* AM/SW */
				full_scale = 45;
				break;
			}
			/* SSB/CW：信号条打折（减半计算），RSSI 数字显示保持不变 */
			uint8_t meterRssi = rsqStatus.resp.RSSI;
			if (si4732mode == SI47XX_LSB || si4732mode == SI47XX_USB || si4732mode == SI47XX_CW) {
				meterRssi = (uint8_t)(meterRssi / 2U);
			}
			const uint16_t units = UI_FM_MapRssiToUnits(meterRssi, full_scale, 15);
			/* S 表整体下移 5px；S 数字贴左边 */
			UI_FM_DrawSmeter(0, 29, units, 15);
		}
		/* 底部倒数第二行(line 5)：LNA/BW/STP/BFO，选中时反色块包住文字 */
		{
			const uint8_t focus = FM_GetAM_OptionFocus();
			#define AM_OPT_X0  0
			#define AM_OPT_X1 32
			#define AM_OPT_X2 64
			#define AM_OPT_X3 96
			#define AM_OPT_W  30
			/* 像素级调整：整体上移 1px；LNA/STP/BFO 右移 5px；BW 右移 8px */
			UI_FM_DrawSmallStringAt((uint8_t)(AM_OPT_X0 + 5), 39, "LNA");
			UI_FM_DrawSmallStringAt((uint8_t)(AM_OPT_X1 + 8), 39, "BW");
			UI_FM_DrawSmallStringAt((uint8_t)(AM_OPT_X2 + 5), 39, "STP");
			UI_FM_DrawSmallStringAt((uint8_t)(AM_OPT_X3 + 5), 39, "BFO");
			/* 反色条整体上移 1px（去掉上方分割横线后更紧凑） */
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
		/* 底行(line 6)左侧：当前焦点对应的子选项值；右侧：RSSI/SNR */
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
		UI_PrintString("FM", 2, 0, 0, 8); /* 同上，Si4732 FM 模式 */
		sprintf(String, "%d%s-%dM",
			BK1080_GetFreqLoLimit(gEeprom.FM_Band)/10,
			gEeprom.FM_Band == 0 ? ".5" : "",
			BK1080_GetFreqHiLimit(gEeprom.FM_Band)/10
		);
		UI_PrintStringSmallNormal(String, 1, 0, 6);

		/* RSSI 强度条：BK1080 RSSI（REG_10），在频率下方、line3 左侧 */
		if (gInputBoxIndex == 0) {
			const uint16_t st = BK1080_ReadRegister(BK1080_REG_10);
			/* FM 满刻度 70，线性映射 */
			const uint16_t units = (uint16_t)(uint8_t)BK1080_REG_10_GET_RSSI(st) * (15U * 4U) / 70U;
			UI_FM_DrawSmeter(0, 29, units, 15);
		}
	}

#ifdef ENABLE_FM_SI4732
	/* FM 时 RSSI/SNR 在 line 6 右侧 */
	if (!SI47XX_IsAMFamily() && gInputBoxIndex == 0) {
		RSQ_GET();
		sprintf(String, "%u/%u", (unsigned)rsqStatus.resp.RSSI, (unsigned)rsqStatus.resp.SNR);
		UI_PrintStringSmallNormal(String, 88, 0, 6);
	}
#endif

	//uint8_t spacings[] = {20,10,5};
	//sprintf(String, "%d0k", spacings[gEeprom.FM_Space % 3]);
	//UI_PrintStringSmallNormal(String, 127 - 4*7, 0, 6);

	// 已禁用 FM 搜索/保存功能：不再显示 SAVE?/DEL?、A-SCAN/M-SCAN
	if (gFM_ScanState == FM_SCAN_OFF) {
#ifdef ENABLE_FM_SI4732
		if (SI47XX_IsAMFamily()) {
			pPrintStr = ""; /* AM/LSB/USB/CW: hide VFO label */
		} else
#endif
		{
			if (gEeprom.FM_IsMrMode) {
				sprintf(String, "MR(CH%02u)", gEeprom.FM_SelectedChannel + 1);
				pPrintStr = String;
			} else {
				/* 在 FM 模式下不再显示 VFO / VFO(CHxx) 标签 */
				pPrintStr = "";
			}
		}
	} else {
		// 若仍处于扫描状态（例如旧版本遗留），仍然显示一个中性提示
		pPrintStr = "SCAN";
	}

	UI_PrintString(pPrintStr, 0, 127, 3, 10); // memory, vfo, scan

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
				/* <10 MHz：首位虚线 0，与 10.xxx 同宽 */
				if (mhz < 10u)
					sprintf(String, "-%u.%03u", mhz, khz3);
				else
					sprintf(String, "%u.%03u", mhz, khz3);
			} else
#endif
			{
				const uint16_t freq10 = gEeprom.FM_FrequencyPlaying;
				/* <100.0 MHz：百位虚线 0，与 100.x 同宽（D99.9 / D09.5） */
				if (freq10 < 1000u)
					sprintf(String, "-%02d.%d", freq10 / 10u, freq10 % 10u);
				else
					sprintf(String, "%3d.%d", freq10 / 10u, freq10 % 10u);
			}
		} else {
			const char * ascii = INPUTBOX_GetAscii();
#ifdef ENABLE_FM_SI4732
			if (SI47XX_IsAMFamily()) {
				/* AM 输入显示：--.---（5 位），未输入用 '-' 占位 */
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

		/* FM：为了让 91.5 与 107.5 末位对齐（STP 下划线位置一致），禁止基于前导空格的“居中补偿” */
#ifdef ENABLE_FM_SI4732
		const bool centerFreq = (gInputBoxIndex == 0) && SI47XX_IsAMFamily();
#else
		const bool centerFreq = false;
#endif
		UI_DisplayFrequency(String, 36, 1, centerFreq);  // frequency

#ifdef ENABLE_FM_SI4732
		/* AM/SSB：按 STP 在频率数字下方标记“下划线” */
		if (SI47XX_IsAMFamily() && gInputBoxIndex == 0) {
			/* 放在大数字下沿（line2 的底部），避免与分割线/底部选项重叠 */
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

#else

#include <string.h>

#include "app/fm.h"
#include "driver/bk1080.h"
#include "driver/st7565.h"
#include "external/printf/printf.h"
#include "misc.h"
#include "settings.h"
#include "ui/fmradio.h"
#include "ui/helper.h"
#include "ui/inputbox.h"
#include "ui/ui.h"

void UI_DisplayFM(void)
{
    char String[16] = {0};
    char *pPrintStr = String;
    UI_DisplayClear();

#ifdef ENABLE_FEAT_F4HWN
    UI_DisplayUnlockKeyboard(5);
#endif

    UI_PrintString("FM", 2, 0, 0, 8);

    sprintf(String, "%d%s-%dM", 
        BK1080_GetFreqLoLimit(gEeprom.FM_Band)/10,
        gEeprom.FM_Band == 0 ? ".5" : "",
        BK1080_GetFreqHiLimit(gEeprom.FM_Band)/10
        );
    
    UI_PrintStringSmallNormal(String, 1, 0, 6);

    //uint8_t spacings[] = {20,10,5};
    //sprintf(String, "%d0k", spacings[gEeprom.FM_Space % 3]);
    //UI_PrintStringSmallNormal(String, 127 - 4*7, 0, 6);

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

    UI_PrintString(pPrintStr, 0, 127, 3, 10); // memory, vfo, scan

    memset(String, 0, sizeof(String));
    if (gAskToSave || (gEeprom.FM_IsMrMode && gInputBoxIndex > 0)) {
        UI_GenerateChannelString(String, gFM_ChannelPosition);
    } else if (gAskToDelete) {
        sprintf(String, "CH-%02u", gEeprom.FM_SelectedChannel + 1);
    } else {
        if (gInputBoxIndex == 0) {
            sprintf(String, "%3d.%d", gEeprom.FM_FrequencyPlaying / 10, gEeprom.FM_FrequencyPlaying % 10);
        } else {
            const char * ascii = INPUTBOX_GetAscii();
            sprintf(String, "%.3s.%.1s",ascii, ascii + 3);
        }

        UI_DisplayFrequency(String, 36, 1, gInputBoxIndex == 0);  // frequency
        ST7565_BlitFullScreen();
        return;
    }

    UI_PrintString(String, 0, 127, 1, 10);

    ST7565_BlitFullScreen();
}

#endif

#endif
