/*
 * MATHQ_renderer.c — implementation of the Math Quiz TFT contract.
 *  LOCATION: APP/ (flat).
 *
 *  HTFT surface used (complete list): FillRect, DrawRect, DrawString,
 *  DrawString2x, DrawChar2x, DrawImage. Zero new primitives.
 *
 *  Palette comes entirely from APP/MATHQ_config.h (zone 2).
 *
 *  The heart is the gallery-test heart image, reused verbatim — the
 *  project's first asset reuse across games.
 */

#include <HAL/TFT/HTFT_interface.h>
#include "APP/MATHQ_renderer.h"
#include "APP/MATHQ_config.h"

/* local aliases — config is the single source of truth */
#define MATHQ_COLOR_BG            MATHQ_CFG_COLOR_BG
#define MATHQ_COLOR_PANEL_BORDER  MATHQ_CFG_COLOR_PANEL_BORDER
#define MATHQ_COLOR_QUESTION      MATHQ_CFG_COLOR_QUESTION
#define MATHQ_COLOR_KEY_FILL      MATHQ_CFG_COLOR_KEY_FILL
#define MATHQ_COLOR_KEY_BORDER    MATHQ_CFG_COLOR_KEY_BORDER
#define MATHQ_COLOR_KEY_PRESSED   MATHQ_CFG_COLOR_KEY_PRESSED
#define MATHQ_COLOR_KEY_BORDER_HI MATHQ_CFG_COLOR_KEY_BORDER_HI
#define MATHQ_COLOR_CURSOR        MATHQ_CFG_COLOR_CURSOR
#define MATHQ_COLOR_HUD           MATHQ_CFG_COLOR_HUD
#define MATHQ_COLOR_PROMPT        MATHQ_CFG_COLOR_PROMPT

/* ------------------------------------------------------------------ *
 *  HEART IMAGE — 8x8 RGB565, the gallery-test heart (red body, white
 *  glint, black bg). The bg pixels match the HUD row's background,
 *  so the image lands cleanly in the 8-px HUD row.
 * ------------------------------------------------------------------ */
#define MATHQ_HR   HTFT_COLOR_RED
#define MATHQ_HB   HTFT_COLOR_BLACK
#define MATHQ_HW   HTFT_COLOR_WHITE

static const u16 MATHQ_AU16Heart[64] =
{
    MATHQ_HB, MATHQ_HR, MATHQ_HR, MATHQ_HB, MATHQ_HB, MATHQ_HR, MATHQ_HR, MATHQ_HB,
    MATHQ_HR, MATHQ_HR, MATHQ_HR, MATHQ_HR, MATHQ_HR, MATHQ_HR, MATHQ_HR, MATHQ_HR,
    MATHQ_HR, MATHQ_HR, MATHQ_HW, MATHQ_HR, MATHQ_HR, MATHQ_HR, MATHQ_HR, MATHQ_HR,
    MATHQ_HR, MATHQ_HR, MATHQ_HR, MATHQ_HR, MATHQ_HR, MATHQ_HR, MATHQ_HR, MATHQ_HR,
    MATHQ_HB, MATHQ_HR, MATHQ_HR, MATHQ_HR, MATHQ_HR, MATHQ_HR, MATHQ_HR, MATHQ_HB,
    MATHQ_HB, MATHQ_HB, MATHQ_HR, MATHQ_HR, MATHQ_HR, MATHQ_HR, MATHQ_HB, MATHQ_HB,
    MATHQ_HB, MATHQ_HB, MATHQ_HB, MATHQ_HR, MATHQ_HR, MATHQ_HB, MATHQ_HB, MATHQ_HB,
    MATHQ_HB, MATHQ_HB, MATHQ_HB, MATHQ_HB, MATHQ_HB, MATHQ_HB, MATHQ_HB, MATHQ_HB
};

#undef MATHQ_HR
#undef MATHQ_HB
#undef MATHQ_HW

/* ------------------------------------------------------------------ *
 *  Keypad labels — index 0..11 (see the layout map in the header)    *
 * ------------------------------------------------------------------ */
