/*
 * TTT_session.c — the online session layer: the game's network
 *  intermediary over NET, and the owner of the NET STATES.
 *  LOCATION: APP/ (flat).
 *
 *  CHANGE SET K (previous revision): THE JOINER KEYBOARD —
 *  2/8/4/6 navigate the 6x6 grid (wrapping); 5 snaps home; CH
 *  selects / submits at four chars; LEFT deletes; UP backs out.
 *  The grid is the SINGLE entry surface — alphabet-complete for
 *  any code the referee generates.
 *
 *  CHANGE SET B1 — THE TEARDOWN + THE RECOVERY, per the referee's
 *  consolidated spec (both-seat countersigned): the leave send-side
 *  on every local exit; the fifth site; the 5-key rewired; the
 *  ALREADY_IN_ROOM recovery; the LOBBY as the recovery context.
 *
 *  STAGE 2 (M9-M14) and STAGE 1 (M1-M6) as previously registered.
 *  Provenance: pure moves from main.c v1.3 rev 5+fixes and the
 *  Stage-1 TTT_session.c; the K and B1 pieces marked. No HESP
 *  include — NET only.
 *
 *  THE RTOS ABSORPTION (this revision — File 2 of 5). Three edits,
 *  each marked ABSORPTION EDIT at its site:
 *    EDIT 1 — the NET pump call removed from enumTask (the NET
 *      task owns the wire; this layer is a pure HUB-context
 *      consumer — which is where the TTS policy handlers MUST
 *      live, because they paint, and two tasks never draw on the
 *      write-only panel).
 *    EDIT 2 — the FOTA event family routed away at the consume
 *      entry, counted (F-T2 resolved as routing; the floor that
 *      holds regardless of the wiring milestone's drain shape).
 *    EDIT 3 — enumInit restructured: session state only, per-
 *      launch (the module calls it at Run() entry). The NET init
 *      and D15 live in NET's bring-up command now.
 *  ZERO FreeRTOS knowledge: no OS includes, no kernel calls — the
 *  seam (NET's queues/task) did all the conversion work.
 */

#include "STD_TYPES.h"

#include <MCAL/SYSTICK/MSYSTICK_interface.h>   /* retry/message timestamps —
                                                  the shim serves these in the
                                                  HUB task, 1:1 with the old
                                                  GetElapsed_ms semantics     */
#include <HAL/TFT/HTFT_interface.h>            /* the net screens (app-level practice) */

#include "APP/TTT_config.h"
#include "APP/TTT_renderer.h"
#include "APP/TTT_session.h"
#include "APP/TTT_app.h"

#include "APP/NET_interface.h"

/* ---- constants ---- */
#define TTS_EVENT_QUEUE_DEPTH 4u

/* ---- Stage-1 statics (verbatim) ---- */
static TTS_Event_t Global_stEvents[TTS_EVENT_QUEUE_DEPTH];
static u8   Global_u8EvtHead;
static u8   Global_u8EvtCount;
static u8   Global_u8EventsDropped;
static u8   Global_u8Initialized = 0u;

typedef struct
{
    u32  Magic;
    u8   IsHost;
    char Code[5];
    char Token[33];
} TTS_CredBlock_t;

#define TTS_CRED_MAGIC  0x54545332UL
/* F-T1 (the absorption review): the fixed SRAM address is the
   reset-survival mechanism — outside every linker section, so the
   C startup's .bss wipe never touches it; the credential outlives
   an MCU reset and RECONNECT-after-reset works. Safe by today's
   arithmetic (last 4 KB of SRAM, between .bss end and the MSP
   stack). PENDING: declare this region in the linker script
   (NOLOAD, named) so any future claim collides LOUDLY at link
   time instead of silently at runtime — rides the absorption
   build's linker pass. */
#define TTS_CRED_ADDR   ((volatile TTS_CredBlock_t *) 0x2000F000UL)

/* ---- M10/M12/M13: the net-flow statics, moved from main.c ---- */
static u8   Global_u8NetBusy;      /* was Game_u8NetBusy   */
static u8   Global_u8RetryCmd;     /* was Game_u8RetryCmd  */
static u32  Global_u32RetryT0;     /* was Game_u32RetryT0  */
static u32  Global_u32NetT0;       /* was Game_u32NetT0    */
static char Global_chCode[5];      /* was Game_chCode      */
static char Global_chEntry[5];     /* was Game_chEntry     */
static u8   Global_u8EntryLen;     /* was Game_u8EntryLen  */
static u32  Global_u32PollT0;      /* was Game_u32PollT0 (the session's: NET_WAIT probe + HOST_WAIT poll) */
static u32  Global_u32NetT0Wait;   /* was Game_u32StateT0 in the NET_WAIT context */
static u8   Global_u8EntryMsg;     /* was Game_u8TakenFlag==2 (the M13 split) */
static u32  Global_u32MsgT0;       /* was Game_u32StateT0 in the entry-msg context */
static u8   Global_u8NetFailExit;  /* was Game_u8NetFailExit */
static u8   Global_u8OnlineMode;   /* was Game_u8OnlineMode (M12) */
static u8   Global_u8FotaDropped;  /* ABSORPTION EDIT 2: FOTA-family events
                                      routed away (see ConsumeNetEvent) */

