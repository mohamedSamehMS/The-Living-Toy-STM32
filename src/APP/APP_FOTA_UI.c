/*
 * APP_FOTA_UI.c � the FOTA update experience (the R5 buzzer wiring).
 *
 *  THE FOTA SEAT'S v1.0 DESIGN, PRESERVED: the layout, the stateless
 *  draw primitives, the pattern engine's rhythm (heartbeat during
 *  progress, bounded burst on failure, the success pair), the
 *  draw-before law honored at both stalls, the panel-persistence
 *  physics of the DO-NOT-POWER-OFF screens. Every screen coordinate
 *  and every string is theirs, verbatim.
 *
 *  THE WIRING (this revision � File 2 of the FOTA package): the
 *  transducer primitive swaps. Their v1.0 drove an ACTIVE buzzer's
 *  GPIO pin on/off through UI_privateBeepSet; the owner has ruled the
 *  part is PASSIVE (bench-proven: distinct pitches through TIM10 PWM)
 *  and the shared driver HBUZZER is BUILT AND CLOSED on this tree.
 *  The swap, per the countersigned palette (FOTA-61's final form):
 *
 *    heartbeat element   60 ms  -> HBUZZER_enumToneStartTimed(MEDIUM, 750, 60)
 *    failure burst elem  80 ms  -> HBUZZER_enumToneStartTimed(MEDIUM, 2500, 80)
 *    success pair        80 ms  -> HBUZZER_enumToneStartTimed(MEDIUM, 1200, 80) x2
 *    attention pair      80 ms  -> the same 1200 pair (Available's chirp)
 *
 *  THE ARBITRATION (FOTA-33, formally superseded � the FOTA seat's
 *  registered approval): in-driver priority preemption replaces the
 *  call-site getter gate. FOTA's MEDIUM tones automatically suppress
 *  the input blip (LOW) inside HBUZZER � zero coupling; the tones ARE
 *  the state signal. The UI's pattern engine needs NO arbitration
 *  knowledge: it fires tones; the driver arbitrates.
 *
 *  THE TIMED-TONE PROPERTY that makes the swap clean: every HBUZZER
 *  timed tone is fire-and-forget � it ENDS ITSELF by hardware (TIM10's
 *  two-phase teardown). The pattern engine's BeepSet(0) calls become
 *  no-ops (the tone already ended); its BeepSet(1) calls become tone
 *  starts. The engine's STATE MACHINERY (the on-windows, the burst
 *  count, the pacing) is preserved verbatim � it now paces tone STARTS
 *  instead of pin levels, and the hardware ends each element exactly
 *  when the engine's window said it should.
 *
 *  THE BLOCKING-BEEP QUESTION (their v1.0 used SetBusyWait_ms in the
 *  success/attention pairs): on this tree the shim's calibrated loop
 *  serves it � pre-scheduler-safe, HUB-context-blocked ~80 ms with the
 *  wire idle (their own documented safety condition: "the wire is idle
 *  at the DONE terminal"). Preserved as blocking, deliberately: the
 *  blocking pairs only fire at terminals, where their v1.0 analysis
 *  already proved the wire idle.
 *
 *  FONT LAW (theirs, unchanged): all strings uppercase, 0x20-0x5F.
 *
 *  Revision history:
 *    v1.0        � the FOTA seat's first delivery (patterns + active-GPIO stubs)
 *    R5-wiring   � THIS: the passive transducer swap onto HBUZZER (File 2 of 5)
 */

#include "STD_TYPES.h"

#include <MCAL/GPIO/MGPIO_interface.h>
#include <MCAL/SYSTICK/MSYSTICK_interface.h>   /* the shim serves the HUB task */
#include <MCAL/USART/MUSART_interface.h>

#include <HAL/TFT/HTFT_interface.h>
#include <HAL/BUZZER/HBUZZER_interface.h>     /* R5: the shared buzzer driver */

#include "APP/APP_FOTA_UI_config.h"
#include "APP/APP_FOTA_UI.h"


/* ---- self-contained palette (the FOTA seat's, verbatim) ---- */
#define UI_COLOR_BG      ((u16)0x0000u)   /* black              */
#define UI_COLOR_TEXT    ((u16)0xFFFFu)   /* white              */
#define UI_COLOR_BAR     ((u16)0x07E0u)   /* green              */
#define UI_COLOR_ERR     ((u16)0xF800u)   /* red                */

