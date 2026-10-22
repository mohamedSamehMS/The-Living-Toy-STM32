/*
 * HBUZZER_program.c — shared buzzer HAL driver, IMPLEMENTATION
 *
 *  Project 4 — Interactive Gaming Hub (ARM Embedded Systems Diploma)
 *  Target : STM32F401CCU6 (WeAct Black Pill), register-level bare metal
 *  Layer  : HAL -> MCAL (MTIM, MGPIO) + FreeRTOS (critical sections)
 *
 *  DESIGN PRINCIPLES (documented, deliberate):
 *
 *  1. ONE TRANSDUCER, ONE OWNER. Three concerns share this buzzer;
 *     arbitration (priority preemption) lives HERE so every call site
 *     stays a one-liner. See the interface banner for the ladder.
 *
 *  2. HARDWARE-TIMED DURATIONS, TWO-PHASE TEARDOWN (Rev 2). A timed
 *     tone ends in TWO steps inside the update ISR: one period BEFORE
 *     the deadline, CCR is preloaded to 0 — the NEXT update event
 *     latches the output INACTIVE (LOW); only then does DeInit freeze
 *     the pin, now at a DEFINED silent level. (Rev 1's single-phase
 *     DeInit froze the pin at whatever level the PWM held — and the
 *     teardown instant is deterministically the UPDATE instant, where
 *     PWM mode 1 has JUST set the output ACTIVE: the pin froze HIGH
 *     every single blip, leaving DC on the transducer. Bench-confirmed:
 *     bz=50 ISRs/blip, bzd=1 teardown/blip — mechanism perfect, output
 *     level wrong. The fix makes the END STATE a contract, not luck.)
 *
 *  3. FREQUENCY CHANGE = DeInit + PWMStart. One PSC/ARR pair per
 *     counter is silicon (MTIM's mode rule); bench-proven sequence.
 *
 *  4. STATE PROTECTION = KERNEL CRITICAL SECTIONS. Mutating APIs run
 *     under taskENTER/EXIT_CRITICAL: the sequences are microseconds,
 *     and BASEPRI masks the TIM10 ISR (priority 5, at/below the
 *     syscall mask) — task-vs-task AND task-vs-ISR interleaving are
 *     both excluded on this single core.
 *
 *  5. SILENT BY CONSTRUCTION AT BOOT. Init wires the pin and nothing
 *     else — TIM10 is untouched until the first tone request.
 *
 *  Revision history:
 *    Rev 1 — first release; the frozen-output bug (teardown froze the
 *            pin HIGH, deterministically, at the update instant)
 *    Rev 2 — two-phase teardown (this revision): CCR=0 preload one
 *            period early -> output latches LOW -> DeInit freezes the
 *            defined level. ToneStop shares the mechanism. Diagnosed
 *            on the bench (bz/bzd counters), fixed by design.
 *
 *  Created on: Oct 2, 2026
 *      Author: dell
 */

#include "STD_TYPES.h"

#include <MCAL/GPIO/MGPIO_interface.h>
#include <MCAL/TIM/MTIM_interface.h>

#include <OS/FreeRTOS.h>
#include <OS/task.h>


#include "HAL/BUZZER/HBUZZER_private.h"
#include "HAL/BUZZER/HBUZZER_interface.h"
#include "HAL/BUZZER/HBUZZER_config.h"



/* ==== module state ================================================= */

static u8 Global_u8Initialized = 0u;

/* Shared task<->ISR (both volatile: written under critical sections
 * from task context, read/written by the TIM10 ISR at tone end).    */
static volatile HBUZZER_Mode_t      Global_eMode = HBUZZER_MODE_SILENT;
static volatile u32                 Global_u32PeriodsRemaining = 0u;

/* Task-context-only (written and read under critical sections).      */
static HBUZZER_Priority_t Global_eActivePriority = HBUZZER_PRIORITY_LOW;

/* ==== THE tone-end ISR ============================================== */

static void HBUZZER_privateUpdateISR(void)
{
    /* ISR context, TIM10's priority (5). Register/static writes only.

     * PHASE 1 (PeriodsRemaining == 1): CCR=0, preloaded — takes
     * effect at the NEXT update event. This period still sounds
     * (correct: the requested duration includes it); the period
     * AFTER it will be silent. The clamp in StartTone guarantees
     * this branch has runway (>= 2 periods).
     *
     * PHASE 2 (== 0): this update event just latched the preloaded
     * CCR=0 with CNT=0 — compare 0 < 0 is false, so the output has
     * been INACTIVE (LOW) from this instant. DeInit now freezes the
     * pin at a DEFINED silent level. Never a frozen HIGH, never DC. */
    if (Global_eMode != HBUZZER_MODE_TIMED) { return; }

    if (Global_u32PeriodsRemaining != 0u) { Global_u32PeriodsRemaining--; }

    if (Global_u32PeriodsRemaining == 1u)
    {
        (void)MTIM_PWMSetDuty(HBUZZER_TIMER, MTIM_CH1, 0u);   /* phase 1 */
    }
    else if (Global_u32PeriodsRemaining == 0u)
    {
        (void)MTIM_DeInit(HBUZZER_TIMER);                     /* phase 2 */
        Global_eMode = HBUZZER_MODE_SILENT;
    }
    else { /* > 1: still counting — the tone continues */ }
}

