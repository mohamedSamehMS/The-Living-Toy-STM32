/*
 * HIR_program.c — IR receiver driver (NEC link layer), IMPLEMENTATION
 *                  TIM5 INPUT-CAPTURE EDITION (the Phase-2 engine swap)
 *
 *  Layer: HAL (link layer) -> MCAL (MGPIO, MTIM). Consumed by HNEC_REMOTE.
 *
 *  DESIGN (documented, deliberate):
 *   1. DECODE IS EDGE-DRIVEN, IN THE CAPTURE ISRs. Per edge: one u32
 *      subtraction + one state transition (~2-3 us). ~68 edges per data
 *      frame = ~0.2 ms of ISR work replacing the old 68 ms busy-spin.
 *      Frame completion is a HARDWARE event: the trailing burst's
 *      rising edge (HIR_private.h banner).
 *   2. THE PAIR CONTRACT SURVIVES, NON-BLOCKING. DecodeFrame polls a
 *      seqlock mailbox (MSYSTICK Rev 3 pattern); CloseFrame sets a
 *      timestamp deadline. HNEC_REMOTE is byte-identical above this.
 *   3. NO SYSTICK. Timestamps are TIM5's own 1 MHz counter (callback
 *      arguments); the quiet window is u32 modular arithmetic.
 *   4. THE WINDOW = (u32)(completion - lastDelivered) < quietUs. The
 *      library's wrap-safe subtraction idiom; the anchor refreshes at
 *      every delivery. Swallowed frames are consumed silently (no
 *      CloseFrame owed) — the window persists until a frame passes it.
 *   5. STRUCTURAL NOISE DISCARDS SILENTLY (deliberate divergence — see
 *      the delivery notes); CHECKSUM FAILURES still latch INVALID and
 *      receive their window through HNEC's normal path.
 *   6. ENABLE/DISABLE = capture arm/disarm. The disarm-before-reset
 *      ordering is the barrier making the thread-side reset race-free.
 *
 *  Created on: Sep 12, 2026   |   Capture edition: this revision
 *      Author: dell
 */

#include "STD_TYPES.h"

#include <MCAL/GPIO/MGPIO_interface.h>
#include <MCAL/TIM/MTIM_interface.h>

#include "HAL/IR/HIR_config.h"
#include "HAL/IR/HIR_interface.h"
#include "HAL/IR/HIR_private.h"

/* The AF2 fact is PA0-specific — pin this pairing (Constitution rule) */
_Static_assert((HIR_PORT == MGPIO_PORTA) && (HIR_PIN == MGPIO_PIN_0),
               "HIR: PA0-specific AF - re-derive HIR_PIN_AF if the pin moves");

/* ===================== module state (thread-side) ===================== */

static u8 Global_u8Initialized = 0u;
static u8 Global_u8Enabled     = 0u;
static u8 Global_u8DecodeOpen  = 0u;

/* Raw-frame debug slot (thread-only: DecodeFrame writes, GetLastRawFrame
 * reads — no concurrency, same as before the swap). */
static u8 Global_u8LastAddr = 0u;
static u8 Global_u8LastCmd  = 0u;
static u8 Global_u8RawFresh = 0u;

/* Window state (thread-only): the swallow deadline is anchored to the
 * TIM5-us completion of the last DELIVERED frame. */
static u32 HIR_privateLastCompletion = 0u;
static u32 HIR_privateQuietWindowUs  = 0u;
static u8  HIR_privateConsumedSeq    = 0u;   /* mailbox version consumed */

/* ============ the ISR mailbox (written ONLY in capture ISRs) ============ */

static volatile u8  HIR_privateSlotSeq = 0u;               /* even = stable */
static volatile HIR_FrameType_t HIR_privateSlotType = HIR_FRAME_INVALID;
static volatile u8  HIR_privateSlotAddr = 0u;
static volatile u8  HIR_privateSlotCmd  = 0u;
static volatile u32 HIR_privateSlotCompletion = 0u;       /* TIM5 us */

/* ====== the decode state machine (TIM5 ISR context only; the Disable
 *         path resets it AFTER disarming capture — the barrier) ====== */

