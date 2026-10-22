# Project 4 — STM32↔ESP8266 Layer 1 Grammar & Vocabulary Spec

Version:  v1.1 — FROZEN (countersigned by both seats; the ESP seat's
          receipt of Amendment Sheet A completes the freeze)
This is:  the canonical "grammar docs" — consumed by the bench, both
          seats, and the merge build.
Provenance: origin draft (design era, pre-first-flash) → frozen
          contract + amendments (silicon-proven through A2) → v1.1
          consolidation + Amendment Sheet A (this document).
Markers:  [FROZEN] · [AMENDED] · [ADDED] governed addition ·
          [RESERVED] no v1 emitter · [PHASE-2] drafted, deferred ·
          [OPEN] pending disposition.

## 1. Physical [FROZEN]
115200 8N1. Bench: PA9→ESP RX (GPIO3), ESP TX (GPIO1)→PA10, common
GND. Final: Serial.swap() → GPIO15 TX / GPIO13 RX, USB for flashing.
PA9 pulled during ESP terminal-feeding and reflash. ~100 ms of
74880-baud ROM garbage precedes every ESP reset — contracted.

## 2. Frame Form [FROZEN]
TYPE_NAME:TID:field,field,... + CRLF. LINE_MAX 192; max legal frame
160 incl CRLF (content 158); MAX_FIELDS 12. Over-length lines dropped
whole, counted — never truncated. Both ends refuse truncated
emissions (ESP drop-on-overlength; STM32 poison-then-refuse).

## 3. Types, Direction, Concurrency [FROZEN]
CMD_ STM32→ESP · RESP_ ESP→STM32 · EVENT_ ESP→STM32, except
EVENT_STM_READY (the one legal inbound EVENT_). Wrong-direction
frames: counted, skipped, silent — the loop-breaker (Run B, 450+).
Cross-type CMDs may be in flight together, TID-matched; same-type
second CMD while pending → BUSY.

## 4. TID [FROZEN]
Digits only, mandatory on CMD_/RESP_, ≤8 chars to parse; u8 on the
STM32 builder (≤3 digits, wraps 255). ESP copies verbatim, never
generates. Bad TID → malformed, counted, skipped.

## 5. Structural Parsing [FROZEN + AMENDED]
FIRST ':' ends the name; SECOND ':' ends the TID; the remainder
comma-splits, ':' legal as field data from the third structural
colon. Empty fields legal (interior + trailing); splitters preserve
empties. Empty lines (rxLen==0): skipped silently, uncounted, both
sides. Whitespace-only: parse-fail → malformed, both sides. '\r':
ESP drops at accumulation; STM32 strips one trailing '\r', rejects
interior 0x0D — consumer tolerance is a strict superset of the
producer rule; divergence is garbage-class only. CRLF the only
terminator.

## 6. Field Charset [codified, enforced]
Producer: printable 0x20-0x7E; ',' excluded; ':' legal; '\r' never
legal field data. Builder guard: TxFieldStr refuses violations
(HESP_TX_ARGS) — F4. [OPEN, merge] ESP-side sanitizer for L2-sourced
free text (opponent_name, scan SSIDs).

## 7. Validation Order [silicon-proven]
grammar → name known? (unknown CMD_ → RESP_ERR:tid:UNSUPPORTED) →
arity (BAD_ARGS) → state (NOT_READY) → dispatch.

## 8. RESP_ERR Tokens [FROZEN — eleven entries]
TEN v1 emitters: NO_WIFI NO_NET SERVER_ERR BAD_ARGS UNSUPPORTED
NOT_READY ROOM_NOT_FOUND ROOM_FULL BUSY TIMEOUT. Plus AUTH_TIMEOUT
[RESERVED] — no v1 producer (every credential failure at the L2
boundary is UNKNOWN_ROOM → ROOM_NOT_FOUND, silicon-observed T3/T4;
room code + token are the credentials). Reserved tokens activate
only by governed addition. TIMEOUT = pending-table deadline expiry,
the generic L1 transaction timeout. Branch on code, never message.

