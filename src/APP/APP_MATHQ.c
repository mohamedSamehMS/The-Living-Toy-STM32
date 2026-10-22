/*
 * APP_MATHQ.c — the Math Quiz game module: FSM + generator + input.
 *  LOCATION: APP/ (flat). FINAL POLISH REVISION — replaces the stub
 *  file-for-file; the hub's dispatch row needs ZERO changes.
 *
 *  THE FORK RESOLUTION (this module's design center): numeric answer
 *  entry via a CURSOR-NAVIGATED ON-SCREEN KEYPAD. The remote never
 *  types digits directly — it moves a cursor (2/8/4/6, wrap in the
 *  3x4 grid) and presses keys with CH. The "0 digit" problem (the
 *  remote's 0 is globally BACK) dissolves: zero is a POSITION on the
 *  screen, reached by navigation. The renderer's keypad contract
 *  (PaintKey/PaintKeyCursor) was designed for exactly this.
 *
 *  INPUT MAP (the system-wide keypad convention):
 *    2/8/4/6 = cursor up/down/left/right (single-step, wrap in the
 *              grid — digits are repeat-masked at the driver)
 *    D-pad   = same directions (glide on hold — the dual map)
 *    CH      = press the key under the cursor
 *    0       = exit ask (the exit seam — the house pattern)
 *
 *  KEYPAD INDEX MAP (row-major, matches MATHQ_renderer.h):
 *      0:'1' 1:'2' 2:'3'
 *      3:'4' 4:'5' 5:'6'
 *      6:'7' 7:'8' 8:'9'
 *      9:'<' 10:'0' 11:'='      '<' = backspace, '=' = submit
 *
 *  FSM:
 *      QUESTION -> FEED_GOOD (correct, 600 ms) -> next question
 *      QUESTION -> FEED_BAD (three beats, §7)  -> next | RESULTS | over
 *      QUESTION -> EXIT_ASK (BACK) -> EXITED | resume (5 s timeout)
 *      RESULTS (10 questions) -> CH: next round | BACK: GAMEOVER
 *      Hearts 0 (after beat 2)   -> return GAMEOVER (hub owns screen)
 *
 *  GENERATOR CONTRACTS (all from the validated demo):
 *    L1 (+/-): a 2..10, b 2..9, a>=b on '-'  — no negatives, ever
 *    L2 (4 correct in round) adds X: tables 2..9, +/- operands to 20
 *    L3 (7 correct in round) adds /: built BACKWARDS (divisor 2..9,
 *        quotient 2..9, dividend = product) — the answer is always a
 *        clean integer; random division is never allowed
 *    At most ONE two-digit operand per question (renderer contract)
 *    Level ramp is ROUND-relative: resets with each 10-question round
 *
 *  DESIGN PRINCIPLES (the house set):
 *    Run-owns-the-loop (stall-tolerant by design; the current tree's
 *    input seam is faster still — invisible either way); the screen
 *    is a cache of state (RepaintAll regenerates everything — the
 *    exit overlay is removed by repaint, never "restored"); validate-
 *    before-execute (submit checks; nothing executes mid-beat);
 *    event-driven beats (no blocking delays — the ANSWER SLOT
 *    updating IS the press feedback).
 *
 *  Revision history:
 *    Rev 1 — original module delivery
 *    Rev 2 — verification fixes (AppendU16 score, AppendStr offsets,
 *            beat-2 placeholder removal)
 *    Rev 3 — THIS FILE: final polish. Generator X-case cleaned,
 *            entry-order fixed (Generate BEFORE RepaintAll — the
 *            stale-first-paint class, same family as Snake's ghost
 *            food), points calc simplified, MQ_KEYS removed.
 *
 *  Created on: Sep 30, 2026
 */
#include "STD_TYPES.h"
#include <MCAL/SYSTICK/MSYSTICK_interface.h>
#include <HAL/TFT/HTFT_interface.h>
#include "APP/APP_MATHQ.h"
#include "APP/APP_INPUT.h"
#include "HAL/AUDIO/HAUDIO_interface.h"
#include "APP/MATHQ_renderer.h"
#include "APP/MATHQ_config.h"

