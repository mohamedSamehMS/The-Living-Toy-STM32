/*
 * APP_FOTA_NET.c — the FOTA orchestrator (FOTA seat, v1.2 — the
 *                  consent-model deltas; see the header).
 *
 *  Revision history:
 *    v1.2 — the compare; POLICY 1 gating BOTH modes; the NOTIFIED
 *           re-accept (the refresh); the WATCHDOG resume shape.
 *    v1.0 — first delivery.
 *    R5   — + the stuck-stage flag (the ESP-side STAGE_CRC_FAIL
 *           condition, surfaced for the module's CH:RETRY recovery);
 *           the FA fix (the duplicated enumPrepare conditional —
 *           the FOTA seat's finding, this milestone's commit point).
 */

#include "STD_TYPES.h"

#include <MCAL/SYSTICK/MSYSTICK_interface.h>
#include <MCAL/FLS/MFLS_interface.h>

#include <HAL/ESP/HESP_interface.h>      /* the raw handler + raw TX */

#include "APP/APP_FOTA.h"

#include "APP/APP_FOTA_NET.h"
#include "APP/APP_FOTA_NET_config.h"

#include "APP/APP_FOTA_UI.h"
#include "APP/APP_FOTA_config.h"            /* FOTA_DATA_SIZE / FOTA_TRAILER_SIZE */

#define ORCH_VERSION_MAX   16u

static APP_FOTA_NET_State_t Global_EnumState = APP_FOTA_NET_STATE_IDLE;
static u8  Global_u8Armed       = 0u;   /* the trampoline's gate */
static char Global_achThisVersion[ORCH_VERSION_MAX];
static char Global_achNewVersion[ORCH_VERSION_MAX];
static u32 Global_u32NewLength;
static u32 Global_u32NewE2eCrc;
static u32 Global_u32StagingBytes;      /* the capacity pre-check */
static u8  Global_u8BusyRetries;
static u32 Global_u32BusySinceMs;
static APP_FOTA_Status_t Global_EnumLastStatus = FOTA_STATUS_IDLE;
static APP_FOTA_NET_Notify_t Global_pfNotify = 0;

/* R5: the stuck-stage flag — 1 = the LAST ladder failure was
 * STAGE_CRC_FAIL (the ESP-side staged fetch is bad; the recorded
 * remedy is FRESET + re-BEGIN, human-gated). Set in the RESP_ERR
 * error branch AFTER ToIdle (which clears it — the ordering is
 * load-bearing); cleared on every ladder reset. */
static u8 Global_u8StageCrcStuck = 0u;

/* ---- bounded copy (the string discipline) ---- */
static void ORCH_privateCopyBounded(char *Copy_pchDst, u8 Copy_u8Size, const char *Copy_pchSrc)
{
    u8 Local_u8I = 0u;
    while ((Local_u8I + 1u) < Copy_u8Size && Copy_pchSrc[Local_u8I] != '\0')
    {
        Copy_pchDst[Local_u8I] = Copy_pchSrc[Local_u8I];
        Local_u8I++;
    }
    Copy_pchDst[Local_u8I] = '\0';
}

/* =====================================================================
 *  THE VERSION COMPARE (FOTA-49 delta 1) — numeric per component,
 *  missing = 0. The lexical trap closed.
 * ===================================================================== */
u8 APP_FOTA_NET_u8VersionNewer(const char *Copy_pchNew,
                               const char *Copy_pchRunning)
{
    u8  Local_u8C;
    u32 Local_u32N;
    u32 Local_u32R;
    const char *Local_pchN = Copy_pchNew;
    const char *Local_pchR = Copy_pchRunning;

    if ((Copy_pchNew == 0) || (Copy_pchRunning == 0))
    {
        return 0u;
    }

    for (Local_u8C = 0u; Local_u8C < 4u; Local_u8C++)
    {
        Local_u32N = 0u;
        while ((*Local_pchN >= '0') && (*Local_pchN <= '9'))
        {
            Local_u32N = (Local_u32N * 10u) + (u32)(*Local_pchN - '0');
            Local_pchN++;
        }
        Local_u32R = 0u;
        while ((*Local_pchR >= '0') && (*Local_pchR <= '9'))
        {
            Local_u32R = (Local_u32R * 10u) + (u32)(*Local_pchR - '0');
            Local_pchR++;
        }

        if (Local_u32N != Local_u32R)
        {
            return (Local_u32N > Local_u32R) ? 1u : 0u;
        }

        if (*Local_pchN == '.') { Local_pchN++; }
        if (*Local_pchR == '.') { Local_pchR++; }

        if ((*Local_pchN == '\0') && (*Local_pchR == '\0'))
        {
            return 0u;
        }
    }
    return 0u;
}

