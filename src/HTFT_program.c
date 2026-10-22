/*
 * HTFT_program.c
 *
 *  Implementation of the ST7735 TFT HAL driver (1.8", 128x160, RGB565).
 *
 *  ARCHITECTURE — three layers inside this file, bottom to top:
 *    1. TRANSACTION layer: WriteCommand / WriteData / WriteData16 —
 *       the SPI protocol pattern every panel access reduces to.
 *    2. WINDOW layer: SetWindow — programs a rectangle in GRAM and
 *       opens it with RAMWR; everything after is a raw pixel stream.
 *    3. DRAWING layer: FillRect and everything built on it — fills,
 *       lines, shapes, text. All drawing reduces to streams into
 *       windows; nothing draws per-pixel except true diagonals.
 *
 *  MCAL dependencies: MGPIO (control pins — atomic BSRR value writes
 *  only, so this driver is safe against ISRs on the same port),
 *  MSPI (blocking byte transport), MSYSTICK (init delays).
 *  No direct register access from this layer.
 *
 *  RAM footprint: the 64-pixel glyph scratch buffer below (128 bytes)
 *  is the driver's ENTIRE RAM usage. No framebuffer — see the
 *  architecture note in HTFT_interface.h for how redraw works.
 *
 *  Init table: { command, dataCount, delayMs, data bytes... } records,
 *  terminated by a lone 0x00 (command NOP — safe sentinel, cannot
 *  collide with any real entry). Values are the well-proven
 *  Adafruit ST7735R register set; per-panel variant bits come from
 *  HTFT_config.h (inversion) and HTFT_private.h (MADCTL/COLMOD).
 *
 *  Controller-swap note: porting to an ILI9341 240x320 module = swap
 *  this init table, the dimensions in HTFT_interface.h, and (maybe)
 *  the MADCTL/COLMOD values in HTFT_private.h. The transaction,
 *  window, and drawing layers are controller-agnostic.
 */

#include <MCAL/GPIO/MGPIO_interface.h>
#include <MCAL/SPI/MSPI_interface.h>
#include <MCAL/SYSTICK/MSYSTICK_interface.h>

#include "HAL/TFT/HTFT_config.h"
#include "HAL/TFT/HTFT_interface.h"
#include "HAL/TFT/HTFT_private.h"
#include "HAL/TFT/HTFT_font.h"

/* ------------------------------------------------------------------ *
 *  PRIVATE FUNCTION PROTOTYPES                                       *
 *  (WritePixels needs one: its definition sits below its first use.) *
 * ------------------------------------------------------------------ */
static void HTFT_voidDelayMs(u32 Copy_u32Ms);
static void HTFT_voidWriteCommand(u8 Copy_u8Command);
static void HTFT_voidWriteData(u8 Copy_u8Data);
static void HTFT_voidWriteData16(u16 Copy_u16Data);
static void HTFT_voidWritePixels(const u16 *Copy_pu16Pixels, u32 Copy_u32Count);
static void HTFT_voidSetWindow(u16 Copy_u16X0, u16 Copy_u16Y0,
                               u16 Copy_u16X1, u16 Copy_u16Y1);
static void HTFT_voidRunInitSequence(void);
static s32 HTFT_s32ISqrt(s32 Copy_s32Value);   /* integer floor sqrt */
/* ------------------------------------------------------------------ *
 *  INIT SEQUENCE — see the register annotations on each line.
 *  Delay unit: milliseconds, applied AFTER the command's data.
 * ------------------------------------------------------------------ */
static const u8 HTFT_AU8InitSequence[] =
{
    0x01, 0, 150,                                 /* SWRESET — full internal
                                                     reset, long settle       */
    0x11, 0, 255,                                 /* SLPOUT — exit sleep; the
                                                     datasheet's longest wait
                                                     belongs here             */
    0xB1, 3, 0,   0x01, 0x2C, 0x2D,               /* frame rate, normal mode  */
    0xB2, 3, 0,   0x01, 0x2C, 0x2D,               /* frame rate, inversion    */
    0xB3, 6, 0,   0x01, 0x2C, 0x2D,
                  0x01, 0x2C, 0x2D,               /* frame rate, idle mode    */
    0xB4, 1, 0,   0x07,                           /* LCD inversion polarity
                                                     (line vs frame inversion)*/
    0xC0, 3, 0,   0xA2, 0x02, 0x84,               /* power control 1 — GVDD /
                                                     VRH analog voltages      */
    0xC1, 1, 0,   0xC5,                           /* power control 2 — VGH/VGL*/
    0xC2, 2, 0,   0x0A, 0x00,                     /* power control 3 — normal
                                                     + idle mode currents     */
    0xC3, 2, 0,   0x8A, 0x2A,                     /* power control 4 — partial*/
    0xC4, 2, 0,   0x8A, 0xEE,                     /* power control 5 — idle   */
    0xC5, 1, 0,   0x0E,                           /* VCOM voltage — panel
                                                     specific; wrong VCOM =
                                                     washed-out or dark image */
    HTFT_INVERSION_CMD, 0, 0,                     /* INVON/INVOFF — selected by
                                                     the tab type in config:
                                                     this panel (black tab)
                                                     needs inversion ON, or
                                                     every color renders
                                                     negated                  */
    0x36, 1, 0,   HTFT_MADCTL_VALUE,              /* MADCTL — orientation +
                                                     color order; see the bit
                                                     table in HTFT_private.h  */
    0x3A, 1, 0,   HTFT_COLMOD_VALUE,              /* COLMOD — 16bpp RGB565
                                                     (0x05: ST7735 encoding!) */
    0xE0, 16, 0,  0x02, 0x1C, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2D,
                  0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10,
                                                  /* positive gamma curve —
                                                     grayscale response       */
    0xE1, 16, 0,  0x03, 0x1D, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D,
                  0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x07, 0x07, 0x08,
                                                  /* negative gamma curve —
                                                     must mirror E0           */
    0x13, 0, 10,                                  /* NORON — normal display
                                                     mode (vs partial mode)   */
    0x29, 0, 100,                                 /* DISPON — turn the panel
                                                     on; settle before use    */
    0x00, 0, 0                                    /* terminator — NOP         */
};

