/*
 * APP_FOTA_APP.c � THE UPDATE MODULE: FOTA with consent, as a game row.
 *  LOCATION: APP/ (flat). The hub's UPDATE row � selected from the home
 *  menu like any game; Run() owns the whole ceremony.
 *
 *  THE OWNER'S RULING (this module's reason to exist): a menu row, not
 *  a prompt � "it will first ask the user, and he can update anytime he
 *  wants, not forced to update." The check itself is deliberate: no
 *  hand on the remote, no wire. The toy asks; the human decides when.
 *  The child-safety story completes here � no forced updates, no
 *  surprise prompts, a parent's door.
 *
 *  THE CEREMONY (inside Run � the module's FSM):
 *    LINK WAIT   -> NET_enumBringup (the deferred link � this module is
 *                   now its SECOND caller; idempotent, one link, both
 *                   customers) -> the 30 s bound -> NO CONNECTION/BACK
 *    OFFER WAIT  -> the ESP re-offers at every D15; the orchestrator's
 *                   compare gates same/older � a NEWER offer fires the
 *                   notify callback -> the offer screen (version, size)
 *                   -> CH = START, 0 = exit (the offer STANDS � NOTIFIED
 *                   persists; re-entry shows it immediately)
 *                   -> 30 s with no offer = "NO UPDATE AVAILABLE"/BACK
 *    CONFIRM     -> "UPDATE? CH AGAIN" � the second press inside 5 s is
 *                   I-CONFIRM (two-beat: an accidental bump must never
 *                   start an update); 0 or timeout = back to the offer;
 *    UPDATING    -> enumStart() -> PREPARING drawn BEFORE the stall
 *                   (the draw-before law) -> the BUSY window renders as
 *                   WAITING (the ladder retries internally) -> the
 *                   epoch: the bar, the percent, DO NOT POWER OFF ->
 *                   input DEAD (the owner's ruling: after I-CONFIRM the
 *                   child touches nothing) -> terminal: Success + reset
 *                   (never returns; the BL commits; the NEW BUILD's
 *                   banner is the success screen) or Error + dismissal
 *                   -> EXITED
 *
 *  THE ROUTING (this tree's settled design): while this module runs,
 *  ITS loop is the FOTA-family drain point � NET events route here:
 *  FOTA-family -> APP_FOTA_NET_voidOnEvent (HUB context � it draws),
 *  everything else dropped (idle-online strays; NET's queue depth and
 *  the orchestrator's state gates make them harmless � this module
 *  owns FOTA while it runs).
 *
 *  THE SCORE MODEL: updates are not games � GetScore is 0, GAMEOVER
 *  never fires. Success ends in NetReset (never returns); failure
 *  returns EXITED through the seam.
 *
 *  ZERO FreeRTOS knowledge: APP_INPUT, the orchestrator's API, NET's
 *  wrapper � plain calls. The seam did the OS work (same as every
 *  module on this tree).
 *
 *  Revision history:
 *    File 1 � first delivery (the FOTA wiring package, 1 of 5)
 */

#include "STD_TYPES.h"

#include <MCAL/GPIO/MGPIO_interface.h>         /* the module's heartbeat (PC13) */
#include <MCAL/SYSTICK/MSYSTICK_interface.h>   /* the shim serves the HUB task */

#include <HAL/TFT/HTFT_interface.h>

#include "APP/HUB_interface.h"
#include "APP/APP_INPUT.h"
#include "APP/NET_interface.h"

#include "APP/APP_FOTA_NET.h"
#include "APP/APP_FOTA_UI.h"
#include "HAL/AUDIO/HAUDIO_interface.h"

/* ---- the module's FSM ---- */
typedef enum
{
    FA_ST_LINK_WAIT = 0,    /* bring-up + the 30 s bound                 */
    FA_ST_OFFER_WAIT,       /* waiting for a NEWER offer (or timeout)    */
    FA_ST_CONFIRM,          /* the two-beat gate, 5 s window             */
    FA_ST_UPDATING          /* after I-CONFIRM: the autonomous ladder    */
} FA_State_t;

