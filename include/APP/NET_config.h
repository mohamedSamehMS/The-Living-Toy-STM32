/*
 * NET_config.h � tunables of the Network Driver.
 *  LOCATION: APP/NET/.
 *
 *  S5 (constants follow their consumer): protocol / connection
 *  timing lives HERE. Session-state UX timing (turn timeout,
 *  grace windows, exit-confirm, message hold) and game tunables
 *  stay in APP/TTT_config.h. In the session delivery,
 *  TTT_ONLINE_CMD_TIMEOUT_MS is REMOVED from TTT_config.h �
 *  this constant is its new home.
 */

#ifndef NET_CONFIG_H_
#define NET_CONFIG_H_

#include <MCAL/USART/MUSART_interface.h>   /* NET_LINK_USART / NET_CONSOLE_USART */

/* ---- the link ---- */
#define NET_LINK_USART          MUSART_USART1   /* PA9/PA10 � the ESP          */
#define NET_CONSOLE_USART       MUSART_USART2   /* narration � MUST match
                                                   HESP_TX_MIRROR_USART and the
                                                   boot console (PA2/PA3)       */

/* ---- queues (values verbatim from the delivered session) ---- */
#define NET_LINE_MAX            160u
#define NET_LINE_QUEUE_DEPTH    4u

/* ---- D15: the torn-copy guard between the two EVENT_STM_READY
   copies. >= 80 ms per the witness; 100 as delivered. ---- */
#define NET_D15_GAP_MS          100u

/* ---- command response timeout ----
   B1/Q3 (carried from TTT_config.h verbatim): was 3000
   (calibrated on pre-merge instant NOT_READY answers). Merged
   round trips are 1.1-1.7 s typical, 10.2 s worst observed �
   3 s would fire mid-round-trip and discard slow-but-successful
   terminals as late replies. Now sits ABOVE the ESP's
   recommended TIMEOUT deadline (10-15 s) as belt-and-braces
   for a silent ESP; inside the 30 s connect window (one slow
   attempt + one retry fits). Pre-merge behavior unchanged:
   v0.1.4 answers instantly, the terminal always wins. */
#define NET_CMD_TIMEOUT_MS      20000u


/* ---- FOTA-CS: the epoch watchdog (Sheet C's number, countersigned) ----
   90 s from the LAST RECEIVED BYTE (the pinned semantics � FOTA-15/18:
   "zero bytes" means zero bytes; the sender's own retry budget
   (2.0 s x 30 = 60 s worst case per the ESP's ruling) always aborts
   first in every realistic failure � this is the backstop for a
   silently-dead peer. NOTE: consumes GetElapsed_ms � carries the
   FOTA-06 double-read confirm on the FIRING path (the registered
   interim mitigation; the root fix is the MCAL seat's). */
#define NET_FOTA_EPOCH_WATCHDOG_MS   90000u



/* ---- firmware id in EVENT_STM_READY (the boot emission) ----
   The tie has arrived (FOTA-13's flag): this must MATCH
   APP_BUILD_VERSION_STR in main.c � the notify's version compare
   consumes this truth at the hub. Bump per versioned image. */
#define NET_FW_ID               "14.0.0"   /* the absorption build � the
                                              versioned-image rule (ties to
                                              APP_BUILD_VERSION_STR in main) */

/* ABSORPTION: the event queue is a FreeRTOS queue now � depth 8 absorbs
   the events that arrive while no TTT Run drains (Snake sessions); the
   session's state gating absorbs the stale burst on re-entry. */
#define NET_EVENT_QUEUE_DEPTH   8u
#define NET_CMD_QUEUE_DEPTH     4u

#endif /* NET_CONFIG_H_ */
