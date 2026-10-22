/*
 * H74HC595_config.h — 74HC595 shift-register driver, CONFIGURATION
 *
 *  Project 4 — Interactive Gaming Hub (ARM Embedded Systems Diploma)
 *  Target  : STM32F401CCU6 (WeAct Black Pill), register-level bare metal
 *  Layer   : HAL. Sits directly on MGPIO (bit-bang backend — no MSPI
 *            dependency in this revision; moving SER/SRCLK onto real
 *            SPI pins later is a natural upgrade, see program header).
 *  Purpose : "serial-to-parallel" expansion — N output pins from 3 MCU
 *            pins. In Project 4 it drives the 8x8 LED matrix through
 *            two daisy-chained chips (HLEDMTX 595 backend).
 *
 *  CHIP REFERENCE (74HC595, 16-pin package — names used throughout):
 *      pin 14 SER     serial data in         (driven: DATA pin below)
 *      pin 11 SRCLK   shift register clock   (driven: SHIFT_CLOCK)
 *      pin 12 RCLK    storage/latch clock    (driven: LATCH_CLOCK)
 *      pin 13 OE      output enable, active LOW    (board: tied GND)
 *      pin 10 SRCLR   shift register clear, active LOW (board: tied VCC)
 *      pins 15..1, QA..QH  the 8 parallel outputs
 *      pin  9 QH'     serial data OUT — the daisy-chain tap to the
 *                     NEXT chip's SER
 *
 *  DAISY CHAIN (NUMBER_OF_74HC595_CHIPS = 2, the matrix case):
 *
 *      MCU SER (PA0) --> SER [chip 1] QH' --> SER [chip 2] QH' --> (n/c)
 *      MCU SRCLK(PA1)--> SRCLK of BOTH chips (shared clock)
 *      MCU RCLK (PA2)--> RCLK  of BOTH chips (shared latch)
 *
 *      Result: 16 parallel outputs from 3 MCU pins. Chip 1 (wired to
 *      the MCU) is the NEAR chip; chip 2 is the FAR chip.
 *
 *  REVISION HISTORY
 *  ----------------
 *    Aug 14, 2026  rev 1  original (busy-wait clocks, 1 chip)
 *    rev 2  chip count fixed to 2 (the matrix chain), SysTick
 *           dependency made conditional, init clears the chain
 *    rev 3  ported to the new MGPIO API (MGPIO_interface.h)
 *
 *  Created on: Aug 14, 2026
 *      Author: dell
 */
#ifndef H74HC595_CONFIG_H_
#define H74HC595_CONFIG_H_

/* MGPIO_interface.h provides MGPIO_PORTA / MGPIO_PIN_x symbols. */
#include "MCAL/GPIO/MGPIO_interface.h"

/* =====================================================================
 *  Chain length
 *
 *  Number of 74HC595s daisy-chained:  MCU SER -> chip 1 -> chip 2 ...
 *  Each chip adds 8 parallel outputs. SendData shifts EXACTLY this
 *  many bytes, so this MUST match the physical chain — the rev-1 bug
 *  was exactly this mismatch (chips = 1, caller passing 2 bytes: the
 *  row byte was silently dropped and the matrix could never light a
 *  row; nothing erro out anywhere, which is what made it hard to see).
 *  2 = the LED matrix (one chip = 8 rows, one chip = 8 columns).
 *  Byte-to-chip mapping is documented on H74HC595_voidSendData.      */
#define NUMBER_OF_74HC595_CHIPS      2

/* =====================================================================
 *  Control pins — the entire pin cost of this driver (3 pins total)
 * ===================================================================== */

/* Serial Data Pin (SER, pin 14) — bit values are presented here, one
 * per shift-clock pulse, MSB of each byte first. */
#define HC595_DATA_PORT              MGPIO_PORTA
#define HC595_DATA_PIN               MGPIO_PIN_0

/* Shift Clock Pin (SRCLK, pin 11) — one bit shifts into the register
 * on each RISING edge. Driven LOW->HIGH->LOW per bit by the driver. */
#define HC595_SHIFT_CLOCK_PORT       MGPIO_PORTA
#define HC595_SHIFT_CLOCK_PIN        MGPIO_PIN_1

/* Latch Clock Pin (RCLK, pin 12) — on a RISING edge the whole shift
 * register is copied to the output latches: all outputs of all chips
 * change TOGETHER (that atomic update is what lets the LED matrix
 * swap a complete scan line with no ghosting).
 *
 * !!! PIN COLLISION WARNING: PA2 = USART2_TX (AF7). If HESP ever
 * lands on USART2, this line must move BEFORE wiring — a shift
 * register latch clock and a UART transmitter cannot share a pin.
 * (USART1 on PA9/PA10 does not collide.) */
#define HC595_LATCH_CLOCK_PORT       MGPIO_PORTA
#define HC595_LATCH_CLOCK_PIN        MGPIO_PIN_2

/* =====================================================================
 *  Clock timing knob
 *
 *  Minimum clock half-period, microseconds. The 74HC595 needs only
 *  ~tens of ns of setup/pulse width at 3.3 V; ONE MGPIO call is a
 *  few hundred ns at 84 MHz, so back-to-back calls already exceed
 *  the datasheet requirement by ~10x even with NO delay. Hence:
 *
 *      0 (DEFAULT) = no busy-wait at all.
 *          - SendData is pure GPIO: ~10-15 us for a 2-chip update,
 *            ISR-safe, and has NO SysTick dependency whatsoever.
 *          - The resulting bit-bang clock runs in the low-MHz range —
 *            still far inside spec.
 *      1-2         = re-add the rev-1-style busy-wait between edges.
 *          Use ONLY if the chain misbehaves (long wires, breadboard
 *          ringing on the clock line — a scope is the way to decide).
 *          Any value > 0 pulls in MSYSTICK (conditional include in
 *          the program file), which means: SysTick must be initialized
 *          BEFORE SendData is first called, and SendData must NOT be
 *          called from inside the SysTick ISR itself (a busy-wait
 *          inside SysTick's own handler reprograms the counter it is
 *          waiting on — the periodic tick breaks).                     */
#define HC595_MIN_HALF_CLOCK_US      1

#endif /* H74HC595_CONFIG_H_ */