/* ---- layout (128x160 portrait; the FOTA seat's, verbatim) ---- */
#define UI_SCREEN_W      128u
#define UI_SCR_H         160u
#define UI_BAR_X         14u
#define UI_BAR_Y         56u
#define UI_BAR_W         100u
#define UI_BAR_H         18u
#define UI_TITLE_Y       16u
#define UI_PCT_Y         92u
#define UI_FOOT_Y         140u

/* ---- the beep engine (the FOTA seat's timings, verbatim) ---- */
#define UI_BEEP_PERIOD_MS    1500u   /* progress heartbeat spacing */
#define UI_BEEP_ON_MS          60u   /* one heartbeat beep length   */
#define UI_FAIL_ON_MS          80u   /* failure burst element on   */
#define UI_FAIL_OFF_MS         80u   /*        "          off     */
#define UI_FAIL_COUNT           6u   /* burst length (bounded)    */
#define UI_OK_ON_MS             80u   /* success beep (x2, block) */

/* ---- R5: THE TONE PALETTE (the countersigned �5.6 final form) ----
 * Frequencies: FOTA's �5.6 spec (750 progress / 2500 failure) + the
 * success pair FOTA-61 completed the palette with (~1.2 kHz). All
 * MEDIUM priority: FOTA owns the buzzer during its ceremony; the
 * input blip (LOW) is suppressed by the driver's arbitration.        */
#define UI_TONE_HEARTBEAT_HZ   750u
#define UI_TONE_FAIL_HZ       2500u
#define UI_TONE_OK_HZ         1200u

static u8  Global_u8BeepMode  = 0u;   /* 0 none, 1 heartbeat, 2 burst */
static u8  Global_u8BeepOn    = 0u;
static u32 Global_u32NextMs   = 0u;
static u32 Global_u32UntilMs  = 0u;
static u8  Global_u8BeepStep  = 0u;
static u8  Global_u8LastPct   = 0xFFu;

/* =====================================================================
 *  R5: THE TRANSDUCER PRIMITIVE � the swap's entire substance.
 *  The FOTA seat's UI_privateBeepSet(on) drove an ACTIVE buzzer's pin.
 *  The passive part + HBUZZER replace it: ON = a timed tone at the
 *  engine's chosen frequency for the engine's chosen duration (the
 *  tone ends ITSELF � hardware-timed); OFF = a no-op (a timed tone
 *  needs no explicit stop; only SUSTAINED tones do, and this engine
 *  never starts one). The HBUZZER status is ignored BY DESIGN: BUSY
 *  means a higher-priority tone owns the transducer � the arbitration
 *  working, not an error (FOTA-33's supersession, registered).
 * ===================================================================== */
static void UI_privateBeepOn(u16 Copy_u16FreqHz, u32 Copy_u32OnMs)
{
    (void)HBUZZER_enumToneStartTimed(HBUZZER_PRIORITY_MEDIUM,
                                     Copy_u16FreqHz,
                                     (u16)Copy_u32OnMs);
}

/* the engine's OFF hook � a no-op for timed tones (kept so the engine's
   state machinery stays verbatim; the calls remain, the pin does not) */
static void UI_privateBeepOff(void)
{
    /* Timed tones self-end: nothing to stop. (A SUSTAINED tone would
       need HBUZZER_enumToneStop(MEDIUM) � this engine never starts one.) */
}

/* =====================================================================
 *  The blocking pair (terminals only � the wire-idle condition, the
 *  FOTA seat's own documented safety analysis, preserved)
 * ===================================================================== */
static void UI_privateBeepPair(u16 Copy_u16FreqHz, u32 Copy_u32OnMs)
{
    UI_privateBeepOn(Copy_u16FreqHz, Copy_u32OnMs);
    (void)MSYSTICK_SetBusyWait_ms(Copy_u32OnMs);
    (void)MSYSTICK_SetBusyWait_ms(80u);                 /* the pair's gap */
    UI_privateBeepOn(Copy_u16FreqHz, Copy_u32OnMs);
    (void)MSYSTICK_SetBusyWait_ms(Copy_u32OnMs);        /* let it land    */
}

