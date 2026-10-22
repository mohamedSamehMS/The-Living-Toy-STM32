/*
 * APP_TTT.c — Tic-Tac-Toe, THE MODULE: offline AI + online multiplayer.
 *  LOCATION: APP/ (flat). Replaces the pending-module stub — same
 *  contract, so the hub's dispatch row needs ZERO changes.
 *
 *  PROVENANCE: the merged-tree router's game half (main.c v1.3 Stage 2),
 *  ported behind the module contract per the link-side seat's
 *  integration map. The bodies are VERBATIM except where a marked
 *  PORT-CHANGE or PORT-FIX says otherwise. The complete port map:
 *
 *    1. WRAPPING: the router's loop becomes APP_TTT_enumRun() —
 *       Run-owns-the-loop (the hub ruling; the nesting is the design).
 *       Game_voidInitFail is RETIRED (the hub owns boot failure).
 *    2. INPUT: HNEC_REMOTE_enumGetEvent -> APP_INPUT_enumGetEvent —
 *       a 1:1 swap. The input blip arrives for free through the seam.
 *    3. BOOT (the mode menu): CH = play offline (unchanged);
 *       1 = online — NET_enumBringup() FIRST (the deferred link: the
 *       only place USART1 ever wakes), then EnterNetWait;
 *       BACK = THE EXIT SEAM, two-beat (the house law — an accidental
 *       press must not kill the game): "EXIT? 0 AGAIN", 5 s window.
 *       (The router's BACK-plays-offline is superseded by the hub's
 *       exit-seam law; the two-beat gate is the module-local shape.)
 *    4. SESSION LIFETIME: TTT_SESSION_enumInit() at Run() entry —
 *       per-launch state (File 2's contract); the RNG seeds from
 *       navigation time (the house entropy source).
 *    5. THE STREAK SCORE MODEL (the recorded recommendation, ruled):
 *       one Run = play until a LOSS or DRAW; the score = the session's
 *       win count (offline X wins + online match wins — OnlineEnd
 *       increments). An X win extends the session straight into the
 *       next game; an O win or a draw returns GAME_RESULT_GAMEOVER
 *       and the hub's GameOver screen carries the streak. Online play
 *       never produces GAMEOVER (the play-again cycle lives in the
 *       LOBBY); leaving online returns through the mode-menu seam as
 *       EXITED. The X/O/D counters reset per-Run.
 *    6. F-R1 REVIEW FIX (rides the port, the M7 precedent): the
 *       online EXIT_ASK resume polarity was inverted — ExitPrev==1
 *       (exit began on OUR turn) must resume ON_TURN, not ON_WAIT;
 *       the old mapping handed ON_TURN after a WAIT-exit (an
 *       out-of-turn placement bounces off the server) and ON_WAIT
 *       after a TURN-exit (30 s waiting for a move that cannot come,
 *       then a false FRIEND-LEFT escalation). Bench card row 2.6
 *       verifies the fix; if glass disagrees, the swap is one line.
 *    7. F-R2 REVIEW FIX: exit-resume fires a STATE chase — heals any
 *       event dropped during the ask window (an OPP_MOVE during
 *       EXIT_ASK falls to the state guards; the board resyncs).
 *    8. The module carries ZERO FreeRTOS knowledge — APP_INPUT, the
 *       session, and NET's wrapper API are plain calls; the seam did
 *       all the OS work. The heartbeat (500 ms) is module-owned (Run
 *       owns its beat).
 *
 *  PORT-FIX (post-bench, row-1.8's finding): THE OFFLINE EXIT SEAM.
 *    The v1.2 router had no exit because it was a STANDALONE WORLD —
 *    nothing to exit to; its BACK-during-play = restart was the
 *    substitute. Inside the hub, every game follows the house law:
 *    0 during play -> exit ask -> 0 = home. The port initially kept
 *    the router's restart verbatim (and the bench plan's row 1.8
 *    codified the gap — the owner's thumb found it in minutes). This
 *    fix adds the seam in the standard shape: two-beat gate from the
 *    HUMAN and AI states, cancel = resume in place (the AI think
 *    timer restarted fairly), confirm = EXITED to the hub. The v1.2
 *    mid-game restart is deliberately superseded; if playtesting
 *    later wants a quick-restart, it maps to another key (a UX
 *    ruling for that day, not a port decision).
 *
 *  Revision history:
 *    Stub     — the pending-module placeholder (proves the wiring)
 *    Port     — the router's game half, wrapped (Files 1-5 of 5)
 *    Port-FIX — + the offline exit seam (this file)
 */

#include "STD_TYPES.h"

#include <MCAL/SYSTICK/MSYSTICK_interface.h>   /* the shim serves the HUB task */
#include <MCAL/GPIO/MGPIO_interface.h>         /* the module's heartbeat (PC13) */
#include <HAL/TFT/HTFT_interface.h>

#include "APP/APP_TTT.h"
#include "APP/APP_INPUT.h"
#include "APP/TTT_renderer.h"
#include "APP/TTT_config.h"
#include "APP/TTT_session.h"
#include "APP/TTT_app.h"
#include "APP/NET_interface.h"      /* Bringup + the [L1] narration (M14 sites) */
#include "HAL/AUDIO/HAUDIO_interface.h"

/* ---- constants (router, verbatim) ---- */
#define GAME_PLAYER_X          1u
#define GAME_PLAYER_O          2u

/* PORT-CHANGE 5: the draw's hold before GAMEOVER (the child sees the
   DRAW screen; the hub's GameOver follows). */
#define TTT_END_HOLD_MS        3000u

static const u8 Game_AU8WinLines[8][3] =
{
    {0,1,2},{3,4,5},{6,7,8},{0,3,6},{1,4,7},{2,5,8},{0,4,8},{2,4,6}
};
static const SongNote_t Game_astTheme[] =
    { { 294u, 200u }, { 0u, 200u }, { 294u, 200u }, { 0u, 200u },
      { 349u, 200u }, { 330u, 200u }, { 294u, 200u }, { 262u, 200u },
      { 0u, 400u } };
static const Song_t Game_stTheme =
    { Game_astTheme, 9u, 1u, 4u };
static const SongNote_t Game_astLoseSting[] =
    { { 330u, 300u }, { 262u, 300u }, { 196u, 600u } };
static const Song_t Game_stLoseSting =
    { Game_astLoseSting, 3u, 0u, 0u };
static const SongNote_t Game_astWinSting[] =
    { { 262u, 150u }, { 330u, 150u }, { 392u, 150u },
      { 523u, 400u } };
static const Song_t Game_stWinSting =
    { Game_astWinSting, 4u, 0u, 0u };

