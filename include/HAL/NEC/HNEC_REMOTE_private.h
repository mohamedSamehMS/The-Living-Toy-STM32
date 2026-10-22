/*
 * HNEC_REMOTE_private.h — NEC remote controller, PRIVATE header
 *
 *  Never include from application code. Private prototypes only —
 *  all state is file-static in HNEC_REMOTE_program.c (the MSYSTICK
 *  Rev-2 lesson: no externs in private headers).
 *
 *  Created on: Sep 13, 2026
 *      Author: dell
 */
#ifndef HNEC_REMOTE_PRIVATE_H_
#define HNEC_REMOTE_PRIVATE_H_

#include "STD_TYPES.h"

#include "HAL/NEC/HNEC_REMOTE_interface.h"     /* event type */

/* The device-layer service: run HIR's decode, apply identity/policy
 * (address filter, mapping, repeat decision), store/emit the event,
 * then close the frame with the window the policy chose. The ONLY
 * place HIR_enumCloseFrame is called — the pair contract is
 * structurally guaranteed here (one straight-line path). */
static void HNEC_REMOTE_privateService(void);

/* Linear lookup in the config table; BTN_NONE = unmapped code. */
static VirtualButton_t HNEC_REMOTE_privateLookupButton(u8 Copy_u8NecCmd);

#endif /* HNEC_REMOTE_PRIVATE_H_ */
