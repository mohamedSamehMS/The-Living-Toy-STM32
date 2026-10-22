/*
 * HIR_config.h — IR receiver driver (NEC link layer), CONFIGURATION
 *
 *  Project 4 — Interactive Gaming Hub (ARM Embedded Systems Diploma)
 *  Target  : STM32F401CCU6 (WeAct Black Pill), register-level bare metal
 *  Layer   : HAL — the LOWER half of the two-driver IR stack (see the
 *            file-header diagram in HIR_interface.h).
 *
 *  This driver knows NOTHING about remotes, buttons, or virtual
 *  controllers: it decodes NEC frames and hands raw (addr, cmd) up.
 *  Address filtering, code->button mapping, and repeat policy live
 *  in HNEC_REMOTE_config.h — changing remotes never touches this file.
 *
 *  Reference: NEC infrared transmission protocol (standard timings);
 *             RM0368 ch. 8 (GPIO), ch. 10 (EXTI).
 *
 *  !!! CROSS-DRIVER DEPENDENCY (the one hard coupling of this driver):
 *  EXTI line ROUTING is not settable through any MEXTI API — it comes
 *  from MEXTI_config.h's static table. This driver requires the row:
 *      { MEXTI_LINE0, MGPIO_PORTA, MEXTI_TRIGGER_FALLING, ... }
 *  HIR_enumInit enforces what it can (the trigger edge, via the
 *  public MEXTI_SetTrigger); routing is trusted from the table. If
 *  that row is ever reworked, this driver breaks SILENTLY — no
 *  compile error, just a dead receiver. This comment is the
 *  contract's home — keep it true.
 *
 *  Revision history:
 *    Rev 1 — combined driver (sensor + protocol + mapping in one)
 *    Rev 2 — this split: sensor/link layer only; mapping, address
 *            filter and repeat policy moved up to HNEC_REMOTE
 *
 *  Created on: Sep 12, 2026
 *      Author: dell
 */
#ifndef HIR_CONFIG_H_
#define HIR_CONFIG_H_

#include "STD_TYPES.h"

#include <MCAL/GPIO/MGPIO_interface.h>     /* MGPIO_PORTA / MGPIO_PIN_0  */
#include <MCAL/EXTI/MEXTI_interface.h>     /* MEXTI_LINE0                */

/* =====================================================================
 *  1. Hardware connection  (as tested on the two-LED build)
 *
 *  PA0 doubles as TIM2_CH1 / TIM5_CH1 — deliberately preserved so the
 *  Phase-2 input-capture migration (an HIR-INTERNAL change with this
 *  split — HNEC_REMOTE never knows) is a recompile, never a rewire.
 *  If the pin ever moves, move it to another capture-capable pin
 *  (PA1/PA2/PA3 are all TIM2/TIM5 channels).
 * ===================================================================== */
/* ---- Hardware connection (TIM5 capture — the Phase-2 engine, landed) ----
 * PA0 in AF mode, AF2 = TIM5_CH1. NOTE: AF1 would be TIM2_CH1 — the AF
 * is pin-specific; if the pin ever moves, re-derive HIR_PIN_AF from the
 * RM0368 §8.3 table (PA1/PA2/PA3 are also TIM2/TIM5 channels).        */
#define HIR_PORT       MGPIO_PORTA
#define HIR_PIN        MGPIO_PIN_0
#define HIR_PIN_AF     MGPIO_AF2
#define HIR_TIM        MTIM_TIM5
#define HIR_PIN_PULL   MGPIO_PULL_UP   /* PUPDR applies in AF mode too:
                                          an unwired line stays idle-high */

/* !!! CROSS-DRIVER DEPENDENCY (the one hard coupling — successor of the
 * old MEXTI-table row): MTIM_config.h MUST carry the row
 *      { MTIM_TIM5, MTIM_MODE_FREE_RUN, 1000000u }
 * HIR_enumInit fails loudly if MTIM is uninitialized or TIM5 is not in
 * FREE_RUN — but the 1 MHz rate itself is trusted from the table. Every
 * threshold above is in microseconds BECAUSE of that row. If the row
 * changes, this driver breaks loudly (first misdecode), never silently. */

#include <MCAL/TIM/MTIM_interface.h>   /* replaces the MEXTI include */

#endif /* HIR_CONFIG_H_ */