/* ------------------------------------------------------------------ *
 *  PUBLIC FUNCTIONS                                                  *
 * ------------------------------------------------------------------ */

void HTFT_voidInit(void)
{
    /* ---- 1. Control pins as outputs with correct idle levels ----
     * CS high (panel deselected), RST high (panel released), DC low.
     * Levels are set BEFORE anything else so the panel never sees a
     * selected-but-undriven bus during configuration. CS/DC run at
     * HIGH speed: they toggle between consecutive pixel bytes during
     * fills and must not limit the SPI clock's edges. RST idles.     */
    (void)MGPIO_SetPinMode(HTFT_CS_PORT, HTFT_CS_PIN, MGPIO_MODE_OUTPUT);
    (void)MGPIO_SetPinOutputConfig(HTFT_CS_PORT, HTFT_CS_PIN,
                                   MGPIO_OUTPUT_PUSH_PULL, MGPIO_SPEED_HIGH);
    (void)MGPIO_SetPinValue(HTFT_CS_PORT, HTFT_CS_PIN, MGPIO_PIN_HIGH);

    (void)MGPIO_SetPinMode(HTFT_RST_PORT, HTFT_RST_PIN, MGPIO_MODE_OUTPUT);
    (void)MGPIO_SetPinOutputConfig(HTFT_RST_PORT, HTFT_RST_PIN,
                                   MGPIO_OUTPUT_PUSH_PULL, MGPIO_SPEED_LOW);
    (void)MGPIO_SetPinValue(HTFT_RST_PORT, HTFT_RST_PIN, MGPIO_PIN_HIGH);

    (void)MGPIO_SetPinMode(HTFT_DC_PORT, HTFT_DC_PIN, MGPIO_MODE_OUTPUT);
    (void)MGPIO_SetPinOutputConfig(HTFT_DC_PORT, HTFT_DC_PIN,
                                   MGPIO_OUTPUT_PUSH_PULL, MGPIO_SPEED_HIGH);
    (void)MGPIO_SetPinValue(HTFT_DC_PORT, HTFT_DC_PIN, MGPIO_PIN_LOW);

    /* ---- 2. Hardware reset pulse ----
     * RST is held high first so the pulse starts from a known state.
     * Datasheet needs only 10 us low; we hold 10 ms. The 120 ms settle
     * after release is the real requirement — during it the panel
     * reloads its own defaults and IGNORES commands. Skipping these
     * delays is the #1 cause of "backlight on, screen dead".        */
    (void)MGPIO_SetPinValue(HTFT_RST_PORT, HTFT_RST_PIN, MGPIO_PIN_HIGH);
    HTFT_voidDelayMs(HTFT_RST_HIGH_MS);
    (void)MGPIO_SetPinValue(HTFT_RST_PORT, HTFT_RST_PIN, MGPIO_PIN_LOW);
    HTFT_voidDelayMs(HTFT_RST_LOW_MS);
    (void)MGPIO_SetPinValue(HTFT_RST_PORT, HTFT_RST_PIN, MGPIO_PIN_HIGH);
    HTFT_voidDelayMs(HTFT_RST_SETTLE_MS);

    /* ---- 3. Register init sequence (table-driven) ---- */
    HTFT_voidRunInitSequence();
}

