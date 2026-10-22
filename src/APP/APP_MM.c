/*
 * APP_MM.c — the Memory Match game module: FSM + shuffle + input.
 *  LOCATION: APP/ (flat). Replaces the pending-module stub — same
 *  contract, so the hub's dispatch row needs ZERO changes.
 *
 *  THE GAME: 16 cards (8 symbol pairs, A C E H K M P T — the
 *  rotationally-confusable B/D/P/Q family pruned to P only, per the
 *  frozen symbol ruling), Fisher-Yates shuffled per launch (seeded
 *  from menu-navigation time — the house entropy source). Flip two;
 *  a match locks green; a mismatch shows both, then closes them.
 *  Find all 8 pairs.
 *
 *  FSM (7 states — the beats are the §7 pedagogy):
 *    PICK1  -> CH on a face-down card  -> flip up -> PICK2
 *    PICK2  -> CH on another face-down  -> match?  -> MATCH_BEAT
 *                                       -> mismatch -> REVEAL
 *    MATCH_BEAT (600 ms, input-free)    -> PICK1 | WIN (8th pair)
 *    REVEAL     (800 ms, input-free)    -> cards close -> FLIPBACK
 *    FLIPBACK   (300 ms, input-free)   -> PICK1
 *    WIN        -> CH/0/3 s             -> GAMEOVER (hub owns screen)
 *    EXIT_ASK   (the house seam)        -> EXITED | resume-in-place
 *
 *  SELECTION GATING (one check does triple duty): CH only acts on a
 *  card whose state is DOWN — which simultaneously rejects matched
 *  cards AND re-selecting the first (it is UP during PICK2). No
 *  index bookkeeping to get wrong.
 *
 *  TRIES: incremented on every SECOND flip (the standard attempt
 *  count, per the config comment) — matches and misses both count.
 *
 *  DESIGN PRINCIPLES (the house set): Run-owns-the-loop; the screen
 *  is a cache of state (RepaintAll regenerates everything — the exit
 *  overlay is removed by repaint, never "restored"); events during
 *  the input-free beats are consumed-and-discarded (the TTT ST_AI /
 *  WIN_FLASH precedent); deadline checks only — no busy-waits.
 */
#include "STD_TYPES.h"
#include <MCAL/SYSTICK/MSYSTICK_interface.h>
#include <HAL/TFT/HTFT_interface.h>       /* the WIN screen (terminal
                                             screens own no renderer) */
#include "APP/APP_MM.h"
#include "APP/APP_INPUT.h"
#include "APP/MM_renderer.h"
#include "APP/MM_config.h"
#include "HAL/AUDIO/HAUDIO_interface.h"

/* ---- states ---- */
typedef enum
{
    MM_ST_PICK1 = 0,     /* no card up: navigate + first flip        */
    MM_ST_PICK2,         /* one card up: navigate + second flip      */
    MM_ST_MATCH_BEAT,    /* matched pair green — the success beat    */
    MM_ST_REVEAL,        /* mismatch shown — the memorization beat   */
    MM_ST_FLIPBACK,      /* cards closed again — the settle beat      */
    MM_ST_WIN,           /* all 8 pairs found: terminal hold         */
    MM_ST_EXIT_ASK       /* the exit seam                             */
} Mm_State_t;

#define MM_EXIT_ASK_MS          5000u   /* the house seam window       */
#define MM_SYMBOLS              8u
#define MM_GRID                 4u     /* 4x4 — wrap in both axes      */

/* The symbol set — see the banner for the confusability ruling.     */
static const char Mm_ACHSet[MM_SYMBOLS] =
{ 'A', 'C', 'E', 'H', 'K', 'M', 'P', 'T' };

/* ---- module state (all file-private) ---- */
static Mm_State_t      Mm_eState;
static MM_CardState_t  Mm_AeCard[MM_CARDS];   /* per-card visual state */
static char            Mm_AchSym[MM_CARDS];   /* shuffled symbols      */
static u8              Mm_u8Cursor;
static u8              Mm_u8First;            /* PICK2's up card       */
static u8              Mm_u8Pairs;
static u16             Mm_u16Tries;
static u16             Mm_u16Score;
static u32             Mm_u32T0;              /* the beat timer        */
static u8              Mm_u8ResumePick2;      /* exit-resume target     */

