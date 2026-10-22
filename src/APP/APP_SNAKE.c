/*
 * APP_SNAKE.c - the Snake game module: FSM + logic, absorbed from the
 *  hardware-proven standalone Snake v1.0 main.
 *  LOCATION: APP/ (flat).
 *
 *  WHAT IT IS: the whole game between the hub's two calls. The hub
 *  suspends inside enumRun() for the entire session; nothing polls
 *  this module, nothing pushes commands into it. It paints through
 *  the dumb renderer (SNAKE_renderer.c), reads its tunables from
 *  SNAKE_config.h, drinks input from APP_INPUT, and RETURNS a
 *  GameResult_t when done. That is the entire integration surface.
 *
 *  THE SIX DELTAS FROM THE STANDALONE v1.0 main (each is a direct
 *  consequence of the hub architecture - the engine itself was NOT
 *  touched; the tick/queue/food/collision code below is the code
 *  that passed on hardware):
 *    1. NO BOOT STATE. The hub's menu already selected this game -
 *       Run entry IS the start. The old "CH OR 0 TO START" screen
 *       and its CONFIRM wait are gone.
 *    2. NO GAMEOVER SCREEN. Death runs its blink+hold, then RETURNS
 *       GAME_RESULT_GAMEOVER. The HUB paints GameOver, reads the
 *       score, updates the leaderboard. One screen, one owner.
 *    3. RNG SEEDED AT RUN ENTRY. The old BOOT state seeded on the
 *       first CONFIRM press (human reaction = entropy). In the hub
 *       world the equivalent entropy is the menu-navigation time
 *       before launch - elapsed ms at Run entry. Different every
 *       launch, zero extra states.
 *    4. BACK = EXIT ASK (the exit seam). Standalone v1.0 restarted
 *       instantly on BACK; the hub needs an EXIT path, so BACK opens
 *       a 5-second "EXIT? 0 AGAIN" confirm: BACK again returns
 *       GAME_RESULT_EXITED (hub goes home), anything else or timeout
 *       resumes the exact state the player was in (run or pause).
 *    5. RUNTIME DIFFICULTY. SNAKE_config.h's zone-1 values are
 *       compile-time NORMAL; the difficulty authority is now the
 *       APP_STORE setting (set by the hub's Settings screen).
 *       enumInit maps store value -> this module's RAM config.
 *       EASY = slow + wrap-around walls (the v1.0 modulo branch,
 *       finally live), NORMAL = the proven defaults, HARD = fast
 *       floor + solid walls.
 *    6. HI BELONGS TO THE STORE. This module never writes high
 *       scores - it only READS the store's HI for the in-game HUD.
 *       The hub writes it once, after GAMEOVER. One writer, one
 *       truth (the APP_STORE ownership rule).
 *
 *  DESIGN PRINCIPLES (documented, deliberate):
 *
 *  1. RUN OWNS THE LOOP - see the header. The ~270 ms IR stall per
 *     fresh press happens inside this loop, exactly where the
 *     standalone build proved it graceful: the world freezes one
 *     beat, SysTick and every ISR run right through it, held D-pad
 *     repeats (~11 ms each) ARE the glide.
 *  2. ABSORBED, NOT REWRITTEN - the engine's order of operations,
 *     thresholds, and laws are byte-for-byte the tested build's.
 *  3. THE SCREEN IS A CACHE OF STATE - every pixel is regenerable
 *     from the module's statics; Snk_voidRepaintAll() is the proof
 *     and the tool. That is what makes pause and the exit overlay
 *     trivial: repaint, never "restore".
 *  4. TWO-TONE AT BIRTH - each body cell's tone is assigned when the
 *     cell is created and NEVER recomputed, so growth ticks cannot
 *     strobe the pattern (the documented trap). The tone is stored
 *     per ring slot so RepaintAll reproduces the exact stripes.
 *  5. SKIP-LATE TIMING - a late tick fires once and rebases; time
 *     lost to a stall is skipped, never burst-caught-up.
 *
 *  STATE MACHINE (4 states - the hub owns boot and game-over):
 *      SNK_RUN -> SNK_PAUSE (START) -> SNK_RUN (START)
 *      SNK_RUN / SNK_PAUSE -> SNK_EXIT_ASK (BACK)
 *          -> returns EXITED (BACK) | resumes (timeout/other)
 *      SNK_RUN -> SNK_DYING (collision/wall)
 *          -> returns GAMEOVER (after 3 blinks + 600 ms hold)
 *
 *  Created on: Sep 30, 2026
 */
