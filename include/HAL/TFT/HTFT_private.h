/*
 * HTFT_private.h
 *
 *  INTERNAL definitions of the HTFT driver: ST7735 command codes and
 *  controller register values. Included ONLY by HTFT_program.c —
 *  never from HAL consumers or the application layer. If the app ever
 *  needs a value from here, it belongs in HTFT_interface.h instead.
 *
 *  Reference: Sitronix ST7735 datasheet — command table and MADCTL /
 *  COLMOD register descriptions.
 */

#ifndef HTFT_PRIVATE_H_
#define HTFT_PRIVATE_H_

#include "STD_TYPES.h"

/* =====================================================================
 *  ST7735 COMMAND CODES used by this driver
 *
 *  Note for anyone porting from ILI9341 code: SLPOUT / DISPON / CASET /
 *  RASET(=PASET) / RAMWR / MADCTL / COLMOD share their command NUMBERS
 *  with the ILI9341 — which is exactly why the ILI9341 init woke this
 *  panel up at all — but their DATA semantics differ (see COLMOD).
 * ===================================================================== */
#define HTFT_CMD_SWRESET        0x01    /* soft reset — long settle follows   */
#define HTFT_CMD_SLPOUT         0x11    /* exit sleep mode                     */
#define HTFT_CMD_INVOFF         0x20    /* non-inverted display                */
#define HTFT_CMD_INVON          0x21    /* inverted display (black-tab panels)*/
#define HTFT_CMD_NORON          0x13    /* normal display mode (vs partial)   */
#define HTFT_CMD_DISPON         0x29    /* display on                          */
#define HTFT_CMD_CASET          0x2A    /* column address set — X window       */
#define HTFT_CMD_RASET          0x2B    /* row address set — Y window (the
                                           ILI9341 calls this PASET)          */
#define HTFT_CMD_RAMWR          0x2C    /* memory write — data after this is
                                           pixel data until CS rises          */
#define HTFT_CMD_MADCTL         0x36    /* memory data access control          */
#define HTFT_CMD_COLMOD         0x3A    /* interface pixel format              */

/* =====================================================================
 *  MADCTL (0x36) VALUE — orientation + color order
 *
 *  Bit layout (ST7735):
 *      0x80  MY   row    order mirror (vertical flip)
 *      0x40  MX   column order mirror (horizontal flip)
 *      0x20  MV   row/column exchange (axes swap = landscape)
 *      0x10  ML   vertical refresh order (leave 0)
 *      0x08  RGB  0 = RGB color order (0xF800 shows RED)
 *                 1 = BGR color order (0xF800 shows BLUE)
 *      0x04  MH   horizontal refresh order (leave 0)
 *
 *  0xC0 = MY | MX: both axes mirrored. The panel's native scan starts
 *  at the bottom-right; mirroring both axes puts logical (0,0) at the
 *  physical TOP-LEFT with x rightward and y downward — the convention
 *  every game and UI in this project is written against. Verified on
 *  hardware: the 40x40 corner-block test landed at the coded corners.
 *
 *  History: 0xC8 was tried first (RGB bit set) and pure red rendered
 *  as blue — the panel wants the bit CLEAR. Both bits are now correct
 *  and verified; do not "fix" either without re-running the color test.
 * ===================================================================== */
#define HTFT_MADCTL_VALUE       0xC0

/* =====================================================================
 *  COLMOD (0x3A) VALUE — bits per pixel
 *
 *  ST7735 encoding: 0x05 = 16 bits per pixel (RGB565). NOTE: the
 *  ILI9341's value for the same setting is 0x55 — sending 0x55 to an
 *  ST7735 is INVALID, which is one reason the ILI9341 init sequence
 *  produced nothing usable on this panel. Same command number,
 *  different encoding — the classic cross-controller trap.
 * ===================================================================== */
#define HTFT_COLMOD_VALUE       0x05

/* =====================================================================
 *  RESET / POST-COMMAND TIMINGS
 *
 *  Datasheet minimums are RST-low >= 10 us and ~120 ms internal
 *  settle after release; we run comfortably slower in whole
 *  milliseconds because init happens exactly once — milliseconds
 *  here cost nothing and buy margin.
 * ===================================================================== */
#define HTFT_RST_HIGH_MS        10      /* idle level before the pulse    */
#define HTFT_RST_LOW_MS         10      /* reset assert (>= 10 us needed) */
#define HTFT_RST_SETTLE_MS      120     /* internal reset completion      */

#endif /* HTFT_PRIVATE_H_ */
