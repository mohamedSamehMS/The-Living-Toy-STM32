/*
 * APP_STORE.c — RAM backing store.
 *
 *  Defensive indexing: out-of-range indices are ignored/return 0 —
 *  a wrong game id can never corrupt another row. The arrays are
 *  statics: zero-initialized by the C startup before main runs.
 */
#include "STD_TYPES.h"
#include "APP/APP_STORE.h"

static u16 APP_STORE_Au16Hi[APP_STORE_GAME_COUNT];
static u8  APP_STORE_Au8Setting[APP_STORE_SETTING_COUNT];

void APP_STORE_enumInit(void)
{
    /* Factory defaults. Snake's compile-time default is NORMAL
     * (SNAKE_CFG_DIFFICULTY == 1) — the seed keeps the runtime
     * default identical to the proven build. Flash will later
     * overwrite these seeds with saved values, in this function.  */
    APP_STORE_Au8Setting[APP_STORE_SETTING_SNAKE_LEVEL] = 1u;   /* NORMAL */
}

u16  APP_STORE_u16GetHi(u8 Copy_u8Game)
{ return (Copy_u8Game < APP_STORE_GAME_COUNT) ? APP_STORE_Au16Hi[Copy_u8Game] : 0u; }

void APP_STORE_voidSetHi(u8 Copy_u8Game, u16 Copy_u16Hi)
{ if (Copy_u8Game < APP_STORE_GAME_COUNT) { APP_STORE_Au16Hi[Copy_u8Game] = Copy_u16Hi; } }

u8   APP_STORE_u8GetSetting(u8 Copy_u8Setting)
{ return (Copy_u8Setting < APP_STORE_SETTING_COUNT) ? APP_STORE_Au8Setting[Copy_u8Setting] : 0u; }

void APP_STORE_voidSetSetting(u8 Copy_u8Setting, u8 Copy_u8Value)
{ if (Copy_u8Setting < APP_STORE_SETTING_COUNT) { APP_STORE_Au8Setting[Copy_u8Setting] = Copy_u8Value; } }