/* the shared LCG — seeded at Run entry (menu navigation entropy)    */
static u32 Mq_u32RandState = 12345u;
static u32 Mq_u32Rand(void)
{ Mq_u32RandState = (Mq_u32RandState * 1103515245u) + 12345u;
  return (Mq_u32RandState >> 16u); }

/* ---- states ---- */
typedef enum
{
    MQ_ST_QUESTION = 0,     /* live: navigate + type + submit           */
    MQ_ST_FEED_GOOD,        /* green reveal, MATHQ_CFG_FEEDBACK_GOOD_MS */
    MQ_ST_FEED_BAD,         /* the §7 three beats                       */
    MQ_ST_RESULTS,          /* round complete: accuracy screen           */
    MQ_ST_EXIT_ASK          /* the exit seam                            */
} Mq_State_t;

#define MQ_EXIT_ASK_MS          5000u   /* the house seam window        */
#define MQ_ANSWER_MAX           2u      /* two answer slots              */
#define MQ_SHIFTS               2u      /* F2 REVIEW FIX: was 4 — on 5-char
                                           equations (any two-digit operand),
                                           the shake's opaque glyphs overdraw
                                           the panel border columns (static
                                           line x=8 -> draws at x=4; answer
                                           end 123 -> the right border), and
                                           ErasePanelInterior (5..122) never
                                           repairs them: a permanent notch
                                           after every wrong two-digit
                                           answer. At 2, every draw stays
                                           inside 5..122; the shake remains
                                           clearly visible.                 */

/* ---- module state (all file-private) ---- */
static Mq_State_t Mq_eState;
static u8  Mq_u8KeyIdx;                          /* cursor: 0..11        */
static char Mq_achAnswer[MQ_ANSWER_MAX];         /* typed digits         */
static u8  Mq_u8AnsLen;
static u8  Mq_u8A; static char Mq_chOp; static u8 Mq_u8B;   /* question */
static u8  Mq_u8Answer;                          /* the correct value    */
static u8  Mq_u8Hearts;
static u16 Mq_u16Score;
static u8  Mq_u8Streak;
static u8  Mq_u8RoundCorrect;                    /* level ramp driver    */
static u8  Mq_u8QNum;                            /* 1..10 in the round   */
static u32 Mq_u32T0;                             /* beat timer           */
static u8  Mq_u8FeedPhase;                       /* 0/1/2 = beat 1/2/3   */
static u8  Mq_u8ShakeStep;                       /* beat-1 shake steps   */

static const SongNote_t Mq_astTheme[] =
    { { 262u, 150u }, { 330u, 150u }, { 392u, 150u }, { 523u, 250u },
      { 0u, 150u }, { 392u, 150u }, { 330u, 150u }, { 262u, 150u },
      { 294u, 150u }, { 349u, 150u }, { 440u, 150u }, { 523u, 250u },
      { 0u, 300u } };
static const Song_t Mq_stTheme =
    { Mq_astTheme, 13u, 1u, 4u };
static const SongNote_t Mq_astLoseSting[] =
    { { 330u, 300u }, { 262u, 300u }, { 196u, 600u } };
static const Song_t Mq_stLoseSting =
    { Mq_astLoseSting, 3u, 0u, 0u };

/* ---- small string helpers (no libc — the house rule) -------------- */

/* Copy a string (dest must be large enough — caller owns sizes).   */
static void Mq_voidCopyZ(char *Copy_pchDst, const char *Copy_pchSrc)
{
    u8 Local_u8I = 0u;
    while (Copy_pchSrc[Local_u8I] != '\0')
    { Copy_pchDst[Local_u8I] = Copy_pchSrc[Local_u8I]; Local_u8I++; }
    Copy_pchDst[Local_u8I] = '\0';
}

/* Append a u8's decimal digits to a string (digits appended in the
 * correct order; finds the destination's current length itself).    */