/* ---- K: the on-screen keyboard ---- */
#define TTS_KB_COLS        6u
#define TTS_KB_ROWS        6u
#define TTS_KB_CELL_W      20u
#define TTS_KB_CELL_H      16u
#define TTS_KB_X0          4u
#define TTS_KB_Y0          38u

static const char TTS_achKeys[(TTS_KB_ROWS * TTS_KB_COLS) + 1u] =
        "1234567890ABCDEFGHIJKLMNOPQRSTUVWXYZ";

static u8 Global_u8KbRow;     /* the grid cursor */
static u8 Global_u8KbCol;

/* ---- prototypes ---- */
static TTS_EnumStatus_t TTS_MapStatus(NET_EnumStatus_t Copy_eNet);
static void TTS_voidConsumeNetEvent(const NET_Event_t *Copy_pstNet);
static void TTS_voidEnqueue(TTS_Event_t *Copy_pstEvent);
static void TTS_voidCopyBounded(char *Copy_chDst, u8 Copy_u8DstSize, const char *Copy_chSrc);
static u8   TTS_u8StrEq(const char *Copy_chA, const char *Copy_chB);
static u8   TTS_u8IsNotReadyClass(const char *Copy_chReason);
static void TTS_voidPaintEntrySlots(void);

/* K: the keyboard painters */
static void TTS_voidPaintKeyCell(u8 Copy_u8Row, u8 Copy_u8Col, u8 Copy_u8Selected);
static void TTS_voidPaintKeyboard(void);
static void TTS_voidMoveKbCursor(s8 Copy_s8Dr, s8 Copy_s8Dc);

/* ---- init ----
 * ABSORPTION EDIT 3: the NET_enumInit call is GONE — the transport
 * init and the D15 double emission live inside NET's bring-up
 * command now (the deferred link; executed at the online-mode
 * entry, never at boot). What remains is the session's OWN state,
 * reset fresh at every Run() entry (the module calls this — File 3
 * — because a game launch, not a boot, is the session's lifetime).
 * Idempotent within a launch. (Also fixed in passing: the event-
 * drop counter now resets per-launch — the delivered code counted
 * it per-boot only.) */
TTS_EnumStatus_t TTT_SESSION_enumInit(void)
{
    Global_u8EvtHead       = 0u;
    Global_u8EvtCount      = 0u;
    Global_u8EventsDropped = 0u;
    Global_u8Initialized   = 1u;

    Global_u8NetBusy      = 0u;
    Global_u8RetryCmd     = 0u;
    Global_u8EntryLen     = 0u;
    Global_chEntry[0]     = '\0';
    Global_chCode[0]      = '\0';
    Global_u8EntryMsg     = 0u;
    Global_u8NetFailExit  = 0u;
    Global_u8OnlineMode   = 0u;
    Global_u8FotaDropped  = 0u;   /* ABSORPTION EDIT 2 */
    return TTS_OK;
}

/* ---- task ----
 * ABSORPTION EDIT 1: the NET pump call is REMOVED — the NET task
 * owns the wire now (spawned at boot; the pump, parse, timeout
 * sweep, and watchdog run in its context). What remains is the
 * DRAIN: the session's event consumption, running in HUB context —
 * which is exactly where the TTS policy handlers must live (they
 * paint: GoHostWait, the status lines, the lobby redraw — two
 * tasks must never draw on the write-only panel). The signature,
 * the drain loop, and the consumers are unchanged. */
TTS_EnumStatus_t TTT_SESSION_enumTask(void)
{
    NET_Event_t Local_stNet;

    if (Global_u8Initialized == 0u)
    {
        return TTS_NOK;
    }

    while (NET_enumGetEvent(&Local_stNet) == NET_OK)
    {
        TTS_voidConsumeNetEvent(&Local_stNet);
    }
    return TTS_OK;
}

/* ---- events out (Stage 1, verbatim) ---- */
TTS_EnumStatus_t TTT_SESSION_enumGetEvent(TTS_Event_t *Copy_pstEvent)
{
    u8 i;

    if ((Global_u8Initialized == 0u) || (Copy_pstEvent == 0))
    {
        return TTS_NOK;
    }
    if (Global_u8EvtCount == 0u)
    {
        return TTS_NO_EVENT;
    }

    for (i = 0u; i < sizeof(TTS_Event_t); i++)
    {
        ((u8 *)Copy_pstEvent)[i] = ((u8 *)&Global_stEvents[Global_u8EvtHead])[i];
    }
    Global_u8EvtHead = (u8)((Global_u8EvtHead + 1u) % TTS_EVENT_QUEUE_DEPTH);
    Global_u8EvtCount--;
    return TTS_OK;
}

/* ---- snapshot / link / probe / counters / credential /
 *      wrappers: Stage 1, VERBATIM ---- */
const TTS_State_t * TTT_SESSION_pcstGetState(void)
{
    return (const TTS_State_t *) NET_pcstGetState();
}