#include "STD_TYPES.h"
#include <MCAL/SYSTICK/MSYSTICK_interface.h>     /* the ms timebase         */
#include <HAL/TFT/HTFT_interface.h>              /* pause/exit overlay text */
#include "APP/APP_SNAKE.h"
#include "APP/APP_INPUT.h"
#include "APP/APP_STORE.h"
#include "APP/SNAKE_renderer.h"
#include "APP/SNAKE_config.h"
#include "HAL/AUDIO/HAUDIO_interface.h"

/* Direction encoding. The ^1 trick: opposite = dir ^ 1 - UP/DOWN and
 * LEFT/RIGHT are adjacent pairs, so a reversal check is one XOR.   */
#define DIR_UP              0u
#define DIR_DOWN            1u
#define DIR_LEFT            2u
#define DIR_RIGHT           3u

/* Ring capacity = whole field (270 cells). Memory cost: 3 arrays of
 * 270 bytes - the price of O(1) head/tail moves and full-state
 * repaint. Trivial against 64 KB.                                  */
#define SNAKE_BODY_MAX      (SNAKE_FIELD_COLS * SNAKE_FIELD_ROWS)

/* Death choreography - the frozen world, the deliberate flicker law
 * (same-cell timed toggle), and the hold before the handoff.       */
#define SNAKE_DEATH_BLINKS  3u
#define SNAKE_DEATH_STEP_MS 150u
#define SNAKE_DEATH_HOLD_MS 600u

/* Exit-confirm window (matches the merged online main's EXIT_ASK). */
#define SNAKE_EXIT_ASK_MS   5000u

typedef enum { SNK_RUN = 0, SNK_PAUSE, SNK_DYING, SNK_EXIT_ASK } Snk_State_t;

/* ---- module state (all file-private, the whole game in ~1 KB) --- */

/* Runtime difficulty - seeded by enumInit from the APP_STORE setting.
 * THE designed zone-1 RAM migration, fed by the hub Settings screen:
 * the SNAKE_config.h compile-time values remain the NORMAL preset's
 * definition; everything else is mapped here.                       */
static struct { u16 TickBase; u16 TickMin; u8 Speedup; u8 WallsHard; } Snk_stCfg;

static Snk_State_t Snk_eState;              /* the FSM                  */

/* The body: a RING of (col,row,tone) slots. Cells never move in the
 * ring - the HEAD INDEX advances; cells enter at the new head slot
 * and leave by falling out of the live window. Cell i behind the
 * head lives at slot (Head + MAX - i) % MAX; the tail is i = Len-1.
 * Stale slots past the window need no clearing - Len bounds reads.  */
static u8  Snk_Au8Col[SNAKE_BODY_MAX];
static u8  Snk_Au8Row[SNAKE_BODY_MAX];
static u8  Snk_Au8Tone[SNAKE_BODY_MAX];     /* BODY_A / BODY_B per slot */
static u16 Snk_u16Head, Snk_u16Len;

/* Direction queue - the 2-deep fairness rule: a queued direction is
 * validated against the LAST QUEUED (or current) direction, so two
 * fast presses both land (UP then LEFT while moving RIGHT = two
 * turns, not one), and reversals are rejected at PUSH time.        */
static u8  Snk_u8DirCur;
static u8  Snk_Au8DirQ[2]; static u8 Snk_u8DirQLen;

static u8  Snk_u8FoodCol, Snk_u8FoodRow, Snk_u8FoodOn, Snk_u8FoodDim;
static u16 Snk_u16Score;
static u16 Snk_u16TickMs;                   /* current period (ramps)   */
static u32 Snk_u32TickLast, Snk_u32PulseLast, Snk_u32DeathT0;
static u8  Snk_u8DeathPhase, Snk_u8ExitPrev;   /* ExitPrev: came from pause? */

static const SongNote_t Snk_astCrashSting[] =
    { { 330u, 300u }, { 262u, 300u }, { 196u, 600u } };
