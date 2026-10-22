/*
 * HUB_renderer.h — the hub's dumb painter contract + frozen geometry.
 *  LOCATION: APP/ (flat).
 *
 *  THE RULE (same as every game renderer): this layer paints WHAT it
 *  is told and knows NOTHING about navigation, selection semantics,
 *  or game state. The menu engine (HUB_menu) is its only caller and
 *  owns all decisions. Pixels live here; policy lives there.
 *
 *  GEOMETRY (frozen contract facts — change only with a redesign):
 *    title  : 2x text, centered, 16 px tall, at y 8
 *    rule   : 1-px separator line at y 30
 *    info   : optional 1x line at y 34 (16 chars max)
 *    rows   : 1x text, pitch 14 px, starting y 46, max 8 rows
 *             (46 + 7*14 + 8 = 152 — fits 160 with margin)
 *    arrow  : 1-char cell at x 4 (">" when selected, " " when not)
 *    label  : x 16 — max 9 chars when the row has a value field
 *    value  : fixed 5-char field at x 88..123 — fixed-rect overwrite
 *
 *  THE LABEL+VALUE LAW: a row with a value must keep its label
 *  within 9 chars (x 16..87) so it never collides with the value
 *  field. Labels without values may run to 16 chars. This is the
 *  Memory-Match HUD lesson enforced by layout instead of luck.
 */
#ifndef HUB_RENDERER_H_
#define HUB_RENDERER_H_
#include "STD_TYPES.h"

#define HUB_MENU_TITLE_Y     8u
#define HUB_MENU_SEP_Y       30u
#define HUB_MENU_INFO_Y      34u
#define HUB_MENU_ROW_Y0      46u
#define HUB_MENU_ROW_PITCH   14u
#define HUB_MENU_ROW_MAX     8u
#define HUB_MENU_ARROW_X     4u
#define HUB_MENU_LABEL_X     16u
#define HUB_MENU_VALUE_X     88u
#define HUB_MENU_VALUE_LEN   5u

/* Full frame repaint (the screen-transition rule): bg wipe + title
 * + separator + optional info line. Called on menu ENTRY and after
 * any action that may have changed everything (value cycle).       */
void HUB_RENDERER_voidPaintFrame(const char *Copy_pchTitle, const char *Copy_pchInfo);

/* One row: arrow cell + label + optional value field, all OPAQUE
 * 1x cells (same-rect overwrite law — no erase step, no smearing).
 * Copy_pchValue == NULL skips the field entirely (rows without
 * values). The value is padded to the fixed 5-char field so a
 * shorter new value fully covers a longer old one — the moving-
 * content trap, avoided by construction.                          */
void HUB_RENDERER_voidPaintRow(u8 Copy_u8RowIdx, const char *Copy_pchLabel,
                               const char *Copy_pchValue, u8 Copy_u8Selected);

#endif /* HUB_RENDERER_H_ */
