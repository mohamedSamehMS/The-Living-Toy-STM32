/*
 * HVirtualButtons_interface.h — SHARED virtual button type
 *
 *  Project 4 — Interactive Gaming Hub (ARM Embedded Systems Diploma)
 *  Target  : STM32F401CCU6 (WeAct Black Pill), register-level bare metal
 *  Layer   : HAL-common — sits ABOVE the input-controller HAL drivers.
 *
 *  WHY THIS FILE EXISTS (the layering rule):
 *  HIR (NEC remote) and HButtons (physical push buttons) are PEER
 *  drivers that both emit this type. The type therefore may not live
 *  in either driver's header: two HAL headers defining the same enum
 *  either redefinition-error at compile time, or — worse — quietly
 *  diverge into two enums with the same name and different values
 *  across translation units. One definition, included by both
 *  controllers and by Control/game logic.
 *
 *  Downstream contract: game/state logic receives BTN_* values and
 *  CANNOT tell (and never needs to tell) whether an event came from
 *  the IR remote or a physical button. The ESP/Tic-Tac-Toe online
 *  path does NOT emit these events — internet play carries MOVES,
 *  not button events (settled in the multiplayer discussion).
 *
 *  Values are stable identifiers, not bit positions — ORDER IS ABI:
 *  appending is safe; never insert/reorder (the HIR repeat-allow
 *  mask and every game's switch tables index by these values).
 *
 *  Created on: Sep 12, 2026
 *      Author: dell
 */
#ifndef HVIRTUALBUTTONS_INTERFACE_H_
#define HVIRTUALBUTTONS_INTERFACE_H_

/* No includes needed — this header is dependency-free by design so
 * every consumer (HIR, HButtons, Control, games) can include it
 * without dragging MCAL headers along. */

/* =====================================================================
 *  VirtualButton_t — semantic button roles
 *
 *  Two layers, matching the remote's physical split:
 *    - controller layer : D-pad, Confirm, Back, Options, Start, L1/R1
 *      (the PlayStation-style abstraction — movement and menus; every
 *      game consumes these)
 *    - numeric layer    : digits 1-9, 100+, 200+ (game-specific entry:
 *      Tic-Tac-Toe cell selection, Math Quiz answers, Memory Match
 *      card selection, direct game-launch shortcuts)
 *
 *  BTN_NUM_0 exists in the enum for controller-set completeness, but
 *  NO remote button maps to it: the design layout assigns the "0" key
 *  to BTN_BACK (the numeric layer is 1-9). Note for Math Quiz: answers
 *  containing the digit 0 need an application-level decision (e.g.
 *  repurposing 100+/200+ in that game's context) — open item.
 *
 *  BTN_NONE is the sentinel: "no button". Used for "no pending event",
 *  "unmapped code", and "no last-accepted button". It is NEVER a valid
 *  emitted event.
 * ===================================================================== */
typedef enum
{
    /* ---- controller layer ---- */
    BTN_UP = 0,                     /* D-pad                          */
    BTN_DOWN,                       /* D-pad                          */
    BTN_LEFT,                       /* D-pad                          */
    BTN_RIGHT,                      /* D-pad                          */
    BTN_CONFIRM,                    /* "Cross"                        */
    BTN_BACK,                       /* "Circle" — remote key "0"      */
    BTN_OPTIONS,                    /* "Triangle"                     */
    BTN_START,                      /* play/pause                     */
    BTN_L1,                         /* shoulder                       */
    BTN_R1,                         /* shoulder                       */

    /* ---- numeric layer ---- */
    BTN_NUM_0,                      /* NOT produced by this remote    */
    BTN_NUM_1, BTN_NUM_2, BTN_NUM_3, BTN_NUM_4,
    BTN_NUM_5, BTN_NUM_6, BTN_NUM_7, BTN_NUM_8, BTN_NUM_9,
    BTN_100PLUS,
    BTN_200PLUS,

    /* ---- sentinel ---- */
    BTN_NONE                        /* keep LAST (repeat mask is u32) */
} VirtualButton_t;

#endif /* HVIRTUALBUTTONS_INTERFACE_H_ */