static void Mq_voidAppendU8(char *Copy_pchDst, u8 Copy_u8Val)
{
    char Local_ach[4];
    u8  Local_u8N = 0u;
    u8  Local_u8Len = 0u;
    u8  Local_u8I;

    if (Copy_u8Val == 0u) { Local_ach[Local_u8N++] = '0'; }
    while (Copy_u8Val > 0u)
    {
        Local_ach[Local_u8N++] = (char)('0' + (Copy_u8Val % 10u));
        Copy_u8Val /= 10u;
    }

    while (Copy_pchDst[Local_u8Len] != '\0') { Local_u8Len++; }
    for (Local_u8I = Local_u8N; Local_u8I > 0u; Local_u8I--)
    { Copy_pchDst[Local_u8Len++] = Local_ach[Local_u8I - 1u]; }
    Copy_pchDst[Local_u8Len] = '\0';
}

/* u16 variant: the u8 sibling silently wraps >255 (the reviewer's
 * catch — long sessions cross it). Digits appended in order.        */
static void Mq_voidAppendU16(char *Copy_pchDst, u16 Copy_u16Val)
{
    char Local_ach[6];
    u8  Local_u8N = 0u;
    u8  Local_u8Len = 0u;
    u8  Local_u8I;

    if (Copy_u16Val == 0u) { Local_ach[Local_u8N++] = '0'; }
    while (Copy_u16Val > 0u)
    {
        Local_ach[Local_u8N++] = (char)('0' + (Copy_u16Val % 10u));
        Copy_u16Val /= 10u;
    }
    while (Copy_pchDst[Local_u8Len] != '\0') { Local_u8Len++; }
    for (Local_u8I = Local_u8N; Local_u8I > 0u; Local_u8I--)
    { Copy_pchDst[Local_u8Len++] = Local_ach[Local_u8I - 1u]; }
    Copy_pchDst[Local_u8Len] = '\0';
}

/* Append a string to a string (offset computed, never guessed — the
 * fix for the fragile +8/+10 constants in the RESULTS painter).    */
static void Mq_voidAppendStr(char *Copy_pchDst, const char *Copy_pchSrc)
{
    u8 Local_u8Len = 0u;
    while (Copy_pchDst[Local_u8Len] != '\0') { Local_u8Len++; }
    Mq_voidCopyZ(&Copy_pchDst[Local_u8Len], Copy_pchSrc);
}

/* level from the round-relative correct count (the retuned ramp:
 * L2@4, L3@7 — both reachable inside a 10-question round)          */
static u8 Mq_u8LevelNow(void)
{
    if (Mq_u8RoundCorrect >= MATHQ_CFG_LEVEL3_AT) { return 3u; }
    if (Mq_u8RoundCorrect >= MATHQ_CFG_LEVEL2_AT) { return 2u; }
    return 1u;
}

/* ---- the generator (contracts in the banner) ---- */
static void Mq_voidGenerate(void)
{
    u8 Local_u8Level = Mq_u8LevelNow();
    u8 Local_u8Pick;
    u8 Local_u8Range = (Local_u8Level >= 2u) ? 20u : 10u;

    if      (Local_u8Level == 1u)
    { Local_u8Pick = (u8)(Mq_u32Rand() % 2u); }
    else if (Local_u8Level == 2u)
    { Local_u8Pick = (u8)(Mq_u32Rand() % 3u); }
    else
    { Local_u8Pick = (u8)(Mq_u32Rand() % 4u); }

    if (Local_u8Pick == 0u)                      /* addition */
    {
        Mq_chOp = '+';
        Mq_u8A = (u8)(2u + (Mq_u32Rand() % (Local_u8Range - 1u)));
        Mq_u8B = (u8)(2u + (Mq_u32Rand() % 8u));
        Mq_u8Answer = (u8)(Mq_u8A + Mq_u8B);
    }
    else if (Local_u8Pick == 1u)                 /* subtraction: a >= b, no negatives */
    {
        Mq_chOp = '-';
        Mq_u8A = (u8)(2u + (Mq_u32Rand() % (Local_u8Range - 1u)));
        Mq_u8B = (u8)(2u + (Mq_u32Rand() % 8u));
        if (Mq_u8B > Mq_u8A)                     /* swap, never borrow */
        { u8 Local_u8T = Mq_u8A; Mq_u8A = Mq_u8B; Mq_u8B = Local_u8T; }
        Mq_u8Answer = (u8)(Mq_u8A - Mq_u8B);
    }
    else if (Local_u8Pick == 2u)                 /* multiplication: tables */
    {
        Mq_chOp = 'X';
        Mq_u8A = (u8)(2u + (Mq_u32Rand() % 8u));
        Mq_u8B = (u8)(2u + (Mq_u32Rand() % 8u));   /* REV 3: was the dead
                                           "Mq_u8B * 0u +" artifact */
        Mq_u8Answer = (u8)(Mq_u8A * Mq_u8B);
    }
    else                                         /* division, built backwards */
    {
        u8 Local_u8Divisor = (u8)(2u + (Mq_u32Rand() % 8u));
        u8 Local_u8Quotient = (u8)(2u + (Mq_u32Rand() % 8u));
        Mq_chOp = '/';
        Mq_u8A = (u8)(Local_u8Divisor * Local_u8Quotient);  /* dividend  */
        Mq_u8B = Local_u8Divisor;
        Mq_u8Answer = Local_u8Quotient;                     /* always clean */
    }
}