static const Song_t Snk_stCrashSting =
    { Snk_astCrashSting, 3u, 0u };
static const SongNote_t Snk_astTheme[] =
    { { 165u, 200u }, { 165u, 100u }, { 196u, 200u }, { 220u, 200u },
      { 196u, 200u }, { 165u, 200u }, { 147u, 200u }, { 165u, 200u },
      { 0u, 400u } };
static const Song_t Snk_stTheme =
    { Snk_astTheme, 9u, 1u, 4u };

/* The shared LCG (4th use in the project). Seeded at Run entry.    */
static u32 Snk_u32RandState = 12345u;
static u32 Snk_u32Rand(void)
{ Snk_u32RandState = (Snk_u32RandState * 1103515245u) + 12345u;
  return (Snk_u32RandState >> 16u); }

/* ------------------------------------------------------------------ *
 *  Body query - with THE tail-vacating exception.                     *
 *  IgnoreTail=1 skips the last cell: the tail vacates THIS tick
 *  (moves away), so the head may legally move into it. The caller
 *  passes IgnoreTail only when the snake is NOT growing this tick -
 *  on a growth tick the tail stays put and its cell is lethal.      *
 *  Cost: O(Len) - at most 270 compares, once per tick.              */
static u8 Snk_u8BodyAt(s16 Copy_s16Col, s16 Copy_s16Row, u8 Copy_u8IgnoreTail)
{
    u16 Local_u16I;
    for (Local_u16I = 0u; Local_u16I < Snk_u16Len; Local_u16I++)
    {
        u16 Local_u16Slot;
        if ((Copy_u8IgnoreTail != 0u) && (Local_u16I == (Snk_u16Len - 1u))) { continue; }
        Local_u16Slot = (u16)((Snk_u16Head + SNAKE_BODY_MAX - Local_u16I) % SNAKE_BODY_MAX);
        if (((s16)Snk_Au8Col[Local_u16Slot] == Copy_s16Col) &&
            ((s16)Snk_Au8Row[Local_u16Slot] == Copy_s16Row)) { return 1u; }
    }
    return 0u;
}

/* Direction queue push - the fairness rule. Reference direction is
 * the queue's LAST entry if any (so chains validate against each
 * other), else the current motion. Reject: same direction (no-op)
 * and reversal (would be an instant 180). Queue depth 2 = the
 * maximum useful lookahead at human press rates.                    */
static void Snk_voidQueueDir(u8 Copy_u8Dir)
{
    u8 Local_u8Ref = (Snk_u8DirQLen != 0u) ? Snk_Au8DirQ[Snk_u8DirQLen - 1u]
                                           : Snk_u8DirCur;
    if ((Copy_u8Dir != Local_u8Ref) &&
        (Copy_u8Dir != (u8)(Local_u8Ref ^ 1u)) &&
        (Snk_u8DirQLen < 2u))
    { Snk_Au8DirQ[Snk_u8DirQLen++] = Copy_u8Dir; }
}

/* Food placement: rejection sampling over the field (cheap: the body
 * rarely covers a meaningful fraction). Fallback: a linear scan from
 * a random start - guarantees termination even at extreme lengths.
 * Degenerate "board full" leaves FoodOn=0 and the game simply runs
 * on (documented edge: unreachable in practice at 270 cells).       */
static void Snk_voidSpawnFood(void)
{
    s16 Local_s16C, Local_s16R;
    u32 Local_u32Tries = 0u;
    do {
        Local_s16C = (s16)(Snk_u32Rand() % SNAKE_FIELD_COLS);
        Local_s16R = (s16)(Snk_u32Rand() % SNAKE_FIELD_ROWS);
        Local_u32Tries++;
    } while ((Snk_u8BodyAt(Local_s16C, Local_s16R, 0u) != 0u) &&
             (Local_u32Tries < 64u));
    if (Snk_u8BodyAt(Local_s16C, Local_s16R, 0u) != 0u)
    {
        u16 Local_u16I; u8 Local_u8Found = 0u;
        u16 Local_u16Start = (u16)(Snk_u32Rand() % SNAKE_BODY_MAX);
        for (Local_u16I = 0u; (Local_u16I < SNAKE_BODY_MAX) && (Local_u8Found == 0u); Local_u16I++)
        {
            u16 Local_u16Cell = (u16)((Local_u16Start + Local_u16I) % SNAKE_BODY_MAX);
            Local_s16C = (s16)(Local_u16Cell % SNAKE_FIELD_COLS);
            Local_s16R = (s16)(Local_u16Cell / SNAKE_FIELD_COLS);
            if (Snk_u8BodyAt(Local_s16C, Local_s16R, 0u) == 0u) { Local_u8Found = 1u; }
        }
        if (Local_u8Found == 0u) { Snk_u8FoodOn = 0u; return; }
    }
    Snk_u8FoodCol = (u8)Local_s16C;  Snk_u8FoodRow = (u8)Local_s16R;
    Snk_u8FoodOn = 1u;  Snk_u8FoodDim = 0u;
    SNAKE_voidPaintCell(Snk_u8FoodCol, Snk_u8FoodRow, SNAKE_CELL_FOOD);
}

