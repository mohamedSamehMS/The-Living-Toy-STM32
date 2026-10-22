/*
 * MM_renderer.c — implementation of the Memory Match TFT contract.
 *  LOCATION: APP/ (flat).
 *
 *  HTFT surface used (complete list): FillRect, DrawRect, DrawChar2x,
 *  DrawString. Zero new primitives.
 *
 *  Palette comes entirely from APP/MM_config.h (zone 2).
 *
 *  Cursor ring erase = repaint the card, then redraw nothing: the
 *  ring occupies only the 2-px gaps, so erasing means repainting the
 *  card UNDER it in its current state — the caller supplies that
 *  state via PaintCard before PaintCursor(...,0) is called.
 *  Implementation below erases with four bg strips (never touching
 *  the card), so the caller does NOT need to repaint the card —
 *  same as the keypad cursor. The card itself is never touched.
 */

#include <HAL/TFT/HTFT_interface.h>
#include "APP/MM_renderer.h"
#include "APP/MM_config.h"

/* local aliases — config is the single source of truth */
#define MM_COLOR_BG             MM_CFG_COLOR_BG
#define MM_COLOR_CARD_DOWN      MM_CFG_COLOR_CARD_DOWN
#define MM_COLOR_CARD_DOWN_BRD  MM_CFG_COLOR_CARD_DOWN_BRD
#define MM_COLOR_CARD_UP        MM_CFG_COLOR_CARD_UP
#define MM_COLOR_CARD_UP_BRD    MM_CFG_COLOR_CARD_UP_BRD
#define MM_COLOR_MATCHED        MM_CFG_COLOR_MATCHED
#define MM_COLOR_MATCHED_BRD    MM_CFG_COLOR_MATCHED_BRD
#define MM_COLOR_SYMBOL         MM_CFG_COLOR_SYMBOL
#define MM_COLOR_SYMBOL_MATCHED MM_CFG_COLOR_SYMBOL_MATCHED
#define MM_COLOR_CURSOR         MM_CFG_COLOR_CURSOR
#define MM_COLOR_HUD            MM_CFG_COLOR_HUD

/* ------------------------------------------------------------------ *
 *  PRIVATE: card index -> pixel geometry                              *
 * ------------------------------------------------------------------ */
static void MM_privateCardGeo(u8 Copy_u8CardIdx, u16 *Copy_pu16X, u16 *Copy_pu16Y)
{
    *Copy_pu16X = (u16)(MM_GRID_X0 + ((u16)(Copy_u8CardIdx % 4u) * MM_CARD_PITCH));
    *Copy_pu16Y = (u16)(MM_GRID_Y0 + ((u16)(Copy_u8CardIdx / 4u) * MM_CARD_PITCH));
}

/* ------------------------------------------------------------------ *
 *  PUBLIC                                                             *
 * ------------------------------------------------------------------ */

void MM_voidPaintScreen(void)
{
    u8 Local_u8I;

    HTFT_voidFillScreen(MM_COLOR_BG);

    for (Local_u8I = 0u; Local_u8I < MM_CARDS; Local_u8I++)
    {
        MM_voidPaintCard(Local_u8I, MM_CARD_DOWN, ' ');
    }

    MM_voidPaintPairs(0u);
    MM_voidPaintTries(0u);
    MM_voidPaintMessage("FIND THE PAIRS", MM_COLOR_HUD);
}

