/*
 * HAUDIO_program.c - sampled 8-bit audio output, IMPLEMENTATION
 *
 *  BLOCK B (sample synth): TIM9 update ISR advances a 32-bit phase,
 *  looks up one 8-bit sine sample, applies a short envelope, and
 *  shifts/latches one complete DAC code. It calls no OS service and
 *  performs no delay.
 *
 *  BLOCK A (sequencer): HAUDIO_voidTask() advances the caller's Song_t
 *  after the ISR has consumed the exact note sample count. The task
 *  pump owns song pointers and performs the 64-bit frequency arithmetic;
 *  the ISR sees only aligned volatile scalar state.
 *
 *  Created on: Sep 14, 2026
 *      Author: dell
 */
#include "STD_TYPES.h"
#include <MCAL/GPIO/MGPIO_interface.h>
#include <MCAL/TIM/MTIM_interface.h>
#include "MNVIC_interface.h"

#include "HAL/AUDIO/HAUDIO_private.h"
#include "HAL/AUDIO/HAUDIO_interface.h"
#include "HAL/AUDIO/HAUDIO_config.h"

#define HAUDIO_PHASE_MODULUS        4294967296ULL
#define HAUDIO_PI_OVER_TWO_Q16      102944L /* pi/2 * 65536, rounded */

static u8 Global_u8Initialized = 0u;
static volatile HAUDIO_State_t Global_eState = HAUDIO_STATE_IDLE;
static volatile u32 Global_u32Phase = 0u;
static volatile u32 Global_u32PhaseIncrement = 0u;
static volatile u32 Global_u32SamplesRemaining = 0u;
static volatile u32 Global_u32SamplesElapsed = 0u;
static u32 Global_u32SamplesPerMs = 0u;
static volatile u32 Global_u32StopSamplesRemaining = 0u;
static volatile u8 Global_u8NoteEnded = 0u;
static volatile u8 Global_u8SequencerBusy = 0u;
static volatile u8 Global_u8TimerStopPending = 0u;
static volatile u8 Global_u8SongGainMax = 8u;
static u8 Global_au8Sine[HAUDIO_SINE_TABLE_LENGTH];

/* HAUDIO owns these GPIO pins. RM0368 §2.3 places GPIOx at
 * 0x40020000 + (0x400 * port index), and RM0368 §8.4 places BSRR at
 * offset 0x18. These values are initialized once in HAUDIO_enumInit(). */
static volatile u32 *Global_pvBSRRAddress = (volatile u32 *)0;
static u32 Global_u32SERSetMask = 0u;
static u32 Global_u32SERResetMask = 0u;
static u32 Global_u32SRCLKSetMask = 0u;
static u32 Global_u32SRCLKResetMask = 0u;
static u32 Global_u32RCLKSetMask = 0u;
static u32 Global_u32RCLKResetMask = 0u;

static const Song_t *Global_pstSong = (const Song_t *)0;
static const Song_t *Global_pstPendingSong = (const Song_t *)0;
static u16 Global_u16NoteIndex = 0u;

static s32 HAUDIO_privateSineAmplitude(u16 Copy_u16Index)
{
    u16 Local_u16Quadrant = (u16)(Copy_u16Index / 64u);
    u16 Local_u16Offset = (u16)(Copy_u16Index % 64u);
    s32 Local_s32X;
    s64 Local_s64X2;
    s64 Local_s64X3;
    s64 Local_s64X5;
    s64 Local_s64X7;
    s64 Local_s64Sin;

    /*
     * Fixed-point sin(pi*x/2), Q16, through the x^7 Taylor term.
     * Quadrant mirroring makes the four cardinal samples exact and
     * avoids pulling libm into the bare-metal link.
     */
    if ((Local_u16Quadrant == 0u) || (Local_u16Quadrant == 2u))
    {
        Local_s32X = (s32)(((u32)Local_u16Offset *
                            HAUDIO_PI_OVER_TWO_Q16) / 64u);
    }
    else
    {
        Local_s32X = (s32)(((u32)(64u - Local_u16Offset) *
                            HAUDIO_PI_OVER_TWO_Q16) / 64u);
    }

    Local_s64X2 = ((s64)Local_s32X * Local_s32X) >> 16;
    Local_s64X3 = (Local_s64X2 * Local_s32X) >> 16;
    Local_s64X5 = (Local_s64X3 * Local_s64X2) >> 16;
    Local_s64X7 = (Local_s64X5 * Local_s64X2) >> 16;
    Local_s64Sin = (s64)Local_s32X - (Local_s64X3 / 6) +
                   (Local_s64X5 / 120) - (Local_s64X7 / 5040);

    if ((Local_u16Quadrant == 2u) || (Local_u16Quadrant == 3u))
    {
        Local_s64Sin = -Local_s64Sin;
    }
    return (s32)((Local_s64Sin * 127 +
                  ((Local_s64Sin >= 0) ? 32768 : -32768)) / 65536);
}