void APP_FOTA_UI_voidInit(void)
{
    /* R5: RETIRED � the FOTA seat's buzzer-pin GPIO setup. HBUZZER owns
       the pin (PB8, TIM10_CH1); it was initialized at the hub's boot.
       This function now only resets the engine's state. */
    Global_u8BeepMode = 0u;
    Global_u8BeepOn   = 0u;
}

/* =====================================================================
 *  Drawing helpers (the FOTA seat's, verbatim)
 * ===================================================================== */
static void UI_privateCentered1x(const char *Copy_pch, u16 Copy_u16Y, u16 Copy_u16Color)
{
    u16 Local_u16Len = 0u;
    while (Copy_pch[Local_u16Len] != '\0') { Local_u16Len++; }
    HTFT_voidDrawString((u16)((UI_SCREEN_W - (Local_u16Len * 8u)) / 2u), Copy_u16Y,
                        Copy_pch, Copy_u16Color, UI_COLOR_BG);
}

static void UI_privateCentered2x(const char *Copy_pch, u16 Copy_u16Y, u16 Copy_u16Color)
{
    u16 Local_u16Len = 0u;
    while (Copy_pch[Local_u16Len] != '\0') { Local_u16Len++; }
    HTFT_voidDrawString2x((u16)((UI_SCREEN_W - (Local_u16Len * 16u)) / 2u), Copy_u16Y,
                          Copy_pch, Copy_u16Color, UI_COLOR_BG);
}

/* Fixed-width right-aligned "NNN%" � no ghosting (the value law) */
static void UI_privateDrawPercent(u8 Copy_u8Pct)
{
    char Local_ach[5u];
    u8   Local_u8H = (u8)(Copy_u8Pct / 100u);
    u8   Local_u8T = (u8)((Copy_u8Pct / 10u) % 10u);

    Local_ach[0] = (Local_u8H != 0u) ? (char)('0' + Local_u8H) : ' ';
    Local_ach[1] = (Local_u8H != 0u || Local_u8T != 0u) ? (char)('0' + Local_u8T) : ' ';
    Local_ach[2] = (char)('0' + (Copy_u8Pct % 10u));
    Local_ach[3] = '%';
    Local_ach[4] = '\0';
    HTFT_voidDrawString2x(32u, UI_PCT_Y, Local_ach, UI_COLOR_TEXT, UI_COLOR_BG);
}

/* =====================================================================
 *  The transitions (the FOTA seat's, with the primitive swapped)
 * ===================================================================== */
void APP_FOTA_UI_Available(const char *Copy_pchVersion, u32 Copy_u32LengthBytes)
{
    char Local_ach[16u];
    u32 Local_u32Kb = (Copy_u32LengthBytes + 1023u) / 1024u;
    u8  Local_u8Idx;

    HTFT_voidFillScreen(UI_COLOR_BG);
    UI_privateCentered2x("UPDATE", UI_TITLE_Y, UI_COLOR_TEXT);

    /* the version string, verbatim (Ruling 2(a)) */
    if (Copy_pchVersion != 0)
    {
        UI_privateCentered1x(Copy_pchVersion, 64u, UI_COLOR_TEXT);
    }

    /* "<kb> KB" */
    Local_u8Idx = 0u;
    if (Local_u32Kb == 0u) { Local_ach[Local_u8Idx++] = '0'; }
    while ((Local_u32Kb != 0u) && (Local_u8Idx < 8u))
    {
        Local_ach[Local_u8Idx++] = (char)('0' + (Local_u32Kb % 10u));
        Local_u32Kb /= 10u;
    }
    { u8 a=0u, b=(u8)(Local_u8Idx-1u); while (a<b){ char t=Local_ach[a]; Local_ach[a]=Local_ach[b]; Local_ach[b]=t; a++; b--; } }
    Local_ach[Local_u8Idx++]=' '; Local_ach[Local_u8Idx++]='K'; Local_ach[Local_u8Idx++]='B';
    Local_ach[Local_u8Idx] = '\0';
    UI_privateCentered1x(Local_ach, 84u, UI_COLOR_TEXT);


    /* R5-FIX: the action prompt � the AUTO-mode design assumed the
           orchestrator drew it; hub mode's user has no other affordance.
           CH = arm the confirm gate; 0 = leave (the offer stands).       */
    HTFT_voidDrawString(0u, 110u, "CH:START 0:BACK",
    		UI_COLOR_TEXT, UI_COLOR_BG);

    /* R5: the attention pair � the ~1.2k success-family tone, blocking
       (the wire is idle: this is an offer screen, nothing is in flight) */
    UI_privateBeepPair(UI_TONE_OK_HZ, UI_OK_ON_MS);
}