void HTFT_voidFillRect(u16 Copy_u16X, u16 Copy_u16Y,
                       u16 Copy_u16Width, u16 Copy_u16Height,
                       u16 Copy_u16Color)
{
    u32 Local_u32Pixels;

    if ((Copy_u16Width == 0) || (Copy_u16Height == 0))
    {
        return;   /* degenerate rectangle — nothing to stream */
    }

#if HTFT_ENABLE_BOUNDS_CHECK
    /* Off-screen draws WRAP inside GRAM and corrupt distant screen
     * regions. Normally disabled (the caller owns correctness); flip
     * it in config while hunting a wrap bug. u32 math so X+W cannot
     * overflow before the comparison.                                */
    if (((u32)Copy_u16X + (u32)Copy_u16Width  > (u32)HTFT_SCREEN_WIDTH) ||
        ((u32)Copy_u16Y + (u32)Copy_u16Height > (u32)HTFT_SCREEN_HEIGHT))
    {
        return;
    }
#endif

    /* Program the address window, then open RAMWR. From here the
     * panel auto-increments its GRAM write pointer through the window
     * and expects nothing but pixel data.                            */
    HTFT_voidSetWindow(Copy_u16X, Copy_u16Y,
                       (u16)(Copy_u16X + Copy_u16Width - 1u),
                       (u16)(Copy_u16Y + Copy_u16Height - 1u));

    /* u32 on purpose: width*height can exceed u16 for legal caller
     * values even though this panel's own max (20480) does not.     */
    Local_u32Pixels = (u32)Copy_u16Width * (u32)Copy_u16Height;

    /* Extended transaction: CS stays LOW and DC stays HIGH for the
     * ENTIRE stream — one transaction for all pixels, not one per
     * byte. This is the single biggest performance decision in the
     * driver: raising CS per byte would ~5x every fill.             */
    (void)MGPIO_SetPinValue(HTFT_CS_PORT, HTFT_CS_PIN, MGPIO_PIN_LOW);
    (void)MGPIO_SetPinValue(HTFT_DC_PORT, HTFT_DC_PIN, MGPIO_PIN_HIGH);

    while (Local_u32Pixels--)
    {
        /* 16-bit pixels go out BIG-ENDIAN: high byte first. The
         * ST7735's serial interface assembles bytes MSB-first into
         * its 16-bit datapath; a low-byte-first bug mirrors every
         * color (red 0xF800 becomes cyan 0x001F-adjacent garbage).  */
        (void)MSPI_u8SendReceiveByteBlocking(HTFT_SPI_INSTANCE, (u8)(Copy_u16Color >> 8));
        (void)MSPI_u8SendReceiveByteBlocking(HTFT_SPI_INSTANCE, (u8)(Copy_u16Color & 0xFFu));
    }

    (void)MGPIO_SetPinValue(HTFT_CS_PORT, HTFT_CS_PIN, MGPIO_PIN_HIGH);
}

void HTFT_voidFillScreen(u16 Copy_u16Color)
{
    HTFT_voidFillRect(0, 0, HTFT_SCREEN_WIDTH, HTFT_SCREEN_HEIGHT, Copy_u16Color);
}

/* ------------------------------------------------------------------ *
 *  DRAWING PRIMITIVES                                                *
 *                                                                    *
 *  Design rule: pixels are cheap only when streamed contiguously    *
 *  (one window setup, then raw bytes). So:                          *
 *    - H/V lines  = 1-px-thick FillRects (fast paths)               *
 *    - rectangles = four H/V lines                                   *
 *    - diagonals  = the only true per-pixel path (Bresenham)        *
 * ------------------------------------------------------------------ */

void HTFT_voidDrawPixel(u16 Copy_u16X, u16 Copy_u16Y, u16 Copy_u16Color)
{
    /* A 1x1 fill pays the full window-setup cost for one pixel —
     * correct, uniform, and deliberately the only implementation.
     * No duplicate byte-streaming code anywhere in the driver.      */
    HTFT_voidFillRect(Copy_u16X, Copy_u16Y, 1u, 1u, Copy_u16Color);
}

void HTFT_voidDrawHLine(u16 Copy_u16X, u16 Copy_u16Y,
                        u16 Copy_u16Length, u16 Copy_u16Color)
{
    HTFT_voidFillRect(Copy_u16X, Copy_u16Y, Copy_u16Length, 1u, Copy_u16Color);
}

void HTFT_voidDrawVLine(u16 Copy_u16X, u16 Copy_u16Y,
                        u16 Copy_u16Length, u16 Copy_u16Color)
{
    HTFT_voidFillRect(Copy_u16X, Copy_u16Y, 1u, Copy_u16Length, Copy_u16Color);
}

void HTFT_voidDrawRect(u16 Copy_u16X, u16 Copy_u16Y,
                       u16 Copy_u16Width, u16 Copy_u16Height,
                       u16 Copy_u16Color)
{
    if ((Copy_u16Width == 0u) || (Copy_u16Height == 0u))
    {
        return;
    }
    HTFT_voidDrawHLine(Copy_u16X, Copy_u16Y, Copy_u16Width, Copy_u16Color);                             /* top    */
    HTFT_voidDrawHLine(Copy_u16X, (u16)(Copy_u16Y + Copy_u16Height - 1u), Copy_u16Width, Copy_u16Color); /* bottom */
    HTFT_voidDrawVLine(Copy_u16X, Copy_u16Y, Copy_u16Height, Copy_u16Color);                             /* left   */
    HTFT_voidDrawVLine((u16)(Copy_u16X + Copy_u16Width - 1u), Copy_u16Y, Copy_u16Height, Copy_u16Color); /* right  */
}