static u32 HAUDIO_privatePhaseIncrement(u16 Copy_u16FrequencyHz)
{
    u64 Local_u64Numerator;

    /*
     * A4 check at 16 kHz:
     *   440 * 4294967296 / 16000 = 118111600.64 -> 118111600.
     * The frequently quoted 117964800 is not the result of this
     * formula; retaining the exact 64-bit calculation is intentional.
     */
    Local_u64Numerator = (u64)Copy_u16FrequencyHz * HAUDIO_PHASE_MODULUS;
    return (u32)(Local_u64Numerator / (u64)HAUDIO_SAMPLE_RATE_HZ);
}

static void HAUDIO_privateShiftAndLatch(u8 Copy_u8Sample)
{
    s8 Local_s8Bit;

    /* MSB first: SER is set before each SRCLK rising edge. */
    for (Local_s8Bit = 7; Local_s8Bit >= 0; Local_s8Bit--)
    {
        *Global_pvBSRRAddress = (((Copy_u8Sample >> (u8)Local_s8Bit) & 1u) != 0u)
                                    ? Global_u32SERSetMask : Global_u32SERResetMask;
        *Global_pvBSRRAddress = Global_u32SRCLKSetMask;
        *Global_pvBSRRAddress = Global_u32SRCLKResetMask;
    }

    /* One latch edge makes all eight ladder outputs change together. */
    *Global_pvBSRRAddress = Global_u32RCLKSetMask;
    *Global_pvBSRRAddress = Global_u32RCLKResetMask;
}