/* ---- game statics (router, verbatim + the port's additions) ---- */
static u8  Game_Au8Board[9];
static u8  Game_u8MovesPlayed;
static u8  Game_u8CursorRow, Game_u8CursorCol;
static u8  Game_Au8XWins, Game_Au8OWins, Game_Au8Draws;
static u8  Game_Au8WinCells[3];
static u8  Game_u8TakenFlag;        /* M13: the in-game "TAKEN!" only */
static u8  Game_u8BlinkPhase;
static u32 Game_u32StateT0;         /* the GAME's timer (ON_WAIT/GRACE/EXIT_ASK) */
static u32 Game_u32LastBlinkMs;
static u32 Game_u32PollT0;          /* the GAME's poll timer (ON_GRACE) */
static u8  Game_u8MyPlayer, Game_u8OppPlayer;
static u8  Game_u8IAmHost;
static u8  Game_u8MovePending, Game_u8PendingCell;
static u8  Game_u8ExitPrev;
static u8  Game_u8StateForce;
static u16 Game_u16GraceMs;

/* PORT-CHANGE 5: the streak model's state */
static u16 Game_u16Score;           /* the session's win count            */
static u8  Game_u8LastWinner;       /* WIN_FLASH's verdict (X or O)       */
static u8  Game_u8GameOver;         /* the loop's GAMEOVER signal         */

/* PORT-CHANGE 3 + PORT-FIX: the two-beat exit gates. ExitAsk values:
     0 = none; 1 = from the mode menu; 2 = from offline play
   (the PORT-FIX: the offline in-game seam — the house law, which
   the v1.2 router lacked because it was a standalone world with
   nowhere to exit to; BACK-restart was its substitute for an exit.) */
static u8  Game_u8ExitAsk;
static u32 Game_u32ExitT0;
static u8  Game_u8OfflineExitFrom;   /* 0 = HUMAN, 1 = AI — cancel-resume target */
static u8  Game_u8ExitModule;        /* the loop's EXITED signal               */

static u32 Game_u32RandState = 12345u;
static u32 Game_u32Rand(void)
{ Game_u32RandState = (Game_u32RandState * 1103515245u) + 12345u;
  return (Game_u32RandState >> 16u); }

static void Game_voidToggleHeartbeat(void)
{
    static u8 Local_u8State = 0;
    Local_u8State = (u8)!Local_u8State;
    (void)MGPIO_SetPinValue(MGPIO_PORTC, MGPIO_PIN_13,
            (Local_u8State != 0u) ? MGPIO_PIN_HIGH : MGPIO_PIN_LOW);
}

/* ---- cursor (v1.2, verbatim; OnlineMode via the API, M12) ---- */
static u8 Game_u8CursorIdx(void)
{ return (u8)((Game_u8CursorRow * 3u) + Game_u8CursorCol); }

static void Game_voidMoveCursor(s8 Copy_s8Dr, s8 Copy_s8Dc)
{
    u8 Local_u8OldIdx = Game_u8CursorIdx();
    TTT_CellRole_t Local_eOldRole;
    if (Game_Au8Board[Local_u8OldIdx] == GAME_PLAYER_X)      { Local_eOldRole = TTT_CELL_X; }
    else if (Game_Au8Board[Local_u8OldIdx] == GAME_PLAYER_O) { Local_eOldRole = TTT_CELL_O; }
    else                                                     { Local_eOldRole = TTT_CELL_EMPTY; }
    TTT_voidPaintCell(Local_u8OldIdx, Local_eOldRole);
    Game_u8CursorRow = (u8)((Game_u8CursorRow + Copy_s8Dr + 3) % 3);
    Game_u8CursorCol = (u8)((Game_u8CursorCol + Copy_s8Dc + 3) % 3);
    TTT_voidPaintCursor(Game_u8CursorIdx());
    if (Game_u8TakenFlag == 1u)
    { Game_u8TakenFlag = 0u;
      TTT_voidPaintStatus(TTT_SESSION_u8OnlineMode() ? "YOUR MOVE" : "X: YOUR MOVE",
                          TTT_SESSION_u8OnlineMode() ?
                          ((Game_u8MyPlayer == GAME_PLAYER_X) ? TTT_CFG_COLOR_X
                                                              : TTT_CFG_COLOR_O)
                          : TTT_CFG_COLOR_X); }
}

static u8 Game_u8HandleCursorMove(VirtualButton_t Copy_eButton)
{
    s8 Local_s8Dr = 0, Local_s8Dc = 0;
    u8 Local_u8IsMove = 1u;
    switch (Copy_eButton)
    {
    case BTN_UP:    Local_s8Dr = -1; Local_s8Dc =  0; break;
    case BTN_DOWN:  Local_s8Dr =  1; Local_s8Dc =  0; break;
    case BTN_LEFT:  Local_s8Dc = -1; Local_s8Dr =  0; break;
    case BTN_RIGHT: Local_s8Dc =  1; Local_s8Dr =  0; break;
    case BTN_NUM_2: Local_s8Dr = -1; Local_s8Dc =  0; break;
    case BTN_NUM_8: Local_s8Dr =  1; Local_s8Dc =  0; break;
    case BTN_NUM_4: Local_s8Dc = -1; Local_s8Dr =  0; break;
    case BTN_NUM_6: Local_s8Dc =  1; Local_s8Dr =  0; break;
    case BTN_NUM_5:
        Local_s8Dr = (s8)((s32)1 - (s32)Game_u8CursorRow);
        Local_s8Dc = (s8)((s32)1 - (s32)Game_u8CursorCol);
        break;
    default: Local_u8IsMove = 0u; break;
    }
    if (Local_u8IsMove != 0u) { Game_voidMoveCursor(Local_s8Dr, Local_s8Dc); }
    return Local_u8IsMove;
}