TTS_LinkState_t TTT_SESSION_enumGetLinkState(void)
{
    return (TTS_LinkState_t) NET_enumGetLinkState();
}

u8 TTT_SESSION_u8LinkLive(void)
{
    return (TTT_SESSION_enumGetLinkState() == TTS_LINK_LIVE) ? 1u : 0u;
}

TTS_EnumStatus_t TTT_SESSION_enumProbeStatus(void)
{
    return TTS_MapStatus(NET_enumProbeStatus());
}

u32 TTT_SESSION_u32GetDropped(void)     { return NET_u32GetDropped(); }
u32 TTT_SESSION_u32GetJunkLines(void)   { return NET_u32GetJunkLines(); }
u32 TTT_SESSION_u32GetIdleTicks(void)   { return NET_u32GetIdleTicks(); }
u32 TTT_SESSION_u32GetLateReplies(void) { return NET_u32GetLateReplies(); }
u8  TTT_SESSION_u8GetFotaDropped(void)  { return Global_u8FotaDropped; }   /* ABSORPTION EDIT 2 */

u8 TTT_SESSION_u8HasCredential(void)
{
    return (TTS_CRED_ADDR->Magic == TTS_CRED_MAGIC) ? 1u : 0u;
}

u8 TTT_SESSION_u8IsHost(void)
{
    return ((TTT_SESSION_u8HasCredential() != 0u) && (TTS_CRED_ADDR->IsHost != 0u)) ? 1u : 0u;
}

void TTT_SESSION_voidGetCredential(char *Copy_chCodeOut)
{
    if ((TTT_SESSION_u8HasCredential() != 0u) && (Copy_chCodeOut != 0))
    {
        TTS_voidCopyBounded(Copy_chCodeOut, 5u, (const char *)TTS_CRED_ADDR->Code);
    }
    else if (Copy_chCodeOut != 0)
    {
        Copy_chCodeOut[0] = '\0';
    }
    else
    {
        /* NULL: nothing to do */
    }
}

void TTT_SESSION_voidInvalidateCredential(void)
{
    TTS_CRED_ADDR->Magic = 0u;
}

TTS_EnumStatus_t TTT_SESSION_enumHost(void)
{
    return TTS_MapStatus(NET_enumHost("ttt", TTT_ONLINE_NAME));
}

TTS_EnumStatus_t TTT_SESSION_enumJoin(const char *Copy_chCode)
{
    if (Copy_chCode == 0)
    {
        return TTS_NOK;
    }
    return TTS_MapStatus(NET_enumJoin(Copy_chCode, TTT_ONLINE_NAME));
}

TTS_EnumStatus_t TTT_SESSION_enumReconnect(void)
{
    char Local_chCode[8];
    char Local_chToken[40];

    if (TTT_SESSION_u8HasCredential() == 0u)
    {
        return TTS_NOK;
    }
    TTS_voidCopyBounded(Local_chCode,  sizeof(Local_chCode),  (const char *)TTS_CRED_ADDR->Code);
    TTS_voidCopyBounded(Local_chToken, sizeof(Local_chToken), (const char *)TTS_CRED_ADDR->Token);
    return TTS_MapStatus(NET_enumReconnect(Local_chCode, Local_chToken));
}

TTS_EnumStatus_t TTT_SESSION_enumSendMove(u8 Copy_u8Cell)
{
    return TTS_MapStatus(NET_enumSendMove(Copy_u8Cell));
}

TTS_EnumStatus_t TTT_SESSION_enumRequestState(void)
{
    return TTS_MapStatus(NET_enumRequestState());
}

/* ---- B1: the teardown's wrapper (Rule 1) ---- */
TTS_EnumStatus_t TTT_SESSION_enumLeaveRoom(void)
{
    /* EAGER LOCAL INVALIDATION: the exit intent kills the
       credential NOW — the lobby paints a moment later and must
       not advertise a room we just chose to leave. The ack's M1
       site re-confirms (idempotent). The lost-ack edge stays
       recoverable: the create-bounce chase resumes the room
       without the 5. */
    TTT_SESSION_voidInvalidateCredential();
    return TTS_MapStatus(NET_enumLeaveRoom());
}

/* ---- M12: the online-flow flag ---- */
void TTT_SESSION_voidSetOnlineMode(u8 Copy_u8On) { Global_u8OnlineMode = (Copy_u8On != 0u) ? 1u : 0u; }
u8   TTT_SESSION_u8OnlineMode(void)              { return Global_u8OnlineMode; }

/* ---- M13: the cross-owned bits ---- */
u8   TTT_SESSION_u8Busy(void)             { return Global_u8NetBusy; }
void TTT_SESSION_voidClearBusy(void)      { Global_u8NetBusy = 0u; }
u8   TTT_SESSION_u8ConsumeNetFailExit(void)
{
    u8 Local_u8Was = Global_u8NetFailExit;
    Global_u8NetFailExit = 0u;
    return Local_u8Was;
}

/* ================================================================= *
 *  M10 — THE NET STATES' SCREENS                                     *
 * ================================================================= */

