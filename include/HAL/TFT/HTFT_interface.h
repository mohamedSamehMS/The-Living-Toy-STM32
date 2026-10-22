/*
 * HTFT_interface.h
 *
 *  PUBLIC API of the TFT HAL driver — the ONLY HTFT header the
 *  application layer may include. Everything hardware-specific
 *  (pins, panel variant) lives in HTFT_config.h; everything internal
 *  (commands, register values) lives in HTFT_private.h.
 *
 *  Display: 1.8" ST7735, 128x160 RGB565, SPI write-only (no MISO to
 *  the panel — the MCU can never read pixels back, which is why GRAM
 *  is treated as write-only throughout this driver's design).
 *
 *  ARCHITECTURE — how drawing works in this project (read this once
 *  before writing any screen code):
 *    - There is NO framebuffer (128*160*2 = 40 KB would consume most
 *      of the F401CC's 64 KB SRAM). Every draw call writes directly
 *      into the panel's GRAM over SPI.
 *    - Drawing cost = one address-window setup + a byte stream. Fills
 *      and lines are cheap; diagonal per-pixel draws are expensive.
 *    - Opaque text cells are the ERASE mechanism: redrawing a
 *      character cell with its own background color cleanly wipes the
 *      previous content. Live scores/timers/cursors update by
 *      overwriting exactly their own cells and touching nothing else.
 *    - Every program starts with FillScreen, and every screen-state
 *      transition (menu -> game, game -> leaderboard) repaints the
 *      new screen fully. GRAM content is undefined at power-up and
 *      untrustworthy across MCU resets (the panel stays powered while
 *      the MCU reboots — never build on what is already there).
 *
 *  INIT CONTRACT — the application MUST have done ALL of the following
 *  BEFORE calling HTFT_voidInit():
 *    1. MRCC_Init() + peripheral clocks: GPIOA (SPI bus pins),
 *       GPIOB (CS/RST/DC), SPI1.
 *    2. MSYSTICK_voidInit() AND the ms tick interrupt enabled
 *       (call MSYSTICK_voidInterruptEnable() — required until the
 *       MSYSTICK driver sets TICKINT inside its own Init).
 *       All HTFT delays hang silently without a ticking ms counter.
 *    3. SPI bus pins as AF5 (PA5 SCK, PA7 MOSI, PA6 reserved for the
 *       Phase-2 SD card) + MSPI_voidInit(MSPI1) — mode 0, 8-bit,
 *       MSB first, per the MSPI1 static config.
 *  HTFT then takes ownership of its own CS/RST/DC pins.
 */

#ifndef HTFT_INTERFACE_H_
#define HTFT_INTERFACE_H_

#include "STD_TYPES.h"

/* =====================================================================
 *  COLOR CONSTANTS — RGB565, 16 bits/pixel
 *
 *  Layout:  RRRRR GGGGGG BBBBB  (5 red, 6 green, 5 blue bits).
 *  MADCTL = 0xC0 selects RGB order, so 0xF800 really is red on this
 *  panel (verified by the solid-color test).
 * ===================================================================== */
#define HTFT_COLOR_BLACK        ((u16)0x0000)
#define HTFT_COLOR_WHITE        ((u16)0xFFFF)
#define HTFT_COLOR_RED          ((u16)0xF800)
#define HTFT_COLOR_GREEN        ((u16)0x07E0)
#define HTFT_COLOR_BLUE         ((u16)0x001F)
#define HTFT_COLOR_YELLOW       ((u16)0xFFE0)
#define HTFT_COLOR_CYAN         ((u16)0x07FF)
#define HTFT_COLOR_MAGENTA      ((u16)0xF81F)
#define HTFT_COLOR_DARKGRAY     ((u16)0x7BEF)   /* 50% white — grids, rules   */
#define HTFT_COLOR_LIGHTGRAY    ((u16)0xBDF7)   /* 75% white — hint text      */

/** Build an RGB565 color from three 8-bit components (each 0..255).
 *  Masks truncate to the top 5/6/5 bits — the low bits are dropped,
 *  not rounded (imperceptible on this panel).                      */
#define HTFT_RGB565(r, g, b)    ((u16)((((u16)(r) & 0xF8) << 8) | \
                                       (((u16)(g) & 0xFC) << 3) | \
                                        ((u16)(b) >> 3)))

/* =====================================================================
 *  PANEL GEOMETRY — public facts of the display contract
 *
 *  Deliberately in the INTERFACE, not the config: application layout
 *  code (menus, game fields) legitimately needs these to position
 *  elements; they are facts of the physical module, not a user-tunable
 *  wiring choice. Change only when the physical module changes — and
 *  then the init sequence in HTFT_program.c changes with it.
 * ===================================================================== */