static const char MATHQ_AchKeyLabels[12] =
{
    '1', '2', '3', '4', '5', '6', '7', '8', '9', '<', '0', '='
};

/* ------------------------------------------------------------------ *
 *  PRIVATE: layout state + key geometry                               *
 * ------------------------------------------------------------------ */

/* Set by PaintQuestionLine, consumed by PaintAnswer — the answer
 * slot position of the CURRENT question.                            */
static u8  MATHQ_u8StaticLen = 4u;
static u16 MATHQ_u16LineX    = 16u;

static void MATHQ_privateKeyGeo(u8 Copy_u8KeyIdx, u16 *Copy_pu16X, u16 *Copy_pu16Y)
{
    *Copy_pu16X = (u16)(MATHQ_KEYPAD_X0 + ((u16)(Copy_u8KeyIdx % 3u) * MATHQ_KEY_PITCH));
    *Copy_pu16Y = (u16)(MATHQ_KEYPAD_Y0 + ((u16)(Copy_u8KeyIdx / 3u) * MATHQ_KEY_PITCH));
}

/* ------------------------------------------------------------------ *
 *  PUBLIC                                                             *
 * ------------------------------------------------------------------ */

void MATHQ_voidPaintScreen(void)
{
    u8 Local_u8I;

    HTFT_voidFillScreen(MATHQ_COLOR_BG);
    HTFT_voidDrawRect(MATHQ_PANEL_X, MATHQ_PANEL_Y, MATHQ_PANEL_W, MATHQ_PANEL_H,
                      MATHQ_COLOR_PANEL_BORDER);

    MATHQ_voidPaintScore(0u);
    MATHQ_voidPaintLevel(1u);
    for (Local_u8I = 0u; Local_u8I < 3u; Local_u8I++)
    {
        MATHQ_voidPaintHeart(Local_u8I, 1u);
    }
    MATHQ_voidPaintMessage("TYPE ANSWER", MATHQ_COLOR_PROMPT);

    for (Local_u8I = 0u; Local_u8I < MATHQ_KEYPAD_KEYS; Local_u8I++)
    {
        MATHQ_voidPaintKey(Local_u8I, 0u);
    }
}

void MATHQ_voidErasePanelInterior(void)
{
    /* interior of the panel box: border lines stay intact            */
    HTFT_voidFillRect(5u, 11u, 118u, 26u, MATHQ_COLOR_BG);
}

void MATHQ_voidPaintQuestionLine(u8 Copy_u8A, char Copy_chOp, u8 Copy_u8B,
                                 const char *Copy_chAns, u8 Copy_u8AnsLen,
                                 u16 Copy_u16AnsColor, s8 Copy_s8Shift)
{
    char Local_chStatic[8];
    u8  Local_u8StaticLen = 0u;
    u16 Local_u16LineX;
    char Local_chAnsPadded[3];

    /* WIPE FIRST (bug fix): the line is CENTERED on its content, so
     * the answer-slot rectangle MOVES between questions (e.g. 4-char
     * equation -> answer cells 80..111; 5-char -> 88..119). A 2-digit
     * answer under a wide equation can stick out past a narrower
     * equation's coverage and survive as a half-digit ghost. Wiping
     * the panel interior makes every question a clean sub-region
     * repaint — the transition rule applied at panel scale.          */
    MATHQ_voidErasePanelInterior();

    /* build "a op b =" — at most one two-digit operand (generator
     * contract), so the static part is 4..5 chars                    */
    if (Copy_u8A >= 10u)
    {
        Local_chStatic[Local_u8StaticLen++] = (char)('0' + (Copy_u8A / 10u));
    }
    Local_chStatic[Local_u8StaticLen++] = (char)('0' + (Copy_u8A % 10u));
    Local_chStatic[Local_u8StaticLen++] = Copy_chOp;
    if (Copy_u8B >= 10u)
    {
        Local_chStatic[Local_u8StaticLen++] = (char)('0' + (Copy_u8B / 10u));
    }
    Local_chStatic[Local_u8StaticLen++] = (char)('0' + (Copy_u8B % 10u));
    Local_chStatic[Local_u8StaticLen++] = '=';
    Local_chStatic[Local_u8StaticLen] = '\0';

    /* center: static slots + 2 reserved answer slots (16 px each)   */
    Local_u16LineX = (u16)((128u - (16u * ((u16)Local_u8StaticLen + 2u))) / 2u);

    /* remember the unshifted slot position for PaintAnswer          */
    MATHQ_u8StaticLen = Local_u8StaticLen;
    MATHQ_u16LineX    = Local_u16LineX;

    HTFT_voidDrawString2x((u16)((s32)Local_u16LineX + (s32)Copy_s8Shift), MATHQ_LINE_Y,
                          Local_chStatic, MATHQ_COLOR_QUESTION, MATHQ_COLOR_BG);

    /* answer slots: drawn digits, blank-padded to 2 slots           */
    Local_chAnsPadded[0] = (Copy_u8AnsLen > 0u) ? Copy_chAns[0] : ' ';
    Local_chAnsPadded[1] = (Copy_u8AnsLen > 1u) ? Copy_chAns[1] : ' ';
    Local_chAnsPadded[2] = '\0';
    HTFT_voidDrawString2x((u16)((s32)Local_u16LineX + (16u * (s32)Local_u8StaticLen) + (s32)Copy_s8Shift),
                          MATHQ_LINE_Y, Local_chAnsPadded, Copy_u16AnsColor, MATHQ_COLOR_BG);
}

