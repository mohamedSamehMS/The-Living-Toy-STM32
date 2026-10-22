/*
 * APP_SNAKE.h — the Snake game module's PUBLIC contract.
 *  LOCATION: APP/ (flat). FILENAME IS APP_SNAKE.h — the APP_ prefix
 *  is part of the name; includes must match it exactly.
 *
 *  WHAT THIS MODULE IS: one row of the hub's dispatch table — the
 *  app-facing shell of the Snake game. The hub calls these three
 *  functions and knows NOTHING else about Snake. The game's own
 *  anatomy is the classic three files: this contract, the logic
 *  module (APP_SNAKE.c), and the untouched, hardware-proven pair
 *  SNAKE_renderer.c + SNAKE_config.h.
 *
 *  CONTRACT:
 *    enumInit  — called by the hub BEFORE EVERY Run(): reads the
 *                difficulty setting from APP_STORE and seeds this
 *                module's runtime config. Must be idempotent and
 *                cheap (it is). Settings-screen changes take effect
 *                on the next launch through exactly this call.
 *    enumRun   — THE game. Owns the loop until the game ends or the
 *                player exits; may block for minutes. See the .c
 *                banner for what happens inside.
 *    u16GetScore — valid only after enumRun returned
 *                GAME_RESULT_GAMEOVER. The hub reads it once, then
 *                owns the number (leaderboard).
 *
 *  Created on: Sep 30, 2026
 *      Author: dell
 */
#ifndef APP_SNAKE_H_
#define APP_SNAKE_H_
#include "STD_TYPES.h"
#include "APP/HUB_interface.h"     /* GameResult_t — the exit seam  */

void         APP_SNAKE_enumInit(void);
GameResult_t APP_SNAKE_enumRun(void);
u16          APP_SNAKE_u16GetScore(void);

#endif /* APP_SNAKE_H_ */
