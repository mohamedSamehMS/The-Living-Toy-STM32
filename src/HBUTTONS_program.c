/*
 * HBUTTONS_program.c - polled, active-low physical buttons
 */
#include "STD_TYPES.h"
#include <MCAL/GPIO/MGPIO_interface.h>
#include "HAL/BUTTONS/HBUTTONS_config.h"
#include "HAL/BUTTONS/HBUTTONS_private.h"

typedef struct
{
    MGPIO_Port_t Port;
    MGPIO_Pin_t Pin;
    HBUTTONS_State_t State;
    u8 StableCount;
    u8 EventLatched;
} HBUTTONS_Button_t;

static HBUTTONS_Button_t HBUTTONS_astButtons[HBUTTONS_BUTTON_COUNT] =
{
    { HBUTTONS_CONFIRM_PORT, HBUTTONS_CONFIRM_PIN,
      HBUTTONS_STATE_RELEASED, 0u, 0u },
    { HBUTTONS_BACK_PORT, HBUTTONS_BACK_PIN,
      HBUTTONS_STATE_RELEASED, 0u, 0u },
    { HBUTTONS_NEXT_PORT, HBUTTONS_NEXT_PIN,
      HBUTTONS_STATE_RELEASED, 0u, 0u }
};
static u8 HBUTTONS_u8Initialized;

HBUTTONS_ReturnType_t HBUTTONS_enumInit(void)
{
    u8 Local_u8Index;
    MGPIO_ReturnType_t Local_eStatus;

    if (HBUTTONS_u8Initialized != 0u)
    {
        return HBUTTONS_OK;
    }

    for (Local_u8Index = 0u; Local_u8Index < HBUTTONS_BUTTON_COUNT; Local_u8Index++)
    {
        Local_eStatus = MGPIO_SetPinMode(HBUTTONS_astButtons[Local_u8Index].Port,
                                         HBUTTONS_astButtons[Local_u8Index].Pin,
                                         MGPIO_MODE_INPUT);
        if (Local_eStatus != MGPIO_OK)
        {
            return HBUTTONS_INIT_FAILED;
        }
        Local_eStatus = MGPIO_SetPinPull(HBUTTONS_astButtons[Local_u8Index].Port,
                                         HBUTTONS_astButtons[Local_u8Index].Pin,
                                         MGPIO_PULL_UP);
        if (Local_eStatus != MGPIO_OK)
        {
            return HBUTTONS_INIT_FAILED;
        }
    }

    for (Local_u8Index = 0u; Local_u8Index < HBUTTONS_BUTTON_COUNT; Local_u8Index++)
    {
        HBUTTONS_astButtons[Local_u8Index].State = HBUTTONS_STATE_RELEASED;
        HBUTTONS_astButtons[Local_u8Index].StableCount = 0u;
        HBUTTONS_astButtons[Local_u8Index].EventLatched = 0u;
    }
    HBUTTONS_u8Initialized = 1u;
    return HBUTTONS_OK;
}

void HBUTTONS_voidPoll(void)
{
    u8 Local_u8Index;
    u8 Local_u8Level;

    if (HBUTTONS_u8Initialized == 0u)
    {
        return;
    }

    for (Local_u8Index = 0u; Local_u8Index < HBUTTONS_BUTTON_COUNT; Local_u8Index++)
    {
        Local_u8Level = 1u;
        (void)MGPIO_GetPinValue(HBUTTONS_astButtons[Local_u8Index].Port,
                                HBUTTONS_astButtons[Local_u8Index].Pin,
                                &Local_u8Level);

        if (HBUTTONS_astButtons[Local_u8Index].State == HBUTTONS_STATE_RELEASED)
        {
            if (Local_u8Level == 0u)
            {
                if (HBUTTONS_astButtons[Local_u8Index].StableCount <
                    HBUTTONS_DEBOUNCE_SAMPLES)
                {
                    HBUTTONS_astButtons[Local_u8Index].StableCount++;
                }
                if (HBUTTONS_astButtons[Local_u8Index].StableCount >=
                    HBUTTONS_DEBOUNCE_SAMPLES)
                {
                    HBUTTONS_astButtons[Local_u8Index].State = HBUTTONS_STATE_PRESSED;
                    HBUTTONS_astButtons[Local_u8Index].StableCount = 0u;
                    HBUTTONS_astButtons[Local_u8Index].EventLatched = 1u;
                }
            }
            else
            {
                HBUTTONS_astButtons[Local_u8Index].StableCount = 0u;
            }
        }
        else if (Local_u8Level != 0u)
        {
            if (HBUTTONS_astButtons[Local_u8Index].StableCount <
                HBUTTONS_RELEASE_SAMPLES)
            {
                HBUTTONS_astButtons[Local_u8Index].StableCount++;
            }
            if (HBUTTONS_astButtons[Local_u8Index].StableCount >=
                HBUTTONS_RELEASE_SAMPLES)
            {
                HBUTTONS_astButtons[Local_u8Index].State = HBUTTONS_STATE_RELEASED;
                HBUTTONS_astButtons[Local_u8Index].StableCount = 0u;
            }
        }
        else
        {
            HBUTTONS_astButtons[Local_u8Index].StableCount = 0u;
        }
    }
}

HBUTTONS_ReturnType_t HBUTTONS_enumGetEvent(u8 *Copy_pu8ButtonIndex)
{
    u8 Local_u8Index;

    if (Copy_pu8ButtonIndex == (u8 *)0)
    {
        return HBUTTONS_INVALID_PARAM;
    }
    if (HBUTTONS_u8Initialized == 0u)
    {
        return HBUTTONS_NOT_INITIALIZED;
    }

    for (Local_u8Index = 0u; Local_u8Index < HBUTTONS_BUTTON_COUNT; Local_u8Index++)
    {
        if (HBUTTONS_astButtons[Local_u8Index].EventLatched != 0u)
        {
            HBUTTONS_astButtons[Local_u8Index].EventLatched = 0u;
            *Copy_pu8ButtonIndex = Local_u8Index;
            return HBUTTONS_OK;
        }
    }
    return HBUTTONS_NO_EVENT;
}