void APP_FOTA_UI_Preparing(void)
{
    HTFT_voidFillScreen(UI_COLOR_BG);
    UI_privateCentered1x("PREPARING...", 76u, UI_COLOR_TEXT);
}

void APP_FOTA_UI_UpdatingStart(void)
{
    HTFT_voidFillScreen(UI_COLOR_BG);
    UI_privateCentered2x("UPDATE", UI_TITLE_Y, UI_COLOR_TEXT);
    HTFT_voidDrawRect((u16)(UI_BAR_X - 1u), (u16)(UI_BAR_Y - 1u),
                      (u16)(UI_BAR_W + 2u), (u16)(UI_BAR_H + 2u), UI_COLOR_TEXT);
    UI_privateDrawPercent(0u);
    HTFT_voidDrawString(0u, UI_FOOT_Y, "DO NOT POWER OFF", UI_COLOR_TEXT, UI_COLOR_BG);

    Global_u8LastPct  = 0u;
    Global_u8BeepMode = 1u;              /* the progress heartbeat */
    Global_u8BeepOn   = 0u;
    Global_u32NextMs  = MSYSTICK_GetElapsed_ms();
}

void APP_FOTA_UI_UpdatingProgress(u32 Copy_u32FramesDone, u32 Copy_u32FramesTotal)
{
    u8 Local_u8Pct;

    if ((Copy_u32FramesTotal == 0u) || (Copy_u32FramesDone > Copy_u32FramesTotal))
    {
        return;
    }
    Local_u8Pct = (u8)((Copy_u32FramesDone * 100u) / Copy_u32FramesTotal);
    if (Local_u8Pct == Global_u8LastPct)
    {
        return;                          /* integer-percent throttle */
    }

    /* incremental fill: only the new slice */
    HTFT_voidFillRect((u16)(UI_BAR_X + Global_u8LastPct), UI_BAR_Y,
                      (u16)(Local_u8Pct - Global_u8LastPct), UI_BAR_H, UI_COLOR_BAR);
    UI_privateDrawPercent(Local_u8Pct);
    Global_u8LastPct = Local_u8Pct;
}

void APP_FOTA_UI_Success(void)
{
    Global_u8BeepMode = 0u;
    UI_privateBeepOff();

    HTFT_voidFillScreen(UI_COLOR_BG);
    UI_privateCentered2x("DONE!", UI_TITLE_Y, UI_COLOR_BAR);
    UI_privateCentered1x("RESTARTING", 84u, UI_COLOR_TEXT);
    HTFT_voidDrawString(0u, UI_FOOT_Y, "DO NOT POWER OFF", UI_COLOR_TEXT, UI_COLOR_BG);

    /* R5: the success pair (~1.2k, blocking � the wire is idle at the
       DONE terminal; the FOTA seat's own safety analysis, preserved) */
    UI_privateBeepPair(UI_TONE_OK_HZ, UI_OK_ON_MS);
}

void APP_FOTA_UI_Error(const char *Copy_pchWhat)
{
    Global_u8BeepMode = 2u;              /* the failure burst */
    Global_u8BeepOn   = 0u;
    Global_u8BeepStep = 0u;
    Global_u32NextMs  = MSYSTICK_GetElapsed_ms();

    HTFT_voidFillScreen(UI_COLOR_BG);
    UI_privateCentered2x("FAILED", UI_TITLE_Y, UI_COLOR_ERR);
    if (Copy_pchWhat != 0)
    {
        /* R5-FIX 2: clamp to the screen � long reasons (18+ chars)
           centered at 1x ran 144 px wide on the 128-px screen: the
           first and last characters clipped off BOTH edges ("NO..."
           "...LE"). <= 16 chars (128 px): center as designed; longer:
           left-align and visibly truncate � the honest failure. */
        u16 Local_u16Len = 0u;
        while (Copy_pchWhat[Local_u16Len] != '\0') { Local_u16Len++; }
        if (Local_u16Len <= 16u)
        {
            UI_privateCentered1x(Copy_pchWhat, 64u, UI_COLOR_TEXT);
        }
        else
        {
            HTFT_voidDrawString(0u, 64u, Copy_pchWhat, UI_COLOR_TEXT, UI_COLOR_BG);
        }
    }
    HTFT_voidDrawString(0u, 120u, "TRY AGAIN LATER", UI_COLOR_TEXT, UI_COLOR_BG);
}