/* ---- painting from state (the screen-is-cache law) ---- */

/* the answer area: typed digits, or '?' while empty                */
static void Mq_voidPaintAnswerNow(u16 Copy_u16Color)
{
    if (Mq_u8AnsLen != 0u)
    { MATHQ_voidPaintAnswer(Mq_achAnswer, Mq_u8AnsLen, Copy_u16Color); }
    else
    { MATHQ_voidPaintAnswer("?", 1u, Copy_u16Color); }
}

/* the full question line at the given shift + answer color         */
static void Mq_voidPaintQuestion(u16 Copy_u16AnsColor, s8 Copy_s8Shift)
{
    MATHQ_voidPaintQuestionLine(Mq_u8A, Mq_chOp, Mq_u8B,
            (Mq_u8AnsLen != 0u) ? Mq_achAnswer : "?",
            (Mq_u8AnsLen != 0u) ? Mq_u8AnsLen : 1u,
            Copy_u16AnsColor, Copy_s8Shift);
}

/* regenerate EVERYTHING from statics. PaintScreen paints the entry
 * defaults (score 0, level 1, hearts on); the real values are
 * painted over it immediately after. NOTE THE ORDER (Rev 3): the
 * CALLER generates the question FIRST — RepaintAll paints whatever
 * the statics hold, so generating after would flash one stale
 * question (the Snake ghost-food class).                          */
static void Mq_voidRepaintAll(void)
{
    u8 Local_u8I;
    MATHQ_voidPaintScreen();
    MATHQ_voidPaintScore(Mq_u16Score);
    MATHQ_voidPaintLevel(Mq_u8LevelNow());
    for (Local_u8I = 0u; Local_u8I < MATHQ_CFG_LIVES; Local_u8I++)
    { MATHQ_voidPaintHeart(Local_u8I, (u8)(Local_u8I < Mq_u8Hearts)); }
    MATHQ_voidPaintMessage("TYPE ANSWER", MATHQ_CFG_COLOR_PROMPT);
    Mq_voidPaintQuestion(MATHQ_CFG_COLOR_ANSWER_TYPED, 0);
    MATHQ_voidPaintKeyCursor(Mq_u8KeyIdx, 1u);
}

/* new question: generate, reset the buffer, paint, back to live.
 * No full repaint needed — PaintQuestionLine wipes the panel
 * interior itself (the renderer's wipe-first contract).            */
static void Mq_voidNextQuestion(void)
{
    Mq_voidGenerate();
    Mq_u8AnsLen = 0u;
    Mq_voidPaintQuestion(MATHQ_CFG_COLOR_ANSWER_TYPED, 0);
    MATHQ_voidPaintMessage("TYPE ANSWER", MATHQ_CFG_COLOR_PROMPT);
    Mq_eState = MQ_ST_QUESTION;
}

/* ---- typing (the cursor-press actions) ---- */