static HIR_SmState_t    HIR_privateSmState = HIR_SM_IDLE;
static u32              HIR_privateLastEdgeTs = 0u;
static u8               HIR_privateBitIndex = 0u;
static u8               HIR_privateFrameBytes[4] = {0u, 0u, 0u, 0u};
static HIR_FrameType_t  HIR_privateFrameType = HIR_FRAME_INVALID;

/* ===================== ISR helpers ===================== */

static void HIR_privateLatchFrame(u32 Copy_u32CompletionTs)
{
    HIR_privateSlotSeq++;                          /* odd: writing */

    HIR_privateSlotType = HIR_privateFrameType;
    if (HIR_privateFrameType == HIR_FRAME_DATA)
    {
        HIR_privateSlotAddr = HIR_privateFrameBytes[0];
        HIR_privateSlotCmd  = HIR_privateFrameBytes[2];
    }
    else
    {
        HIR_privateSlotAddr = 0u;                  /* REPEAT/INVALID: none */
        HIR_privateSlotCmd  = 0u;
    }
    HIR_privateSlotCompletion = Copy_u32CompletionTs;

    HIR_privateSlotSeq++;                          /* even: stable */
}

static void HIR_privateResetMachine(void)
{
    HIR_privateSmState = HIR_SM_IDLE;
    HIR_privateBitIndex = 0u;
    HIR_privateFrameBytes[0] = 0u;
    HIR_privateFrameBytes[1] = 0u;
    HIR_privateFrameBytes[2] = 0u;
    HIR_privateFrameBytes[3] = 0u;
    HIR_privateFrameType = HIR_FRAME_INVALID;
    /* HIR_privateLastEdgeTs deliberately NOT reset: the next falling edge
     * (header start) overwrites it before any delta is consumed. */
}

/* ---- RISING edge: the LOW level that preceded it just ended ---------- */
static void HIR_privateRisingEdgeCb(u32 Copy_u32Timestamp)
{
    /* wrap-safe by unsigned subtraction (deltas are ms-scale; a stale
     * prevTs across a wrap reads huge -> caught by the timeout guards) */
    u32 Local_u32Dur = (Copy_u32Timestamp - HIR_privateLastEdgeTs);

    switch (HIR_privateSmState)
    {
        case HIR_SM_HDR_LOW:
        {
            /* candidate AGC burst just ended */
            if ((Local_u32Dur >= HIR_FRAME_TIMEOUT_US) ||
                (Local_u32Dur <  HIR_HDR_LOW_MIN_US))
            {
                HIR_privateSmState = HIR_SM_IDLE;        /* not a header */
            }
            else
            {
                HIR_privateSmState = HIR_SM_HDR_SPACE;   /* AGC confirmed */
            }
            break;
        }

        case HIR_SM_BIT_LOW:
        {
            /* data-bit burst just ended (nominally 560us) */
            if ((Local_u32Dur >= HIR_FRAME_TIMEOUT_US) ||
                (Local_u32Dur >  HIR_BIT_BURST_MAX_US))
            {
                HIR_privateSmState = HIR_SM_IDLE;        /* corrupt */
            }
            else
            {
                HIR_privateSmState = HIR_SM_BIT_SPACE;
            }
            break;
        }

        case HIR_SM_STOP:
        {
            /* the trailing burst ended — FRAME COMPLETE */
            HIR_privateLatchFrame(Copy_u32Timestamp);
            HIR_privateSmState = HIR_SM_IDLE;
            break;
        }

        case HIR_SM_IDLE:
        default:    /* HDR_SPACE / BIT_SPACE expect FALLING here */
        {
            /* Stray rising (e.g. the trailing burst of a frame whose
             * decode was discarded): carries no frame information — a
             * header always starts with a FALLING edge. Ignore. */
            break;
        }
    }

    HIR_privateLastEdgeTs = Copy_u32Timestamp;
}