/* ---- AI (v1.2, verbatim) ---- */
static u8 Game_u8FindLineSpot(u8 Copy_u8Player)
{
    for (u8 L = 0u; L < 8u; L++)
    { u8 cnt = 0u, emp = 9u;
      for (u8 i = 0u; i < 3u; i++)
      { u8 v = Game_Au8Board[Game_AU8WinLines[L][i]];
        if (v == Copy_u8Player) { cnt++; }
        else if (v == 0u) { emp = Game_AU8WinLines[L][i]; } }
      if ((cnt == 2u) && (emp != 9u)) { return emp; } }
    return 9u;
}
static u8 Game_u8RulesMove(void)
{
    static const u8 c[4] = {0u, 2u, 6u, 8u};
    u8 cell;
    if ((cell = Game_u8FindLineSpot(GAME_PLAYER_O)) != 9u) { return cell; }
    if ((cell = Game_u8FindLineSpot(GAME_PLAYER_X)) != 9u) { return cell; }
    if (Game_Au8Board[4] == 0u) { return 4u; }
    for (u8 i = 0u; i < 4u; i++) { if (Game_Au8Board[c[i]] == 0u) { return c[i]; } }
    for (cell = 0u; cell < 9u; cell++) { if (Game_Au8Board[cell] == 0u) { return cell; } }
    return 9u;
}
static u8 Game_u8AiChoose(void)
{
    u8 cnt = 0u, pick;
    if ((Game_u32Rand() % 100u) < TTT_CFG_AI_SKILL_PCT) { return Game_u8RulesMove(); }
    for (u8 i = 0u; i < 9u; i++) { if (Game_Au8Board[i] == 0u) { cnt++; } }
    if (cnt == 0u) { return 9u; }
    pick = (u8)(Game_u32Rand() % cnt);
    for (u8 i = 0u; i < 9u; i++)
    { if (Game_Au8Board[i] == 0u) { if (pick == 0u) { return i; } pick--; } }
    return 9u;
}

static u8 Game_u8CheckWin(void)
{
    for (u8 L = 0u; L < 8u; L++)
    { u8 v0 = Game_Au8Board[Game_AU8WinLines[L][0]];
      u8 v1 = Game_Au8Board[Game_AU8WinLines[L][1]];
      u8 v2 = Game_Au8Board[Game_AU8WinLines[L][2]];
      if ((v0 != 0u) && (v0 == v1) && (v1 == v2))
      { for (u8 i = 0u; i < 3u; i++) { Game_Au8WinCells[i] = Game_AU8WinLines[L][i]; }
        return v0; } }
    return 0u;
}

/* ---- place: the ONE mode-gated transition point (verbatim + the
 *      streak model's marked additions) ---- */
static void Game_voidPlace(u8 Copy_u8Idx, u8 Copy_u8Player, u32 Copy_u32Now)
{
    u8 Local_u8Winner;
    u16 Local_u16Color = (Copy_u8Player == GAME_PLAYER_X) ? TTT_CFG_COLOR_X
                                                          : TTT_CFG_COLOR_O;
    Game_Au8Board[Copy_u8Idx] = Copy_u8Player;
    Game_u8MovesPlayed++;
    TTT_voidPaintCell(Copy_u8Idx,
            (Copy_u8Player == GAME_PLAYER_X) ? TTT_CELL_X : TTT_CELL_O);

    Local_u8Winner = Game_u8CheckWin();
    if (Local_u8Winner != 0u)
    {
        Game_u8LastWinner = Local_u8Winner;   /* PORT-CHANGE 5: the verdict
                                                 feeds WIN_FLASH's completion */
        if (TTT_SESSION_u8OnlineMode() == 0u)
        {
            if (Local_u8Winner == GAME_PLAYER_X) { Game_Au8XWins++; }
            else                                 { Game_Au8OWins++; }
            TTT_voidPaintScores(Game_Au8XWins, Game_Au8OWins, Game_Au8Draws);
            if (Local_u8Winner == GAME_PLAYER_X)
            {
                (void)HAUDIO_enumPlaySting(&Game_stWinSting);
            }
            else
            {
                (void)HAUDIO_enumPlaySting(&Game_stLoseSting);
            }
        }
        TTT_voidPaintWinLine(Game_Au8WinCells[0], Game_Au8WinCells[2],
                             TTT_CFG_COLOR_WINLINE);
        TTT_voidPaintStatus(TTT_SESSION_u8OnlineMode()
                            ? ((Local_u8Winner == Game_u8MyPlayer) ? "YOU WIN!" : "FRIEND WINS")
                            : ((Local_u8Winner == GAME_PLAYER_X) ? "X WINS!" : "O WINS!"),
                            Local_u16Color);
        Game_eState = GAME_ST_WIN_FLASH;
        Game_u8BlinkPhase = 0u;
        Game_u32LastBlinkMs = Copy_u32Now;
        return;
    }
    if (Game_u8MovesPlayed >= 9u)
    {
        (void)HAUDIO_enumStop();
        if (TTT_SESSION_u8OnlineMode() == 0u)
        { Game_Au8Draws++;
          TTT_voidPaintScores(Game_Au8XWins, Game_Au8OWins, Game_Au8Draws);
          Game_u32StateT0 = Copy_u32Now;      /* PORT-CHANGE 5: the draw hold's timer */
        }
        TTT_voidPaintStatus("DRAW", TTT_CFG_COLOR_TEXT);
        if (TTT_SESSION_u8OnlineMode() != 0u)
        { HTFT_voidDrawString(0u, 8u, "0:LEAVE", TTT_CFG_COLOR_SCORE_D,
                              TTT_CFG_COLOR_BG); }
        Game_eState = (TTT_SESSION_u8OnlineMode() != 0u) ? GAME_ST_ON_END
                                                         : GAME_ST_GAME_END;
        return;
    }
    if (TTT_SESSION_u8OnlineMode() == 0u)
    {
        if (Copy_u8Player == GAME_PLAYER_X)
        { Game_eState = GAME_ST_AI; Game_u32StateT0 = Copy_u32Now;
          TTT_voidPaintStatus("O THINKING...", TTT_CFG_COLOR_O); }
        else
        { Game_eState = GAME_ST_HUMAN;
          TTT_voidPaintStatus("X: YOUR MOVE", TTT_CFG_COLOR_X);
          TTT_voidPaintCursor(Game_u8CursorIdx()); }
    }
    else
    {
        if (Copy_u8Player == Game_u8MyPlayer)
        { Game_eState = GAME_ST_ON_WAIT; Game_u32StateT0 = Copy_u32Now;
          TTT_voidPaintStatus("WAITING FRIEND..", TTT_CFG_COLOR_SCORE_D); }
        else
        { Game_eState = GAME_ST_ON_TURN;
          TTT_voidPaintStatus("YOUR MOVE", Local_u16Color);
          TTT_voidPaintCursor(Game_u8CursorIdx()); }
    }
}

/* ---- offline restart (v1.2, verbatim) ---- */
static void Game_voidNewGame(void)
{
    TTT_voidPaintBoard();
    for (u8 i = 0u; i < 9u; i++) { Game_Au8Board[i] = 0u; }
    Game_u8MovesPlayed = 0u; Game_u8TakenFlag = 0u;
    Game_u8CursorRow = 1u; Game_u8CursorCol = 1u;
    TTT_voidPaintCursor(4u);
    TTT_voidPaintScores(Game_Au8XWins, Game_Au8OWins, Game_Au8Draws);
    TTT_voidPaintStatus("X: YOUR MOVE", TTT_CFG_COLOR_X);
    Game_eState = GAME_ST_HUMAN;
}