static void Mq_voidAppendDigit(char Copy_chDigit)
{
    if (Mq_u8AnsLen < MQ_ANSWER_MAX)
    {
        Mq_achAnswer[Mq_u8AnsLen++] = Copy_chDigit;
        Mq_voidPaintAnswerNow(MATHQ_CFG_COLOR_ANSWER_TYPED);  /* the press
            feedback IS the slot updating (see banner: no flash state) */
    }
}

static void Mq_voidBackspace(void)
{
    if (Mq_u8AnsLen > 0u)
    {
        Mq_u8AnsLen--;
        Mq_voidPaintAnswerNow(MATHQ_CFG_COLOR_ANSWER_TYPED);
    }
}

static void Mq_voidSubmit(void)
{
    u8 Local_u8Typed = 0u;
    if (Mq_u8AnsLen == 0u) { return; }           /* nothing to check */

    Local_u8Typed = (u8)(Mq_achAnswer[0] - '0');
    if (Mq_u8AnsLen == 2u)
    { Local_u8Typed = (u8)((Local_u8Typed * 10u) + (Mq_achAnswer[1] - '0')); }

    if (Local_u8Typed == Mq_u8Answer)
    {
        u8 Local_u8Points;
        char Local_achMsg[17];

        Mq_u8Streak++;
        Mq_u8RoundCorrect++;
        /* points = 1 + min(streak-1, cap) — the streak-1 term IS the
         * streak itself at this point (Rev 3 simplification)        */
        Local_u8Points = Mq_u8Streak;
        if (Local_u8Points > (1u + MATHQ_CFG_STREAK_CAP))
        { Local_u8Points = (u8)(1u + MATHQ_CFG_STREAK_CAP); }
        Mq_u16Score = (u16)(Mq_u16Score + Local_u8Points);
        MATHQ_voidPaintScore(Mq_u16Score);

        /* the good message: level-up beats streak beats plain         */
        if (Mq_u8RoundCorrect == MATHQ_CFG_LEVEL2_AT)
        { Mq_voidCopyZ(Local_achMsg, "LEVEL 2!"); }
        else if (Mq_u8RoundCorrect == MATHQ_CFG_LEVEL3_AT)
        { Mq_voidCopyZ(Local_achMsg, "LEVEL 3!"); }
        else if (Mq_u8Streak >= 2u)
        {
            Mq_voidCopyZ(Local_achMsg, "STREAK +");
            Mq_voidAppendU8(Local_achMsg, (u8)(Local_u8Points - 1u));
        }
        else
        { Mq_voidCopyZ(Local_achMsg, "GOOD!"); }

        MATHQ_voidPaintAnswer(Mq_achAnswer, Mq_u8AnsLen, MATHQ_CFG_COLOR_CORRECT);
        MATHQ_voidPaintMessage(Local_achMsg, MATHQ_CFG_COLOR_CORRECT);
        Mq_eState = MQ_ST_FEED_GOOD;
        Mq_u32T0 = MSYSTICK_GetElapsed_ms();
    }
    else
    {
        /* the §7 three beats start here — see the state handler      */
        Mq_u8Streak = 0u;
        Mq_u8FeedPhase = 0u;
        Mq_u8ShakeStep = 0u;
        Mq_eState = MQ_ST_FEED_BAD;
        Mq_u32T0 = MSYSTICK_GetElapsed_ms();
        Mq_voidPaintQuestion(MATHQ_CFG_COLOR_WRONG, 0);   /* red, shaking */
    }
}

/* cursor move with WRAP inside the 3x4 grid (the grid convention —
 * menus clamp, grids wrap). The old ring is erased with the
 * renderer's four-strip erase (the key is never touched), the new
 * one drawn — different-cell law.                                    */
