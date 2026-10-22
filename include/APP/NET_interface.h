/*
 * NET_interface.h - PUBLIC API of the Network Driver (L1
 * protocol + connection state). LOCATION: APP/NET/.
 *
 *  TWO PROVENANCE LINES, load-bearing:
 *    [MOVE] Everything here is adopted from the delivered
 *           TTT_session (rev 4+fix - the 15-repair revision,
 *           Phase-1-proven on the wire) as PURE MOVES, plus the
 *           [L1] counter narration from main.c v1.3 rev 5. The
 *           marked changes are annotated in NET_program.c:
 *             M1 - the four parser credential sites removed;
 *                  events carry the fields; the consumer writes.
 *             M2 - ROOM_JOINED's event gains Code (the join-arg
 *                  echo).
 *             M3 - the event type is NET_Event_t (the TTS_
 *                  shape, NET_ names); one queue shared by TTT_SESSION and
 *                  APP_FOTA_APP in separate hub dispatch runs.
 *             M4 - Host/Join/Reconnect parameterized: NET
 *                  carries no TTT literals.
 *             M5 - observability additions: two dropped-counter
 *                  getters + the pending-transaction truth.
 *             M6 - the narration moved in from main.c.
 *           Acceptance: the Phase-1 script's [TX] capture diffs
 *           BYTE-IDENTICAL on the restructured tree.
 *    [FUTURE] FOTA / leaderboard / usage vocabulary arrives per
 *           feature: an L1 amendment + referee L2 types FIRST,
 *           then the NET command - never a quiet grammar edit.
 *           The grammar TOLERATES unknown names (silent else);
 *           it does not pre-register them. FOTA's binary
 *           framing is a grammar revision (two-witness), not a
 *           NET patch.
 *
 *  CONTEXT CONTRACTS:
 *    - NET_enumTask / all command calls / NET_voidNarrate-
 *      Counters: LOOP context only, call every super-loop pass.
 *    - NET_enumInit: call AFTER pins + MNVIC + MUSART_Init on
 *      the link USART (HESP's boot contract). It performs the
 *      D15 double EVENT_STM_READY emission - the boot
 *      standard's session-init step, home transferred
 *      TTT_session -> NET (the ruling's subject - never HESP -
 *      unchanged).
 *    - NET_enumGetEvent: drained by TTT_SESSION during TTT Run
 *      and APP_FOTA_APP during UPDATE Run. The hub runs one
 *      dispatch row at a time, so these consumers are in
 *      different modes and do not drain concurrently.
 *    - NET never draws: no TFT call anywhere. Console narration
 *      is diagnostics - the HESP [TX]-mirror precedent.
 *    - No HESP type appears in this header: consumers never
 *      learn the carrier exists.
 *
 *
 *  [the existing header comment stands - add to it:]
 *  FOTA-CS (this revision): the FOTA family per the settled Sheet C
 *  skeleton - the bracket's NET half. Additions:
 *    - events: FOTA_AVAILABLE / FOTA_READY / FOTA_DONE / FOTA_WATCHDOG
 *    - the FOTA info sub-struct in NET_Event_t (version/length/e2e/
 *      size-echo - all DECIMAL on the wire, the ESP seat's ruling)
 *    - commands: NET_enumFotaBegin / NET_enumFotaDone (ordinary
 *      pending transactions - the mooted-exemption ruling: BOTH
 *      transactions are short; the standard sweep governs both)
 *    - the epoch: opened IN THE PARSER at RESP_FOTA_READY (the
 *      READY-parse race - see HESP_interface.h); closed at the DONE
 *      terminal, the 90 s watchdog, or the DONE pending-timeout.
 *      NET owns the wire mode (the ruling); the APP-layer orchestrator
 *      (the FOTA seat's glue, a separate file) drives APP_FOTA
 *      through these events - NET never calls into APP_FOTA.
 */


#ifndef NET_INTERFACE_H_
#define NET_INTERFACE_H_

#include "STD_TYPES.h"

/* ---- status ---- */
typedef enum
{
    NET_OK = 0,
    NET_NOK,
    NET_NO_EVENT,
    NET_BUSY          /* reserved for CONSUMER policy - NET itself
                         never returns it (pure move; see
                         NET_u8PendingActive) */
} NET_EnumStatus_t;

/* ---- link state ----
 * LINK_LIVE means TRANSPORT-alive: the ESP speaks. Server-
 * connected truth arrives as reply classes (NOT_READY /
 * NO_WIFI / NO_NET) - the one-way liveness ruling. LINK_LOST
 * is RESERVED: v1 never sets it (pure move); it arrives with
 * change set B (NET_DISCONNECT) or the merged link-health
 * policy. */
typedef enum
{
    NET_LINK_IDLE = 0,
    NET_LINK_CONNECTING,
    NET_LINK_LIVE,
    NET_LINK_LOST
} NET_LinkState_t;

/* ---- room status (the snapshot's first field) ---- */
typedef enum
{
    NET_STATUS_UNKNOWN = 0,
    NET_STATUS_WAITING,
    NET_STATUS_ACTIVE,
    NET_STATUS_FINISHED
} NET_Status_t;

