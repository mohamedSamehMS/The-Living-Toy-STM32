/*
 * HUB_program.c - the ORCHESTRATOR. Four jobs, nothing else:
 *    1. own the game dispatch table,
 *    2. define the menus as data (descriptors + row strings),
 *    3. run the hub state loop (home -> game/scores/settings -> home),
 *    4. run the GameOver cycle (score, record, play-again loop).
 *  LOCATION: APP/ (flat).
 *
 *  THE HUB "FSM" (deliberately tiny - the menu engine is the real
 *  machinery): there is no explicit state enum at all. The home
 *  menu's SELECTED row IS the transition: rows 0..3 -> a game, row
 *  4 -> scores screen, row 5 -> settings screen. Every screen
 *  returns to the home menu, which remembers its cursor. Games are
 *  entered by CALLING Run() - the hub suspends inside it - and
 *  return as data (GameResult_t), never as control flow games keep.
 *
 *  DATA LIFETIME (read before editing): all menu descriptors are
 *  statics (they are modified - Cursor in/out - so they must live
 *  forever). The ONE exception: HUB_stEnd.Info points at a buffer
 *  built on HUB_privateRunGame's stack - safe because the menu that
 *  consumes it runs INSIDE that function's frame, and the pointer is
 *  reassigned before every use. It is the only stack-backed field in
 *  the hub; keep it that way or make it static.
 *
 *  R5 (the FOTA wiring, File 3 of 5): THE UPDATE ROW. The owner's
 *  ruling: a menu row, not a prompt - "it will first ask the user,
 *  and he can update anytime he wants, not forced to update." The
 *  check itself is deliberate. THE LEADERBOARD RULING (recorded):
 *  UPDATE joins the DISPATCH table as entry 4, but APP_STORE_GAME_
 *  COUNT STAYS 4 - the dispatch grows independently of the score
 *  array, and the menu is longer than the score table BY DESIGN.
 *  UPDATE never returns GAMEOVER (success ends in NetReset - never
 *  returns; failure exits through the seam), so the store's
 *  game-indexed paths are never exercised for it - and the
 *  impossible path is still safe (APP_STORE's defensive bounds
 *  checks ignore index >= GAME_COUNT). Updates do not compete for
 *  high scores.
 *
 *  Revision history:
 *    Rev 1 - original listing (approved)
 *    Rev 2 - documentation pass + banner-digit fix (Rev 1 wrote the
 *            hundreds digit three times - caught in review)
 *    R5    - the UPDATE row (the dispatch + the home menu + the
 *            routing case; the leaderboard ruling recorded above)
 */

#include "STD_TYPES.h"
#include <HAL/TFT/HTFT_interface.h>
#include "HAL/AUDIO/HAUDIO_interface.h"
#include "APP/APP_FOTA_NET.h"
#include "APP/APP_FOTA_UI.h"

#include "APP/HUB_interface.h"
#include "APP/HUB_menu.h"
#include "APP/HUB_renderer.h"
#include "APP/HUB_config.h"
#include "APP/APP_INPUT.h"
#include "APP/APP_STORE.h"

#include "APP/APP_SNAKE.h"
#include "APP/APP_TTT.h"
#include "APP/APP_MATHQ.h"
#include "APP/APP_MM.h"
#include "APP/APP_FOTA_APP.h"      /* R5: the UPDATE module (the FOTA wiring) */

/* ---- 1. THE DISPATCH TABLE --------------------------------------
 * Order is ABI for the GAMES: game index = row index in the home menu
 * = index in APP_STORE's high-score array. Init is optional.
 *
 * R5 ADDITION - THE LEADERBOARD RULING (recorded): UPDATE joins the
 * DISPATCH table as entry 4, but APP_STORE_GAME_COUNT STAYS 4 - the
 * dispatch grows independently of the score array, and the menu is
 * longer than the score table BY DESIGN. UPDATE is not a game: it
 * never returns GAMEOVER (success ends in NetReset, which never
 * returns; failure exits through the seam), so the store's
 * game-indexed paths are never exercised for it - and even the
 * impossible path is safe (APP_STORE's defensive bounds check ignores
 * index >= GAME_COUNT). Updates do not compete for high scores.      */
#define HUB_DISPATCH_COUNT   (APP_STORE_GAME_COUNT + 1u)   /* 4 games + UPDATE */