static void Mq_voidMoveCursor(s8 Copy_s8RowDelta, s8 Copy_s8ColDelta)
{
    u8 Local_u8Row = (u8)(Mq_u8KeyIdx / 3u);
    u8 Local_u8Col = (u8)(Mq_u8KeyIdx % 3u);

    MATHQ_voidPaintKeyCursor(Mq_u8KeyIdx, 0u);
    Local_u8Row = (u8)((Local_u8Row + Copy_s8RowDelta + 4u) % 4u);
    Local_u8Col = (u8)((Local_u8Col + Copy_s8ColDelta + 3u) % 3u);
    Mq_u8KeyIdx = (u8)((Local_u8Row * 3u) + Local_u8Col);
    MATHQ_voidPaintKeyCursor(Mq_u8KeyIdx, 1u);
}

/* ---- the RESULTS screen (round complete; the app-layer direct-draw
 *      exception: terminal screens own no renderer) ---- */
static void Mq_voidPaintResults(void)
{
    char Local_ach[17];

    (void)HAUDIO_enumStop();
    HTFT_voidFillScreen(HTFT_COLOR_BLACK);
    HTFT_voidDrawString2x(8u, 8u, "RESULTS", HTFT_COLOR_CYAN, HTFT_COLOR_BLACK);

    /* u16 append — scores cross 255 in long sessions (Rev 2 fix)     */
    Mq_voidCopyZ(Local_ach, "SCORE: ");
    Mq_voidAppendU16(Local_ach, (u16)(Mq_u16Score % 1000u));
    HTFT_voidDrawString(0u, 48u, Local_ach, HTFT_COLOR_WHITE, HTFT_COLOR_BLACK);

    /* AppendStr — offsets computed, never guessed (Rev 2 fix): the
     * old "+8/+10" constants ate the first digit on ACCURACY and
     * broke RIGHT exactly on a 10/10 round                          */
    Mq_voidCopyZ(Local_ach, "RIGHT: ");
    Mq_voidAppendU8(Local_ach, Mq_u8RoundCorrect);
    Mq_voidAppendStr(Local_ach, "/10");
    HTFT_voidDrawString(0u, 60u, Local_ach, HTFT_COLOR_WHITE, HTFT_COLOR_BLACK);

    Mq_voidCopyZ(Local_ach, "ACCURACY: ");
    Mq_voidAppendU8(Local_ach, (u8)(Mq_u8RoundCorrect * 10u));
    Mq_voidAppendStr(Local_ach, "%");
    HTFT_voidDrawString(0u, 72u, Local_ach, MATHQ_CFG_COLOR_CORRECT, HTFT_COLOR_BLACK);

    HTFT_voidDrawString(8u, 120u, "CH:NEXT 0:DONE", HTFT_COLOR_YELLOW, HTFT_COLOR_BLACK);
    Mq_eState = MQ_ST_RESULTS;
}

/* ---- public contract ---- */

