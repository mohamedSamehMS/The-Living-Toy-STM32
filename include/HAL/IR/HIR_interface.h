/*
 * HIR_interface.h — IR receiver driver (NEC link layer), PUBLIC INTERFACE
 *
 *  THE TWO-DRIVER STACK:
 *
 *      Application / Control
 *            |   HNEC_REMOTE_enumGetEvent / Enable / Disable ...
 *      HNEC_REMOTE      (device layer — the app-facing API)
 *            |   HIR_enumDecodeFrame / HIR_enumCloseFrame / ...
 *          HIR          (this driver — sensor + NEC decode)
 *            |   MGPIO / MEXTI / MSYSTICK
 *
 *  HNEC_REMOTE is this driver's ONE designed consumer. An application
 *  includes this header only for echo/debug builds (raw frames) or if
 *  it drives the link layer directly without a device abstraction.
 *
 *  ===================================================================
 *  THE INTER-DRIVER SERVICE PAIR — read before consuming
 *  ===================================================================
 *  Two-phase BY NECESSITY: the quiet-window DURATION after a frame is
 *  a DEVICE-layer decision (it depends on the repeat policy, which
 *  depends on the DECODED frame) — but the decode must run with the
 *  EXTI line disabled, and the line must stay disabled until the
 *  window ends. Hence a matched pair:
 *
 *    HIR_enumDecodeFrame(&frame):
 *      - no activity  -> HIR_NO_NEW_EVENT. Line untouched, nothing
 *                        owed. (Cheap: one flag read — call freely.)
 *      - activity     -> disables the line, clears the flag, runs the
 *                        ~68 ms BLOCKING decode, fills frame
 *                        (Type / Addr / Cmd), returns HIR_OK — and
 *                        the caller then OWES exactly ONE
 *                        HIR_enumCloseFrame() call, for ANY Type
 *                        (INVALID included: noise gets a window too).
 *      - called again while a close is owed -> HIR_DECODE_OPEN
 *                        (contract violation — loudly refused, line
 *                        state untouched).
 *
 *    HIR_enumCloseFrame(quiet_ms):
 *      - busy-waits quiet_ms (the caller's swallow window), then
 *        clears the line's pending and re-arms the line. The wipe
 *        before re-arm IS the deliberate event loss — it is what
 *        swallows hold-repeat bursts and noise between frames.
 *      - called with nothing owed -> HIR_NOK, registers untouched.
 *      - re-arms ONLY if the driver is enabled (a Disable issued
 *        mid-pair stays authoritative — cannot happen in the
 *        single-consumer super-loop; the guard is belt-and-braces).
 *
 *  In HNEC_REMOTE's service the pairing is structurally guaranteed:
 *  one straight-line path, one CloseFrame call site. That is the
 *  pattern to keep if anyone else ever consumes this driver.
 *  ===================================================================
 *
 *  BOOT-ORDER CONTRACT (prerequisites of HIR_enumInit; HNEC_REMOTE's
 *  init forwards them — listed here because the registers are here):
 *      1. MRCC_Init()
 *      2. MRCC_EnablePeripheralClock(MRCC_GPIOA)      // IR pin port
 *      3. MRCC_EnablePeripheralClock(MRCC_SYSCFG)     // EXTI routing
 *      4. MNVIC_Init();  MNVIC_SetPriority(MNVIC_EXTI0, 1, 0)
 *      5. MEXTI_Init()   // table MUST contain the LINE0/PORTA row
 *      6. MSYSTICK_Init()  // the busy-wait engine depends on it
 *
 *  Created on: Sep 12, 2026
 *      Author: dell
 */
#ifndef HIR_INTERFACE_H_
#define HIR_INTERFACE_H_

#include "STD_TYPES.h"

/* Driver status — the library convention */
typedef enum
{
    HIR_OK = 0,            /* Frame decoded (see Type) / operation done  */
    HIR_NOK,               /* Not initialized, MCAL failure, or CloseFrame
                              called with no decode owed                  */
    HIR_NULL_POINTER,      /* NULL output pointer                        */
    HIR_NO_NEW_EVENT,      /* No edge since the last service — nothing
                              owed, line untouched                        */
    HIR_DISABLED,          /* The receiver is currently disabled         */
    HIR_DECODE_OPEN        /* DecodeFrame called twice without a
                              CloseFrame in between — contract violation */
} HIR_EnumStatus_t;

/* Frame classification (NEC protocol level — no button semantics) */
typedef enum
{
    HIR_FRAME_DATA = 0,    /* full frame, checksum passed (Addr/Cmd valid) */
    HIR_FRAME_REPEAT,      /* repeat frame — carries NO data (repeat the
                              last accepted button; decision is the
                              consumer's — this driver only reports)      */
    HIR_FRAME_INVALID      /* noise / corruption / stale service           */
} HIR_FrameType_t;

/* One decoded NEC frame, pre-everything (no address filter, no
 * mapping — those are HNEC_REMOTE's). Addr/Cmd are meaningful only
 * for HIR_FRAME_DATA; zeroed otherwise. */
typedef struct
{
    HIR_FrameType_t Type;
    u8              Addr;
    u8              Cmd;
} HIR_Frame_t;

/* =====================================================================
 *  API
 * ===================================================================== */

/* Configure the IR pin (input + pull), enforce the falling trigger
 * edge, register the EXTI callback, arm the line (MEXTI_Enable's
 * internal stale-wipe makes the arm clean). State starts ENABLED.
 * Re-calling is harmless (idempotent register writes, state reset).
 * Normally invoked BY HNEC_REMOTE_enumInit — direct app use only for
 * echo builds or standalone link-layer testing. */
HIR_EnumStatus_t HIR_enumInit(void);

/* ---- Line gating (consumed by HNEC_REMOTE's enable/disable) ---------
 * Wrapped at the EXTI level on purpose: a press while disabled costs
 * literally nothing. Disable also drops the activity flag — a
 * disabled receiver discards everything; re-enable starts clean. */
HIR_EnumStatus_t HIR_enumEnable(void);
HIR_EnumStatus_t HIR_enumDisable(void);

/* ---- THE service pair (see the banner above) ------------------------ */
HIR_EnumStatus_t HIR_enumDecodeFrame(HIR_Frame_t *Copy_pstFrame);
HIR_EnumStatus_t HIR_enumCloseFrame(u32 Copy_u32QuietMs);

/* ---- Debug/echo: last raw frame --------------------------------------
 * Returns (addr, cmd) of the last checksum-VALID data frame — before
 * ANY filtering (there is none at this layer — filtering lives above).
 * Freshness semantics: HIR_NO_NEW_EVENT until a new frame decoded
 * since the last call. THE mechanism for the button-code echo pass
 * (USB-TTL build: poll the device layer's GetEvent to drive the
 * service, then read frames here — see HNEC_REMOTE_interface.h). */
HIR_EnumStatus_t HIR_enumGetLastRawFrame(u8 *Copy_pu8Addr, u8 *Copy_pu8Cmd);

#endif /* HIR_INTERFACE_H_ */