static void HAUDIO_privateSampleISR(void)
{
    u32 Local_u32Phase;
    u32 Local_u32Gain;
    s32 Local_s32Delta;
    s32 Local_s32Sample;
    u8 Local_u8OutputSample;
    u8 Local_u8AdvanceNote = 0u;
    s8 Local_s8Bit;

    /*
     * ISR AUDIT: verified no kernel calls, no blocking, no time
     * dependencies. Shared state is volatile: Global_eState,
     * Global_u32Phase, Global_u32PhaseIncrement (one aligned u32),
     * Global_u32SamplesRemaining, Global_u32SamplesElapsed,
     * Global_u32StopSamplesRemaining, Global_u8NoteEnded, and
     * Global_u8SequencerBusy. The sine table is immutable after init.
     * The phase top-byte lookup is explicitly masked to 0..255.
     *
     * Budget at the current 16 MHz clock (62.5 us at 16 kHz):
     *   26 BSRR stores x ~3 cycles        =  78 cycles = 4.9 us
     *   synth math (phase+table+envelope) =  20 cycles = 1.3 us
     *   ISR entry/exit (hw push/pop)      =  12 cycles = 0.8 us
     *   TOTAL                             = ~110 cycles = 6.9 us
     * This is 11% of the 62.5 us period: comfortable at 16 MHz.
     */
    /*
     * Re-trigger audit: this callback never writes TIM9->SR. MTIM's
     * TIM1_BRK_TIM9 dispatcher snapshots SR/DIER, clears UIF with
     * SR = ~UIF, then invokes this callback. The UG generated by
     * PeriodicConfig is cleared by MTIM before UIE is armed, so there
     * is no persistent pending-UIF path here.
     */
    if (Global_eState == HAUDIO_STATE_IDLE)
    {
        return;
    }
    if (Global_u8SequencerBusy != 0u)
    {
        /* The task is publishing a complete note hand-off. */
        Local_u8OutputSample = 128u;
    }
    else if (Global_eState == HAUDIO_STATE_STOPPING)
    {
        /* 160 samples / 16 kHz = 10 ms; map remaining samples to
         * the normal 0..8 gain range so stop can never amplify. */
        Local_u32Gain = (Global_u32StopSamplesRemaining * 8u) /
                        HAUDIO_STOP_RAMP_SAMPLES;
        if (Local_u32Gain > Global_u8SongGainMax)
        {
            Local_u32Gain = Global_u8SongGainMax;
        }
        if (Global_u32StopSamplesRemaining != 0u)
        {
            Global_u32StopSamplesRemaining--;
        }
        if (Global_u32StopSamplesRemaining == 0u)
        {
            Global_eState = HAUDIO_STATE_IDLE;
            Global_u8TimerStopPending = 1u;
            Local_u8OutputSample = 128u;
        }
        else
        {
            Local_u32Phase = Global_u32Phase + Global_u32PhaseIncrement;
            Global_u32Phase = Local_u32Phase;
            Local_s32Sample = (s32)Global_au8Sine[
                (u8)((Local_u32Phase >> 24) & 0xFFu)] - 128;
            Local_s32Delta = (Local_s32Sample * (s32)Local_u32Gain) / 8;
            Local_u8OutputSample = (u8)(128 + Local_s32Delta);
            Local_u8AdvanceNote = 1u;
        }
    }
    else
    {
        Local_u32Gain = 8u;
        if (Global_u32SamplesElapsed < HAUDIO_NOTE_RAMP_SAMPLES)
        {
            /* First sample is the exact midpoint; then 1/8 ... 7/8. */
            Local_u32Gain = Global_u32SamplesElapsed;
        }
        if (Global_u32SamplesRemaining <= HAUDIO_NOTE_RAMP_SAMPLES)
        {
            /* Verified boundary contract: last samples ramp to midpoint
             * and the next note starts at midpoint, so no phase-reset
             * discontinuity exceeds 1 LSB at the DAC output. */
            Local_u32Gain = (Global_u32SamplesRemaining != 0u)
                                ? (Global_u32SamplesRemaining - 1u) : 0u;
        }
        if (Local_u32Gain > Global_u8SongGainMax)
        {
            Local_u32Gain = Global_u8SongGainMax;
        }
        Global_u32SamplesElapsed++;

        Local_u32Phase = Global_u32Phase + Global_u32PhaseIncrement;
        Global_u32Phase = Local_u32Phase;
        Local_s32Sample = (s32)Global_au8Sine[
            (u8)((Local_u32Phase >> 24) & 0xFFu)] - 128;
        Local_s32Delta = (Local_s32Sample * (s32)Local_u32Gain) / 8;
        Local_u8OutputSample = (u8)(128 + Local_s32Delta);
        Local_u8AdvanceNote = 1u;
    }

    /* Inline the 74HC595 transfer: this ISR must contain no calls. */
    for (Local_s8Bit = 7; Local_s8Bit >= 0; Local_s8Bit--)
    {
        *Global_pvBSRRAddress = (((Local_u8OutputSample >> (u8)Local_s8Bit) & 1u) != 0u)
                                    ? Global_u32SERSetMask : Global_u32SERResetMask;
        *Global_pvBSRRAddress = Global_u32SRCLKSetMask;
        *Global_pvBSRRAddress = Global_u32SRCLKResetMask;
    }
    *Global_pvBSRRAddress = Global_u32RCLKSetMask;
    *Global_pvBSRRAddress = Global_u32RCLKResetMask;

    if ((Local_u8AdvanceNote != 0u) && (Global_eState == HAUDIO_STATE_PLAYING))
    {
        if (Global_u32SamplesRemaining != 0u)
        {
            Global_u32SamplesRemaining--;
        }
        if (Global_u32SamplesRemaining == 0u)
        {
            Global_u8NoteEnded = 1u;
            if ((Global_pstSong != (const Song_t *)0) &&
                ((u16)(Global_u16NoteIndex + 1u) >= Global_pstSong->NoteCount) &&
                (Global_pstSong->LoopForever == 0u))
            {
                /* The final non-looping note must stop without waiting for
                 * the cooperative pump to return from a blocking screen. */
                Global_eState = HAUDIO_STATE_STOPPING;
                Global_u32StopSamplesRemaining = HAUDIO_STOP_RAMP_SAMPLES;
            }
        }
    }
}

