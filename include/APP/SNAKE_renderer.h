/*
 * SNAKE_renderer.h — the TFT-side contract for Snake (frozen v1.1).
 *
 *  LOCATION: APP/ (flat — no subfolders).
 *
 *  The renderer is deliberately DUMB: it paints cell roles at cell
 *  coordinates and knows nothing about game rules, direction, or the
 *  body chain. All logic (parity choice, movement, death) lives in
 *  the caller. The app-layer FSM drives this exact interface.
 *
 *  Coordinate systems: game logic lives in CELLS (col 0..14,
 *  row 0..17). Pixels exist only inside SNAKE_renderer.c.
 *
 *  PALETTE NOTE: all colors are defined in APP/SNAKE_config.h (zone 2,
 *  VISUAL). The renderer consumes them; it defines none of its own.
 *
 *  Preconditions: HTFT_voidInit() has run; the ms timebase is alive.
 */

#ifndef SNAKE_RENDERER_H_
#define SNAKE_RENDERER_H_

#include "STD_TYPES.h"

/* ---- Geometry (frozen contract — change only with a redesign) ---- */
#define SNAKE_FIELD_COLS        15u
#define SNAKE_FIELD_ROWS        18u
#define SNAKE_CELL_PITCH        8u     /* cell spacing, px             */
#define SNAKE_CELL_FILL         7u     /* visible fill size, px        */
                                        /* -> 1-px gap between segments */
#define SNAKE_FIELD_X0          4u     /* pixel origin of cell (0,0)   */
#define SNAKE_FIELD_Y0          12u

/* ---- Cell roles (what to paint, never where/why) ---- */
typedef enum
{
    SNAKE_CELL_EMPTY = 0,      /* black — this IS the erase            */
    SNAKE_CELL_BODY_A,         /* body tone A (from config palette)    */
    SNAKE_CELL_BODY_B,         /* body tone B (from config palette)    */
    SNAKE_CELL_HEAD,           /* light green + fixed eyes (image)     */
    SNAKE_CELL_FOOD,           /* bright red (from config)             */
    SNAKE_CELL_FOOD_DIM        /* pulse dim state (from config)        */
} SNAKE_CellRole_t;

/* Entry/transition repaint: black canvas + field border. HUD values
 * follow via PaintScore / PaintHi (they are dynamic cells).          */
void SNAKE_voidPaintField(u8 Copy_u8BorderOn);

/* Paint ONE cell in the given role. Cell-aligned 7x7 rectangles only
 * — the no-partial-overlap law the whole redraw plan rests on.
 * Out-of-range cells are rejected silently (cheap insurance; the
 * caller owns correctness).                                          */
void SNAKE_voidPaintCell(u8 Copy_u8Col, u8 Copy_u8Row, SNAKE_CellRole_t Copy_eRole);

/* HUD cells — fixed-width strings: every update is a same-rect
 * opaque overwrite (no erase, no smear).                             */
void SNAKE_voidPaintScore(u16 Copy_u16Score);
void SNAKE_voidPaintHi(u16 Copy_u16Hi);

#endif /* SNAKE_RENDERER_H_ */
