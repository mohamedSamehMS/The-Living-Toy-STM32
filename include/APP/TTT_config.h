/*
 * TTT_config.h — ALL user-tunable settings for Tic-Tac-Toe.
 *  LOCATION: APP/ (flat).
 *
 *  CONSUMERS (who includes this file, and for what):
 *    TTT_renderer.c — zone 2 only (palette + strokes), via its
 *                     local aliases. Geometry NEVER lives here —
 *                     it is the renderer header's frozen contract.
 *    main.c         — zones 1, 2, 4 (the offline game + the online
 *                     state machine's UX timing).
 *    TTT_session.c  — zone 4's NAME only (the M4 wrappers pass it
 *                     as the player-name field on the wire).
 *
 *  STAGE 1 OF THE RESTRUCTURE, this revision:
 *    - The file narrows to its two real consumers: the GAME layer
 *      (zones 1-2) and the SESSION layer's UX timing (zone 4).
 *      Everything protocol / connection moved DOWN to
 *      APP/NET/NET_config.h (S5 — constants follow their consumer).
 *    - The TTS_ONLINE_* shadow block (11 macros, dead since
 *      delivery, stale pre-merge values — the A2 landmine) is
 *      DELETED (M8). Ledger at the bottom of zone 4.
 *    - Every SURVIVING name and value is byte-identical to the
 *      delivered file. Comments are the only additions; deletions
 *      are the only removals. Zero behavior change by construction.
 *
 *  History: v1.2's input integration replaced the demo's X-vs-O
 *  skill pair with the single AI knob and retired zone 3 with the
 *  demo driver; Stage 1 narrows per S5.
 *
 *  Zones: 1. GAMEPLAY (offline AI opponent, pacing)
 *         2. VISUAL   (palette, strokes, blink)
 *         3. FEATURES — retired; future: hub-settings flags
 *         4. ONLINE   — the session layer's UX timing (all ms).
 *            Protocol/connection constants are NOT here anymore:
 *            see APP/NET/NET_config.h.
 */

#ifndef TTT_CONFIG_H_
#define TTT_CONFIG_H_

#include "STD_TYPES.h"
#include <HAL/TFT/HTFT_interface.h>

/* =====================================================================
 *  1. GAMEPLAY — the OFFLINE opponent. (The online game needs
 *     nothing from this zone: the remote player IS the opponent.)
 *
 *  v1 roles are FIXED: human = X, moves first; AI = O. Runtime role /
 *  difficulty selection arrives with the hub settings menu — these
 *  values graduate to a RAM struct then, and the layout is designed
 *  for that migration: one zone, one struct, no scatter.
 * ===================================================================== */

/* AI = O. Percent chance the AI takes its best (tier-1 rules) move;
 * otherwise a random free cell. 40 = beatable-but-competent — the
 * child-friendly default: a child should win more often than not,
 * but not always. Raise for a harder opponent (100 = the pure
 * rules player: blocks + center + corners).
 * Consumer: Game_u8AiChoose(), the offline AI turn.                */
#define TTT_CFG_AI_SKILL_PCT         40u

/* Visible "thinking" delay before the AI move lands — the beat that
 * makes the opponent feel deliberate rather than instant (a reply
 * in the same frame as the human's move reads as mechanical).
 * Consumer: GAME_ST_AI's elapsed-comparison timer — no blocking
 * delay: the free-running loop keeps servicing IR, UART, and the
 * heartbeat throughout. Paces the OFFLINE game only.               */
#define TTT_CFG_AI_THINK_MS          800u

/* =====================================================================
 *  2. VISUAL / RENDERER — palette, strokes, blink. What the painters
 *     use; nothing the logic does. Geometry is NOT here (the
 *     renderer header's frozen contract facts: cell size, origin,
 *     piece span/radius). Consumer: TTT_renderer.c through its
 *     local aliases — this file is the single source of truth for
 *     every color and size below.
 * ===================================================================== */

#define TTT_CFG_COLOR_BG            HTFT_COLOR_BLACK     /* the erase color: PaintBoard's flood AND PaintCell's full-rect wipe */
#define TTT_CFG_COLOR_GRID          HTFT_COLOR_LIGHTGRAY /* the 4 interior grid lines                          */
#define TTT_CFG_COLOR_X             HTFT_COLOR_CYAN      /* X pieces, X's score cell, X's status lines         */
#define TTT_CFG_COLOR_O             HTFT_COLOR_YELLOW    /* O pieces, O's score cell, O's status lines         */
#define TTT_CFG_COLOR_TEXT          HTFT_COLOR_WHITE     /* neutral status text (DRAW, prompts, footers)       */
#define TTT_CFG_COLOR_SCORE_D       HTFT_COLOR_LIGHTGRAY /* the draws counter + the dim "WAITING FRIEND.."     */
#define TTT_CFG_COLOR_WINLINE       HTFT_COLOR_WHITE     /* the win strike, ON phase                           */
#define TTT_CFG_COLOR_WINLINE_DIM   HTFT_COLOR_DARKGRAY  /* the win strike, OFF phase — the blink is a COLOR-PULSE, never a background erase (survives overlay: the drawing law) */
#define TTT_CFG_COLOR_CURSOR        HTFT_COLOR_YELLOW   /* NEW (input integration): the 33x33 selection ring. Drawn OVER any cell state; erased by PaintCell's full repaint — the renderer's header contract. */
#define TTT_CFG_COLOR_TAKEN         HTFT_COLOR_RED      /* NEW: the alert line — TAKEN!, NET ERROR, FRIEND LEFT?, LINK LOST */