void HTFT_voidDrawLine(u16 Copy_u16X0, u16 Copy_u16Y0,
                       u16 Copy_u16X1, u16 Copy_u16Y1,
                       u16 Copy_u16Color)
{
    s32 Local_s32Dx, Local_s32Dy, Local_s32Sx, Local_s32Sy, Local_s32Err, Local_s32E2;
    u16 Local_u16X, Local_u16Y;

    /* Fast paths: axis-aligned lines never enter Bresenham — they
     * become single fill streams. (X0/X1 or Y0/Y1 equal -> horizontal
     * or vertical, regardless of endpoint order.)                   */
    if (Copy_u16Y0 == Copy_u16Y1)
    {
        Local_u16X = (Copy_u16X0 < Copy_u16X1) ? Copy_u16X0 : Copy_u16X1;
        HTFT_voidDrawHLine(Local_u16X, Copy_u16Y0,
                           (u16)(((Copy_u16X0 < Copy_u16X1) ? (Copy_u16X1 - Copy_u16X0)
                                                           : (Copy_u16X0 - Copy_u16X1)) + 1u),
                           Copy_u16Color);
        return;
    }
    if (Copy_u16X0 == Copy_u16X1)
    {
        Local_u16Y = (Copy_u16Y0 < Copy_u16Y1) ? Copy_u16Y0 : Copy_u16Y1;
        HTFT_voidDrawVLine(Copy_u16X0, Local_u16Y,
                           (u16)(((Copy_u16Y0 < Copy_u16Y1) ? (Copy_u16Y1 - Copy_u16Y0)
                                                           : (Copy_u16Y0 - Copy_u16Y1)) + 1u),
                           Copy_u16Color);
        return;
    }

    /* Bresenham integer line — works from any endpoint toward any
     * other (all four diagonal directions), no floating point, no
     * multiplication. s32 math because differences can be negative.
     *
     * The classic trick below: Dy is NEGATED after taking |Dx|,|Dy|,
     * and the error term starts at Dx + Dy. This formulation makes
     * one 'err' comparison drive BOTH axis steps in the standard
     * "all-octants" form — E2 >= Dy steps x, E2 <= Dx steps y (Dy
     * being negative, >= and <= compare against opposite signs).   */
    Local_s32Sx = (Copy_u16X1 > Copy_u16X0) ? 1 : -1;
    Local_s32Sy = (Copy_u16Y1 > Copy_u16Y0) ? 1 : -1;
    Local_s32Dx = (s32)Copy_u16X1 - (s32)Copy_u16X0;
    Local_s32Dy = (s32)Copy_u16Y1 - (s32)Copy_u16Y0;
    if (Local_s32Dx < 0) { Local_s32Dx = -Local_s32Dx; }
    if (Local_s32Dy < 0) { Local_s32Dy = -Local_s32Dy; }
    Local_s32Dy  = -Local_s32Dy;                /* the sign trick */
    Local_s32Err = Local_s32Dx + Local_s32Dy;

    Local_u16X = Copy_u16X0;
    Local_u16Y = Copy_u16Y0;
    while (1)
    {
        HTFT_voidDrawPixel(Local_u16X, Local_u16Y, Copy_u16Color);
        if ((Local_u16X == Copy_u16X1) && (Local_u16Y == Copy_u16Y1))
        {
            break;   /* endpoint reached — always terminates exactly */
        }
        Local_s32E2 = 2 * Local_s32Err;
        if (Local_s32E2 >= Local_s32Dy)
        {
            Local_s32Err += Local_s32Dy;
            Local_u16X = (u16)((s32)Local_u16X + Local_s32Sx);
        }
        if (Local_s32E2 <= Local_s32Dx)
        {
            Local_s32Err += Local_s32Dx;
            Local_u16Y = (u16)((s32)Local_u16Y + Local_s32Sy);
        }
    }
}

/* ------------------------------------------------------------------ *
 *  PRIVATE FUNCTIONS — transaction / window layers                  *
 *                                                                    *
 *  PROTOCOL PATTERN: every single-byte panel access is one           *
 *  transaction:                                                      *
 *      1. CS LOW     — panel listens                                 *
 *      2. DC level   — LOW: a command byte follows                   *
 *                      HIGH: a data byte follows                     *
 *      3. one byte   — blocking send (MSPI's round trip guarantees   *
 *                      the byte is fully clocked before we return)   *
 *      4. CS HIGH    — panel processes                               *
 *  Multi-byte streams (fills, glyphs) keep CS low and DC high across *
 *  the whole stream — see FillRect.                                  *
 * ------------------------------------------------------------------ */

static void HTFT_voidDelayMs(u32 Copy_u32Ms)
{
    /* Thin wrapper: keeps every ms delay in the driver routed through
     * the same call, so swapping in a different time source later is
     * a one-line change. Rev 3 note: returns MSYSTICK_NOT_INITIALIZED
     * and does NOT wait if Init never ran — the HTFT init contract
     * (interface header) requires MSYSTICK_Init() first, so a hang is
     * impossible and the status is safely discardable here.          */
    (void)MSYSTICK_SetBusyWait_ms(Copy_u32Ms);
}