/* PORT-FIX: cancel-resume for the offline exit gate — the board is
   untouched (only the status line changed); repaint the prompt and,
   for the AI state, restart its think timer FAIRLY (the gate must
   not eat the child's thinking time). */
static void Game_voidResumeOffline(void)
{
    if (Game_u8OfflineExitFrom == 1u)
    {
        Game_u32StateT0 = MSYSTICK_GetElapsed_ms();
        TTT_voidPaintStatus("O THINKING...", TTT_CFG_COLOR_O);
    }
    else
    {
        TTT_voidPaintStatus("X: YOUR MOVE", TTT_CFG_COLOR_X);
        TTT_voidPaintCursor(Game_u8CursorIdx());
    }
}

/* ---- the mode menu (BOOT's paint — verbatim) ---- */
static void Game_voidPaintBootScreen(void)
{
    TTT_SESSION_voidSetOnlineMode(0u);
    TTT_voidPaintBoard();
    TTT_voidPaintScores(0u, 0u, 0u);
    Game_u8CursorRow = 1u; Game_u8CursorCol = 1u;
    TTT_voidPaintCursor(4u);
    TTT_voidPaintStatus("CH:PLAY 1:ONLINE", TTT_CFG_COLOR_TEXT);
    Game_eState = GAME_ST_BOOT;
}

/* ---- online helpers (verbatim + the score increment) ---- */
static void Game_voidStartOnline(u8 Copy_u8MyPlayerIsX, u8 Copy_u8MyTurn, u32 now)
{
    TTT_SESSION_voidSetOnlineMode(1u);
    Game_u8MyPlayer  = Copy_u8MyPlayerIsX ? GAME_PLAYER_X : GAME_PLAYER_O;
    Game_u8OppPlayer = Copy_u8MyPlayerIsX ? GAME_PLAYER_O : GAME_PLAYER_X;
    Game_u8IAmHost = TTT_SESSION_u8IsHost();
    Game_u8MovePending = 0u;
    for (u8 i = 0u; i < 9u; i++) { Game_Au8Board[i] = 0u; }
    Game_u8MovesPlayed = 0u; Game_u8TakenFlag = 0u;
    TTT_voidPaintBoard();
    Game_u8CursorRow = 1u; Game_u8CursorCol = 1u;
    if (Copy_u8MyTurn)
    { Game_eState = GAME_ST_ON_TURN;
      TTT_voidPaintStatus("YOUR MOVE",
              Copy_u8MyPlayerIsX ? TTT_CFG_COLOR_X : TTT_CFG_COLOR_O);
      TTT_voidPaintCursor(4u); }
    else
    { Game_eState = GAME_ST_ON_WAIT; Game_u32StateT0 = now;
      TTT_voidPaintStatus("WAITING FRIEND..", TTT_CFG_COLOR_SCORE_D); }
}

static void Game_voidRollbackMove(const char *Copy_chReason)
{
    const char *Local_pch;
    if ((Game_eState != GAME_ST_ON_TURN) || (Game_u8MovePending == 0u))
    { (void)TTT_SESSION_enumRequestState(); return; }
    Game_Au8Board[Game_u8PendingCell] = 0u;
    Game_u8MovesPlayed--;
    TTT_voidPaintCell(Game_u8PendingCell, TTT_CELL_EMPTY);
    TTT_voidPaintCursor(Game_u8CursorIdx());
    Game_u8MovePending = 0u;
    if      (Copy_chReason[0] == 'N') { Local_pch = "WAIT YOUR TURN"; }
    else if (Copy_chReason[0] == 'C') { Local_pch = "TAKEN!"; }
    else if (Copy_chReason[0] == 'G') { Local_pch = "GAME OVER"; }
    else                              { Local_pch = "TRY AGAIN"; }
    TTT_voidPaintStatus(Local_pch, TTT_CFG_COLOR_TAKEN);
    Game_u8TakenFlag = 1u;
    Game_u8StateForce = 1u;
    (void)TTT_SESSION_enumRequestState();
}

static void Game_voidOnlineEnd(u8 Copy_u8Winner, u32 now)
{
    (void)now;
    if ((Copy_u8Winner == 'd') || (Copy_u8Winner == 0u))
    { TTT_voidPaintStatus("DRAW", TTT_CFG_COLOR_TEXT); }
    else if ((Copy_u8Winner == 'h') == (Game_u8IAmHost != 0u))
    { TTT_voidPaintStatus("YOU WIN!", Game_u8MyPlayer == GAME_PLAYER_X ?
      TTT_CFG_COLOR_X : TTT_CFG_COLOR_O);
      (void)HAUDIO_enumPlaySting(&Game_stWinSting);
      Game_u16Score++;                      /* PORT-CHANGE 5: online wins count
                                               in the session's streak */
    }
    else
    {
      (void)HAUDIO_enumPlaySting(&Game_stLoseSting);
      TTT_voidPaintStatus("FRIEND WINS", Game_u8OppPlayer == GAME_PLAYER_X ?
      TTT_CFG_COLOR_X : TTT_CFG_COLOR_O); }
    HTFT_voidDrawString(0u, 8u, "0:LEAVE", TTT_CFG_COLOR_SCORE_D,
                        TTT_CFG_COLOR_BG);
    NET_voidNarrateCounters();      /* M14: NET owns the [L1] line */
    Game_eState = GAME_ST_ON_END;
}

static void Game_voidPaintEventLine(const TTS_Event_t *Copy_pstEvent)
{
    if ((Copy_pstEvent->Line[0] >= '0') && (Copy_pstEvent->Line[0] <= '8') &&
        (Copy_pstEvent->Line[1] >= '0') && (Copy_pstEvent->Line[1] <= '8') &&
        (Copy_pstEvent->Line[2] >= '0') && (Copy_pstEvent->Line[2] <= '8'))
    {
        TTT_voidPaintWinLine((u8)(Copy_pstEvent->Line[0] - '0'),
                             (u8)(Copy_pstEvent->Line[2] - '0'),
                             TTT_CFG_COLOR_WINLINE);
    }
}

