/*
 * NET_program.c — the Network Driver (L1 protocol + connection
 * state), implementation. LOCATION: APP/NET/.
 *
 *  PROVENANCE [MOVE]: pure moves from the delivered TTT_session.c
 *  (rev 4+fix) and main.c v1.3 rev 5 (the narration); the marked
 *  changes M1-M6 annotated at their sites.
 *
 *  B1: the teardown + the recovery (the four branches, the link
 *  state's LOST arm, SocketIsNew).
 *
 *  FOTA-CS + SHEET C RIPPLE: the FOTA family per Sheet C (FROZEN —
 *  the owner's adoption + the ESP seat's countersign) — the parser
 *  branches, the two transactions, the 90 s zero-byte epoch watchdog
 *  (FOTA-06 double-read confirm on the firing path), Addendum A.
 *  NET owns the wire mode and NEVER calls into APP_FOTA.
 *
 *  THE RTOS CONVERSION (the TTT absorption, R4): the wire owner is a
 *  DEDICATED NET TASK (priority 1). HESP's TX builder: single writer
 *  again; the pending/TID machinery: single context; the event path:
 *  a FreeRTOS queue. What crosses tasks: TWO queues (commands
 *  HUB->NET, events NET->HUB, both by value) and three atomic-u8
 *  flags. The parser's RESP_MATCH_STATE writes are critical-section
 *  guarded. THE DEFERRED LINK: USART1's entire bring-up lives inside
 *  the bring-up command — the online entry's act, never boot.
 *
 *  R5 (the FOTA wiring): + NET_CMD_SOLICIT (the offer solicitation —
 *  the D15 re-emit; the ESP re-offers at STM_READY/reconnect only)
 *  and NET_CMD_ESP_FRESET (the hub-day stuck-stage recovery — the
 *  ESP seat's bench lines originated on the wire; both confirmations
 *  granted, v0.4.1-traced: the bare word passes the console gate
 *  with debug narration armed; zero settle time; the RAM offer
 *  survives the wipe).
 */

#include "STD_TYPES.h"

#include <MCAL/SYSTICK/MSYSTICK_interface.h>
#include <MCAL/USART/MUSART_interface.h>

#include <MCAL/RCC/MRCC_interface.h>      /* bring-up: the USART1 clock */
#include <MCAL/GPIO/MGPIO_interface.h>    /* bring-up: PA9/PA10 AF7     */
#include "../MNVIC_interface.h"              /* bring-up: the priority     */

#include <OS/FreeRTOS.h>
#include <OS/task.h>
#include <OS/queue.h>

#include "APP/NET_interface.h"
#include "APP/NET_config.h"
#include "APP/NET_private.h"

#include <HAL/ESP/HESP_interface.h>

/* ================================================================= *
 *  SECTION 1 — PROTOCOL                                             *
 * ================================================================= */

static char Global_chLines[NET_LINE_QUEUE_DEPTH][NET_LINE_MAX + 1u];
static u8   Global_u8LineHead;
static u8   Global_u8LineCount;
static u8   Global_u8LinesDropped;

/* ABSORPTION: the event queue is a FreeRTOS queue — NET-task-produced
 * (parser / TX_FAIL / timeout / watchdog — all single-context), HUB-
 * consumed. The hand-rolled array is retired. u32 counter: the
 * nobody-drains-during-other-games era can drop more than 255. */
static u32 Global_u8EventsDropped;

typedef struct
{
    u8  Active;
    u8  Tid;
    u32 SentAt;
    char Cmd[20];
} NET_Pending_t;
static NET_Pending_t Global_stPending;
static u8 Global_u8NextTid = 1u;
static u8 Global_u8QuarantineTid = 0u;

static NET_State_t Global_stState;
static u8 Global_u8Initialized = 0u;
static u8 Global_u8EspReady = 0u;
static u32 Global_u32LateReplies = 0u;

/* The join-argument echo (M2) */
static char Global_chLastCode[8];

/* ---- B1: the connection-state flags ---- */
static u8 Global_u8LinkLost  = 0u;
static u8 Global_u8SocketNew = 0u;

/* ---- FOTA-CS: the epoch state ---- */
static u8 Global_u8FotaEpoch = 0u;

/* ---- THE RTOS CONVERSION: the crossing state ---- */
static QueueHandle_t Global_xCmdQueue = NULL;
static QueueHandle_t Global_xEvtQueue = NULL;
static u8 Global_u8BroughtUp = 0u;

TaskHandle_t NET_xTaskHandle = NULL;   /* DEBUG R4D: non-static — the [ST2] line reads it */

#define NET_TASK_PERIOD_MS    10u    /* the pump's pacing — against the
                                        1.1-1.7 s RTT, +5 ms is noise   */
#define NET_TASK_STACK_WORDS  512u   /* 2 KB: the parser's ~450 B of locals
                                        + the kernel send paths, generous */
#define NET_TASK_PRIORITY     1u     /* the comm class — the ladder      */

/* ---- the line hook (loop context, from inside TransportPoll) ---- */
static void NET_voidLineHook(const u8 *Copy_pu8Line, u16 Copy_u16Len)
{
    u16 Local_u16Idx = 0u;
    char *Local_chDst;

    if (Global_u8LineCount >= NET_LINE_QUEUE_DEPTH)
    {
        Global_u8LinesDropped++;
        return;
    }
    Local_chDst = Global_chLines[(u8)((Global_u8LineHead + Global_u8LineCount) % NET_LINE_QUEUE_DEPTH)];
    while ((Local_u16Idx < Copy_u16Len) && (Local_u16Idx < NET_LINE_MAX))
    {
        Local_chDst[Local_u16Idx] = (char)Copy_pu8Line[Local_u16Idx];
        Local_u16Idx++;
    }
    Local_chDst[Local_u16Idx] = '\0';
    Global_u8LineCount++;
}