void TTT_SESSION_voidEnterNetWait(u32 Copy_u32Now)
{
    HTFT_voidFillScreen(TTT_CFG_COLOR_BG);
    HTFT_voidDrawString(24u, 56u, "CONNECTING", TTT_CFG_COLOR_TEXT,
                        TTT_CFG_COLOR_BG);
    TTT_voidPaintStatus("0:CANCEL", TTT_CFG_COLOR_SCORE_D);
    Game_eState = GAME_ST_NET_WAIT;
    Global_u32NetT0Wait = Copy_u32Now;
    Global_u32PollT0    = Copy_u32Now;
}

void TTT_SESSION_voidGoLobby(void)
{
    Global_u8NetBusy  = 0u;
    Global_u8RetryCmd = 0u;
    HTFT_voidFillScreen(TTT_CFG_COLOR_BG);
    HTFT_voidDrawString2x(24u, 20u, "ONLINE", TTT_CFG_COLOR_TEXT,
                          TTT_CFG_COLOR_BG);
    TTT_voidPaintStatus("1:HOST 2:JOIN", TTT_CFG_COLOR_TEXT);
    HTFT_voidDrawString(0u, 8u, TTT_SESSION_u8HasCredential() ? "5:REJOIN ROOM" : " ",
                        TTT_CFG_COLOR_CURSOR, TTT_CFG_COLOR_BG);
    Game_eState = GAME_ST_LOBBY;
}

void TTT_SESSION_voidGoHostWait(u32 Copy_u32Now)
{
    TTS_voidCopyBounded(Global_chCode, sizeof(Global_chCode),
                        (const char *)TTS_CRED_ADDR->Code);
    HTFT_voidFillScreen(TTT_CFG_COLOR_BG);
    TTT_voidPaintStatus("ROOM CODE", TTT_CFG_COLOR_TEXT);
    HTFT_voidDrawString2x(32u, 40u, Global_chCode, TTT_CFG_COLOR_CURSOR,
                          TTT_CFG_COLOR_BG);
    HTFT_voidDrawString(0u, 72u, "WAITING FRIEND..", TTT_CFG_COLOR_SCORE_D,
                        TTT_CFG_COLOR_BG);
    HTFT_voidDrawString(0u, 148u, "0:CANCEL", TTT_CFG_COLOR_SCORE_D,
                        TTT_CFG_COLOR_BG);
    Game_eState  = GAME_ST_HOST_WAIT;
    Global_u32PollT0 = Copy_u32Now;
}

void TTT_SESSION_voidGoEntry(void)
{
    /* K: the entry screen carries the keyboard. Home = '1'. */
    HTFT_voidFillScreen(TTT_CFG_COLOR_BG);
    TTT_voidPaintStatus("ENTER ROOM CODE", TTT_CFG_COLOR_TEXT);
    Global_u8EntryLen = 0u; Global_chEntry[0] = '\0';
    Global_u8KbRow = 0u; Global_u8KbCol = 0u;
    HTFT_voidDrawString(0u, 8u, "UP:BACK", TTT_CFG_COLOR_SCORE_D,
                        TTT_CFG_COLOR_BG);
    TTS_voidPaintEntrySlots();
    TTS_voidPaintKeyboard();
    HTFT_voidDrawString(0u, 144u, "CH:SEL LEFT:DEL", TTT_CFG_COLOR_SCORE_D,
                        TTT_CFG_COLOR_BG);
    Game_eState = GAME_ST_JOIN_ENTRY;
}

void TTT_SESSION_voidNetFail(void)
{
    Global_u8OnlineMode = 0u;
    HTFT_voidFillScreen(TTT_CFG_COLOR_BG);
    HTFT_voidDrawString(16u, 64u, "NO CONNECTION", TTT_CFG_COLOR_TAKEN,
                        TTT_CFG_COLOR_BG);
    HTFT_voidDrawString(0u, 148u, "0:BACK", TTT_CFG_COLOR_SCORE_D,
                        TTT_CFG_COLOR_BG);
    NET_voidNarrateCounters();
    Global_u8NetFailExit = 1u;
    Game_eState = GAME_ST_ON_END;
}

/* ---- the entry slots (K: y48 -> y18) ---- */
static void TTS_voidPaintEntrySlots(void)
{
    char Local_ach[10]; u8 n = 0u;
    for (u8 i = 0u; i < 4u; i++)
    {
        if (i < Global_u8EntryLen)
        { Local_ach[n++] = Global_chEntry[i]; Local_ach[n++] = ' '; }
        else
        { Local_ach[n++] = '_'; Local_ach[n++] = ' '; }
    }
    Local_ach[n] = '\0';
    HTFT_voidDrawString2x(0u, 18u, Local_ach, TTT_CFG_COLOR_CURSOR,
                          TTT_CFG_COLOR_BG);
}

/* ================================================================= *
 *  K — THE KEYBOARD PAINTERS                                         *
 * ================================================================= */

