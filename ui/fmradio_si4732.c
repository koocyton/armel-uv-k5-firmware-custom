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
#define UI_FM_FREQ_DOT_WIDTH 7U

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
			x = (uint8_t)(x + UI_FM_FREQ_DOT_WIDTH);
		} else {
			x = (uint8_t)(x + 13);
		}
	}
	if (nd < 2)
		return;

	uint16_t step = FM_GetAM_StepKHz();
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
			x = (uint8_t)(x + UI_FM_FREQ_DOT_WIDTH);
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

static void UI_FM_DrawDashedStringAt(uint8_t x, uint8_t y, const char *s)
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
			uint8_t b = gFontSmall[idx][col];
			for (uint8_t bit = 0; bit < 8; bit++) {
				if (((bit & 1U) == 0U) || ((col & 1U) != 0U))
					b &= (uint8_t)~(1U << bit);
			}
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

/* 与对讲机主界面频率下方扩展信息相同：gFont3x5（3x5 + 1px 间距） */
static void UI_FM_DrawSmallestAt(uint8_t x, uint8_t y, const char *s)
{
	if (!s || !*s) return;
	GUI_DisplaySmallest(s, x, y, false, true);
}

/* 右端像素对齐 rightX */
static void UI_FM_DrawSmallestRight(uint8_t rightX, uint8_t y, const char *s)
{
	uint8_t n = 0;

	if (!s || !*s) return;
	while (s[n]) n++;
	if (n == 0) return;

	/* 每字 3px 宽 + 1px 间距；末字右缘 = leftX + (n-1)*4 + 2 */
	const uint8_t leftX = (uint8_t)(rightX - 2U - (n - 1U) * 4U);
	UI_FM_DrawSmallestAt(leftX, y, s);
}

/* 与 UI_FM_DrawSmeter 一致的进度条左右边界 */
static void UI_FM_GetSmeterBarBounds(uint8_t smeterX, uint8_t cells, uint8_t *barLeft, uint8_t *barRight)
{
	const uint8_t bw = 6U;
	const uint8_t gap = 1U;
	const uint8_t labelW = (uint8_t)(3U * 6U);
	const uint8_t barX = (uint8_t)(smeterX + labelW + (bw + gap));

	if (barLeft) *barLeft = barX;
	if (barRight && cells > 0U)
		*barRight = (uint8_t)(barX + cells * bw + (cells - 1U) * gap - 1U);
}

#ifdef ENABLE_FM_SI4732
/* 垂直布局：RSSI/SNR 底缘与 LNA 行反色框顶缘间隔 1px */
#define UI_FM_AM_OPT_LABEL_Y        39U
#define UI_FM_AM_OPT_INV_Y0         38U
#define UI_FM_GAP_ABOVE_AM_OPT      1U
#define UI_FM_RSQ_CHAR_H            5U
#define UI_FM_SMETER_BAR_H          7U
#define UI_FM_SMETER_TO_RSQ_GAP     1U
#define UI_FM_UNDERLINE_TO_SMETER_GAP 6U
#define UI_FM_FREQ_DIGIT_H          16U
#define UI_FM_FREQ_SHIFT_Y          2U  /* 频率数字额外下移 */
#define UI_FM_SNR_LEFT_SHIFT        2U  /* SNR 相对进度条右缘左移 */

#define UI_FM_RSQ_Y                 (UI_FM_AM_OPT_INV_Y0 - UI_FM_GAP_ABOVE_AM_OPT - UI_FM_RSQ_CHAR_H)
#define UI_FM_SMETER_Y              (UI_FM_RSQ_Y - UI_FM_SMETER_TO_RSQ_GAP - UI_FM_SMETER_BAR_H)
#define UI_FM_UNDERLINE_Y           (UI_FM_SMETER_Y - UI_FM_UNDERLINE_TO_SMETER_GAP + UI_FM_FREQ_SHIFT_Y)
#define UI_FM_FREQ_Y                (UI_FM_UNDERLINE_Y + 1U - UI_FM_FREQ_DIGIT_H)

static void UI_FM_BlitFreqGlyph(uint8_t x, uint8_t y, uint8_t glyphIndex)
{
	const uint8_t half = 10U;
	const uint8_t row = (uint8_t)(y / 8U);
	const uint8_t shift = (uint8_t)(y % 8U);
	const uint8_t *src = gFontBigDigits[glyphIndex];

	for (uint8_t col = 0; col < half; col++) {
		const uint8_t px = (uint8_t)(x + 2U + col);
		const uint8_t top = src[col];
		const uint8_t bot = src[col + half];

		if (px >= 128) break;
		if (shift == 0) {
			if (row < FRAME_LINES) gFrameBuffer[row][px] |= top;
			if (row + 1U < FRAME_LINES) gFrameBuffer[row + 1U][px] |= bot;
		} else {
			if (row < FRAME_LINES)
				gFrameBuffer[row][px] |= (uint8_t)(top << shift);
			if (row + 1U < FRAME_LINES)
				gFrameBuffer[row + 1U][px] |= (uint8_t)((top >> (8U - shift)) | (bot << shift));
			if (row + 2U < FRAME_LINES)
				gFrameBuffer[row + 2U][px] |= (uint8_t)(bot >> (8U - shift));
		}
	}
}