void MATHQ_voidPaintAnswer(const char *Copy_chDigits, u8 Copy_u8Len, u16 Copy_u16Color)
{
    char Local_chPadded[3];

    Local_chPadded[0] = (Copy_u8Len > 0u) ? Copy_chDigits[0] : ' ';
    Local_chPadded[1] = (Copy_u8Len > 1u) ? Copy_chDigits[1] : ' ';
    Local_chPadded[2] = '\0';

    /* opaque cells at the reserved slots — the blank slot self-erases */
    HTFT_voidDrawString2x((u16)(MATHQ_u16LineX + (16u * (u32)MATHQ_u8StaticLen)),
                          MATHQ_LINE_Y, Local_chPadded, Copy_u16Color, MATHQ_COLOR_BG);
}

void MATHQ_voidPaintKey(u8 Copy_u8KeyIdx, u8 Copy_u8Pressed)
{
    u16 Local_u16X, Local_u16Y;
    u16 Local_u16Fill = (Copy_u8Pressed != 0u) ? MATHQ_COLOR_KEY_PRESSED : MATHQ_COLOR_KEY_FILL;

    if (Copy_u8KeyIdx >= MATHQ_KEYPAD_KEYS)
    {
        return;   /* insurance: caller owns correctness              */
    }
    MATHQ_privateKeyGeo(Copy_u8KeyIdx, &Local_u16X, &Local_u16Y);

    HTFT_voidFillRect(Local_u16X, Local_u16Y, MATHQ_KEY_PX, MATHQ_KEY_PX, Local_u16Fill);
    HTFT_voidDrawRect(Local_u16X, Local_u16Y, MATHQ_KEY_PX, MATHQ_KEY_PX,
                      (Copy_u8Pressed != 0u) ? MATHQ_COLOR_KEY_BORDER_HI : MATHQ_COLOR_KEY_BORDER);
    /* 2x glyph centered in the 24x24 key — glyph bg matches the key
     * fill so the cell doesn't punch a black hole in the key        */
    HTFT_voidDrawChar2x((u16)(Local_u16X + 4u), (u16)(Local_u16Y + 4u),
                        (u8)MATHQ_AchKeyLabels[Copy_u8KeyIdx],
                        MATHQ_COLOR_QUESTION, Local_u16Fill);
}

