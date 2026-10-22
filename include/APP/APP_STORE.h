/*
 * APP_STORE.h — the persistence SEAM. Scores and settings behind a
 *  stable interface, so storage can change without touching a caller.
 *  LOCATION: APP/ (flat).
 *
 *  TODAY: RAM. Every value clears on power-cycle; enumInit() reseeds
 *  factory defaults at boot. High scores therefore live for the
 *  session — fine for bring-up and the demo.
 *  LATER: the Flash driver lands BEHIND this interface. The swap is
 *  this file + this .c only: same functions, backing store changes.
 *  No game, no menu, no hub line moves. That is the entire point of
 *  the seam: persistence is a decision you can defer without cost.
 *
 *  OWNERSHIP rule: the STORE owns high scores. Games display the HI
 *  (read-only) but never write it — the hub writes it once, after a
 *  GAMEOVER return, from GetScore(). One writer, one truth.
 */
#ifndef APP_STORE_H_
#define APP_STORE_H_
#include "STD_TYPES.h"

/* Game indices — MUST match the HUB dispatch table order.          */
#define APP_STORE_GAME_COUNT              4u

/* Setting indices — one per tunable the Settings screen exposes.    */
#define APP_STORE_SETTING_SNAKE_LEVEL     0u   /* 0 EASY 1 NORMAL 2 HARD */
#define APP_STORE_SETTING_COUNT           1u

/* Seed factory defaults (call once at boot, before any reader).     */
void APP_STORE_enumInit(void);

u16  APP_STORE_u16GetHi(u8 Copy_u8Game);
void APP_STORE_voidSetHi(u8 Copy_u8Game, u16 Copy_u16Hi);
u8   APP_STORE_u8GetSetting(u8 Copy_u8Setting);
void APP_STORE_voidSetSetting(u8 Copy_u8Setting, u8 Copy_u8Value);

#endif /* APP_STORE_H_ */