static const GameEntry_t HUB_AstGames[HUB_DISPATCH_COUNT] =
{
    { "SNAKE",       APP_SNAKE_enumInit, APP_SNAKE_enumRun,     APP_SNAKE_u16GetScore },
    { "TIC TAC TOE", (void (*)(void))0,  APP_TTT_enumRun,       APP_TTT_u16GetScore   },
    { "MATH QUIZ",   (void (*)(void))0,  APP_MATHQ_enumRun,     APP_MATHQ_u16GetScore  },
    { "MEMORY",      (void (*)(void))0,  APP_MM_enumRun,        APP_MM_u16GetScore     },
    { "UPDATE",      (void (*)(void))0,  APP_FOTA_APP_enumRun,  APP_FOTA_APP_u16GetScore },
};

/* ---- 2. MENUS AS DATA --------------------------------------------
 * Row strings are statics (flash). Labels obey the 9-char law where
 * a value field follows (see HUB_renderer.h). The home menu doubles
 * as the game-select screen - one list instead of two screens, per
 * the simplified hub design.
 *
 * R5: UPDATE as row 6 - the LAST row: games first, then the bookkeeping
 * screens, then the parent's door at the bottom. 7 rows <= the 8-row
 * geometry law (46 + 6*14 + 8 = 138 < 160 - verified).                */
static const char * const HUB_apchHomeRows[] =
{ "SNAKE", "TIC TAC TOE", "MATH QUIZ", "MEMORY", "HIGH SCORES", "SETTINGS", "UPDATE" };

static const char * const HUB_apchScoreRows[] =
{ "SNAKE", "TICTACTOE", "MATHQUIZ", "MEMORY" };     /* 9-char law -
                                                       4 rows = the games
                                                       (UPDATE has no row:
                                                       the leaderboard ruling) */

static const char * const HUB_apchSetRows[] = { "SNAKE LVL" };   /* 9 chars - the
                                                                      label law */
static const char * const HUB_apchEndRows[]   = { "PLAY AGAIN", "MENU" };

/* Descriptors: static because the engine writes Cursor back into
 * them (menus remember where you were). */
static HUB_Menu_t HUB_stHome   = { "GAME HUB",   0,                       7u, HUB_apchHomeRows,  0u, 0, 0 };
static HUB_Menu_t HUB_stScores = { "SCORES", "BEST PER GAME",      4u, 	  HUB_apchScoreRows, 0u, 0, 0 };
static HUB_Menu_t HUB_stSet    = { "SETTINGS",   0,                       1u, HUB_apchSetRows,   0u, 0, 0 };
static HUB_Menu_t HUB_stEnd    = { "GAME END",  0, /* Info set per run */ 2u, HUB_apchEndRows,   0u, 0, 0 };

/* Placeholder event songs: const tables stay in flash until the games'
 * seat replaces these motifs with the final compositions. */
static const SongNote_t HUB_astLaunch0[] = { { 262u, 120u }, { 330u, 120u }, { 392u, 180u } };
static const SongNote_t HUB_astLaunch1[] = { { 330u, 120u }, { 392u, 120u }, { 523u, 180u } };
static const SongNote_t HUB_astLaunch2[] = { { 392u, 120u }, { 330u, 120u }, { 262u, 180u } };
static const SongNote_t HUB_astLaunch3[] = { { 262u, 100u }, { 392u, 100u }, { 523u, 220u } };
static const Song_t HUB_astGameEntrySongs[] =
{
    { HUB_astLaunch0, 3u, 0u },
    { HUB_astLaunch1, 3u, 0u },
    { HUB_astLaunch2, 3u, 0u },
    { HUB_astLaunch3, 3u, 0u }
};
static const SongNote_t HUB_astGameOver[] =
    { { 523u, 160u }, { 392u, 160u }, { 262u, 220u } };
static const Song_t HUB_stGameOver =
    { HUB_astGameOver, 3u, 0u };
static const SongNote_t HUB_astWelcome[] =
    { { 262u, 140u }, { 330u, 140u }, { 523u, 220u } };
static const Song_t HUB_stWelcome =
    { HUB_astWelcome, 3u, 0u };

/* ---- 3. ROW-VALUE CALLBACKS (policy lives HERE, engine stays dumb) */

/* Leaderboard row value: the game's HI, right-justified in the
 * 5-char field (so digits align across rows). do-while prints at
 * least one '0' for a zero score.                                  */
