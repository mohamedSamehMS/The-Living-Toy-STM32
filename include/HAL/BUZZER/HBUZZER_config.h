/*
 * HBUZZER_config.h — shared buzzer HAL driver, CONFIGURATION
 *
 *  Project 4 — Interactive Gaming Hub (ARM Embedded Systems Diploma)
 *  Target : STM32F401CCU6 (WeAct Black Pill), register-level bare metal
 *  Layer  : HAL — the shared owner of the passive buzzer on TIM10_CH1.
 *
 *  BUZZER TYPE: PASSIVE (bench-proven — the R2 timer bench played
 *  distinct 1.0k/1.5k/2.0 kHz pitches through TIM10 PWM; an active
 *  buzzer would have produced identical clicks). Passive = the tone
 *  IS the PWM frequency; volume tracks duty.
 *
 *  OWNERS (three, arbitration inside this driver):
 *    LOW    — the NEC input blip (FOTA-33, owner-ruled)
 *    MEDIUM — FOTA status tones (progress ~750 Hz sustained,
 *             failure ~2.5 kHz burst — FOTA seat's §5.6)
 *    HIGH   — reserved: Music Maker effects (future)
 *
 *  This file: pin, timer, tone parameters. Priority POLICY lives in
 *  the interface; behavior lives in the program.
 *
 *  Created on: Oct 2, 2026
 *      Author: dell
 */
#ifndef HBUZZER_CONFIG_H_
#define HBUZZER_CONFIG_H_

#include "STD_TYPES.h"
#include <MCAL/GPIO/MGPIO_interface.h>
#include <MCAL/TIM/MTIM_interface.h>

/* ==== 1. Hardware connection (as bench-wired) =====================
 * PB8 = TIM10_CH1 (AF3, RM0368 §8.3). Two-terminal passive buzzer
 * module: PB8 -> +, GND -> -. Change pin here + re-derive the AF. */
#define HBUZZER_PORT                MGPIO_PORTB
#define HBUZZER_PIN                 MGPIO_PIN_8
#define HBUZZER_PIN_AF              MGPIO_AF3
#define HBUZZER_TIMER               MTIM_TIM10

/* ==== 2. Tone parameters ========================================== */
/* 50% duty — the passive-buzzer optimum (max fundamental energy,
 * bench-proven loud and clean on all three bench frequencies).      */
#define HBUZZER_DUTY_PERMILLE       500u

/* The input blip (FOTA-33's owner ruling): ~1 kHz, ~50 ms. The
 * three-way palette (1k blip / 750 progress / 2.5k failure) is
 * distinct by design — no sound mistakable for another.            */
#define HBUZZER_BLIP_FREQ_HZ        1000u
#define HBUZZER_BLIP_DURATION_MS    50u

/* ==== 3. Validation ================================================= */
#if (HBUZZER_DUTY_PERMILLE > 1000u)
#error "HBUZZER: HBUZZER_DUTY_PERMILLE must be 0..1000"
#endif
#if (HBUZZER_BLIP_FREQ_HZ == 0u) || (HBUZZER_BLIP_DURATION_MS == 0u)
#error "HBUZZER: blip frequency and duration must be non-zero"
#endif

#endif /* HBUZZER_CONFIG_H_ */