void MM_voidPaintCard(u8 Copy_u8CardIdx, MM_CardState_t Copy_eState, char Copy_chSym)
{
    u16 Local_u16X, Local_u16Y;

    if (Copy_u8CardIdx >= MM_CARDS)
    {
        return;   /* insurance: caller owns correctness              */
    }
    MM_privateCardGeo(Copy_u8CardIdx, &Local_u16X, &Local_u16Y);

    switch (Copy_eState)
    {
    case MM_CARD_DOWN:
        HTFT_voidFillRect(Local_u16X, Local_u16Y, MM_CARD_PX, MM_CARD_PX, MM_COLOR_CARD_DOWN);
        HTFT_voidDrawRect(Local_u16X, Local_u16Y, MM_CARD_PX, MM_CARD_PX, MM_COLOR_CARD_DOWN_BRD);
        break;

    case MM_CARD_UP:
        HTFT_voidFillRect(Local_u16X, Local_u16Y, MM_CARD_PX, MM_CARD_PX, MM_COLOR_CARD_UP);
        HTFT_voidDrawRect(Local_u16X, Local_u16Y, MM_CARD_PX, MM_CARD_PX, MM_COLOR_CARD_UP_BRD);
        /* 2x glyph (16x16) centered in the 24x24 card — glyph bg
         * matches the card fill so the cell doesn't punch a hole    */
        HTFT_voidDrawChar2x((u16)(Local_u16X + 4u), (u16)(Local_u16Y + 4u),
                            (u8)Copy_chSym, MM_COLOR_SYMBOL, MM_COLOR_CARD_UP);
        break;

    case MM_CARD_MATCHED:
        HTFT_voidFillRect(Local_u16X, Local_u16Y, MM_CARD_PX, MM_CARD_PX, MM_COLOR_CARD_UP);
        HTFT_voidDrawRect(Local_u16X, Local_u16Y, MM_CARD_PX, MM_CARD_PX, MM_COLOR_MATCHED_BRD);
        HTFT_voidDrawChar2x((u16)(Local_u16X + 4u), (u16)(Local_u16Y + 4u),
                            (u8)Copy_chSym, MM_COLOR_SYMBOL_MATCHED, MM_COLOR_CARD_UP);
        break;

    default:
        break;
    }
}

void MM_voidPaintCursor(u8 Copy_u8CardIdx, u8 Copy_u8Visible)
{
    u16 Local_u16X, Local_u16Y;

    if (Copy_u8CardIdx >= MM_CARDS)
    {
        return;
    }
    MM_privateCardGeo(Copy_u8CardIdx, &Local_u16X, &Local_u16Y);

    if (Copy_u8Visible != 0u)
    {
        /* 1-px yellow ring, 1 px outside the card — lives in the
         * 2-px gaps between cards                                     */
        HTFT_voidDrawRect((u16)(Local_u16X - 1u), (u16)(Local_u16Y - 1u),
                          26u, 26u, MM_COLOR_CURSOR);
    }
    else
    {
        /* erase the ring with four strips — the card itself is never
         * touched (its fill/border survive)                           */
        HTFT_voidFillRect((u16)(Local_u16X - 1u), (u16)(Local_u16Y - 1u), 26u, 1u, MM_COLOR_BG);
        HTFT_voidFillRect((u16)(Local_u16X - 1u), (u16)(Local_u16Y + MM_CARD_PX), 26u, 1u, MM_COLOR_BG);
        HTFT_voidFillRect((u16)(Local_u16X - 1u), Local_u16Y, 1u, MM_CARD_PX, MM_COLOR_BG);
        HTFT_voidFillRect((u16)(Local_u16X + MM_CARD_PX), Local_u16Y, 1u, MM_CARD_PX, MM_COLOR_BG);
    }
}

void MM_voidPaintPairs(u8 Copy_u8Pairs)
{
	/* FIX: was "PAIRS:0/8" (9 cells) — overlapped TRIES at x=64 by one
	       cell; 17 cells cannot pack into 16. "PAIR:0/8" = 8 cells.       */
	char Local_ch[9] = "PAIR:0/8";
	Local_ch[5] = (char)('0' + (Copy_u8Pairs % 10u));   /* index 6 -> 5 */
    HTFT_voidDrawString(0u, 0u, Local_ch, MM_COLOR_HUD, MM_COLOR_BG);
}

void MM_voidPaintTries(u16 Copy_u16Tries)
{
    /* "TRIES:00" = 8 cells = 64 px at x=64..127. Fixed width.       */
    char Local_ch[9] = "TRIES:00";
    Local_ch[6] = (char)('0' + ((Copy_u16Tries / 10u)  % 10u));
    Local_ch[7] = (char)('0' + (Copy_u16Tries % 10u));
    HTFT_voidDrawString(64u, 0u, Local_ch, MM_COLOR_HUD, MM_COLOR_BG);
}

void MM_voidPaintMessage(const char *Copy_chText, u16 Copy_u16Color)
{
    /* 16-char padded opaque overwrite — ghosting impossible          */
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
    HTFT_voidDrawString(0u, MM_MSG_Y, Local_chLine, Copy_u16Color, MM_COLOR_BG);
}
