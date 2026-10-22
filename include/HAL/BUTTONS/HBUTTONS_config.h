/*
 * HBUTTONS_config.h - polled physical-button configuration
 */
#ifndef HBUTTONS_CONFIG_H_
#define HBUTTONS_CONFIG_H_

#include "STD_TYPES.h"
#include <MCAL/GPIO/MGPIO_interface.h>

#define HBUTTONS_CONFIRM_PORT       MGPIO_PORTB
#define HBUTTONS_CONFIRM_PIN        MGPIO_PIN_5
#define HBUTTONS_BACK_PORT          MGPIO_PORTB
#define HBUTTONS_BACK_PIN           MGPIO_PIN_9
#define HBUTTONS_NEXT_PORT          MGPIO_PORTB
#define HBUTTONS_NEXT_PIN           MGPIO_PIN_12

#define HBUTTONS_DEBOUNCE_SAMPLES   3u
#define HBUTTONS_RELEASE_SAMPLES    3u

#if (HBUTTONS_DEBOUNCE_SAMPLES == 0u) || (HBUTTONS_RELEASE_SAMPLES == 0u)
#error "HBUTTONS: debounce and release windows must be non-zero"
#endif
_Static_assert(HBUTTONS_CONFIRM_PIN <= MGPIO_PIN_15,
               "HBUTTONS: confirm pin is outside GPIO range");
_Static_assert(HBUTTONS_BACK_PIN <= MGPIO_PIN_15,
               "HBUTTONS: back pin is outside GPIO range");
_Static_assert(HBUTTONS_NEXT_PIN <= MGPIO_PIN_15,
               "HBUTTONS: next pin is outside GPIO range");
_Static_assert(HBUTTONS_CONFIRM_PORT == MGPIO_PORTB,
               "HBUTTONS: confirm must use GPIOB");
_Static_assert(HBUTTONS_BACK_PORT == MGPIO_PORTB,
               "HBUTTONS: back must use GPIOB");
_Static_assert(HBUTTONS_NEXT_PORT == MGPIO_PORTB,
               "HBUTTONS: next must use GPIOB");

#endif /* HBUTTONS_CONFIG_H_ */
