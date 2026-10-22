/*
 * H74HC595_program.c — 74HC595 shift-register driver, IMPLEMENTATION
 *
 *  Project 4 — Interactive Gaming Hub (ARM Embedded Systems Diploma)
 *  Target  : STM32F401CCU6 (WeAct Black Pill), register-level bare metal
 *  Layer   : HAL -> MCAL (MGPIO). Deliberately no MSPI dependency in
 *            this revision (plain GPIO bit-bang); a future backend can
 *            put SER/SRCLK on real SPI pins and keep RCLK as GPIO —
 *            the 595 tolerates sharing an SPI bus safely: foreign
 *            transfers just shift junk into the register, and junk is
 *            harmless because outputs only change on OUR latch pulse,
 *            issued immediately after OUR transfer.
 *  File    : only H74HC595 logic file; tunables in H74HC595_config.h,
 *            prototypes in H74HC595_interface.h.
 *
 *  REFERENCES
 *    74HC595 datasheet (Nexperia/TI): shift/latch timing, pinout
 *    RM0368 ch. 8 : GPIO (BSRR atomic set/reset via MGPIO)
 *
 *  TRANSFER ANATOMY (one SendData, N chips):
 *    for each chip (near->far order = array order):
 *        for each bit (MSB first):
 *            put bit on SER
 *            pulse SRCLK  (rising edge loads the bit)
 *    pulse RCLK once (rising edge = all N*8 outputs update together)
 *
 *  ERROR POLICY (same as HLEDMTX rev 3): INIT CHECKS EVERYTHING,
 *  RUNTIME CHECKS NOTHING. All pins are config constants, so runtime
 *  calls cannot fail after a successful init; first init error is
 *  latched in Global_udtInitError for debugger inspection. Runtime
 *  calls are also ISR-conditional on the timing knob — see config.
 *
 *  Created on: Aug 14, 2026
 *      Author: dell
 */

#include "BIT_MATH.h"		/* GET_BIT */
#include "STD_TYPES.h"		/* u8 */

#include "MCAL/GPIO/MGPIO_interface.h"	/* pin API (new MGPIO_interface.h) */

#include "H74HC595_config.h"	/* pins, chain length, timing knob */

/* SysTick is a dependency ONLY when the clock knob asks for delays.
 * Conditional include = the default (no-delay) build has no SysTick
 * coupling at all, which is what makes ISR-resident refresh safe. */
#if (HC595_MIN_HALF_CLOCK_US > 0)
#include "MCAL/SYSTICK/MSYSTICK_interface.h"
#endif

#include "H74HC595_interface.h"

/* ============ Private helpers ============ */

/* First MGPIO error seen during Init (MGPIO_OK = none). Debugger-
 * visible latch, same rationale as HLEDMTX: the public API stays void
 * so ISR call sites stay clean; read this after Init during bring-up.
 *   INVALID_PIN/PORT -> config header names a pin this package lacks
 *   OK but outputs dead -> port clock not enabled (MGPIO cannot see
 *   the un-clocked case — hardware ignores the write, driver reports
 *   success), or wiring. */
static MGPIO_ReturnType_t Global_udtInitError = MGPIO_OK;

/* Record the FIRST error only — root cause first, and later config
 * calls still run (partial init is more useful in bring-up than
 * aborting after the first problem). */
static void HC595_voidTry(MGPIO_ReturnType_t Copy_udtStatus)
{
    if (Global_udtInitError == MGPIO_OK)
    {
        Global_udtInitError = Copy_udtStatus;
    }
}

/* One shift-clock pulse: LOW -> HIGH -> LOW. The RISING edge loads
 * whatever SER holds into stage 0 of the register; every existing
 * bit moves one stage deeper (toward QH and out QH' into the next
 * chip). The two versions:
 *   delayed (knob > 0): busy-wait between edges — breadboard rescue
 *   default (knob = 0): back-to-back writes. SAFETY ARGUMENT: the
 *     74HC595 needs ~tens of ns setup/pulse at 3.3 V, and the time
 *     spent inside ONE MGPIO_SetPinValue call (validation + a BSRR
 *     store) is a few hundred ns at 84 MHz — the data-pin write that
 *     precedes each clock pulse therefore provides 10x the required
 *     setup time by accident. Hold time is met trivially: the next
 *     SER change happens only after this falling edge. */