static void HTFT_voidWriteCommand(u8 Copy_u8Command)
{
    (void)MGPIO_SetPinValue(HTFT_CS_PORT, HTFT_CS_PIN, MGPIO_PIN_LOW);   /* select   */
    (void)MGPIO_SetPinValue(HTFT_DC_PORT, HTFT_DC_PIN, MGPIO_PIN_LOW);   /* command  */
    (void)MSPI_u8SendReceiveByteBlocking(HTFT_SPI_INSTANCE, Copy_u8Command);
    (void)MGPIO_SetPinValue(HTFT_CS_PORT, HTFT_CS_PIN, MGPIO_PIN_HIGH);  /* deselect */
}

static void HTFT_voidWriteData(u8 Copy_u8Data)
{
    (void)MGPIO_SetPinValue(HTFT_CS_PORT, HTFT_CS_PIN, MGPIO_PIN_LOW);
    (void)MGPIO_SetPinValue(HTFT_DC_PORT, HTFT_DC_PIN, MGPIO_PIN_HIGH);  /* data     */
    (void)MSPI_u8SendReceiveByteBlocking(HTFT_SPI_INSTANCE, Copy_u8Data);
    (void)MGPIO_SetPinValue(HTFT_CS_PORT, HTFT_CS_PIN, MGPIO_PIN_HIGH);
}

static void HTFT_voidWriteData16(u16 Copy_u16Data)
{
    /* 16-bit values (window coordinates) go big-endian, like pixels. */
    (void)MGPIO_SetPinValue(HTFT_CS_PORT, HTFT_CS_PIN, MGPIO_PIN_LOW);
    (void)MGPIO_SetPinValue(HTFT_DC_PORT, HTFT_DC_PIN, MGPIO_PIN_HIGH);
    (void)MSPI_u8SendReceiveByteBlocking(HTFT_SPI_INSTANCE, (u8)(Copy_u16Data >> 8));
    (void)MSPI_u8SendReceiveByteBlocking(HTFT_SPI_INSTANCE, (u8)(Copy_u16Data & 0xFFu));
    (void)MGPIO_SetPinValue(HTFT_CS_PORT, HTFT_CS_PIN, MGPIO_PIN_HIGH);
}

/* Stream an RGB565 array into a previously-programmed window.
 * Caller must have just called SetWindow — this function only opens
 * the extended transaction (CS low, DC high) and pushes bytes.      */
static void HTFT_voidWritePixels(const u16 *Copy_pu16Pixels, u32 Copy_u32Count)
{
    (void)MGPIO_SetPinValue(HTFT_CS_PORT, HTFT_CS_PIN, MGPIO_PIN_LOW);
    (void)MGPIO_SetPinValue(HTFT_DC_PORT, HTFT_DC_PIN, MGPIO_PIN_HIGH);

    while (Copy_u32Count--)
    {
        (void)MSPI_u8SendReceiveByteBlocking(HTFT_SPI_INSTANCE, (u8)(*Copy_pu16Pixels >> 8));
        (void)MSPI_u8SendReceiveByteBlocking(HTFT_SPI_INSTANCE, (u8)(*Copy_pu16Pixels & 0xFFu));
        Copy_pu16Pixels++;
    }

    (void)MGPIO_SetPinValue(HTFT_CS_PORT, HTFT_CS_PIN, MGPIO_PIN_HIGH);
}

static void HTFT_voidSetWindow(u16 Copy_u16X0, u16 Copy_u16Y0,
                               u16 Copy_u16X1, u16 Copy_u16Y1)
{
    /* Two facts baked into every drawing call:
     *
     *  1. ENDPOINTS ARE INCLUSIVE. FillRect compensates by computing
     *     x1 = x + w - 1; a fill with x1 = x + w would be one pixel
     *     wider than asked (and wrap past the screen edge).
     *
     *  2. GRAM OFFSETS (green-tab panels): the visible area starts a
     *     couple of pixels inside GRAM. Offsets are added HERE so all
     *     drawing code above stays in clean 0..127 / 0..159 screen
     *     coordinates. This panel (black tab) uses (0,0) — the code
     *     path is still exercised and harmless.                     */
    Copy_u16X0 = (u16)(Copy_u16X0 + HTFT_COL_OFFSET);
    Copy_u16X1 = (u16)(Copy_u16X1 + HTFT_COL_OFFSET);
    Copy_u16Y0 = (u16)(Copy_u16Y0 + HTFT_ROW_OFFSET);
    Copy_u16Y1 = (u16)(Copy_u16Y1 + HTFT_ROW_OFFSET);

    /* CASET: column (X) window, two inclusive 16-bit endpoints.     */
    HTFT_voidWriteCommand(HTFT_CMD_CASET);
    HTFT_voidWriteData16(Copy_u16X0);
    HTFT_voidWriteData16(Copy_u16X1);

    /* RASET: row (Y) window, same encoding.                          */
    HTFT_voidWriteCommand(HTFT_CMD_RASET);
    HTFT_voidWriteData16(Copy_u16Y0);
    HTFT_voidWriteData16(Copy_u16Y1);

    /* RAMWR opens the window: from now until CS rises, every data
     * byte is a pixel, auto-sequenced by the panel through the
     * window (left->right, top->bottom, wrapping row to row).       */
    HTFT_voidWriteCommand(HTFT_CMD_RAMWR);
}