/* =====================================================================
 *  The beep engine (the FOTA seat's, verbatim � the primitive swapped)
 * ===================================================================== */
void APP_FOTA_UI_voidPoll(void)
{
    u32 Local_u32Now;

    if (Global_u8BeepMode == 0u)
    {
        return;
    }
    Local_u32Now = MSYSTICK_GetElapsed_ms();

    if (Global_u8BeepOn != 0u)
    {
        if ((Local_u32Now - Global_u32UntilMs) < 0x80000000u)
        {
            return;                       /* still inside the ON window �
                                           the timed tone is sounding; it
                                           ends itself at exactly this
                                           window's edge (hardware) */
        }
        UI_privateBeepOff();              /* the no-op (kept: verbatim) */
        Global_u8BeepOn = 0u;
        if (Global_u8BeepMode == 2u)
        {
            Global_u8BeepStep++;
            if (Global_u8BeepStep >= (2u * UI_FAIL_COUNT))
            {
                Global_u8BeepMode = 0u;   /* the burst is bounded */
                return;
            }
        }
        Global_u32NextMs = Local_u32Now +
            ((Global_u8BeepMode == 2u) ? UI_FAIL_OFF_MS : (UI_BEEP_PERIOD_MS - UI_BEEP_ON_MS));
        return;
    }

    if ((Local_u32Now - Global_u32NextMs) < 0x80000000u)
    {
        return;                           /* not yet time */
    }
    /* R5: the element START � the frequency by mode (the palette) */
    if (Global_u8BeepMode == 2u)
    {
        UI_privateBeepOn(UI_TONE_FAIL_HZ, UI_FAIL_ON_MS);
    }
    else
    {
        UI_privateBeepOn(UI_TONE_HEARTBEAT_HZ, UI_BEEP_ON_MS);
    }
    Global_u8BeepOn    = 1u;
    Global_u32UntilMs  = Local_u32Now +
        ((Global_u8BeepMode == 2u) ? UI_FAIL_ON_MS : UI_BEEP_ON_MS);
}

/* =====================================================================
 *  BENCH ONLY � the smoke sequence (the FOTA seat's, the pair swapped)
 * ===================================================================== */
#if APP_FOTA_UI_SMOKE_ENABLE
void APP_FOTA_UI_voidSmoke(u8 Copy_u8Fail)
{
    u8 Local_u8Pct;

    APP_FOTA_UI_Available("1.0.1", 40001u);
    (void)MSYSTICK_SetBusyWait_ms(1500u);

    APP_FOTA_UI_Preparing();
    (void)MSYSTICK_SetBusyWait_ms(800u);   /* a fake erase stall */

    APP_FOTA_UI_UpdatingStart();
    for (Local_u8Pct = 0u; Local_u8Pct <= 100u; Local_u8Pct += 5u)
    {
        APP_FOTA_UI_UpdatingProgress(Local_u8Pct, 100u);
        /* inline heartbeat for the blocking ramp (R5: the timed tone) */
        if ((Local_u8Pct % 20u) == 0u) { UI_privateBeepOn(UI_TONE_HEARTBEAT_HZ, UI_BEEP_ON_MS); }
        (void)MSYSTICK_SetBusyWait_ms(40u);
    }

    if (Copy_u8Fail != 0u)
    {
        APP_FOTA_UI_Error("E2E FAIL DEMO");
        /* let the burst play out, visibly */
        (void)MSYSTICK_SetBusyWait_ms(2000u);
    }
    else
    {
        APP_FOTA_UI_Success();
        (void)MSYSTICK_SetBusyWait_ms(1000u);
    }
}
#endif