/* Full repaint from state - the screen-is-cache law made executable.
 * Wipes everything, then rebuilds border, HUD, body (each slot's
 * STORED tone - the stripes come back exactly), and food. This one
 * function is why pause/resume and exit-ask need no "restore" logic
 * of any kind: overlays are removed by regenerating what was there. */
static void Snk_voidRepaintAll(void)
{
    u16 Local_u16I, Local_u16Slot;
    SNAKE_voidPaintField((u8)Snk_stCfg.WallsHard);   /* CHANGED: border only
                                          when walls are solid (EASY = open) */
    SNAKE_voidPaintScore(Snk_u16Score);
    SNAKE_voidPaintHi(APP_STORE_u16GetHi(0u));   /* display only - the
                                                   store owns the record */
    for (Local_u16I = 0u; Local_u16I < Snk_u16Len; Local_u16I++)
    {
        Local_u16Slot = (u16)((Snk_u16Head + SNAKE_BODY_MAX - Local_u16I) % SNAKE_BODY_MAX);
        SNAKE_voidPaintCell(Snk_Au8Col[Local_u16Slot], Snk_Au8Row[Local_u16Slot],
                (Local_u16I == 0u) ? SNAKE_CELL_HEAD : (SNAKE_CellRole_t)Snk_Au8Tone[Local_u16Slot]);
    }
    if (Snk_u8FoodOn != 0u)
    {
        SNAKE_voidPaintCell(Snk_u8FoodCol, Snk_u8FoodRow,
                (Snk_u8FoodDim != 0u) ? SNAKE_CELL_FOOD_DIM : SNAKE_CELL_FOOD);
    }
}

/* Fresh game: 3-cell body on the center row heading right. Tones
 * are assigned AT CREATION (the law) for the two body cells; the
 * head slot's tone is irrelevant until it becomes body on tick one.
 * RepaintAll wipes the previous game's world, first food spawns.   */
static void Snk_voidNewGame(u32 Copy_u32Now)
{
    Snk_u16Len  = SNAKE_CFG_START_LEN;
    Snk_u16Head = (u16)(SNAKE_CFG_START_LEN - 1u);
    Snk_Au8Col[0] = 3u;  Snk_Au8Row[0] = 9u;   /* tail   */
    Snk_Au8Col[1] = 4u;  Snk_Au8Row[1] = 9u;
    Snk_Au8Col[2] = 5u;  Snk_Au8Row[2] = 9u;   /* head   */
    Snk_Au8Tone[0] = SNAKE_CELL_BODY_A;
    Snk_Au8Tone[1] = SNAKE_CELL_BODY_B;
    Snk_u8DirCur = DIR_RIGHT;  Snk_u8DirQLen = 0u;
    Snk_u16Score = 0u;
    Snk_u8FoodOn = 0u;                        /* NEW - kill the previous game's
                                                      food BEFORE the repaint, or
                                                      RepaintAll repaints it as a
                                                      ghost beside the new food   */
    Snk_u16TickMs = Snk_stCfg.TickBase;
    Snk_u32TickLast = Copy_u32Now;  Snk_u32PulseLast = Copy_u32Now;
    Snk_voidRepaintAll();
    Snk_voidSpawnFood();
    Snk_eState = SNK_RUN;
}

/* Death: freeze the world (ticks AND the food pulse stop - the pulse
 * is gated on SNK_RUN in the main loop), start the blink counter.  */
