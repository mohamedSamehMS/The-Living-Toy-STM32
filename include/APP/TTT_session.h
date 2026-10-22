/*
 * TTT_session.h — the online session layer, PUBLIC API.
 *  LOCATION: APP/ (flat).
 *
 *  STAGE 1 surface (unchanged): the command wrappers, the
 *  credential, the event queue, the counters, the snapshot
 *  pass-through. STAGE 2 ADDITIONS (M9-M14, annotated in
 *  TTT_session.c): the net states, their screens, the retry
 *  machinery, and the policy half of the event dispatcher —
 *  all moved DOWN from main.c.
 *
 *  THE EVENT SPLIT (M11): the session CONSUMES the policy
 *  events (ROOM_CREATED in full; ROOM_GONE / RESP_ERR /
 *  CMD_TIMEOUT for its own states + the retry arming) and
 *  forwards everything else 1:1 — the game's dispatcher holds
 *  the game cases. Consumers see the forwarded set only.
 *
 *  THE RTOS ABSORPTION (this revision — File 2 of the absorption
 *  package): the session is now a PURE HUB-CONTEXT CONSUMER.
 *    - The NET pump call is GONE — the NET task owns the wire
 *      (spawned at boot; the link brought up only at the online
 *      entry, per the deferred-link ruling).
 *    - enumInit resets SESSION STATE ONLY, per-LAUNCH — the TTT
 *      module calls it at Run() entry (the session's lifetime is
 *      a game launch, not a boot).
 *    - The FOTA event family is routed away at the consume entry,
 *      COUNTED (the TTS enum cannot represent it; the FOTA wiring
 *      milestone's drain routes those events to the orchestrator
 *      before the session ever sees them — this guard is the floor
 *      that holds regardless of the routing's final shape).
 *  ZERO FreeRTOS knowledge in this layer — the seam did all the
 *  work: no OS includes, no kernel calls, nothing.
 */

#ifndef TTT_SESSION_H_
#define TTT_SESSION_H_

#include "STD_TYPES.h"
#include "HAL/HVirtualButtons_interface.h"     /* VirtualButton_t (dependency-free) */

/* ===================== status / types (Stage 1, verbatim) ===================== */
typedef enum
{
    TTS_OK = 0,
    TTS_NOK,
    TTS_NO_EVENT,
    TTS_BUSY
} TTS_EnumStatus_t;

typedef enum
{
    TTS_LINK_IDLE = 0,
    TTS_LINK_CONNECTING,
    TTS_LINK_LIVE,
    TTS_LINK_LOST
} TTS_LinkState_t;

typedef enum
{
    TTS_STATUS_UNKNOWN = 0,
    TTS_STATUS_WAITING,
    TTS_STATUS_ACTIVE,
    TTS_STATUS_FINISHED
} TTS_Status_t;

typedef struct
{
    TTS_Status_t Status;
    char Board[10];
    u8   MyMark;
    u8   MyTurn;
    char OppName[21];
    u8   OppConnected;
    u8   Winner;
} TTS_State_t;

typedef enum
{
    TTS_EVT_NONE = 0,
    TTS_EVT_LINK_LOST,
    TTS_EVT_CMD_TIMEOUT,
    TTS_EVT_ROOM_CREATED,
    TTS_EVT_ROOM_JOINED,
    TTS_EVT_ROOM_GONE,
    TTS_EVT_RESP_ERR,
    TTS_EVT_MOVE_OK,
    TTS_EVT_MOVE_REJECTED,
    TTS_EVT_STATE,
    TTS_EVT_OPP_MOVE,
    TTS_EVT_OPP_JOINED,
    TTS_EVT_OPP_LEFT,
    TTS_EVT_MATCH_ENDED,
    TTS_EVT_REMATCH_OFFERED,
    TTS_EVT_REMATCH_STARTED,
    TTS_EVT_REJOINED,
    TTS_EVT_ROOM_LEFT,
    TTS_EVT_NET_DISCONNECTED,
    TTS_EVT_LINK_BACK
} TTS_EventType_t;

typedef struct
{
    TTS_EventType_t Type;
    u8   Cell;
    u8   Mark;
    u8   MyTurn;
    u8   Winner;
    u16  GraceS;
    char Code[5];
    char Token[33];
    char Name[21];
    char Reason[24];
    char Board[10];
    char Line[4];
} TTS_Event_t;