static void Game_voidApplyState(u32 now, u8 Copy_u8Force)
{
    const TTS_State_t *L = TTT_SESSION_pcstGetState();
    u8 Local_u8MyIsX;
    if (L->Status == TTS_STATUS_UNKNOWN) { return; }

    if (Copy_u8Force == 0u)
    {
        if ((Game_eState == GAME_ST_HOST_WAIT) && (L->Status == TTS_STATUS_WAITING))
        { return; }
        if (L->Status == TTS_STATUS_ACTIVE)
        {
            if (((Game_eState == GAME_ST_ON_WAIT)  && (L->MyTurn == 0u)) ||
                ((Game_eState == GAME_ST_ON_GRACE) && (L->MyTurn == 0u)) ||
                ((Game_eState == GAME_ST_ON_TURN)  && (L->MyTurn != 0u)))
            { return; }
        }
    }

    TTT_SESSION_voidSetOnlineMode(1u);
    Local_u8MyIsX = (L->MyMark == 'X');
    Game_u8MyPlayer  = Local_u8MyIsX ? GAME_PLAYER_X : GAME_PLAYER_O;
    Game_u8OppPlayer = Local_u8MyIsX ? GAME_PLAYER_O : GAME_PLAYER_X;
    Game_u8IAmHost   = TTT_SESSION_u8IsHost();

    TTT_voidPaintBoard();
    Game_u8MovesPlayed = 0u; Game_u8TakenFlag = 0u; Game_u8MovePending = 0u;
    for (u8 i = 0u; i < 9u; i++)
    {
        if      (L->Board[i] == 'X')
        { Game_Au8Board[i] = GAME_PLAYER_X; TTT_voidPaintCell(i, TTT_CELL_X);
          Game_u8MovesPlayed++; }
        else if (L->Board[i] == 'O')
        { Game_Au8Board[i] = GAME_PLAYER_O; TTT_voidPaintCell(i, TTT_CELL_O);
          Game_u8MovesPlayed++; }
        else { Game_Au8Board[i] = 0u; }
    }
    Game_u8CursorRow = 1u; Game_u8CursorCol = 1u;

    if (L->Status == TTS_STATUS_WAITING)
    { TTT_SESSION_voidGoHostWait(now); }        /* the session's screen + poll timer */
    else if (L->Status == TTS_STATUS_FINISHED)
    { Game_voidOnlineEnd(L->Winner, now); }
    else
    {
        if (L->MyTurn)
        { Game_eState = GAME_ST_ON_TURN;
          TTT_voidPaintStatus("YOUR MOVE",
                  Local_u8MyIsX ? TTT_CFG_COLOR_X : TTT_CFG_COLOR_O);
          TTT_voidPaintCursor(4u); }
        else
        { Game_eState = GAME_ST_ON_WAIT; Game_u32StateT0 = now;
          TTT_voidPaintStatus("WAITING FRIEND..", TTT_CFG_COLOR_SCORE_D); }
    }
}

static void Game_voidApplyOppMove(u8 Copy_u8Cell, u8 Copy_u8Mark, u32 now)
{
    u8 Local_u8Player;
    if ((Game_eState != GAME_ST_ON_TURN) && (Game_eState != GAME_ST_ON_WAIT) &&
        (Game_eState != GAME_ST_ON_GRACE))
    { return; }
    if ((Copy_u8Cell > 8u) || (Game_Au8Board[Copy_u8Cell] != 0u) ||
        ((Copy_u8Mark != 'X') && (Copy_u8Mark != 'O')) ||
        (TTT_SESSION_u8OnlineMode() == 0u))
    { (void)TTT_SESSION_enumRequestState(); return; }
    Local_u8Player = (Copy_u8Mark == 'X') ? GAME_PLAYER_X : GAME_PLAYER_O;
    if (Local_u8Player != Game_u8OppPlayer)
    { (void)TTT_SESSION_enumRequestState(); return; }
    Game_voidPlace(Copy_u8Cell, Local_u8Player, now);
}

/* ---- THE GAME HALF of the dispatcher (M11; verbatim) ---- */
static void Game_voidSessionEvent(TTS_Event_t *Copy_pstEvent, u32 Copy_u32Now)
{
    switch (Copy_pstEvent->Type)
    {
    case TTS_EVT_LINK_LOST:
        /* reserved (never fires in v1); the old branch's retry clear
           is provably safe to drop — see the router's banner */
        if (TTT_SESSION_u8OnlineMode() != 0u)
        { TTT_SESSION_voidSetOnlineMode(0u);
          TTT_SESSION_voidClearBusy();
          TTT_voidPaintStatus("LINK LOST", TTT_CFG_COLOR_TAKEN);
          HTFT_voidDrawString(0u, 8u, "0:BACK", TTT_CFG_COLOR_SCORE_D,
                              TTT_CFG_COLOR_BG);
          Game_eState = GAME_ST_ON_END; }
        break;

    case TTS_EVT_ROOM_JOINED:
        Game_voidStartOnline((Copy_pstEvent->Mark == 'X'),
                             Copy_pstEvent->MyTurn, Copy_u32Now);
        break;

    case TTS_EVT_OPP_JOINED:
        if (Game_eState == GAME_ST_HOST_WAIT)
        { Game_voidStartOnline(1u, Copy_pstEvent->MyTurn, Copy_u32Now); }
        break;

    case TTS_EVT_OPP_MOVE:
        Game_voidApplyOppMove(Copy_pstEvent->Cell, Copy_pstEvent->Mark,
                              Copy_u32Now);
        break;

    case TTS_EVT_MOVE_OK:
        Game_u8MovePending = 0u;
        break;

    case TTS_EVT_MOVE_REJECTED:
        Game_voidRollbackMove(Copy_pstEvent->Reason);
        break;

    case TTS_EVT_OPP_LEFT:
        if (Game_eState == GAME_ST_ON_WAIT)
        { Game_eState = GAME_ST_ON_GRACE;
          Game_u32StateT0 = Copy_u32Now; Game_u32PollT0 = Copy_u32Now;
          Game_u16GraceMs =
              ((Copy_pstEvent->GraceS != 0u) && (Copy_pstEvent->GraceS <= 60u))
              ? (u16)(Copy_pstEvent->GraceS * 1000u) : TTT_ONLINE_REJOIN_MS;
          TTT_voidPaintStatus("FRIEND LEFT?", TTT_CFG_COLOR_TAKEN); }
        break;

    case TTS_EVT_MATCH_ENDED:
        if ((TTT_SESSION_u8OnlineMode() != 0u) &&
            (Game_eState != GAME_ST_WIN_FLASH) && (Game_eState != GAME_ST_ON_END))
        { Game_voidPaintEventLine(Copy_pstEvent);
          Game_voidOnlineEnd(Copy_pstEvent->Winner, Copy_u32Now); }
        break;

    case TTS_EVT_STATE:
        if ((TTT_SESSION_u8Busy() != 0u) ||
            (Game_eState == GAME_ST_HOST_WAIT) ||
            (Game_eState == GAME_ST_ON_TURN)  ||
            (Game_eState == GAME_ST_ON_WAIT)  ||
            (Game_eState == GAME_ST_ON_GRACE))
        {
            u8 Local_u8Force = Game_u8StateForce;
            Game_u8StateForce = 0u;
            TTT_SESSION_voidClearBusy();
            Game_voidApplyState(Copy_u32Now, Local_u8Force);
        }
        break;

    case TTS_EVT_ROOM_GONE:
        /* the session's consume already did the busy/retry clears
           and the JOIN_ENTRY message; the game states remain */
        if ((Game_eState == GAME_ST_ON_GRACE) || (Game_eState == GAME_ST_HOST_WAIT) ||
            (Game_eState == GAME_ST_ON_WAIT)  || (Game_eState == GAME_ST_ON_TURN))
        { TTT_SESSION_voidSetOnlineMode(0u); TTT_SESSION_voidGoLobby(); }
        break;

    case TTS_EVT_CMD_TIMEOUT:
        /* the net-state branches are session-consumed; the play
           context is all that reaches here */
        TTT_voidPaintStatus("NET SLOW..", TTT_CFG_COLOR_TAKEN);
        break;

    case TTS_EVT_RESP_ERR:
        TTT_voidPaintStatus("NET ERROR", TTT_CFG_COLOR_TAKEN);
        break;

    /* REV 5 (B4): the REJOINED grace trigger — the STATE chase is the
     * canonical resync; MOVE-during-grace stays belt-and-braces. */
    case TTS_EVT_REJOINED:
        if (Game_eState == GAME_ST_ON_GRACE)
        { (void)TTT_SESSION_enumRequestState(); }
        break;

    case TTS_EVT_REMATCH_OFFERED:
    case TTS_EVT_REMATCH_STARTED:
    default:
        break;
    }
}