/* ---- timing (the config-discipline: UX timing here, wire timing in
 *       NET/orchestrator configs; all values from the recorded specs) ---- */
#define FA_LINK_WAIT_MS     30000u   /* the frozen 30 s link bound       */
#define FA_OFFER_WAIT_MS    30000u   /* no newer offer in 30 s = none    */
#define FA_CONFIRM_MS       5000u    /* the two-beat window (the house law) */
#define FA_HEARTBEAT_MS     500u     /* Run owns its beat (the house pattern) */

/* ---- module state (file-private) ---- */
static FA_State_t FA_eState;
static u32  FA_u32T0;
static u8   FA_u8HaveOffer;          /* the callback fired this Run      */
static u8   FA_u8DismissArm;         /* the error-dismissal exit's arm   */

/* the heartbeat (the house pattern � PC13, Run owns its beat) */
static void FA_voidToggleHeartbeat(void)
{
    static u8 Local_u8State = 0;
    Local_u8State = (u8)!Local_u8State;
    (void)MGPIO_SetPinValue(MGPIO_PORTC, MGPIO_PIN_13,
            (Local_u8State != 0u) ? MGPIO_PIN_HIGH : MGPIO_PIN_LOW);
}

/* ---- THE NOTIFY CALLBACK (hub mode's entry: the offer arrives) ----
 * Registered at Run entry. The orchestrator fires it on a NEWER offer
 * while NOTIFIED (the compare already gated same/older below us).
 * Our job: remember it, and paint � WE are the screen owner (HUB
 * context, per the routing design). */
static void FA_privateNotify(const char *Copy_pchVersion,
                             u32 Copy_u32Length,
                             u32 Copy_u32E2eCrc)
{
    (void)Copy_u32E2eCrc;   /* the compare already ran; the triple's
                               third member is the orchestrator's book */
    FA_u8HaveOffer = 1u;

    /* R5-FIX 1: the offer may arrive during LINK_WAIT � the ESP pushes
       EVENT_FOTA_AVAILABLE immediately after answering D15, BEFORE the
       module's FSM leaves LINK_WAIT. Paint it whenever we are pre-
       CONFIRM: the screen follows the offer, not the FSM's phase. (The
       old OFFER_WAIT-only gate swallowed the paint; the offer stood
       invisible and the 30 s timeout reported "NO UPDATE AVAILABLE".) */
    if ((FA_eState == FA_ST_OFFER_WAIT) || (FA_eState == FA_ST_LINK_WAIT))
    {
        APP_FOTA_UI_Available(Copy_pchVersion, Copy_u32Length);
    }
    /* mid-ceremony (CONFIRM/UPDATING): the refresh is recorded
       (FA_u8HaveOffer); the orchestrator's own gates handle it */
}

/* ================================================================= *
 *  THE MODULE CONTRACT                                              *
 * ================================================================= */