/* ---- TID allocation (quarantine-aware, 1-255 wrap) ---- */
static u8 NET_u8AllocTid(void)
{
    u8 Local_u8Tid = Global_u8NextTid;

    if (Local_u8Tid == Global_u8QuarantineTid)
    {
        Local_u8Tid = (u8)(Local_u8Tid + 1u);
        if (Local_u8Tid == 0u)
        {
            Local_u8Tid = 1u;
        }
    }
    Global_u8NextTid = (u8)(Local_u8Tid + 1u);
    if (Global_u8NextTid == 0u)
    {
        Global_u8NextTid = 1u;
    }
    return Local_u8Tid;
}

/* ---- send + pending (one transaction; a new command abandons
   a live one — its reply lands in the late counter). NET-TASK
   CONTEXT ONLY. ---- */
static NET_EnumStatus_t NET_ENUM_SEND_CMD(const char *Copy_chName, u8 Copy_u8Tid,
                                          const char *Copy_chField1,
                                          const char *Copy_chField2)
{
    HESP_Error_t Local_eError;
    NET_Event_t  Local_stEvent;
    u8 i;

    if (Global_u8Initialized == 0u)
    {
        return NET_NOK;
    }

    Local_eError = HESP_TxBegin(Copy_chName, Copy_u8Tid);
    if ((Local_eError == HESP_OK) && (Copy_chField1 != 0))
    {
        Local_eError = HESP_TxFieldStr(Copy_chField1);
    }
    if ((Local_eError == HESP_OK) && (Copy_chField2 != 0))
    {
        Local_eError = HESP_TxFieldStr(Copy_chField2);
    }
    if (Local_eError == HESP_OK)
    {
        Local_eError = HESP_TxSend();
    }

    if (Local_eError != HESP_OK)
    {
        for (i = 0u; i < sizeof(NET_Event_t); i++)
        {
            ((u8 *)&Local_stEvent)[i] = 0u;
        }
        Local_stEvent.Type = NET_EVT_RESP_ERR;
        NET_voidCopyBounded(Local_stEvent.Reason, sizeof(Local_stEvent.Reason), "TX_FAIL");
        NET_voidEnqueue(&Local_stEvent);
        return NET_NOK;
    }

    Global_stPending.Active = 1u;
    Global_stPending.Tid    = Copy_u8Tid;
    Global_stPending.SentAt = MSYSTICK_GetElapsed_ms();
    NET_voidCopyBounded(Global_stPending.Cmd, sizeof(Global_stPending.Cmd), Copy_chName);
    return NET_OK;
}

/* FOTA-CS: decimal field -> u32 (bounded; saturates, never wraps) */
static u32 NET_u32ParseDec(const char *Copy_chSrc)
{
    u32 Local_u32Value = 0u;

    while ((*Copy_chSrc >= '0') && (*Copy_chSrc <= '9'))
    {
        if (Local_u32Value <= (0xFFFFFFFFu / 10u))
        {
            Local_u32Value = (Local_u32Value * 10u) + (u32)(*Copy_chSrc - '0');
        }
        Copy_chSrc++;
    }
    return Local_u32Value;
}

/* FOTA-CS: the epoch's single close path — wire mode to text. */
static void NET_voidFotaClose(void)
{
    if (Global_u8FotaEpoch != 0u)
    {
        HESP_SetBinaryMode(0u);
        Global_u8FotaEpoch = 0u;
    }
}

