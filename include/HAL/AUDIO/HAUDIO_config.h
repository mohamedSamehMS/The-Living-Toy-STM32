/*
 * HAUDIO_config.h - 8-bit R-2R audio output, CONFIGURATION
 *
 *  Project 4 - Interactive Gaming Hub (ARM Embedded Systems Diploma)
 *  Target : STM32F401CCU6, register-level bare metal
 *  Layer  : HAL -> MCAL (MGPIO, MTIM)
 *
 *  The confirmed wiring deliberately keeps all three wires on GPIOA
 *  and does not claim a timer alternate-function output. TIM9 is
 *  pinless.
 *
 *  Created on: Sep 14, 2026
 *      Author: dell
 */
#ifndef HAUDIO_CONFIG_H_
#define HAUDIO_CONFIG_H_

#include "STD_TYPES.h"
#include <MCAL/GPIO/MGPIO_interface.h>
#include <MCAL/TIM/MTIM_interface.h>
#include <MCAL/RCC/MRCC_interface.h>

/* 74HC595 control wiring. OE is tied low and SRCLR is tied high. */
#define HAUDIO_SER_PORT             MGPIO_PORTA
#define HAUDIO_SER_PIN              MGPIO_PIN_8  /* SER -> 74HC595 pin 14 */
#define HAUDIO_SRCLK_PORT           MGPIO_PORTA
#define HAUDIO_SRCLK_PIN            MGPIO_PIN_11 /* SRCLK -> 74HC595 pin 11 */
#define HAUDIO_RCLK_PORT            MGPIO_PORTA
#define HAUDIO_RCLK_PIN             MGPIO_PIN_12 /* RCLK -> 74HC595 pin 12 */

/*
 * OUTPUT MAPPING CONTRACT: the driver shifts MSB-first into the
 * 595's shift register, so after the latch:
 *     QH (pin 7)  = sample bit 7 (the MSB)
 *     QA (pin 15) = sample bit 0 (the LSB)
 * WIRING RULE: QH connects to the ladder's MSB input (the ladder
 * input with the SHORTEST path to the sum node), QA to the LSB
 * input (LONGEST path). If the ladder was built with the opposite
 * pin order, the wiring swaps - the DRIVER never changes; bit order
 * in the register is fixed by silicon.
 */

#define HAUDIO_TIMER                MTIM_TIM9
#define HAUDIO_SAMPLE_RATE_HZ       16000u
#define HAUDIO_SINE_TABLE_LENGTH    256u
#define HAUDIO_NOTE_RAMP_SAMPLES    8u
#define HAUDIO_STOP_RAMP_SAMPLES    160u       /* approximately 10 ms */

/*
 * TIM9 is on APB2. With the current published model:
 *   SYSTEM_CLOCK=HSI=16000 kHz, APB2 /1, timer clock=16000000 Hz.
 *   16000000 / (PSC+1) / (ARR+1) = 16000
 *   PSC=0, ARR=999 -> 16000000 / 1 / 1000 = 16000 Hz exactly.
 * MTIM_PeriodicConfig performs this same clock-derived solve at runtime.
 * AUDIT RESULT: HAUDIO passes 16000 Hz (not kHz), and MTIM_TIM9's
 * instance table selects MTIM_APB2_TIMER_CLK_HZ. No APB1/unit mismatch.
 */
#define HAUDIO_TIMER_CLOCK_HZ       (MRCC_APB2_TIMER_CLK_KHZ * 1000UL)

#if (HAUDIO_SAMPLE_RATE_HZ == 0u)
#error "HAUDIO: sample rate must be non-zero"
#endif
#if (HAUDIO_NOTE_RAMP_SAMPLES == 0u)
#error "HAUDIO: note ramp must be non-zero"
#endif

#endif /* HAUDIO_CONFIG_H_ */
