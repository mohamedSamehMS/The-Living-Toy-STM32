/*
 * HNEC_REMOTE_program.c — NEC remote controller, IMPLEMENTATION
 *
 *  Project 4 — Interactive Gaming Hub (ARM Embedded Systems Diploma)
 *  Target  : STM32F401CCU6 (WeAct Black Pill), register-level bare metal
 *  Layer   : HAL device layer -> HIR (HAL link layer) -> MCAL
 *
 *  DESIGN PRINCIPLES (documented, deliberate):
 *
 *  1. NO ENGINE, NO PINS, NO PROTOCOL HERE. Everything hardware and
 *     everything NEC-timing lives in HIR. This driver is pure policy:
 *     which remote (address), which button (table), repeat or not
 *     (mask), swallow how long (windows). The Phase-2 capture
 *     migration never touches this file.
 *
 *  2. THE PAIR CONTRACT IS STRUCTURAL. HIR_enumDecodeFrame and
 *     HIR_enumCloseFrame are called from exactly one straight-line
 *     function (the service below) — every path through it closes
 *     the frame it opened. HIR's decode-open guard backstops this.
 *
 *  3. EVENT-LOSS IS SOMETIMES THE FEATURE. The full window after
 *     non-accepted frames is the swallow that enforces "one action
 *     per press" for discrete buttons and the initial delay for
 *     repeatable ones. Do not "fix" the loss without revisiting the
 *     repeat contract (config §2).
 *
 *  4. SILENT REJECTION. Foreign addresses, unmapped codes, discarded
 *     repeats — no event, no status distinction. Downstream cannot
 *     and should not tell noise from policy.
 *
 *  5. ISR-MINIMAL BY INHERITANCE. This driver registers no ISR of
 *     its own; HIR's one-store callback is the entire interrupt-side
 *     workload of the IR stack.
 *
 *  Created on: Sep 13, 2026
 *      Author: dell
 */

#include "STD_TYPES.h"
#include "BIT_MATH.h"

#include "HAL/IR/HIR_interface.h"             /* the link layer consumed      */

#include "HAL/NEC/HNEC_REMOTE_config.h"
#include "HAL/NEC/HNEC_REMOTE_interface.h"
#include "HAL/NEC/HNEC_REMOTE_private.h"

/* =====================================================================
 *  Compile-time consistency guards
 * ===================================================================== */

/* The repeat mask is a u32 bit-per-enum-value — the enum must fit. */
_Static_assert(BTN_NONE < 32, "HNEC_REMOTE: VirtualButton_t exceeds the u32 repeat mask");

/* =====================================================================
 *  Module state (all file-private)
 * ===================================================================== */

static u8 Global_u8Initialized = 0u;
static u8 Global_u8Enabled     = 0u;

/* Pending event slot (the polling channel). Single slot, single
 * consumer: the protocol itself rate-limits events (a fresh event at
 * most every ~110 ms). */
static HNEC_REMOTE_Event_t Global_stPendingEvent = { BTN_NONE, 0u };
static u8 Global_u8PendingValid = 0u;

/* Last accepted button — what a decoded repeat frame refers to
 * (NEC repeats carry NO command data: "repeat whatever was last"). */
static VirtualButton_t Global_udtLastAccepted = BTN_NONE;

/* Event callback (thread-context channel). NULL = unregistered. */
static void (*Global_pfEventCallback)(HNEC_REMOTE_Event_t) = NULL;

/* =====================================================================
 *  Translation
 * ===================================================================== */
static VirtualButton_t HNEC_REMOTE_privateLookupButton(u8 Copy_u8NecCmd)
{
    u8 Local_u8Index;

    /* Linear scan of 21 const rows at most once per ~110 ms — nothing
     * to optimize. Unknown code -> BTN_NONE (silently no event). */
    for (Local_u8Index = 0u;
         Local_u8Index < (u8)HNEC_REMOTE_BUTTON_MAP_SIZE;
         Local_u8Index++)
    {
        if (HNEC_REMOTE_ButtonMap[Local_u8Index].NecCmd == Copy_u8NecCmd)
        {
            return HNEC_REMOTE_ButtonMap[Local_u8Index].VirtualButton;
        }
    }
    return BTN_NONE;
}

/* =====================================================================
 *  THE SERVICE (thread context — called from GetEvent only)
 * ===================================================================== */