## 9. Terminal-Frame Invariant [FROZEN — progress frames restored
## from the origin draft]
Every CMD → exactly one terminal frame (documented RESP or
RESP_ERR:tid:TOKEN). Progress frames may precede it; the terminal is
detected by frame NAME, never by first-response; progress frames are
TID-routed and non-terminal. RESP_ERR is always terminal. Pending
table + timeouts live above the driver (vocabulary layer).

## 10. Command Set v1 [twelve, silicon: ESP v0.1.4]
Response naming principle: every response carries RESP_ plus the
issuing command's family stem; one documented exception — the
dual-role RESP_MATCH_STATE, terminal for both STATE_GET and
ROOM_RECONNECT.

WiFi:
- WIFI_CONNECT:ssid,password (2) → progress RESP_WIFI_CONNECTING:tid
  → terminal RESP_WIFI_CONNECTED:tid:ip or RESP_ERR. Wrong password
  and wrong SSID both end as deadline expiry — indistinguishable
  without scan correlation (AP_NOT_FOUND retired; WIFI_SCAN
  correlation is the diagnostic path). Non-blocking; deadline 30 s;
  one-retry flow is a merge-design option.
- WIFI_DISCONNECT (0)
- WIFI_SCAN (0) → progress RESP_WIFI_SCAN_PART:tid:idx,total,ssid,
  rssi,ch,auth ×N → terminal RESP_WIFI_SCAN_END:tid:count or RESP_ERR
- RADIO_OFF/ON (0)
- NO_WIFI = not associated · NO_NET = associated, socket never up ·
  SERVER_ERR = L2 error/timeout

Rooms:
- ROOM_CREATE:game,name (2) → RESP_ROOM_CREATED:tid:code,token,mark
  [ttl_s dropped — no L2 source]
- ROOM_JOIN:code,name (2) → RESP_ROOM_JOINED:tid:mark,your_turn,token
  [game dropped — same rule]
- ROOM_RECONNECT:code,token (2) → terminal RESP_MATCH_STATE or
  ROOM_NOT_FOUND
- STATE_GET (0) → RESP_MATCH_STATE

Moves:
- MOVE_SEND:move (1) → RESP_MOVE_OK:tid:move,mark or
  RESP_MOVE_REJECTED:tid:REASON. Never blind-retry — STATE_GET,
  resend only if unapplied.

Link:
- STATUS (0) → RESP_STATUS:tid:wifi_state,ip,rssi,socket_state,
  pong_age_s,uptime_s — WIFI_DOWN|JOINING|UP, SOCKET_DOWN|JOINING|UP,
  pong_age 999=never, uptime live (mask in compares)
- DEBUG:0|1 (1) → RESP_DEBUG:tid:state [ADDED, governed, the
  addition model]

Usage: USAGE_TOTALS — oracle-implemented only, not in the twelve;
real silicon answers UNSUPPORTED (pre-declared both sides). Phase-2.

Superseded from the origin draft: NET_CONNECT (absorbed into the
room flow at the merge); MATCH_JOIN/match_id/player_id (rooms
supersede: code, token, mark, seat-as-credential).

## 11. Event Set
- EVENT_ESP_READY:proto,fw [FROZEN] — every boot, first frame after
  garbage; proto==1 exact, fw parsed never byte-compared. N1=120 ms
  (85 ms second sample), N2=14470 ms; WAITING_FOR_LINK ≈ 3.4 s.
- EVENT_STM_READY:proto,fw [ADDED exception; ancestor: the STM32_UP
  banner] — consumed silently. D15: emitted twice, gap ≥ 80 ms,
  copy #1 sacrificial, copy #2 clean.
- EVENT_NET_LOST/NET_BACK [FROZEN] — spontaneous, edge-triggered.
- Merge mappings: EVENT_OPPONENT_MOVE:move,mark · OPPONENT_JOINED:
  name,your_turn · OPPONENT_LEFT:reason,grace_s · MATCH_ENDED:
  winner,reason. [OPEN] OPPONENT_REJOINED / ROOM_EXPIRED have no L1
  events — phase-2 disposition. Events carry no TID.

