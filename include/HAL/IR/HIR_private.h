/*
 * HIR_private.h - IR receiver driver (NEC link layer), PRIVATE header
 *
 *  THE ENGINE - LANDED: TIM5 both-edges input capture on PA0 (AF2).
 *  The stepping engine (HIR_privateMeasureLevelUs, HIR_STEP_US, the
 *  0.85x-calibrated threshold set) is RETIRED with this revision. The
 *  engine seam closed exactly as designed: HIR_enumDecodeFrame /
 *  HIR_enumCloseFrame keep their contracts (now non-blocking), and
 *  HNEC_REMOTE + application code are untouched.
 *
 *  FRAME STRUCTURE (the structural fact this machine is built on): NEC
 *  data AND repeat frames end with a trailing ~560us LOW burst - the
 *  bit-31 space (and the repeat header space) is terminated by its
 *  falling edge, and its rising edge is the frame-completion event.
 *  68 edges per data frame, 4 per repeat (matches the testbench oracle).
 */
#ifndef HIR_PRIVATE_H_
#define HIR_PRIVATE_H_

#include "STD_TYPES.h"
#include "HAL/IR/HIR_interface.h"     /* HIR_FrameType_t */

/* ---- Decode thresholds - TRUE MICROSECONDS (TIM5 hardware timestamps).
 *        SHIP-FROZEN: the bench echo pass verified all 21 buttons.
 *        Never mix these with the old calibrated set - the numbers and
 *        the engine are a matched pair, and the pair has changed. ---- */
#define HIR_FRAME_TIMEOUT_US     15000u  /* level stuck > 15 ms = dead frame */
#define HIR_HDR_LOW_MIN_US       6000u   /* low >= this = AGC header          */
#define HIR_HDR_SPACE_MIN_US     3000u   /* space >= this = data frame        */
#define HIR_RPT_MIN_US           1400u   /* space >= this (and < hdr) = repeat*/
#define HIR_BIT_BURST_MAX_US     1200u   /* bit low longer = corrupt          */
#define HIR_BIT_ONE_MIN_US       1100u   /* bit space >= this = logic 1       */

/* ---- The edge-driven decode state machine (ISR context only) ---- */
typedef enum
{
    HIR_SM_IDLE = 0,     /* no frame in progress                            */
    HIR_SM_HDR_LOW,      /* inside a candidate AGC burst                    */
    HIR_SM_HDR_SPACE,    /* AGC confirmed; measuring the header space        */
    HIR_SM_BIT_LOW,      /* inside a data-bit burst                          */
    HIR_SM_BIT_SPACE,    /* measuring a data-bit space                       */
    HIR_SM_STOP          /* awaiting the trailing burst's rising edge        */
} HIR_SmState_t;

/* Capture callbacks - MTIM_CaptureCallback_t signature, ISR context */
static void HIR_privateRisingEdgeCb (u32 Copy_u32Timestamp);
static void HIR_privateFallingEdgeCb(u32 Copy_u32Timestamp);

/* Latch a completed frame into the ISR->thread mailbox (ISR context) */
static void HIR_privateLatchFrame(u32 Copy_u32CompletionTs);

/* Reset the machine to IDLE. Thread-context callers must have disarmed
 * capture FIRST (the disable barrier); ISR-context use is always safe. */
static void HIR_privateResetMachine(void);

#endif /* HIR_PRIVATE_H_ */