static const SongNote_t Mm_astTheme[] =
    { { 220u, 250u }, { 262u, 250u }, { 330u, 250u }, { 262u, 250u },
      { 349u, 250u }, { 330u, 250u }, { 294u, 250u }, { 262u, 250u },
      { 0u, 500u } };
static const Song_t Mm_stTheme =
    { Mm_astTheme, 9u, 1u, 4u };
static const SongNote_t Mm_astCompletionSting[] =
    { { 262u, 150u }, { 330u, 150u }, { 392u, 150u }, { 523u, 400u } };
static const Song_t Mm_stCompletionSting =
    { Mm_astCompletionSting, 4u, 0u, 0u };

/* the shared LCG — seeded at Run entry (house pattern)              */
static u32 Mm_u32RandState = 12345u;
static u32 Mm_u32Rand(void)
{ Mm_u32RandState = (Mm_u32RandState * 1103515245u) + 12345u;
  return (Mm_u32RandState >> 16u); }

/* ---- the deal: 8 symbols twice, then Fisher-Yates ---- */
static void Mm_voidShuffle(void)
{
    u8 Local_u8I;

    for (Local_u8I = 0u; Local_u8I < MM_SYMBOLS; Local_u8I++)
    {
        Mm_AchSym[(u8)(2u * Local_u8I)]       = Mm_ACHSet[Local_u8I];
        Mm_AchSym[(u8)(2u * Local_u8I + 1u)]  = Mm_ACHSet[Local_u8I];
    }
    /* Fisher-Yates (the pattern bank's shuffle): every permutation
     * equally likely; i from the top down, swap with any j <= i.    */
    for (Local_u8I = (u8)(MM_CARDS - 1u); Local_u8I > 0u; Local_u8I--)
    {
        u8  Local_u8J = (u8)(Mm_u32Rand() % (u32)(Local_u8I + 1u));
        char Local_chT = Mm_AchSym[Local_u8I];
        Mm_AchSym[Local_u8I] = Mm_AchSym[Local_u8J];
        Mm_AchSym[Local_u8J] = Local_chT;
    }
}

/* ---- cursor: WRAP in the 4x4 (the grid convention — menus clamp,
 *      grids wrap; MathQ's keypad set the precedent). Ring erase is
 *      the renderer's four-strip erase — the card is never touched. */
static void Mm_voidMoveCursor(s8 Copy_s8Dr, s8 Copy_s8Dc)
{
    u8 Local_u8R = (u8)(Mm_u8Cursor / MM_GRID);
    u8 Local_u8C = (u8)(Mm_u8Cursor % MM_GRID);

    MM_voidPaintCursor(Mm_u8Cursor, 0u);
    Local_u8R = (u8)((Local_u8R + Copy_s8Dr + MM_GRID) % MM_GRID);
    Local_u8C = (u8)((Local_u8C + Copy_s8Dc + MM_GRID) % MM_GRID);
    Mm_u8Cursor = (u8)((Local_u8R * MM_GRID) + Local_u8C);
    MM_voidPaintCursor(Mm_u8Cursor, 1u);
}

/* ---- regenerate EVERYTHING from statics (the screen-is-cache law).
 *      The exit overlay's removal tool. Message follows the state
 *      we are resuming INTO — set the state before calling this.    */
static void Mm_voidRepaintAll(void)
{
    u8 Local_u8I;
    HTFT_voidFillScreen(MM_CFG_COLOR_BG);
    for (Local_u8I = 0u; Local_u8I < MM_CARDS; Local_u8I++)
    { MM_voidPaintCard(Local_u8I, Mm_AeCard[Local_u8I], Mm_AchSym[Local_u8I]); }
    MM_voidPaintPairs(Mm_u8Pairs);
    MM_voidPaintTries(Mm_u16Tries);
    MM_voidPaintMessage(
        (Mm_eState == MM_ST_PICK2) ? "FIND THE MATCH" : "PICK A CARD",
        MM_CFG_COLOR_HUD);
    MM_voidPaintCursor(Mm_u8Cursor, 1u);
}