## 12. RESP_MATCH_STATE [FROZEN — 7 fields]
tid: status,board,your_mark,your_turn,opponent_name,
opponent_connected,winner. WAITING|ACTIVE|FINISHED (UPPER_SNAKE,
case-sensitive; unknown tokens pass through). Board 9 chars
per-cell, all-null "---------"; opponent_name null→'-'.

## 13. Transaction-Layer Rules [final form]
- Routing is strictly TID-keyed. The ESP stamps TIDs exclusively
  from received CMD frames (pending or dispatch-rejected —
  UNSUPPORTED and BAD_ARGS carry the received TID); unsolicited L2
  pushes map exclusively to the EVENT_ class; no uncommanded RESP_
  of any name can arrive in v1.
- The MATCH_STATE pending-exclusion rule (STATE_GET and
  ROOM_RECONNECT never simultaneously pending) lives ESP-side, in
  the translator — it closes the stamping ambiguity at its source.
- Late replies (RESP_ whose TID matches no live transaction =
  post-deadline arrival): count-and-skip — never a stale route,
  never silent.
- TID discipline: never reuse a LIVE TID; timed-out TIDs are
  quarantined until u8 wrap forces reuse (deadlines ≤ 30 s; wrap
  rotation takes minutes — quarantine-until-wrap is free and total).

## 14. Phase-2 Families [drafted, not in v1]
- Leaderboard (draft: SCORE_SUBMIT, LB_GET → RESP_LB rows): the
  second multi-frame consumer; the progress-frame model generalizes.
- Usage/parental (draft: USAGE_REPORT period_s,play_s,per-game;
  EVENT_PARENTAL_LOCK/UNLOCK; EVENT_TIME_LIMIT): both usage forms
  recorded (draft's USAGE_REPORT and the oracle's USAGE_TOTALS);
  disposition at the control-plane phase. Parental events are
  must-not-be-lost — the ACK-vs-reconciliation question stands
  there (for moves, server authority + STATE heal settled it for
  reconciliation; the draft's ParsePing gap-check was the
  precedent).

## 15. No Checksum [standing]
Tolerance = echo + reject + authority chain. Binary framing + CRC +
Python cross-check queued before FOTA.

## 16. Stuck-Line Recovery [D18, live both sides]
ESP flush 50 ms; STM32 30 ms — un-harmonized by design. Counters
attributed separately. Boot garbage may carry 0D 0A — newline-junk
expected.

## Amendment History
- Origin draft — design era, pre-first-flash (128-byte buffer era,
  STM32_UP banner, TID undecided, match_id model, 7 tokens)
- Frozen contract — colon amendment, TID-mandatory, empty fields,
  EVENT_STM_READY exception, wrong-direction loop-breaker (Run B),
  LINE_MAX 128→192, tokens → ten
- v0.1.4 silicon (A2) — CMD_DEBUG governed addition; D18 flushes
  both sides
- v1.1 + Amendment Sheet A — progress frames restored from the
  draft; F4 field charset; F1 empty-line semantics; TIMEOUT adopted
  + AUTH_TIMEOUT reserved (eleven); SCAN family names restored (the
  one contested delta); WiFi deadline 30 s; ttl_s/game dropped;
  transaction-layer rules (§13)








  EDIT 1 — the header status line:
OLD: Version:  v1.1 — FROZEN (countersigned by both seats; the ESP
     seat's receipt of Amendment Sheet A completes the freeze)
NEW: Version:  v1.1 — FROZEN. Two-witness record complete: contract,
     amendments, diff, one contest, resolution, freeze (ESP-seat
     freeze signature received).

EDIT 2 — the §10 naming principle:
OLD: Response naming principle: every response carries RESP_ plus
     the issuing command's family stem; one documented exception —
     the dual-role RESP_MATCH_STATE, terminal for both STATE_GET
     and ROOM_RECONNECT.
NEW: Response naming principle: every documented success-path
     response carries the issuing command's family stem.
     Exceptions, named and closed: RESP_ERR (the generic terminal
     — no family stem, fires for any command) and RESP_MATCH_STATE
     (dual-role, terminal for both STATE_GET and ROOM_RECONNECT).
     No family-stemmed error variants (RESP_WIFI_ERR-class) are
     legal.