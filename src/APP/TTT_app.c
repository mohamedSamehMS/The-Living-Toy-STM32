/*
 * TTT_app.c — the shared flow-state variable. Three lines, by
 * design: the state is app-common, not any module's private.
 *  LOCATION: APP/ (flat).
 */
#include "APP/TTT_app.h"

Game_State_t Game_eState = GAME_ST_BOOT;
