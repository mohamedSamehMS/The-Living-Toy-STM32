/*
 * APP_INPUT.c � THE INPUT PUMP, Stage 2 edition.
 *
 *  LINEAGE: Phase 1 � a thin forwarder; the polling loop drove HNEC
 *  and stalled ~270 ms per press. Gate D � the capture engine killed
 *  the stall; still a forwarder, still driven by whoever polled.
 *  Stage 2 (this file) � the pump grows its own task: a producer
 *  (priority 3) drains HNEC every 10 ms into a queue; consumers read
 *  through the unchanged GetEvent signature.
 *
 *  SEAM FACTS (audited): events cross by value; HNEC/HIR single-owner
 *  by structure; producer never blocks on a full queue (drops are
 *  counted, expected zero forever); no ISR touches the kernel.
 *
 *  Created on: Oct 1, 2026
 */
#include "STD_TYPES.h"
#include "APP/APP_INPUT.h"
#include "HAL/NEC/HNEC_REMOTE_interface.h"
#include "HAL/BUTTONS/HBUTTONS_interface.h"
#include "HAL/BUZZER/HBUZZER_interface.h"

#include <OS/FreeRTOS.h>
#include <OS/task.h>
#include <OS/queue.h>

/* DEBUG R4D: the NET task's high-water mark joins the [ST2] line */
extern TaskHandle_t NET_xTaskHandle;

/* ---- configuration ---- */
#define APP_INPUT_QUEUE_DEPTH         8u    /* >1s of un-drained repeats: health margin */
#define APP_INPUT_POLL_PERIOD_MS      10u   /* << NEC's ~110 ms cadence                   */
#define APP_INPUT_CONSUMER_TIMEOUT_MS 2u    /* the pacing block � see the .h contract     */
#define APP_INPUT_ENABLE_STATS        1     /* the Stage-2 instrument (0 = off)           */
#define APP_INPUT_STATS_PERIOD_MS     30000u

/* Hub task handle � created in main.c, read ONLY by the stats line. */
extern TaskHandle_t App_xHubTaskHandle;

static QueueHandle_t APP_INPUT_xEventQueue = NULL;
#if APP_INPUT_ENABLE_STATS
static volatile u32 APP_INPUT_u32DroppedEvents = 0u;
#endif

static void APP_INPUT_privatePost(VirtualButton_t Copy_eButton, u8 Copy_u8IsRepeat)
{
    APP_INPUT_Event_t Local_stEvt;
    Local_stEvt.Button = Copy_eButton;
    Local_stEvt.IsRepeat = Copy_u8IsRepeat;
    if (xQueueSend(APP_INPUT_xEventQueue, &Local_stEvt, 0) != pdTRUE)
    {
#if APP_INPUT_ENABLE_STATS
        APP_INPUT_u32DroppedEvents++;
#endif
    }
}

#if APP_INPUT_ENABLE_STATS
#include <MCAL/USART/MUSART_interface.h>

static void APP_INPUT_privatePrint(const char *Copy_pchStr)
{
    while (*Copy_pchStr != '\0')
    {
        (void)MUSART_SendByteBlocking(MUSART_USART2, (u8)(*Copy_pchStr));
        Copy_pchStr++;
    }
}

static void APP_INPUT_privatePrintU32(u32 Copy_u32Value)
{
    char Local_achDigits[10u];
    u8  Local_u8Count = 0u;

    if (Copy_u32Value == 0u) { APP_INPUT_privatePrint("0"); return; }
    while (Copy_u32Value != 0u)
    {
        Local_achDigits[Local_u8Count++] = (char)('0' + (Copy_u32Value % 10u));
        Copy_u32Value /= 10u;
    }
    while (Local_u8Count != 0u)
    {
        Local_u8Count--;
        (void)MUSART_SendByteBlocking(MUSART_USART2, (u8)Local_achDigits[Local_u8Count]);
    }
}
#endif /* APP_INPUT_ENABLE_STATS */