static void UI_FM_DisplayFrequencyAt(uint8_t x, uint8_t y, const char *string, bool center, bool dashIsPlaceholder)
{
	const uint8_t char_width = 13U;
	uint8_t cx = x;
	bool bCanDisplay = false;
	const uint8_t len = (uint8_t)strlen(string);
	(void)dashIsPlaceholder;

	for (uint8_t i = 0; i < len; i++) {
		char c = string[i];
		if (c == '-')
			c = '9' + 1;
		else if (c == '_')
			c = '9' + 1;
		if (bCanDisplay || c != ' ') {
			bCanDisplay = true;
			if (c >= '0' && c <= '9' + 2) {
				UI_FM_BlitFreqGlyph(cx, y, (uint8_t)(c - '0'));
			} else if (c == '.') {
				const uint8_t dotY = (uint8_t)(y + 8U);
				const uint8_t row = (uint8_t)(dotY / 8U);
				const uint8_t shift = (uint8_t)(dotY % 8U);
				for (uint8_t d = 0; d < 3U; d++) {
					const uint8_t px = (uint8_t)(cx + 2U + d);
					if (px >= 128) break;
					if (shift == 0) {
						gFrameBuffer[row][px] |= 0x60U;
					} else {
						gFrameBuffer[row][px] |= (uint8_t)(0x60U << shift);
						if (row + 1U < FRAME_LINES)
							gFrameBuffer[row + 1U][px] |= (uint8_t)(0x60U >> (8U - shift));
					}
				}
				cx = (uint8_t)(cx + UI_FM_FREQ_DOT_WIDTH);
				continue;
			}
		} else if (center) {
			cx = (uint8_t)(cx - 6U);
		}
		cx = (uint8_t)(cx + char_width);
	}
}

static uint16_t UI_FM_MapRssiToUnits(uint8_t rssi, uint16_t fullScale, uint8_t cells)
{
	const uint16_t totalUnits = (uint16_t)cells * 4U;
	if (fullScale == 0) return 0;
	if (rssi > fullScale) rssi = (uint8_t)fullScale;
	return (uint16_t)rssi * totalUnits / fullScale;
}

/* S 表正下方：RSSI 左缘对齐进度条左缘，SNR 右缘对齐进度条右缘 */
static void UI_FM_DrawRsqBelowSmeter(uint8_t smeterX, uint8_t cells)
{
	uint8_t barLeft;
	uint8_t barRight;
	char valStr[12];

	UI_FM_GetSmeterBarBounds(smeterX, cells, &barLeft, &barRight);
	RSQ_GET();
	sprintf(valStr, "RSSI %u", (unsigned)rsqStatus.resp.RSSI);
	UI_FM_DrawSmallestAt(barLeft, UI_FM_RSQ_Y, valStr);
	sprintf(valStr, "SNR %u", (unsigned)rsqStatus.resp.SNR);
	UI_FM_DrawSmallestRight((uint8_t)(barRight - UI_FM_SNR_LEFT_SHIFT), UI_FM_RSQ_Y, valStr);
}
#endif