/* ---- the parser ---- */
static void NET_voidParseLine(const char *Copy_chLine)
{
    char Local_chName[24];
    char Local_AchFields[7][36];
    u8   Local_u8Name = 0u;
    u8   Local_u8F = 0u;
    u8   Local_u8IsResp = 0u;
    u8   Local_u8Tid = 0u;
    const char *Local_pch = Copy_chLine;
    NET_Event_t Local_stEvent;

    for (u8 i = 0u; i < sizeof(NET_Event_t); i++)
    {
        ((u8 *)&Local_stEvent)[i] = 0u;
    }

    /* F1: zero ALL field slots first. */
    for (u8 f = 0u; f < 7u; f++)
    {
        Local_AchFields[f][0] = '\0';
    }

    /* Name up to the first ':'. */
    while ((*Local_pch != '\0') && (*Local_pch != ':') &&
           (Local_u8Name < (sizeof(Local_chName) - 1u)))
    {
        Local_chName[Local_u8Name++] = *Local_pch++;
    }
    Local_chName[Local_u8Name] = '\0';
    if (*Local_pch == ':')
    {
        Local_pch++;
    }

    /* RESP_ prefix (covers RESP_ERR too). */
    if ((Local_u8Name > 5u) && (Local_chName[0] == 'R') &&
        (Local_chName[1] == 'E') && (Local_chName[2] == 'S') &&
        (Local_chName[3] == 'P') && (Local_chName[4] == '_'))
    {
        Local_u8IsResp = 1u;
    }

    if (Local_u8IsResp != 0u)
    {
        while ((*Local_pch >= '0') && (*Local_pch <= '9'))
        {
            Local_u8Tid = (u8)((Local_u8Tid * 10u) + (u8)(*Local_pch++ - '0'));
        }
        if (*Local_pch == ':')
        {
            Local_pch++;
        }

        if ((Global_stPending.Active == 0u) ||
            (Global_stPending.Tid != Local_u8Tid))
        {
            Global_u32LateReplies++;
            return;
        }
        Global_stPending.Active = 0u;
    }

    /* Fields: comma-separated. */
    while ((*Local_pch != '\0') && (Local_u8F < 7u))
    {
        u8 Local_u8Len = 0u;
        while ((*Local_pch != '\0') && (*Local_pch != ',') &&
               (Local_u8Len < (sizeof(Local_AchFields[0]) - 1u)))
        {
            Local_AchFields[Local_u8F][Local_u8Len++] = *Local_pch++;
        }
        Local_AchFields[Local_u8F][Local_u8Len] = '\0';
        if (*Local_pch == ',')
        {
            Local_pch++;
        }
        Local_u8F++;
    }

    /* ---- link-level: the ESP's boot announcement. ---- */
    if (NET_u8StrEq(Local_chName, "EVENT_ESP_READY"))
    {
        if (Local_AchFields[0][0] == '1')
        {
            if (Global_u8EspReady != 0u)
            {
                Global_u8SocketNew = 1u;   /* B1 */
            }
            Global_u8EspReady = 1u;
        }
        return;
    }

    /* ---- link-level: the probe's answer. ---- */
    if (NET_u8StrEq(Local_chName, "RESP_STATUS"))
    {
        Global_u8EspReady = 1u;
        return;
    }

    /* ---- dispatch by frame name ---- */
    if (NET_u8StrEq(Local_chName, "RESP_ROOM_CREATED"))
    {
        Local_stEvent.Type = NET_EVT_ROOM_CREATED;
        NET_voidCopyBounded(Local_stEvent.Code,  sizeof(Local_stEvent.Code),  Local_AchFields[0]);
        NET_voidCopyBounded(Local_stEvent.Token, sizeof(Local_stEvent.Token), Local_AchFields[1]);
        Local_stEvent.Mark = (u8)Local_AchFields[2][0];
    }
    else if (NET_u8StrEq(Local_chName, "RESP_ROOM_JOINED"))
    {
        Local_stEvent.Type = NET_EVT_ROOM_JOINED;
        Local_stEvent.Mark = (u8)Local_AchFields[0][0];
        Local_stEvent.MyTurn = (Local_AchFields[1][0] == '1') ? 1u : 0u;
        NET_voidCopyBounded(Local_stEvent.Token, sizeof(Local_stEvent.Token), Local_AchFields[2]);
        NET_voidCopyBounded(Local_stEvent.Code, sizeof(Local_stEvent.Code), Global_chLastCode);
    }
    else if (NET_u8StrEq(Local_chName, "RESP_MATCH_STATE"))
    {
        Local_stEvent.Type = NET_EVT_STATE;
        /* ABSORPTION: the LOW-priority writer (NET, prio 1) must not be
         * preempted mid-struct by the HIGH-priority reader (HUB, prio 2)
         * — the critical section masks the tick, making the write
         * sequence atomic-as-a-whole. The reader needs no guard: NET
         * never preempts HUB. Readers see old-or-new, never torn.     */
        taskENTER_CRITICAL();
        {
            Global_u8SocketNew = 0u;   /* B1 */
            if (NET_u8StrEq(Local_AchFields[0], "WAITING"))
            {
                Global_stState.Status = NET_STATUS_WAITING;
            }
            else if (NET_u8StrEq(Local_AchFields[0], "ACTIVE"))
            {
                Global_stState.Status = NET_STATUS_ACTIVE;
            }
            else if (NET_u8StrEq(Local_AchFields[0], "FINISHED"))
            {
                Global_stState.Status = NET_STATUS_FINISHED;
            }
            else
            {
                Global_stState.Status = NET_STATUS_UNKNOWN;
            }

            for (u8 i = 0u; i < 9u; i++)
            {
                Global_stState.Board[i] = Local_AchFields[1][i];
            }
            Global_stState.Board[9] = '\0';

            Global_stState.MyMark = (u8)Local_AchFields[2][0];
            Global_stState.MyTurn = (Local_AchFields[3][0] == '1') ? 1u : 0u;
            NET_voidCopyBounded(Global_stState.OppName,
                                sizeof(Global_stState.OppName), Local_AchFields[4]);
            Global_stState.OppConnected = (Local_AchFields[5][0] == '1') ? 1u : 0u;
            Global_stState.Winner = (u8)Local_AchFields[6][0];
        }
        taskEXIT_CRITICAL();
    }
    else if (NET_u8StrEq(Local_chName, "RESP_MOVE_OK"))
    {
        Local_stEvent.Type = NET_EVT_MOVE_OK;
        Local_stEvent.Cell = (u8)(Local_AchFields[0][0] - '0');
        Local_stEvent.Mark = (u8)Local_AchFields[1][0];
    }
    else if (NET_u8StrEq(Local_chName, "RESP_MOVE_REJECTED"))
    {
        Local_stEvent.Type = NET_EVT_MOVE_REJECTED;
        NET_voidCopyBounded(Local_stEvent.Reason, sizeof(Local_stEvent.Reason), Local_AchFields[0]);
    }
    else if (NET_u8StrEq(Local_chName, "RESP_REMATCH"))
    {
        Local_stEvent.Type = NET_EVT_REMATCH_OFFERED;
    }
    else if (NET_u8StrEq(Local_chName, "RESP_ERR"))
    {
        if (NET_u8StrEq(Local_AchFields[0], "ROOM_NOT_FOUND"))
        {
            Local_stEvent.Type = NET_EVT_ROOM_GONE;
        }
        else
        {
            Local_stEvent.Type = NET_EVT_RESP_ERR;
            NET_voidCopyBounded(Local_stEvent.Reason, sizeof(Local_stEvent.Reason), Local_AchFields[0]);
        }
    }
    else if (NET_u8StrEq(Local_chName, "EVENT_OPPONENT_MOVE"))
    {
        Local_stEvent.Type = NET_EVT_OPP_MOVE;
        Local_stEvent.Cell = (u8)(Local_AchFields[0][0] - '0');
        Local_stEvent.Mark = (u8)Local_AchFields[1][0];
    }
    else if (NET_u8StrEq(Local_chName, "EVENT_OPPONENT_JOINED"))
    {
        Local_stEvent.Type = NET_EVT_OPP_JOINED;
        NET_voidCopyBounded(Local_stEvent.Name, sizeof(Local_stEvent.Name), Local_AchFields[0]);
        Local_stEvent.MyTurn = (Local_AchFields[1][0] == '1') ? 1u : 0u;
    }
    else if (NET_u8StrEq(Local_chName, "EVENT_OPPONENT_LEFT"))
    {
        Local_stEvent.Type = NET_EVT_OPP_LEFT;
        NET_voidCopyBounded(Local_stEvent.Reason, sizeof(Local_stEvent.Reason), Local_AchFields[0]);

        for (u8 i = 0u; (i < 5u) && (Local_AchFields[1][i] >= '0') && (Local_AchFields[1][i] <= '9'); i++)
        {
            Local_stEvent.GraceS = (u16)(Local_stEvent.GraceS * 10u + (u16)(Local_AchFields[1][i] - '0'));
        }
    }
    else if (NET_u8StrEq(Local_chName, "EVENT_MATCH_ENDED"))
    {
        Local_stEvent.Type = NET_EVT_MATCH_ENDED;
        Local_stEvent.Winner = (u8)Local_AchFields[0][0];
        NET_voidCopyBounded(Local_stEvent.Reason, sizeof(Local_stEvent.Reason), Local_AchFields[1]);
        NET_voidCopyBounded(Local_stEvent.Board, sizeof(Local_stEvent.Board), Local_AchFields[2]);
        NET_voidCopyBounded(Local_stEvent.Line,  sizeof(Local_stEvent.Line),  Local_AchFields[3]);
    }
    else if (NET_u8StrEq(Local_chName, "EVENT_REMATCH_OFFERED"))
    {
        Local_stEvent.Type = NET_EVT_REMATCH_OFFERED;
    }
    else if (NET_u8StrEq(Local_chName, "EVENT_REMATCH_STARTED"))
    {
        Local_stEvent.Type = NET_EVT_REMATCH_STARTED;
    }
    else if (NET_u8StrEq(Local_chName, "EVENT_OPPONENT_REJOINED"))
    {
        Local_stEvent.Type = NET_EVT_REJOINED;
    }
    else if (NET_u8StrEq(Local_chName, "EVENT_ROOM_EXPIRED"))
    {
        Local_stEvent.Type = NET_EVT_ROOM_GONE;
    }
    else if (NET_u8StrEq(Local_chName, "RESP_ROOM_LEFT"))
    {
        Local_stEvent.Type = NET_EVT_ROOM_LEFT;
    }
    else if (NET_u8StrEq(Local_chName, "RESP_NET_DISCONNECTED"))
    {
        Local_stEvent.Type = NET_EVT_NET_DISCONNECTED;
    }
    else if (NET_u8StrEq(Local_chName, "EVENT_NET_LOST"))
    {
        Local_stEvent.Type = NET_EVT_LINK_LOST;
        Global_u8LinkLost  = 1u;
        Global_u8SocketNew = 1u;
    }
    else if (NET_u8StrEq(Local_chName, "EVENT_NET_BACK"))
    {
        Local_stEvent.Type = NET_EVT_LINK_BACK;
        Global_u8LinkLost = 0u;
    }
    /* ================= FOTA-CS: the FOTA family ================= */
    else if (NET_u8StrEq(Local_chName, "EVENT_FOTA_AVAILABLE"))
    {
        Local_stEvent.Type = NET_EVT_FOTA_AVAILABLE;
        NET_voidCopyBounded(Local_stEvent.Fota.Version,
                            sizeof(Local_stEvent.Fota.Version), Local_AchFields[0]);
        Local_stEvent.Fota.Length = NET_u32ParseDec(Local_AchFields[1]);
        Local_stEvent.Fota.E2eCrc = NET_u32ParseDec(Local_AchFields[2]);
    }
    else if (NET_u8StrEq(Local_chName, "RESP_FOTA_READY"))
    {
        Local_stEvent.Type = NET_EVT_FOTA_READY;
        HESP_SetBinaryMode(1u);
        Global_u8FotaEpoch = 1u;
    }
    else if (NET_u8StrEq(Local_chName, "RESP_FOTA_DONE"))
    {
        Local_stEvent.Type = NET_EVT_FOTA_DONE;
        NET_voidFotaClose();
    }
    /* =============== end FOTA-CS parser branches ================ */
    else
    {
        return;
    }

    NET_voidEnqueue(&Local_stEvent);
}