/* ---- the flips ---- */

/* PICK1's action: a face-down card goes up; the hunt for its twin
 * begins. The DOWN check (done by the caller) already rejected
 * matched cards; PICK1 has no other up card to collide with.       */
static void Mm_voidFlipFirst(u8 Copy_u8Idx)
{
    Mm_AeCard[Copy_u8Idx] = MM_CARD_UP;
    MM_voidPaintCard(Copy_u8Idx, MM_CARD_UP, Mm_AchSym[Copy_u8Idx]);
    Mm_u8First = Copy_u8Idx;
    MM_voidPaintMessage("FIND THE MATCH", MM_CFG_COLOR_HUD);
    Mm_eState = MM_ST_PICK2;
}

/* PICK2's action: the second card. One try is counted on EVERY
 * second flip (the standard) — then the verdict splits the flow.    */
static void Mm_voidFlipSecond(u8 Copy_u8Idx)
{
    Mm_u16Tries++;
    MM_voidPaintTries(Mm_u16Tries);          /* same-rect, no erase  */
    Mm_u32T0 = MSYSTICK_GetElapsed_ms();

    if (Mm_AchSym[Copy_u8Idx] == Mm_AchSym[Mm_u8First])
    {
        /* MATCH: both lock green at once — the white-up state of the
         * second card never shows; green IS the reveal.              */
        Mm_AeCard[Copy_u8Idx]      = MM_CARD_MATCHED;
        Mm_AeCard[Mm_u8First]      = MM_CARD_MATCHED;
        MM_voidPaintCard(Copy_u8Idx,      MM_CARD_MATCHED, Mm_AchSym[Copy_u8Idx]);
        MM_voidPaintCard(Mm_u8First,      MM_CARD_MATCHED, Mm_AchSym[Mm_u8First]);
        Mm_u8Pairs++;
        MM_voidPaintPairs(Mm_u8Pairs);
        MM_voidPaintMessage("MATCH!", MM_CFG_COLOR_MSG_GOOD);
        Mm_eState = MM_ST_MATCH_BEAT;
    }
    else
    {
        /* MISMATCH: both stay visible through the REVEAL beat — the
         * memorization window. Closing happens at the beat's end.   */
        Mm_AeCard[Copy_u8Idx] = MM_CARD_UP;
        MM_voidPaintCard(Copy_u8Idx, MM_CARD_UP, Mm_AchSym[Copy_u8Idx]);
        MM_voidPaintMessage("NOT A MATCH", MM_CFG_COLOR_MSG_BAD);
        Mm_eState = MM_ST_REVEAL;
    }
}

/* ---- the terminal screen (app-layer direct draw, the legitimized
 *      exception: terminal screens own no renderer) ---- */