/* ==== the common start sequence ===================================== */

static HBUZZER_ReturnType_t HBUZZER_privateStartTone(HBUZZER_Priority_t Copy_Priority,
                                                     u16 Copy_u16FreqHz,
                                                     u16 Copy_u16DurationMs,
                                                     u8  Copy_u8Timed)
{
    HBUZZER_ReturnType_t Local_ReturnStatus = HBUZZER_OK;
    MTIM_ReturnType_t    Local_MtimStatus;

    if (Global_u8Initialized == 0u)                          { return HBUZZER_NOK; }
    if ((u32)Copy_Priority >= (u32)HBUZZER_PRIORITY_COUNT)   { return HBUZZER_INVALID_PARAM; }
    if (Copy_u16FreqHz == 0u)                                { return HBUZZER_INVALID_PARAM; }
    if ((Copy_u8Timed != 0u) && (Copy_u16DurationMs == 0u))  { return HBUZZER_INVALID_PARAM; }

    /* ---- one critical section: arbitration decision + hardware
     * reconfiguration + state publication, atomic against tasks AND
     * the TIM10 ISR (masked by BASEPRI at priority 5).             */
    taskENTER_CRITICAL();

    /* ARBITRATION: lower than the active tone -> dropped (BUSY,
     * silent by design). Equal -> replace (blips merge). Higher ->
     * preempt (FOTA's MEDIUM overrides the LOW blip).              */
    if ((Global_eMode != HBUZZER_MODE_SILENT) &&
        ((u32)Copy_Priority < (u32)Global_eActivePriority))
    {
        taskEXIT_CRITICAL();
        return HBUZZER_BUSY;
    }

    /* A restart supersedes any in-flight countdown: DeInit kills the
     * old tone outright, and a fresh request below has its own full
     * countdown. The OLD tone's pin level is irrelevant — the new
     * PWMStart drives the pin from its own first period.           */
    (void)MTIM_DeInit(HBUZZER_TIMER);

    Local_MtimStatus = MTIM_PWMStart(HBUZZER_TIMER, MTIM_CH1, (u32)Copy_u16FreqHz,
                                     (u16)HBUZZER_DUTY_PERMILLE, MTIM_PWM_ACTIVE_HIGH);
    if (Local_MtimStatus != MTIM_OK)
    {
        Global_eMode = HBUZZER_MODE_SILENT;      /* mirror the DeInit  */
        taskEXIT_CRITICAL();
        return (Local_MtimStatus == MTIM_INVALID_PARAM) ? HBUZZER_INVALID_PARAM
                                                        : HBUZZER_NOK;
    }

    if (Copy_u8Timed != 0u)
    {
        u32 Local_u32Periods;

        Local_u32Periods = ((((u32)Copy_u16DurationMs * (u32)Copy_u16FreqHz) + 999u) / 1000u);

        /* Phase-1 needs one period of runway: clamp to 2. (A 50 ms/
           1 kHz blip computes 50 — the clamp only guards degenerate
           short-duration/high-frequency corners.)                    */
        if (Local_u32Periods < 2u) { Local_u32Periods = 2u; }

        /* State BEFORE arming (a pending UIF may fire immediately —
         * MTIM's documented periodic semantics; the countdown must
         * already be valid).                                        */
        Global_eMode = HBUZZER_MODE_TIMED;
        Global_u32PeriodsRemaining = Local_u32Periods;
        Global_eActivePriority = Copy_Priority;

        if (MTIM_EnableUpdateInterrupt(HBUZZER_TIMER, HBUZZER_privateUpdateISR) != MTIM_OK)
        {
            /* UIE failed to arm: the tone would never self-end — tear
             * it down NOW (pin may freeze at an active level, but a
             * stuck-config error state beats an infinite blip; the
             * next Start or Stop re-synchronizes).                   */
            (void)MTIM_DeInit(HBUZZER_TIMER);
            Global_eMode = HBUZZER_MODE_SILENT;
            taskEXIT_CRITICAL();
            return HBUZZER_NOK;
        }
    }
    else
    {
        /* Sustained: PWM only — NO interrupt runs while it sounds. */
        Global_eMode = HBUZZER_MODE_SUSTAINED;
        Global_eActivePriority = Copy_Priority;
    }

    taskEXIT_CRITICAL();
    return Local_ReturnStatus;
}