static void Snk_voidDie(u32 Copy_u32Now)
{
    (void)HAUDIO_enumPlaySting(&Snk_stCrashSting);
    Snk_eState = SNK_DYING;
    Snk_u32DeathT0 = Copy_u32Now;
    Snk_u8DeathPhase = 0u;
}

/* ------------------------------------------------------------------ *
 *  THE TICK - one step of the world. Read in order; every section
 *  is the tested v1.0 logic, commented:
 *    1. pop one queued direction (re-validated vs actual motion -
 *       belt-and-braces; the push-time checks already held)
 *    2. compute the new head; wall rule: EASY wraps (two modulos),
 *       NORMAL/HARD kills (the one-branch difficulty rule)
 *    3. food check -> growth decision
 *    4. self-collision with the tail-vacating exception (exception
 *       granted ONLY when not growing - a growing tick keeps the
 *       tail in place, so its cell is lethal)
 *    5. old head becomes BODY - tone assigned NOW, at the cell's
 *       birth, and stored; never recomputed (anti-strobe law)
 *    6. ring advances; new head painted (the image with eyes)
 *    7. growth: score, speed ramp (clamped to the floor), new food;
 *       otherwise the tail cell is erased (it just left the body)   */
static void Snk_voidTick(void)
{
    s16 Local_s16NewCol = (s16)Snk_Au8Col[Snk_u16Head];
    s16 Local_s16NewRow = (s16)Snk_Au8Row[Snk_u16Head];
    u8  Local_u8Grow, Local_u8Tone;
    u16 Local_u16TailSlot, Local_u16OldHead = Snk_u16Head;

    if (Snk_u8DirQLen != 0u)
    {
        u8 Local_u8D = Snk_Au8DirQ[0];
        Snk_Au8DirQ[0] = Snk_Au8DirQ[1];
        Snk_u8DirQLen--;
        if ((Local_u8D != Snk_u8DirCur) &&
            (Local_u8D != (u8)(Snk_u8DirCur ^ 1u)))
        { Snk_u8DirCur = Local_u8D; }
    }

    switch (Snk_u8DirCur)
    {
    case DIR_UP:    Local_s16NewRow--; break;
    case DIR_DOWN:  Local_s16NewRow++; break;
    case DIR_LEFT:  Local_s16NewCol--; break;
    case DIR_RIGHT: Local_s16NewCol++; break;
    default: break;
    }

    if (Snk_stCfg.WallsHard != 0u)              /* solid walls: die   */
    {
        if ((Local_s16NewCol < 0) || (Local_s16NewCol >= (s16)SNAKE_FIELD_COLS) ||
            (Local_s16NewRow < 0) || (Local_s16NewRow >= (s16)SNAKE_FIELD_ROWS))
        { Snk_voidDie(MSYSTICK_GetElapsed_ms()); return; }
    }
    else                                          /* EASY: wrap around  */
    {
        Local_s16NewCol = (s16)(((Local_s16NewCol % (s16)SNAKE_FIELD_COLS) + (s16)SNAKE_FIELD_COLS) % (s16)SNAKE_FIELD_COLS);
        Local_s16NewRow = (s16)(((Local_s16NewRow % (s16)SNAKE_FIELD_ROWS) + (s16)SNAKE_FIELD_ROWS) % (s16)SNAKE_FIELD_ROWS);
    }

    Local_u8Grow = ((Snk_u8FoodOn != 0u) &&
                    ((s16)Snk_u8FoodCol == Local_s16NewCol) &&
                    ((s16)Snk_u8FoodRow == Local_s16NewRow)) ? 1u : 0u;

    if (Snk_u8BodyAt(Local_s16NewCol, Local_s16NewRow, (u8)((Local_u8Grow != 0u) ? 0u : 1u)) != 0u)
    { Snk_voidDie(MSYSTICK_GetElapsed_ms()); return; }

    /* tone at birth - Len parity: flips only when a cell is born,
     * never en masse, so the stripes cannot strobe on growth        */
    Local_u8Tone = (Snk_u16Len & 1u) ? SNAKE_CELL_BODY_A : SNAKE_CELL_BODY_B;
    Snk_Au8Tone[Local_u16OldHead] = Local_u8Tone;
    SNAKE_voidPaintCell(Snk_Au8Col[Local_u16OldHead], Snk_Au8Row[Local_u16OldHead], Local_u8Tone);

    Snk_u16Head = (u16)((Snk_u16Head + 1u) % SNAKE_BODY_MAX);
    Snk_Au8Col[Snk_u16Head] = (u8)Local_s16NewCol;
    Snk_Au8Row[Snk_u16Head] = (u8)Local_s16NewRow;
    SNAKE_voidPaintCell((u8)Local_s16NewCol, (u8)Local_s16NewRow, SNAKE_CELL_HEAD);

    if (Local_u8Grow != 0u)
    {
        Snk_u16Len++;
        Snk_u16Score++;
        SNAKE_voidPaintScore(Snk_u16Score);          /* same-rect, no erase */
        Snk_u16TickMs = (u16)(Snk_stCfg.TickBase - (Snk_stCfg.Speedup * (Snk_u16Len - SNAKE_CFG_START_LEN)));
        if (Snk_u16TickMs < Snk_stCfg.TickMin) { Snk_u16TickMs = Snk_stCfg.TickMin; }
        Snk_voidSpawnFood();
    }
    else
    {
    	/* the departing cell is ONE SLOT BEHIND the new tail:
    	 * window = Head..Head-(Len-1); departed = Head-Len.
    	 * (Rev 1 erased Head-(Len-1) - the LIVE tail: stale trail bug) */
    	Local_u16TailSlot = (u16)((Snk_u16Head + SNAKE_BODY_MAX - Snk_u16Len) % SNAKE_BODY_MAX);
    	SNAKE_voidPaintCell(Snk_Au8Col[Local_u16TailSlot],
    			Snk_Au8Row[Local_u16TailSlot], SNAKE_CELL_EMPTY);
    }
}