static void TTS_voidPaintKeyCell(u8 Copy_u8Row, u8 Copy_u8Col, u8 Copy_u8Selected)
{
    char Local_ach[2];
    u16 Local_u16X = (u16)(TTS_KB_X0 + (Copy_u8Col * TTS_KB_CELL_W));
    u16 Local_u16Y = (u16)(TTS_KB_Y0 + (Copy_u8Row * TTS_KB_CELL_H));

    HTFT_voidFillRect(Local_u16X, Local_u16Y, TTS_KB_CELL_W, TTS_KB_CELL_H,
                      (Copy_u8Selected != 0u) ? TTT_CFG_COLOR_CURSOR
                                              : TTT_CFG_COLOR_BG);
    Local_ach[0] = TTS_achKeys[(Copy_u8Row * TTS_KB_COLS) + Copy_u8Col];
    Local_ach[1] = '\0';
    HTFT_voidDrawString((u16)(Local_u16X + 6u), (u16)(Local_u16Y + 4u), Local_ach,
                        (Copy_u8Selected != 0u) ? TTT_CFG_COLOR_BG
                                                : TTT_CFG_COLOR_TEXT,
                        (Copy_u8Selected != 0u) ? TTT_CFG_COLOR_CURSOR
                                                : TTT_CFG_COLOR_BG);
}

static void TTS_voidPaintKeyboard(void)
{
    for (u8 Local_u8R = 0u; Local_u8R < TTS_KB_ROWS; Local_u8R++)
    {
        for (u8 Local_u8C = 0u; Local_u8C < TTS_KB_COLS; Local_u8C++)
        {
            TTS_voidPaintKeyCell(Local_u8R, Local_u8C,
                    (u8)((Local_u8R == Global_u8KbRow) &&
                         (Local_u8C == Global_u8KbCol)));
        }
    }
}

static void TTS_voidMoveKbCursor(s8 Copy_s8Dr, s8 Copy_s8Dc)
{
    u8 Local_u8OldR = Global_u8KbRow;
    u8 Local_u8OldC = Global_u8KbCol;

    Global_u8KbRow = (u8)((Global_u8KbRow + Copy_s8Dr + (s8)TTS_KB_ROWS) % TTS_KB_ROWS);
    Global_u8KbCol = (u8)((Global_u8KbCol + Copy_s8Dc + (s8)TTS_KB_COLS) % TTS_KB_COLS);

    TTS_voidPaintKeyCell(Local_u8OldR, Local_u8OldC, 0u);
    TTS_voidPaintKeyCell(Global_u8KbRow, Global_u8KbCol, 1u);
}

/* ================================================================= *
 *  M10 — THE NET STATES' LOOP HANDLER                                *
 * ================================================================= */