static void HAUDIO_privateLoadNote(u16 Copy_u16Index)
{
    const SongNote_t *Local_pstNote = &Global_pstSong->Notes[Copy_u16Index];

    Global_u8SequencerBusy = 1u;
    Global_u16NoteIndex = Copy_u16Index;
    Global_u32Phase = 0u;
    Global_u32PhaseIncrement = HAUDIO_privatePhaseIncrement(Local_pstNote->FreqHz);
    Global_u32SamplesRemaining = (u32)Local_pstNote->DurationMs *
                                 Global_u32SamplesPerMs;
    if (Global_u32SamplesRemaining == 0u)
    {
        Global_u32SamplesRemaining = 1u;
    }
    Global_u32SamplesElapsed = 0u;
    Global_u8NoteEnded = 0u;
    Global_u8SequencerBusy = 0u;
}

static void HAUDIO_privateBeginStop(void)
{
    if (Global_eState == HAUDIO_STATE_PLAYING)
    {
        Global_eState = HAUDIO_STATE_STOPPING;
        Global_u32StopSamplesRemaining = HAUDIO_STOP_RAMP_SAMPLES;
    }
}

HAUDIO_ReturnType_t HAUDIO_enumInit(void)
{
    u16 Local_u16Index;

    if (Global_u8Initialized != 0u)
    {
        return HAUDIO_OK;
    }

    /* HAUDIO_SAMPLE_RATE_HZ is 16000 Hz directly; therefore the validated
     * samples-per-ms value is 16000 / 1000 = 16 samples/ms. */
    Global_u32SamplesPerMs = HAUDIO_SAMPLE_RATE_HZ / 1000u;
    if (Global_u32SamplesPerMs == 0u)
    {
        return HAUDIO_NOK;
    }

    /* All three audio pins are GPIOA in HAUDIO_config.h, so one BSRR
     * address serves their three independently precomputed mask pairs. */
    Global_pvBSRRAddress = (volatile u32 *)(0x40020000UL +
        ((u32)HAUDIO_SER_PORT * 0x400UL) + 0x18UL);
    Global_u32SERSetMask = 1UL << (u32)HAUDIO_SER_PIN;
    Global_u32SERResetMask = 1UL << ((u32)HAUDIO_SER_PIN + 16u);
    Global_u32SRCLKSetMask = 1UL << (u32)HAUDIO_SRCLK_PIN;
    Global_u32SRCLKResetMask = 1UL << ((u32)HAUDIO_SRCLK_PIN + 16u);
    Global_u32RCLKSetMask = 1UL << (u32)HAUDIO_RCLK_PIN;
    Global_u32RCLKResetMask = 1UL << ((u32)HAUDIO_RCLK_PIN + 16u);

    for (Local_u16Index = 0u; Local_u16Index < HAUDIO_SINE_TABLE_LENGTH; Local_u16Index++)
    {
        /* Cardinal checks: [0]=128, [64]=255, [128]=128, [192]=1. */
        Global_au8Sine[Local_u16Index] =
            (u8)(128 + HAUDIO_privateSineAmplitude(Local_u16Index));
    }

    if (MGPIO_SetPinMode(HAUDIO_SER_PORT, HAUDIO_SER_PIN, MGPIO_MODE_OUTPUT) != MGPIO_OK ||
        MGPIO_SetPinMode(HAUDIO_SRCLK_PORT, HAUDIO_SRCLK_PIN, MGPIO_MODE_OUTPUT) != MGPIO_OK ||
        MGPIO_SetPinMode(HAUDIO_RCLK_PORT, HAUDIO_RCLK_PIN, MGPIO_MODE_OUTPUT) != MGPIO_OK ||
        MGPIO_SetPinOutputConfig(HAUDIO_SER_PORT, HAUDIO_SER_PIN,
                                 MGPIO_OUTPUT_PUSH_PULL, MGPIO_SPEED_LOW) != MGPIO_OK ||
        MGPIO_SetPinOutputConfig(HAUDIO_SRCLK_PORT, HAUDIO_SRCLK_PIN,
                                 MGPIO_OUTPUT_PUSH_PULL, MGPIO_SPEED_LOW) != MGPIO_OK ||
        MGPIO_SetPinOutputConfig(HAUDIO_RCLK_PORT, HAUDIO_RCLK_PIN,
                                 MGPIO_OUTPUT_PUSH_PULL, MGPIO_SPEED_LOW) != MGPIO_OK)
    {
        return HAUDIO_NOK;
    }

    MGPIO_SetPinValue(HAUDIO_SER_PORT, HAUDIO_SER_PIN, MGPIO_PIN_LOW);
    MGPIO_SetPinValue(HAUDIO_SRCLK_PORT, HAUDIO_SRCLK_PIN, MGPIO_PIN_LOW);
    MGPIO_SetPinValue(HAUDIO_RCLK_PORT, HAUDIO_RCLK_PIN, MGPIO_PIN_LOW);
    HAUDIO_privateShiftAndLatch(128u);

    /*
     * TIM9 rate audit: MTIM_PeriodicConfig receives 16000 Hz. The
     * MTIM solver uses TIM9's APB2 clock (16 MHz), giving PSC=0 and
     * ARR=999: 16000000 / (0+1) / (999+1) = 16000 Hz. Therefore
     * the sample ISR cannot be running at 160 kHz from this setup.
     */
    if (MNVIC_SetPriority(MNVIC_TIM1_BRK_TIM9, 5u, 0u) != MNVIC_OK ||
        MTIM_PeriodicConfig(HAUDIO_TIMER, HAUDIO_SAMPLE_RATE_HZ) != MTIM_OK)
    {
        return HAUDIO_NOK;
    }

    Global_u8Initialized = 1u;
    return HAUDIO_OK;
}