static void HTFT_voidRunInitSequence(void)
{
    const u8 *Local_pu8Byte = HTFT_AU8InitSequence;

    /* Walk the table: command, count, delay, then that many data
     * bytes. The lone 0x00 terminator is a legal NOP, so the loop
     * can never walk off the table even if a count were wrong.     */
    while (*Local_pu8Byte != 0x00u)
    {
        u8 Local_u8Command   = *Local_pu8Byte++;
        u8 Local_u8DataCount = *Local_pu8Byte++;
        u8 Local_u8DelayMs   = *Local_pu8Byte++;

        HTFT_voidWriteCommand(Local_u8Command);
        while (Local_u8DataCount--)
        {
            HTFT_voidWriteData(*Local_pu8Byte++);
        }
        if (Local_u8DelayMs != 0u)
        {
            HTFT_voidDelayMs(Local_u8DelayMs);
        }
    }
}

/* ------------------------------------------------------------------ *
 *  TEXT RENDERING                                                    *
 *                                                                    *
 *  One character = one 8x8 window + one 64-pixel stream. The glyph   *
 *  is expanded from 1-bpp to RGB565 in a scratch buffer FIRST —      *
 *  the alternative (a window setup per pixel) would be ~64x slower.  *
 *                                                                    *
 *  Cells are OPAQUE (background drawn too) — which makes redrawing   *
 *  a cell the erase mechanism: scores, timers, and cursors update    *
 *  by overwriting exactly their own cells. This is the partial-      *
 *  redraw discipline the whole no-framebuffer design rests on.       *
 *                                                                    *
 *  This 128-byte buffer is the driver's entire RAM footprint.        *
 * ------------------------------------------------------------------ */
static u16 HTFT_AU16GlyphBuffer[64];

void HTFT_voidDrawChar(u16 Copy_u16X, u16 Copy_u16Y, u8 Copy_u8Char,
                       u16 Copy_u16Fg, u16 Copy_u16Bg)
{
    const u8 *Local_pu8Glyph;
    u8 Local_u8Row, Local_u8Col;

    /* Range gate BEFORE indexing the font table — chars outside
     * 0x20..0x5F (notably lowercase) render as blanks instead of
     * reading out of bounds. Widen this check when the table grows. */
    if ((Copy_u8Char < 0x20u) || (Copy_u8Char > 0x5Fu))
    {
        Copy_u8Char = (u8)' ';
    }
    Local_pu8Glyph = HTFT_AU8Font[Copy_u8Char - 0x20u];

    /* Expand: each set bit -> Fg, each clear bit -> Bg. Bit 7 of the
     * glyph byte is the LEFTMOST pixel (see HTFT_font.h encoding) —
     * the 0x80 >> col mask walks bits left to right. Including the
     * background bits is what makes cells opaque/erasable.          */
    for (Local_u8Row = 0; Local_u8Row < 8u; Local_u8Row++)
    {
        for (Local_u8Col = 0; Local_u8Col < 8u; Local_u8Col++)
        {
            HTFT_AU16GlyphBuffer[(u16)(Local_u8Row * 8u) + Local_u8Col] =
                ((Local_pu8Glyph[Local_u8Row] & (u8)(0x80u >> Local_u8Col)) != 0u)
                    ? Copy_u16Fg : Copy_u16Bg;
        }
    }

    /* One window, one stream — the whole character in one shot.     */
    HTFT_voidSetWindow(Copy_u16X, Copy_u16Y,
                       (u16)(Copy_u16X + 7u), (u16)(Copy_u16Y + 7u));
    HTFT_voidWritePixels(HTFT_AU16GlyphBuffer, 64u);
}

void HTFT_voidDrawString(u16 Copy_u16X, u16 Copy_u16Y, const char *Copy_chString,
                         u16 Copy_u16Fg, u16 Copy_u16Bg)
{
    /* Fixed 8-px advance — including for the 6-px glyphs — because
     * the 2-px gap is baked into the cell. Fixed pitch also means a
     * character's screen position never depends on what came before
     * it: the timer-overwrite trick (redraw the same string in the
     * same place) stays pixel-exact.                                 */
    while (*Copy_chString != '\0')
    {
        HTFT_voidDrawChar(Copy_u16X, Copy_u16Y, (u8)*Copy_chString, Copy_u16Fg, Copy_u16Bg);
        Copy_u16X = (u16)(Copy_u16X + 8u);
        Copy_chString++;
    }
}

/* ------------------------------------------------------------------ *
 *  GRAPHICS EXTENSIONS                                               *
 *                                                                    *
 *  Correctness rule for this section: every function uses ONLY the   *
 *  transaction shapes already proven on hardware —                  *
 *    DrawChar2x / DrawImage : one window + ONE pixel stream          *
 *    DrawSprite             : HLine fast paths only                  *
 *    FillCircle             : HLine fast paths only                  *
 *    DrawCircle             : DrawPixel only                         *
 *  (The 2x buffer exists specifically to keep the one-stream rule:   *
 *  streaming row-by-row across CS toggles would depend on the GRAM   *
 *  write pointer persisting between transactions — undocumented      *
 *  controller behavior we have never needed to rely on.)             *
 * ------------------------------------------------------------------ */