void MATHQ_voidPaintKeyCursor(u8 Copy_u8KeyIdx, u8 Copy_u8Visible)
{
    u16 Local_u16X, Local_u16Y;

    if (Copy_u8KeyIdx >= MATHQ_KEYPAD_KEYS)
    {
        return;
    }
    MATHQ_privateKeyGeo(Copy_u8KeyIdx, &Local_u16X, &Local_u16Y);

    if (Copy_u8Visible != 0u)
    {
        /* 1-px yellow ring, 2 px outside the key — lives entirely in
         * the black gaps between keys                                */
        HTFT_voidDrawRect((u16)(Local_u16X - 2u), (u16)(Local_u16Y - 2u),
                          28u, 28u, MATHQ_COLOR_CURSOR);
    }
    else
    {
        /* erase the ring with four strips around the key — the key
         * itself is never touched (its fill/border survive)          */
        HTFT_voidFillRect((u16)(Local_u16X - 2u), (u16)(Local_u16Y - 2u), 28u, 2u, MATHQ_COLOR_BG);
        HTFT_voidFillRect((u16)(Local_u16X - 2u), (u16)(Local_u16Y + MATHQ_KEY_PX), 28u, 2u, MATHQ_COLOR_BG);
        HTFT_voidFillRect((u16)(Local_u16X - 2u), Local_u16Y, 2u, MATHQ_KEY_PX, MATHQ_COLOR_BG);
        HTFT_voidFillRect((u16)(Local_u16X + MATHQ_KEY_PX), Local_u16Y, 2u, MATHQ_KEY_PX, MATHQ_COLOR_BG);
    }
}

void MATHQ_voidPaintHeart(u8 Copy_u8Idx, u8 Copy_u8Visible)
{
    if (Copy_u8Idx > 2u)
    {
        return;
    }
    if (Copy_u8Visible != 0u)
    {
        HTFT_voidDrawImage((u16)(80u + (8u * (u16)Copy_u8Idx)), 0u, 8u, 8u, MATHQ_AU16Heart);
    }
    else
    {
        HTFT_voidFillRect((u16)(80u + (8u * (u16)Copy_u8Idx)), 0u, 8u, 8u, MATHQ_COLOR_BG);
    }
}

void MATHQ_voidPaintScore(u16 Copy_u16Score)
{
    /* "SCORE:000" = 9 cells = 72 px, x 0..71. Fixed width: every
     * overwrite is a same-rect redraw — zero smearing.               */
    char Local_chScore[10] = "SCORE:000";
    Local_chScore[6] = (char)('0' + ((Copy_u16Score / 100u) % 10u));
    Local_chScore[7] = (char)('0' + ((Copy_u16Score / 10u)  % 10u));
    Local_chScore[8] = (char)('0' + (Copy_u16Score % 10u));
    HTFT_voidDrawString(0u, 0u, Local_chScore, MATHQ_COLOR_HUD, MATHQ_COLOR_BG);
}

void MATHQ_voidPaintLevel(u8 Copy_u8Level)
{
    /* "L:1" = 3 cells = 24 px at x 104..127 (HUD packing: score
     * 0..71, hearts 80..103, level 104..127 = exactly 128)          */
    char Local_chLevel[4] = "L:1";
    Local_chLevel[2] = (char)('0' + (Copy_u8Level % 10u));
    HTFT_voidDrawString(104u, 0u, Local_chLevel, MATHQ_COLOR_HUD, MATHQ_COLOR_BG);
}

void MATHQ_voidPaintMessage(const char *Copy_chText, u16 Copy_u16Color)
{
    /* 16-char padded opaque overwrite — ghosting impossible         */
    char Local_chLine[17];
    u8 Local_u8I;

    for (Local_u8I = 0u; Local_u8I < 16u; Local_u8I++)
    {
        Local_chLine[Local_u8I] = ' ';
    }
    Local_chLine[16] = '\0';

    for (Local_u8I = 0u; (Local_u8I < 16u) && (Copy_chText[Local_u8I] != '\0'); Local_u8I++)
    {
        Local_chLine[Local_u8I] = Copy_chText[Local_u8I];
    }
    HTFT_voidDrawString(0u, MATHQ_MSG_Y, Local_chLine, Copy_u16Color, MATHQ_COLOR_BG);
}