/* ---- FALLING edge: the HIGH level that preceded it just ended --------- */
static void HIR_privateFallingEdgeCb(u32 Copy_u32Timestamp)
{
    u32 Local_u32Dur = (Copy_u32Timestamp - HIR_privateLastEdgeTs);

    switch (HIR_privateSmState)
    {
        case HIR_SM_HDR_SPACE:
        {
            /* the header space just ended: 4500us = data, 2250us = repeat */
            if (Local_u32Dur >= HIR_FRAME_TIMEOUT_US)
            {
                HIR_privateSmState = HIR_SM_IDLE;            /* dead frame */
            }
            else if (Local_u32Dur < HIR_RPT_MIN_US)
            {
                HIR_privateSmState = HIR_SM_IDLE;            /* corrupt */
            }
            else if (Local_u32Dur < HIR_HDR_SPACE_MIN_US)
            {
                HIR_privateFrameType = HIR_FRAME_REPEAT;     /* no bits    */
                HIR_privateSmState   = HIR_SM_STOP;           /* burst now  */
            }
            else
            {
                HIR_privateFrameType = HIR_FRAME_DATA;
                HIR_privateBitIndex  = 0u;
                HIR_privateFrameBytes[0] = 0u;
                HIR_privateFrameBytes[1] = 0u;
                HIR_privateFrameBytes[2] = 0u;
                HIR_privateFrameBytes[3] = 0u;
                HIR_privateSmState = HIR_SM_BIT_LOW;         /* bit 0 now  */
            }
            break;
        }

        case HIR_SM_BIT_SPACE:
        {
            /* a data-bit space just ended: >= BIT_ONE_MIN = logic 1 */
            if (Local_u32Dur >= HIR_FRAME_TIMEOUT_US)
            {
                HIR_privateSmState = HIR_SM_IDLE;            /* dead frame */
            }
            else
            {
                if (Local_u32Dur >= HIR_BIT_ONE_MIN_US)
                {
                    HIR_privateFrameBytes[HIR_privateBitIndex >> 3]
                        |= (u8)(1u << (HIR_privateBitIndex & 7u));
                }
                HIR_privateBitIndex++;

                if (HIR_privateBitIndex >= 32u)
                {
                    if (((HIR_privateFrameBytes[0] ^ HIR_privateFrameBytes[1]) != 0xFFu) ||
                        ((HIR_privateFrameBytes[2] ^ HIR_privateFrameBytes[3]) != 0xFFu))
                    {
                        /* Structurally complete, checksum-corrupt: report
                         * it — noise gets a window (the old contract). */
                        HIR_privateFrameType = HIR_FRAME_INVALID;
                        HIR_privateLatchFrame(Copy_u32Timestamp);
                        HIR_privateSmState = HIR_SM_IDLE;
                    }
                    else
                    {
                        HIR_privateSmState = HIR_SM_STOP;    /* stop burst now */
                    }
                }
                else
                {
                    HIR_privateSmState = HIR_SM_BIT_LOW;     /* next burst now */
                }
            }
            break;
        }

        case HIR_SM_IDLE:
        default:    /* HDR_LOW / BIT_LOW / STOP expect RISING here */
        {
            /* IDLE: a candidate AGC start. The other states: a missed
             * rising edge (overcapture — impossible at NEC rates) or a
             * machine parked since a dead frame. Re-treat THIS edge as a
             * candidate header start: the machine self-heals instead of
             * eating the next real frame. (A frame unlatched in STOP is
             * lost in the missed-edge case — bounded, documented.) */
            HIR_privateSmState = HIR_SM_HDR_LOW;
            break;
        }
    }

    HIR_privateLastEdgeTs = Copy_u32Timestamp;
}

/* ===================== public functions ===================== */