static void HC595_voidPulseShiftClock(void)
{
#if (HC595_MIN_HALF_CLOCK_US > 0)
    MGPIO_SetPinValue(HC595_SHIFT_CLOCK_PORT, HC595_SHIFT_CLOCK_PIN, MGPIO_PIN_HIGH);
    MSYSTICK_voidSetBusyWait_us(HC595_MIN_HALF_CLOCK_US);
    MGPIO_SetPinValue(HC595_SHIFT_CLOCK_PORT, HC595_SHIFT_CLOCK_PIN, MGPIO_PIN_LOW);
    MSYSTICK_voidSetBusyWait_us(HC595_MIN_HALF_CLOCK_US);
#else
    /* No delay — see the safety argument above. */
    MGPIO_SetPinValue(HC595_SHIFT_CLOCK_PORT, HC595_SHIFT_CLOCK_PIN, MGPIO_PIN_HIGH);
    MGPIO_SetPinValue(HC595_SHIFT_CLOCK_PORT, HC595_SHIFT_CLOCK_PIN, MGPIO_PIN_LOW);
#endif
}

/* One latch pulse: LOW -> HIGH -> LOW. On the RISING edge every chip
 * copies its shift register to its output latches in the same clock
 * (RCLK is shared by the whole chain) — this single edge is the
 * ATOMIC OUTPUT UPDATE. Between latches the outputs are frozen: you
 * can shift garbage all day and nothing appears on the pins. That is
 * the property both the no-ghosting matrix scan and the "595 can
 * share an SPI bus" note rely on. */
static void HC595_voidPulseLatchClock(void)
{
#if (HC595_MIN_HALF_CLOCK_US > 0)
    MGPIO_SetPinValue(HC595_LATCH_CLOCK_PORT, HC595_LATCH_CLOCK_PIN, MGPIO_PIN_HIGH);
    MSYSTICK_voidSetBusyWait_us(HC595_MIN_HALF_CLOCK_US);
    MGPIO_SetPinValue(HC595_LATCH_CLOCK_PORT, HC595_LATCH_CLOCK_PIN, MGPIO_PIN_LOW);
    MSYSTICK_voidSetBusyWait_us(HC595_MIN_HALF_CLOCK_US);
#else
    /* No delay — same margin argument as the shift clock. */
    MGPIO_SetPinValue(HC595_LATCH_CLOCK_PORT, HC595_LATCH_CLOCK_PIN, MGPIO_PIN_HIGH);
    MGPIO_SetPinValue(HC595_LATCH_CLOCK_PORT, HC595_LATCH_CLOCK_PIN, MGPIO_PIN_LOW);
#endif
}

/* ============ Public functions ============ */