/* ---- the trampoline: HESP's raw consumer (loop context) ---- */
static void ORCH_privateRawByte(u8 Copy_u8Byte)
{
    if (Global_u8Armed != 0u)
    {
        APP_FOTA_voidFeedByte(Copy_u8Byte);
    }
}

/* ---- the TX adapter ---- */
static void ORCH_privateTxByte(u8 Copy_u8Byte)
{
    (void)HESP_TxRawByte(Copy_u8Byte);
}

static u8 ORCH_privateStrEq(const char *Copy_pchA, const char *Copy_pchB)
{
    u8 Local_u8I = 0u;
    while ((Copy_pchA[Local_u8I] != '\0') && (Copy_pchB[Local_u8I] != '\0') &&
           (Copy_pchA[Local_u8I] == Copy_pchB[Local_u8I]))
    {
        Local_u8I++;
    }
    return (Copy_pchA[Local_u8I] == Copy_pchB[Local_u8I]) ? 1u : 0u;
}

static void ORCH_privateToIdle(void)
{
    Global_u8Armed = 0u;
    Global_EnumState = APP_FOTA_NET_STATE_IDLE;
    /* R5-FIX: the stuck flag is NOT cleared here — ToIdle runs INSIDE
       the very branch that sets it (set-after-call was a fragile
       ordering). The flag's lifetime is now: set at the RESP_ERR
       failure, cleared only when the NEXT attempt begins (Accept) —
       the module can sample it any time in between. */
}

/* ---- the accept path: sanity -> screens -> prepare -> BEGIN ---- */
static void ORCH_privateAccept(void)
{
    u32 Local_u32Frames;

    /* R5-FIX: a new attempt begins — the previous failure's stuck
       condition expires here (and only here) */
    Global_u8StageCrcStuck = 0u;

    if ((Global_u32NewLength == 0u) || (Global_u32NewE2eCrc == 0u))
    {
        APP_FOTA_UI_Error("BAD NOTIFY");
        ORCH_privateToIdle();
        return;
    }
    Local_u32Frames = (Global_u32NewLength + (FOTA_DATA_SIZE - 1u)) / FOTA_DATA_SIZE;
    if ((Local_u32Frames * FOTA_DATA_SIZE) > Global_u32StagingBytes)
    {
        APP_FOTA_UI_Error("TOO BIG");
        ORCH_privateToIdle();
        return;
    }

    /* FA FIX (the FOTA seat's finding, applied at this milestone — the
       confirmed commit point: the first build linking this file). The
       duplicated conditional is GONE — one enumPrepare call, one check. */
    if (APP_FOTA_enumPrepare() != FOTA_STATUS_IDLE)
    {
        APP_FOTA_UI_Error("ERASE FAIL");
        ORCH_privateToIdle();
        return;
    }

    if (NET_enumFotaBegin(Global_achNewVersion, Global_u32NewLength, Global_u32NewE2eCrc) != NET_OK)
    {
        APP_FOTA_UI_Error("TX FAIL");
        ORCH_privateToIdle();
        return;
    }
    Global_u8BusyRetries = 0u;
    Global_EnumState = APP_FOTA_NET_STATE_BEGIN_WAIT;
}

/* =====================================================================
 *  Public API
 * ===================================================================== */
NET_EnumStatus_t APP_FOTA_NET_enumInit(const char *Copy_pchThisVersion)
{
    u32 Local_u32Base;
    u32 Local_u32Size;

    if (MFLS_GetSectorBounds(MFLS_SECTOR_5, &Local_u32Base, &Local_u32Size) != MFLS_OK)
    {
        return NET_NOK;
    }
    Global_u32StagingBytes = Local_u32Size - FOTA_TRAILER_SIZE;

    ORCH_privateCopyBounded(Global_achThisVersion, sizeof(Global_achThisVersion),
                            (Copy_pchThisVersion != 0) ? Copy_pchThisVersion : "");
    Global_u8Armed   = 0u;
    Global_EnumState = APP_FOTA_NET_STATE_IDLE;
    Global_pfNotify  = 0;
    Global_u8StageCrcStuck = 0u;   /* R5 */

    APP_FOTA_UI_voidInit();
    HESP_SetRawByteHandler(ORCH_privateRawByte);
    return NET_OK;
}