/* ---- event queue (the FreeRTOS send: by value, 0-timeout, counted) ---- */
static void NET_voidEnqueue(NET_Event_t *Copy_pstEvent)
{
    if (Global_xEvtQueue == NULL)
    {
        return;
    }
    if (xQueueSend(Global_xEvtQueue, Copy_pstEvent, 0) != pdTRUE)
    {
        Global_u8EventsDropped++;
    }
}

/* ---- helpers ---- */
static u8 NET_u8StrEq(const char *Copy_chA, const char *Copy_chB)
{
    u8 Local_u8Idx = 0u;

    while ((Copy_chA[Local_u8Idx] != '\0') && (Copy_chB[Local_u8Idx] != '\0') &&
           (Copy_chA[Local_u8Idx] == Copy_chB[Local_u8Idx]))
    {
        Local_u8Idx++;
    }
    return (Copy_chA[Local_u8Idx] == Copy_chB[Local_u8Idx]) ? 1u : 0u;
}

static void NET_voidCopyBounded(char *Copy_chDst, u8 Copy_u8DstSize, const char *Copy_chSrc)
{
    u8 Local_u8Idx = 0u;

    while ((Local_u8Idx + 1u) < Copy_u8DstSize && Copy_chSrc[Local_u8Idx] != '\0')
    {
        Copy_chDst[Local_u8Idx] = Copy_chSrc[Local_u8Idx];
        Local_u8Idx++;
    }
    Copy_chDst[Local_u8Idx] = '\0';
}

/* ================================================================= *
 *  THE RTOS CONVERSION — bring-up, executor, task, spawn            *
 * ================================================================= */

