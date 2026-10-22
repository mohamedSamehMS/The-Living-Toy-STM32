/*
 * HNEC_REMOTE_interface.h — NEC remote controller, PUBLIC INTERFACE
 *
 *  The ONLY file the application includes for the IR remote (echo
 *  builds additionally include HIR_interface.h for raw frames).
 *  All tunables live in HNEC_REMOTE_config.h; the link layer it
 *  consumes is HIR.
 *
 *  Reference: design guide "HIR Driver — Design Guide (HAL NEC IR
 *             Remote)"; feature notes "NEC IR Remote — Design Notes".
 *
 *  ===================================================================
 *  THE TIMING COST — NOT HIDDEN, BY DESIGN (read before using):
 *
 *  HNEC_REMOTE_enumGetEvent() BLOCKS the calling loop while it works
 *  (it drives HIR's decode + the quiet windows):
 *    - decoding one frame ............ ~68 ms  (busy-wait measurement)
 *    - quiet window after it ......... ~200 ms (swallow / initial delay)
 *    - total per press ............... ~270 ms of thread-context stall
 *    - accepted repeat events ........ ~11 ms each, ~every 110 ms
 *    - repeats of a non-repeatable
 *      button while held ............. ~11 ms per ~211 ms cycle
 *
 *  During every one of those stalls: the SysTick 1 ms tick keeps
 *  running (LED matrix POV stays alive, ms timebase stays correct),
 *  and higher-priority ISRs (buttons, DMA, USART) keep firing — their
 *  flags latch and are serviced after the stall. ONLY thread-context
 *  work (game logic, TFT redraw) waits. This is the documented
 *  Phase-1 super-loop trade-off; the Phase-2 input-capture migration
 *  (internal to HIR) removes it.
 *
 *  Consequence for the app: call HNEC_REMOTE_enumGetEvent() EVERY
 *  loop iteration. Idle cost is one flag read (~sub-us). A loop that
 *  runs longer than ~9 ms between calls can miss the start of a
 *  burst and drop that press (HIR's decode sanity-checks the pin
 *  level first and bails on a stale frame).
 *  ===================================================================
 *
 *  BOOT-ORDER CONTRACT (HIR's prerequisites, forwarded here):
 *      1. MRCC_Init()
 *      2. MRCC_EnablePeripheralClock(MRCC_GPIOA)
 *      3. MRCC_EnablePeripheralClock(MRCC_SYSCFG)
 *      4. MNVIC_Init();  MNVIC_SetPriority(MNVIC_EXTI0, 1, 0)
 *      5. MEXTI_Init()   // table MUST contain the LINE0/PORTA row —
 *                        // see the cross-driver note in HIR_config.h
 *      6. MSYSTICK_Init()
 *      7. HNEC_REMOTE_enumInit()   // initializes HIR internally
 *
 *  Boot window note: MEXTI_Init arms line 0 at step 5 while the
 *  callback is registered at step 7 — an IR press in that window
 *  pends, fires, dispatches nothing, and is dropped. Harmless;
 *  documented so it is never a mystery.
 *
 *  Created on: Sep 13, 2026
 *      Author: dell
 */
#ifndef HNEC_REMOTE_INTERFACE_H_
#define HNEC_REMOTE_INTERFACE_H_

#include "STD_TYPES.h"
#include "HAL/HVirtualButtons_interface.h"     /* VirtualButton_t */

/* Driver status — the library convention */
typedef enum
{
    HNEC_REMOTE_OK = 0,       /* Event delivered / operation done       */
    HNEC_REMOTE_NOK,          /* Not initialized, or HIR/MCAL failure
                                 during Init (check the boot order)     */
    HNEC_REMOTE_NULL_POINTER, /* NULL output pointer                    */
    HNEC_REMOTE_NO_NEW_EVENT, /* Nothing decoded, nothing pending       */
    HNEC_REMOTE_DISABLED      /* The remote source is disabled          */
} HNEC_REMOTE_EnumStatus_t;

/* One decoded event, ready for Control/game logic.
 *  Button   : the virtual role (never BTN_NONE on a delivered event)
 *  IsRepeat : 0 = fresh press (NEC data frame)
 *             1 = held-button auto-repeat (NEC repeat frame, emitted
 *                 only for repeat-allowed roles — config §4)          */
