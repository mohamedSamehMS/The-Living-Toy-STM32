/*
 * HUB_menu.c - THE GENERIC LIST ENGINE: one navigation FSM + one UI
 *  for every menu in the project. Menus are DATA.
 *  LOCATION: APP/ (flat).
 *
 *  THE INSIGHT THIS FILE EXISTS FOR: every hub screen is a list -
 *  home, settings, leaderboard, game-over. One engine writes the
 *  behavior ONCE; each screen is a descriptor (strings + callbacks).
 *  Adding a menu = adding a struct of strings, not code.
 *
 *  NAVIGATION CONTRACT (Rev 3 - the KEYPAD CONVENTION, system-wide):
 *    2 / 8  - up / down. TWO sources, deliberately both live:
 *             the KEYPAD digits (single-step per press - digits are
 *             repeat-masked at the HNEC_REMOTE driver, so holding 8
 *             can never spin the list) and the D-PAD (PREV/NEXT,
 *             repeat-allowed - hold = glide). The dual navigation
 *             map, TTT v1.2's proven pattern, now hub-wide.
 *    4 / 6  - left / right: NO-OP in vertical lists (they fall to
 *             default and are consumed). The axis exists for grid
 *             contexts - Math Quiz's on-screen keypad, Memory Match's
 *             card grid. Documented, not an error.
 *    CH     - CONFIRM: if OnRowConfirm is NULL the engine RETURNS
 *             HUB_MENU_SELECTED with the row in Cursor (launch-style
 *             menus). If non-NULL, the callback fires and the engine
 *             STAYS (action rows - value cycling in Settings); the
 *             engine repaints so a changed value lands immediately.
 *             CONFIRM is repeat-masked at the driver: one action per
 *             press (the guard below is belt-and-braces).
 *    0      - BACK: always returns HUB_MENU_BACKED; the cursor
 *             position is preserved in Cursor - menus remember where
 *             you were. Returning to the home menu lands you on the
 *             row you left from.
 *    Cursor movement CLAMPS at the ends (never wraps): a held D-pad
 *    parks on the last row instead of spinning through the list.
 *
 *  WHY THE MENU "FSM" IS ALMOST NOTHING: for a list, the input events
 *  ARE the transitions - UP/DOWN move the cursor, CONFIRM selects,
 *  BACK exits. Only cursor arithmetic remains. (Games are the
 *  opposite - real FSMs with deadlines - which is exactly why game
 *  logic stays distributed in Run modules while menus centralize here.)
 *
 *  COSTS: entry repaint = frame + N rows (N <= 8 rows of 1x text);
 *  cursor/value changes repaint only the affected row or two rows.
 *  Idle loop cost: one input poll + one ms read.
 *
 *  LIFETIME note: descriptors are MODIFIED (Cursor in/out) - they
 *  live as statics in HUB_program.c, never on the stack.
 *
 *  Revision history:
 *    Rev 1 - original listing
 *    Rev 2 - documentation pass + PC13 heartbeat in the menu loop
 *    Rev 3 - keypad navigation (2/8) - the system-wide convention
 *
 *  Created on: Sep 30, 2026
 */
#include "STD_TYPES.h"
#include <MCAL/SYSTICK/MSYSTICK_interface.h>
#include <MCAL/GPIO/MGPIO_interface.h>          /* PC13 heartbeat */
#include "APP/HUB_menu.h"
#include "APP/HUB_renderer.h"
#include "APP/HUB_config.h"
#include "APP/APP_INPUT.h"
#include "HAL/AUDIO/HAUDIO_interface.h"

static const SongNote_t HUB_astLobbyTheme[] =
    { { 392u, 400u }, { 0u, 200u }, { 523u, 400u }, { 0u, 200u },
      { 659u, 600u }, { 0u, 400u }, { 523u, 400u }, { 0u, 200u },
      { 392u, 400u }, { 0u, 200u }, { 330u, 800u }, { 0u, 800u } };
static const Song_t HUB_stLobbyTheme =
    { HUB_astLobbyTheme, 12u, 1u, 3u };

static void HUB_MENU_privatePaintRow(const HUB_Menu_t *Copy_pstMenu,
                                      u8 Copy_u8Row, u8 Copy_u8Selected)
{
    char Local_achVal[HUB_MENU_VALUE_LEN + 1u];
    const char *Local_pchVal = (const char *)0;

    if (Copy_pstMenu->GetRowValue != (void (*)(u8, char *, u8))0)
    {
        Copy_pstMenu->GetRowValue(Copy_u8Row, Local_achVal,
                                  (u8)(HUB_MENU_VALUE_LEN + 1u));
        Local_pchVal = Local_achVal;
    }
    HUB_RENDERER_voidPaintRow(Copy_u8Row,
                              Copy_pstMenu->Rows[Copy_u8Row],
                              Local_pchVal, Copy_u8Selected);
}

/* Repaint the whole list (frame + every row, selection marked).
 * Used on screen entry. */
static void HUB_MENU_privatePaintAll(const HUB_Menu_t *Copy_pstMenu, u8 Copy_u8Row)
{
    u8 Local_u8I;

    HUB_RENDERER_voidPaintFrame(Copy_pstMenu->Title, Copy_pstMenu->Info);
    HAUDIO_voidTask();
    for (Local_u8I = 0u; Local_u8I < Copy_pstMenu->RowCount; Local_u8I++)
    {
        HUB_MENU_privatePaintRow(Copy_pstMenu, Local_u8I,
                                 (u8)(Local_u8I == Copy_u8Row));
    }
}