static void Mm_voidPaintWin(void)
{
    char Local_ach[17];

    /* score: 80 - (tries - 8), floor 0 — perfect memory = 80        */
    s32 Local_s32Score = (s32)80 - ((s32)Mm_u16Tries - 8);
    if (Local_s32Score < 0) { Local_s32Score = 0; }
    Mm_u16Score = (u16)Local_s32Score;

    HTFT_voidFillScreen(HTFT_COLOR_BLACK);
    HTFT_voidDrawString2x(0u, 24u, "YOU WIN!",     /* 8 chars = 128 px, x=0 */
                          MM_CFG_COLOR_MSG_GOOD, HTFT_COLOR_BLACK);

    /* "PAIRS: 8/8" — the game is by definition complete here        */
    Local_ach[0]='P'; Local_ach[1]='A'; Local_ach[2]='I'; Local_ach[3]='R';
    Local_ach[4]='S'; Local_ach[5]=':'; Local_ach[6]=' ';
    Local_ach[7]='8'; Local_ach[8]='/'; Local_ach[9]='8'; Local_ach[10]='\0';
    HTFT_voidDrawString(20u, 64u, Local_ach, MM_CFG_COLOR_HUD, HTFT_COLOR_BLACK);

    /* "TRIES: nnn" — the true count, three digits (the in-game HUD
     * is contractually 2 digits; the terminal screen shows reality) */
    Local_ach[0]='T'; Local_ach[1]='R'; Local_ach[2]='I'; Local_ach[3]='E';
    Local_ach[4]='S'; Local_ach[5]=':'; Local_ach[6]=' ';
    Local_ach[7] = (char)('0' + ((Mm_u16Tries / 100u) % 10u));
    Local_ach[8] = (char)('0' + ((Mm_u16Tries / 10u)  % 10u));
    Local_ach[9] = (char)('0' + ( Mm_u16Tries        % 10u));
    Local_ach[10] = '\0';
    HTFT_voidDrawString(20u, 76u, Local_ach, MM_CFG_COLOR_HUD, HTFT_COLOR_BLACK);

    HTFT_voidDrawString(16u, 120u, "CH OR 0: DONE", MM_CFG_COLOR_CURSOR, HTFT_COLOR_BLACK);
}

/* ---- public contract ---- */

