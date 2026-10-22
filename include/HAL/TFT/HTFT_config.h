/*
 * HTFT_config.h
 *
 *  HAL configuration — 1.8" TFT module, 128x160, ST7735 controller.
 *  (Module label: "1.8 TFT SPI 128*160". This is NOT the ILI9341
 *  240x320 module the project originally assumed — every ILI9341-era
 *  assumption has been purged from this driver.)
 *
 *  ROLE OF THIS FILE: every user-tunable hardware-mapping choice for
 *  the display lives here and ONLY here. Rewiring the module, moving
 *  the bus, or swapping the panel variant = editing this file only.
 *  No code anywhere else hardcodes a pin, an offset, or a variant.
 *
 *  Layer position: HAL, above MCAL. The driver consumes MGPIO (control
 *  pins), MSPI (pixel/command transport), MSYSTICK (init delays).
 *
 *  References: Sitronix ST7735 datasheet (init registers, MADCTL);
 *  RM0368 §8 (GPIO), §28 (SPI) — MCU side only.
 */

#ifndef HTFT_CONFIG_H_
#define HTFT_CONFIG_H_

#include <MCAL/GPIO/MGPIO_interface.h>   /* port/pin enums            */
#include <MCAL/SPI/MSPI_interface.h>     /* MSPI1 instance handle     */

/* =====================================================================
 *  1. CONTROL PINS — plain GPIO outputs, owned ENTIRELY by HTFT
 *
 *  These are NOT alternate-function pins: CS/RST/DC are ordinary
 *  digital outputs the driver toggles around each SPI transaction.
 *  Nothing else in the system may touch them.
 *
 *  Pin history (recorded so the choice is never accidentally undone):
 *    - CS  -> PB0, RST -> PB1: free, adjacent, no conflicts.
 *    - DC  -> B10: chosen after discovering the WeAct Black Pill does
 *      NOT break PB11 out to any header pin (the silkscreen labels
 *      jump B10 -> B12). The MGPIO package mask (all of port B valid)
 *      cannot see board breakout — only the silkscreen can.
 *
 *  Idle states (set by HTFT_voidInit, maintained by the driver):
 *    CS  HIGH = deselected, RST HIGH = released, DC LOW.
 * ===================================================================== */
#define HTFT_CS_PORT            MGPIO_PORTB
#define HTFT_CS_PIN             MGPIO_PIN_0      /* chip select, active LOW  */
#define HTFT_RST_PORT           MGPIO_PORTB
#define HTFT_RST_PIN            MGPIO_PIN_1      /* hardware reset, active LOW */
#define HTFT_DC_PORT            MGPIO_PORTB
#define HTFT_DC_PIN             MGPIO_PIN_10     /* "A0" on the module header:
                                                    0 = command byte, 1 = data.
                                                    May be silkscreened DC/RS. */

/* =====================================================================
 *  2. SPI TRANSPORT
 *
 *  Requirements on the configured instance (all verified on hardware
 *  in this build): MASTER, CPOL=LOW + CPHA=FIRST (SPI mode 0), 8-bit
 *  frames, MSB first, software slave management. Mode 0 is what both
 *  this panel and (later) the SD card expect — one config serves both.
 *
 *  Speed: MSPI_BR_FPCLCK_DIV_2 at HSI 16 MHz = 8 MHz SCK — soak-tested
 *  clean on fills, primitives, and text. DIV_8 (2 MHz) is the safe
 *  fallback if a wiring change ever introduces corruption at speed.
 * ===================================================================== */
#define HTFT_SPI_INSTANCE       MSPI1

/* =====================================================================
 *  3. PANEL VARIANT ("tab" color on the flex-cable stiffener)
 *
 *  ST7735 panels of identical size come in variants that differ in:
 *    - a small GRAM offset: the visible 128x160 window may start at
 *      (2,1) inside the controller's memory instead of (0,0)
 *    - color inversion: some glasses need the INVON command
 *  The variant is physically identified by the colored tab where the
 *  flex enters the glass. THIS panel: black tab -> INVON, no offset,
 *  determined empirically (black<-white swap + yellow-for-red were the
 *  fingerprints — see the HTFT_private.h MADCTL note).
 *
 *  How to calibrate an unknown panel: leave TAB_RED, run the standard
 *  color test, and read the symptoms:
 *    colors negative            -> the INVERTED variant
 *    shapes shifted ~2 px       -> the OFFSET variant
 * ===================================================================== */
#define HTFT_TAB_RED            0   /* offsets (0,0), colors normal   */
#define HTFT_TAB_GREEN          1   /* offsets (2,1), colors normal   */
#define HTFT_TAB_BLACK          2   /* offsets (0,0), colors inverted */

#define HTFT_TAB_TYPE           HTFT_TAB_BLACK   /* verified on hardware */

/* Derived per-variant constants — consumed by HTFT_voidSetWindow and
 * the init sequence. Do not edit these directly; edit TAB_TYPE.       */
#if HTFT_TAB_TYPE == HTFT_TAB_GREEN
  #define HTFT_COL_OFFSET       2       /* GRAM columns before visible area */
  #define HTFT_ROW_OFFSET       1       /* GRAM rows before visible area    */
  #define HTFT_INVERT_COLORS    0
#elif HTFT_TAB_TYPE == HTFT_TAB_BLACK
  #define HTFT_COL_OFFSET       0
  #define HTFT_ROW_OFFSET       0
  #define HTFT_INVERT_COLORS    1
#else /* HTFT_TAB_RED */
  #define HTFT_COL_OFFSET       0
  #define HTFT_ROW_OFFSET       0
  #define HTFT_INVERT_COLORS    0
#endif

/* The literal command byte sent during init: 0x21 = INVON (invert on),
 * 0x20 = INVOFF. Selected here so the init table stays a pure table.  */
#if HTFT_INVERT_COLORS == 1
  #define HTFT_INVERSION_CMD    0x21
#else
  #define HTFT_INVERSION_CMD    0x20
#endif

/* =====================================================================
 *  4. DEBUG AID — rectangle bounds checking (default OFF)
 *
 *  0 = current proven behavior: FillRect trusts the caller. Off-screen
 *      coordinates wrap into GRAM and corrupt distant screen regions —
 *      fast, and fine while all callers are ours.
 *  1 = FillRect silently clips/rejects out-of-bounds rectangles.
 *      Costs a few compares per call. Flip it on ONLY while hunting a
 *      suspected off-screen draw; it converts a mystery corruption on
 *      the far side of the screen into "the caller was wrong".
 * ===================================================================== */
#define HTFT_ENABLE_BOUNDS_CHECK    0

#endif /* HTFT_CONFIG_H_ */
