/*
 * APP_FOTA_NET.h — the FOTA orchestrator (FOTA seat, v1.2 — the
 *                   consent-model deltas).
 *
 *  THE CONSENT DELTAS (FOTA-48/49, per the ESP seat's v0.4.0 brief):
 *    1. THE DOTTED COMPARE: APP_FOTA_NET_u8VersionNewer() — numeric
 *       per component, missing = 0 ("8.0" == "8.0.0"; "10.0.0" >
 *       "7.0.0" — the lexical trap closed). Exposed PUBLIC: the
 *       hub consumes it for its own policy decisions (one
 *       implementation, one truth).
 *    2. POLICY 1 UN-BLOCKED: the compare now gates the NOTIFIED
 *       transition in BOTH modes — same/older than the running
 *       build => the notify is DISCARDED (a refresh re-notify of a
 *       suppressed version never reaches the hub; the auto-accept
 *       loop closes by construction: post-commit, C12's re-offer
 *       of the just-installed version drops here).
 *    3. THE NOTIFIED RE-ACCEPT (Finding 1): AVAILABLE is now
 *       accepted in IDLE and NOTIFIED — a newer offer while the
 *       prompt stands UPDATES the triple and RE-FIRES the
 *       callback (the screen refreshes; the ESP seat's precedence
 *       rule composes by design). A same/older re-offer while
 *       NOTIFIED: discarded (the standing offer is >= it).
 *    4. THE WATCHDOG RESUME: in hub mode, the watchdog returns to
 *       NOTIFIED-with-triple (the hub re-offers: "update
 *       interrupted — resume?"); in AUTO mode, to IDLE (NOTIFIED
 *       has no AUTO-mode exit — a stranded state).
 *
 *  Everything else stands as v1.1 (see that header's contract).
 *
 *  Revision history:
 *    v1.2 — the consent deltas (the compare; POLICY 1; the
 *           NOTIFIED re-accept; the WATCHDOG resume shape).
 *    v1.1 — the string-version ripple.
 *    v1.0 — first delivery.
 */

#ifndef APP_FOTA_NET_H_
#define APP_FOTA_NET_H_

#include "STD_TYPES.h"
#include "APP/NET_interface.h"    /* the events, the commands   */

/* The hub-day notify callback (AUTO_BEGIN 0 only): called with the
 * notify's fields; the hub then calls enumStart() on confirmation.
 * Fired on: the first accepted offer (IDLE -> NOTIFIED) and every
 * NEWER offer while a prompt stands (Finding 1: the refresh). */
typedef void (*APP_FOTA_NET_Notify_t)(const char *Copy_pchVersion,
                                      u32 Copy_u32Length,
                                      u32 Copy_u32E2eCrc);

typedef enum
{
    APP_FOTA_NET_STATE_IDLE = 0,
    APP_FOTA_NET_STATE_NOTIFIED,      /* hub mode: awaiting Start  */
    APP_FOTA_NET_STATE_BEGIN_WAIT,     /* BEGIN sent, awaiting READY */
    APP_FOTA_NET_STATE_BEGIN_RETRY,   /* BUSY spacing, will resend  */
    APP_FOTA_NET_STATE_RECEIVING,     /* the epoch: frames flowing */
    APP_FOTA_NET_STATE_DONE_WAIT      /* DONE sent, awaiting ack   */
} APP_FOTA_NET_State_t;

/* =====================================================================
 *  THE VERSION COMPARE (FOTA-49 delta 1) — public, one truth
 *
 *  Returns 1 iff Copy_pchNew is STRICTLY NEWER than Copy_pchRunning
 *  (numeric per component; missing components = 0; leading zeros
 *  tolerated; any non-digit component ends that component's number
 *  — the defensive parse). The lexical trap is closed:
 *  "10.0.0" > "7.0.0" returns 1 here, though lexically it is not.
 * ===================================================================== */
u8 APP_FOTA_NET_u8VersionNewer(const char *Copy_pchNew,
                               const char *Copy_pchRunning);

/* Boot: store the running version, init the UI, register the raw
 * trampoline with HESP. Call AFTER NET_enumInit and TFT init. */
NET_EnumStatus_t APP_FOTA_NET_enumInit(const char *Copy_pchThisVersion);

/* Every super-loop pass: pumps the receiver during the epoch,
 * drives the progress UI, services the BEGIN-retry spacing, and
 * runs the UI's beep engine. */
void APP_FOTA_NET_voidTask(void);

/* THE EVENT ENTRY — the single NET event drainer calls this with
 * EVERY event it drains (the orchestrator filters). */
void APP_FOTA_NET_voidOnEvent(const NET_Event_t *Copy_pstEvent);

/* Hub-day: register the notify callback (AUTO_BEGIN 0 mode). */
void APP_FOTA_NET_SetNotifyHandler(APP_FOTA_NET_Notify_t Copy_pfNotify);

/* Hub-day: proceed with the stored notify (after confirmation).
 * Only valid in the NOTIFIED state. */
NET_EnumStatus_t APP_FOTA_NET_enumStart(void);

/* Narration/diagnostics. */
APP_FOTA_NET_State_t APP_FOTA_NET_enumGetState(void);

/* R5: 1 = the last ladder failure was STAGE_CRC_FAIL (the ESP-side
 * stage is bad; the recorded remedy is FRESET + re-BEGIN — the hub
 * day's recovery path, gated on the human's retry press). */
u8 APP_FOTA_NET_u8StageCrcStuck(void);

#endif /* APP_FOTA_NET_H_ */