TTS_Request_t TTT_SESSION_enumRunNetState(u32 Copy_u32Now, u8 Copy_u8HaveIr,
                                          VirtualButton_t Copy_eButton)
{
    switch (Game_eState)
    {
    case GAME_ST_NET_WAIT:
        if (TTT_SESSION_u8LinkLive() != 0u)
        { TTT_SESSION_voidGoLobby(); }
        else if ((Copy_u8HaveIr != 0u) && (Copy_eButton == BTN_BACK))
        { return TTS_REQ_BOOT; }
        else if ((Copy_u32Now - Global_u32NetT0Wait) >= TTT_ONLINE_CONNECT_MS)
        { TTT_SESSION_voidNetFail(); }
        else if ((Copy_u32Now - Global_u32PollT0) >= TTT_ONLINE_NET_POLL_MS)
        {
            Global_u32PollT0 = Copy_u32Now;
            (void)TTT_SESSION_enumProbeStatus();
        }
        break;

    case GAME_ST_LOBBY:
        if ((Copy_u8HaveIr != 0u) && (Global_u8NetBusy == 0u))
        {
            if (Copy_eButton == BTN_NUM_1)
            { (void)TTT_SESSION_enumHost();
              Global_chCode[0] = '\0';
              TTT_voidPaintStatus("CREATING..", TTT_CFG_COLOR_TEXT);
              Global_u32PollT0  = Copy_u32Now;
              Global_u32NetT0   = Copy_u32Now;
              Global_u32RetryT0 = Copy_u32Now;
              Global_u8RetryCmd = 0u;
              Game_eState = GAME_ST_HOST_WAIT; }
            else if (Copy_eButton == BTN_NUM_2)
            { TTT_SESSION_voidGoEntry(); }
            else if ((Copy_eButton == BTN_NUM_5) &&
                     (TTT_SESSION_u8HasCredential() != 0u))
            { Global_u8NetBusy = 1u;
              /* B1 / Rule 2: RECONNECT only on actual link-loss —
                 the socket is new server-side (NET_LOST->NET_BACK,
                 or the ESP rebooted: ESP_READY re-arrived). Connected
                 and bound: never RECONNECT — Rule 3's GET_STATE; the
                 reply's status drives the screen (active -> resume,
                 waiting -> the code, finished -> the end screen,
                 NOT_READY -> the room is gone). */
              if (NET_u8SocketIsNew() != 0u)
              { (void)TTT_SESSION_enumReconnect();
                TTT_voidPaintStatus("RECONNECTING..", TTT_CFG_COLOR_TEXT); }
              else
              { (void)TTT_SESSION_enumRequestState();
              TTT_voidPaintStatus("CHECKING ROOM..", TTT_CFG_COLOR_TEXT); } }
            else if (Copy_eButton == BTN_BACK)
            { return TTS_REQ_BOOT; }
        }
        break;

    case GAME_ST_HOST_WAIT:
        if (Global_u8RetryCmd == 1u)
        {
            if ((Copy_u32Now - Global_u32RetryT0) >= TTT_ONLINE_RETRY_MS)
            {
                if ((Copy_u32Now - Global_u32NetT0) < TTT_ONLINE_CONNECT_MS)
                { (void)TTT_SESSION_enumHost();
                  Global_u8RetryCmd = 0u; Global_u32RetryT0 = Copy_u32Now; }
                else
                { Global_u8RetryCmd = 0u; TTT_SESSION_voidNetFail(); }
            }
        }
        else if ((Copy_u32Now - Global_u32PollT0) >= TTT_ONLINE_WAIT_POLL_MS)
        { Global_u32PollT0 = Copy_u32Now;
          (void)TTT_SESSION_enumRequestState(); }
        if ((Copy_u8HaveIr != 0u) && (Copy_eButton == BTN_BACK))
        { Global_u8OnlineMode = 0u;
          (void)TTT_SESSION_enumLeaveRoom();      /* B1: the waiting arm —
                                                      the silent dissolve */
          TTT_SESSION_voidGoLobby(); }
        break;

    case GAME_ST_JOIN_ENTRY:
        /* the retry ladder — verbatim; the input is the keyboard */
        if ((Global_u8RetryCmd == 2u) &&
            ((Copy_u32Now - Global_u32RetryT0) >= TTT_ONLINE_RETRY_MS))
        {
            if ((Copy_u32Now - Global_u32NetT0) < TTT_ONLINE_CONNECT_MS)
            { (void)TTT_SESSION_enumJoin(Global_chEntry);
              Global_u8RetryCmd = 0u; Global_u32RetryT0 = Copy_u32Now; }
            else
            { Global_u8RetryCmd = 0u; TTT_SESSION_voidNetFail(); }
        }
        if (Copy_u8HaveIr != 0u)
        {
            VirtualButton_t b = Copy_eButton;
            switch (b)
            {
            case BTN_NUM_2:  TTS_voidMoveKbCursor(-1,  0); break;
            case BTN_NUM_8:  TTS_voidMoveKbCursor( 1,  0); break;
            case BTN_NUM_4:  TTS_voidMoveKbCursor( 0, -1); break;
            case BTN_NUM_6:  TTS_voidMoveKbCursor( 0,  1); break;

            case BTN_NUM_5:   /* home — the board's 5-to-center mirror */
            {
                u8 Local_u8OldR = Global_u8KbRow;
                u8 Local_u8OldC = Global_u8KbCol;
                Global_u8KbRow = 0u; Global_u8KbCol = 0u;
                TTS_voidPaintKeyCell(Local_u8OldR, Local_u8OldC, 0u);
                TTS_voidPaintKeyCell(0u, 0u, 1u);
            }
            break;

            case BTN_LEFT:   /* delete — verbatim */
            { if (Global_u8EntryLen > 0u)
              { Global_chEntry[--Global_u8EntryLen] = '\0'; } }
            break;

            case BTN_UP:
            { TTT_SESSION_voidGoLobby(); return TTS_REQ_NONE; }

            case BTN_CONFIRM:
            {
                if (Global_u8EntryLen < 4u)
                { Global_chEntry[Global_u8EntryLen++] =
                        TTS_achKeys[(Global_u8KbRow * TTS_KB_COLS) + Global_u8KbCol]; }
                else   /* four chars in — CONFIRM submits */
                { Global_chEntry[4] = '\0';
                  Global_u32NetT0 = Copy_u32Now;
                  (void)TTT_SESSION_enumJoin(Global_chEntry);
                  TTT_voidPaintStatus("JOINING..", TTT_CFG_COLOR_TEXT); }
            }
            break;

            default:
                break;
            }
            Global_chEntry[Global_u8EntryLen] = '\0';
            TTS_voidPaintEntrySlots();
        }
        if ((Global_u8EntryMsg != 0u) &&
            ((Copy_u32Now - Global_u32MsgT0) >= TTT_ONLINE_MSG_MS))
        { Global_u8EntryMsg = 0u;
          TTT_voidPaintStatus("ENTER ROOM CODE", TTT_CFG_COLOR_TEXT); }
        break;

    default:
        break;
    }
    return TTS_REQ_NONE;
}

/* ================================================================= *
 *  M11 + B1 — THE CONSUMER: the M1 sites, the policy half, and the   *
 *  recovery (the ALREADY_IN_ROOM chase, the LOBBY outcomes)          *
 * ================================================================= */

