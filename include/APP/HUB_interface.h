/*
 * HUB_interface.h — the hub's PUBLIC contract: the result type, the
 *  dispatch row, and the hub lifecycle.
 *  LOCATION: APP/ (flat).
 *
 *  THE ARCHITECTURE IN ONE PARAGRAPH:
 *  The hub is a LAUNCHER, not a supervisor. Menus are DATA (one
 *  generic list engine — HUB_menu — renders and navigates any menu
 *  descriptor); games are RUN MODULES that own their loop until they
 *  RETURN a result. The hub suspends inside Run(), wakes with an
 *  answer, shows the GameOver screen, and either relaunches or goes
 *  home. Nothing polls games, nothing pushes commands into them.
 *
 *  WHY Run OWNS THE LOOP (the load-bearing ruling):
 *  Every proven game runs a free while(1) with input polling inline,
 *  and the ~270 ms IR-stall semantics are tuned to that: a fresh
 *  press freezes the GAME gracefully; SysTick keeps running. A
 *  hub-driven (cooperative) Run would move that stall into the hub
 *  itself, including the menus. Run-owns-the-loop keeps the stall
 *  where a year of hardware runs proved it is harmless.
 *
 *  WHAT HAPPENED TO Screen_t (the old design's screen struct):
 *  Dissolved into the menu engine. The old design's five hand-built
 *  menu screens were all the same thing — a list — so they collapsed
 *  into ONE engine plus five data descriptors. Non-list screens
 *  (GameOver) are menu descriptors too. Adding a screen = adding a
 *  struct of strings, not code.
 *
 *  Revision history:
 *    Rev 1 — original listing (approved)
 *    Rev 2 — documentation pass
 *
 *  Created on: Sep 30, 2026
 */
#ifndef HUB_INTERFACE_H_
#define HUB_INTERFACE_H_
#include "STD_TYPES.h"

/* The exit seam: what one Run() of a game reports back.
 *  GAMEOVER  — the game ran to its natural end (death / last
 *              question / board full). GetScore() is valid NOW.
 *  EXITED    — the player left through the exit seam (BACK ->
 *              "EXIT? 0 AGAIN" -> BACK). No score is consumed.
 * The hub treats the two differently: GAMEOVER goes to the GameOver
 * screen (and the leaderboard update); EXITED goes straight home.   */
typedef enum { GAME_RESULT_GAMEOVER = 0, GAME_RESULT_EXITED } GameResult_t;

/* One row of the game dispatch table. The hub's InGame "state" is
 * nothing but: call row->Init (if any), call row->Run, read the
 * result. The table is walked by the home menu to build the game
 * list — adding a fifth game = one row here + one menu row string.
 *
 *  Init      — optional per-launch setup (NULL = none). Runs BEFORE
 *              every Run(), so settings changed in the Settings
 *              screen take effect on the very next launch. Must be
 *              idempotent and cheap.
 *  Run       — owns the loop; returns only on end or player exit.
 *  GetScore  — valid only after a GAMEOVER return; the hub reads it
 *              exactly once, then owns the number (leaderboard).   */
typedef struct
{
    const char   *Name;
    void         (*Init)(void);
    GameResult_t (*Run)(void);
    u16          (*GetScore)(void);
} GameEntry_t;

/* Lifecycle. enumInit currently only seeds the store defaults via
 * APP_STORE (see that file); it exists so future boot needs of the
 * hub have an obvious home. enumRun never returns.               */
void HUB_enumInit(void);
void HUB_enumRun(void);

#endif /* HUB_INTERFACE_H_ */