HIR_EnumStatus_t HIR_enumInit(void)
{
    u32 Local_u32Now;

    /* Pin: alternate function, TIM5_CH1. MEXTI wiring is GONE — TIM5
     * captures the pin directly. The pull stays: PUPDR applies in AF
     * mode; an unwired line rests idle-high (no ghost edges). */
    if (MGPIO_SetPinMode(HIR_PORT, HIR_PIN, MGPIO_MODE_ALT_FUNC) != MGPIO_OK)
    {
        return HIR_NOK;
    }
    if (MGPIO_SetPinAltFunction(HIR_PORT, HIR_PIN, HIR_PIN_AF) != MGPIO_OK)
    {
        return HIR_NOK;
    }
    if (MGPIO_SetPinPull(HIR_PORT, HIR_PIN, HIR_PIN_PULL) != MGPIO_OK)
    {
        return HIR_NOK;
    }

    /* MTIM_Init must already have applied the TIM5 FREE_RUN 1 MHz row
     * (HIR_config.h banner). Disarm-if-armed keeps re-init idempotent;
     * reset BEFORE arming so no ISR can observe mid-reset state. */
    (void)MTIM_CaptureStop(HIR_TIM, MTIM_CH1);
    (void)MTIM_CaptureStop(HIR_TIM, MTIM_CH2);
    HIR_privateResetMachine();

    /* Anchor the window to the current counter — kills the phantom
     * window-from-zero boot corner (a first press 35.8-71.6 min after
     * boot would otherwise be swallowed by a stale zero deadline). */
    if (MTIM_GetCounter(HIR_TIM, &Local_u32Now) != MTIM_OK)
    {
        return HIR_NOK;
    }
    HIR_privateLastCompletion = Local_u32Now;
    HIR_privateQuietWindowUs  = 0u;

    if (MTIM_CaptureStartBothEdges(HIR_TIM,
                                   HIR_privateRisingEdgeCb,
                                   HIR_privateFallingEdgeCb) != MTIM_OK)
    {
        return HIR_NOK;      /* MTIM uninitialized, or TIM5 not FREE_RUN */
    }
    if (MTIM_Start(HIR_TIM) != MTIM_OK)
    {
        return HIR_NOK;
    }

    /* Clean software state. */
    HIR_privateConsumedSeq = HIR_privateSlotSeq;
    Global_u8RawFresh   = 0u;
    Global_u8DecodeOpen = 0u;
    Global_u8Enabled    = 1u;
    Global_u8Initialized = 1u;

    return HIR_OK;
}

HIR_EnumStatus_t HIR_enumEnable(void)
{
    if (Global_u8Initialized == 0u) { return HIR_NOK; }

    /* Clean boundary before arming (the arm wipes stale CCxIF itself). */
    HIR_privateResetMachine();
    HIR_privateConsumedSeq = HIR_privateSlotSeq;   /* discard pre-disable */

    if (MTIM_CaptureStartBothEdges(HIR_TIM,
                                   HIR_privateRisingEdgeCb,
                                   HIR_privateFallingEdgeCb) != MTIM_OK)
    {
        return HIR_NOK;
    }
    Global_u8Enabled = 1u;
    return HIR_OK;
}

HIR_EnumStatus_t HIR_enumDisable(void)
{
    if (Global_u8Initialized == 0u) { return HIR_NOK; }

    /* Disarm BOTH channels first: after these return, no capture ISR can
     * run — the barrier that makes the resets below race-free. A frame
     * in flight at the boundary is discarded by design. The COUNTER KEEPS
     * RUNNING (timestamps must stay continuous). */
    (void)MTIM_CaptureStop(HIR_TIM, MTIM_CH1);
    (void)MTIM_CaptureStop(HIR_TIM, MTIM_CH2);

    HIR_privateResetMachine();
    HIR_privateConsumedSeq = HIR_privateSlotSeq;   /* drop anything pending */
    Global_u8Enabled = 0u;
    return HIR_OK;
}