/* ============ STAGE 2 ADDITIONS (M9-M14) ============ */

/* Cross-owner transition requests: the session cannot call up;
 * requests bubble to main, which routes (M9). */
typedef enum
{
    TTS_REQ_NONE = 0,
    TTS_REQ_BOOT               /* cancel to the hub menu */
} TTS_Request_t;

/* Net-state entry points — the screens moved from main.c
 * (M10); each paints, resets its own flags, sets the state. */
void TTT_SESSION_voidEnterNetWait(u32 Copy_u32Now);   /* was PaintNetWait + timers  */
void TTT_SESSION_voidGoLobby(void);                   /* was PaintLobby             */
void TTT_SESSION_voidGoHostWait(u32 Copy_u32Now);     /* was PaintHostWait + poll   */
void TTT_SESSION_voidGoEntry(void);                   /* was PaintEntry + slots     */
void TTT_SESSION_voidNetFail(void);                   /* was PaintNetFail           */

/* The net states' loop handler — main calls it when the state
 * is session-owned (APP_IS_NET_STATE). Branch bodies are the
 * old main.c cases, verbatim. Returns a request when a
 * transition must cross the ownership boundary. */
TTS_Request_t TTT_SESSION_enumRunNetState(u32 Copy_u32Now, u8 Copy_u8HaveIr,
                                          VirtualButton_t Copy_eButton);

/* The online-flow flag (M12: re-homed to the session — a FLOW
 * property; the game reads and sets it downward via these). */
void TTT_SESSION_voidSetOnlineMode(u8 Copy_u8On);
u8   TTT_SESSION_u8OnlineMode(void);

/* The remaining cross-owned bits (M13): the busy flag (set in
 * LOBBY, cleared by the game's STATE branch), and the net-fail
 * exit marker (set by NetFail, consumed by the game's ON_END
 * branch). */
u8   TTT_SESSION_u8Busy(void);
void TTT_SESSION_voidClearBusy(void);
u8   TTT_SESSION_u8ConsumeNetFailExit(void);

/* ============ STAGE 1 SURFACE (with the absorption deltas) ============ */

/* ---- init ----
 * ABSORPTION: session-state reset ONLY (the NET init and the D15
 * double emission live in NET's bring-up command — the deferred
 * link). Call at the TTT module's Run() entry (per-launch state),
 * not at boot. */
TTS_EnumStatus_t TTT_SESSION_enumInit(void);

/* ---- task ----
 * ABSORPTION: the NET pump call is REMOVED (the NET task owns the
 * wire). What remains is the DRAIN — the session's event
 * consumption, HUB context. Call every module-loop pass. */
TTS_EnumStatus_t TTT_SESSION_enumTask(void);

TTS_EnumStatus_t TTT_SESSION_enumGetEvent(TTS_Event_t *Copy_pstEvent);
const TTS_State_t * TTT_SESSION_pcstGetState(void);

TTS_LinkState_t TTT_SESSION_enumGetLinkState(void);
u8              TTT_SESSION_u8LinkLive(void);

TTS_EnumStatus_t TTT_SESSION_enumProbeStatus(void);

u32 TTT_SESSION_u32GetDropped(void);
u32 TTT_SESSION_u32GetJunkLines(void);
u32 TTT_SESSION_u32GetIdleTicks(void);
u32 TTT_SESSION_u32GetLateReplies(void);
u8  TTT_SESSION_u8GetFotaDropped(void);   /* ABSORPTION: FOTA-family events
                                             routed away at the consume entry */

u8  TTT_SESSION_u8HasCredential(void);
u8  TTT_SESSION_u8IsHost(void);
void TTT_SESSION_voidGetCredential(char *Copy_chCodeOut);
void TTT_SESSION_voidInvalidateCredential(void);

TTS_EnumStatus_t TTT_SESSION_enumHost(void);
TTS_EnumStatus_t TTT_SESSION_enumJoin(const char *Copy_chCode);
TTS_EnumStatus_t TTT_SESSION_enumReconnect(void);
TTS_EnumStatus_t TTT_SESSION_enumSendMove(u8 Copy_u8Cell);
TTS_EnumStatus_t TTT_SESSION_enumRequestState(void);

TTS_EnumStatus_t TTT_SESSION_enumLeaveRoom(void);

#endif /* TTT_SESSION_H_ */
