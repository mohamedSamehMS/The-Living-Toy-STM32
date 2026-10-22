/*
 * MATHQ_config.h — ALL user-tunable settings for Math Quiz.
 *  LOCATION: APP/ (flat).
 *
 *  Three zones, same order as every game's config:
 *    1. GAMEPLAY  — round structure, difficulty ramp, demo accuracy
 *    2. VISUAL    — palette + feedback timing
 *    3. FEATURES  — demo pacing
 *
 *  Geometry (keypad layout, panel box) lives in MATHQ_renderer.h —
 *  contract facts, not tunables.
 */

#ifndef MATHQ_CONFIG_H_
#define MATHQ_CONFIG_H_

#include "STD_TYPES.h"
#include <HAL/TFT/HTFT_interface.h>   /* HTFT_RGB565 / HTFT_COLOR_*    */

/* =====================================================================
 *  1. GAMEPLAY / DIFFICULTY
 * ===================================================================== */
#define MATHQ_CFG_QUESTIONS_PER_ROUND   10u
#define MATHQ_CFG_LIVES                 3u

/* Level ramp, ROUND-RELATIVE (correct answers this round).
 * Retuned from the design sketch's 5/12: rounds are 10 questions, so
 * 12 was unreachable. 4 and 7 are both reachable at demo accuracy.  */
#define MATHQ_CFG_LEVEL2_AT             4u
#define MATHQ_CFG_LEVEL3_AT             7u

/* Streak bonus cap: points = 1 + min(streak-1, cap)                */
#define MATHQ_CFG_STREAK_CAP            3u

/* Demo autopilot accuracy, percent — exercises both feedback paths
 * and both terminal outcomes across rounds.                        */
#define MATHQ_CFG_DEMO_ACCURACY_PCT     70u

/* =====================================================================
 *  2. VISUAL / RENDERER
 * ===================================================================== */
#define MATHQ_CFG_COLOR_BG              HTFT_COLOR_BLACK
#define MATHQ_CFG_COLOR_PANEL_BORDER    HTFT_COLOR_WHITE
#define MATHQ_CFG_COLOR_QUESTION        HTFT_COLOR_WHITE     /* equation  */
#define MATHQ_CFG_COLOR_ANSWER_TYPED    HTFT_COLOR_WHITE     /* as typed  */
#define MATHQ_CFG_COLOR_CORRECT         HTFT_RGB565(0, 180, 0)  /* proven tone */
#define MATHQ_CFG_COLOR_WRONG           HTFT_COLOR_RED
#define MATHQ_CFG_COLOR_MESSAGE         HTFT_COLOR_YELLOW
#define MATHQ_CFG_COLOR_PROMPT          HTFT_COLOR_LIGHTGRAY
#define MATHQ_CFG_COLOR_HUD             HTFT_COLOR_WHITE

/* Keypad key visuals */
#define MATHQ_CFG_COLOR_KEY_FILL        HTFT_RGB565(30, 30, 30)
#define MATHQ_CFG_COLOR_KEY_BORDER      HTFT_COLOR_DARKGRAY
#define MATHQ_CFG_COLOR_KEY_PRESSED     HTFT_RGB565(80, 80, 80)
#define MATHQ_CFG_COLOR_KEY_BORDER_HI   HTFT_COLOR_WHITE
#define MATHQ_CFG_COLOR_CURSOR          HTFT_COLOR_YELLOW

/* Feedback timing (ms) */
#define MATHQ_CFG_FEEDBACK_GOOD_MS      600u
#define MATHQ_CFG_FEEDBACK_BAD_MS       1200u
#define MATHQ_CFG_SHAKE_STEP_MS         100u    /* 3 shake steps, then reveal */

/* THE §7 THREE-BEAT WRONG-ANSWER PACING (the pedagogy ruling — applied
 * from day one of this module). Each wrong answer is three distinct
 * beats the child can process, not one abrupt flash:
 *   beat 1 (WRONG_MS)   : red answer + shake, "WRONG" — registers the miss
 *   beat 2 (REVEAL_MS)  : correct answer in green, "ANSWER: n" — the
 *                         teaching beat; the heart goes black here
 *   beat 3 (GAP_MS)     : quiet pause before the next question          */
#define MATHQ_CFG_FEEDBACK_WRONG_MS     800u
#define MATHQ_CFG_FEEDBACK_REVEAL_MS    1500u
#define MATHQ_CFG_FEEDBACK_GAP_MS       200u
/* FEEDBACK_GOOD_MS 600u and SHAKE_STEP_MS 100u stay as-is.            */
/* =====================================================================
 *  3. FEATURES / DEMO PACING
 * ===================================================================== */
#define MATHQ_CFG_DEMO_THINK_MS         500u    /* pause after question shows */
#define MATHQ_CFG_DEMO_WALK_MS          150u    /* cursor step period         */
#define MATHQ_CFG_DEMO_PRESS_MS         200u    /* key flash duration         */
#define MATHQ_CFG_DEMO_RESTART_MS       4000u   /* terminal screen hold       */

#endif /* MATHQ_CONFIG_H_ */