static void HUB_privateScoreValue(u8 Copy_u8Row, char *Copy_pchOut, u8 Copy_u8MaxLen)
{
    u16 Local_u16Hi = APP_STORE_u16GetHi(Copy_u8Row);
    u8 Local_u8I;
    for (Local_u8I = 0u; Local_u8I < (Copy_u8MaxLen - 1u); Local_u8I++)
    { Copy_pchOut[Local_u8I] = ' '; }
    Copy_pchOut[Copy_u8MaxLen - 1u] = '\0';
    Local_u8I = (u8)(Copy_u8MaxLen - 2u);
    do
    { Copy_pchOut[Local_u8I--] = (char)('0' + (Local_u16Hi % 10u));
      Local_u16Hi /= 10u;
    } while (Local_u16Hi != 0u);
}

/* Settings row value: the current Snake level name. Reads the store
 * on every paint - the engine repaints after a cycle, so the new
 * value lands immediately.                                          */
static void HUB_privateLevelValue(u8 Copy_u8Row, char *Copy_pchOut, u8 Copy_u8MaxLen)
{
    (void)Copy_u8Row;
    switch (APP_STORE_u8GetSetting(APP_STORE_SETTING_SNAKE_LEVEL))
    {
    case 0u: { const char *L = "EASY"; u8 i = 0u; while (L[i]) { Copy_pchOut[i] = L[i]; i++; } Copy_pchOut[i] = '\0'; } break;
    case 2u: { const char *L = "HARD"; u8 i = 0u; while (L[i]) { Copy_pchOut[i] = L[i]; i++; } Copy_pchOut[i] = '\0'; } break;
    default: { const char *L = "NORM"; u8 i = 0u; while (L[i]) { Copy_pchOut[i] = L[i]; i++; } Copy_pchOut[i] = '\0'; } break;
    }
}

/* Settings CONFIRM action: cycle EASY -> NORMAL -> HARD -> EASY.
 * Takes effect on the game's NEXT launch (Init reads the store -
 * that is why Init runs before every Run, not once at boot).        */
static void HUB_privateCycleLevel(u8 Copy_u8Row)
{
    (void)Copy_u8Row;
    APP_STORE_voidSetSetting(APP_STORE_SETTING_SNAKE_LEVEL,
        (u8)((APP_STORE_u8GetSetting(APP_STORE_SETTING_SNAKE_LEVEL) + 1u) % 3u));
}

/* ---- 4. THE GAME CYCLE --------------------------------------------
 * Launch -> Run -> result -> (GameOver screen + play-again loop).
 * The ONLY place in the hub that calls Run().                       */