/* 16x16 RGB565 expansion of one glyph — 512 bytes. Driver RAM total
 * is now 640 bytes (128-byte 1x buffer + this). Still no framebuffer. */
static u16 HTFT_AU16Glyph2xBuffer[256];

void HTFT_voidDrawChar2x(u16 Copy_u16X, u16 Copy_u16Y, u8 Copy_u8Char,
                         u16 Copy_u16Fg, u16 Copy_u16Bg)
{
    const u8 *Local_pu8Glyph;
    u16 Local_u16Row, Local_u16Rep, Local_u16Col;

    if ((Copy_u8Char < 0x20u) || (Copy_u8Char > 0x5Fu))
    {
        Copy_u8Char = (u8)' ';
    }
    Local_pu8Glyph = HTFT_AU8Font[Copy_u8Char - 0x20u];

    /* Expand 8x8 glyph -> 16x16: every bit becomes a 2x2 block.
     * Row index = (2*row + rep), column index = (2*col + dup).
     * The zero bits 1..0 of each byte scale into the 4-px right gap,
     * and the zero row 7 into the 2-px bottom gap — spacing scales. */
    for (Local_u16Row = 0u; Local_u16Row < 8u; Local_u16Row++)
    {
        for (Local_u16Rep = 0u; Local_u16Rep < 2u; Local_u16Rep++)
        {
            u16 Local_u16BufferRow = (2u * Local_u16Row) + Local_u16Rep;
            for (Local_u16Col = 0u; Local_u16Col < 8u; Local_u16Col++)
            {
                u16 Local_u16Pixel =
                    ((Local_pu8Glyph[Local_u16Row] & (u8)(0x80u >> Local_u16Col)) != 0u)
                        ? Copy_u16Fg : Copy_u16Bg;
                HTFT_AU16Glyph2xBuffer[(16u * Local_u16BufferRow) + (2u * Local_u16Col)]       = Local_u16Pixel;
                HTFT_AU16Glyph2xBuffer[(16u * Local_u16BufferRow) + (2u * Local_u16Col) + 1u] = Local_u16Pixel;
            }
        }
    }

    /* Same shape as DrawChar: expand, then one window + one stream.  */
    HTFT_voidSetWindow(Copy_u16X, Copy_u16Y,
                       (u16)(Copy_u16X + 15u), (u16)(Copy_u16Y + 15u));
    HTFT_voidWritePixels(HTFT_AU16Glyph2xBuffer, 256u);
}

void HTFT_voidDrawString2x(u16 Copy_u16X, u16 Copy_u16Y, const char *Copy_chString,
                           u16 Copy_u16Fg, u16 Copy_u16Bg)
{
    while (*Copy_chString != '\0')
    {
        HTFT_voidDrawChar2x(Copy_u16X, Copy_u16Y, (u8)*Copy_chString, Copy_u16Fg, Copy_u16Bg);
        Copy_u16X = (u16)(Copy_u16X + 16u);
        Copy_chString++;
    }
}

void HTFT_voidDrawImage(u16 Copy_u16X, u16 Copy_u16Y,
                        u16 Copy_u16Width, u16 Copy_u16Height,
                        const u16 *Copy_pu16Pixels)
{
    if ((Copy_u16Width == 0u) || (Copy_u16Height == 0u))
    {
        return;
    }

    /* Exactly FillRect's proven shape — the source is an array
     * instead of a repeated constant. Row-major pixel order matches
     * the window's left->right, top->bottom auto-advance.           */
    HTFT_voidSetWindow(Copy_u16X, Copy_u16Y,
                       (u16)(Copy_u16X + Copy_u16Width - 1u),
                       (u16)(Copy_u16Y + Copy_u16Height - 1u));
    HTFT_voidWritePixels(Copy_pu16Pixels, (u32)Copy_u16Width * (u32)Copy_u16Height);
}

void HTFT_voidDrawSprite(u16 Copy_u16X, u16 Copy_u16Y,
                         u16 Copy_u16Width, u16 Copy_u16Height,
                         const u8 *Copy_pu8Bitmap, u16 Copy_u16Color)
{
    u16 Local_u16BytesPerRow = (u16)((Copy_u16Width + 7u) / 8u);
    u16 Local_u16Row, Local_u16Col;

    /* Transparent compositing WITHOUT readback: only set bits are
     * written, so whatever is already on screen shows through the
     * holes. Each run of consecutive set bits in a row becomes ONE
     * HLine — a typical icon is a handful of streams per row.       */
    for (Local_u16Row = 0u; Local_u16Row < Copy_u16Height; Local_u16Row++)
    {
        const u8 *Local_pu8Row =
            Copy_pu8Bitmap + ((u32)Local_u16Row * Local_u16BytesPerRow);

        Local_u16Col = 0u;
        while (Local_u16Col < Copy_u16Width)
        {
            if ((Local_pu8Row[Local_u16Col >> 3] & (u8)(0x80u >> (Local_u16Col & 7u))) != 0u)
            {
                /* run start found — extend to the last set bit       */
                u16 Local_u16RunStart = Local_u16Col;
                while ((Local_u16Col < Copy_u16Width) &&
                       ((Local_pu8Row[Local_u16Col >> 3] & (u8)(0x80u >> (Local_u16Col & 7u))) != 0u))
                {
                    Local_u16Col++;
                }
                HTFT_voidDrawHLine((u16)(Copy_u16X + Local_u16RunStart),
                                   (u16)(Copy_u16Y + Local_u16Row),
                                   (u16)(Local_u16Col - Local_u16RunStart),
                                   Copy_u16Color);
            }
            else
            {
                Local_u16Col++;   /* transparent pixel — skip, do not write */
            }
        }
    }
}

