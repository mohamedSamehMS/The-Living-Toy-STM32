/*
 * HBUZZER_interface.h — shared buzzer HAL driver, PUBLIC INTERFACE
 *
 *  THE ONE-OWNER RULE, AUDIO EDITION: one passive buzzer, three
 *  concerns (input blip / FOTA tones / future Music Maker). This
 *  driver owns the transducer; nobody else touches TIM10 or PB8.
 *  Call sites stay ONE-LINERS forever — arbitration lives here.
 *
 *  ARBITRATION (the load-bearing contract — replaces FOTA-33's
 *  getter-check by design):
 *    - a request at HIGHER priority than the active tone PREEMPTS it
 *    - EQUAL priority REPLACES it (rapid blips merge into one longer
 *      blip — busy input = continuous light feedback)
 *    - LOWER priority is DROPPED, returning HBUZZER_BUSY — silent by
 *      design (a blip during a FOTA tone is the rule WORKING, not an
 *      error; fire-and-forget callers ignore the status)
 *  Consequence: during a FOTA progress tone or failure burst, blips
 *  are suppressed with ZERO coupling — no FOTA-state query anywhere
 *  in this driver or at the blip's call site. The tones themselves
 *  are the FOTA-state signal. (Session gaps with no tone sounding
 *  may blip — harmless by FOTA-33's own rationale; if absolute
 *  session silence is ever wanted, a getter check is a one-line
 *  tightening at FOTA integration day.)
 *
 *  TIMING MODEL (why every API is non-blocking):
 *    - SUSTAINED tones (progress): PWM only, NO interrupt at all —
 *      runs until ToneStop. Zero CPU, zero ISRs while sounding.
 *    - TIMED tones (blip, failure burst): TIM10's own update
 *      interrupt counts PWM periods and stops the tone at the
 *      deadline — hardware-timed, precise to one PWM period, needs
 *      no service call, no task, no calling-context cooperation.
 *      Fires only while a timed tone sounds (~50 ISRs per blip,
 *      ~150 us total; zero when idle).
 *  THE FIRE-AND-FORGET PROPERTY: Blip() returns in microseconds and
 *  the sound ends by itself, whatever any task is doing meanwhile.
 *
 *  CONCURRENCY CONTRACT: all functions are TASK-context-only (never
 *  call from an ISR). Internally protected by kernel critical
 *      sections — this is the project's FIRST HAL driver to link the
 *      FreeRTOS API directly (platform ruling: the kernel is platform
 *      infrastructure; the register-ownership layering rules are
 *      unchanged). Multiple tasks may call concurrently. IsSounding
 *      is safe from any context (aligned reads).
 *
 *  BOOT-ORDER CONTRACT (prerequisites of HBUZZER_enumInit):
 *      1. MRCC_Init()
 *      2. MRCC_EnablePeripheralClock(MRCC_GPIOB)   // the buzzer pin
 *         MRCC_EnablePeripheralClock(MRCC_TIM10)   // the tone engine
 *      3. MNVIC_Init();
 *         MNVIC_SetPriority(MNVIC_TIM1_UP_TIM10, 5, 0)   // TIM10
 *         rides TIM1's UPDATE vector (position 25, shared) — set
 *         BEFORE the first timed tone (priority before enable)
 *      4. MTIM_Init()
 *      5. HBUZZER_enumInit()   (wires PB8 AF3; stays silent)
 *
 *  Created on: Oct 2, 2026
 *      Author: dell
 */
#ifndef HBUZZER_INTERFACE_H_
#define HBUZZER_INTERFACE_H_

#include "STD_TYPES.h"

/* Driver status — the library convention */
typedef enum
{
    HBUZZER_OK = 0,             /* Tone started / stopped / state read   */
    HBUZZER_NOK,                /* Not initialized, or an MCAL failure
                                   (check the boot-order contract)      */
    HBUZZER_INVALID_PARAM,      /* Frequency/duration/priority out of
                                   range (frequency reachability is
                                   validated by MTIM's solver — a target
                                   the clock cannot produce is rejected,
                                   never silently mis-tuned)            */
    HBUZZER_BUSY                /* A HIGHER-priority tone is active;
                                   request dropped (silent by design —
                                   see the arbitration contract)        */
} HBUZZER_ReturnType_t;

/* Sound ownership — the arbitration ladder. LOW yields to everyone;
 * MEDIUM (FOTA) overrides LOW; HIGH is reserved for Music Maker.
 * Nothing above HIGH exists: three owners is the complete set.      */
typedef enum
{
    HBUZZER_PRIORITY_LOW = 0,   /* the NEC input blip                   */
    HBUZZER_PRIORITY_MEDIUM,    /* FOTA status tones (§5.6)             */
    HBUZZER_PRIORITY_HIGH,      /* reserved: Music Maker (future)       */
    HBUZZER_PRIORITY_COUNT      /* validation bound — not a valid input */
} HBUZZER_Priority_t;

/* Wire the pin (PB8 AF3) and reset state to SILENT. Does NOT touch
 * TIM10 — no sound until a tone is requested. Idempotent.           */
HBUZZER_ReturnType_t HBUZZER_enumInit(void);

/* THE INPUT BLIP (FOTA-33's owner ruling): config'd ~1 kHz, ~50 ms,
 * LOW priority, fire-and-forget. Returns in microseconds; the tone
 * ends by hardware. Rate-limiting (one blip per fresh press, repeats
 * silent) is the CALL SITE's job — IsRepeat is input-domain data.   */
HBUZZER_ReturnType_t HBUZZER_enumBlip(void);

/* Sustained tone: sounds until ToneStop(same priority) or a higher
 * priority preempts. THE FOTA progress-tone primitive. No interrupt
 * runs while it sounds.                                              */
HBUZZER_ReturnType_t HBUZZER_enumToneStart(HBUZZER_Priority_t Copy_Priority,
                                           u16 Copy_u16FreqHz);

/* Timed tone: sounds for Copy_u16DurationMs, then stops itself
 * (TIM10's update interrupt counts the periods — hardware-timed,
 * no caller obligations). THE blip/failure-burst primitive. Duration
 * precision: +/- one PWM period (1 ms at 1 kHz).                    */
HBUZZER_ReturnType_t HBUZZER_enumToneStartTimed(HBUZZER_Priority_t Copy_Priority,
                                                u16 Copy_u16FreqHz,
                                                u16 Copy_u16DurationMs);

/* Stop the active tone — but ONLY if it belongs to Copy_Priority
 * (you may silence your own sound, never someone else's). If a
 * higher priority preempted yours, this returns NOK and changes
 * nothing: the sound isn't yours anymore.                            */
HBUZZER_ReturnType_t HBUZZER_enumToneStop(HBUZZER_Priority_t Copy_Priority);

/* Diagnostic: 1 while any tone sounds. Safe from any context.       */
HBUZZER_ReturnType_t HBUZZER_enumIsSounding(u8 *Copy_pu8Sounding);


/* DIAGNOSTIC R2D — remove after the debug flash: ISR / teardown counters */
HBUZZER_ReturnType_t HBUZZER_enumGetDebugCounts(u32 *Copy_pu32Isr, u32 *Copy_pu32Teardown);

#endif /* HBUZZER_INTERFACE_H_ */