GameResult_t APP_FOTA_APP_enumRun(void)
{
    APP_INPUT_Event_t Local_stEvt;

    (void)HAUDIO_enumStop();

    /* ---- per-launch state ---- */
    FA_eState       = FA_ST_LINK_WAIT;
    FA_u8HaveOffer  = 0u;
    FA_u8DismissArm = 0u;
    FA_u32T0        = MSYSTICK_GetElapsed_ms();

    /* register the notify handler (the orchestrator stores it �
       re-registration per Run is the module's ownership claim) */
    APP_FOTA_NET_SetNotifyHandler(FA_privateNotify);

    /* ---- the entry screen: PREPARING-shape (the link is coming) ---- */
    APP_FOTA_UI_Preparing();     /* "PREPARING..." � the link wait's face */

    /* ---- the deferred link: THIS module's second caller ---- */
    (void)NET_enumBringup();     /* idempotent: online-already = instant */

    /* R5-FIX: SOLICIT the offer. The ESP re-offers on STM_READY and on
     * reconnect ONLY � a re-entry with the link already up sends no D15,
     * so no fresh offer ever arrives (the previous session's re-offer
     * died in its queue/guards). One explicit STM_READY pair wakes the
     * ESP's re-offer trigger and the ceremony proceeds. Uses NET's
     * documented bring-up command � idempotent in-task (it re-sends D15
     * only via this same path when explicitly re-armed... see the note). */
    (void)NET_enumSolicitOffer();

    /* ============ THE MODULE LOOP ============ */
    while (1)
    {
        u32 Local_u32Now = MSYSTICK_GetElapsed_ms();
        u8 Local_u8HaveEvt =
            (APP_INPUT_enumGetEvent(&Local_stEvt) == APP_INPUT_OK);
        NET_Event_t Local_stNet;

        /* ---- THE ROUTING DRAIN (R5-FIX: EVERY event forwards) ----
         * The FOTA seat's contract: "the single NET event drainer calls
         * this with EVERY event it drains (the orchestrator filters)".
         * The original four-type filter here DROPPED the very events the
         * ladder lives on � RESP_ERR (BUSY: the retry's trigger), CMD_
         * TIMEOUT (the fallback), LINK_LOST (the clean abort) � so a
         * BUSY answer never armed the retry and the module hung at
         * PREPARING with the orchestrator parked in BEGIN_WAIT forever.
         * Forwarding everything is safe BY THE ORCHESTRATOR'S DESIGN:
         * its switch + state guards filter internally; non-FOTA events
         * fall to default and are ignored.                               */
        while (NET_enumGetEvent(&Local_stNet) == NET_OK)
        {
            APP_FOTA_NET_voidOnEvent(&Local_stNet);
        }

        /* ---- the orchestrator's task: EVERY pass (the epoch pump, the
                UI engine, the BUSY spacing). Before I-CONFIRM it is
                harmless (state-gated inside); calling it unconditionally
                keeps the ladder's retry spacing alive even while the
                offer screen idles (the watchdog-resume path re-offers
                through the callback). ---- */
        APP_FOTA_NET_voidTask();

        /* ---- the FSM ---- */
        switch (FA_eState)
        {
        case FA_ST_LINK_WAIT:
        	/* R5-FIX 2: the offer IS the link proof. EVENT_ESP_READY's
                       line can be decapitated by the bring-up window (the ESP
                       answers D15 copy 1 during the 100 ms task-delay; the
                       pump drains mid-stream and the line's head is lost) �
                       so LinkLive() may legitimately stay false while the
                       link provably works. The FSM advances on EITHER: the
                       flag, or an offer having arrived (the callback set
                       FA_u8HaveOffer � which also proves RX, parse, route,
                       notify: the whole chain). */
            if ((NET_u8LinkLive() != 0u) || (FA_u8HaveOffer != 0u))
            {
                FA_eState = FA_ST_OFFER_WAIT;
                FA_u32T0  = Local_u32Now;
                if (FA_u8HaveOffer == 0u)
                {
                    APP_FOTA_UI_Preparing();   /* no offer yet: hold this */
                }
                /* offer standing: its screen is already painted (Fix 1's
                   callback) � do NOT overwrite it with PREPARING */
            }
            else if ((Local_u32Now - FA_u32T0) >= FA_LINK_WAIT_MS)
            {
                /* R5-FIX: honest wording � with the link up and every
                   offer rejected by the compare (the anti-reinstall
                   lock at work, e.g. the ESP re-offering the version
                   the board already replaced), the truthful report is
                   NO NEW UPDATE, not NO CONNECTION. The dead-link case
                   is rare and self-evident (no D15 answer, no ring
                   activity on the console breadcrumbs). */
                APP_FOTA_UI_Error("NO NEW UPDATE");   /* 14 chars � fits */
                FA_eState     = FA_ST_UPDATING;
                FA_u8DismissArm = 1u;
            }
            else if ((Local_u8HaveEvt != 0u) && (Local_stEvt.Button == BTN_BACK)
                     && (Local_stEvt.IsRepeat == 0u))
            { return GAME_RESULT_EXITED; }   /* THE SEAM */
            break;

        case FA_ST_OFFER_WAIT:
            if (FA_u8HaveOffer != 0u)
            {
                /* the callback painted the offer screen. CH arms the
                   gate; BACK exits (the offer STANDS � NOTIFIED). */
                if ((Local_u8HaveEvt != 0u) && (Local_stEvt.Button == BTN_CONFIRM)
                    && (Local_stEvt.IsRepeat == 0u))
                {
                    FA_eState = FA_ST_CONFIRM;
                    FA_u32T0  = Local_u32Now;
                    /* the two-beat ask (the module's one direct draw �
                       the app-layer practice for ceremony screens).
                       R5-FIX: title lowered (the 2x glyphs' tops crowded
                       the screen edge) and the prompt SPLIT to two
                       centered lines � one action per line, each line
                       width-verified: UPDATE? 7*16=112@x8; CH:CONFIRM
                       10*8=80@x24; 0:BACK 6*8=48@x40 � all inside 128. */
                    HTFT_voidFillScreen(HTFT_COLOR_BLACK);
                    HTFT_voidDrawString2x(8u, 32u, "UPDATE?",
                                         HTFT_COLOR_WHITE, HTFT_COLOR_BLACK);
                    HTFT_voidDrawString(24u, 80u, "CH:CONFIRM",
                                        HTFT_COLOR_YELLOW, HTFT_COLOR_BLACK);
                    HTFT_voidDrawString(40u, 96u, "0:BACK",
                                        HTFT_COLOR_YELLOW, HTFT_COLOR_BLACK);
                }
                else if ((Local_u8HaveEvt != 0u) && (Local_stEvt.Button == BTN_BACK)
                         && (Local_stEvt.IsRepeat == 0u))
                { return GAME_RESULT_EXITED; }   /* the offer stands */
            }
            else if ((FA_u8HaveOffer == 0u) &&
                     ((Local_u32Now - FA_u32T0) >= FA_OFFER_WAIT_MS))
            {
                /* 30 s, no newer offer: the honest answer */
                APP_FOTA_UI_Error("NO NEW UPDATE");
                FA_eState     = FA_ST_UPDATING;  /* the dismissal shape */
                FA_u8DismissArm = 1u;
            }
            else if ((Local_u8HaveEvt != 0u) && (Local_stEvt.Button == BTN_BACK)
                     && (Local_stEvt.IsRepeat == 0u))
            { return GAME_RESULT_EXITED; }
            break;

        case FA_ST_CONFIRM:
            if ((Local_u8HaveEvt != 0u) && (Local_stEvt.IsRepeat == 0u))
            {
                if (Local_stEvt.Button == BTN_CONFIRM)
                {
                    /* I-CONFIRM � the ceremony's second beat. From here
                       the chain is autonomous (the owner's ruling). */
                    FA_eState     = FA_ST_UPDATING;
                    FA_u8DismissArm = 0u;
                    /* PREPARING drawn BEFORE enumStart() � the stall's
                       screen must exist before the stall (the law). The
                       orchestrator's accept path draws it in AUTO mode
                       only; hub mode: WE draw it (FOTA-51 �3). */
                    APP_FOTA_UI_Preparing();
                    (void)APP_FOTA_NET_enumStart();
                }
                else if (Local_stEvt.Button == BTN_BACK)
                {
                    /* cancelled: the offer stands (NOTIFIED persists).
                       The honest repaint: name the state; the offer
                       re-paints on the next notify or re-entry (the
                       triple lives below us � a getter is the FOTA
                       seat's one-line call if they prefer). */
                    FA_eState = FA_ST_OFFER_WAIT;
                    FA_u32T0  = Local_u32Now;
                    /* R5-FIX: title lowered AND dropped to 1x (11 chars
                       at 2x = 176 px > 128 � it was clipping since
                       delivery), prompt split to two centered lines */
                    HTFT_voidFillScreen(HTFT_COLOR_BLACK);
                    HTFT_voidDrawString(20u, 32u, "OFFER STANDS",
                                        HTFT_COLOR_WHITE, HTFT_COLOR_BLACK);
                    HTFT_voidDrawString(24u, 80u, "CH:START",
                                        HTFT_COLOR_YELLOW, HTFT_COLOR_BLACK);
                    HTFT_voidDrawString(40u, 96u, "0:BACK",
                                        HTFT_COLOR_YELLOW, HTFT_COLOR_BLACK);
                }
                else
                {
                    /* any other key: dismissed back to the offer */
                    FA_eState = FA_ST_OFFER_WAIT;
                }
            }
            else if ((Local_u32Now - FA_u32T0) >= FA_CONFIRM_MS)
            {
                FA_eState = FA_ST_OFFER_WAIT;   /* timeout: cancel */
            }
            break;

        case FA_ST_UPDATING:
        	/* INPUT DEAD � by the owner's ruling (after I-CONFIRM the
                       child touches nothing) � EXCEPT the dismissal exits. The
                       ladder renders itself (the orchestrator calls the UI
                       primitives); our job is only the exits. */
        	if (FA_u8DismissArm != 0u)
        	{
        		/* an error screen stands: BACK exits */
        		if ((Local_u8HaveEvt != 0u) && (Local_stEvt.Button == BTN_BACK)
        				&& (Local_stEvt.IsRepeat == 0u))
        		{ return GAME_RESULT_EXITED; }
        	}
        	else
        	{
        		/* the live-update arm: watch for the FAILURE terminal
                           (success never returns � NetReset resets the board;
                           the BL commits; the new build's banner is the end). */
        		/* R5-FIX: the terminal-state check must catch EVERY
                           ended-ladder state, not just IDLE � the LINK_LOST/
                           timeout/error paths can pass through IDLE between
                           events (an ESP reconnect re-offer lands the
                           orchestrator at NOTIFIED before the module's next
                           2 ms poll samples it) and the dismissal arm missed
                           its moment: PREPARING forever, BACK dead. NOTIFIED
                           (a fresh offer standing after a failure) is equally
                           "the ladder ended" from THIS Run's perspective � the
                           offer belongs to the NEXT entry. */
        		{
        			APP_FOTA_NET_State_t Local_eOrch = APP_FOTA_NET_enumGetState();
        			if ((Local_eOrch == APP_FOTA_NET_STATE_IDLE) ||
        					(Local_eOrch == APP_FOTA_NET_STATE_NOTIFIED))
        			{
        				FA_u8DismissArm = 1u;
        				/* R5-FIX (edit 2): the honest screen � the ladder
                                   ended; guarantees something current stands even
                                   if the event sequence overwrote or skipped the
                                   orchestrator's error paint (the stuck-PREPARING
                                   symptom). 14 chars � fits. */
        				APP_FOTA_UI_Error("UPDATE STOPPED");
        			}
        		}
        		/* success: NetReset() never returns � the BL commits,
                           the new build's banner is the success screen */
        	}
        	break;

        default:
            break;
        }

        /* the heartbeat (Run owns its beat) */
        {
            static u32 Local_u32ShownHalf = 0u;
            if ((Local_u32Now / FA_HEARTBEAT_MS) != Local_u32ShownHalf)
            { Local_u32ShownHalf = Local_u32Now / FA_HEARTBEAT_MS;
              FA_voidToggleHeartbeat(); }
        }
    }
}

u16 APP_FOTA_APP_u16GetScore(void) { return 0u; }   /* updates are not games */