/* ==== public functions =============================================== */

HBUZZER_ReturnType_t HBUZZER_enumInit(void)
{
    /* Pin: AF mode, TIM10_CH1. Output stage: push-pull, low speed —
     * kHz-class edges, bench-proven at this setting. The timer is
     * NOT touched: silent until the first tone.                      */
    if (MGPIO_SetPinMode        (HBUZZER_PORT, HBUZZER_PIN, MGPIO_MODE_ALT_FUNC) != MGPIO_OK)
    {
        return HBUZZER_NOK;
    }
    if (MGPIO_SetPinOutputConfig(HBUZZER_PORT, HBUZZER_PIN,
                                 MGPIO_OUTPUT_PUSH_PULL, MGPIO_SPEED_LOW) != MGPIO_OK)
    {
        return HBUZZER_NOK;
    }
    if (MGPIO_SetPinAltFunction (HBUZZER_PORT, HBUZZER_PIN, HBUZZER_PIN_AF) != MGPIO_OK)
    {
        return HBUZZER_NOK;
    }

    Global_eMode = HBUZZER_MODE_SILENT;
    Global_u32PeriodsRemaining = 0u;
    Global_u8Initialized = 1u;
    return HBUZZER_OK;
}

HBUZZER_ReturnType_t HBUZZER_enumBlip(void)
{
    return HBUZZER_privateStartTone(HBUZZER_PRIORITY_LOW,
                                    (u16)HBUZZER_BLIP_FREQ_HZ,
                                    (u16)HBUZZER_BLIP_DURATION_MS,
                                    1u);
}

HBUZZER_ReturnType_t HBUZZER_enumToneStart(HBUZZER_Priority_t Copy_Priority,
                                           u16 Copy_u16FreqHz)
{
    return HBUZZER_privateStartTone(Copy_Priority, Copy_u16FreqHz, 0u, 0u);
}
HBUZZER_ReturnType_t HBUZZER_enumToneStartTimed(HBUZZER_Priority_t Copy_Priority,
                                                u16 Copy_u16FreqHz,
                                                u16 Copy_u16DurationMs)
{
    return HBUZZER_privateStartTone(Copy_Priority, Copy_u16FreqHz, Copy_u16DurationMs, 1u);
}

HBUZZER_ReturnType_t HBUZZER_enumToneStop(HBUZZER_Priority_t Copy_Priority)
{
    if (Global_u8Initialized == 0u)                        { return HBUZZER_NOK; }
    if ((u32)Copy_Priority >= (u32)HBUZZER_PRIORITY_COUNT) { return HBUZZER_INVALID_PARAM; }

    taskENTER_CRITICAL();

    /* You may silence YOUR OWN sound, never someone else's.        */
    if ((Global_eMode != HBUZZER_MODE_SILENT) &&
        (Global_eActivePriority == Copy_Priority))
    {
        /* Same frozen-output cure, from task context: route the stop
         * through the ISR's two-phase teardown. CCR=0 preloaded ->
         * output latches LOW at the next update -> DeInit freezes
         * the defined level. One period of residual sound (~1 ms at
         * 1 kHz) is the price of a defined end state — accepted.   */
        if (Global_eMode == HBUZZER_MODE_SUSTAINED)
        {
            Global_eMode = HBUZZER_MODE_TIMED;          /* join the ISR path */
            Global_u32PeriodsRemaining = 2u;            /* 2 = phase 1 next tick */
            if (MTIM_EnableUpdateInterrupt(HBUZZER_TIMER, HBUZZER_privateUpdateISR) != MTIM_OK)
            {
                /* Arming failed: fall back to the immediate stop —
                 * an active-level freeze in an error corner beats a
                 * tone that ignores Stop. The next Start repairs.   */
                (void)MTIM_DeInit(HBUZZER_TIMER);
                Global_eMode = HBUZZER_MODE_SILENT;
                taskEXIT_CRITICAL();
                return HBUZZER_NOK;
            }
        }
        /* TIMED already: its own ISR completes the teardown — do
         * nothing here; the pin ends LOW by the mechanism.         */
        taskEXIT_CRITICAL();
        return HBUZZER_OK;
    }

    taskEXIT_CRITICAL();
    return HBUZZER_NOK;    /* nothing sounding, or not yours */
}

HBUZZER_ReturnType_t HBUZZER_enumIsSounding(u8 *Copy_pu8Sounding)
{
    if (Global_u8Initialized == 0u)  { return HBUZZER_NOK; }
    if (Copy_pu8Sounding == (u8 *)0) { return HBUZZER_INVALID_PARAM; }

    *Copy_pu8Sounding = (u8)((Global_eMode != HBUZZER_MODE_SILENT) ? 1u : 0u);
    return HBUZZER_OK;
}
