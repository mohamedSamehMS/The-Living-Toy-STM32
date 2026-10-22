/*
 * TTT_renderer.c — implementation of the Tic-Tac-Toe TFT contract.
 *  LOCATION: APP/ (flat).
 *
 *  HTFT surface used (complete list): FillRect, DrawHLine, DrawVLine,
 *  DrawLine (Bresenham), DrawCircle, DrawString. Zero new primitives.
 *
 *  Palette comes entirely from APP/TTT_config.h (zone 2).
 *
 *  X = two Bresenham diagonals, 3-px stroke (three 1-px offset passes).
 *  O = three concentric circles, r = O_RADIUS..O_RADIUS+2 (3-px ring).
 *  Both piece types render inside the cell's 40x40 area with margin.
 */

#include <HAL/TFT/HTFT_interface.h>
#include "APP/TTT_renderer.h"
#include "APP/TTT_config.h"

/* local aliases — config is the single source of truth */
#define TTT_COLOR_BG        TTT_CFG_COLOR_BG
#define TTT_COLOR_GRID      TTT_CFG_COLOR_GRID
#define TTT_COLOR_X         TTT_CFG_COLOR_X
#define TTT_COLOR_O         TTT_CFG_COLOR_O
#define TTT_COLOR_TEXT      TTT_CFG_COLOR_TEXT
#define TTT_COLOR_SCORE_D   TTT_CFG_COLOR_SCORE_D

/* ------------------------------------------------------------------ *
 *  PRIVATE: cell index -> pixel geometry                              *
 * ------------------------------------------------------------------ */
static void TTT_privateCellCenter(u8 Copy_u8CellIdx, u16 *Copy_pu16Cx, u16 *Copy_pu16Cy)
{
    u8 Local_u8Col = (u8)(Copy_u8CellIdx % 3u);
    u8 Local_u8Row = (u8)(Copy_u8CellIdx / 3u);
    *Copy_pu16Cx = (u16)(TTT_BOARD_X0 + 20u + (Local_u8Col * TTT_CELL_PX));
    *Copy_pu16Cy = (u16)(TTT_BOARD_Y0 + 20u + (Local_u8Row * TTT_CELL_PX));
}

/* ------------------------------------------------------------------ *
 *  PUBLIC                                                             *
 * ------------------------------------------------------------------ */

void TTT_voidPaintBoard(void)
{
    u8 Local_u8I;

    HTFT_voidFillScreen(TTT_COLOR_BG);

    /* grid: 4 interior lines (2 vertical, 2 horizontal)             */
    for (Local_u8I = 0u; Local_u8I < 2u; Local_u8I++)
    {
        HTFT_voidDrawVLine((u16)(TTT_BOARD_X0 + TTT_CELL_PX + (Local_u8I * TTT_CELL_PX)),
                           TTT_BOARD_Y0,
                           (u16)(3u * TTT_CELL_PX),
                           TTT_COLOR_GRID);
        HTFT_voidDrawHLine(TTT_BOARD_X0,
                           (u16)(TTT_BOARD_Y0 + TTT_CELL_PX + (Local_u8I * TTT_CELL_PX)),
                           (u16)(3u * TTT_CELL_PX),
                           TTT_COLOR_GRID);
    }
}