#ifdef ENABLE_FM_SI4732
/* 在任意像素 y 处绘制 gFontBig(7x16)；xor=true 时与背景异或（黑底白字） */
static void UI_FM_DrawBigStringAt(uint8_t x, uint8_t y, const char *s, bool invert)
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
			if (invert) {
				if (row < FRAME_LINES)        gFrameBuffer[row][cx]        ^= (uint8_t)(v & 0xFF);
				if (row + 1 < FRAME_LINES)    gFrameBuffer[row + 1][cx]    ^= (uint8_t)((v >> 8) & 0xFF);
				if (row + 2 < FRAME_LINES)    gFrameBuffer[row + 2][cx]    ^= (uint8_t)((v >> 16) & 0xFF);
			} else {
				if (row < FRAME_LINES)        gFrameBuffer[row][cx]        |= (uint8_t)(v & 0xFF);
				if (row + 1 < FRAME_LINES)    gFrameBuffer[row + 1][cx]    |= (uint8_t)((v >> 8) & 0xFF);
				if (row + 2 < FRAME_LINES)    gFrameBuffer[row + 2][cx]    |= (uint8_t)((v >> 16) & 0xFF);
			}
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

	/* 黑色实心背板 */
	UI_FillRectangleBuffer(gFrameBuffer,
		(uint8_t)x0, (uint8_t)y0,
		(uint8_t)(x0 + boxWidth - 1), (uint8_t)(y0 + boxHeight - 1),
		true);

	const uint8_t textX = (uint8_t)(x0 + (boxWidth - textW) / 2);
	const uint8_t textY = (uint8_t)(y0 + (boxHeight - 16) / 2);
	/* 黑底上 XOR 大字 → 反色白字 WAIT */
	UI_FM_DrawBigStringAt(textX, textY, msg, true);
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
			UI_FM_DrawSmeter(0, UI_FM_SMETER_Y, units, 15);
			UI_FM_DrawRsqBelowSmeter(0, 15);
		}
		/* AM/SSB: AGC/BW/BFO/AVC/SMT. STP is indicated only by the underline. */
		{
			const uint8_t focus = FM_GetAM_OptionFocus();
			static const uint8_t x[] = { 0, 26, 52, 78, 104 };
			static const char * const label[] = { "AGC", "BW", "BFO", "AVC", "SMT" };
			for (uint8_t i = 0; i < 5; i++) {
				const uint8_t dx = (label[i][2] == '\0') ? 7U : 2U;
				UI_FM_DrawSmallStringAt((uint8_t)(x[i] + dx), UI_FM_AM_OPT_LABEL_Y, label[i]);
			}
			UI_InvertRectangleBuffer(gFrameBuffer, x[focus < 5U ? focus : 0U], UI_FM_AM_OPT_INV_Y0,
				(uint8_t)(x[focus < 5U ? focus : 0U] + 23U), 46);
		}
		/* 底行(line 6)左侧：当前焦点对应的子选项值 */
		{
			char valStr[16];
			const uint8_t focus = FM_GetAM_OptionFocus();
			const uint8_t agc = FM_GetAM_LnaIndex();
			const uint8_t bw = FM_GetAM_BW_Index();
			static const char * const agcVals[] = { "AGC ON", "ATT 0", "ATT 1", "ATT 5", "ATT 15", "ATT 26" };
			static const char * const avcVals[] = { "MIN", "DEF", "MID", "MAX" };
			static const char * const smtVals[] = { "OFF", "4dB", "8dB", "12d" };
			static const char * const bwVals[] = { "0.5", "1.0", "1.2", "2.2", "3.0", "4.0", "5.0" };
			if (FM_IsSSBMode()) {
				if (focus == 0) sprintf(valStr, "%s", FM_GetSSB_AgcOn() ? "AGC ON" : "AGC FIX");
				else if (focus == 1) sprintf(valStr, "%sk", bw < 7u ? bwVals[bw] : "2.2");
				else if (focus == 2) sprintf(valStr, "BFO %+d", (int)FM_GetAM_BfoHz());
				else if (focus == 3) sprintf(valStr, "%s", avcVals[FM_GetSSB_AvcIndex() < 4u ? FM_GetSSB_AvcIndex() : 3]);
				else sprintf(valStr, "%s", smtVals[FM_GetSSB_SoftMuteIndex() < 4u ? FM_GetSSB_SoftMuteIndex() : 0]);
			} else {
				if (focus == 0) sprintf(valStr, "%s", agc < 6u ? agcVals[agc] : "AGC ON");
				else if (focus == 1) sprintf(valStr, "%sk", bw < 7u ? bwVals[bw] : "1.2");
				else if (focus == 2) sprintf(valStr, "BFO %+d", (int)FM_GetAM_BfoHz());
				else if (focus == 3) sprintf(valStr, "%s", avcVals[FM_GetAM_AvcIndex() < 4u ? FM_GetAM_AvcIndex() : 3]);
				else sprintf(valStr, "%s", smtVals[FM_GetAM_SoftMuteIndex() < 4u ? FM_GetAM_SoftMuteIndex() : 0]);
			}
			UI_PrintStringSmallNormal(valStr, 0, 0, 6);
		}
	} else