/* ---- the deferred link (the online entry's command; idempotent) --- */
static void NET_PRIVATE_Bringup(void)
{
    u8 Local_u8Idx;

    if (Global_u8BroughtUp != 0u)
    {
        return;
    }

    (void)MRCC_EnablePeripheralClock(MRCC_USART1);
    (void)MGPIO_SetPinMode        (MGPIO_PORTA, MGPIO_PIN_9,  MGPIO_MODE_ALT_FUNC);
    (void)MGPIO_SetPinOutputConfig(MGPIO_PORTA, MGPIO_PIN_9,  MGPIO_OUTPUT_PUSH_PULL, MGPIO_SPEED_HIGH);
    (void)MGPIO_SetPinAltFunction (MGPIO_PORTA, MGPIO_PIN_9,  MGPIO_AF7);
    (void)MGPIO_SetPinMode        (MGPIO_PORTA, MGPIO_PIN_10, MGPIO_MODE_ALT_FUNC);
    (void)MGPIO_SetPinOutputConfig(MGPIO_PORTA, MGPIO_PIN_10, MGPIO_OUTPUT_PUSH_PULL, MGPIO_SPEED_HIGH);
    (void)MGPIO_SetPinAltFunction (MGPIO_PORTA, MGPIO_PIN_10, MGPIO_AF7);

    (void)MNVIC_SetPriority(MNVIC_USART1, 5u, 0u);   /* ruling B */

    if (MUSART_Init(NET_LINK_USART) != MUSART_OK)
    {
        return;
    }

    Global_u8LineHead        = 0u;
    Global_u8LineCount       = 0u;
    Global_stPending.Active  = 0u;
    Global_u8NextTid         = 1u;
    Global_u8QuarantineTid   = 0u;
    Global_u8EspReady        = 0u;
    Global_chLastCode[0]     = '\0';
    Global_stState.Status    = NET_STATUS_UNKNOWN;
    Global_stState.Board[0]  = '\0';
    Global_u8LinkLost        = 0u;   /* B1 */
    Global_u8SocketNew       = 0u;   /* B1 */
    Global_u8FotaEpoch       = 0u;   /* FOTA-CS */
    Global_u8Initialized     = 1u;

    HESP_TransportInit(NET_LINK_USART, NET_voidLineHook);

    /* D15: the double EVENT_STM_READY — TASK-PACED. */
    for (Local_u8Idx = 0u; Local_u8Idx < 2u; Local_u8Idx++)
    {
        (void)HESP_TxBegin("EVENT_STM_READY", 0u);
        (void)HESP_TxFieldStr("1");
        (void)HESP_TxFieldStr(NET_FW_ID);
        (void)HESP_TxSend();
        if (Local_u8Idx == 0u)
        {
            vTaskDelay(pdMS_TO_TICKS(NET_D15_GAP_MS));
        }
    }

    Global_u8BroughtUp = 1u;
}

/* ---- the executor: descriptor -> wire (all in NET-task context) ---- */
static void NET_PRIVATE_ExecuteCmd(const NET_Cmd_t *Copy_pstCmd)
{
    char Local_achCell[2];
    char Local_achSize[11];
    char Local_achCrc[11];
    u8   Local_u8Len;

    switch (Copy_pstCmd->Type)
    {
    case NET_CMD_BRINGUP:
        NET_PRIVATE_Bringup();
        break;

    case NET_CMD_SOLICIT:
        /* R5: the D15 pair, verbatim — the ESP's re-offer trigger.
         * (Idempotent-safe: harmless when the link is already up;
         * the ESP re-offers on each STM_READY — the second of the pair
         * yields a refresh offer, not a duplicate ceremony.)        */
        {
            u8 Local_u8Idx;
            for (Local_u8Idx = 0u; Local_u8Idx < 2u; Local_u8Idx++)
            {
                (void)HESP_TxBegin("EVENT_STM_READY", 0u);
                (void)HESP_TxFieldStr("1");
                (void)HESP_TxFieldStr(NET_FW_ID);
                (void)HESP_TxSend();
                if (Local_u8Idx == 0u)
                {
                    vTaskDelay(pdMS_TO_TICKS(NET_D15_GAP_MS));
                }
            }
        }
        break;

    case NET_CMD_ESP_FRESET:
        /* R5: THE HUB-DAY FRESET — the ESP seat's stuck-stage recovery,
         * both confirmations granted (v0.4.1-traced). The bench form's
         * two lines, originated on the wire: the DEBUG-arming
         * transaction first (the console gate's load-bearing condition),
         * then the literal word as a bare EVENT-shaped line (no prefix,
         * no TID — passes the console gate by direct string-compare;
         * single-threaded line processing guarantees the ordering; the
         * CMD_DEBUG terminal is emitted before FWRESET is even read).
         * The wipe is synchronous and complete before the handler
         * returns — a BEGIN in the very next line is legal (N=0).
         * The RAM offer SURVIVES (only /fw.bin, /fw_meta.bin, and the
         * staging struct are wiped) — the next BEGIN re-fetches clean. */
        (void)NET_ENUM_SEND_CMD("CMD_DEBUG", NET_u8AllocTid(), "1", 0);
        (void)HESP_TxBegin("FWRESET", 0u);
        (void)HESP_TxSend();
        break;

    case NET_CMD_PROBE:
        (void)NET_ENUM_SEND_CMD("CMD_STATUS", NET_u8AllocTid(), 0, 0);
        break;

    case NET_CMD_HOST:
        (void)NET_ENUM_SEND_CMD("CMD_ROOM_CREATE", NET_u8AllocTid(),
                                Copy_pstCmd->Arg1, Copy_pstCmd->Arg2);
        break;

    case NET_CMD_JOIN:
        NET_voidCopyBounded(Global_chLastCode, sizeof(Global_chLastCode),
                            Copy_pstCmd->Arg1);
        (void)NET_ENUM_SEND_CMD("CMD_ROOM_JOIN", NET_u8AllocTid(),
                                Global_chLastCode, Copy_pstCmd->Arg2);
        break;

    case NET_CMD_RECONNECT:
        (void)NET_ENUM_SEND_CMD("CMD_ROOM_RECONNECT", NET_u8AllocTid(),
                                Copy_pstCmd->Arg1, Copy_pstCmd->Arg2);
        break;

    case NET_CMD_MOVE:
        Local_achCell[0] = (char)('0' + (Copy_pstCmd->Cell % 10u));
        Local_achCell[1] = '\0';
        (void)NET_ENUM_SEND_CMD("CMD_MOVE_SEND", NET_u8AllocTid(),
                                Local_achCell, 0);
        break;

    case NET_CMD_STATE_GET:
        (void)NET_ENUM_SEND_CMD("CMD_STATE_GET", NET_u8AllocTid(), 0, 0);
        break;

    case NET_CMD_LEAVE:
        (void)NET_ENUM_SEND_CMD("CMD_ROOM_LEAVE", NET_u8AllocTid(), 0, 0);
        break;

    case NET_CMD_FOTA_BEGIN:
    {
        HESP_Error_t Local_eError;
        u8 Local_u8Tid;

        Local_u8Len = NET_u8AppendU32(Local_achSize, 0u, Copy_pstCmd->Size);
        Local_achSize[Local_u8Len] = '\0';
        Local_u8Len = NET_u8AppendU32(Local_achCrc,  0u, Copy_pstCmd->E2eCrc);
        Local_achCrc[Local_u8Len] = '\0';

        Local_u8Tid = NET_u8AllocTid();
        Local_eError = HESP_TxBegin("CMD_FOTA_BEGIN", Local_u8Tid);
        if (Local_eError == HESP_OK) { Local_eError = HESP_TxFieldStr(Copy_pstCmd->Arg1); }
        if (Local_eError == HESP_OK) { Local_eError = HESP_TxFieldStr(Local_achSize); }
        if (Local_eError == HESP_OK) { Local_eError = HESP_TxFieldStr(Local_achCrc); }
        if (Local_eError == HESP_OK) { Local_eError = HESP_TxSend(); }

        if (Local_eError != HESP_OK)
        {
            NET_Event_t Local_stEvent;
            u8 i;
            for (i = 0u; i < sizeof(NET_Event_t); i++)
            {
                ((u8 *)&Local_stEvent)[i] = 0u;
            }
            Local_stEvent.Type = NET_EVT_RESP_ERR;
            NET_voidCopyBounded(Local_stEvent.Reason,
                                sizeof(Local_stEvent.Reason), "TX_FAIL");
            NET_voidEnqueue(&Local_stEvent);
            break;
        }
        Global_stPending.Active = 1u;
        Global_stPending.Tid    = Local_u8Tid;
        Global_stPending.SentAt = MSYSTICK_GetElapsed_ms();
        NET_voidCopyBounded(Global_stPending.Cmd,
                            sizeof(Global_stPending.Cmd), "CMD_FOTA_BEGIN");
        break;
    }

    case NET_CMD_FOTA_DONE:
        (void)NET_ENUM_SEND_CMD("CMD_FOTA_DONE", NET_u8AllocTid(),
                                Copy_pstCmd->Arg1, 0);
        break;

    default:
        break;
    }
}