void APP_FOTA_NET_SetNotifyHandler(APP_FOTA_NET_Notify_t Copy_pfNotify)
{
    Global_pfNotify = Copy_pfNotify;
}

NET_EnumStatus_t APP_FOTA_NET_enumStart(void)
{
    if (Global_EnumState != APP_FOTA_NET_STATE_NOTIFIED)
    {
        return NET_BUSY;
    }
    ORCH_privateAccept();
    return NET_OK;
}

APP_FOTA_NET_State_t APP_FOTA_NET_enumGetState(void)
{
    return Global_EnumState;
}

/* R5: the stuck-stage truth — the module's CH:RETRY affordance reads it */
u8 APP_FOTA_NET_u8StageCrcStuck(void)
{
    return Global_u8StageCrcStuck;
}

/* ---- the event entry ---- */
void APP_FOTA_NET_voidOnEvent(const NET_Event_t *Copy_pstEvent)
{
    if (Copy_pstEvent == 0)
    {
        return;
    }

    switch (Copy_pstEvent->Type)
    {
        case NET_EVT_FOTA_AVAILABLE:
        {
            if (  (Global_EnumState == APP_FOTA_NET_STATE_IDLE)
               || (Global_EnumState == APP_FOTA_NET_STATE_NOTIFIED) )
            {
                if (APP_FOTA_NET_u8VersionNewer(Copy_pstEvent->Fota.Version,
                                                Global_achThisVersion) == 0u)
                {
                    break;              /* suppressed — the anti-reinstall lock */
                }

                ORCH_privateCopyBounded(Global_achNewVersion, sizeof(Global_achNewVersion),
                                        Copy_pstEvent->Fota.Version);
                Global_u32NewLength = Copy_pstEvent->Fota.Length;
                Global_u32NewE2eCrc = Copy_pstEvent->Fota.E2eCrc;

#if (APP_FOTA_NET_AUTO_BEGIN != 0)
                APP_FOTA_UI_Available(Global_achNewVersion, Global_u32NewLength);
                ORCH_privateAccept();
#else
                Global_EnumState = APP_FOTA_NET_STATE_NOTIFIED;
                if (Global_pfNotify != 0)
                {
                    Global_pfNotify(Global_achNewVersion, Global_u32NewLength, Global_u32NewE2eCrc);
                }
#endif
            }
            break;
        }

        case NET_EVT_FOTA_READY:
        {
            APP_FOTA_Status_t Local_EnumSt;

            if ((Global_EnumState == APP_FOTA_NET_STATE_BEGIN_WAIT) ||
                (Global_EnumState == APP_FOTA_NET_STATE_BEGIN_RETRY))
            {
                Local_EnumSt = APP_FOTA_enumBeginNet(0u,
                                                     Global_u32NewLength,
                                                     Global_u32NewE2eCrc,
                                                     (u16)FOTA_DATA_SIZE,
                                                     ORCH_privateTxByte);
                Global_EnumLastStatus = Local_EnumSt;

                if (Local_EnumSt == FOTA_STATUS_RECEIVING)
                {
                    Global_u8Armed   = 1u;
                    Global_EnumState = APP_FOTA_NET_STATE_RECEIVING;
                    APP_FOTA_UI_UpdatingStart();
                }
                else
                {
                    NET_voidFotaEpochClose();
                    APP_FOTA_UI_Error(APP_FOTA_pchGetStatusToken());
                    (void)NET_enumFotaDone(APP_FOTA_pchGetStatusToken());
                    Global_EnumState = APP_FOTA_NET_STATE_DONE_WAIT;
                }
            }
            break;
        }

        case NET_EVT_RESP_ERR:
        {
            if ((Global_EnumState == APP_FOTA_NET_STATE_BEGIN_WAIT) ||
                (Global_EnumState == APP_FOTA_NET_STATE_BEGIN_RETRY))
            {
                if ((ORCH_privateStrEq(Copy_pstEvent->Reason, "BUSY") != 0u) &&
                    (Global_u8BusyRetries < APP_FOTA_NET_BEGIN_BUSY_RETRIES))
                {
                    Global_u8BusyRetries++;
                    Global_u32BusySinceMs = MSYSTICK_GetElapsed_ms();
                    Global_EnumState = APP_FOTA_NET_STATE_BEGIN_RETRY;
                }
                else
                {
                    APP_FOTA_UI_Error(Copy_pstEvent->Reason);
                    ORCH_privateToIdle();
                    /* R5: set AFTER ToIdle — ToIdle no longer clears it
                       (the flag lives until the NEXT attempt's Accept),
                       but the after-position is kept as belt-and-braces */
                    Global_u8StageCrcStuck =
                        ORCH_privateStrEq(Copy_pstEvent->Reason, "STAGE_CRC_FAIL");
                }
            }
            break;
        }

        case NET_EVT_CMD_TIMEOUT:
        {
            if ((Global_EnumState == APP_FOTA_NET_STATE_BEGIN_WAIT) ||
                (Global_EnumState == APP_FOTA_NET_STATE_BEGIN_RETRY))
            {
                APP_FOTA_UI_Error("NO ANSWER");
                ORCH_privateToIdle();
            }
            else if (Global_EnumState == APP_FOTA_NET_STATE_DONE_WAIT)
            {
                if (Global_EnumLastStatus == FOTA_STATUS_STAGED_OK)
                {
                    APP_FOTA_UI_Success();
                    APP_FOTA_voidNetReset();       /* never returns */
                }
                else
                {
                    ORCH_privateToIdle();
                }
            }
            break;
        }

        case NET_EVT_FOTA_WATCHDOG:
        {
            if (Global_EnumState != APP_FOTA_NET_STATE_IDLE)
            {
                APP_FOTA_UI_Error("TIMEOUT");
#if (APP_FOTA_NET_AUTO_BEGIN != 0)
                ORCH_privateToIdle();
#else
                Global_u8Armed = 0u;
                Global_EnumState = APP_FOTA_NET_STATE_NOTIFIED;  /* the re-offer */
#endif
            }
            break;
        }

        case NET_EVT_FOTA_DONE:
        {
            if (Global_EnumState == APP_FOTA_NET_STATE_DONE_WAIT)
            {
                if (Global_EnumLastStatus == FOTA_STATUS_STAGED_OK)
                {
                    APP_FOTA_UI_Success();
                    APP_FOTA_voidNetReset();       /* never returns */
                }
                else
                {
                    ORCH_privateToIdle();
                }
            }
            break;
        }

        case NET_EVT_LINK_LOST:
        {
            if (Global_EnumState != APP_FOTA_NET_STATE_IDLE)
            {
                NET_voidFotaEpochClose();
                (void)NET_enumFotaDone("ABORTED");
                APP_FOTA_UI_Error("LINK LOST");
                ORCH_privateToIdle();
            }
            break;
        }

        default:
            break;
    }
}