#endif
	{
		UI_PrintString("FM", 2, 0, 0, 8); /* 同上，Si4732 FM 模式 */
		{
			const uint8_t focus = FM_GetFM_OptionFocus();
			static const uint8_t x[] = { 0, 26, 52, 78, 104 };
			static const char * const label[] = { "AUD", "BW", "ANT", "AVC", "SMT" };
			for (uint8_t i = 0; i < 5; i++) {
				const uint8_t dx = (label[i][2] == '\0') ? 7U : 2U;
				if (i >= 3U)
					UI_FM_DrawDashedStringAt((uint8_t)(x[i] + dx), UI_FM_AM_OPT_LABEL_Y, label[i]);
				else
					UI_FM_DrawSmallStringAt((uint8_t)(x[i] + dx), UI_FM_AM_OPT_LABEL_Y, label[i]);
			}
			UI_InvertRectangleBuffer(gFrameBuffer, x[focus < 3U ? focus : 0U], UI_FM_AM_OPT_INV_Y0,
				(uint8_t)(x[focus < 3U ? focus : 0U] + 23U), 46);
		}
		{
			char valStr[16];
			const uint8_t focus = FM_GetFM_OptionFocus();
			static const char * const audVals[] = { "HI-FI", "NORM", "DX" };
			static const char * const bwVals[] = { "AUTO", "110K", "84K", "60K", "40K" };
			if (focus == 0U)
				sprintf(valStr, "%s", audVals[FM_GetFM_AudioProfile() < 3U ? FM_GetFM_AudioProfile() : 1U]);
			else if (focus == 1U)
				sprintf(valStr, "%s", bwVals[FM_GetFM_BW_Index() < 5U ? FM_GetFM_BW_Index() : 0U]);
			else
				sprintf(valStr, "%s", FM_GetFM_UseFMI() ? "FMI" : "AMI");
			UI_PrintStringSmallNormal(valStr, 0, 0, 6);
		}

		/* RSSI 强度条：BK1080 RSSI（REG_10），在频率下方 */
		if (gInputBoxIndex == 0) {
			const uint16_t st = BK1080_ReadRegister(BK1080_REG_10);
			/* FM 满刻度 70，线性映射 */
			const uint16_t units = (uint16_t)(uint8_t)BK1080_REG_10_GET_RSSI(st) * (15U * 4U) / 70U;
			UI_FM_DrawSmeter(0, UI_FM_SMETER_Y, units, 15);
			UI_FM_DrawRsqBelowSmeter(0, 15);
		}
	}

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
					sprintf(String, " %u.%03u", mhz, khz3);
				else
					sprintf(String, "%u.%03u", mhz, khz3);
			} else
#endif
			{
				const uint16_t freq10 = gEeprom.FM_FrequencyPlaying;
				/* <100.0 MHz：百位虚线 0，与 100.x 同宽（D99.9 / D09.5） */
				if (freq10 < 1000u)
					sprintf(String, " %02d.%d", freq10 / 10u, freq10 % 10u);
				else
					sprintf(String, "%3d.%d", freq10 / 10u, freq10 % 10u);
			}
		} else {
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
			} else {
				/* FM 输入显示：---.-（4 位），未输入用 '-' 占位 */
				char d[4] = {'-','-','-','-'};
				for (uint8_t i = 0; i < 4 && i < gInputBoxIndex; i++) {
					const uint8_t v = gInputBox[i];
					if (v <= 9)
						d[i] = (char)('0' + v);
				}
				sprintf(String, "%c%c%c.%c", d[0], d[1], d[2], d[3]);
			}
#else
			const char *ascii = INPUTBOX_GetAscii();
			sprintf(String, "%.3s.%.1s", ascii, ascii + 3);
#endif
		}

		/* FM：为了让 91.5 与 107.5 末位对齐（STP 下划线位置一致），禁止基于前导空格的“居中补偿” */
#ifdef ENABLE_FM_SI4732
		const bool centerFreq = false;
		const bool dashIsPlaceholder = (gInputBoxIndex > 0);
#else
		const bool centerFreq = false;
		const bool dashIsPlaceholder = (gInputBoxIndex > 0);
#endif
		UI_FM_DisplayFrequencyAt(36, UI_FM_FREQ_Y, String, centerFreq, dashIsPlaceholder);

#ifdef ENABLE_FM_SI4732
		/* AM/SSB：按 STP 在频率数字下方标记“下划线” */
		if (SI47XX_IsAMFamily() && gInputBoxIndex == 0) {
			UI_FM_DrawStepUnderline_AM(36, UI_FM_UNDERLINE_Y, String);
		} else if (!SI47XX_IsAMFamily() && gInputBoxIndex == 0) {
			UI_FM_DrawStepUnderline_FM(36, UI_FM_UNDERLINE_Y, String);
		}
#else
		if (gInputBoxIndex == 0) {
			UI_FM_DrawStepUnderline_FM(36, UI_FM_UNDERLINE_Y, String);
		}
#endif
		ST7565_BlitFullScreen();
		return;
	}

	UI_PrintString(String, 0, 127, 1, 10);

	ST7565_BlitFullScreen();
}

#endif

#endif