/* ---- the exit seam (hub delta #4) -------------------------------- */

/* Overlay panel: solid backdrop + 2x title (<=7 chars) + 1x info line
 * (<=14 chars), both centered. The backdrop is the legibility fix -
 * text drawn directly over live cells had pixels bleeding through and
 * lines wider than the screen got clipped ("START RESUME O").       */
static void Snk_voidShowOverlay(const char *Copy_pchTitle,
                                const char *Copy_pchInfo, u16 Copy_u16Color)
{
    u8  Local_u8Len = 0u;
    u16 Local_u16X;

    HTFT_voidFillRect(8u, 62u, 112u, 38u, SNAKE_CFG_COLOR_BG);

    while (Copy_pchTitle[Local_u8Len] != '\0') { Local_u8Len++; }
    Local_u16X = (u16)((128u - (16u * (u16)Local_u8Len)) / 2u);
    HTFT_voidDrawString2x(Local_u16X, 66u, Copy_pchTitle, Copy_u16Color, SNAKE_CFG_COLOR_BG);

    Local_u8Len = 0u;
    while (Copy_pchInfo[Local_u8Len] != '\0') { Local_u8Len++; }
    Local_u16X = (u16)((128u - (8u * (u16)Local_u8Len)) / 2u);
    HTFT_voidDrawString(Local_u16X, 86u, Copy_pchInfo, SNAKE_CFG_COLOR_BG, SNAKE_CFG_COLOR_BORDER);
}

/* Open the exit-confirm overlay. ExitPrev remembers whether we came
 * from RUN or PAUSE - the resume path must land the player back in
 * the exact state they left. The timer field is reused (DeathT0):
 * one u32, two phases, never simultaneous.                          */
static void Snk_voidAskExit(u32 Copy_u32Now)
{
    Snk_u8ExitPrev = (u8)((Snk_eState == SNK_PAUSE) ? 1u : 0u);
    Snk_eState = SNK_EXIT_ASK;
    Snk_u32DeathT0 = Copy_u32Now;              /* reused as ask timer */
    Snk_voidShowOverlay("EXIT?", "0:EXIT >:STAY", SNAKE_CFG_COLOR_FOOD);
}

/* Leave the exit-ask state WITHOUT exiting: regenerate the world
 * (removes the overlay - the screen-is-cache law), then re-show the
 * pause overlay if that is where we came from. Timers rebase so no
 * burst of ticks fires for the time spent in the ask window.        */
