/*
 * TTT_app.h — the shared application flow state (M9).
 *  LOCATION: APP/ (flat).
 *
 *  The FSM state is shared app-level state: the enum and the
 *  variable live here; OWNERSHIP is of the HANDLING (the loop
 *  branch), which follows the enum's ranges:
 *    BOOT                 -> main.c        (the hub menu)
 *    NET_WAIT..JOIN_ENTRY -> TTT_session.c (the net states)
 *    ON_TURN..GAME_END    -> APP_TTT.c     (the game states)
 *
 *  The extern keeps the moved code verbatim: every
 *  `Game_eState = X` moves unchanged.
 */
#ifndef TTT_APP_H_
#define TTT_APP_H_

#include "STD_TYPES.h"

typedef enum
{
    GAME_ST_BOOT = 0,
    GAME_ST_NET_WAIT, GAME_ST_LOBBY, GAME_ST_HOST_WAIT, GAME_ST_JOIN_ENTRY,
    GAME_ST_ON_TURN, GAME_ST_ON_WAIT, GAME_ST_ON_GRACE, GAME_ST_EXIT_ASK,
    GAME_ST_ON_END,
    GAME_ST_HUMAN, GAME_ST_AI, GAME_ST_WIN_FLASH, GAME_ST_GAME_END
} Game_State_t;

/* ownership by range — the enum order is load-bearing */
#define APP_IS_NET_STATE(s)  (((s) >= GAME_ST_NET_WAIT) && ((s) <= GAME_ST_JOIN_ENTRY))
#define APP_IS_GAME_STATE(s) (((s) >= GAME_ST_ON_TURN)  && ((s) <= GAME_ST_GAME_END))

/* the shared flow-state variable (defined in TTT_app.c) */
extern Game_State_t Game_eState;

#endif /* TTT_APP_H_ */