HUB_MenuResult_t HUB_MENU_enumRun(HUB_Menu_t *Copy_pstMenu)
{
    u8  Local_u8Row = Copy_pstMenu->Cursor;    /* IN: remembered row  */
    u32 Local_u32LastEvt = MSYSTICK_GetElapsed_ms();

    HUB_MENU_privatePaintAll(Copy_pstMenu, Local_u8Row);

    while (1)
    {
        APP_INPUT_Event_t Local_stEvt;
        u32 Local_u32Now = MSYSTICK_GetElapsed_ms();

        HAUDIO_voidTask();
        if (HAUDIO_enumGetState() == HAUDIO_STATE_IDLE)
        {
            (void)HAUDIO_enumPlaySong(&HUB_stLobbyTheme);
        }

        /* Heartbeat (PC13, 500 ms): the hub's liveness channel. Runs
         * in every menu because the engine IS the hub loop here. It
         * pauses during a game's Run (the game owns its loop - the
         * documented trade) and resumes the moment Run returns.     */
        {
            static u32 Local_u32ShownHalf = 0u;
            if ((Local_u32Now / 500u) != Local_u32ShownHalf)
            {
                static u8 Local_u8Beat = 0u;
                Local_u32ShownHalf = Local_u32Now / 500u;
                Local_u8Beat = (u8)!Local_u8Beat;
                (void)MGPIO_SetPinValue(MGPIO_PORTC, MGPIO_PIN_13,
                        (Local_u8Beat != 0u) ? MGPIO_PIN_HIGH : MGPIO_PIN_LOW);
            }
        }

        if (APP_INPUT_enumGetEvent(&Local_stEvt) == APP_INPUT_OK)
        {
        	Local_u32LastEvt = Local_u32Now;   /* any event feeds kiosk */
        	u8 Local_u8PreRow = Local_u8Row;   /* ghost-refresh fix: the
        	                                                  row before this event */
            u8 Local_u8NeedsPaint = 0u;

            switch (Local_stEvt.Button)
            {
            case BTN_UP:
            case BTN_NUM_2:      /* KEYPAD CONVENTION: 2 = up - single
                                   step (digits are repeat-masked at the
                                   driver). The D-pad case keeps its
                                   glide (repeats ARE the scroll).      */
                if (Local_u8Row > 0u) { Local_u8Row--; }
                break;

            case BTN_DOWN:
            case BTN_NUM_8:      /* KEYPAD CONVENTION: 8 = down.       */
                Local_u8Row = (u8)((Local_u8Row + 1u) % Copy_pstMenu->RowCount);
                break;

            /* BTN_NUM_4 / BTN_NUM_6 (left/right) fall to default on
             * purpose: vertical lists have no horizontal axis. They
             * are consumed here, not errors - the axis is live in
             * grid contexts (Math Quiz keypad, MM cards).            */

            case BTN_CONFIRM:
            	if (Local_stEvt.IsRepeat != 0u) { break; }
            	if (Copy_pstMenu->OnRowConfirm != (void (*)(u8))0)
            	{
            		Copy_pstMenu->OnRowConfirm(Local_u8Row);
            		Local_u8NeedsPaint = 1u;    /* value may have cycled */
            	}
            	else
            	{
            		Copy_pstMenu->Cursor = Local_u8Row;
            		return HUB_MENU_SELECTED;
            	}
            	break;


            case BTN_BACK:
                if (Local_stEvt.IsRepeat != 0u) { break; }
                Copy_pstMenu->Cursor = Local_u8Row;   /* remember + exit */
                return HUB_MENU_BACKED;

            default:
                break;   /* unmapped buttons consumed, ignored         */
            }
            /* Repaint only the rows affected by this event. A clamped no-op
             * (UP at row 0, 4/6 in a vertical list, unmapped buttons) consumed
             * the event but moved nothing - repaint would be pure SPI traffic. This is
             * the post-release "ghost refresh" fix: held-repeat
             * trains at a clamped edge now consume silently. */
            if (Local_u8Row != Local_u8PreRow)
            {
                HUB_MENU_privatePaintRow(Copy_pstMenu, Local_u8PreRow, 0u);
                HUB_MENU_privatePaintRow(Copy_pstMenu, Local_u8Row, 1u);
            }
            else if (Local_u8NeedsPaint != 0u)
            {
                HUB_MENU_privatePaintRow(Copy_pstMenu, Local_u8Row, 1u);
            }
        }

        /* Kiosk attract mode (config-gated, 0 = off): no input for
         * the whole window -> advance the cursor, wrapping. This is
         * the "auto-cycling menu" from the design notes, and it is
         * why Local_u32LastEvt exists at all.                        */
        if ((HUB_CFG_KIOSK_CYCLE_MS != 0u) &&
            ((Local_u32Now - Local_u32LastEvt) >= HUB_CFG_KIOSK_CYCLE_MS))
        {
            Local_u32LastEvt = Local_u32Now;
            {
                u8 Local_u8PreRow = Local_u8Row;
                Local_u8Row = (u8)((Local_u8Row + 1u) % Copy_pstMenu->RowCount);
                HUB_MENU_privatePaintRow(Copy_pstMenu, Local_u8PreRow, 0u);
                HUB_MENU_privatePaintRow(Copy_pstMenu, Local_u8Row, 1u);
            }
        }
    }

}