#define TTT_CFG_X_STROKE_PX         3u    /* X: three offset diagonal passes over span TTT_PIECE_SPAN          */
#define TTT_CFG_O_STROKE_PX         3u    /* O: three concentric circles, r = TTT_O_RADIUS..+2 — matches X's visual weight */
#define TTT_CFG_WINLINE_HALF_STROKE 2u    /* strike boldness: x- AND y-offset passes, +/- this -> same bold width on all 8 win lines (the uniform-stroke fix) */
#define TTT_CFG_BLINK_MS            150u  /* WIN_FLASH half-phase                                              */
#define TTT_CFG_BLINK_COUNT         3u    /* WIN_FLASH blinks: 6 half-phases x 150 ms = ~900 ms, then the end screen */

/* =====================================================================
 *  3. FEATURES — retired with the demo driver (auto-restart is gone:
 *  the real game waits for CONFIRM/BACK). Future: hub-settings flags
 *  (per-game difficulty, multiplayerEnabled[], irRemoteEnabled —
 *  the settings-menu era). Nothing here until then.
 * ===================================================================== */

/* =====================================================================
 *  4. ONLINE — the SESSION layer's UX timing. All times in ms.
 *
 *  The S5 split, restated: everything that times the PROTOCOL or
 *  the CONNECTION lives in APP/NET/NET_config.h (command timeout,
 *  D15 gap, queue depths, firmware id). This zone times the USER —
 *  how long screens wait, how often they poll, how long messages
 *  hold. Stage-1 consumer: main.c's state machine (NET_WAIT /
 *  LOBBY / HOST_WAIT / JOIN_ENTRY / ON_WAIT / ON_GRACE / EXIT_ASK).
 *  At Stage 2 the consumers move into TTT_session.c / TTT_game.c —
 *  the VALUES stay here; only the callers change.
 * ===================================================================== */

/* Player identity on the wire: field 2 of CMD_ROOM_CREATE and
 * CMD_ROOM_JOIN (the M4 wrappers in TTT_session.c pass it) — and it
 * arrives back as the opponent's view: EVENT_OPPONENT_JOINED's name
 * field and RESP_MATCH_STATE's oppname. CONSTRAINTS: keep it <= 20
 * chars (the OppName[21] / Name[21] bounds; longer truncates via
 * CopyBounded, silently), and the ESP-side F7 free-text sanitizer
 * rejects ',' ':' '"' — plain alphanumerics only.                  */
#define TTT_ONLINE_NAME             "PLAYER"

/* NET_WAIT bound — the 30 s frozen link-spec deadline. TWO jobs in
 * main.c: (a) NET_WAIT's NO CONNECTION deadline; (b) the HOST_WAIT /
 * JOIN_ENTRY retry window — a NOT_READY-class terminal after
 * t0+30 s fails to the NO CONNECTION screen instead of retrying
 * again. Calibration: ESP boot->connected ~9 s typical, 23.7 s worst
 * observed (the late-AP case) — the bound absorbs the worst with a
 * retry's worth of headroom. REGISTERED INTERACTION (B1): with NET's
 * 20 s command timeout, a SILENT first attempt leaves the retry
 * <= 7 s of this window — accepted as designed; the deadline stays
 * correct either way.                                              */
#define TTT_ONLINE_CONNECT_MS       30000u

/* ---- moved DOWN in Stage 1 (S5) — tombstone: ---------------------
 * TTT_ONLINE_CMD_TIMEOUT_MS 20000u  -> NET_CMD_TIMEOUT_MS
 * (APP/NET/NET_config.h). The full Q3 consultation rationale moved
 * with it verbatim — do not restate it here; one home per rule.
 * ------------------------------------------------------------------ */

/* HOST_WAIT state poll: while waiting for the friend, ask the server
 * for room state (CMD_STATE_GET) every 10 s. Purpose: room-expiry
 * DISCOVERY — if the room died server-side, the poll's answer is
 * RESP_ERR:ROOM_NOT_FOUND -> ROOM_GONE -> the M1 invalidation (in
 * the session) -> lobby. Discovery latency = this cadence + one
 * RTT. The OPPONENT_JOINED transition itself rides the unsolicited
 * push (the fast path); this poll is the safety net.               */
#define TTT_ONLINE_WAIT_POLL_MS     10000u

/* ON_WAIT silence timeout: our turn to WAIT, no opponent move for
 * 30 s -> ON_GRACE ("FRIEND LEFT?"). A HEURISTIC prompt, not a
 * verdict — the opponent's real liveness is referee truth; this
 * only opens the grace window early enough to rejoin inside it.
 * Deliberately generous vs real turns (RTT 1.1-1.7 s typical): a
 * slow human is not a dead one. Worst path: 30 s here + the grace
 * window below = the "FRIEND LEFT" screen.                          */