/* ---- THE PRODUCER: the input task ---------------------------------- */
static void APP_INPUT_privateTask(void *pvParameters)
{
    (void)pvParameters;
#if APP_INPUT_ENABLE_STATS
    u32 Local_u32LastStat = 0u;
#endif

    for (;;)
    {
        HBUTTONS_voidPoll();
        {
            u8 Local_u8ButtonIndex;
            while (HBUTTONS_enumGetEvent(&Local_u8ButtonIndex) == HBUTTONS_OK)
            {
                if (Local_u8ButtonIndex == HBUTTONS_BUTTON_CONFIRM)
                {
                    APP_INPUT_privatePost(BTN_CONFIRM, 0u);
                }
                else if (Local_u8ButtonIndex == HBUTTONS_BUTTON_BACK)
                {
                    APP_INPUT_privatePost(BTN_BACK, 0u);
                }
                else
                {
                    APP_INPUT_privatePost(BTN_DOWN, 0u);
                }
            }
        }

        /* Drain HNEC fully each wake (its pending slot is single):
           the HButtons slot goes HERE, before this loop, the day it
           lands � the drain-order ruling, producer-side.            */
        HNEC_REMOTE_Event_t Local_stEvent;
        while (HNEC_REMOTE_enumGetEvent(&Local_stEvent) == HNEC_REMOTE_OK)
        {
            APP_INPUT_privatePost(Local_stEvent.Button, Local_stEvent.IsRepeat);
        }

#if APP_INPUT_ENABLE_STATS
        /* The Stage-2 instrument: stack high-water (WORDS of headroom
           ever remaining) + queue health. hwHUB of 512-word stack ->
           data to shrink it at the next flash, from measurement.     */
        {
            u32 Local_u32Now = xTaskGetTickCount();
            if ((Local_u32Now - Local_u32LastStat) >= APP_INPUT_STATS_PERIOD_MS)
            {
                Local_u32LastStat = Local_u32Now;
                APP_INPUT_privatePrint("[ST2] hwHUB=");
                APP_INPUT_privatePrintU32(uxTaskGetStackHighWaterMark(App_xHubTaskHandle));
                APP_INPUT_privatePrint(" hwIN=");
                APP_INPUT_privatePrintU32(uxTaskGetStackHighWaterMark(NULL));
                APP_INPUT_privatePrint(" hwNET=");
                APP_INPUT_privatePrintU32(uxTaskGetStackHighWaterMark(NET_xTaskHandle));
                APP_INPUT_privatePrint(" hp=");
                APP_INPUT_privatePrintU32(xPortGetFreeHeapSize());

                APP_INPUT_privatePrint(" qdrop=");
                APP_INPUT_privatePrintU32(APP_INPUT_u32DroppedEvents);

                APP_INPUT_privatePrint("\r\n");
            }
        }
#endif

        vTaskDelay(pdMS_TO_TICKS(APP_INPUT_POLL_PERIOD_MS));
    }
}

/* ---- Init: queue + task, once, pre-scheduler ------------------------ */
APP_INPUT_EnumStatus_t APP_INPUT_enumInit(void)
{
    if (APP_INPUT_xEventQueue != NULL) { return APP_INPUT_OK; }   /* idempotent */

    APP_INPUT_xEventQueue = xQueueCreate(APP_INPUT_QUEUE_DEPTH,
                                         sizeof(APP_INPUT_Event_t));
    if (APP_INPUT_xEventQueue == NULL) { return APP_INPUT_INIT_FAILED; }

    if (HBUTTONS_enumInit() != HBUTTONS_OK) { return APP_INPUT_INIT_FAILED; }

    /* 256 words = 512 B (HNEC drain + queue send + one print line);
       priority 3 = INPUT, above the hub's 2 � the ladder's head.     */
    if (xTaskCreate(APP_INPUT_privateTask, "INPUT", 256u, NULL, 3u, NULL) != pdPASS)
    {
        return APP_INPUT_INIT_FAILED;
    }
    return APP_INPUT_OK;
}

/* ---- THE CONSUMER: every menu and game, signature unchanged --------- */
APP_INPUT_EnumStatus_t APP_INPUT_enumGetEvent(APP_INPUT_Event_t *Copy_pstEvent)
{
    if ((APP_INPUT_xEventQueue == NULL) || (Copy_pstEvent == NULL))
    {
        return APP_INPUT_NO_EVENT;   /* not initialized / bad argument */
    }

    if (xQueueReceive(APP_INPUT_xEventQueue, Copy_pstEvent,
                      pdMS_TO_TICKS(APP_INPUT_CONSUMER_TIMEOUT_MS)) == pdTRUE)
    {
        /* Owner-ruled input feedback: one blip per FRESH press � repeats
         * are silent (a hold-glide blips once; the visual cursor carries
         * the glide). Arbitration is emergent: FOTA's MEDIUM tones
         * preempt this LOW blip inside HBUZZER � no FOTA-state query.
         * BUSY is the rule working; the status is ignored by design.  */
        if (Copy_pstEvent->IsRepeat == 0u)
        {
            (void)HBUZZER_enumBlip();
        }
        return APP_INPUT_OK;
    }
    return APP_INPUT_NO_EVENT;       /* idle � the 2 ms pacing block */
}
