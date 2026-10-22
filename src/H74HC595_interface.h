/*
 * H74HC595_interface.h — 74HC595 shift-register driver, PUBLIC INTERFACE
 *
 *  The ONLY H74HC595 file application code may include. All tunables
 *  (pins, chain length, timing) live in H74HC595_config.h.
 *
 *  Created on: Aug 14, 2026
 *      Author: dell
 */
#ifndef H74HC595_INTERFACE_H_
#define H74HC595_INTERFACE_H_

#include "STD_TYPES.h"

/* =====================================================================
 *  BOARD ASSUMPTIONS — not driven by this driver (hardwired on the
 *  board), documented here so nobody has to rediscover them:
 *      OE    (pin 13) -> GND : outputs always enabled (active-LOW pin)
 *      SRCLR (pin 10) -> VCC : shift register never hardware-reset
 *  If you ever want MCU control of brightness (OE PWM) or a hardware
 *  blank (SRCLR), that is new wiring + a driver extension, not a
 *  config change.
 * ===================================================================== */

/* Configure the 3 control pins (mode, output stage, idle LOW) and —
 * this is the part people forget — CLEAR the chain: shift-register
 * contents are INDETERMINATE at power-up, so until the first
 * transfer the outputs hold random bits. Init shifts out all-zero
 * bytes and latches once, leaving every output LOW from init on.
 *
 * PRECONDITION — port clock must be enabled first or every MGPIO
 * write is silently ignored (see MGPIO_interface.h; MGPIO cannot
 * detect this case):
 *      MRCC_EnablePeripheralClock(MRCC_GPIOA);   // HC595_* pins' port
 *
 * After Init the outputs stay at their latched state indefinitely
 * (the 595 is a register, not a refresh device) until the next
 * SendData. */
void H74HC595_voidInit(void);

/* Shift NUMBER_OF_74HC595_CHIPS bytes through the chain, then pulse
 * the latch ONCE — all outputs of all chips update atomically in that
 * single edge (no intermediate states ever visible on the pins).
 *
 * DAISY-CHAIN MAPPING (easy to get backwards — the #1 wiring bug):
 *      Copy_pu8DataArray[0]   shifted FIRST  -> travels the full depth
 *                                               of the chain -> ends in
 *                                               the chip FARTHEST from
 *                                               the MCU
 *      Copy_pu8DataArray[N-1] shifted LAST   -> stays in the chip wired
 *                                               DIRECTLY to SER (the
 *                                               near chip)
 * Why: every later byte pushes the earlier bytes one chip deeper; the
 * last byte shifted has nowhere left to go, so it sits in chip 1.
 *
 * BIT ORDER: MSB-first within each byte (bit 7 shifted first, ends at
 * Q7/QH after 8 clocks). This matches SPI mode-0 order, so the same
 * payload works unchanged if the driver is later moved onto MSPI.
 *
 * The array must hold at least NUMBER_OF_74HC595_CHIPS bytes.
 * NULL is ignored (defensive no-op).
 *
 * CONCURRENCY: the shift sequence itself is NOT atomic — an ISR that
 * also calls SendData between two of the 16 clocks will interleave
 * bits and the final latch commits garbage. Safe patterns: call only
 * from main level, OR only from one ISR, OR wrap one side in a
 * disable/enable-IRQ critical section. (The HLEDMTX POV pattern calls
 * this from exactly ONE context — the periodic tick — so it is fine.)
 *
 * BLOCKING: ~10-15 us for 2 chips with the default no-delay config
 * (HC595_MIN_HALF_CLOCK_US = 0). */
void H74HC595_voidSendData(const u8* Copy_pu8DataArray);

#endif /* H74HC595_INTERFACE_H_ */