#define HTFT_SCREEN_WIDTH       ((u16)128)
#define HTFT_SCREEN_HEIGHT      ((u16)160)

/* =====================================================================
 *  API
 * ===================================================================== */

/**
 * @brief  Initialize the display: take ownership of CS/RST/DC pins,
 *         hardware reset pulse, full ST7735 register init (power,
 *         gamma, orientation, pixel format, sleep-out, display-on).
 *         Leaves the panel ON, portrait, RGB565, (0,0) at top-left.
 * @note   The init contract at the top of this file must be satisfied
 *         first. Does NOT clear the screen — always follow with
 *         HTFT_voidFillScreen() to establish a known canvas.
 */
void HTFT_voidInit(void);

/**
 * @brief  Fill an axis-aligned rectangle with a solid color.
 * @param  Copy_u16X, Copy_u16Y          top-left corner, (0,0) = screen
 *                                       top-left, x rightward, y downward
 * @param  Copy_u16Width, Copy_u16Height size in pixels (both must be > 0)
 * @param  Copy_u16Color                 RGB565 color constant
 * @note   Caller must keep X+Width <= 128 and Y+Height <= 160 (bounds
 *         are only enforced when HTFT_ENABLE_BOUNDS_CHECK = 1 in the
 *         config — see that file for why it defaults to 0).
 *         THE primitive: every other drawing call in this driver
 *         ultimately reduces to FillRect streams.
 */
void HTFT_voidFillRect(u16 Copy_u16X, u16 Copy_u16Y,
                       u16 Copy_u16Width, u16 Copy_u16Height,
                       u16 Copy_u16Color);

/** @brief Fill the entire screen with one color. ~20480 pixels: about
 *         26 ms of pure SPI streaming at 8 MHz. */
void HTFT_voidFillScreen(u16 Copy_u16Color);

/**
 * @brief  Set one pixel. Implemented as a 1x1 FillRect — one full
 *         address-window setup per call, so per-pixel drawing is
 *         ~5x the cost of streamed pixels. Use lines/rects wherever
 *         pixels are contiguous; reserve DrawPixel for sparse dots
 *         (it exists mainly as the Bresenham stepping primitive).
 */
void HTFT_voidDrawPixel(u16 Copy_u16X, u16 Copy_u16Y, u16 Copy_u16Color);

/** @brief Horizontal line of Copy_u16Length px starting at (X, Y).
 *         Fast path: one window + one stream. */
void HTFT_voidDrawHLine(u16 Copy_u16X, u16 Copy_u16Y,
                        u16 Copy_u16Length, u16 Copy_u16Color);

/** @brief Vertical line of Copy_u16Length px starting at (X, Y).
 *         Fast path: one window + one stream. */
void HTFT_voidDrawVLine(u16 Copy_u16X, u16 Copy_u16Y,
                        u16 Copy_u16Length, u16 Copy_u16Color);

/** @brief 1-px outline rectangle at (X, Y), size (W, H) — four
 *         line draws (top/bottom/left/right), each a fast path. */
void HTFT_voidDrawRect(u16 Copy_u16X, u16 Copy_u16Y,
                       u16 Copy_u16Width, u16 Copy_u16Height,
                       u16 Copy_u16Color);

/** @brief Arbitrary straight line (Bresenham's algorithm) in any of
 *         the 8 directions. Horizontal/vertical calls are detected
 *         and routed to the fast fill paths automatically; only true
 *         diagonals pay per-pixel window setup. */
void HTFT_voidDrawLine(u16 Copy_u16X0, u16 Copy_u16Y0,
                       u16 Copy_u16X1, u16 Copy_u16Y1,
                       u16 Copy_u16Color);

/**
 * @brief  Draw one character in an opaque 8x8 cell at (X, Y).
 * @param  Copy_u8Char  supported range 0x20..0x5F (space..underscore:
 *                      uppercase, digits, common punctuation); anything
 *                      outside renders as a blank cell — including
 *                      lowercase until the font table is extended.
 * @param  Copy_u16Fg / Copy_u16Bg  glyph color / cell background.
 * @note   OPAQUE is the point: redrawing the same cell with new text
 *         (or with spaces) cleanly erases the old content — this is
 *         the partial-redraw primitive for live scores, timers, and
 *         cursors. Keep X <= 120 and Y <= 152 (no clipping).
 */