HIR_EnumStatus_t HIR_enumDecodeFrame(HIR_Frame_t *Copy_pstFrame)
{
    HIR_FrameType_t Local_Type;
    u8  Local_u8Addr;
    u8  Local_u8Cmd;
    u32 Local_u32Completion;
    u8  Local_u8Seq1;
    u8  Local_u8Seq2;

    if (Copy_pstFrame == (HIR_Frame_t *)0) { return HIR_NULL_POINTER; }
    if (Global_u8Initialized == 0u)        { return HIR_NOK; }
    if (Global_u8Enabled == 0u)            { return HIR_DISABLED; }
    if (Global_u8DecodeOpen != 0u)         { return HIR_DECODE_OPEN; }

    /* ---- seqlock-consistent mailbox copy (the MSYSTICK Rev 3 pattern:
     *      retry while writing, retry if changed across the copy) ---- */
    do
    {
        do
        {
            Local_u8Seq1 = HIR_privateSlotSeq;
        } while ((Local_u8Seq1 & 1u) != 0u);

        Local_Type          = HIR_privateSlotType;
        Local_u8Addr        = HIR_privateSlotAddr;
        Local_u8Cmd         = HIR_privateSlotCmd;
        Local_u32Completion = HIR_privateSlotCompletion;

        Local_u8Seq2 = HIR_privateSlotSeq;
    } while (Local_u8Seq1 != Local_u8Seq2);

    /* Nothing new since the last consume — the idle path (still ~sub-us:
     * a handful of volatile reads). */
    if (Local_u8Seq2 == HIR_privateConsumedSeq)
    {
        return HIR_NO_NEW_EVENT;
    }
    HIR_privateConsumedSeq = Local_u8Seq2;   /* consumed: delivered OR swallowed */

    /* ---- the quiet window (non-blocking swallow) ----
     * A frame COMPLETING within the window of the last DELIVERED frame is
     * swallowed. This is the old engine's decode-mask + quiet busy-wait
     * translated to TIM5-timestamp arithmetic: the anchor refreshes at
     * every delivery, the subtraction is wrap-safe by construction, and
     * waiting out the window now costs ZERO thread time. Swallowed frames
     * are consumed WITHOUT a CloseFrame owed — the window persists (still
     * anchored to the old frame) until one passes it. */
    if ((u32)(Local_u32Completion - HIR_privateLastCompletion)
        < HIR_privateQuietWindowUs)
    {
        return HIR_NO_NEW_EVENT;             /* swallowed by the window */
    }

    Copy_pstFrame->Type = Local_Type;
    Copy_pstFrame->Addr = Local_u8Addr;
    Copy_pstFrame->Cmd  = Local_u8Cmd;

    if (Local_Type == HIR_FRAME_DATA)
    {
        Global_u8LastAddr = Local_u8Addr;    /* raw debug slot — thread-only */
        Global_u8LastCmd  = Local_u8Cmd;
        Global_u8RawFresh = 1u;
    }

    HIR_privateLastCompletion = Local_u32Completion;
    Global_u8DecodeOpen = 1u;                /* a CloseFrame is now OWED */
    return HIR_OK;
}

HIR_EnumStatus_t HIR_enumCloseFrame(u32 Copy_u32QuietMs)
{
    if (Global_u8Initialized == 0u) { return HIR_NOK; }

    /* Nothing owed (or a stray double-close): state untouched — a
     * spurious window would silently eat a pending frame. */
    if (Global_u8DecodeOpen == 0u)  { return HIR_NOK; }

    Global_u8DecodeOpen = 0u;

    /* THE WINDOW — a deadline, not a wait. The duration stays the
     * CONSUMER's policy (HNEC chooses 200/0 exactly as before); HIR only
     * owns the mechanism, which no longer blocks anyone. */
    HIR_privateQuietWindowUs = Copy_u32QuietMs * 1000u;
    return HIR_OK;
}

HIR_EnumStatus_t HIR_enumGetLastRawFrame(u8 *Copy_pu8Addr, u8 *Copy_pu8Cmd)
{
    if (Global_u8Initialized == 0u) { return HIR_NOK; }
    if ((Copy_pu8Addr == (u8 *)0) || (Copy_pu8Cmd == (u8 *)0))
    {
        return HIR_NULL_POINTER;
    }
    if (Global_u8RawFresh == 0u) { return HIR_NO_NEW_EVENT; }

    *Copy_pu8Addr = Global_u8LastAddr;
    *Copy_pu8Cmd  = Global_u8LastCmd;
    Global_u8RawFresh = 0u;
    return HIR_OK;
}