/* ---- the snapshot: RESP_MATCH_STATE's frame-to-struct ---- */
typedef struct
{
    NET_Status_t Status;
    char Board[10];
    u8   MyMark;
    u8   MyTurn;
    char OppName[21];
    u8   OppConnected;
    u8   Winner;
} NET_State_t;

typedef enum
{
    NET_EVT_NONE = 0,
    NET_EVT_LINK_LOST,
    NET_EVT_CMD_TIMEOUT,
    NET_EVT_ROOM_CREATED,
    NET_EVT_ROOM_JOINED,
    NET_EVT_ROOM_GONE,
    NET_EVT_RESP_ERR,
    NET_EVT_MOVE_OK,
    NET_EVT_MOVE_REJECTED,
    NET_EVT_STATE,
    NET_EVT_OPP_MOVE,
    NET_EVT_OPP_JOINED,
    NET_EVT_OPP_LEFT,
    NET_EVT_MATCH_ENDED,
    NET_EVT_REMATCH_OFFERED,
    NET_EVT_REMATCH_STARTED,
	NET_EVT_REJOINED,
	NET_EVT_ROOM_LEFT,
	NET_EVT_NET_DISCONNECTED,
    NET_EVT_LINK_BACK,
    /* FOTA-CS: the FOTA family (Sheet C's settled skeleton) */
    NET_EVT_FOTA_AVAILABLE,   /* the notify: version/length/e2e ride
                                 the Fota sub-struct; the consumer
                                 compares the version LOCALLY against
                                 its build number and decides        */
    NET_EVT_FOTA_READY,       /* RESP_FOTA_READY parsed; the epoch is
                                 OPEN (NET flipped HESP in the parser
                                 - the race); Fota.SizeEcho carries
                                 the echoed chunk size; the consumer
                                 arms the receiver NOW               */
    NET_EVT_FOTA_DONE,        /* the closing terminal; on this event
                                 + a successful status the consumer
                                 performs the reset                  */
    NET_EVT_FOTA_WATCHDOG     /* the 90 s zero-byte backstop fired;
                                 the epoch is closed; DONE:WATCHDOG
                                 was sent (its terminal or timeout
                                 follows on its own)                 */
} NET_EventType_t;



/* FOTA-CS: the FOTA frame fields. Version is STRING, VERBATIM
   (the referee's Ruling 2(a), Sheet C C.1); size and e2e_crc are
   u32 DECIMAL. SizeEcho is RESERVED (C.3 is bare - the negotiation
   deferred; the field returns with a future amendment). */
typedef struct
{
    char Version[16];   /* the string, verbatim from the notify   */
    u32 Length;         /* TRUE size, unpadded                    */
    u32 E2eCrc;         /* the padded-rule CRC                    */
    u16 SizeEcho;       /* RESERVED (unused at C.3-bare)          */
} NET_FotaInfo_t;


typedef struct
{
    NET_EventType_t Type;
    u8   Cell;
    u8   Mark;
    u8   MyTurn;
    u8   Winner;
    u16  GraceS;
    char Code[5];    /* M2: filled on ROOM_CREATED and (NEW) on
                        ROOM_JOINED - the join-arg echo */
    char Token[33];
    char Name[21];
    char Reason[24]; /* RESP_ERR reason VERBATIM (the 13-entry
                        token table passes through); "TX_FAIL" on
                        a local build/send failure */
    char Board[10];
    char Line[4];
    NET_FotaInfo_t Fota;   /* FOTA-CS - at the end: the byte-wise
                                  queue copies use sizeof; rebuild the
                                  tree together (all NET consumers)     */
} NET_Event_t;

/* ---- lifecycle (the RTOS conversion) ---- */

/* Create both queues + spawn the NET task. Call ONCE at boot,
 * PRE-sCHEDULER (from main). The task idles at ~zero cost until the
 * first bring-up command (the pump over an unbound, un-clocked
 * transport is a no-op). Idempotent. */
NET_EnumStatus_t NET_enumSpawn(void);

/* THE DEFERRED LINK: enqueue the bring-up command - USART1 clock +
 * PA9/PA10 AF7 + priority 5 (ruling B) + MUSART_Init + transport
 * init + the D15 double STM_READY (task-paced - the 100 ms gap
 * blocks only the comm lane). Executed IN the NET task; idempotent.
 * Call at the ONLINE-mode entry (the module's mode-menu 1-key),
 * never at boot - the ESP-silent ruling rides this. */
NET_EnumStatus_t NET_enumBringup(void);

/* ---- events (TTT_SESSION / APP_FOTA_APP drain in separate hub modes) ---- */
NET_EnumStatus_t NET_enumGetEvent(NET_Event_t *Copy_pstEvent);

/* ---- the snapshot (read-only) ---- */
const NET_State_t * NET_pcstGetState(void);

/* ---- link state ---- */
NET_LinkState_t NET_enumGetLinkState(void);
u8              NET_u8LinkLive(void);

