/*
 * SNAKE_renderer.c — implementation of the frozen TFT contract v1.1.
 *
 *  LOCATION: APP/ (flat — no subfolders).
 *
 *  HTFT surface used (complete list): FillRect, DrawImage, DrawString.
 *  Nothing here touches MCAL directly; nothing here knows the rules.
 *
 *  PALETTE: every color comes from APP/SNAKE_config.h (zone 2). This
 *  file defines NO colors of its own — tune visuals in the config.
 *  The HEAD image below uses the config colors via the HH/EE macros.
 *
 *  Internal naming note: the renderer keeps its local SNAKE_COLOR_*
 *  aliases mapped 1:1 onto the SNAKE_CFG_COLOR_* config values, so
 *  the paint code reads short and the migration was a nine-line edit.
 */

#include <HAL/TFT/HTFT_interface.h>
#include "APP/SNAKE_renderer.h"
#include "APP/SNAKE_config.h"

/* Local aliases: one line per color, config is the single source    */
#define SNAKE_COLOR_HEAD      SNAKE_CFG_COLOR_HEAD
#define SNAKE_COLOR_BODY_A    SNAKE_CFG_COLOR_BODY_A
#define SNAKE_COLOR_BODY_B    SNAKE_CFG_COLOR_BODY_B
#define SNAKE_COLOR_FOOD      SNAKE_CFG_COLOR_FOOD
#define SNAKE_COLOR_FOOD_DIM  SNAKE_CFG_COLOR_FOOD_DIM
#define SNAKE_COLOR_BORDER    SNAKE_CFG_COLOR_BORDER
#define SNAKE_COLOR_BG        SNAKE_CFG_COLOR_BG
#define SNAKE_COLOR_HUD_SCORE SNAKE_CFG_COLOR_HUD_SCORE
#define SNAKE_COLOR_HUD_HI    SNAKE_CFG_COLOR_HUD_HI

/* ------------------------------------------------------------------ *
 *  HEAD IMAGE — 7x7 RGB565, fixed eyes (row 1, cols 1 and 5).
 *  Reads like ASCII art in the source — any tweak is a bit you can
 *  see. Painted via the public DrawImage: one window + one 49-pixel
 *  stream, identical cost to a plain fill.
 *
 *  Colors enter through the config aliases, so a palette edit in
 *  SNAKE_config.h changes the head too — one source of truth.
 * ------------------------------------------------------------------ */
#define HH   SNAKE_COLOR_HEAD
#define EE   HTFT_COLOR_BLACK             /* eyes: always black      */

static const u16 SNAKE_AU16Head[SNAKE_CELL_FILL * SNAKE_CELL_FILL] =
{
    HH, HH, HH, HH, HH, HH, HH,
    HH, EE, HH, HH, HH, EE, HH,      /* the eyes                      */
    HH, HH, HH, HH, HH, HH, HH,
    HH, HH, HH, HH, HH, HH, HH,
    HH, HH, HH, HH, HH, HH, HH,
    HH, HH, HH, HH, HH, HH, HH,
    HH, HH, HH, HH, HH, HH, HH
};

#undef HH
#undef EE

/* ------------------------------------------------------------------ *
 *  PRIVATE: the game's ONLY coordinate translation                    *
 * ------------------------------------------------------------------ */
static void SNAKE_privateCellToPixel(u8 Copy_u8Col, u8 Copy_u8Row,
                                     u16 *Copy_pu16Px, u16 *Copy_pu16Py)
{
    *Copy_pu16Px = (u16)(SNAKE_FIELD_X0 + ((u16)Copy_u8Col * SNAKE_CELL_PITCH));
    *Copy_pu16Py = (u16)(SNAKE_FIELD_Y0 + ((u16)Copy_u8Row * SNAKE_CELL_PITCH));
}

/* ------------------------------------------------------------------ *
 *  PUBLIC                                                             *
 * ------------------------------------------------------------------ */