typedef struct
{
    VirtualButton_t Button;
    u8              IsRepeat;
} HNEC_REMOTE_Event_t;

/* =====================================================================
 *  API
 * ===================================================================== */

/* Initialize the remote: runs HIR_enumInit (pin, trigger, callback,
 * arm — HIR's prerequisites must have run; see the boot banner), then
 * resets device-layer state (no pending event, no last-accepted
 * button, no callback, ENABLED). Re-calling is harmless. */
HNEC_REMOTE_EnumStatus_t HNEC_REMOTE_enumInit(void);

/* ---- Runtime enable/disable (parental control / FSM safety gate) ---
 * Gated at the EXTI level on purpose (via HIR): a press while disabled
 * costs literally nothing. Disable also drops any not-yet-read
 * pending event; re-enabling starts clean. Idempotent.
 * Persistence of the parental-control setting is an application/
 * Flash-layer concern, not this driver's. */
HNEC_REMOTE_EnumStatus_t HNEC_REMOTE_enumEnable(void);
HNEC_REMOTE_EnumStatus_t HNEC_REMOTE_enumDisable(void);
HNEC_REMOTE_EnumStatus_t HNEC_REMOTE_enumGetEnabledState(u8 *Copy_pu8State);

/* ---- THE consumption call (polling model) ---------------------------
 * Call every super-loop iteration. Runs the full service when a frame
 * has arrived (see the timing-cost banner — it BLOCKS ~270 ms), then
 * delivers one pending event. Returns:
 *   HNEC_REMOTE_OK          — event copied out (pending slot emptied)
 *   HNEC_REMOTE_NO_NEW_EVENT— nothing happened / frame rejected
 *                             (noise, checksum fail, foreign address,
 *                             unmapped code, discarded repeat — all
 *                             silent by design)
 *   HNEC_REMOTE_DISABLED    — source currently disabled
 *   HNEC_REMOTE_NULL_POINTER / _NOK — argument / init errors
 * Single pending slot: poll faster than ~200 ms or events overwrite
 * (at the protocol's own rate that takes deliberate effort). */
HNEC_REMOTE_EnumStatus_t HNEC_REMOTE_enumGetEvent(HNEC_REMOTE_Event_t *Copy_pstEvent);

/* ---- Optional callback channel ---------------------------------------
 * If registered, invoked from INSIDE the service — THREAD context,
 * NEVER the ISR (the decode is ~68 ms). Timing: after decode, BEFORE
 * the quiet window — a slow callback simply extends the window.
 * The decoded event is stored to the pending slot EITHER WAY; the
 * callback and GetEvent are two views of the same event. Pick ONE
 * consumption pattern in the application (the callback is primarily
 * for bring-up/echo builds). NULL unregisters (library convention).
 * DO NOT call any HIR/HNEC_REMOTE function from inside the callback. */
HNEC_REMOTE_EnumStatus_t HNEC_REMOTE_enumSetEventCallback(
        void (*Copy_pfCallback)(HNEC_REMOTE_Event_t));

/* =====================================================================
 *  Usage example (boot order abbreviated — see the banner above):
 *
 *      HNEC_REMOTE_enumInit();
 *      while (1)
 *      {
 *          HNEC_REMOTE_Event_t Local_stEvent;
 *          if (HNEC_REMOTE_enumGetEvent(&Local_stEvent) == HNEC_REMOTE_OK)
 *          {
 *              switch (Local_stEvent.Button)
 *              {
 *                  case BTN_UP:    /* Snake: queue direction change * /
 *                  case BTN_START: /* pause / resume                 * /
 *                  case BTN_NUM_5: /* Math Quiz: digit 5             * /
 *                  default:        break;
 *              }
 *          }
 *          /* ... rest of the super-loop ... * /
 *      }
 *
 *  Echo/debug builds additionally use HIR_enumGetLastRawFrame() —
 *  see HIR_interface.h (the layer that owns raw frames).
 * ===================================================================== */

#endif /* HNEC_REMOTE_INTERFACE_H_ */
