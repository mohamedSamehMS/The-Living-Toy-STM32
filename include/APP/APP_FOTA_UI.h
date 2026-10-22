/*
 * APP_FOTA_UI.h — the FOTA update experience (FOTA seat, v1.0).
 *
 *  THE §5.6 SPEC, AS RULED BY THE OWNER: a TFT progress bar with
 *  status text, plus buzzer feedback — a LOW TONE (slow heartbeat
 *  pattern) during progress, a LOUDER/DIFFERENT tone (rapid burst)
 *  on failure. No LED matrix, no seven-segment.
 *
 *  WHO DRIVES IT: the orchestrator (APP_FOTA_NET) calls every
 *  transition at its state changes; APP_FOTA_UI_voidPoll() (the
 *  beep engine) is called from the orchestrator's task. The module
 *  never knows the transport — the SAME screens serve the PC bench
 *  and the wireless path, because they consume only the
 *  orchestrator's calls.
 *
 *  BOOT CONTRACT: the TFT must be initialized BEFORE any screen
 *  (HTFT_voidInit() in main — the SPI pins per the TFT config).
 *  This module never touches SPI setup; it only draws.
 *
 *  THE DRAW-BEFORE LAW (the stall contract): PREPARING is drawn
 *  BEFORE the staging erase (~2.3 s core freeze); the success
 *  screen carries "DO NOT POWER OFF" BEFORE the self-reset — and
 *  the panel HOLDS that screen through the bootloader's commit
 *  (~2-4 s), because the ST7735's own RAM is untouched by an MCU
 *  reset and the BL owns no TFT. The screen persists by physics;
 *  the new app's first draw takes over.
 *
 *  FONT LAW: HTFT's font covers 0x20-0x5F — ALL STRINGS ARE
 *  UPPERCASE (digits, '!', '.', '%', spaces are fine).
 *
 *  Revision history:
 *    v1.0 — first delivery (descoped §5.6: TFT + buzzer).
 */

#ifndef APP_FOTA_UI_H_
#define APP_FOTA_UI_H_

#include "STD_TYPES.h"

/* Buzzer GPIO setup (call once at boot, after MGPIO is usable).
 * Silent — no chirp; the bench confirms the buzzer, not the boot. */
void APP_FOTA_UI_voidInit(void);

/* ---- the transitions (orchestrator-driven) ---- */

/* "UPDATE READY / <version string> / <kb> KB" + an attention
   double-chirp. The version is the STRING, verbatim (Ruling 2(a)). */
void APP_FOTA_UI_Available(const char *Copy_pchVersion, u32 Copy_u32LengthBytes);

/* "PREPARING..." — MUST be called BEFORE APP_FOTA_enumPrepare()
 * (the draw-before law: the screen explains the coming freeze). */
void APP_FOTA_UI_Preparing(void);

/* Full layout: title, bar frame, 0%, "DO NOT POWER OFF".
 * Starts the progress heartbeat (the low-tone feedback). */
void APP_FOTA_UI_UpdatingStart(void);

/* Percent change -> incremental bar fill + redrawn percent text.
 * Cheap: redraws only on integer-percent changes; the inter-frame
 * stop-and-wait gap (plus HESP's 1 KB ring) is the timing budget. */
void APP_FOTA_UI_UpdatingProgress(u32 Copy_u32FramesDone, u32 Copy_u32FramesTotal);

/* "UPDATE COMPLETE / RESTARTING / DO NOT POWER OFF" + the success
 * beeps (~240 ms, blocking — the wire is idle at the DONE terminal).
 * The screen persists through the BL commit (the panel holds it). */
void APP_FOTA_UI_Success(void);

/* "UPDATE FAILED / <WHAT> / TRY AGAIN LATER" + the failure burst
 * (poll-driven, bounded — the app returns to normal operation). */
void APP_FOTA_UI_Error(const char *Copy_pchWhat);

/* The beep engine — call every super-loop pass (from the
 * orchestrator's task). Timing is cosmetic (FOTA-06 note: a
 * transient wrong clock read here costs a skipped or extra beep,
 * nothing else — the double-read mitigation is for abort-firing
 * paths, not beeps). */
void APP_FOTA_UI_voidPoll(void);

#if APP_FOTA_UI_SMOKE_ENABLE
/* BENCH ONLY: the full sequence with fake progress.
 * Copy_u8Fail: 0 = success ending, 1 = failure ending. Requires
 * the TFT initialized. Blocking (~10 s). */
void APP_FOTA_UI_voidSmoke(u8 Copy_u8Fail);
#endif

#endif /* APP_FOTA_UI_H_ */
