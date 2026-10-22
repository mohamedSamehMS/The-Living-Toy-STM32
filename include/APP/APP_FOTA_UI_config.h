/*
 * APP_FOTA_UI_config.h — configuration of the FOTA update UI
 * (FOTA seat, v1.0 — briefing §5.6, as ruled by the owner:
 * TFT progress bar + buzzer tones; NO LED matrix, NO seven-
 * segment).
 *
 *  SECTION 1 — the buzzer hardware policy (the owner's ruling:
 *              a low tone during progress, a louder/different
 *              tone on failure).
 *  SECTION 2 — bench-only machinery.
 *
 *  THE BUZZER PART IS NOT YET CHOSEN/ WIRED: set the port/pin
 *  to the real wiring before the first bench. Until a buzzer
 *  exists, ENABLE 0 runs the whole UI silently (bar + text
 *  only).
 *
 *  Revision history:
 *    v1.0 — first delivery (the §5.6 module, descoped to
 *           TFT + buzzer per the owner's ruling)
 */

#ifndef APP_FOTA_UI_CONFIG_H_
#define APP_FOTA_UI_CONFIG_H_

#include <MCAL/GPIO/MGPIO_interface.h>   /* the buzzer pin    */

/* =====================================================================
 *  SECTION 1 — BUZZER POLICY (R5: RETIRED to HBUZZER)
 *
 *  The R5 wiring moved the transducer to the shared HBUZZER driver
 *  (TIM10/PB8, priority arbitration, the countersigned §5.6 palette:
 *  750 Hz heartbeat / 2500 Hz failure burst / ~1200 Hz success pair).
 *  The knobs below are GONE — HBUZZER owns the pin, the tones, and
 *  the arbitration (FOTA-33 formally superseded). This section remains
 *  as the tombstone: no buzzer configuration lives here anymore.
 *
 *  tombstones:
 *    APP_FOTA_UI_BUZZER_ENABLE      -> retired (HBUZZER always present)
 *    APP_FOTA_UI_BUZZER_ACTIVE      -> retired (passive, owner-ruled)
 *    APP_FOTA_UI_BUZZER_PORT/PIN    -> retired (PB8, HBUZZER's own)
 *    APP_FOTA_UI_BUZZER_ACTIVE_HIGH -> retired (no GPIO here at all)
 * ===================================================================== */
/* Master switch. 0 = no buzzer (silent UI — bar and text only). */
#define APP_FOTA_UI_BUZZER_ENABLE      1u

/* 1 = ACTIVE buzzer module (fixed pitch; GPIO on/off — tones are
 *     delivered as PATTERNS: a slow heartbeat during progress,
 *     a rapid burst on failure — the owner's "different tone").
 * 0 = PASSIVE buzzer (needs a frequency — the tone calls are
 *     STUBS pending the tone driver: the MCAL seat's allocation
 *     reserves TIM10/11 as "conditional buzzer". Flip to 0 +
 *     wire the tone functions when the part and driver exist.) */
#define APP_FOTA_UI_BUZZER_ACTIVE      0u

/* The pin. DEFAULT PA8 — FREE on this board (PA0 IR, PA2/3 console,
 * PA9/10 ESP, PA13/14 SWD, PB0/1/10 TFT, PC13 LED). SET TO YOUR
 * WIRING. */
#define APP_FOTA_UI_BUZZER_PORT        MGPIO_PORTA
#define APP_FOTA_UI_BUZZER_PIN         MGPIO_PIN_8

/* The module's drive polarity: 1 = high = beep. */
#define APP_FOTA_UI_BUZZER_ACTIVE_HIGH 1u

/* =====================================================================
 *  SECTION 2 — BENCH-ONLY MACHINERY
 * ===================================================================== */

/* The self-test: the full screen sequence with fake progress
 * (Available -> Preparing -> Updating ramp -> Success or Error).
 * 1 in bench builds; production sets 0 (dead code eliminated). */
#define APP_FOTA_UI_SMOKE_ENABLE       1u

/* =====================================================================
 *  Validation
 * ===================================================================== */
#if (APP_FOTA_UI_BUZZER_ENABLE != 0) && (APP_FOTA_UI_BUZZER_ENABLE != 1)
#error "APP_FOTA_UI: APP_FOTA_UI_BUZZER_ENABLE must be 0 or 1"
#endif
#if (APP_FOTA_UI_BUZZER_ACTIVE != 0) && (APP_FOTA_UI_BUZZER_ACTIVE != 1)
#error "APP_FOTA_UI: APP_FOTA_UI_BUZZER_ACTIVE must be 0 or 1"
#endif
#if (APP_FOTA_UI_BUZZER_ACTIVE_HIGH != 0) && (APP_FOTA_UI_BUZZER_ACTIVE_HIGH != 1)
#error "APP_FOTA_UI: APP_FOTA_UI_BUZZER_ACTIVE_HIGH must be 0 or 1"
#endif
#if (APP_FOTA_UI_SMOKE_ENABLE != 0) && (APP_FOTA_UI_SMOKE_ENABLE != 1)
#error "APP_FOTA_UI: APP_FOTA_UI_SMOKE_ENABLE must be 0 or 1"
#endif

#endif /* APP_FOTA_UI_CONFIG_H_ */
