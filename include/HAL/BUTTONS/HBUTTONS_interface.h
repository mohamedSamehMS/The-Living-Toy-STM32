/*
 * HBUTTONS_interface.h - polled physical-button HAL
 */
#ifndef HBUTTONS_INTERFACE_H_
#define HBUTTONS_INTERFACE_H_

#include "STD_TYPES.h"

typedef enum
{
    HBUTTONS_OK = 0,
    HBUTTONS_NOT_INITIALIZED,
    HBUTTONS_INVALID_PARAM,
    HBUTTONS_INIT_FAILED,
    HBUTTONS_NO_EVENT
} HBUTTONS_ReturnType_t;

typedef enum
{
    HBUTTONS_BUTTON_CONFIRM = 0,
    HBUTTONS_BUTTON_BACK,
    HBUTTONS_BUTTON_NEXT,
    HBUTTONS_BUTTON_COUNT
} HBUTTONS_ButtonIndex_t;

HBUTTONS_ReturnType_t HBUTTONS_enumInit(void);
void HBUTTONS_voidPoll(void);
HBUTTONS_ReturnType_t HBUTTONS_enumGetEvent(u8 *Copy_pu8ButtonIndex);

#endif /* HBUTTONS_INTERFACE_H_ */