/* ---- the task ---- */
void APP_FOTA_NET_voidTask(void)
{
    APP_FOTA_UI_voidPoll();

    if (Global_EnumState == APP_FOTA_NET_STATE_BEGIN_RETRY)
    {
        if ((MSYSTICK_GetElapsed_ms() - Global_u32BusySinceMs) >= APP_FOTA_NET_BEGIN_RETRY_MS)
        {
            if (NET_enumFotaBegin(Global_achNewVersion, Global_u32NewLength, Global_u32NewE2eCrc) == NET_OK)
            {
                Global_EnumState = APP_FOTA_NET_STATE_BEGIN_WAIT;
            }
            else
            {
                APP_FOTA_UI_Error("TX FAIL");
                ORCH_privateToIdle();
            }
        }
    }

    if (Global_EnumState == APP_FOTA_NET_STATE_RECEIVING)
    {
        APP_FOTA_voidPoll();

        if (APP_FOTA_enumGetStatus() == FOTA_STATUS_RECEIVING)
        {
            APP_FOTA_UI_UpdatingProgress(APP_FOTA_u32GetFramesDone(),
                                         APP_FOTA_u32GetFramesTotal());
        }
        else
        {
            Global_EnumLastStatus = APP_FOTA_enumGetStatus();

            NET_voidFotaEpochClose();

            if (Global_EnumLastStatus != FOTA_STATUS_STAGED_OK)
            {
                APP_FOTA_UI_Error(APP_FOTA_pchGetStatusToken());
            }
            (void)NET_enumFotaDone(APP_FOTA_pchGetStatusToken());
            Global_EnumState = APP_FOTA_NET_STATE_DONE_WAIT;
        }
    }
}