static void TTS_voidConsumeNetEvent(const NET_Event_t *Copy_pstNet)
{
    TTS_Event_t Local_stTts;
    u8 i;

    /* ABSORPTION EDIT 2 (F-T2 resolved as routing): the FOTA family
     * is DROPPED here, counted — the TTS enum has no corresponding
     * values (the byte-copy would translate them into out-of-range
     * junk that forwards to the game). The FOTA wiring milestone's
     * drain routes these events to the orchestrator BEFORE the
     * session ever sees them; this guard is the floor that keeps
     * the session safe even if routing changes shape above it. The
     * counter makes the drops visible, per invariant 7. */
    if ((Copy_pstNet->Type == NET_EVT_FOTA_AVAILABLE) ||
        (Copy_pstNet->Type == NET_EVT_FOTA_READY)     ||
        (Copy_pstNet->Type == NET_EVT_FOTA_DONE)      ||
        (Copy_pstNet->Type == NET_EVT_FOTA_WATCHDOG))
    {
        Global_u8FotaDropped++;
        return;
    }

    /* M3 glue (Stage 1, verbatim) */
    for (i = 0u; i < sizeof(TTS_Event_t); i++)
    {
        ((u8 *)&Local_stTts)[i] = ((const u8 *)Copy_pstNet)[i];
    }
    Local_stTts.Type = (TTS_EventType_t)Copy_pstNet->Type;

    /* ---- M1 (+ B1's fifth site): the credential sites ---- */
    switch (Local_stTts.Type)
    {
    case TTS_EVT_ROOM_CREATED:
        TTS_CRED_ADDR->Magic = TTS_CRED_MAGIC;
        TTS_voidCopyBounded((char *)TTS_CRED_ADDR->Code,  5u,  Local_stTts.Code);
        TTS_voidCopyBounded((char *)TTS_CRED_ADDR->Token, 33u, Local_stTts.Token);
        TTS_CRED_ADDR->IsHost = 1u;
        break;

    case TTS_EVT_ROOM_JOINED:
        TTS_CRED_ADDR->Magic = TTS_CRED_MAGIC;
        TTS_voidCopyBounded((char *)TTS_CRED_ADDR->Token, 33u, Local_stTts.Token);
        TTS_voidCopyBounded((char *)TTS_CRED_ADDR->Code,  5u, Local_stTts.Code);
        TTS_CRED_ADDR->IsHost = 0u;
        break;

    case TTS_EVT_ROOM_GONE:
        TTT_SESSION_voidInvalidateCredential();
        break;

    case TTS_EVT_ROOM_LEFT:
        /* B1: the leave's terminal — the binding is freed; the
           credential dies with the room. */
        TTT_SESSION_voidInvalidateCredential();
        break;

    default:
        break;
    }

    /* ---- M11: the policy consumption (+ B1's recovery) ---- */
    switch (Local_stTts.Type)
    {
    case TTS_EVT_ROOM_CREATED:
        if (Game_eState == GAME_ST_HOST_WAIT)
        {
            Global_u8RetryCmd = 0u;
            TTT_SESSION_voidGoHostWait(MSYSTICK_GetElapsed_ms());
        }
        return;

    case TTS_EVT_ROOM_GONE:
        Global_u8NetBusy  = 0u;
        Global_u8RetryCmd = 0u;
        if (Game_eState == GAME_ST_JOIN_ENTRY)
        {
            TTT_voidPaintStatus("ROOM NOT FOUND", TTT_CFG_COLOR_TAKEN);
            Global_u32MsgT0 = MSYSTICK_GetElapsed_ms();
            Global_u8EntryMsg = 1u;
            return;
        }
        break;   /* forward — the game handles its own states */

    case TTS_EVT_RESP_ERR:
        Global_u8NetBusy = 0u;
        /* B1 (Q3-confirmed): the ALREADY_IN_ROOM recovery, ANY
           context — the state chase. The reply's status drives the
           screen (waiting -> host-wait; active -> the game;
           finished -> the end screen). Also self-heals a fast
           create racing the leave's ack. Consumed: the STATE event
           forwards and the game applies it. */
        if (TTS_u8StrEq(Local_stTts.Reason, "ALREADY_IN_ROOM"))
        {
            Global_u8RetryCmd = 0u;
            TTT_voidPaintStatus("IN ROOM..", TTT_CFG_COLOR_TEXT);
            (void)TTT_SESSION_enumRequestState();
            return;
        }
        if (Game_eState == GAME_ST_HOST_WAIT)
        {
            if (TTS_u8IsNotReadyClass(Local_stTts.Reason) != 0u)
            { Global_u8RetryCmd = 1u; Global_u32RetryT0 = MSYSTICK_GetElapsed_ms();
              TTT_voidPaintStatus("CONNECTING..", TTT_CFG_COLOR_TEXT); }
            else
            { Global_u8RetryCmd = 0u; TTT_SESSION_voidNetFail(); }
            return;
        }
        else if (Game_eState == GAME_ST_JOIN_ENTRY)
        {
            if (TTS_u8IsNotReadyClass(Local_stTts.Reason) != 0u)
            { Global_u8RetryCmd = 2u; Global_u32RetryT0 = MSYSTICK_GetElapsed_ms();
              TTT_voidPaintStatus("CONNECTING..", TTT_CFG_COLOR_TEXT); }
            else if (Local_stTts.Reason[0] == 'R')
            { TTT_voidPaintStatus("ROOM FULL", TTT_CFG_COLOR_TAKEN);
              Global_u32MsgT0 = MSYSTICK_GetElapsed_ms(); Global_u8EntryMsg = 1u; }
            else
            { TTT_voidPaintStatus(Local_stTts.Reason, TTT_CFG_COLOR_TAKEN);
              Global_u32MsgT0 = MSYSTICK_GetElapsed_ms(); Global_u8EntryMsg = 1u; }
            return;
        }
        else if (Game_eState == GAME_ST_LOBBY)
        {
        	/* B1: the lobby is the recovery context — the 5's and
                       the leave's answers land here. NOT_READY = unbound,
                       the room is gone (the chase's "free" outcome — an
                       answer, not an error). TIMEOUT = an uncertain leave —
                       Rule 3's chase settles it (the lost-ack edge). */
        	if (TTS_u8StrEq(Local_stTts.Reason, "NOT_READY"))
        	{ TTT_SESSION_voidInvalidateCredential();
        	TTT_voidPaintStatus("ROOM GONE", TTT_CFG_COLOR_TAKEN); }
        	else if (TTS_u8StrEq(Local_stTts.Reason, "TIMEOUT"))
        	{ TTT_voidPaintStatus("CHECKING..", TTT_CFG_COLOR_TEXT);
        	(void)TTT_SESSION_enumRequestState(); }
        	return;
        }
        break;   /* forward — the game's play-context "NET ERROR" */

    case TTS_EVT_CMD_TIMEOUT:
            if (Game_eState == GAME_ST_NET_WAIT)
            { return; }
            if (Game_eState == GAME_ST_HOST_WAIT)
            { Global_u8RetryCmd = 1u; Global_u32RetryT0 = MSYSTICK_GetElapsed_ms();
              TTT_voidPaintStatus("CONNECTING..", TTT_CFG_COLOR_TEXT); return; }
            else if (Game_eState == GAME_ST_JOIN_ENTRY)
            { Global_u8RetryCmd = 2u; Global_u32RetryT0 = MSYSTICK_GetElapsed_ms();
              TTT_voidPaintStatus("CONNECTING..", TTT_CFG_COLOR_TEXT); return; }
            break;

        default:
            break;
        }

        TTS_voidEnqueue(&Local_stTts);
}