GameResult_t APP_MATHQ_enumRun(void)
{
    /* seed from navigation time — the entropy source, as in Snake   */
    Mq_u32RandState ^= MSYSTICK_GetElapsed_ms();
    Mq_u32RandState = (Mq_u32RandState * 1103515245u) + 12345u;

    Mq_u8Hearts       = MATHQ_CFG_LIVES;
    Mq_u16Score       = 0u;
    Mq_u8Streak       = 0u;
    Mq_u8RoundCorrect = 0u;
    Mq_u8QNum         = 1u;
    Mq_u8KeyIdx       = 0u;
    Mq_u8AnsLen       = 0u;

    /* REV 3 ORDER: generate FIRST, then repaint — RepaintAll paints
     * the statics as they are, so this shows the real question with
     * zero stale flash. (Was: Repaint -> Generate -> PaintQuestion,
     * which painted one question, then replaced it.)                */
    Mq_voidGenerate();
    Mq_voidRepaintAll();
    Mq_eState = MQ_ST_QUESTION;

    while (1)
    {
        u32 Local_u32Now = MSYSTICK_GetElapsed_ms();
        APP_INPUT_Event_t Local_stEvt;
        u8 Local_u8HaveEvt =
            (APP_INPUT_enumGetEvent(&Local_stEvt) == APP_INPUT_OK);

        HAUDIO_voidTask();
        if ((Mq_eState == MQ_ST_QUESTION) &&
            (HAUDIO_enumGetState() == HAUDIO_STATE_IDLE))
        {
            (void)HAUDIO_enumPlaySong(&Mq_stTheme);
        }

        switch (Mq_eState)
        {
        case MQ_ST_QUESTION:
            if (Local_u8HaveEvt != 0u)
            {
                switch (Local_stEvt.Button)
                {
                case BTN_NUM_2: case BTN_UP:    Mq_voidMoveCursor(-1,  0); break;
                case BTN_NUM_8: case BTN_DOWN:  Mq_voidMoveCursor( 1,  0); break;
                case BTN_NUM_4: case BTN_LEFT:  Mq_voidMoveCursor( 0, -1); break;
                case BTN_NUM_6: case BTN_RIGHT: Mq_voidMoveCursor( 0,  1); break;

                case BTN_CONFIRM:
                    if (Local_stEvt.IsRepeat != 0u) { break; }
                    /* press the key under the cursor */
                    if      (Mq_u8KeyIdx <= 8u)  { Mq_voidAppendDigit((char)('1' + Mq_u8KeyIdx)); }
                    else if (Mq_u8KeyIdx == 9u)  { Mq_voidBackspace(); }
                    else if (Mq_u8KeyIdx == 10u) { Mq_voidAppendDigit('0'); }
                    else                         { Mq_voidSubmit(); }
                    break;

                case BTN_BACK:
                    if (Local_stEvt.IsRepeat != 0u) { break; }
                    Mq_eState = MQ_ST_EXIT_ASK;
                    Mq_u32T0 = Local_u32Now;
                    MATHQ_voidPaintMessage("EXIT? 0 AGAIN", MATHQ_CFG_COLOR_WRONG);
                    break;

                default:
                    break;
                }
            }
            break;

        case MQ_ST_FEED_GOOD:
            /* F1 REVIEW FIX: correct answers count toward the round
             * too. This site was missing — QNum advanced only on wrong
             * answers (FEED_BAD phase 3), so RESULTS required 10 wrong
             * answers while hearts die at 3: unreachable, rounds dead,
             * one endless round instead. (Also explains the seat's
             * pending "RESULTS glass confirmation" — the screen was
             * never reachable on glass.) FEED_BAD's structure mirrored
             * minus the hearts check: a correct answer cannot cost a
             * heart, so the GAMEOVER branch does not exist here.       */
            if ((Local_u32Now - Mq_u32T0) >= MATHQ_CFG_FEEDBACK_GOOD_MS)
            {
                if (Mq_u8QNum >= MATHQ_CFG_QUESTIONS_PER_ROUND)
                {
                    Mq_voidPaintResults();      /* sets MQ_ST_RESULTS  */
                }
                else
                {
                    Mq_u8QNum++;
                    Mq_voidNextQuestion();
                }
            }
            break;

        case MQ_ST_FEED_BAD:
        {
            /* THE §7 TIMELINE — three beats the child can process    */
            u32 Local_u32Elapsed = Local_u32Now - Mq_u32T0;

            if (Mq_u8FeedPhase == 0u)          /* beat 1: wrong + shake */
            {
                if ((Mq_u8ShakeStep == 0u) && (Local_u32Elapsed >= (1u * MATHQ_CFG_SHAKE_STEP_MS)))
                { Mq_u8ShakeStep = 1u; Mq_voidPaintQuestion(MATHQ_CFG_COLOR_WRONG, (s8)-MQ_SHIFTS); }
                else if ((Mq_u8ShakeStep == 1u) && (Local_u32Elapsed >= (2u * MATHQ_CFG_SHAKE_STEP_MS)))
                { Mq_u8ShakeStep = 2u; Mq_voidPaintQuestion(MATHQ_CFG_COLOR_WRONG, (s8)MQ_SHIFTS); }
                else if ((Mq_u8ShakeStep == 2u) && (Local_u32Elapsed >= (3u * MATHQ_CFG_SHAKE_STEP_MS)))
                {
                    Mq_u8ShakeStep = 3u;
                    Mq_voidPaintQuestion(MATHQ_CFG_COLOR_WRONG, 0);
                    MATHQ_voidPaintMessage("WRONG", MATHQ_CFG_COLOR_WRONG);
                }
                else if (Local_u32Elapsed >= MATHQ_CFG_FEEDBACK_WRONG_MS)
                {
                    /* beat 2: the teaching reveal + heart loss        */
                    char Local_achMsg[17];
                    u8 Local_u8I = 0u;
                    char Local_achAns[3];

                    Mq_u8FeedPhase = 1u;
                    Mq_u32T0 = Local_u32Now;
                    if (Mq_u8Hearts > 0u) { Mq_u8Hearts--; }
                    MATHQ_voidPaintHeart(Mq_u8Hearts, 0u);

                    /* the REAL answer, green — built from the
                     * generator's value, both digits when needed       */
                    if (Mq_u8Answer >= 10u)
                    { Local_achAns[Local_u8I++] = (char)('0' + (Mq_u8Answer / 10u)); }
                    Local_achAns[Local_u8I++] = (char)('0' + (Mq_u8Answer % 10u));
                    Local_achAns[Local_u8I] = '\0';
                    MATHQ_voidPaintAnswer(Local_achAns, Local_u8I, MATHQ_CFG_COLOR_CORRECT);

                    Mq_voidCopyZ(Local_achMsg, "ANSWER: ");
                    Mq_voidAppendU8(Local_achMsg, Mq_u8Answer);
                    MATHQ_voidPaintMessage(Local_achMsg, MATHQ_CFG_COLOR_CORRECT);
                }
            }
            else if (Mq_u8FeedPhase == 1u)     /* beat 2: reveal holds */
            {
                if (Local_u32Elapsed >= MATHQ_CFG_FEEDBACK_REVEAL_MS)
                {
                    Mq_u8FeedPhase = 2u;
                    Mq_u32T0 = Local_u32Now;
                }
            }
            else                               /* beat 3: the gap      */
            {
                if (Local_u32Elapsed >= MATHQ_CFG_FEEDBACK_GAP_MS)
                {
                    if (Mq_u8Hearts == 0u)
                    {
                        (void)HAUDIO_enumPlaySting(&Mq_stLoseSting);
                        return GAME_RESULT_GAMEOVER;
                    }
                    if (Mq_u8QNum >= MATHQ_CFG_QUESTIONS_PER_ROUND)
                    { Mq_voidPaintResults(); }
                    else
                    { Mq_u8QNum++; Mq_voidNextQuestion(); }
                }
            }
            break;
        }

        case MQ_ST_RESULTS:
            if (Local_u8HaveEvt != 0u)
            {
                if (Local_stEvt.Button == BTN_CONFIRM)
                {
                    /* fresh round: ramp resets, hearts and score stay.
                     * REV 3 ORDER: generate before the repaint.       */
                    Mq_u8QNum = 1u;
                    Mq_u8RoundCorrect = 0u;
                    Mq_u8KeyIdx = 0u;
                    Mq_u8AnsLen = 0u;
                    Mq_voidGenerate();
                    Mq_voidRepaintAll();
                    Mq_eState = MQ_ST_QUESTION;
                }
                else if (Local_stEvt.Button == BTN_BACK)
                {
                    /* leaving AT RESULTS = a completed game: the score
                     * is real and lands on the leaderboard            */
                    return GAME_RESULT_GAMEOVER;
                }
            }
            break;

        case MQ_ST_EXIT_ASK:
            if (Local_u8HaveEvt != 0u)
            {
                if (Local_stEvt.Button == BTN_BACK)
                { return GAME_RESULT_EXITED; }            /* the seam   */
                if ((Local_stEvt.Button == BTN_CONFIRM) ||
                    (Local_stEvt.Button == BTN_NUM_2) || (Local_stEvt.Button == BTN_NUM_8))
                { Mq_voidRepaintAll(); Mq_eState = MQ_ST_QUESTION; break; }
            }
            if ((Local_u32Now - Mq_u32T0) >= MQ_EXIT_ASK_MS)
            { Mq_voidRepaintAll(); Mq_eState = MQ_ST_QUESTION; }
            break;

        default:
            break;
        }
    }
}

u16 APP_MATHQ_u16GetScore(void) { return Mq_u16Score; }