#define TTT_ONLINE_TURN_TIMEOUT_MS  30000u

/* Rejoin grace window (default). In ON_GRACE the referee holds the
 * room; the opponent may reconnect. TWO resume triggers (Rev 5,
 * B4): canonical = EVENT_OPPONENT_REJOINED (bare) -> the STATE
 * chase (CMD_STATE_GET resyncs the board); belt-and-braces =
 * EVENT_OPPONENT_MOVE during grace (a move is ground-truth
 * presence). OVERRIDE: EVENT_OPPONENT_LEFT carries the server's
 * grace seconds (field 2) — used INSTEAD of this default when it
 * arrives sane (1..60). Expiry with no resume -> "FRIEND LEFT" +
 * ON_END + the counter narration.                                    */
#define TTT_ONLINE_REJOIN_MS        60000u

/* Grace poll cadence: CMD_STATE_GET every 2 s while ON_GRACE — room
 * existence + the resync source (a STATE reply with MyTurn re-seats
 * the game even without REJOINED). Faster than the HOST_WAIT poll
 * on purpose: the grace window is the shortest decision-bearing
 * window in the whole flow.                                         */
#define TTT_ONLINE_GRACE_POLL_MS    2000u

/* Exit-confirm window: BACK mid-match -> "EXIT? 0 AGAIN"; a second
 * BACK inside 5 s confirms the exit to the lobby; any other key or
 * the timeout returns to the game (ExitPrev restores ON_TURN /
 * ON_WAIT). Rationale: a stray press must not kill a live match —
 * the exit is a deliberate two-beat action (a children's device).
 * Change-set-B note: once the ROOM_LEAVE vocabulary lands, the
 * confirmed exit gains its teardown emission — this window stays
 * the trigger either way.                                           */
#define TTT_ONLINE_EXIT_ASK_MS      5000u

/* Transient message hold: how long a status-line message holds on
 * the JOIN_ENTRY screen ("ROOM NOT FOUND" / "ROOM FULL" / a raw
 * RESP_ERR reason) before the state repaints the prompt. Consumer:
 * the TakenFlag==2 branch. (The in-game "TAKEN!" is TakenFlag==1 —
 * cleared by the next cursor move, not by time.)                    */
#define TTT_ONLINE_MSG_MS           3000u

/* ---- moved DOWN in Stage 1 (S5) — tombstone: ---------------------
 * TTT_ONLINE_FW "1.0"  -> NET_FW_ID (APP/NET/NET_config.h).
 * Kept "1.0" so the Phase-1 [TX] capture diffs byte-identical —
 * the id bumps at the next VERSIONED image, never mid-restructure.
 * ------------------------------------------------------------------ */

/* NOT_READY-class auto-retry cadence: when a room command's terminal
 * is the NOT-READY CLASS (NOT_READY / NO_WIFI / NO_NET / BUSY —
 * Game_u8IsNotReadyClass), the state machine waits this long before
 * re-sending. Terminal-driven, never blind: each retry is answered,
 * each answer decides the next, and the 30 s window bounds the
 * whole ladder. Design (bring-up absorbed in the ESP): the STM32
 * LEARNS the ESP's readiness from these reply classes instead of
 * polling for it — the retry IS the readiness poll.                */
#define TTT_ONLINE_RETRY_MS         3000u

/* NET_WAIT liveness probe: CMD_STATUS every 1 s until the link is
 * live. WHY a probe at all (Round 5, 7a): ESP v0.1.4 announces READY
 * exactly once at its own boot and never re-announces on
 * EVENT_STM_READY (the boot invariant, one-path) — in the
 * reset-with-ESP-alive power order the announcement is already gone,
 * and the probe is the ONLY thing that closes the link. Cadence
 * note: each probe re-arms the pending transaction; the unanswered
 * ones accumulate in the late counter BY DESIGN — NET_WAIT is
 * bounded by the 30 s window, not by any command timeout. (The old
 * "supersedes the 3 s timeout" comment was stale — the timeout is
 * NET's 20 s now, and the probe's job never depended on it.)       */
#define TTT_ONLINE_NET_POLL_MS      1000u

/* ---- DELETION LEDGER (Stage 1, M8) -------------------------------- *
 * The TTS_ONLINE_* shadow block (11 macros) that ended this file  *
 * is DELETED. Dead since delivery — zero users tree-wide, verified *
 * by inventory — and carrying STALE pre-merge values, worst of    *
 * them TTS_ONLINE_CMD_TIMEOUT_MS = 3000: the exact trap for a     *
 * future editor looking for "the session's timeout" under the     *
 * session's own TTS_ prefix, compiling in silence. Deleting (not  *
 * leaving dead) makes the build SAY so if any consumer outside    *
 * the record's tree ever existed. That loudness is the point.     *
 * ------------------------------------------------------------------ */

#endif /* TTT_CONFIG_H_ */
