/*
 * HESP_private.h — internal definitions. Included ONLY by
 * HESP_program.c. No hardware registers here: HESP owns none (pure
 * consumer of MUSART). This file carries the invariant set — the
 * load-bearing rules of the adopted code — as the review surface.
 */

#ifndef HESP_PRIVATE_H_
#define HESP_PRIVATE_H_

#include "STD_TYPES.h"

/* =====================================================================
 *  THE SEVEN INVARIANTS (from the validated link bench — keep with
 *  the code they govern; removal is a review-flagged change)
 *
 *  1. SPSC: HespHead written ONLY in ISR context, HespTail ONLY in
 *     the loop. Aligned u8/u16 accesses are atomic on Cortex-M4 ->
 *     NO critical sections anywhere. volatile on all ISR-shared
 *     state is mandatory, not decorative.
 *  2. Drop-NEWEST + counter, never block the ISR. A truncated frame
 *     fails the parse and reconciliation handles the loss; the
 *     counter makes it visible instead of silent.
 *  3. Callback bodies are ISR-MINIMAL: no parsing, no TX, no calls
 *     into anything that could block. All logic lives in loop context.
 *  4. Ring size power of 2 (mask arithmetic).
 *  5. The line handler runs in LOOP context, once per complete line,
 *     NUL-terminated, length excluding the terminator. Consume/copy
 *     within the handler — HespLine is overwritten by the next line.
 *  6. Junk policy in ONE place: empty lines skipped; over-length
 *     (buffer cap 192 or content > 158) counted and discarded; both
 *     announce via the junk handler (Patch 0).
 *  7. '\n' is THE frame boundary — NOT the IDLE event. IDLE ticks
 *     are the stuck-line diagnostic only.
 *
 *  TX invariants (1-6 of deliverable 3): thread-context only; NO
 *  volatile in TX state (single context, single writer); one
 *  in-flight build; reject-never-truncate; CRLF outside the content
 *  budget; misuse guards mirror the receiver's type rule.
 * ===================================================================== */

/* internal prototypes (static — program.c only) */
static void HESP_RxByteCallback(u8 Copy_u8Byte);
static void HESP_IdleCallback(void);
static u8   HESP_Assemble(u8 Copy_u8Byte);
static void HESP_TxRawString(MUSART_USART_t Copy_USART, const char *Copy_pchText);
static void HESP_TxRawBytes(MUSART_USART_t Copy_USART, const u8 *Copy_pu8Data, u16 Copy_u16Length);
static u8   HESP_NameHasPrefix(const char *Copy_pchName, const char *Copy_pchPrefix);
static HESP_Error_t HESP_TxAppendChar(u8 Copy_u8Char);
static HESP_Error_t HESP_TxAppendString(const char *Copy_pchText);
static HESP_Error_t HESP_TxAppendU16(u16 Copy_u16Value);
static HESP_Error_t HESP_TxAppendSeparator(void);

#endif /* HESP_PRIVATE_H_ */
