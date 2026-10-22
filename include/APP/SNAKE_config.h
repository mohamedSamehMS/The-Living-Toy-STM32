/*
 * SNAKE_config.h � ALL user-tunable settings for the Snake game.
 *
 *  LOCATION: APP/ (flat).
 *
 *  THREE ZONES (same order in every game's config, for grep-ability):
 *    1. GAMEPLAY  � rules, difficulty, pacing
 *    2. VISUAL    � palette + visual timing
 *    3. FEATURES  � demo mode / difficulty flags
 *
 *  NOT here: field geometry (renderer contract facts), any code.
 *  Every value below is safe to change without code edits; anything
 *  needing redesign lives in SNAKE_renderer.h by design.
 */

#ifndef SNAKE_CONFIG_H_
#define SNAKE_CONFIG_H_

#include "STD_TYPES.h"
#include <HAL/TFT/HTFT_interface.h>   /* zone 2 uses HTFT_RGB565 / HTFT_COLOR_* */

/* =====================================================================
 *  1. GAMEPLAY / DIFFICULTY
 * ===================================================================== */

/* Difficulty model: the ramp's two ends. Difficulty presets may set
 * these; individual values remain overridable.                     */
#define SNAKE_CFG_TICK_MS_BASE       250u   /* initial tick � slowest */
#define SNAKE_CFG_TICK_MS_MIN        140u   /* speed floor � fastest  */
#define SNAKE_CFG_SPEEDUP_PER_FOOD   4u     /* ms faster per segment  */
#define SNAKE_CFG_START_LEN          3u     /* initial body segments  */

/* Wall behavior � selected by the hub's Snake difficulty setting:
 *   SNAKE_CFG_WALLS_HARD   0 = wrap-around edges (easy: exit right,
 *                               re-enter left; death by self only)
 *   SNAKE_CFG_WALLS_HARD   1 = solid walls (hard: edges kill)
 * Implemented in the game's tick, one modulo branch.
 * Renderer is unaffected � a cell is a cell.                        */
#define SNAKE_CFG_WALLS_HARD         1u     /* v1: classic solid walls */

/* Difficulty preset selection is read from the hub Settings menu
 * at launch.
 * Values: 0 = EASY, 1 = NORMAL, 2 = HARD � presets below re-map the
 * gameplay zone's numbers.                                          */
#define SNAKE_CFG_DIFFICULTY         1u     /* NORMAL                   */

#if   SNAKE_CFG_DIFFICULTY == 0      /* EASY: slow, forgiving, wraps   */
  #undef  SNAKE_CFG_TICK_MS_BASE
  #undef  SNAKE_CFG_TICK_MS_MIN
  #define SNAKE_CFG_TICK_MS_BASE     320u
  #define SNAKE_CFG_TICK_MS_MIN      200u
  #define SNAKE_CFG_WALLS_HARD       0u
#elif SNAKE_CFG_DIFFICULTY == 2      /* HARD: fast, tight, solid walls */
  #undef  SNAKE_CFG_TICK_MS_MIN
  #define SNAKE_CFG_TICK_MS_MIN      90u
#endif
/* =====================================================================
 *  2. VISUAL / RENDERER � the values migrate verbatim from the top of
 *     SNAKE_renderer.c; renderer.c includes this file and deletes its
 *     local palette block.
 * ===================================================================== */
#define SNAKE_CFG_COLOR_HEAD         HTFT_RGB565(80, 255, 80)
#define SNAKE_CFG_COLOR_BODY_A       HTFT_RGB565(0, 180, 0)
#define SNAKE_CFG_COLOR_BODY_B       HTFT_RGB565(0, 110, 0)
#define SNAKE_CFG_COLOR_FOOD         HTFT_RGB565(255, 0, 0)
#define SNAKE_CFG_COLOR_FOOD_DIM     HTFT_RGB565(140, 0, 0)
#define SNAKE_CFG_COLOR_BORDER       HTFT_COLOR_WHITE
#define SNAKE_CFG_COLOR_BG           HTFT_COLOR_BLACK
#define SNAKE_CFG_COLOR_HUD_SCORE    HTFT_COLOR_WHITE
#define SNAKE_CFG_COLOR_HUD_HI       HTFT_COLOR_GREEN

/* Food pulse period, ms. 0 disables the pulse entirely.             */
#define SNAKE_CFG_FOOD_PULSE_MS      250u

/* =====================================================================
 *  3. FEATURES / DEMO
 * ===================================================================== */

/* Demo-mode demo settings (apply while the game runs its autopilot) */
#define SNAKE_CFG_DEMO_CRASH_LEN     24u    /* scripted demo death     */
#define SNAKE_CFG_DEMO_FOOD_AHEAD    8u

#endif /* SNAKE_CONFIG_H_ */
