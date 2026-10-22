/*
 * HUB_config.h — the hub's tunables. Same three-zone shape as every
 *  game config, so the whole APP layer greps the same way.
 *  LOCATION: APP/ (flat).
 *
 *  ZONE 1 — navigation pacing
 *  ZONE 2 — palette (all menu colors; one place to restyle the hub)
 *  ZONE 3 — features
 *  Geometry is NOT here — it lives in HUB_renderer.h as frozen
 *  contract facts, exactly like every game's renderer header.
 */
#ifndef HUB_CONFIG_H_
#define HUB_CONFIG_H_
#include "STD_TYPES.h"
#include <HAL/TFT/HTFT_interface.h>

/* ===================== 1. NAVIGATION ===================== */

/* Kiosk auto-cycle: 0 = off. N = after N ms with NO input, the
 * cursor advances one row (attract mode — the expo demo behavior).
 * Wraps around the list. Set to taste at flash time; the engine
 * checks it every loop pass, cost-free when 0.                     */
#define HUB_CFG_KIOSK_CYCLE_MS       0u

/* ===================== 2. PALETTE ===================== */
#define HUB_CFG_COLOR_BG             HTFT_COLOR_BLACK
#define HUB_CFG_COLOR_FRAME          HTFT_COLOR_LIGHTGRAY
#define HUB_CFG_COLOR_TITLE          HTFT_COLOR_CYAN
#define HUB_CFG_COLOR_TEXT           HTFT_COLOR_WHITE
#define HUB_CFG_COLOR_CURSOR         HTFT_COLOR_YELLOW
#define HUB_CFG_COLOR_VALUE          HTFT_COLOR_GREEN
#define HUB_CFG_COLOR_INFO           HTFT_COLOR_LIGHTGRAY

/* ===================== 3. FEATURES ===================== */

/* Score display width (zero-padded digits) in the GameOver banner
 * and leaderboard rows. 3 = up to 999.                             */
#define HUB_CFG_SCORE_DIGITS         3u

#endif /* HUB_CONFIG_H_ */
