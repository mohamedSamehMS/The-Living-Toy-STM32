/*
 * HUB_menu.h - THE GENERIC LIST ENGINE. One FSM + one UI for every
 *  menu in the project. Menus are DATA.
 *  LOCATION: APP/ (flat).
 *
 *  THE INSIGHT THIS FILE EXISTS FOR: every hub screen is a list.
 *  Home menu, game select, settings, leaderboard, game-over - all
 *  "rows, a cursor, scroll, select, back." The old design wrote
 *  five bespoke screens; this engine writes the behavior ONCE and
 *  reduces each screen to a descriptor. Adding a menu = adding a
 *  struct of strings.
 *
 *  WHY THE MENU "FSM" IS ALMOST NOTHING: for a list, the input
 *  events ARE the transitions - UP/DOWN move the cursor, CONFIRM
 *  selects, BACK exits. There is no state machine to build, only
 *  cursor arithmetic. (Games are the opposite - real FSMs with
 *  deadlines - which is exactly why their logic stays distributed
 *  in Run modules while menus centralize here.)
 *
 *  NAVIGATION CONTRACT:
 *    UP/DOWN   - CLAMP, never wrap: a held D-pad (repeats ARE the
 *                scroll, ~110 ms native glide) parks at the end
 *                row instead of spinning through the list forever.
 *    CONFIRM   - if OnRowConfirm is NULL: the engine RETURNS
 *                SELECTED with the row in Cursor (launch-style
 *                menus). If non-NULL: the callback fires and the
 *                engine STAYS (action-style rows - value cycling in
 *                Settings); CONFIRM is repeat-masked at the driver,
 *                one action per press.
 *    BACK      - always returns BACKED; the cursor position is
 *                preserved in Cursor (menus remember where you
 *                were - returning to the home menu lands you on
 *                the row you left from).
 *
 *  LIFETIME note: the descriptor is MODIFIED (Cursor in/out) -
 *  descriptors live in static storage in HUB_program.c, never on
 *  the stack (except the GameOver Info pointer - see HUB_program).
 */
#ifndef HUB_MENU_H_
#define HUB_MENU_H_
#include "STD_TYPES.h"

typedef struct
{
    const char          *Title;             /* 1..8 chars, 2x, centered */
    const char          *Info;              /* optional 1x line, <=16   */
    u8                   RowCount;
    const char * const  *Rows;              /* row labels               */
    u8                   Cursor;            /* IN/OUT - survives return */
    void (*GetRowValue)(u8 Copy_u8Row, char *Copy_pchOut, u8 Copy_u8MaxLen);
    void (*OnRowConfirm)(u8 Copy_u8Row);   /* NULL = select, else act  */
} HUB_Menu_t;

typedef enum { HUB_MENU_SELECTED = 0, HUB_MENU_BACKED } HUB_MenuResult_t;

/* Runs one menu interaction to selection or BACK. Blocks like every
 * proven consumer loop (stall-tolerant by design - see APP_INPUT). */
HUB_MenuResult_t HUB_MENU_enumRun(HUB_Menu_t *Copy_pstMenu);

/* Diagnostic: duration of the most recently measured event repaint. */

#endif /* HUB_MENU_H_ */