static void Snk_voidResumeFromAsk(void)
{
    Snk_voidRepaintAll();
    if (Snk_u8ExitPrev != 0u)
    {
        Snk_eState = SNK_PAUSE;
        Snk_voidShowOverlay("PAUSED", ">:RESUME 0:NEW", SNAKE_CFG_COLOR_HUD_SCORE);
    }
    else { Snk_eState = SNK_RUN; }
    Snk_u32TickLast = MSYSTICK_GetElapsed_ms();
    Snk_u32PulseLast = Snk_u32TickLast;
}



/* ---- public contract ---------------------------------------------- */

/* Hub calls this before EVERY Run: map the store's difficulty into
 * the runtime config. NORMAL (the default branch) reads the
 * SNAKE_config.h values themselves - the compile-time preset stays
 * the definition of NORMAL, exactly as documented in the config.    */
void APP_SNAKE_enumInit(void)
{
    switch (APP_STORE_u8GetSetting(APP_STORE_SETTING_SNAKE_LEVEL))
    {
    case 0u: Snk_stCfg.TickBase = 320u; Snk_stCfg.TickMin = 200u; Snk_stCfg.WallsHard = 0u; break;
    case 2u: Snk_stCfg.TickBase = 250u; Snk_stCfg.TickMin = 90u;  Snk_stCfg.WallsHard = 1u; break;
    default: Snk_stCfg.TickBase = SNAKE_CFG_TICK_MS_BASE;
             Snk_stCfg.TickMin = SNAKE_CFG_TICK_MS_MIN;
             Snk_stCfg.WallsHard = SNAKE_CFG_WALLS_HARD; break;
    }
    Snk_stCfg.Speedup = SNAKE_CFG_SPEEDUP_PER_FOOD;
}

/* THE game. Enters straight into a fresh game (hub delta #1: no
 * boot screen - the menu selected this game), runs the 4-state FSM
 * until death completes (GAMEOVER) or the player exits (EXITED).   */
