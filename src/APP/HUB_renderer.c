/*
 * HUB_renderer.c — the hub's painters. HTFT surface used: FillScreen,
 *  DrawString2x, DrawHLine, DrawString. Nothing else.
 */
#include <HAL/TFT/HTFT_interface.h>
#include "APP/HUB_renderer.h"
#include "APP/HUB_config.h"

/* 2x title centering: (128 - 16*len)/2. len is counted, not guessed
 * — a longer title than 8 chars would overflow the line (16 px per
 * 2x char, 128 px per line), so titles are 1..8 chars by contract. */
static void HUB_RENDERER_privateCenter2x(const char *Copy_pchText, u16 *Copy_pu16X)
{
    u8  Local_u8Len = 0u;
    s32 Local_s32X;
    while (Copy_pchText[Local_u8Len] != '\0') { Local_u8Len++; }
    Local_s32X = ((s32)128 - (16 * (s32)Local_u8Len)) / 2;
    if (Local_s32X < 0) { Local_s32X = 0; }   /* oversized: clamp + clip,
                                                never teleport                    */
    *Copy_pu16X = (u16)Local_s32X;
}
void HUB_RENDERER_voidPaintFrame(const char *Copy_pchTitle, const char *Copy_pchInfo)
{
    u16 Local_u16X;
    /* Full wipe first — GRAM is never trusted across transitions
     * (the screen-is-cache-of-state rule, applied at menu scale).  */
    HTFT_voidFillScreen(HUB_CFG_COLOR_BG);
    HUB_RENDERER_privateCenter2x(Copy_pchTitle, &Local_u16X);
    HTFT_voidDrawString2x(Local_u16X, HUB_MENU_TITLE_Y, Copy_pchTitle,
                          HUB_CFG_COLOR_TITLE, HUB_CFG_COLOR_BG);
    HTFT_voidDrawHLine(0u, HUB_MENU_SEP_Y, 128u, HUB_CFG_COLOR_FRAME);
    if (Copy_pchInfo != (const char *)0)
    { HTFT_voidDrawString(0u, HUB_MENU_INFO_Y, Copy_pchInfo,
                          HUB_CFG_COLOR_INFO, HUB_CFG_COLOR_BG); }
}

void HUB_RENDERER_voidPaintRow(u8 Copy_u8RowIdx, const char *Copy_pchLabel,
                               const char *Copy_pchValue, u8 Copy_u8Selected)
{
    u16 Local_u16Y = (u16)(HUB_MENU_ROW_Y0 + ((u16)Copy_u8RowIdx * HUB_MENU_ROW_PITCH));

    /* Arrow cell: ">" or " " — the space draw is the ERASE. Opaque
     * 1x cell, same rect every time: selection flicker is impossible
     * by construction (Law 2, same-rect replace).                   */
    HTFT_voidDrawString(HUB_MENU_ARROW_X, Local_u16Y,
                        (Copy_u8Selected != 0u) ? ">" : " ",
                        HUB_CFG_COLOR_CURSOR, HUB_CFG_COLOR_BG);

    /* Label: opaque cell — repainting a label always lands clean.   */
    HTFT_voidDrawString(HUB_MENU_LABEL_X, Local_u16Y, Copy_pchLabel,
                        HUB_CFG_COLOR_TEXT, HUB_CFG_COLOR_BG);

    /* Value: copy into a FIXED-WIDTH field, padded right with
     * spaces. The padding is the whole trick — "EASY" fully covers
     * a previous "NORMAL" because both render as exactly 5 opaque
     * cells. Without the pad, a shrinking value leaves ghosts (the
     * Math Quiz centered-content lesson, pre-empted).               */
    if (Copy_pchValue != (const char *)0)
    {
        char Local_achVal[HUB_MENU_VALUE_LEN + 1u];
        u8 Local_u8I = 0u;
        while ((Local_u8I < HUB_MENU_VALUE_LEN) && (Copy_pchValue[Local_u8I] != '\0'))
        { Local_achVal[Local_u8I] = Copy_pchValue[Local_u8I]; Local_u8I++; }
        while (Local_u8I < HUB_MENU_VALUE_LEN) { Local_achVal[Local_u8I++] = ' '; }
        Local_achVal[HUB_MENU_VALUE_LEN] = '\0';
        HTFT_voidDrawString(HUB_MENU_VALUE_X, Local_u16Y, Local_achVal,
                            HUB_CFG_COLOR_VALUE, HUB_CFG_COLOR_BG);
    }
}
