/*
 * MM_renderer.h — the TFT-side contract for Memory Match.
 *  LOCATION: APP/ (flat).
 *
 *  The renderer paints the card grid, the four card states, the
 *  cursor ring, HUD cells, and the message line. It knows nothing
 *  about pairs, turns, or memory — the game logic drives this
 *  interface. Pixels exist only inside MM_renderer.c.
 *
 *  Card index: 0..15, row-major:
 *     0  1  2  3
 *     4  5  6  7
 *     8  9 10 11
 *    12 13 14 15
 *
 *  Preconditions: HTFT_voidInit() has run; ms timebase alive.
 */

#ifndef MM_RENDERER_H_
#define MM_RENDERER_H_

#include "STD_TYPES.h"

/* ---- Geometry (frozen contract facts, not tunables) ---- */
#define MM_CARD_PX               24u    /* card size                   */
#define MM_CARD_PITCH            26u    /* spacing (2-px gaps)         */
#define MM_GRID_X0               13u    /* grid origin (centered)      */
#define MM_GRID_Y0               12u
#define MM_CARDS                 16u

#define MM_MSG_Y                 122u   /* message line, 16-ch row     */

/* ---- Card states (what to paint) ---- */
typedef enum
{
    MM_CARD_DOWN = 0,          /* face-down: dark blue + darker border */
    MM_CARD_UP,                /* face-up: white + symbol              */
    MM_CARD_MATCHED            /* face-up: green border + green symbol */
} MM_CardState_t;

/* ---- API ---- */

/* Entry/transition repaint: bg + all 16 cards face-down + HUD.       */
void MM_voidPaintScreen(void);

/* Paint ONE card in the given state. Symbols render as 2x glyphs
 * centered in the card. Card-aligned 24x24 rects only — no partial
 * overlap by contract. Symbol is 'Copy_chSym' (e.g. 'A').           */
void MM_voidPaintCard(u8 Copy_u8CardIdx, MM_CardState_t Copy_eState,
                      char Copy_chSym);

/* Yellow cursor ring around a card (visible=1) / erase it (visible=0).
 * The ring lives in the 2-px gaps — the card itself is untouched
 * (the Math Quiz keypad-cursor pattern, third use).                 */
void MM_voidPaintCursor(u8 Copy_u8CardIdx, u8 Copy_u8Visible);

/* HUD cells — fixed-width opaque overwrites.                        */
void MM_voidPaintPairs(u8 Copy_u8Pairs);
void MM_voidPaintTries(u16 Copy_u16Tries);

/* Message line (y 122), 16-char padded opaque overwrite.            */
void MM_voidPaintMessage(const char *Copy_chText, u16 Copy_u16Color);

#endif /* MM_RENDERER_H_ */