/* ---- the arbitration TRUTH (M5): policy lives above ----
 * 1 = a transaction is pending. The consumer (session) decides
 * whether to refuse with BUSY or allow-and-abandon (the
 * abandoned reply lands in the late counter - designed). NET
 * itself never refuses: pure move of the delivered behavior. */
u8 NET_u8PendingActive(void);

/* ---- commands (all: loop context; frames identical to the
 *      delivered session's - M4 parameterized, same bytes) ---- */
NET_EnumStatus_t NET_enumProbeStatus(void);
NET_EnumStatus_t NET_enumHost(const char *Copy_pchGame, const char *Copy_pchName);
NET_EnumStatus_t NET_enumJoin(const char *Copy_pchCode, const char *Copy_pchName);
NET_EnumStatus_t NET_enumReconnect(const char *Copy_pchCode, const char *Copy_pchToken);
NET_EnumStatus_t NET_enumSendMove(u8 Copy_u8Cell);
NET_EnumStatus_t NET_enumRequestState(void);


/* ---- FOTA-CS: the bracket's two transactions ----
 * BOTH are ordinary pending commands (the mooted-exemption ruling:
 * fetch-on-notify made both short; the standard sweep governs both).
 * Loop context.
 *
 * BEGIN: sends CMD_FOTA_BEGIN:tid:<version>[,<chunk_size>]. The
 *   chunk-size field is the OPTIONAL proposal (absent = 128, the
 *   proven default; the amendment's 32..1024 negotiation space);
 *   pass 0 to omit. RESP_FOTA_READY is its terminal - and opens
 *   the epoch (NET flips HESP in the parser: the ESP switches its
 *   side to binary immediately after sending READY, so our side
 *   must flip at the earliest loop-context point).
 * DONE: sends CMD_FOTA_DONE:tid:<status_token>. The token is the
 *   RECEIVER's verdict (the direction ruling: the authoritative
 *   verification is the receiver's; RESP_ flows ESP->STM only) -
 *   the registered set: OK / E2E_FAIL / ABORTED (the receiver's
 *   token function maps); NET itself sends WATCHDOG on the
 *   backstop. RESP_FOTA_DONE is the terminal - and closes the
 *   epoch. */
/* Sheet C C.2: the frozen 3-field opening contract - version
 * (string, verbatim), size, e2e_crc - the ESP's consistency-check
 * data; any divergence bounces VERSION_MISMATCH-class. */
NET_EnumStatus_t NET_enumFotaBegin(const char *Copy_pchVersion,
                                   u32 Copy_u32Size, u32 Copy_u32E2eCrc);
NET_EnumStatus_t NET_enumFotaDone(const char *Copy_pchStatusToken);

/* FOTA-CS: the epoch truth (narration / consumer gating). */
u8 NET_u8FotaEpochOpen(void);


/* FOTA-CS Addendum A: the consumer-requested epoch close. The
 * orchestrator calls this when the RECEIVER reaches a terminal
 * status, BEFORE sending CMD_FOTA_DONE - so the closing
 * transaction runs as text on both sides (the ESP is already
 * in text mode after the final ACK; without this close, its
 * RESP_FOTA_DONE lands in the raw handler and is discarded,
 * costing a 20 s timeout and a late-counter entry on every
 * update). Idempotent. Loop context. */
void NET_voidFotaEpochClose(void);


/* ---- counters ---- */
u32 NET_u32GetDropped(void);      /* HESP ring-full (ISR-counted) */
u32 NET_u32GetJunkLines(void);    /* HESP assembler junk           */
u32 NET_u32GetIdleTicks(void);    /* HESP IDLE diagnostics         */
u32 NET_u32GetLateReplies(void);  /* unmatched/abandoned terminals */
u32 NET_u32GetLinesDropped(void); /* M5: line-queue overflow       */
u32 NET_u32GetEventsDropped(void);/* M5: event-queue overflow      */

/* ---- the [L1] counter line to the console (M6) ----
 * Format kept VERBATIM (4 fields) - the bench "counters at row
 * ends" evidence shape is frozen. The M5 counters are
 * getter-only. */
void NET_voidNarrateCounters(void);



NET_EnumStatus_t NET_enumLeaveRoom(void);
u8 NET_u8SocketIsNew(void);   /* B1/Rule 2: the link died & returned, or the ESP
                                 rebooted - RECONNECT is valid; the socket is new
                                 server-side. Cleared by any RESP_MATCH_STATE
                                 (a state reply = the connection is bound). */


/* R5: re-emit the D15 STM_READY pair - the ESP's re-offer trigger. For
 * a consumer re-entering with the link already up: solicits a fresh
 * offer (the ESP only re-offers at STM_READY / reconnect). */
NET_EnumStatus_t NET_enumSolicitOffer(void);

/* R5: the hub-day FRESET - the ESP's stuck-stage recovery, originated
 * on the wire (the bench form's lines, verbatim). The RAM offer
 * survives; the next BEGIN re-fetches clean. */
NET_EnumStatus_t NET_enumEspFreset(void);
#endif /* NET_INTERFACE_H_ */
