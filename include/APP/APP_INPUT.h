/*
 * APP_INPUT.h — the ONE input stream every screen and game drinks
 *  from. The merge pump.
 *  LOCATION: APP/ (flat).
 *
 *  STAGE 2 CONTRACT (this revision): the pump owns a PRODUCER task
 *  (priority 3) that drains HNEC every 10 ms and posts events to a
 *  FreeRTOS queue (depth 8, by value). The consumer side — every
 *  menu and game — is UNCHANGED: same function, same types.
 *
 *  TIMING: GetEvent returns one event or none, blocking at most
 *  ~2 ms when idle (a queue receive with timeout). The Phase-1
 *  contract was a ~270 ms stall per press; every consumer in the
 *  tree was written stall-tolerant for THAT — 2 ms is invisible.
 *  Side effect (good): consumer loops pace at ~500 Hz instead of
 *  spinning, and the idle task runs between iterations.
 *
 *  INPUT LATENCY: fresh press <= ~78 ms (frame duration + one poll
 *  period); events BUFFER through long paints — a press during a
 *  45 ms repaint is queued, not lost or serviced late.
 *
 *  THE DRAIN-ORDER RULING (preserved, producer-side): when HButtons
 *  lands, its polling joins the input task BEFORE the HNEC drain —
 *  the physical button still "wins" the same frame. Only the .c
 *  changes that day (still true).
 */
#ifndef APP_INPUT_H_
#define APP_INPUT_H_
#include "STD_TYPES.h"
#include "HAL/HVirtualButtons_interface.h"

typedef enum
{
    APP_INPUT_OK = 0,
    APP_INPUT_NO_EVENT,
    APP_INPUT_INIT_FAILED      /* Stage 2: queue/task creation failed */
} APP_INPUT_EnumStatus_t;

typedef struct
{
    VirtualButton_t Button;
    u8              IsRepeat;
} APP_INPUT_Event_t;

/* Create the queue + spawn the input task. Call ONCE at boot, after
 * HNEC_REMOTE_enumInit and BEFORE vTaskStartScheduler. Idempotent. */
APP_INPUT_EnumStatus_t APP_INPUT_enumInit(void);

/* Returns one event or none. May block <= ~2 ms when idle (see the
 * Stage-2 contract above). Call every loop iteration, as ever.     */
APP_INPUT_EnumStatus_t APP_INPUT_enumGetEvent(APP_INPUT_Event_t *Copy_pstEvent);

#endif /* APP_INPUT_H_ */
