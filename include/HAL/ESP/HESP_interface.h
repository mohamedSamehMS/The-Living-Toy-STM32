/*
 * HESP_interface.h — PUBLIC API of the ESP8266 link HAL (L1 v1).
 *
 *  TWO PROVENANCE LINES, load-bearing:
 *    [BENCH] Transport (RX ring + line assembler + callbacks) and the
 *            TX builder are adopted VERBATIM from the validated
 *            STM32<->ESP link bench (USART-agent deliverables 1 & 3),
 *            with Patch 0 (junk handler), the D10 separator fix, and
 *            Patches A (TxSnapshot) / B (junk content) folded in as
 *            specified. All USART-agent cuts.
 *    [GAME]  The production parser / transaction manager is NOT part
 *            of HESP: it registers BEHIND the HESP_LineHandler_t hook
 *            (a consumer) and is accepted by reproducing the bench
 *            reference handler's outputs on the frozen vector set.
 *
 *  FOTA-CS (this revision): THE CARRIER EXTENSION — the frozen
 *  contract's first, per the grammar ruling (NET owns every wire
 *  mode; HESP is the carrier; review-gated, never a silent patch).
 *  FOUR additions, nothing else touched:
 *    - a RAW-BYTE consumer type + setter (loop context)
 *    - the binary-mode flag (NET sets/clears it)
 *    - a public one-byte raw TX (the ACK/NAK path)
 *    - the last-byte timestamp getter (NET's epoch watchdog clock)
 *
 *  CONTEXT CONTRACTS:
 *    - HESP_TransportPoll / all TX calls / HESP_TxSnapshot: LOOP
 *      (thread) context ONLY.
 *    - The line handler, junk handler, and FOTA-CS raw handler run in
 *      LOOP context (called from inside TransportPoll) — consume/copy
 *      before returning (invariant 5 rides for lines).
 *    - HespHead is written only in ISR context, HespTail only in the
 *      loop: SPSC, no critical sections anywhere (invariant 1).
 *
 *  TX: one in-flight build, no nesting (invariant 3). Thread-context
 *      blocking: ~1.1 ms per 13 B, worst legal frame ~14.6 ms.
 *      HESP_TxRawByte: ~87 us per byte (one byte, blocking).
 */

#ifndef HESP_INTERFACE_H_
#define HESP_INTERFACE_H_

#include "STD_TYPES.h"
#include <MCAL/USART/MUSART_interface.h>

/* ---- handler types (loop context) ---- */
typedef void (*HESP_LineHandler_t)(const u8 *Copy_pu8Line, u16 Copy_u16Len);

/* Patch B: the junk callback also receives the discarded line's
 * CONTENT — valid Copy_u16Length bytes, NOT NUL-terminated (in the
 * cap branch Length == 192 and the buffer is full). Content exists
 * for narration/diagnosis only; the frozen A1 gate compares LENGTHS
 * only — what the handler RECORDS is the handler's business.        */
typedef void (*HESP_JunkHandler_t)(const u8 *Copy_pu8Line, u16 Copy_u16Length);

/* FOTA-CS: the binary-epoch byte consumer. LOOP context — called
 * from TransportPoll's ring drain (the FOTA-12 ruling: loop, not
 * ISR — the handler may narrate on the console, which fills the
 * [TX]-mirror gap with receiver-side bench evidence). While binary
 * mode is on AND a handler is registered, every received byte is
 * routed here INSTEAD of the assembler — the 0x0A/0x0D/charset/caps
 * collisions die by construction (the bracket's whole point).
 * NULL handler (the default) = binary mode routes to the assembler
 * unchanged — the safe pre-integration default.                    */
typedef void (*HESP_RawByteHandler_t)(u8 Copy_u8Byte);

/* ---- TX status (transport RX has no error surface by design:
 *        junk/drop counters instead) ---- */
typedef enum
{
    HESP_OK          = 0,
    HESP_TX_OVERFLOW = 1,   /* content > 158: frame POISONED, never truncated */
    HESP_TX_ARGS     = 2,   /* prefix/TID misuse; Send with nothing built      */
    HESP_TX_MUSART   = 3,   /* MUSART_SendByteBlocking failed mid-frame        */
    HESP_NOT_INIT    = 4    /* link not bound yet                              */
} HESP_Error_t;

/* ---- RX transport ---- */