static void HNEC_REMOTE_privateService(void)
{
    HIR_Frame_t Local_stFrame;
    u8  Local_u8AcceptedRepeat = 0u;
    u32 Local_u32WindowMs;

    /* Phase 1 — HIR's decode. NO_NEW_EVENT means idle: nothing owed,
     * nothing to close, return. Every other HIR status (disabled,
     * error) likewise leaves nothing owed. */
    if (HIR_enumDecodeFrame(&Local_stFrame) != HIR_OK)
    {
        return;
    }

    /* Phase 2 — policy (the whole reason this driver exists). */
    if (Local_stFrame.Type == HIR_FRAME_DATA)
    {
        /* Address filter (config §1 — honest scope documented there).
         * Valid frame, wrong remote: silent rejection. */
        if (Local_stFrame.Addr == HNEC_REMOTE_EXPECTED_ADDRESS)
        {
            VirtualButton_t Local_udtButton =
                HNEC_REMOTE_privateLookupButton(Local_stFrame.Cmd);

            if (Local_udtButton != BTN_NONE)
            {
                /* Fresh press: becomes the last-accepted (what future
                 * repeat frames refer to) and a pending event. */
                Global_udtLastAccepted          = Local_udtButton;
                Global_stPendingEvent.Button    = Local_udtButton;
                Global_stPendingEvent.IsRepeat  = 0u;
                Global_u8PendingValid           = 1u;

                if (Global_pfEventCallback != NULL)
                {
                    Global_pfEventCallback(Global_stPendingEvent);
                }
            }
            /* else: valid frame, unmapped code — no event (silent). */
        }
    }
    else if (Local_stFrame.Type == HIR_FRAME_REPEAT)
    {
        /* Repeats carry no data: they repeat the LAST ACCEPTED button.
         * Emit only if that role is repeat-allowed (config §4). */
        if ((Global_udtLastAccepted != BTN_NONE) &&
            ((HNEC_REMOTE_REPEAT_ALLOWED_MASK & (1UL << Global_udtLastAccepted)) != 0u))
        {
            Global_stPendingEvent.Button   = Global_udtLastAccepted;
            Global_stPendingEvent.IsRepeat = 1u;
            Global_u8PendingValid          = 1u;
            Local_u8AcceptedRepeat         = 1u;

            if (Global_pfEventCallback != NULL)
            {
                Global_pfEventCallback(Global_stPendingEvent);
            }
        }
        /* else: repeat of a discrete-only button (or nothing ever
         * accepted) — decoded, discarded, FULL window: the swallow
         * IS the "one action per press" enforcement (principle 3). */
    }
    /* HIR_FRAME_INVALID: noise / corruption / stale service — no
     * event, full window (noise rejection). */

    /* Phase 3 — close the frame with the window the policy chose:
     * short after an ACCEPTED repeat so the next 110 ms repeat frame
     * flows (the auto-repeat rate contract, config §2); full after
     * everything else. THE single CloseFrame call site (principle 2). */
    Local_u32WindowMs = (Local_u8AcceptedRepeat != 0u)
                        ? HNEC_REMOTE_REPEAT_QUIET_MS
                        : HNEC_REMOTE_QUIET_WINDOW_MS;
    (void)HIR_enumCloseFrame(Local_u32WindowMs);
}

/* =====================================================================
 *  Public functions
 * ===================================================================== */

HNEC_REMOTE_EnumStatus_t HNEC_REMOTE_enumInit(void)
{
    /* Bring up the link layer (idempotent) — its MCAL prerequisites
     * must already have run (boot banner). A failure here is almost
     * always boot-order: clocks, MEXTI table, or SysTick. */
    if (HIR_enumInit() != HIR_OK)
    {
        return HNEC_REMOTE_NOK;
    }

    /* Clean device-layer state. */
    Global_u8PendingValid  = 0u;
    Global_udtLastAccepted = BTN_NONE;
    Global_pfEventCallback = NULL;      /* register after init */
    Global_u8Enabled       = 1u;        /* HIR_enumInit armed it */
    Global_u8Initialized   = 1u;

    return HNEC_REMOTE_OK;
}

HNEC_REMOTE_EnumStatus_t HNEC_REMOTE_enumEnable(void)
{
    if (Global_u8Initialized == 0u) { return HNEC_REMOTE_NOK; }

    if (HIR_enumEnable() != HIR_OK)
    {
        return HNEC_REMOTE_NOK;
    }
    Global_u8Enabled = 1u;
    return HNEC_REMOTE_OK;
}

HNEC_REMOTE_EnumStatus_t HNEC_REMOTE_enumDisable(void)
{
    if (Global_u8Initialized == 0u) { return HNEC_REMOTE_NOK; }

    if (HIR_enumDisable() != HIR_OK)
    {
        return HNEC_REMOTE_NOK;
    }
    /* Discard in-flight device state: a press that landed just before
     * the disable is dropped, not half-processed on re-enable. The
     * line is masked, so nothing new can arrive while disabled. */
    Global_u8PendingValid = 0u;
    Global_u8Enabled = 0u;
    return HNEC_REMOTE_OK;
}

HNEC_REMOTE_EnumStatus_t HNEC_REMOTE_enumGetEnabledState(u8 *Copy_pu8State)
{
    if (Global_u8Initialized == 0u) { return HNEC_REMOTE_NOK; }
    if (Copy_pu8State == (u8 *)0)   { return HNEC_REMOTE_NULL_POINTER; }

    *Copy_pu8State = Global_u8Enabled;
    return HNEC_REMOTE_OK;
}

HNEC_REMOTE_EnumStatus_t HNEC_REMOTE_enumGetEvent(HNEC_REMOTE_Event_t *Copy_pstEvent)
{
    if (Copy_pstEvent == (HNEC_REMOTE_Event_t *)0) { return HNEC_REMOTE_NULL_POINTER; }
    if (Global_u8Initialized == 0u)                { return HNEC_REMOTE_NOK; }
    if (Global_u8Enabled == 0u)                    { return HNEC_REMOTE_DISABLED; }

    /* Service first (idle cost: one flag read inside HIR; active
     * cost: the ~270 ms banner), then drain. */
    HNEC_REMOTE_privateService();

    if (Global_u8PendingValid != 0u)
    {
        Copy_pstEvent->Button   = Global_stPendingEvent.Button;
        Copy_pstEvent->IsRepeat = Global_stPendingEvent.IsRepeat;
        Global_u8PendingValid = 0u;
        return HNEC_REMOTE_OK;
    }
    return HNEC_REMOTE_NO_NEW_EVENT;
}

HNEC_REMOTE_EnumStatus_t HNEC_REMOTE_enumSetEventCallback(
        void (*Copy_pfCallback)(HNEC_REMOTE_Event_t))
{
    /* Not init-gated (register before init is fine — mirrors the
     * MSYSTICK SetTickCallback semantics: stored, fires once live).
     * NULL = unregister (library convention). Aligned store = atomic. */
    Global_pfEventCallback = Copy_pfCallback;
    return HNEC_REMOTE_OK;
}