static void HUB_privateRunGame(u8 Copy_u8Game)
{
    const GameEntry_t *Local_pstGame = &HUB_AstGames[Copy_u8Game];
    char Local_achInfo[17];      /* GameOver banner - stack, see file banner */

    while (1)   /* PLAY AGAIN loops here; MENU/BACK exits below     */
    {
        GameResult_t Local_eResult;
        u16 Local_u16Score, Local_u16Hi;
        u8 Local_u8I;

        /* Init per launch: settings changes apply on this very run */
        if (Local_pstGame->Init != (void (*)(void))0) { Local_pstGame->Init(); }

        /* Event hook: each game gets its own placeholder launch sting. */
        if (Copy_u8Game < 4u)
        {
            (void)HAUDIO_enumPlaySong(&HUB_astGameEntrySongs[Copy_u8Game]);
        }

        /* THE handoff: the hub suspends here for the whole game.
         * Nothing times out, nothing polls - Run returns when the
         * game is over or the player exited. The ~270 ms IR stalls
         * happen inside the game, by design (Run-owns-the-loop).   */
        Local_eResult = Local_pstGame->Run();

        /* The game's ending sound owns GAMEOVER. Only an explicit exit
         * gets the hub's departure sting; game-over results are quiet. */
        if (Local_eResult == GAME_RESULT_EXITED)
        {
            (void)HAUDIO_enumPlaySting(&HUB_stGameOver);
            return;
        }
        /* Natural end: read the score, update the record. The hub
         * is the single writer of high scores (the ownership rule
         * in APP_STORE) - the game itself never touched the store
         * except to DISPLAY the HI during play.                    */
        Local_u16Score = Local_pstGame->GetScore();
        Local_u16Hi = APP_STORE_u16GetHi(Copy_u8Game);
        if (Local_u16Score > Local_u16Hi)
        { Local_u16Hi = Local_u16Score; APP_STORE_voidSetHi(Copy_u8Game, Local_u16Hi); }

        /* GameOver banner: "SCORE:999 HI:999" - exactly 16 chars.
         * REV 2 FIX: Rev 1 printed the hundreds digit three times
         * (a 7-point game read "SCORE:777"). Now each digit is its
         * own division - hundreds, tens, ones, in that order.      */
        Local_u8I = 0u;
        { const char *L = "SCORE:"; u8 j = 0u; while (L[j]) { Local_achInfo[Local_u8I] = L[j]; Local_u8I++; j++; } }
        Local_achInfo[Local_u8I++] = (char)('0' + ((Local_u16Score / 100u) % 10u));
        Local_achInfo[Local_u8I++] = (char)('0' + ((Local_u16Score / 10u)  % 10u));
        Local_achInfo[Local_u8I++] = (char)('0' + ( Local_u16Score        % 10u));
        { const char *L = " HI:";  u8 j = 0u; while (L[j]) { Local_achInfo[Local_u8I] = L[j]; Local_u8I++; j++; } }
        Local_achInfo[Local_u8I++] = (char)('0' + ((Local_u16Hi / 100u) % 10u));
        Local_achInfo[Local_u8I++] = (char)('0' + ((Local_u16Hi / 10u)  % 10u));
        Local_achInfo[Local_u8I++] = (char)('0' + ( Local_u16Hi        % 10u));
        Local_achInfo[Local_u8I] = '\0';
        HUB_stEnd.Info = Local_achInfo;    /* stack buffer - see banner */
        HUB_stEnd.Cursor = 0u;             /* default to PLAY AGAIN     */

        /* The GameOver screen is just another menu: PLAY AGAIN /
         * MENU. BACK = leave (same as MENU). Selection row 0 reruns
         * the game - the loop above.                                */
        if (HUB_MENU_enumRun(&HUB_stEnd) == HUB_MENU_BACKED) { return; }
        if (HUB_stEnd.Cursor != 0u)                          { return; }
    }
}

void HUB_enumInit(void)
{
    /* Placeholder: defaults are seeded by APP_STORE_enumInit (called
     * from main). Future hub boot needs land here.                 */
}

void HUB_enumRun(void)
{
    static u8 Local_u8WelcomePlayed = 0u;

    if (Local_u8WelcomePlayed == 0u)
    {
        (void)HAUDIO_enumPlaySong(&HUB_stWelcome);
        Local_u8WelcomePlayed = 1u;
    }

    /* Wire the data-driven screens: value callbacks + confirm
     * actions are functions HERE, so the engine stays policy-free. */
    HUB_stScores.GetRowValue = HUB_privateScoreValue;
    HUB_stSet.GetRowValue    = HUB_privateLevelValue;
    HUB_stSet.OnRowConfirm   = HUB_privateCycleLevel;

    while (1)
    {
        /* HUB_MENU_enumRun does not return per pass, so service audio here. */
        HAUDIO_voidTask();

        /* HOME. BACK from home re-runs home (there is nowhere behind
         * it) - the engine's cursor memory makes that a no-op.     */
        if (HUB_MENU_enumRun(&HUB_stHome) == HUB_MENU_BACKED) { continue; }

        /* The selected row IS the hub's state transition.           */
        switch (HUB_stHome.Cursor)
        {
        case 0u: case 1u: case 2u: case 3u:
            HUB_privateRunGame(HUB_stHome.Cursor);
            break;
        case 6u:
            /* R5: UPDATE - the FOTA module, through the SAME standard
             * path as a game (Run-owns-the-loop; the module contract
             * honored exactly). Dispatch index = the table's last entry
             * (beyond the game indices, per the leaderboard ruling).
             * GAMEOVER never fires from this row; EXITED flows home
             * quietly; success never returns at all. */
            HUB_privateRunGame((HUB_DISPATCH_COUNT - 1u));
            break;
        case 4u:
            /* Leaderboard: read-only list (no OnRowConfirm -> CONFIRM
             * would return SELECTED, which we simply ignore); BACK
             * leaves. Values come from the store live - a new record
             * set minutes ago is already on the board.              */
            (void)HUB_MENU_enumRun(&HUB_stScores);
            break;
        case 5u:
            /* Settings: CONFIRM cycles the selected value (the store
             * write is immediate; games pick it up at next launch). */
            (void)HUB_MENU_enumRun(&HUB_stSet);
            break;
        default:
            break;
        }
    }
}