/* Registers both callbacks + arms RX (RXNE+IDLE pair) on the USART.
 * Call AFTER: pins + MNVIC priority + MUSART_Init (the boot contract).
 * Also binds the TX link to the same instance (deliverable-3 patch). */
void HESP_TransportInit(MUSART_USART_t Copy_USART, HESP_LineHandler_t Copy_pfHandler);

/* Super-loop: drain ring -> assembler -> handler. Cheap when idle;
 * call every pass. FOTA-CS: while binary mode is on and a raw
 * handler is registered, the drain routes to the raw handler
 * instead — the assembler state never moves, so D18 self-disables
 * (and is additionally gated explicitly, belt-and-braces). */
void HESP_TransportPoll(void);

/* Junk announcements (Patch 0 + Patch B): assembler-level discards,
 * delivered with the discarded content. NULL (the default) = behavior
 * identical to the pre-patch skeleton. */
void HESP_SetJunkHandler(HESP_JunkHandler_t Copy_pfHandler);

/* FOTA-CS: register the binary-epoch byte consumer (see the type).
 * Register ONCE at integration init, BEFORE any session — the
 * READY-parse race (the ESP switches its side to binary immediately
 * after sending RESP_FOTA_READY) means bytes can arrive between
 * NET's mode flip and the receiver's arming; the consumer is
 * expected to drop-until-armed (the sender's first-frame retry +
 * DRAIN/VALIDATE absorbs it — the designed self-heal). */
void HESP_SetRawByteHandler(HESP_RawByteHandler_t Copy_pfHandler);

/* FOTA-CS: the binary-mode flag. NET owns it (the ruling); callers:
 * NET's parser (RESP_FOTA_READY — the earliest loop-context point,
 * the race above) and NET's close paths (the DONE terminal, the
 * epoch watchdog, the DONE pending-timeout). Loop context only. */
void HESP_SetBinaryMode(u8 Copy_u8Enable);

/* FOTA-CS: NET's epoch-watchdog clock — the ISR-written last-RX-byte
 * timestamp (D18's variable, unchanged by the extension: the ISR and
 * the ring are byte-agnostic and keep running in binary mode).
 * Aligned u32 read — atomic. */
u32 HESP_GetLastByteMs(void);

/* Soak instrumentation */
u32 HESP_GetDropped(void);      /* ring-full events (ISR-counted)   */
u32 HESP_GetJunkLines(void);    /* assembler junk (both branches)   */
u32 HESP_GetIdleTicks(void);    /* IDLE-after-data events (diag)    */

/* TEST ONLY (config-gated): feeds the assembler directly. */
void HESP_DebugInjectByte(u8 Copy_u8Byte);

/* ---- TX builder ---- */
void         HESP_TxBindLink (MUSART_USART_t Copy_USART);
HESP_Error_t HESP_TxBegin    (const char *Copy_pchName, u8 Copy_u8Tid);
HESP_Error_t HESP_TxFieldStr (const char *Copy_pchField);
HESP_Error_t HESP_TxFieldU16 (u16 Copy_u16Value);
HESP_Error_t HESP_TxSend     (void);

/* Patch A (D11 closure): byte-true gate for the TX builder. Copies
 * the built frame CONTENT (CRLF is appended only inside TxSend) into
 * the caller's buffer. Call between the LAST field call and
 * HESP_TxSend — or instead of it (the next Begin abandons the build
 * by design). Loop context only. Returns the copied length.         */
u16 HESP_TxSnapshot(u8 *Copy_pu8Out, u16 Copy_u16Max);

/* FOTA-CS: one raw byte on the bound link — the epoch's ACK/NAK
 * path. Bind-checked, blocking (~87 us), loop context. NO [TX]
 * mirror (the documented mirror gap: the mirror lives in the text
 * TxSend path only — the receiver's USART2 narration is the epoch's
 * bench evidence instead). The FOTA receiver's raw-TX pointer wraps
 * this (signature adapter at the orchestrator). */
HESP_Error_t HESP_TxRawByte(u8 Copy_u8Byte);

/* TID convention: 0 = no TID (EVENT frames). The builder's misuse
 * guards refuse EVENT+TID, CMD/RESP without TID, and unknown
 * prefixes — it cannot emit a frame the receiver's parser junks. */

#endif /* HESP_INTERFACE_H_ */