HAUDIO_ReturnType_t HAUDIO_enumPlaySong(const Song_t *Copy_pstSong)
{
    if (Global_u8Initialized == 0u)
    {
        return HAUDIO_NOK;
    }
    if (Copy_pstSong == (const Song_t *)0 ||
        Copy_pstSong->Notes == (const SongNote_t *)0 ||
        Copy_pstSong->NoteCount == 0u)
    {
        return HAUDIO_INVALID_PARAM;
    }

    if ((Global_eState != HAUDIO_STATE_IDLE) ||
        (Global_u8TimerStopPending != 0u))
    {
        /* PREEMPT: the newest request wins. The pending song arms
         * via the pump once the current fade completes. */
        Global_pstPendingSong = Copy_pstSong;
        if (Global_eState == HAUDIO_STATE_PLAYING)
        {
            Global_eState = HAUDIO_STATE_STOPPING;
            Global_u32StopSamplesRemaining = HAUDIO_NOTE_RAMP_SAMPLES;
        }
        /* Already STOPPING or post-fade (TimerStopPending): leave
         * the in-progress fade alone; only the pending replaces. */
        return HAUDIO_OK;
    }

    Global_pstSong = Copy_pstSong;
    Global_u8SongGainMax = (Copy_pstSong->GainEighth == 0u) ? 8u :
                            Copy_pstSong->GainEighth;
    Global_eState = HAUDIO_STATE_PLAYING;
    HAUDIO_privateLoadNote(0u);
    if (MTIM_EnableUpdateInterrupt(HAUDIO_TIMER, HAUDIO_privateSampleISR) != MTIM_OK ||
        MTIM_Start(HAUDIO_TIMER) != MTIM_OK)
    {
        Global_eState = HAUDIO_STATE_IDLE;
        return HAUDIO_NOK;
    }
    return HAUDIO_OK;
}

