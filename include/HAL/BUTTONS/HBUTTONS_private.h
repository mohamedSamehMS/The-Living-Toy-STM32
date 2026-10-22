/*
 * HBUTTONS_private.h - private polled-button definitions
 */
#ifndef HBUTTONS_PRIVATE_H_
#define HBUTTONS_PRIVATE_H_

#include "STD_TYPES.h"
#include "HAL/BUTTONS/HBUTTONS_interface.h"

typedef enum
{
    HBUTTONS_STATE_RELEASED = 0,
    HBUTTONS_STATE_PRESSED
} HBUTTONS_State_t;

#endif /* HBUTTONS_PRIVATE_H_ */