/* ---- THE GAME STATES (verbatim bodies; the M7 A1 FIX retained;
 *      F-R1/F-R2 in EXIT_ASK; the streak model in WIN_FLASH/GAME_END;
 *      the PORT-FIX offline exit gate at the top) ---- */
static void Game_voidRunGameStates(u32 Copy_u32Now, u8 Copy_u8HaveIr,
                                   VirtualButton_t Copy_eButton)
{
    /* PORT-FIX: THE OFFLINE EXIT GATE — while the ask is up, the gate
       owns the pass (the timed states pause; input feeds the gate). */
    if (Game_u8ExitAsk == 2u)
    {
        if (Copy_u8HaveIr != 0u)
        {
            if (Copy_eButton == BTN_BACK)
            {
                Game_u8ExitAsk = 0u;
                Game_u8ExitModule = 1u;        /* the loop returns EXITED */
                return;
            }
            Game_u8ExitAsk = 0u;               /* any other key: cancel */
            Game_voidResumeOffline();
            return;
        }
        if ((Copy_u32Now - Game_u32ExitT0) >= TTT_ONLINE_EXIT_ASK_MS)
        {
            Game_u8ExitAsk = 0u;               /* timeout: cancel */
            Game_voidResumeOffline();
        }
        return;
    }

    switch (Game_eState)
    {
    case GAME_ST_ON_TURN:
        if (Copy_u8HaveIr != 0u)
        {
            if (Game_u8HandleCursorMove(Copy_eButton) == 0u)
            {
                if (Copy_eButton == BTN_CONFIRM)
                {
                    if (Game_Au8Board[Game_u8CursorIdx()] == 0u)
                    {
                        Game_u8PendingCell = Game_u8CursorIdx();
                        Game_u8MovePending = 1u;
                        (void)TTT_SESSION_enumSendMove(Game_u8PendingCell);
                        Game_voidPlace(Game_u8PendingCell, Game_u8MyPlayer,
                                       Copy_u32Now); }
                    else { Game_u8TakenFlag = 1u;
                           TTT_voidPaintStatus("TAKEN!", TTT_CFG_COLOR_TAKEN); }
                }
                else if (Copy_eButton == BTN_BACK)
                { Game_u8ExitPrev = 1u; Game_eState = GAME_ST_EXIT_ASK;
                  Game_u32StateT0 = Copy_u32Now;
                  TTT_voidPaintStatus("EXIT? 0 AGAIN", TTT_CFG_COLOR_TAKEN); }
            }
        }
        break;

    case GAME_ST_ON_WAIT:
        if ((Copy_u32Now - Game_u32StateT0) >= TTT_ONLINE_TURN_TIMEOUT_MS)
        { Game_eState = GAME_ST_ON_GRACE;
          Game_u32StateT0 = Copy_u32Now; Game_u32PollT0 = Copy_u32Now;
          Game_u16GraceMs = TTT_ONLINE_REJOIN_MS;
          TTT_voidPaintStatus("FRIEND LEFT?", TTT_CFG_COLOR_TAKEN); }
        if ((Copy_u8HaveIr != 0u) && (Copy_eButton == BTN_BACK))
        { Game_u8ExitPrev = 0u; Game_eState = GAME_ST_EXIT_ASK;
          Game_u32StateT0 = Copy_u32Now;
          TTT_voidPaintStatus("EXIT? 0 AGAIN", TTT_CFG_COLOR_TAKEN); }
        break;

    case GAME_ST_ON_GRACE:
        if ((Copy_u32Now - Game_u32PollT0) >= TTT_ONLINE_GRACE_POLL_MS)
        { Game_u32PollT0 = Copy_u32Now;
          (void)TTT_SESSION_enumRequestState(); }
        if ((Copy_u32Now - Game_u32StateT0) >= Game_u16GraceMs)
        { TTT_SESSION_voidSetOnlineMode(0u);
          HTFT_voidFillScreen(TTT_CFG_COLOR_BG);
          HTFT_voidDrawString(20u, 64u, "FRIEND LEFT", TTT_CFG_COLOR_TAKEN,
                              TTT_CFG_COLOR_BG);
          HTFT_voidDrawString(0u, 148u, "0:LEAVE", TTT_CFG_COLOR_SCORE_D,
                              TTT_CFG_COLOR_BG);
          NET_voidNarrateCounters();          /* M14 */
          Game_eState = GAME_ST_ON_END; }
        if ((Copy_u8HaveIr != 0u) && (Copy_eButton == BTN_BACK))
        { Game_u8ExitPrev = 0u; Game_eState = GAME_ST_EXIT_ASK;
          Game_u32StateT0 = Copy_u32Now;
          TTT_voidPaintStatus("EXIT? 0 AGAIN", TTT_CFG_COLOR_TAKEN); }
        break;

    case GAME_ST_EXIT_ASK:
        if (Copy_u8HaveIr != 0u)
        {
            if (Copy_eButton == BTN_BACK)
            { TTT_SESSION_voidSetOnlineMode(0u); TTT_SESSION_voidGoLobby(); }
            else
            {
                /* F-R1 REVIEW FIX: the resume polarity was inverted.
                   ExitPrev==1 (the exit began during OUR turn) resumes
                   ON_TURN; ==0 (during the friend's) resumes ON_WAIT.
                   The old mapping produced an out-of-turn bounce one way
                   and a false 30 s FRIEND-LEFT escalation the other. */
                Game_eState = Game_u8ExitPrev ? GAME_ST_ON_TURN : GAME_ST_ON_WAIT;
                Game_u32StateT0 = Copy_u32Now;
                /* F-R2 REVIEW FIX: the resync — an OPP_MOVE or STATE reply
                   landing during the ask window falls to the state guards
                   and is lost; one chase makes the board true again. */
                (void)TTT_SESSION_enumRequestState();
                TTT_voidPaintStatus(Game_u8ExitPrev ? "YOUR MOVE" : "WAITING FRIEND..",
                                    Game_u8ExitPrev
                                    ? ((Game_u8MyPlayer == GAME_PLAYER_X) ? TTT_CFG_COLOR_X
                                                                          : TTT_CFG_COLOR_O)
                                    : TTT_CFG_COLOR_SCORE_D);
            }
        }
        else if ((Copy_u32Now - Game_u32StateT0) >= TTT_ONLINE_EXIT_ASK_MS)
        {
            /* the timeout resume: same F-R1 polarity + F-R2 resync */
            Game_eState = Game_u8ExitPrev ? GAME_ST_ON_TURN : GAME_ST_ON_WAIT;
            Game_u32StateT0 = Copy_u32Now;
            (void)TTT_SESSION_enumRequestState();
            TTT_voidPaintStatus(Game_u8ExitPrev ? "YOUR MOVE" : "WAITING FRIEND..",
                                Game_u8ExitPrev
                                ? ((Game_u8MyPlayer == GAME_PLAYER_X) ? TTT_CFG_COLOR_X
                                                                      : TTT_CFG_COLOR_O)
                                : TTT_CFG_COLOR_SCORE_D);
        }
        break;

    case GAME_ST_ON_END:
        /* M7 — THE A1 FIX: the missing parentheses restored (both
           end-states' gates carry the event check FIRST — the stale-
           button-on-no-event class dies here). */
        if ((Copy_u8HaveIr != 0u) &&
                ((Copy_eButton == BTN_BACK) ||
                        (Copy_eButton == BTN_CONFIRM)))
        {
            if (TTT_SESSION_u8ConsumeNetFailExit() != 0u)
            { Game_voidPaintBootScreen(); }
            else
            {
                TTT_SESSION_voidSetOnlineMode(0u);
                (void)TTT_SESSION_enumLeaveRoom();      /* B1 — THE MISSING LINE */
                TTT_SESSION_voidGoLobby();
            }
        }
        break;

    /* ================= OFFLINE (v1.2 verbatim + the PORT-FIX seam) == */
    case GAME_ST_HUMAN:
        if (Copy_u8HaveIr != 0u)
        {
            if (Game_u8HandleCursorMove(Copy_eButton) == 0u)
            {
                if (Copy_eButton == BTN_CONFIRM)
                {
                    if (Game_Au8Board[Game_u8CursorIdx()] == 0u)
                    { Game_voidPlace(Game_u8CursorIdx(), GAME_PLAYER_X,
                                     Copy_u32Now); }
                    else { Game_u8TakenFlag = 1u;
                           TTT_voidPaintStatus("TAKEN!", TTT_CFG_COLOR_TAKEN); }
                }
                else if (Copy_eButton == BTN_BACK)
                {
                    /* PORT-FIX: the house exit seam replaces v1.2's
                       mid-game restart (restart = exit + relaunch, or
                       the streak flow — a deliberate supersession). */
                    Game_u8ExitAsk = 2u;
                    Game_u8OfflineExitFrom = 0u;
                    Game_u32ExitT0 = Copy_u32Now;
                    TTT_voidPaintStatus("EXIT? 0 AGAIN", TTT_CFG_COLOR_TAKEN);
                }
            }
        }
        break;

    case GAME_ST_AI:
        if ((Copy_u8HaveIr != 0u) && (Copy_eButton == BTN_BACK))
        {
            Game_u8ExitAsk = 2u;               /* PORT-FIX: the seam */
            Game_u8OfflineExitFrom = 1u;
            Game_u32ExitT0 = Copy_u32Now;
            TTT_voidPaintStatus("EXIT? 0 AGAIN", TTT_CFG_COLOR_TAKEN);
            break;
        }
        if ((Copy_u32Now - Game_u32StateT0) >= TTT_CFG_AI_THINK_MS)
        { u8 Local_u8Cell = Game_u8AiChoose();
          if (Local_u8Cell != 9u)
          { Game_voidPlace(Local_u8Cell, GAME_PLAYER_O, Copy_u32Now); } }
        break;

    case GAME_ST_WIN_FLASH:
        if ((Copy_u32Now - Game_u32LastBlinkMs) >= TTT_CFG_BLINK_MS)
        {
            Game_u32LastBlinkMs = Copy_u32Now;
            Game_u8BlinkPhase++;
            if (Game_u8BlinkPhase <= (2u * TTT_CFG_BLINK_COUNT))
            { TTT_voidPaintWinLine(Game_Au8WinCells[0], Game_Au8WinCells[2],
                    (Game_u8BlinkPhase & 1u) ? TTT_CFG_COLOR_WINLINE_DIM
                                             : TTT_CFG_COLOR_WINLINE); }
            else
            {
                if (TTT_SESSION_u8OnlineMode() != 0u)
                { HTFT_voidDrawString(0u, 8u, "0:LEAVE",
                        TTT_CFG_COLOR_SCORE_D, TTT_CFG_COLOR_BG);
                  Game_eState = GAME_ST_ON_END; }
                else
                {
                    /* PORT-CHANGE 5 — THE STREAK MODEL: an X win extends
                       the session (score++, straight into the next game —
                       the reward IS the streak); an O win ends the Run
                       (the hub's GameOver screen carries the score). */
                    if (Game_u8LastWinner == GAME_PLAYER_X)
                    { Game_u16Score++;
                      Game_voidNewGame(); }
                    else
                    { Game_u8GameOver = 1u; }   /* the loop returns GAMEOVER */
                }
            }
        }
        break;

    case GAME_ST_GAME_END:
        /* M7 — THE A1 FIX, second site (the gate's shape retained).
           PORT-CHANGE 5: the offline DRAW is the Run's natural end —
           any key or the 3 s hold completes it; the hub's GameOver
           screen carries the streak from here. */
        if ((Copy_u8HaveIr != 0u) &&
            ((Copy_eButton == BTN_CONFIRM) ||
             (Copy_eButton == BTN_BACK)))
        { Game_u8GameOver = 1u; }
        else if ((Copy_u32Now - Game_u32StateT0) >= TTT_END_HOLD_MS)
        { Game_u8GameOver = 1u; }
        break;

    default:
        break;
    }
}