GameResult_t APP_SNAKE_enumRun(void)
{
    /* Hub delta #3: seed from menu-navigation time - the human
     * entropy equivalent of the old first-CONFIRM press.            */
    Snk_u32RandState ^= MSYSTICK_GetElapsed_ms();
    Snk_u32RandState = (Snk_u32RandState * 1103515245u) + 12345u;
    Snk_voidNewGame(MSYSTICK_GetElapsed_ms());

    while (1)
    {
        u32 Local_u32Now = MSYSTICK_GetElapsed_ms();
        APP_INPUT_Event_t Local_stEvt;
        u8 Local_u8HaveEvt = (APP_INPUT_enumGetEvent(&Local_stEvt) == APP_INPUT_OK);

        if ((Snk_eState == SNK_RUN) &&
            (HAUDIO_enumGetState() == HAUDIO_STATE_IDLE))
        {
            (void)HAUDIO_enumPlaySong(&Snk_stTheme);
        }

        switch (Snk_eState)
        {
        case SNK_RUN:
            if (Local_u8HaveEvt != 0u)
            {
                switch (Local_stEvt.Button)
                {
                case BTN_UP:    case BTN_NUM_2: Snk_voidQueueDir(DIR_UP);    break;
                case BTN_DOWN:  case BTN_NUM_8: Snk_voidQueueDir(DIR_DOWN);  break;
                case BTN_LEFT:  case BTN_NUM_4: Snk_voidQueueDir(DIR_LEFT);  break;
                case BTN_RIGHT: case BTN_NUM_6: Snk_voidQueueDir(DIR_RIGHT); break;
                case BTN_START:
                    Snk_eState = SNK_PAUSE;
                    Snk_voidShowOverlay("PAUSED", ">:RESUME 0:NEW", SNAKE_CFG_COLOR_HUD_SCORE);
                    break;
                case BTN_BACK:                 /* open the exit seam   */
                    if (Local_stEvt.IsRepeat == 0u) { Snk_voidAskExit(Local_u32Now); }
                    break;
                default: break;   /* D-pad repeats ARE the glide       */
                }
            }
            /* state may have changed above (pause/exit) - gate the
             * tick so a just-paused game does not step once more    */
            if (Snk_eState == SNK_RUN)
            {
                if ((Local_u32Now - Snk_u32TickLast) >= Snk_u16TickMs)
                { Snk_u32TickLast = Local_u32Now; Snk_voidTick(); }   /* skip-late */
            }
            break;

        case SNK_PAUSE:   /* frozen: no ticks, no pulse */
            if (Local_u8HaveEvt != 0u)
            {
                if (Local_stEvt.Button == BTN_START)
                { Snk_voidRepaintAll(); Snk_eState = SNK_RUN;      /* overlay removed by repaint */
                  Snk_u32TickLast = Local_u32Now; Snk_u32PulseLast = Local_u32Now; }
                else if (Local_stEvt.Button == BTN_BACK)
                { if (Local_stEvt.IsRepeat == 0u) { Snk_voidAskExit(Local_u32Now); } }
            }
            break;

        case SNK_EXIT_ASK:   /* input gated to the two answers */
            if (Local_u8HaveEvt != 0u)
            {
                if (Local_stEvt.Button == BTN_BACK)
                { return GAME_RESULT_EXITED; }               /* THE exit seam */
                if ((Local_stEvt.Button == BTN_START) || (Local_stEvt.Button == BTN_CONFIRM))
                { Snk_voidResumeFromAsk(); break; }
            }
            if ((Local_u32Now - Snk_u32DeathT0) >= SNAKE_EXIT_ASK_MS)
            { Snk_voidResumeFromAsk(); }                     /* timeout = resume */
            break;

        case SNK_DYING:
            /* 3 head blinks (same-cell timed toggle - the deliberate
             * flicker law), then the hold, then hand off. The world
             * is frozen: nothing else in this state.                */
            if (Snk_u8DeathPhase < (2u * SNAKE_DEATH_BLINKS))
            {
                if ((Local_u32Now - Snk_u32DeathT0) >= SNAKE_DEATH_STEP_MS)
                {
                    Snk_u32DeathT0 = Local_u32Now;
                    Snk_u8DeathPhase++;
                    SNAKE_voidPaintCell(Snk_Au8Col[Snk_u16Head], Snk_Au8Row[Snk_u16Head],
                            (Snk_u8DeathPhase & 1u) ? SNAKE_CELL_HEAD : SNAKE_CELL_EMPTY);
                }
            }
            else if ((Local_u32Now - Snk_u32DeathT0) >= SNAKE_DEATH_HOLD_MS)
            { return GAME_RESULT_GAMEOVER; }   /* hub owns the screen + score */
            break;

        default:
            break;
        }

        /* Food pulse - RUNNING-gated (freezes in PAUSE/DYING/EXIT_ASK),
         * config-gated (0 = off). Same-rect repaint, no erase.      */
        if ((Snk_eState == SNK_RUN) && (Snk_u8FoodOn != 0u) && (SNAKE_CFG_FOOD_PULSE_MS != 0u))
        {
            if ((Local_u32Now - Snk_u32PulseLast) >= SNAKE_CFG_FOOD_PULSE_MS)
            {
                Snk_u32PulseLast = Local_u32Now;
                Snk_u8FoodDim = (u8)!Snk_u8FoodDim;
                SNAKE_voidPaintCell(Snk_u8FoodCol, Snk_u8FoodRow,
                        (Snk_u8FoodDim != 0u) ? SNAKE_CELL_FOOD_DIM : SNAKE_CELL_FOOD);
            }
        }

        HAUDIO_voidTask();

        /* PC13 heartbeat: owned by the HUB's menu loop (HUB_menu.c).
         * It pauses during a game - Run owns the loop - and resumes
         * the moment this function returns. Documented, accepted.
         * To keep it alive during play, uncomment this block:
         *
         * { static u32 h = 0u; static u8 b = 0u;
         *   if ((Local_u32Now / 500u) != h) { h = Local_u32Now / 500u;
         *     b = (u8)!b;
         *     (void)MGPIO_SetPinValue(MGPIO_PORTC, MGPIO_PIN_13,
         *             (b != 0u) ? MGPIO_PIN_HIGH : MGPIO_PIN_LOW); } }
         * (and add the MGPIO include at the top)                     */
    }
}

u16 APP_SNAKE_u16GetScore(void) { return Snk_u16Score; }
