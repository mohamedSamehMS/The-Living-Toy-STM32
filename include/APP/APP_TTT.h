/*
 * APP_TTT.h — Tic-Tac-Toe module contract. PENDING MODULE (stub).
 *  LOCATION: APP/ (flat). FILENAME IS APP_TTT.h — prefix included.
 *
 *  STATUS: deferred. TTT carries the multiplayer feature, and online
 *  play is out of the current scope — so the module is a stub and
 *  the whole TTT file set (TTT_renderer.c, TTT_config.h,
 *  TTT_session.c) is OUT of the build. The hub stays fully wired:
 *  this row exists in the dispatch table and the home menu, and
 *  shows "MODULE PENDING" on glass — an honest placeholder, zero
 *  churn the day the real module lands in this exact slot.
 *
 *  WHAT LANDS HERE EVENTUALLY (the recorded plan): the merged main's
 *  14 states — the hardware-proven offline v1.2 FSM (HUMAN/AI/
 *  WIN_FLASH/GAME_END) plus the online chain (NET_WAIT / LOBBY /
 *  HOST_WAIT / JOIN_ENTRY / ON_TURN / ON_WAIT / ON_GRACE / EXIT_ASK /
 *  ON_END) — consuming TTT_session over HESP, with the ESP link
 *  (USART1, PA9/PA10) brought up INSIDE the online-mode entry, D15
 *  STM_READY double emission included. Mode selection (offline-AI
 *  vs online) is the pending UX ruling.
 */
#ifndef APP_TTT_H_
#define APP_TTT_H_
#include "STD_TYPES.h"
#include "APP/HUB_interface.h"

GameResult_t APP_TTT_enumRun(void);
u16          APP_TTT_u16GetScore(void);

#endif /* APP_TTT_H_ */
