/*
 * TTT_renderer.h — the TFT-side contract for Tic-Tac-Toe.
 *  LOCATION: APP/ (flat).
 *
 *  The renderer paints the board, pieces, win line, and HUD. It knows
 *  nothing about turns, AI, or rules — the game logic drives this
 *  interface. Pixels exist only inside TTT_renderer.c.
 *
 *  Preconditions: HTFT_voidInit() has run; ms timebase alive.
 */

#ifndef TTT_RENDERER_H_
#define TTT_RENDERER_H_

#include "STD_TYPES.h"

/* ---- Geometry (frozen contract facts, not tunables) ---- */
#define TTT_CELL_PX              40u    /* cell size                  */
#define TTT_BOARD_X0             4u     /* board pixel origin         */
#define TTT_BOARD_Y0             16u
/* derived: board 120x120 at x 4..123, y 16..135; grid lines at
 * x = 44, 84 and y = 56, 96; cell centers at +20 offsets           */
#define TTT_PIECE_SPAN           25u    /* X diagonal span, px        */
#define TTT_O_RADIUS             12u    /* O outer radius             */

/* ---- Cell roles ---- */
typedef enum
{
    TTT_CELL_EMPTY = 0,
    TTT_CELL_X,
    TTT_CELL_O
} TTT_CellRole_t;

/* Cell index: 0..8, row-major:
 *   0 1 2
 *   3 4 5
 *   6 7 8                                                    */

/* Entry/transition repaint: background + grid lines. HUD follows
 * via PaintStatus / PaintScores.                                     */
void TTT_voidPaintBoard(void);

/* Paint one piece into a cell (EMPTY = erase that cell's area).
 * Cell-aligned rectangles only — no partial overlap by contract.    */
void TTT_voidPaintCell(u8 Copy_u8CellIdx, TTT_CellRole_t Copy_eRole);

/* Strike line through the winning 3 cells (start/end = cell idx).   */
void TTT_voidPaintWinLine(u8 Copy_u8StartIdx, u8 Copy_u8EndIdx, u16 Copy_u16Color);

/* Status line, y 0..7, opaque overwrite.                            */
void TTT_voidPaintStatus(const char *Copy_chText, u16 Copy_u16Color);

/* Scoreboard: "X:0 O:0 D:0", y 144..151, opaque overwrite.          */
void TTT_voidPaintScores(u8 Copy_u8XWins, u8 Copy_u8OWins, u8 Copy_u8Draws);

/* ---- added with the input integration (first consumer: the
 *      remote-driven cursor) ---- */

/** @brief Draw the 1-px yellow cursor ring just inside the cell's
 *         clear area (33x33, pixel-centered). Drawn OVER any cell
 *         state. ERASING is the CALLER's job: PaintCell(idx, role)
 *         fully repaints the cell — the renderer is stateless and
 *         cannot know what to restore underneath.                   */
void TTT_voidPaintCursor(u8 Copy_u8CellIdx);

#endif /* TTT_RENDERER_H_ */