/* ---- THE NET TASK: the wire owner (priority 1) --------------------- */
static void NET_PRIVATE_Task(void *pvParameters)
{
    NET_Cmd_t Local_stCmd;
    (void)pvParameters;

    for (;;)
    {
        /* ---- the wire (the old enumTask body, verbatim) ---- */
        HESP_TransportPoll();

        if (Global_u8Initialized != 0u)
        {
            while (Global_u8LineCount != 0u)
            {
                NET_voidParseLine(Global_chLines[Global_u8LineHead]);
                Global_chLines[Global_u8LineHead][0] = '\0';
                Global_u8LineHead = (u8)((Global_u8LineHead + 1u) % NET_LINE_QUEUE_DEPTH);
                Global_u8LineCount--;
            }

            if ((Global_stPending.Active != 0u) &&
                ((MSYSTICK_GetElapsed_ms() - Global_stPending.SentAt) >= NET_CMD_TIMEOUT_MS))
            {
                NET_Event_t Local_stEvent;
                u8  i;

                for (i = 0u; i < sizeof(NET_Event_t); i++)
                {
                    ((u8 *)&Local_stEvent)[i] = 0u;
                }
                Local_stEvent.Type = NET_EVT_CMD_TIMEOUT;
                NET_voidCopyBounded(Local_stEvent.Reason,
                                    sizeof(Local_stEvent.Reason), Global_stPending.Cmd);
                NET_voidEnqueue(&Local_stEvent);
                Global_u8QuarantineTid = Global_stPending.Tid;
                Global_stPending.Active = 0u;

                NET_voidFotaClose();
            }

            if (Global_u8FotaEpoch != 0u)
            {
                u32 Local_u32Now = MSYSTICK_GetElapsed_ms();

                if ((Local_u32Now - HESP_GetLastByteMs()) >= NET_FOTA_EPOCH_WATCHDOG_MS)
                {
                    Local_u32Now = MSYSTICK_GetElapsed_ms();   /* FOTA-06: re-read */
                    if ((Local_u32Now - HESP_GetLastByteMs()) >= NET_FOTA_EPOCH_WATCHDOG_MS)
                    {
                        NET_Event_t Local_stEvent;
                        u8  i;

                        NET_voidFotaClose();

                        for (i = 0u; i < sizeof(NET_Event_t); i++)
                        {
                            ((u8 *)&Local_stEvent)[i] = 0u;
                        }
                        Local_stEvent.Type = NET_EVT_FOTA_WATCHDOG;
                        NET_voidEnqueue(&Local_stEvent);

                        (void)NET_ENUM_SEND_CMD("CMD_FOTA_DONE", NET_u8AllocTid(),
                                                "WATCHDOG", 0);
                    }
                }
            }
        }

        /* ---- the outbound: the command drain (HUB -> NET, FIFO) ---- */
        while (xQueueReceive(Global_xCmdQueue, &Local_stCmd, 0) == pdTRUE)
        {
            if (Local_stCmd.Type == NET_CMD_BRINGUP)
            {
                NET_PRIVATE_Bringup();
            }
            else
            {
                NET_PRIVATE_ExecuteCmd(&Local_stCmd);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(NET_TASK_PERIOD_MS));
    }
}

/* ---- spawn: queues + task, once, pre-scheduler --------------------- */
NET_EnumStatus_t NET_enumSpawn(void)
{
    if (Global_xCmdQueue != NULL)
    {
        return NET_OK;                     /* idempotent */
    }

    Global_xCmdQueue = xQueueCreate(NET_CMD_QUEUE_DEPTH, sizeof(NET_Cmd_t));
    Global_xEvtQueue = xQueueCreate(NET_EVENT_QUEUE_DEPTH, sizeof(NET_Event_t));
    if ((Global_xCmdQueue == NULL) || (Global_xEvtQueue == NULL))
    {
        return NET_NOK;
    }
    if (xTaskCreate(NET_PRIVATE_Task, "NET", NET_TASK_STACK_WORDS, NULL,
                   NET_TASK_PRIORITY, &NET_xTaskHandle) != pdPASS)
    {
        return NET_NOK;
    }
    return NET_OK;
}

/* ---- the public bring-up: enqueue (idempotent in-task) ------------- */
NET_EnumStatus_t NET_enumBringup(void)
{
    NET_Cmd_t Local_stCmd;
    NET_PRIVATE_ZeroCmd(&Local_stCmd);
    Local_stCmd.Type = NET_CMD_BRINGUP;
    return NET_PRIVATE_EnqueueCmd(&Local_stCmd);
}

/* ---- R5: the offer solicitation — the D15 re-emit ------------------ */
NET_EnumStatus_t NET_enumSolicitOffer(void)
{
    NET_Cmd_t Local_stCmd;
    NET_PRIVATE_ZeroCmd(&Local_stCmd);
    Local_stCmd.Type = NET_CMD_SOLICIT;
    return NET_PRIVATE_EnqueueCmd(&Local_stCmd);
}

/* ---- R5: the hub-day FRESET — the stuck-stage recovery ------------- */
NET_EnumStatus_t NET_enumEspFreset(void)
{
    NET_Cmd_t Local_stCmd;
    NET_PRIVATE_ZeroCmd(&Local_stCmd);
    Local_stCmd.Type = NET_CMD_ESP_FRESET;
    return NET_PRIVATE_EnqueueCmd(&Local_stCmd);
}

/* ---- the command helpers ---- */
static void NET_PRIVATE_ZeroCmd(NET_Cmd_t *Copy_pstCmd)
{
    u8  i;
    u8 *Local_pu8 = (u8 *)Copy_pstCmd;
    for (i = 0u; i < sizeof(NET_Cmd_t); i++) { Local_pu8[i] = 0u; }
}

static NET_EnumStatus_t NET_PRIVATE_EnqueueCmd(NET_Cmd_t *Copy_pstCmd)
{
    if (Global_xCmdQueue == NULL)
    {
        return NET_NOK;
    }
    if (xQueueSend(Global_xCmdQueue, Copy_pstCmd, 0) != pdTRUE)
    {
        return NET_NOK;
    }
    return NET_OK;
}

/* ---- the commands: validate, describe, enqueue (HUB context) ---- */

NET_EnumStatus_t NET_enumProbeStatus(void)
{
    NET_Cmd_t Local_stCmd;
    NET_PRIVATE_ZeroCmd(&Local_stCmd);
    Local_stCmd.Type = NET_CMD_PROBE;
    return NET_PRIVATE_EnqueueCmd(&Local_stCmd);
}

NET_EnumStatus_t NET_enumLeaveRoom(void)
{
    NET_Cmd_t Local_stCmd;
    NET_PRIVATE_ZeroCmd(&Local_stCmd);
    Local_stCmd.Type = NET_CMD_LEAVE;
    return NET_PRIVATE_EnqueueCmd(&Local_stCmd);
}

NET_EnumStatus_t NET_enumHost(const char *Copy_pchGame, const char *Copy_pchName)
{
    NET_Cmd_t Local_stCmd;
    if ((Copy_pchGame == 0) || (Copy_pchName == 0))
    {
        return NET_NOK;
    }
    NET_PRIVATE_ZeroCmd(&Local_stCmd);
    Local_stCmd.Type = NET_CMD_HOST;
    NET_voidCopyBounded(Local_stCmd.Arg1, sizeof(Local_stCmd.Arg1), Copy_pchGame);
    NET_voidCopyBounded(Local_stCmd.Arg2, sizeof(Local_stCmd.Arg2), Copy_pchName);
    return NET_PRIVATE_EnqueueCmd(&Local_stCmd);
}

NET_EnumStatus_t NET_enumJoin(const char *Copy_pchCode, const char *Copy_pchName)
{
    NET_Cmd_t Local_stCmd;
    if ((Copy_pchCode == 0) || (Copy_pchName == 0))
    {
        return NET_NOK;
    }
    NET_PRIVATE_ZeroCmd(&Local_stCmd);
    Local_stCmd.Type = NET_CMD_JOIN;
    NET_voidCopyBounded(Local_stCmd.Arg1, sizeof(Local_stCmd.Arg1), Copy_pchCode);
    NET_voidCopyBounded(Local_stCmd.Arg2, sizeof(Local_stCmd.Arg2), Copy_pchName);
    return NET_PRIVATE_EnqueueCmd(&Local_stCmd);
}

NET_EnumStatus_t NET_enumReconnect(const char *Copy_pchCode, const char *Copy_pchToken)
{
    NET_Cmd_t Local_stCmd;
    if ((Copy_pchCode == 0) || (Copy_pchToken == 0))
    {
        return NET_NOK;
    }
    NET_PRIVATE_ZeroCmd(&Local_stCmd);
    Local_stCmd.Type = NET_CMD_RECONNECT;
    NET_voidCopyBounded(Local_stCmd.Arg1, sizeof(Local_stCmd.Arg1), Copy_pchCode);
    NET_voidCopyBounded(Local_stCmd.Arg2, sizeof(Local_stCmd.Arg2), Copy_pchToken);
    return NET_PRIVATE_EnqueueCmd(&Local_stCmd);
}

NET_EnumStatus_t NET_enumSendMove(u8 Copy_u8Cell)
{
    NET_Cmd_t Local_stCmd;
    NET_PRIVATE_ZeroCmd(&Local_stCmd);
    Local_stCmd.Type = NET_CMD_MOVE;
    Local_stCmd.Cell = Copy_u8Cell;
    return NET_PRIVATE_EnqueueCmd(&Local_stCmd);
}

NET_EnumStatus_t NET_enumRequestState(void)
{
    NET_Cmd_t Local_stCmd;
    NET_PRIVATE_ZeroCmd(&Local_stCmd);
    Local_stCmd.Type = NET_CMD_STATE_GET;
    return NET_PRIVATE_EnqueueCmd(&Local_stCmd);
}

NET_EnumStatus_t NET_enumFotaBegin(const char *Copy_pchVersion,
                                    u32 Copy_u32Size, u32 Copy_u32E2eCrc)
{
    NET_Cmd_t Local_stCmd;
    if (Copy_pchVersion == 0)
    {
        return NET_NOK;
    }
    NET_PRIVATE_ZeroCmd(&Local_stCmd);
    Local_stCmd.Type    = NET_CMD_FOTA_BEGIN;
    Local_stCmd.Size    = Copy_u32Size;
    Local_stCmd.E2eCrc  = Copy_u32E2eCrc;
    NET_voidCopyBounded(Local_stCmd.Arg1, sizeof(Local_stCmd.Arg1), Copy_pchVersion);
    return NET_PRIVATE_EnqueueCmd(&Local_stCmd);
}

NET_EnumStatus_t NET_enumFotaDone(const char *Copy_pchStatusToken)
{
    NET_Cmd_t Local_stCmd;
    if (Copy_pchStatusToken == 0)
    {
        return NET_NOK;
    }
    NET_PRIVATE_ZeroCmd(&Local_stCmd);
    Local_stCmd.Type = NET_CMD_FOTA_DONE;
    NET_voidCopyBounded(Local_stCmd.Arg1, sizeof(Local_stCmd.Arg1), Copy_pchStatusToken);
    return NET_PRIVATE_EnqueueCmd(&Local_stCmd);
}

/* ---- events out (the HUB-side drain; 0-timeout receive) ---- */
NET_EnumStatus_t NET_enumGetEvent(NET_Event_t *Copy_pstEvent)
{
    if ((Global_xEvtQueue == NULL) || (Copy_pstEvent == 0))
    {
        return NET_NOK;
    }
    if (xQueueReceive(Global_xEvtQueue, Copy_pstEvent, 0) == pdTRUE)
    {
        return NET_OK;
    }
    return NET_NO_EVENT;
}

/* ---- the snapshot ---- */
const NET_State_t * NET_pcstGetState(void)
{
    return &Global_stState;
}

/* ---- link state ---- */
NET_LinkState_t NET_enumGetLinkState(void)
{
    if (Global_u8Initialized == 0u)
    {
        return NET_LINK_IDLE;
    }
    if (Global_u8LinkLost != 0u)
    {
        return NET_LINK_LOST;
    }
    if (Global_u8EspReady == 0u)
    {
        return NET_LINK_CONNECTING;
    }
    return NET_LINK_LIVE;
}

u8 NET_u8LinkLive(void)
{
    return (NET_enumGetLinkState() == NET_LINK_LIVE) ? 1u : 0u;
}

/* ---- the arbitration TRUTH (M5) ---- */
u8 NET_u8PendingActive(void)
{
    return (Global_stPending.Active != 0u) ? 1u : 0u;
}

/* ---- B1 / Rule 2's gate ---- */
u8 NET_u8SocketIsNew(void)
{
    return Global_u8SocketNew;
}

u8 NET_u8FotaEpochOpen(void)
{
    return Global_u8FotaEpoch;
}

/* ---- FOTA-CS Addendum A: the consumer-requested epoch close ---- */
void NET_voidFotaEpochClose(void)
{
    NET_voidFotaClose();
}

/* ---- counters ---- */
u32 NET_u32GetDropped(void)      { return HESP_GetDropped(); }
u32 NET_u32GetJunkLines(void)    { return HESP_GetJunkLines(); }
u32 NET_u32GetIdleTicks(void)    { return HESP_GetIdleTicks(); }
u32 NET_u32GetLateReplies(void)  { return Global_u32LateReplies; }
u32 NET_u32GetLinesDropped(void) { return Global_u8LinesDropped; }
u32 NET_u32GetEventsDropped(void){ return Global_u8EventsDropped; }

/* ---- the [L1] counter line (M6) ---- */
static u8 NET_u8AppendU32(char *Copy_chDst, u8 Copy_u8Pos, u32 Copy_u32Val)
{
    char Local_ach[10]; u8 k = 0u;
    if (Copy_u32Val == 0u) { Local_ach[k++] = '0'; }
    while (Copy_u32Val > 0u)
    { Local_ach[k++] = (char)('0' + (Copy_u32Val % 10u)); Copy_u32Val /= 10u; }
    while (k > 0u) { Copy_chDst[Copy_u8Pos++] = Local_ach[--k]; }
    return Copy_u8Pos;
}

void NET_voidNarrateCounters(void)
{
    char Local_ach[64]; u8 n = 0u;
    const char *p = "[L1] drop=";
    while (*p) { Local_ach[n++] = *p++; }
    n = NET_u8AppendU32(Local_ach, n, NET_u32GetDropped());
    p = " junk=";  while (*p) { Local_ach[n++] = *p++; }
    n = NET_u8AppendU32(Local_ach, n, NET_u32GetJunkLines());
    p = " idle=";  while (*p) { Local_ach[n++] = *p++; }
    n = NET_u8AppendU32(Local_ach, n, NET_u32GetIdleTicks());
    p = " late=";  while (*p) { Local_ach[n++] = *p++; }
    n = NET_u8AppendU32(Local_ach, n, NET_u32GetLateReplies());
    Local_ach[n++] = '\r'; Local_ach[n++] = '\n'; Local_ach[n] = '\0';
    for (u8 i = 0u; Local_ach[i] != 0; i++)
    { (void)MUSART_SendByteBlocking(NET_CONSOLE_USART, (u8)Local_ach[i]); }
}