void TTT_voidPaintCell(u8 Copy_u8CellIdx, TTT_CellRole_t Copy_eRole)
{
    u16 Local_u16Cx, Local_u16Cy;
    s8 Local_s8Offset;

    if (Copy_u8CellIdx > 8u)
    {
        return;               /* insurance: caller owns correctness  */
    }
    TTT_privateCellCenter(Copy_u8CellIdx, &Local_u16Cx, &Local_u16Cy);

    switch (Copy_eRole)
    {
    case TTT_CELL_EMPTY:
        /* erase = repaint the cell area in background. Note the cell
         * area spans BETWEEN grid lines: clear 36 px centered, so the
         * neighboring grid lines are never touched.                  */
        HTFT_voidFillRect((u16)(Local_u16Cx - 18u), (u16)(Local_u16Cy - 18u),
                          36u, 36u, TTT_COLOR_BG);
        break;

    case TTT_CELL_X:
    	/* FULL repaint: wipe the cell first — the painter must leave
    	 * the cell in exactly this role's state regardless of prior
    	 * content. The cursor-ring erase path (MoveCursor) depends
    	 * on this completeness.                                      */
    	HTFT_voidFillRect((u16)(Local_u16Cx - 18u), (u16)(Local_u16Cy - 18u),
    			36u, 36u, TTT_COLOR_BG);
        /* two diagonals of span PIECE_SPAN, stroke via offset passes */
        for (Local_s8Offset = -((s8)(TTT_CFG_X_STROKE_PX / 2u));
             Local_s8Offset <= (s8)(TTT_CFG_X_STROKE_PX / 2u);
             Local_s8Offset++)
        {
            s8 Local_s8Half = (s8)(TTT_PIECE_SPAN / 2u);
            /* \ diagonal */
            HTFT_voidDrawLine((u16)((s32)Local_u16Cx - Local_s8Half + Local_s8Offset),
                              (u16)((s32)Local_u16Cy - Local_s8Half),
                              (u16)((s32)Local_u16Cx + Local_s8Half + Local_s8Offset),
                              (u16)((s32)Local_u16Cy + Local_s8Half),
                              TTT_COLOR_X);
            /* / diagonal */
            HTFT_voidDrawLine((u16)((s32)Local_u16Cx - Local_s8Half + Local_s8Offset),
                              (u16)((s32)Local_u16Cy + Local_s8Half),
                              (u16)((s32)Local_u16Cx + Local_s8Half + Local_s8Offset),
                              (u16)((s32)Local_u16Cy - Local_s8Half),
                              TTT_COLOR_X);
        }
        break;

    case TTT_CELL_O:
    	/* same wipe — then the three concentric circles            */
    	HTFT_voidFillRect((u16)(Local_u16Cx - 18u), (u16)(Local_u16Cy - 18u),
    			36u, 36u, TTT_COLOR_BG);
        for (Local_s8Offset = 0u; Local_s8Offset < (s8)TTT_CFG_O_STROKE_PX; Local_s8Offset++)
        {
            HTFT_voidDrawCircle(Local_u16Cx, Local_u16Cy,
                                (u16)(TTT_O_RADIUS + (u16)Local_s8Offset),
                                TTT_COLOR_O);
        }
        break;

    default:
        break;
    }
}
void TTT_voidPaintWinLine(u8 Copy_u8StartIdx, u8 Copy_u8EndIdx, u16 Copy_u16Color)
{
    u16 Local_u16X0, Local_u16Y0, Local_u16X1, Local_u16Y1;
    s32 Local_s32Dx, Local_s32Dy, Local_s32K;

    if ((Copy_u8StartIdx > 8u) || (Copy_u8EndIdx > 8u))
    {
        return;
    }
    TTT_privateCellCenter(Copy_u8StartIdx, &Local_u16X0, &Local_u16Y0);
    TTT_privateCellCenter(Copy_u8EndIdx,   &Local_u16X1, &Local_u16Y1);

    /* Extend both endpoints ~10 px outward along the line so the
     * strike clearly spans all three cells, not just center-to-
     * center. All 8 possible lines stay well inside the screen.    */
    Local_s32Dx = (s32)Local_u16X1 - (s32)Local_u16X0;
    Local_s32Dy = (s32)Local_u16Y1 - (s32)Local_u16Y0;
    if (Local_s32Dx > 0) { Local_u16X0 = (u16)(Local_u16X0 - 10u); Local_u16X1 = (u16)(Local_u16X1 + 10u); }
    else if (Local_s32Dx < 0) { Local_u16X0 = (u16)(Local_u16X0 + 10u); Local_u16X1 = (u16)(Local_u16X1 - 10u); }
    if (Local_s32Dy > 0) { Local_u16Y0 = (u16)(Local_u16Y0 - 10u); Local_u16Y1 = (u16)(Local_u16Y1 + 10u); }
    else if (Local_s32Dy < 0) { Local_u16Y0 = (u16)(Local_u16Y0 + 10u); Local_u16Y1 = (u16)(Local_u16Y1 - 10u); }

    /* UNIFORM BOLD STROKE — the fix for the thin-horizontal bug:
     * draw passes offset in x AND passes offset in y.
     *   vertical lines   -> x-offsets are the real thickness
     *   horizontal lines -> y-offsets are the real thickness
     *   45-deg diagonals -> both sets collapse onto the same
     *                       parallel-diagonal band (harmless overlap)
     * Result: same bold width on all 8 possible win lines.          */
    for (Local_s32K = -((s32)TTT_CFG_WINLINE_HALF_STROKE);
         Local_s32K <= (s32)TTT_CFG_WINLINE_HALF_STROKE;
         Local_s32K++)
    {
        HTFT_voidDrawLine((u16)((s32)Local_u16X0 + Local_s32K), Local_u16Y0,
                          (u16)((s32)Local_u16X1 + Local_s32K), Local_u16Y1,
                          Copy_u16Color);
        HTFT_voidDrawLine(Local_u16X0, (u16)((s32)Local_u16Y0 + Local_s32K),
                          Local_u16X1, (u16)((s32)Local_u16Y1 + Local_s32K),
                          Copy_u16Color);
    }
}