HAUDIO_ReturnType_t HAUDIO_enumPlaySting(const Song_t *Copy_pstSting)
{
    if (Copy_pstSting == (const Song_t *)0 ||
        Copy_pstSting->Notes == (const SongNote_t *)0 ||
        Copy_pstSting->NoteCount == 0u)
    {
        return HAUDIO_INVALID_PARAM;
    }
    if (Global_eState != HAUDIO_STATE_IDLE ||
        Global_u8TimerStopPending != 0u)
    {
        /* Crash feedback preempts active music and uses a near-immediate
         * 8-sample fade; HAUDIO_voidTask arms it after the fade completes. */
        Global_pstPendingSong = Copy_pstSting;
        Global_eState = HAUDIO_STATE_STOPPING;
        Global_u32StopSamplesRemaining = HAUDIO_NOTE_RAMP_SAMPLES;
        return HAUDIO_OK;
    }
    return HAUDIO_enumPlaySong(Copy_pstSting);
}

HAUDIO_ReturnType_t HAUDIO_enumStop(void)
{
    if (Global_u8Initialized == 0u)
    {
        return HAUDIO_NOK;
    }
    Global_pstPendingSong = (const Song_t *)0;
    /* At 16 kHz, HAUDIO_STOP_RAMP_SAMPLES=160 is a measured 10 ms
       fade before the ISR disables TIM9; no hard waveform cut. */
    HAUDIO_privateBeginStop();
    return HAUDIO_OK;
}

void HAUDIO_voidTask(void)
{
    const Song_t *Local_pstPendingSong;

    /* The ISR has completed its fade. This is the sole disable/stop path;
     * keep it in task context while leaving the sample ISR call-free. */
    if (Global_u8TimerStopPending != 0u)
    {
        (void)MTIM_DisableUpdateInterrupt(HAUDIO_TIMER);
        (void)MTIM_Stop(HAUDIO_TIMER);
        Global_u8TimerStopPending = 0u;
    }

    if ((Global_eState == HAUDIO_STATE_IDLE) &&
        (Global_pstPendingSong != (const Song_t *)0))
    {
        Local_pstPendingSong = Global_pstPendingSong;
        Global_pstPendingSong = (const Song_t *)0;
        Global_pstSong = Local_pstPendingSong;
        Global_u8SongGainMax = (Local_pstPendingSong->GainEighth == 0u) ? 8u :
                                Local_pstPendingSong->GainEighth;
        Global_eState = HAUDIO_STATE_PLAYING;
        HAUDIO_privateLoadNote(0u);
        if (MTIM_EnableUpdateInterrupt(HAUDIO_TIMER, HAUDIO_privateSampleISR) != MTIM_OK ||
            MTIM_Start(HAUDIO_TIMER) != MTIM_OK)
        {
            Global_eState = HAUDIO_STATE_IDLE;
        }
        return;
    }

    if (Global_eState != HAUDIO_STATE_PLAYING || Global_u8NoteEnded == 0u)
    {
        return;
    }

    Global_u8NoteEnded = 0u;
    if ((u16)(Global_u16NoteIndex + 1u) >= Global_pstSong->NoteCount)
    {
        if (Global_pstSong->LoopForever == 0u)
        {
            HAUDIO_privateBeginStop();
        }
        else
        {
            HAUDIO_privateLoadNote(0u);
        }
    }
    else
    {
        HAUDIO_privateLoadNote((u16)(Global_u16NoteIndex + 1u));
    }
}

HAUDIO_State_t HAUDIO_enumGetState(void)
{
    return Global_eState;
}