/* ================================================================= *
 *  THE MODULE CONTRACT                                              *
 * ================================================================= */

GameResult_t APP_TTT_enumRun(void)
{
    /* ---- per-launch state (PORT-CHANGE 4): the session's lifetime is
       this Run, not a boot — its statics reset here; the RNG seeds from
       navigation time (the house entropy source); the streak starts
       at zero; the X/O/D counters start clean. ---- */
    (void)TTT_SESSION_enumInit();

    Game_u32RandState ^= MSYSTICK_GetElapsed_ms();
    Game_u32RandState = (Game_u32RandState * 1103515245u) + 12345u;

    Game_u16Score     = 0u;
    Game_u8GameOver   = 0u;
    Game_u8ExitAsk    = 0u;
    Game_u8OfflineExitFrom = 0u;               /* PORT-FIX */
    Game_u8ExitModule = 0u;                    /* PORT-FIX */
    Game_u8LastWinner = 0u;
    Game_Au8XWins     = 0u;
    Game_Au8OWins     = 0u;
    Game_Au8Draws     = 0u;
    Game_u32StateT0   = MSYSTICK_GetElapsed_ms();
    Game_u32PollT0    = Game_u32StateT0;

    Game_voidPaintBootScreen();

    /* ============ THE MODULE LOOP (the router's loop, ported) ====== */
    while (1)
    {
        u32 Local_u32Now = MSYSTICK_GetElapsed_ms();
        APP_INPUT_Event_t Local_stEvt;          /* PORT-CHANGE 2: the seam */
        TTS_Event_t Local_stSession;
        u8 Local_u8HaveEvt =
            (APP_INPUT_enumGetEvent(&Local_stEvt) == APP_INPUT_OK);

        HAUDIO_voidTask();
        if (((Game_eState == GAME_ST_HUMAN) || (Game_eState == GAME_ST_AI) ||
             (Game_eState == GAME_ST_ON_TURN) || (Game_eState == GAME_ST_ON_WAIT)) &&
            (HAUDIO_enumGetState() == HAUDIO_STATE_IDLE))
        {
            (void)HAUDIO_enumPlaySong(&Game_stTheme);
        }

        /* the session's drain (HUB context — File 2's contract: the
           NET task owns the wire; this owns the policy + the paint) */
        (void)TTT_SESSION_enumTask();
        while (TTT_SESSION_enumGetEvent(&Local_stSession) == TTS_OK)
        { Game_voidSessionEvent(&Local_stSession, Local_u32Now); }

        if (Game_eState == GAME_ST_BOOT)
        {
            /* THE MODE MENU (PORT-CHANGE 3): CH = offline; 1 = online
               (the deferred link wakes HERE — the only USART1 site in
               the whole hub build); BACK = the two-beat exit seam. */
            if (Local_u8HaveEvt != 0u)
            {
                if (Game_u8ExitAsk != 0u)
                {
                    if (Local_stEvt.Button == BTN_BACK)
                    { return GAME_RESULT_EXITED; }        /* THE SEAM */
                    /* any other key dismisses (consumed) */
                    Game_u8ExitAsk = 0u;
                    TTT_voidPaintStatus("CH:PLAY 1:ONLINE", TTT_CFG_COLOR_TEXT);
                }
                else if (Game_u8HandleCursorMove(Local_stEvt.Button) == 0u)
                {
                    if (Local_stEvt.Button == BTN_CONFIRM)
                    { Game_voidNewGame(); }
                    else if (Local_stEvt.Button == BTN_NUM_1)
                    {
                        (void)NET_enumBringup();          /* the deferred link */
                        TTT_SESSION_voidEnterNetWait(Local_u32Now);
                    }
                    else if (Local_stEvt.Button == BTN_BACK)
                    {
                        Game_u8ExitAsk  = 1u;
                        Game_u32ExitT0 = Local_u32Now;
                        TTT_voidPaintStatus("EXIT? 0 AGAIN", TTT_CFG_COLOR_TAKEN);
                    }
                }
            }
            else if ((Game_u8ExitAsk != 0u) &&
                     ((Local_u32Now - Game_u32ExitT0) >= TTT_ONLINE_EXIT_ASK_MS))
            {
                Game_u8ExitAsk = 0u;
                TTT_voidPaintStatus("CH:PLAY 1:ONLINE", TTT_CFG_COLOR_TEXT);
            }
        }
        else if (APP_IS_NET_STATE(Game_eState))
        {
            /* the net states: the session owns them; a request bubbles
               up here for the cross-owner transition */
            if (TTT_SESSION_enumRunNetState(Local_u32Now, Local_u8HaveEvt,
                                            Local_stEvt.Button) == TTS_REQ_BOOT)
            { Game_voidPaintBootScreen(); }
        }
        else
        {
            Game_voidRunGameStates(Local_u32Now, Local_u8HaveEvt,
                                   Local_stEvt.Button);
        }

        /* the streak model's exit: a GAMEOVER set by the state handlers
           returns NOW (the hub's GameOver screen owns what follows) */
        if (Game_u8GameOver != 0u)
        {
            if (HAUDIO_enumGetState() != HAUDIO_STATE_PLAYING)
            {
                (void)HAUDIO_enumStop();
            }
            return GAME_RESULT_GAMEOVER;
        }
        if (Game_u8ExitModule != 0u)
        { return GAME_RESULT_EXITED; }         /* PORT-FIX: the offline seam */

        /* the heartbeat (PORT-CHANGE 8: the module owns its beat — the
           hub's pauses during Run, by the house pattern) */
        {
            static u32 Local_u32ShownHalf = 0u;
            if ((Local_u32Now / 500u) != Local_u32ShownHalf)
            { Local_u32ShownHalf = Local_u32Now / 500u;
              Game_voidToggleHeartbeat(); }
        }
    }
}

u16 APP_TTT_u16GetScore(void) { return Game_u16Score; }