/* SNAKE_renderer.h — void SNAKE_voidPaintField(u8 Copy_u8BorderOn); */
void SNAKE_voidPaintField(u8 Copy_u8BorderOn)
{
    HTFT_voidFillScreen(SNAKE_COLOR_BG);
    if (Copy_u8BorderOn != 0u)
    {
        HTFT_voidDrawRect(2u, 10u, 123u, 147u, SNAKE_COLOR_BORDER);
    }
}

void SNAKE_voidPaintCell(u8 Copy_u8Col, u8 Copy_u8Row, SNAKE_CellRole_t Copy_eRole)
{
    u16 Local_u16Px, Local_u16Py;

    /* insurance: never paint outside the field (caller owns logic)  */
    if ((Copy_u8Col >= SNAKE_FIELD_COLS) || (Copy_u8Row >= SNAKE_FIELD_ROWS))
    {
        return;
    }
    SNAKE_privateCellToPixel(Copy_u8Col, Copy_u8Row, &Local_u16Px, &Local_u16Py);

    switch (Copy_eRole)
    {
    case SNAKE_CELL_EMPTY:
        HTFT_voidFillRect(Local_u16Px, Local_u16Py, SNAKE_CELL_FILL, SNAKE_CELL_FILL, SNAKE_COLOR_BG);
        break;
    case SNAKE_CELL_BODY_A:
        HTFT_voidFillRect(Local_u16Px, Local_u16Py, SNAKE_CELL_FILL, SNAKE_CELL_FILL, SNAKE_COLOR_BODY_A);
        break;
    case SNAKE_CELL_BODY_B:
        HTFT_voidFillRect(Local_u16Px, Local_u16Py, SNAKE_CELL_FILL, SNAKE_CELL_FILL, SNAKE_COLOR_BODY_B);
        break;
    case SNAKE_CELL_HEAD:
        /* one window + one stream — same cost as a fill             */
        HTFT_voidDrawImage(Local_u16Px, Local_u16Py, SNAKE_CELL_FILL, SNAKE_CELL_FILL, SNAKE_AU16Head);
        break;
    case SNAKE_CELL_FOOD:
        HTFT_voidFillRect(Local_u16Px, Local_u16Py, SNAKE_CELL_FILL, SNAKE_CELL_FILL, SNAKE_COLOR_FOOD);
        break;
    case SNAKE_CELL_FOOD_DIM:
        HTFT_voidFillRect(Local_u16Px, Local_u16Py, SNAKE_CELL_FILL, SNAKE_CELL_FILL, SNAKE_COLOR_FOOD_DIM);
        break;
    default:
        break;
    }
}

void SNAKE_voidPaintScore(u16 Copy_u16Score)
{
    /* "SCORE:000" = 9 cells = 72 px at x=0. Fixed width: every
     * overwrite is a same-rect redraw — zero smearing.               */
    char Local_chScore[10] = "SCORE:000";
    Local_chScore[6] = (char)('0' + ((Copy_u16Score / 100u) % 10u));
    Local_chScore[7] = (char)('0' + ((Copy_u16Score / 10u)  % 10u));
    Local_chScore[8] = (char)('0' + (Copy_u16Score % 10u));
    HTFT_voidDrawString(0u, 0u, Local_chScore, SNAKE_COLOR_HUD_SCORE, SNAKE_COLOR_BG);
}

void SNAKE_voidPaintHi(u16 Copy_u16Hi)
{
    /* "HI:000" = 6 cells = 48 px at x=72 (right of SCORE:000)       */
    char Local_chHi[7] = "HI:000";
    Local_chHi[3] = (char)('0' + ((Copy_u16Hi / 100u) % 10u));
    Local_chHi[4] = (char)('0' + ((Copy_u16Hi / 10u)  % 10u));
    Local_chHi[5] = (char)('0' + (Copy_u16Hi % 10u));
    HTFT_voidDrawString(72u, 0u, Local_chHi, SNAKE_COLOR_HUD_HI, SNAKE_COLOR_BG);
}