/* Integer floor square root — avoids float anywhere in the driver.
 * Only ever fed values <= ~6400 (radius squared), far inside s32.   */
static s32 HTFT_s32ISqrt(s32 Copy_s32Value)
{
    s32 Local_s32Result = 0;
    s32 Local_s32Bit = 1 << 30;                 /* highest power of 4 */

    while (Local_s32Bit > Copy_s32Value) { Local_s32Bit >>= 2; }
    while (Local_s32Bit != 0)
    {
        if (Copy_s32Value >= Local_s32Result + Local_s32Bit)
        {
            Copy_s32Value  -= Local_s32Result + Local_s32Bit;
            Local_s32Result = (Local_s32Result >> 1) + Local_s32Bit;
        }
        else
        {
            Local_s32Result >>= 1;
        }
        Local_s32Bit >>= 2;
    }
    return Local_s32Result;
}

void HTFT_voidFillCircle(u16 Copy_u16X0, u16 Copy_u16Y0,
                         u16 Copy_u16Radius, u16 Copy_u16Color)
{
    s32 Local_s32Dy;

    /* Each screen row within the circle is one horizontal span:
     * half-width = sqrt(r^2 - dy^2). One HLine per row -> all fast
     * paths. Small radii come out slightly "diamond" at the extreme
     * rows — that is normal discrete rasterization, not a bug.      */
    for (Local_s32Dy = -(s32)Copy_u16Radius; Local_s32Dy <= (s32)Copy_u16Radius; Local_s32Dy++)
    {
        s32 Local_s32Dx = HTFT_s32ISqrt(((s32)Copy_u16Radius * (s32)Copy_u16Radius)
                                        - (Local_s32Dy * Local_s32Dy));
        HTFT_voidDrawHLine((u16)((s32)Copy_u16X0 - Local_s32Dx),
                           (u16)((s32)Copy_u16Y0 + Local_s32Dy),
                           (u16)((2 * Local_s32Dx) + 1),
                           Copy_u16Color);
    }
}

void HTFT_voidDrawCircle(u16 Copy_u16X0, u16 Copy_u16Y0,
                         u16 Copy_u16Radius, u16 Copy_u16Color)
{
    s32 Local_s32X = (s32)Copy_u16Radius;
    s32 Local_s32Y = 0;
    s32 Local_s32Error = 1 - Local_s32X;

    /* Midpoint circle: walks one octant, mirrors to all eight via
     * symmetry — no floating point, no multiplication. Every plotted
     * point pays a full window setup (it IS DrawPixel), which is why
     * this is the driver's slowest primitive: fine for decoration
     * drawn once, wrong for anything animated.                      */
    while (Local_s32X >= Local_s32Y)
    {
        HTFT_voidDrawPixel((u16)(Copy_u16X0 + Local_s32X), (u16)(Copy_u16Y0 + Local_s32Y), Copy_u16Color);
        HTFT_voidDrawPixel((u16)(Copy_u16X0 + Local_s32Y), (u16)(Copy_u16Y0 + Local_s32X), Copy_u16Color);
        HTFT_voidDrawPixel((u16)(Copy_u16X0 - Local_s32Y), (u16)(Copy_u16Y0 + Local_s32X), Copy_u16Color);
        HTFT_voidDrawPixel((u16)(Copy_u16X0 - Local_s32X), (u16)(Copy_u16Y0 + Local_s32Y), Copy_u16Color);
        HTFT_voidDrawPixel((u16)(Copy_u16X0 - Local_s32X), (u16)(Copy_u16Y0 - Local_s32Y), Copy_u16Color);
        HTFT_voidDrawPixel((u16)(Copy_u16X0 - Local_s32Y), (u16)(Copy_u16Y0 - Local_s32X), Copy_u16Color);
        HTFT_voidDrawPixel((u16)(Copy_u16X0 + Local_s32Y), (u16)(Copy_u16Y0 - Local_s32X), Copy_u16Color);
        HTFT_voidDrawPixel((u16)(Copy_u16X0 + Local_s32X), (u16)(Copy_u16Y0 - Local_s32Y), Copy_u16Color);

        Local_s32Y++;
        if (Local_s32Error < 0)
        {
            Local_s32Error += (2 * Local_s32Y) + 1;
        }
        else
        {
            Local_s32X--;
            Local_s32Error += (2 * (Local_s32Y - Local_s32X)) + 1;
        }
    }
}
