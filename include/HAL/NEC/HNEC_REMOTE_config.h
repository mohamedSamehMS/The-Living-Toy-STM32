/*
 * HNEC_REMOTE_config.h � NEC remote controller (device layer), CONFIG
 *
 *  Project 4 � Interactive Gaming Hub (ARM Embedded Systems Diploma)
 *  Target  : STM32F401CCU6 (WeAct Black Pill), register-level bare metal
 *  Layer   : HAL � the UPPER half of the two-driver IR stack. Consumes
 *            HIR (link layer) and emits VirtualButton_t events.
 *
 *  This file is REMOTE IDENTITY + BEHAVIOR ONLY. It contains no pin,
 *  no line, no engine constants � hardware and protocol timing live in
 *  HIR_config.h / HIR_private.h. Changing to a different remote means
 *  editing THIS file and nothing else; changing the receiver pin
 *  means editing HIR_config.h and nothing else. That separation is
 *  the point of the split.
 *
 *  Reference: NEC protocol; CAR-MP3 remote (address 0x00) command set;
 *             the virtual-controller mapping from the feature notes
 *             (PlayStation-style roles + numeric layer).
 *
 *  Created on: Sep 13, 2026
 *      Author: dell
 */
#ifndef HNEC_REMOTE_CONFIG_H_
#define HNEC_REMOTE_CONFIG_H_

#include "STD_TYPES.h"

#include "HAL/HVirtualButtons_interface.h"     /* VirtualButton_t (shared)   */

/* =====================================================================
 *  1. Remote identity � address filter
 *
 *  Expected NEC address. The tested remote (CAR-MP3 clone) uses 0x00.
 *  HONEST SCOPE of this check: it rejects foreign remotes whose address
 *  DIFFERS. It does NOT reject another cheap clone that also ships
 *  0x00 (common!). Residual defenses against that: the NEC inverse-
 *  command checksum (always on, in HIR), table sparsity below
 *  (unmapped codes produce no event), and the runtime disable. */
#define HNEC_REMOTE_EXPECTED_ADDRESS     0x00u

/* =====================================================================
 *  2. Quiet windows � the two halves of the repeat contract
 *
 *  HNEC_REMOTE_QUIET_WINDOW_MS (after data frames, rejected frames,
 *  noise): the swallow window. Swallows the hold-repeat burst of a
 *  NON-repeatable button (one action per press) and sets the
 *  keyboard-style INITIAL DELAY for repeatable buttons: first
 *  auto-repeat lands ~decode(68ms) + this window after the press
 *  (~270ms at 200).
 *
 *  HNEC_REMOTE_REPEAT_QUIET_MS (after an ACCEPTED repeat event):
 *  0 = re-arm immediately -> subsequent repeats flow at NEC's native
 *  ~110ms cadence (~9 events/s while held). Raise to slow the
 *  auto-repeat rate (e.g. 100 skips roughly every other repeat
 *  frame). Affects ONLY already-repeat-allowed buttons.
 *
 *  Net repeat behavior (defaults): press -> one event; hold ->
 *  first repeat ~270ms, then ~110ms � the keyboard auto-repeat model.
 *  CPU while holding a repeatable button: ~11ms decode per 110ms
 *  (~10%). While holding a NON-repeatable button: ~11ms per ~211ms
 *  cycle (~5%) � the full window does the swallowing. Thread-context
 *  stalls only; the SysTick tick and higher-priority ISRs run
 *  throughout.
 * ===================================================================== */
#define HNEC_REMOTE_QUIET_WINDOW_MS      200u
#define HNEC_REMOTE_REPEAT_QUIET_MS      0u

/* =====================================================================
 *  3. NEC command -> VirtualButton_t mapping table
 *
 *  THE single place this remote's physical layout is defined. Changing
 *  remotes = editing this table, nothing else.
 *
 *  Codes are the STANDARD CAR-MP3 set. All 21 keys were verified on
 *  hardware (USB-TTL + HIR_enumGetLastRawFrame); clone variants with
 *  different code tables exist. A wrong code means
 *  a wrong button role, nothing worse (unmapped codes are ignored).
 *
 *  21 rows = the remote's 21 physical buttons. BTN_NUM_0 is
 *  deliberately absent: the "0" key is BTN_BACK per the virtual-
 *  controller layout (numeric layer is 1-9 + 100+/200+).
 * ===================================================================== */
typedef struct
{
    u8              NecCmd;         /* command byte (NEC byte 3)     */
    VirtualButton_t VirtualButton;  /* role it becomes               */
} HNEC_REMOTE_MapEntry_t;

/* static in a header � this file MUST be included by
 * HNEC_REMOTE_program.c ONLY (the MEXTI/MSPI config pattern). */
static const HNEC_REMOTE_MapEntry_t HNEC_REMOTE_ButtonMap[] =
{
    /* ---- virtual-controller layer ---- */
    { 0x45u, BTN_LEFT     },    /* "CH-"          */
    { 0x46u, BTN_CONFIRM  },    /* "CH"           */
    { 0x47u, BTN_RIGHT    },    /* "CH+"          */
    { 0x44u, BTN_UP       },    /* "<<"  PREV     */
    { 0x40u, BTN_DOWN     },    /* ">>"  NEXT     */
    { 0x43u, BTN_START    },    /* ">"   play/pause */
    { 0x07u, BTN_L1       },    /* "-"   VOL-     */
    { 0x15u, BTN_R1       },    /* "+"   VOL+     */
    { 0x09u, BTN_OPTIONS  },    /* "EQ"           */
    { 0x16u, BTN_BACK     },    /* "0"  � Back/Cancel (NOT NUM_0) */

    /* ---- numeric layer ---- */
    { 0x0Cu, BTN_NUM_1    },    /* tested         */
    { 0x18u, BTN_NUM_2    },    /* tested         */
    { 0x5Eu, BTN_NUM_3    },
    { 0x08u, BTN_NUM_4    },
    { 0x1Cu, BTN_NUM_5    },
    { 0x5Au, BTN_NUM_6    },
    { 0x42u, BTN_NUM_7    },
    { 0x52u, BTN_NUM_8    },
    { 0x4Au, BTN_NUM_9    },
    { 0x19u, BTN_100PLUS  },
    { 0x0Du, BTN_200PLUS  }
};

#define HNEC_REMOTE_BUTTON_MAP_SIZE \
    (sizeof(HNEC_REMOTE_ButtonMap) / sizeof(HNEC_REMOTE_ButtonMap[0]))

/* =====================================================================
 *  4. Repeat-allowed mask � which roles honor held-button auto-repeat
 *
 *  Bit per VirtualButton_t value. Allowed: D-pad + shoulders (scroll,
 *  ramp). NOT allowed: Confirm/Back/Options/Start and every digit �
 *  those require a fresh discrete press per action (exactly one event
 *  per press; repeats are decoded and discarded).
 *
 *  This is the deferred decision from the feature notes, given its
 *  config home: edit the mask, no code change. (Final confirmation
 *  belongs to the same echo pass as the codes above.)
 * ===================================================================== */
#define HNEC_REMOTE_REPEAT_ALLOWED_MASK   ( (1UL << BTN_UP)    | \
                                            (1UL << BTN_DOWN)  | \
                                            (1UL << BTN_LEFT)  | \
                                            (1UL << BTN_RIGHT) | \
                                            (1UL << BTN_L1)    | \
                                            (1UL << BTN_R1) )

#endif /* HNEC_REMOTE_CONFIG_H_ */