void H74HC595_voidInit(void)
{
    /* Zero payload sized to the chain — used to clear the registers.
     * Sized from the config constant, so it always matches the chain
     * even if the count changes later. */
    u8 Local_u8Blank[NUMBER_OF_74HC595_CHIPS] = {0};

    Global_udtInitError = MGPIO_OK;

    /* --- Configure the 3 control pins ---------------------------------
     * Mode FIRST, then output stage (MGPIO ordering requirement).
     * PUSH_PULL + LOW speed: these are DC-ish control lines; slow
     * edges minimize ringing on breadboard clock wiring, and the
     * resulting bit-bang clock is far below any speed limit anyway.
     * Idle LOW on all three: clocks rest at their inactive level and
     * SER idles low, so nothing accidental can clock a bit in.
     * Every call checked via Try() — first error latched. */

    /* Data pin (SER) */
    HC595_voidTry(MGPIO_SetPinMode(HC595_DATA_PORT, HC595_DATA_PIN, MGPIO_MODE_OUTPUT));
    HC595_voidTry(MGPIO_SetPinOutputConfig(HC595_DATA_PORT, HC595_DATA_PIN, MGPIO_OUTPUT_PUSH_PULL, MGPIO_SPEED_LOW));
    HC595_voidTry(MGPIO_SetPinValue(HC595_DATA_PORT, HC595_DATA_PIN, MGPIO_PIN_LOW));

    /* Shift clock pin (SRCLK) */
    HC595_voidTry(MGPIO_SetPinMode(HC595_SHIFT_CLOCK_PORT, HC595_SHIFT_CLOCK_PIN, MGPIO_MODE_OUTPUT));
    HC595_voidTry(MGPIO_SetPinOutputConfig(HC595_SHIFT_CLOCK_PORT, HC595_SHIFT_CLOCK_PIN, MGPIO_OUTPUT_PUSH_PULL, MGPIO_SPEED_LOW));
    HC595_voidTry(MGPIO_SetPinValue(HC595_SHIFT_CLOCK_PORT, HC595_SHIFT_CLOCK_PIN, MGPIO_PIN_LOW));

    /* Latch clock pin (RCLK) */
    HC595_voidTry(MGPIO_SetPinMode(HC595_LATCH_CLOCK_PORT, HC595_LATCH_CLOCK_PIN, MGPIO_MODE_OUTPUT));
    HC595_voidTry(MGPIO_SetPinOutputConfig(HC595_LATCH_CLOCK_PORT, HC595_LATCH_CLOCK_PIN, MGPIO_OUTPUT_PUSH_PULL, MGPIO_SPEED_LOW));
    HC595_voidTry(MGPIO_SetPinValue(HC595_LATCH_CLOCK_PORT, HC595_LATCH_CLOCK_PIN, MGPIO_PIN_LOW));

    /* --- Clear the chain ------------------------------------------------
     * Shift-register contents are INDETERMINATE at power-up — until
     * the first latch the outputs can drive anything (LED matrix:
     * random LEDs). One SendData of zeros shifts known bits through
     * every stage and latches: all outputs LOW, known dark state
     * from init until the first real transfer. Reuses the production
     * path instead of a bespoke loop — one code path, one test. */
    H74HC595_voidSendData(Local_u8Blank);
}

void H74HC595_voidSendData(const u8* Copy_pu8DataArray)
{
    u8 Local_u8Chip;
    u8 Local_u8Bit;

    /* Defensive no-op on NULL: a NULL deref inside the POV ISR would
     * be a hard fault; silently keeping the last latched state is the
     * least-bad failure mode. */
    if (Copy_pu8DataArray == NULL)
    {
        return;
    }

    /* CHIP ORDER — array order = near->far travel order:
     * chip 0's byte is shifted FIRST, gets pushed one chip deeper by
     * every later byte, and therefore ends up in the chip FARTHEST
     * from the MCU; the LAST byte stays in the chip wired directly to
     * SER. (Documented on the interface; repeated here because this
     * loop is where the convention lives. If rows/columns come out
     * swapped on first matrix power-up, this mapping is suspect #1.) */
    for (Local_u8Chip = 0; Local_u8Chip < NUMBER_OF_74HC595_CHIPS; Local_u8Chip++)
    {
        u8 Local_u8Data = Copy_pu8DataArray[Local_u8Chip];

        /* BIT ORDER — MSB first: bit 7 is shifted first and travels
         * 8 stages to Q7/QH; bit 0 shifts last into stage 0. Chosen
         * to match SPI mode-0 framing, so payloads stay byte-for-byte
         * compatible with a future MSPI backend. */
        for (Local_u8Bit = 0; Local_u8Bit < 8; Local_u8Bit++)
        {
            /* Present the bit on SER BEFORE clocking it in —
             * setup time is provided by the MGPIO call overhead
             * itself (see the shift-clock comment). The ternary
             * maps the extracted 0/1 to the enum-valued pin levels
             * (enum value => runtime expression, not #if). */
            u8 Local_u8BitValue = GET_BIT(Local_u8Data, 7 - Local_u8Bit);

            MGPIO_SetPinValue(HC595_DATA_PORT, HC595_DATA_PIN,
                (Local_u8BitValue != 0) ? MGPIO_PIN_HIGH : MGPIO_PIN_LOW);

            /* Rising edge: bit enters stage 0, chain advances. */
            HC595_voidPulseShiftClock();
        }
    }

    /* Latch ONLY at the very end — N*8 bits commit to the outputs in
     * one edge. Latching per byte would expose half-transferred
     * states on the pins (on the matrix: torn scan lines, since the
     * far chip would still hold the PREVIOUS line's column bits). */
    HC595_voidPulseLatchClock();
}