void TTT_voidPaintStatus(const char *Copy_chText, u16 Copy_u16Color)
{
    /* opaque full-width overwrite: pad by drawing 16-char row. The
     * string may be shorter — trailing area is background.          */
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
    HTFT_voidDrawString(0u, 0u, Local_chLine, Copy_u16Color, TTT_COLOR_BG);
}

void TTT_voidPaintScores(u8 Copy_u8XWins, u8 Copy_u8OWins, u8 Copy_u8Draws)
{
    /* "X:0 O:0 D:0" as three separate colored draws (fixed rects):
     *   X:0 at x=0 (cyan), O:0 at x=32 (yellow), D:0 at x=64 (gray) */
    char Local_ch[4] = "X:0";
    Local_ch[2] = (char)('0' + (Copy_u8XWins % 10u));
    HTFT_voidDrawString(0u, 144u, Local_ch, TTT_COLOR_X, TTT_COLOR_BG);

    Local_ch[0] = 'O'; Local_ch[1] = ':'; Local_ch[2] = (char)('0' + (Copy_u8OWins % 10u));
    HTFT_voidDrawString(32u, 144u, Local_ch, TTT_COLOR_O, TTT_COLOR_BG);

    Local_ch[0] = 'D'; Local_ch[1] = ':'; Local_ch[2] = (char)('0' + (Copy_u8Draws % 10u));
    HTFT_voidDrawString(64u, 144u, Local_ch, TTT_COLOR_SCORE_D, TTT_COLOR_BG);
}


/* ------------------------------------------------------------------ *
 *  CURSOR (input integration) — yellow ring, 33x33 (odd size ->
 *  pixel-centered on the cell center), inset inside the 36x36 clear
 *  area: 2-px clearance outside the largest piece geometry (O outer
 *  radius 14, X span ~13.5), so it never touches a piece.
 *  Erase = PaintCell(idx, role) — see the header contract.
 * ------------------------------------------------------------------ */
void TTT_voidPaintCursor(u8 Copy_u8CellIdx)
{
    u16 Local_u16Cx, Local_u16Cy;

    if (Copy_u8CellIdx > 8u)
    {
        return;   /* insurance: caller owns correctness              */
    }
    TTT_privateCellCenter(Copy_u8CellIdx, &Local_u16Cx, &Local_u16Cy);

    HTFT_voidDrawRect((u16)(Local_u16Cx - 16u), (u16)(Local_u16Cy - 16u),
                      33u, 33u, TTT_CFG_COLOR_CURSOR);
}
