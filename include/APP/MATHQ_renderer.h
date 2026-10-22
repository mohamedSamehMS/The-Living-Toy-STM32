/*
 * MATHQ_renderer.h — the TFT-side contract for Math Quiz.
 *  LOCATION: APP/ (flat).
 *
 *  The renderer paints the flashcard scene (panel + large equation +
 *  answer slots), the 3x4 keypad, the key cursor, hearts, and HUD.
 *  It knows nothing about questions, levels, or the answer — the
 *  caller supplies values; pixels live only inside MATHQ_renderer.c.
 *
 *  Keypad layout (key index 0..11, row-major):
 *      1  2  3
 *      4  5  6
 *      7  8  9
 *      <  0  =        '<' = backspace, '=' = submit
 *
 *  Preconditions: HTFT_voidInit() has run; ms timebase alive.
 */

#ifndef MATHQ_RENDERER_H_
#define MATHQ_RENDERER_H_

#include "STD_TYPES.h"

/* ---- Geometry (frozen contract facts, not tunables) ---- */
#define MATHQ_KEY_PX            24u     /* key size                     */
#define MATHQ_KEY_PITCH         28u     /* key spacing (4-px gaps)      */
#define MATHQ_KEYPAD_X0         24u     /* keypad origin (centered)     */
#define MATHQ_KEYPAD_Y0         50u
#define MATHQ_KEYPAD_KEYS       12u

#define MATHQ_PANEL_X           4u      /* question panel box           */
#define MATHQ_PANEL_Y           10u
#define MATHQ_PANEL_W           120u
#define MATHQ_PANEL_H           28u

#define MATHQ_LINE_Y            16u     /* 2x text line inside panel    */
#define MATHQ_MSG_Y             40u     /* message/prompt line          */

/* ---- API ---- */

/* Entry/transition repaint: bg, panel box, HUD, message prompt,
 * all 12 keys (no cursor — caller owns cursor position).            */
void MATHQ_voidPaintScreen(void);

/* Wipe the panel interior (for shake redraws). Border stays.        */
void MATHQ_voidErasePanelInterior(void);

/* Draw the full question line: static equation ("a op b =") in
 * question color + the answer slots in Copy_u16AnsColor. The line is
 * CENTERED accounting for 2 reserved answer slots; Copy_s8Shift
 * offsets it horizontally (the wrong-answer shake). Also records the
 * answer-slot position for MATHQ_voidPaintAnswer.
 * Generator contract: at most one two-digit operand (max 7 slots).  */
void MATHQ_voidPaintQuestionLine(u8 Copy_u8A, char Copy_chOp, u8 Copy_u8B,
                                 const char *Copy_chAns, u8 Copy_u8AnsLen,
                                 u16 Copy_u16AnsColor, s8 Copy_s8Shift);

/* Redraw ONLY the answer slots (typed digits / '?' / reveal) in the
 * slots reserved by the last PaintQuestionLine. Opaque cells — the
 * blank slot is erased automatically. Supports 0..2 digits.         */
void MATHQ_voidPaintAnswer(const char *Copy_chDigits, u8 Copy_u8Len,
                           u16 Copy_u16Color);

/* One keypad key: box + 2x label. pressed = highlighted fill.       */
void MATHQ_voidPaintKey(u8 Copy_u8KeyIdx, u8 Copy_u8Pressed);

/* Yellow cursor ring around a key (visible=1) / erase it (visible=0).
 * The ring lives in the 4-px gaps — the key itself is untouched.    */
void MATHQ_voidPaintKeyCursor(u8 Copy_u8KeyIdx, u8 Copy_u8Visible);

/* Heart idx 0..2 in the HUD row: image / black erase.               */
void MATHQ_voidPaintHeart(u8 Copy_u8Idx, u8 Copy_u8Visible);

/* HUD cells — fixed-width opaque overwrites.                        */
void MATHQ_voidPaintScore(u16 Copy_u16Score);
void MATHQ_voidPaintLevel(u8 Copy_u8Level);

/* Message line (y 40), 16-char padded opaque overwrite.             */
void MATHQ_voidPaintMessage(const char *Copy_chText, u16 Copy_u16Color);

#endif /* MATHQ_RENDERER_H_ */
