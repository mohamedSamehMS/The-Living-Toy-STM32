/*
 * HESP_config.h — tunables of the ESP8266 link HAL (L1 v1).
 *
 *  RX sizing rule (invariant 4): rate (11520 B/s at 115200 8N1) x
 *  worst loop stall. 1024 covers all realistic v1 bursts; a full-
 *  rate blast through the 270 ms NEC stall (~3.1 KB) is protocol
 *  misbehavior the DROP COUNTER should announce, not absorb.
 */

#ifndef HESP_CONFIG_H_
#define HESP_CONFIG_H_

#include <MCAL/USART/MUSART_interface.h>   /* HESP_TX_MIRROR_USART     */

/* ---- RX transport (adopted skeleton constants) ---- */
#define HESP_RING_SIZE          1024u   /* power of 2 MANDATORY (mask) */
#define HESP_RING_MASK          (HESP_RING_SIZE - 1u)
#define HESP_LINE_MAX           192u    /* line buffer cap             */
#define HESP_CONTENT_MAX        158u    /* spec: 160 incl. CRLF        */

/* ---- TX builder ---- */
#define HESP_TX_CONTENT_MAX     158u
#define HESP_TX_BUFFER_MAX      160u    /* content + CRLF              */
#define HESP_TX_MIRROR_ENABLE   1       /* [TX] narration to console   */
#define HESP_TX_MIRROR_USART    MUSART_USART2   /* PA2/PA3 debug       */

/* ---- test-only injection path ----
 * Stays 1 in bench builds: the SINGLE-IMAGE bench gates injection at
 * RUNTIME (RUN A feeds it directly); production code never arms it.
 * The link-time-loud property is traded for no-reflash by design —
 * the runtime refusal replaces it.                                   */
#define HESP_DEBUG_INJECT_ENABLE 1

/* D18: stuck-line flush. Partial/overflow line + no bytes this long
   = junk + reset. Covers boot-garbage bursts (74880-baud spew exceeds
   the 192 cap, no newline): without it, the discard window swallows the
   first real frame after the burst. Never fires mid-frame (byte gaps
   ~87 us); fires before READY (gap >= ~40 ms). Mirrors ESP v0.1.4. */
#define HESP_IDLE_FLUSH_MS   30u

#endif /* HESP_CONFIG_H_ */
