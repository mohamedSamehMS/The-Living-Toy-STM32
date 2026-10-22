/*
 * MM_config.h — ALL user-tunable settings for Memory Match.
 *  LOCATION: APP/ (flat).
 *
 *  Three zones, same order as every game's config:
 *    1. GAMEPLAY  — reveal pacing, flip-back hold
 *    2. VISUAL    — palette (card states, cursor, messages)
 *    3. FEATURES  — demo memory skill, restart delay
 *
 *  Geometry (card size, grid origin) lives in MM_renderer.h —
 *  contract facts, not tunables.
 *
 *  PEDAGOGY NOTE (the Math Quiz §7 lesson applied from day one):
 *  the REVEAL delay is a memorization beat, and the FLIP-BACK HOLD
 *  is a distinct "cards closed again" beat — pacing is config, so
 *  both can be tuned by watching children play, without code edits.
 */

#ifndef MM_CONFIG_H_
#define MM_CONFIG_H_

#include "STD_TYPES.h"
#include <HAL/TFT/HTFT_interface.h>   /* HTFT_RGB565 / HTFT_COLOR_*    */

/* =====================================================================
 *  1. GAMEPLAY
 * ===================================================================== */

/* Reveal delay: how long a mismatched pair stays face-up for the
 * child to memorize before flipping back.                              */
#define MM_CFG_REVEAL_MS             800u

/* Flip-back hold: brief pause AFTER the cards return face-down before
 * input resumes — the "closed again" beat.                              */
#define MM_CFG_FLIPBACK_HOLD_MS      300u

/* Tries count every SECOND flip (standard). 8 pairs per game.        */
#define MM_CFG_PAIRS                 8u

/* =====================================================================
 *  2. VISUAL / RENDERER
 * ===================================================================== */
#define MM_CFG_COLOR_BG              HTFT_COLOR_BLACK
#define MM_CFG_COLOR_CARD_DOWN       HTFT_RGB565(0, 0, 100)     /* closed card  */
#define MM_CFG_COLOR_CARD_DOWN_BRD   HTFT_RGB565(0, 0, 60)      /* darker edge  */
#define MM_CFG_COLOR_CARD_UP         HTFT_COLOR_WHITE           /* face-up fill */
#define MM_CFG_COLOR_CARD_UP_BRD     HTFT_COLOR_DARKGRAY
#define MM_CFG_COLOR_MATCHED         HTFT_RGB565(0, 180, 0)     /* proven green */
#define MM_CFG_COLOR_MATCHED_BRD     HTFT_RGB565(0, 255, 0)
#define MM_CFG_COLOR_SYMBOL          HTFT_COLOR_BLACK           /* on white      */
#define MM_CFG_COLOR_SYMBOL_MATCHED  HTFT_RGB565(0, 180, 0)
#define MM_CFG_COLOR_CURSOR          HTFT_COLOR_YELLOW
#define MM_CFG_COLOR_HUD             HTFT_COLOR_WHITE
#define MM_CFG_COLOR_MSG_GOOD        HTFT_RGB565(0, 180, 0)
#define MM_CFG_COLOR_MSG_BAD         HTFT_COLOR_RED

/* =====================================================================
 *  3. FEATURES / DEMO
 * ===================================================================== */

/* Autopilot memory: percent chance of matching a KNOWN pair when one
 * exists. Below that, random pick — so the demo wins visibly (~12-18
 * tries) but still exercises the mismatch/flip-back path.            */
#define MM_CFG_DEMO_MEMORY_PCT       60u

/* Terminal screen hold before restart.                               */
#define MM_CFG_DEMO_RESTART_MS       3000u


/* Match celebration beat: the pair turns green + "MATCH!" and holds
 * before play resumes — the success twin of the REVEAL beat.       */
#define MM_CFG_MATCH_MS              600u

/* Terminal win-screen hold before returning to the hub.            */
#define MM_CFG_WIN_HOLD_MS           3000u
#endif /* MM_CONFIG_H_ */
