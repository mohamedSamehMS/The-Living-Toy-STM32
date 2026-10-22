/*
 * APP_FOTA_APP.h — THE UPDATE MODULE contract. FILENAME IS APP_FOTA_APP.h.
 *  LOCATION: APP/ (flat).
 *
 *  THE OWNER'S RULING (the module's reason): a menu ROW, not a prompt —
 *  "it will first ask the user, and he can update anytime he wants,
 *  not forced to update." The check itself is deliberate: no hand on
 *  the remote, no wire. Run() owns the whole ceremony (link bring-up,
 *  the offer, the two-stage confirm, the autonomous ladder).
 *
 *  CONTRACT NOTES (beyond the standard module shape):
 *    - GAMEOVER NEVER FIRES: updates are not games. Success ends in
 *      APP_FOTA_voidNetReset() (never returns — the BL commits; the
 *      NEW BUILD's banner is the success screen); failure returns
 *      EXITED through the standard seam.
 *    - GetScore() is always 0 — the leaderboard ruling: no store slot.
 *    - The deferred link's SECOND caller: NET_enumBringup() (idempotent
 *      — one link serves TTT and UPDATE alike).
 */
#ifndef APP_FOTA_APP_H_
#define APP_FOTA_APP_H_
#include "STD_TYPES.h"

#include "APP/APP_FOTA_NET.h"
#include "APP/APP_FOTA_UI.h"

#include "APP/HUB_interface.h"

GameResult_t APP_FOTA_APP_enumRun(void);
u16          APP_FOTA_APP_u16GetScore(void);

#endif /* APP_FOTA_APP_H_ */