void HTFT_voidDrawChar(u16 Copy_u16X, u16 Copy_u16Y, u8 Copy_u8Char,
                       u16 Copy_u16Fg, u16 Copy_u16Bg);

/**
 * @brief  Draw a NUL-terminated string, 8 px per character, advancing
 *         rightward from (X, Y).
 * @note   The screen is a 16x20 character grid (128/8 x 160/8).
 *         No clipping and no wrapping — keep strings within one line.
 *         For right-aligned or centered text, compute X from
 *         (8 * strlen) and HTFT_SCREEN_WIDTH in the caller.
 */
void HTFT_voidDrawString(u16 Copy_u16X, u16 Copy_u16Y, const char *Copy_chString,
                         u16 Copy_u16Fg, u16 Copy_u16Bg);


/* =====================================================================
 *  SCALED TEXT (2x)
 *
 *  16x16 cells -> the screen becomes an 8x10 character grid. Same
 *  opaque-cell erase property as 1x text: redrawing a 2x cell cleanly
 *  wipes it. First consumer: game boot / game-over title screens.
 * ===================================================================== */

/**
 * @brief  Draw one character in an opaque 16x16 cell at (X, Y) — every
 *         font pixel rendered as a 2x2 block (chunky "retro title" look).
 * @note   Keep X <= 112 and Y <= 144. Supported chars: 0x20..0x5F.
 */
void HTFT_voidDrawChar2x(u16 Copy_u16X, u16 Copy_u16Y, u8 Copy_u8Char,
                         u16 Copy_u16Fg, u16 Copy_u16Bg);

/** @brief Draw a string in 16x16 cells, 16 px per character (8 chars
 *         per line, 10 lines on screen). */
void HTFT_voidDrawString2x(u16 Copy_u16X, u16 Copy_u16Y, const char *Copy_chString,
                           u16 Copy_u16Fg, u16 Copy_u16Bg);

/* =====================================================================
 *  IMAGES AND SPRITES
 * ===================================================================== */

/**
 * @brief  Blit a raw RGB565 image: row-major, top-left origin, exactly
 *         Copy_u16Width * Copy_u16Height pixels.
 * @param  Copy_pu16Pixels  const array (flash-resident), e.g. assets
 *         loaded to a const table — a full 128x160 splash is 40 KB of
 *         flash, which fits the F401CC once.
 * @note   OPAQUE — completely covers the destination rectangle. This
 *         is the Phase-2 SD-card asset blit path.
 */
void HTFT_voidDrawImage(u16 Copy_u16X, u16 Copy_u16Y,
                        u16 Copy_u16Width, u16 Copy_u16Height,
                        const u16 *Copy_pu16Pixels);

/**
 * @brief  Draw a 1-bpp sprite: ONLY set bits are drawn — everything
 *         else is left untouched, so the sprite composites transparently
 *         over ANY existing background (the only compositing possible
 *         without GRAM readback).
 * @param  Copy_pu8Bitmap  packed bits, MSB = leftmost pixel of each row
 *                         (same bit order as the font), rows padded to
 *                         a byte boundary. Row stride = (W + 7) / 8.
 * @note   Erasing a sprite is the CALLER's job: FillRect over its old
 *         rectangle with the known background color. Runs of set bits
 *         are drawn as single HLines (fast); a worst-case checkerboard
 *         pattern degrades to per-pixel cost.
 */
void HTFT_voidDrawSprite(u16 Copy_u16X, u16 Copy_u16Y,
                         u16 Copy_u16Width, u16 Copy_u16Height,
                         const u8 *Copy_pu8Bitmap, u16 Copy_u16Color);

/* =====================================================================
 *  CIRCLES
 * ===================================================================== */

/** @brief Filled circle, center (X0, Y0), given radius. One HLine per
 *         row span — fast. Radius 0 = single pixel. Integer-only math.
 *         Keep center +/- radius inside the screen. */
void HTFT_voidFillCircle(u16 Copy_u16X0, u16 Copy_u16Y0,
                         u16 Copy_u16Radius, u16 Copy_u16Color);

/** @brief 1-px circle outline, center (X0, Y0) — midpoint algorithm
 *         with 8-way symmetry. Per-pixel window cost: the SLOWEST
 *         primitive in the driver (~10 ms at r=26). Use for static
 *         decoration, never per-frame. */
void HTFT_voidDrawCircle(u16 Copy_u16X0, u16 Copy_u16Y0,
                         u16 Copy_u16Radius, u16 Copy_u16Color);

#endif /* HTFT_INTERFACE_H_ */
