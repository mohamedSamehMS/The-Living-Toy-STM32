/*
 * APP_FOTA_config.h — configuration of the FOTA receiver
 * (FOTA seat, v2.1)
 *
 *  SECTION 1 is POLICY — the knobs you may turn (two of them).
 *  SECTION 2 is FROZEN PROTOCOL TRUTH — the wire contract
 *  shared with the PC sender, the future ESP sender, and the
 *  bootloader. These are NOT tunables: changing one breaks
 *  the other end of the wire (or the commit). They live here
 *  so the protocol's numbers have one reading place — with
 *  the strongest possible DO-NOT-EDIT markers.
 *
 *  Reference: FOTA-SPEC-1 (the frozen wire protocol spec,
 *  cross-addressed to the link and ESP seats)
 *
 *  Revision history:
 *    v2.1 — first config file (the bench-wire + narration
 *           knobs arrived; the frozen constants moved here
 *           from the implementation with their banners)
 * 	  v2.2 — protocol v1.7: the sequence byte; frame 133.
 *  Created: FOTA v2.1 delivery
 *      Author: Mohamed Sameh (with the FOTA Seat AI session)
 */

#ifndef APP_FOTA_CONFIG_H_
#define APP_FOTA_CONFIG_H_

#include <MCAL/USART/MUSART_interface.h>   /* the instance enum */

/* =====================================================================
 *  SECTION 1 — POLICY (the knobs)
 * ===================================================================== */

/* The BENCH binding's wire. Default = USART2 (PA2/PA3, the PC
 * console + YP-01/CP2102 link) — v1 behavior, byte-identical.
 *
 * Set to MUSART_USART1 for the Step-1 ESP bring-up bench: the
 * ESP runs the v1 protocol as sender on PA9/PA10 (the same UART
 * its monitor shares — both windows captured per the ESP-link
 * bench law). Protocol text (READY, the final status line) and
 * frames ride this wire; the console keeps diagnostics.
 *
 * NET mode is NOT affected by this knob (NET feeds the core
 * through its own byte path; narration is console-fixed). */
#define APP_FOTA_BENCH_USART    MUSART_USART1

/* NET-mode informational narration on the console (USART2 — the
 * diagnostics wire; allowed during the epoch; fills the
 * [TX]-mirror gap). 0 = silent. Terminal status lines are NOT
 * gated by this (they are the wire contract in bench mode and
 * the report in NET mode). */
#define APP_FOTA_NET_NARRATE   1u

#if (APP_FOTA_NET_NARRATE != 0) && (APP_FOTA_NET_NARRATE != 1)
#error "APP_FOTA: APP_FOTA_NET_NARRATE must be 0 or 1"
#endif

/* =====================================================================
 *  SECTION 2 — FROZEN PROTOCOL FACTS — DO NOT EDIT
 *
 *  These mirror the sender (fota_send.py v1.6 / the ESP port)
 *  and the bootloader's trailer reader. Changing ANY of them
 *  desynchronizes the ends of the wire. They are contracts,
 *  not preferences. Amendment-level changes only, per the
 *  cross-seat process (FOTA-SPEC-1).
 * ===================================================================== */

#define FOTA_HEADER_BUF_SIZE    32u                 /* bench text-header cap  */
#define FOTA_SEQ_SIZE           1u                  /* v1.7: sequence byte  */
#define FOTA_DATA_SIZE          128u                /* frame payload          */
#define FOTA_CRC_SIZE           4u                 /* per-frame CRC field    */
#define FOTA_FRAME_SIZE         (FOTA_SEQ_SIZE + FOTA_DATA_SIZE + FOTA_CRC_SIZE)  /* 133 */
#define FOTA_TRAILER_SIZE       32u                 /* staging-tail trailer   */
#define FOTA_MAGIC              0x464F5441UL        /* "FOTA" — written LAST  */
#define FOTA_TRAILER_VERSION    2u
#define FOTA_ACK                0x06u
#define FOTA_NAK                0x15u
#define FOTA_RESYNC             0xAAu

/* The no-data abort — DISABLED since the FOTA-06 incident (the
 * MSYSTICK GetElapsed_ms anomaly; see the evidence package). A
 * u32 difference can never exceed 0xFFFFFFFF, so the check is
 * inert by value. NET's 90 s zero-byte epoch watchdog is the v2
 * backstop; the sender's retry budget governs — the proven
 * configuration. Re-enable ONLY after the MCAL root fix. */
#define FOTA_ABORT_MS          0xFFFFFFFFUL

#endif /* APP_FOTA_CONFIG_H_ */