GameResult_t APP_MM_enumRun(void)
{
    u8 Local_u8I;

    /* seed from navigation time — the house entropy source          */
    Mm_u32RandState ^= MSYSTICK_GetElapsed_ms();
    Mm_u32RandState = (Mm_u32RandState * 1103515245u) + 12345u;

    Mm_voidShuffle();
    for (Local_u8I = 0u; Local_u8I < MM_CARDS; Local_u8I++)
    { Mm_AeCard[Local_u8I] = MM_CARD_DOWN; }
    Mm_u8Cursor = 0u;  Mm_u8First = 0u;
    Mm_u8Pairs = 0u;  Mm_u16Tries = 0u;  Mm_u16Score = 0u;

    MM_voidPaintScreen();                 /* entry: all down + HUD     */
    MM_voidPaintCursor(Mm_u8Cursor, 1u);
    Mm_eState = MM_ST_PICK1;

    while (1)
    {
        u32 Local_u32Now = MSYSTICK_GetElapsed_ms();
        APP_INPUT_Event_t Local_stEvt;
        u8 Local_u8HaveEvt =
            (APP_INPUT_enumGetEvent(&Local_stEvt) == APP_INPUT_OK);

        HAUDIO_voidTask();
        if (((Mm_eState == MM_ST_PICK1) || (Mm_eState == MM_ST_PICK2)) &&
            (HAUDIO_enumGetState() == HAUDIO_STATE_IDLE))
        {
            (void)HAUDIO_enumPlaySong(&Mm_stTheme);
        }

        switch (Mm_eState)
        {
        case MM_ST_PICK1:
        case MM_ST_PICK2:   /* shared shape — the flip differs       */
            if (Local_u8HaveEvt != 0u)
            {
                switch (Local_stEvt.Button)
                {
                case BTN_NUM_2: case BTN_UP:    Mm_voidMoveCursor(-1,  0); break;
                case BTN_NUM_8: case BTN_DOWN:  Mm_voidMoveCursor( 1,  0); break;
                case BTN_NUM_4: case BTN_LEFT:  Mm_voidMoveCursor( 0, -1); break;
                case BTN_NUM_6: case BTN_RIGHT: Mm_voidMoveCursor( 0,  1); break;

                case BTN_CONFIRM:
                    if (Local_stEvt.IsRepeat != 0u) { break; }
                    /* THE gate: DOWN-only — rejects matched cards and
                     * (in PICK2) the first card, which is UP.         */
                    if (Mm_AeCard[Mm_u8Cursor] == MM_CARD_DOWN)
                    {
                        if (Mm_eState == MM_ST_PICK1)
                        { Mm_voidFlipFirst(Mm_u8Cursor); }
                        else
                        { Mm_voidFlipSecond(Mm_u8Cursor); }
                    }
                    break;

                case BTN_BACK:
                    if (Local_stEvt.IsRepeat != 0u) { break; }
                    Mm_u8ResumePick2 = (u8)(Mm_eState == MM_ST_PICK2);
                    Mm_eState = MM_ST_EXIT_ASK;
                    Mm_u32T0 = Local_u32Now;
                    MM_voidPaintMessage("EXIT? 0 AGAIN", MM_CFG_COLOR_MSG_BAD);
                    break;

                default:
                    break;
                }
            }
            break;

        case MM_ST_MATCH_BEAT:   /* input-free: the success beat      */
            if ((Local_u32Now - Mm_u32T0) >= MM_CFG_MATCH_MS)
            {
                if (Mm_u8Pairs >= MM_CFG_PAIRS)
                {
                    (void)HAUDIO_enumPlaySting(&Mm_stCompletionSting);
                    Mm_voidPaintWin();
                    Mm_eState = MM_ST_WIN;
                    Mm_u32T0 = Local_u32Now;
                }
                else
                {
                    MM_voidPaintMessage("PICK A CARD", MM_CFG_COLOR_HUD);
                    Mm_eState = MM_ST_PICK1;
                }
            }
            break;

        case MM_ST_REVEAL:       /* input-free: the memorization beat */
            if ((Local_u32Now - Mm_u32T0) >= MM_CFG_REVEAL_MS)
            {
                /* close both — the cards' state returns to DOWN.
                 * F3 REVIEW FIX: the second line was a paste-duplicated
                 * assignment of Mm_u8First (harmless but noise).       */
                Mm_AeCard[Mm_u8Cursor] = MM_CARD_DOWN;
                Mm_AeCard[Mm_u8First]  = MM_CARD_DOWN;
                MM_voidPaintCard(Mm_u8Cursor, MM_CARD_DOWN, ' ');
                MM_voidPaintCard(Mm_u8First, MM_CARD_DOWN, ' ');
                Mm_eState = MM_ST_FLIPBACK;
                Mm_u32T0 = Local_u32Now;
            }
            break;

        case MM_ST_FLIPBACK:     /* input-free: the settle beat       */
            if ((Local_u32Now - Mm_u32T0) >= MM_CFG_FLIPBACK_HOLD_MS)
            {
                MM_voidPaintMessage("PICK A CARD", MM_CFG_COLOR_HUD);
                Mm_eState = MM_ST_PICK1;
            }
            break;

        case MM_ST_WIN:
            if ((Local_u8HaveEvt != 0u) &&
                ((Local_stEvt.Button == BTN_CONFIRM) ||
                 (Local_stEvt.Button == BTN_BACK)) &&
                (Local_stEvt.IsRepeat == 0u))
            { return GAME_RESULT_GAMEOVER; }   /* hub owns the screen */
            if ((Local_u32Now - Mm_u32T0) >= MM_CFG_WIN_HOLD_MS)
            { return GAME_RESULT_GAMEOVER; }   /* child-proof: auto   */
            break;

        case MM_ST_EXIT_ASK:
            if (Local_u8HaveEvt != 0u)
            {
                if (Local_stEvt.Button == BTN_BACK)
                { return GAME_RESULT_EXITED; }           /* the seam  */
                if (Local_stEvt.Button == BTN_CONFIRM)
                {
                    Mm_eState = (Mm_u8ResumePick2 != 0u) ? MM_ST_PICK2
                                                         : MM_ST_PICK1;
                    Mm_voidRepaintAll();   /* state FIRST, then repaint */
                    break;
                }
            }
            if ((Local_u32Now - Mm_u32T0) >= MM_EXIT_ASK_MS)
            {
                Mm_eState = (Mm_u8ResumePick2 != 0u) ? MM_ST_PICK2
                                                     : MM_ST_PICK1;
                Mm_voidRepaintAll();
            }
            break;

        default:
            break;
        }
    }
}

u16 APP_MM_u16GetScore(void) { return Mm_u16Score; }
