/*
 * HBUZZER_private.h — shared buzzer HAL driver, PRIVATE header
 *
 *  Never include from HAL-consumer or application code. Internal
 *  types and prototypes only — all state is file-static in
 *  HBUZZER_program.c (the MSYSTICK Rev-2 lesson: no externs).
 *
 *  Created on: Oct 2, 2026
 *      Author: dell
 */
#ifndef HBUZZER_PRIVATE_H_
#define HBUZZER_PRIVATE_H_

#include "STD_TYPES.h"
#include "HBUZZER_interface.h"

/* Output mode — what the driver is currently doing to TIM10 */
typedef enum
{
    HBUZZER_MODE_SILENT = 0,    /* nothing sounding; TIM10 untouched   */
    HBUZZER_MODE_SUSTAINED,     /* PWM running, NO interrupt armed     */
    HBUZZER_MODE_TIMED          /* PWM running + UIE counting periods  */
} HBUZZER_Mode_t;

/* THE tone-end mechanism: registered with MTIM as TIM10's update
 * callback. Runs in ISR context at TIM10's priority — register
 * writes only, no kernel calls, nothing heavier.                     */
static void HBUZZER_privateUpdateISR(void);

/* Common start sequence (arbitration + DeInit + PWMStart + state),
 * shared by Blip / ToneStart / ToneStartTimed. Task context only.    */
static HBUZZER_ReturnType_t HBUZZER_privateStartTone(HBUZZER_Priority_t Copy_Priority,
                                                     u16 Copy_u16FreqHz,
                                                     u16 Copy_u16DurationMs,  /* 0 = sustained */
                                                     u8  Copy_u8Timed);

#endif /* HBUZZER_PRIVATE_H_ */