/* ---- helpers ---- */
static TTS_EnumStatus_t TTS_MapStatus(NET_EnumStatus_t Copy_eNet)
{
    TTS_EnumStatus_t Local_eTts;

    switch (Copy_eNet)
    {
    case NET_OK:       Local_eTts = TTS_OK;       break;
    case NET_NO_EVENT: Local_eTts = TTS_NO_EVENT; break;
    case NET_BUSY:     Local_eTts = TTS_BUSY;     break;
    default:           Local_eTts = TTS_NOK;      break;
    }
    return Local_eTts;
}

static void TTS_voidEnqueue(TTS_Event_t *Copy_pstEvent)
{
    TTS_Event_t *Local_pstSlot;

    if (Global_u8EvtCount >= TTS_EVENT_QUEUE_DEPTH)
    {
        Global_u8EventsDropped++;
        return;
    }
    Local_pstSlot = &Global_stEvents[(u8)((Global_u8EvtHead + Global_u8EvtCount) % TTS_EVENT_QUEUE_DEPTH)];
    for (u8 i = 0u; i < sizeof(TTS_Event_t); i++)
    {
        ((u8 *)Local_pstSlot)[i] = ((u8 *)Copy_pstEvent)[i];
    }
    Global_u8EvtCount++;
}

static u8 TTS_u8StrEq(const char *Copy_chA, const char *Copy_chB)
{
    u8 Local_u8Idx = 0u;

    while ((Copy_chA[Local_u8Idx] != '\0') && (Copy_chB[Local_u8Idx] != '\0') &&
           (Copy_chA[Local_u8Idx] == Copy_chB[Local_u8Idx]))
    {
        Local_u8Idx++;
    }
    return (Copy_chA[Local_u8Idx] == Copy_chB[Local_u8Idx]) ? 1u : 0u;
}

static u8 TTS_u8IsNotReadyClass(const char *Copy_chReason)
{
    if (TTS_u8StrEq(Copy_chReason, "NOT_READY")) { return 1u; }
    if (TTS_u8StrEq(Copy_chReason, "NO_WIFI"))   { return 1u; }
    if (TTS_u8StrEq(Copy_chReason, "NO_NET"))    { return 1u; }
    if (TTS_u8StrEq(Copy_chReason, "BUSY"))      { return 1u; }
    return 0u;
}

static void TTS_voidCopyBounded(char *Copy_chDst, u8 Copy_u8DstSize, const char *Copy_chSrc)
{
    u8 Local_u8Idx = 0u;

    while ((Local_u8Idx + 1u) < Copy_u8DstSize && Copy_chSrc[Local_u8Idx] != '\0')
    {
        Copy_chDst[Local_u8Idx] = Copy_chSrc[Local_u8Idx];
        Local_u8Idx++;
    }
    Copy_chDst[Local_u8Idx] = '\0';
}
