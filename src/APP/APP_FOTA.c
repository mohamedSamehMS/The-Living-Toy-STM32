/*
 * APP_FOTA.c — FOTA receiver implementation (FOTA seat, v2.1; the R5
 *              absorption's include adaptations applied).
 *
 *  THE CORE (shared): the v1.6 state machine — frame collect /
 *  0xAA-drain / CRC gate / staging write / trailer with the magic
 *  word LAST. Transport-agnostic: bytes in (FeedByte), one byte out
 *  (the active TX), status out.
 *
 *  THE BINDINGS:
 *   [BENCH] MUSART callbacks on APP_FOTA_BENCH_USART (config) +
 *           the v1 text header parse; terminal lines print on the
 *           SAME wire; self-reset after OK STAGED. (Not used on this
 *           tree — NET mode only; the bench binding is dead code
 *           under --gc-sections until something calls Init/Run.)
 *   [NET]   NET supplies bytes (via HESP's raw router) and the
 *           raw-TX function; errors become statuses + console
 *           narration; the reset waits for NET's DONE terminal.
 *
 *  Revision history: v2.2 (protocol v1.7: the sequence byte; the
 *  duplicate re-ACK; the DUP counter) / v2.1 / v2.0 / v1.7 / v1.6/1.5 —
 *  the FOTA seat's line, verbatim. Frozen protocol constants live in
 *  APP_FOTA_config.h (section 2 — DO NOT EDIT).
 */

#include "STD_TYPES.h"
#include <MCAL/SYSTICK/MSYSTICK_interface.h>

#include <MCAL/FLS/MFLS_interface.h>
#include <MCAL/CRC/MCRC_interface.h>
#include <MCAL/USART/MUSART_interface.h>

#include "../MNVIC_interface.h"              /* the system-reset call (the tree's
                                             path form — the delivered
                                             "../MNVIC_interface.h" breaks) */

#include "APP/APP_FOTA_config.h"
#include "APP/APP_FOTA.h"

/* ---- states (internal) ---- */
#define FOTA_STATE_HEADER      0u   /* bench binding only */
#define FOTA_STATE_COLLECT     1u
#define FOTA_STATE_DRAIN       2u
#define FOTA_STATE_DONE        3u
#define FOTA_STATE_VALIDATE    4u   /* v2.3: resync marker validation */

/* ---- shared state (volatile: the BENCH binding feeds this
 *        from the MUSART ISR; the NET binding feeds it from
 *        loop context — the discipline covers the harder case) ---- */
static volatile u8  Global_au8HeaderBuf[FOTA_HEADER_BUF_SIZE];
static volatile u8  Global_u8HeaderLen    = 0u;
static volatile u8  Global_u8HeaderReady  = 0u;
static volatile u8  Global_au8FrameBuf[FOTA_FRAME_SIZE];
static volatile u16 Global_u16FrameIdx    = 0u;
static volatile u8  Global_u8FrameReady   = 0u;
static volatile u32 Global_u32LastByteMs  = 0u;
static volatile u8  Global_u8State        = FOTA_STATE_HEADER;

/* ---- main-loop state ---- */
static u32 Global_u32Version;
static u32 Global_u32Length;         /* declared payload bytes  */
static u32 Global_u32E2eCrc;         /* declared end-to-end CRC */
static u32 Global_u32FramesTotal;
static u32 Global_u32FrameCount;     /* frames written + ACKed  */
static u32 Global_u32DupCount;       /* v1.7: duplicates re-ACKed (diag) */
static u32 Global_u32StagingBase;
static u32 Global_u32StagingSize;
static u32 Global_u32TrailerAddr;
static APP_FOTA_Status_t Global_EnumStatus = FOTA_STATUS_IDLE;
static u8  Global_u8Prepared   = 0u;  /* Prepare/Init ran        */
static u8  Global_u8NetMode    = 0u;
static APP_FOTA_TxByte_t Global_pfTx = 0;   /* the active TX     */

/* v2.1: the text-output target. Bench Init sets it to the bench
   wire (config); NET mode keeps the console. */
static MUSART_USART_t Global_EnumPrintUsart = MUSART_USART2;

/* Word-aligned Flash-write source (main loop only) */
static u32 Global_au32DataWords[FOTA_DATA_SIZE / 4u];

/* =====================================================================
 *  Console/wire helpers — all text goes through Global_EnumPrintUsart
 * ===================================================================== */
static void APP_FOTA_privatePrintStr(const char *Copy_pchText)
{
    while (*Copy_pchText != '\0')
    {
        (void)MUSART_SendByteBlocking(Global_EnumPrintUsart, (u8)*Copy_pchText);
        Copy_pchText++;
    }
}

static void APP_FOTA_privatePrintDec(u32 Copy_u32Value)
{
    char Local_achDigits[10];
    u8  Local_u8Count = 0u;

    if (Copy_u32Value == 0UL) { (void)MUSART_SendByteBlocking(Global_EnumPrintUsart, (u8)'0'); return; }
    while (Copy_u32Value != 0UL)
    {
        Local_achDigits[Local_u8Count++] = (char)('0' + (Copy_u32Value % 10UL));
        Copy_u32Value /= 10UL;
    }
    while (Local_u8Count != 0u)
    {
        Local_u8Count--;
        (void)MUSART_SendByteBlocking(Global_EnumPrintUsart, (u8)Local_achDigits[Local_u8Count]);
    }
}

static void APP_FOTA_privatePrintHex32(u32 Copy_u32Value)
{
    static const char Local_achHex[16] = "0123456789ABCDEF";
    u8 Local_u8Shift;

    APP_FOTA_privatePrintStr("0x");
    for (Local_u8Shift = 0u; Local_u8Shift < 8u; Local_u8Shift++)
    {
        u8 Local_u8Digit = (u8)((Copy_u32Value >> (28u - (4u * Local_u8Shift))) & 0xFu);
        (void)MUSART_SendByteBlocking(Global_EnumPrintUsart, (u8)Local_achHex[Local_u8Digit]);
    }
}

/* NET-mode informational narration (config-gated). Terminal status
   lines are NOT routed through here — they are the wire contract
   in bench mode and the report in NET mode; they print always. */
static void APP_FOTA_privateNarrate(const char *Copy_pchText)
{
#if APP_FOTA_NET_NARRATE
    APP_FOTA_privatePrintStr(Copy_pchText);
#else
    (void)Copy_pchText;
#endif
}

/* The bench binding's TX: the v1 wire */
static void APP_FOTA_privateBenchTx(u8 Copy_u8Byte)
{
    (void)MUSART_SendByteBlocking(APP_FOTA_BENCH_USART, Copy_u8Byte);
}

/* The unified TX — whichever binding armed it */
static void APP_FOTA_privateTx(u8 Copy_u8Byte)
{
    if (Global_pfTx != 0)
    {
        Global_pfTx(Copy_u8Byte);
    }
}

/* =====================================================================
 *  THE CORE — feed / collect / drain
 * ===================================================================== */
void APP_FOTA_voidFeedByte(u8 Copy_u8Data)
{
    Global_u32LastByteMs = MSYSTICK_GetElapsed_ms();

    if (Global_u8State == FOTA_STATE_COLLECT)
    {
        if ((Global_u8FrameReady == 0u) && (Global_u16FrameIdx < FOTA_FRAME_SIZE))
        {
            Global_au8FrameBuf[Global_u16FrameIdx] = Copy_u8Data;
            Global_u16FrameIdx++;
            if (Global_u16FrameIdx == FOTA_FRAME_SIZE)
            {
                Global_u8FrameReady = 1u;
            }
        }
        /* else: frame pending — drop (stop-and-wait + resync) */
    }
    else if (Global_u8State == FOTA_STATE_DRAIN)
    {
        /* v2.3: the marker alone no longer resyncs. 0xAA can appear
           in frame DATA; a data-0xAA resync mid-discard leaves a
           partial collect that partitions every retry identically —
           the inescapable frame-7 attractor. The byte AFTER the
           marker must now validate as the expected or previous SEQ
           before collection resumes. */
        if (Copy_u8Data == (u8)FOTA_RESYNC)
        {
            Global_u16FrameIdx  = 0u;
            Global_u8FrameReady = 0u;
            Global_u8State = FOTA_STATE_VALIDATE;
        }
    }
    else if (Global_u8State == FOTA_STATE_VALIDATE)
    {
        u8 Local_u8Valid = (Copy_u8Data == (u8)(Global_u32FrameCount & 0xFFu));

        if ((Local_u8Valid == 0u) && (Global_u32FrameCount != 0u))
        {
            Local_u8Valid = (Copy_u8Data == (u8)((Global_u32FrameCount - 1u) & 0xFFu));
        }

        if (Local_u8Valid != 0u)
        {
            /* a plausible frame head: store as the SEQ byte, collect
               the rest of the frame */
            Global_au8FrameBuf[0] = Copy_u8Data;
            Global_u16FrameIdx    = 1u;
            Global_u8FrameReady   = 0u;
            Global_u8State = FOTA_STATE_COLLECT;
        }
        else
        {
            /* not a frame head — keep discarding (silent; the
               sender's timeout governs) */
            Global_u8State = FOTA_STATE_DRAIN;
        }
    }
    else if (Global_u8State == FOTA_STATE_HEADER)
    {
        /* Bench binding only (ISR path): collect the text header */
        if (Copy_u8Data == (u8)'\n')
        {
            Global_au8HeaderBuf[Global_u8HeaderLen] = 0u;
            Global_u8HeaderReady = 1u;
            Global_u8State = FOTA_STATE_COLLECT;
        }
        else if (Global_u8HeaderLen < (FOTA_HEADER_BUF_SIZE - 1u))
        {
            Global_au8HeaderBuf[Global_u8HeaderLen] = Copy_u8Data;
            Global_u8HeaderLen++;
        }
        else { /* overlong — the parse will reject */ }
    }
    /* DONE: discard */
}

/* =====================================================================
 *  THE CORE — frame processing + finalize (main loop, both modes)
 * ===================================================================== */
static void APP_FOTA_privateProcessFrame(void)
{
    u32               Local_u32CrcCalc = 0u;
    u32               Local_u32CrcRx;
    u16               Local_u16Index;
    u16               Local_u16Src;
    u8                Local_u8Seq;
    u8                Local_u8Expected;
    MFLS_ReturnType_t Local_EnumFls;
    MCRC_ReturnType_t Local_EnumCrc;

    /* v1.7: CRC over SEQ + DATA (129 bytes, wire order — the
       sender CRC'd exactly these bytes in exactly this order) */
    Local_EnumCrc = MCRC_CalcBuffer((const u8 *)(void *)Global_au8FrameBuf,
                                    (FOTA_SEQ_SIZE + FOTA_DATA_SIZE),
                                    &Local_u32CrcCalc);
    if (Local_EnumCrc != MCRC_OK)
    {
        Global_EnumStatus = FOTA_STATUS_ERR_CRC_ENGINE;
        Global_u8State = FOTA_STATE_DONE;
        APP_FOTA_privatePrintStr("ERR CRC ENGINE\r\n");
        return;
    }

    Local_u32CrcRx =   ((u32)Global_au8FrameBuf[FOTA_SEQ_SIZE + FOTA_DATA_SIZE + 0u] << 24)
                     | ((u32)Global_au8FrameBuf[FOTA_SEQ_SIZE + FOTA_DATA_SIZE + 1u] << 16)
                     | ((u32)Global_au8FrameBuf[FOTA_SEQ_SIZE + FOTA_DATA_SIZE + 2u] << 8)
                     |  (u32)Global_au8FrameBuf[FOTA_SEQ_SIZE + FOTA_DATA_SIZE + 3u];

    if (Local_u32CrcCalc != Local_u32CrcRx)
    {
        APP_FOTA_privateTx(FOTA_NAK);
        Global_u16FrameIdx = 0u;
        Global_u8FrameReady = 0u;
        Global_u8State = FOTA_STATE_DRAIN;
        return;
    }

    /* ---- v1.7: the sequence gate ---- */
    Local_u8Seq      = Global_au8FrameBuf[0];
    Local_u8Expected = (u8)(Global_u32FrameCount & 0xFFu);   /* index mod 256 */

    if (Local_u8Seq == Local_u8Expected)
    {
        /* The normal path: assemble words from DATA (offset = SEQ).
           NATIVE little-endian — byte 0 of data -> low byte; Flash
           bytes land in file order. */
        for (Local_u16Index = 0u; Local_u16Index < (FOTA_DATA_SIZE / 4u); Local_u16Index++)
        {
            Local_u16Src = (u16)(FOTA_SEQ_SIZE + (Local_u16Index * 4u));
            Global_au32DataWords[Local_u16Index] =
                  (u32)Global_au8FrameBuf[Local_u16Src + 0u]
                | ((u32)Global_au8FrameBuf[Local_u16Src + 1u] << 8)
                | ((u32)Global_au8FrameBuf[Local_u16Src + 2u] << 16)
                | ((u32)Global_au8FrameBuf[Local_u16Src + 3u] << 24);
        }

        Local_EnumFls = MFLS_ProgramWords(Global_u32StagingBase + (Global_u32FrameCount * FOTA_DATA_SIZE),
                                          (const u32 *)Global_au32DataWords,
                                          (u16)(FOTA_DATA_SIZE / 4u));
        if (Local_EnumFls != MFLS_OK)
        {
            Global_EnumStatus = FOTA_STATUS_ERR_FLASH;
            Global_u8State = FOTA_STATE_DONE;
            APP_FOTA_privatePrintStr("ERR FLASH WRITE\r\n");
            return;
        }

        APP_FOTA_privateTx(FOTA_ACK);
        Global_u32FrameCount++;
        Global_u16FrameIdx  = 0u;
        Global_u8FrameReady = 0u;
    }
    else if (  (Global_u32FrameCount != 0u)
            && ((u8)((Global_u32FrameCount - 1u) & 0xFFu) == Local_u8Seq) )
    {
        /* v1.7 DUPLICATE: the previous frame retransmitted (its ACK
           was lost in flight). CRC valid, seq == previous. Re-ACK,
           write nothing, advance nothing — the sender recovers on
           the ACK and the slot map stays exact. THIS closes the
           spurious-E2E_FAIL class (FOTA-SPEC-2 §4). */
        APP_FOTA_privateTx(FOTA_ACK);
        Global_u32DupCount++;
        Global_u16FrameIdx  = 0u;
        Global_u8FrameReady = 0u;
    }
    else
    {
        /* seq matches neither the expected nor the previous slot:
           treat as corruption — NAK + resync */
        APP_FOTA_privateTx(FOTA_NAK);
        Global_u16FrameIdx  = 0u;
        Global_u8FrameReady = 0u;
        Global_u8State = FOTA_STATE_DRAIN;
    }
}

static void APP_FOTA_privateFinalize(void)
{
    u32               Local_u32CrcReadback = 0u;
    u32               Local_u32PaddedLength;
    u32               Local_au32Trailer[8];
    MFLS_ReturnType_t Local_EnumFls;
    MCRC_ReturnType_t Local_EnumCrc;
    u8                Local_u8Idx;

    /* E2E over the padded footprint staged (the v1.6 rule) */
    Local_u32PaddedLength = (Global_u32Length + 3u) & ~3u;
    Local_EnumCrc = MCRC_CalcBuffer((const u8 *)(void *)Global_u32StagingBase,
                                    Local_u32PaddedLength, &Local_u32CrcReadback);
    if (Local_EnumCrc != MCRC_OK)
    {
        Global_EnumStatus = FOTA_STATUS_ERR_CRC_ENGINE;
        Global_u8State = FOTA_STATE_DONE;
        APP_FOTA_privatePrintStr("ERR CRC ENGINE\r\n");
        return;
    }

    if (Local_u32CrcReadback != Global_u32E2eCrc)
    {
        Global_EnumStatus = FOTA_STATUS_ERR_E2E;
        Global_u8State = FOTA_STATE_DONE;
        APP_FOTA_privatePrintStr("ERR E2E CRC: GOT 0x");
        APP_FOTA_privatePrintHex32(Local_u32CrcReadback);
        APP_FOTA_privatePrintStr(" WANT 0x");
        APP_FOTA_privatePrintHex32(Global_u32E2eCrc);
        APP_FOTA_privatePrintStr("\r\n");
        return;
    }

    /* Trailer v2 — magic word LAST (the commit point) */
    Local_au32Trailer[0] = FOTA_MAGIC;
    Local_au32Trailer[1] = FOTA_TRAILER_VERSION;
    Local_au32Trailer[2] = Global_u32Version;
    Local_au32Trailer[3] = Global_u32Length;
    Local_au32Trailer[4] = Global_u32E2eCrc;
    Local_EnumCrc = MCRC_CalcBuffer((const u8 *)(void *)&Local_au32Trailer[1], 16u,
                                    &Local_au32Trailer[5]);
    if (Local_EnumCrc != MCRC_OK)
    {
        Global_EnumStatus = FOTA_STATUS_ERR_CRC_ENGINE;
        Global_u8State = FOTA_STATE_DONE;
        return;
    }
    Local_au32Trailer[6] = 0xFFFFFFFFUL;
    Local_au32Trailer[7] = 0xFFFFFFFFUL;

    Local_EnumFls = MFLS_ProgramWords(Global_u32TrailerAddr + 4u, &Local_au32Trailer[1], 7u);
    if (Local_EnumFls == MFLS_OK)
    {
        Local_EnumFls = MFLS_ProgramWords(Global_u32TrailerAddr, &Local_au32Trailer[0], 1u);
    }
    if (Local_EnumFls != MFLS_OK)
    {
        Global_EnumStatus = FOTA_STATUS_ERR_TRAILER;
        Global_u8State = FOTA_STATE_DONE;
        APP_FOTA_privatePrintStr("ERR TRAILER WRITE\r\n");
        return;
    }

    Global_EnumStatus = FOTA_STATUS_STAGED_OK;
    Global_u8State = FOTA_STATE_DONE;

    if (Global_u8NetMode == 0u)
    {
        /* BENCH: the v1 completion line + self-reset (v1.7) */
        u32 Local_u32Fe = 0u;
        (void)MUSART_GetFramingErrorCount(APP_FOTA_BENCH_USART, &Local_u32Fe);
        APP_FOTA_privatePrintStr("OK STAGED: ");
        APP_FOTA_privatePrintDec(Global_u32Length);
        APP_FOTA_privatePrintStr(" BYTES, CRC OK, FE ");
        APP_FOTA_privatePrintDec(Local_u32Fe);
        APP_FOTA_privatePrintStr(", DUP ");
        APP_FOTA_privatePrintDec(Global_u32DupCount);
        APP_FOTA_privatePrintStr(", FIRST: ");
        for (Local_u8Idx = 0u; Local_u8Idx < 4u; Local_u8Idx++)
        {
            APP_FOTA_privatePrintHex32(*((volatile u32 *)(Global_u32StagingBase + (4u * Local_u8Idx))));
            APP_FOTA_privatePrintStr(" ");
        }
        APP_FOTA_privatePrintStr("\r\n");
        /* crude delay (FOTA-06: no SysTick trust), then reset */
        {
            volatile u32 Local_u32Count = 500u * 3000u;
            while (Local_u32Count != 0u) { Local_u32Count--; }
        }
        MNVIC_SystemReset();
    }
    /* NET: status STAGED_OK stands; NET runs the DONE transaction,
       then calls APP_FOTA_voidNetReset(). */
    APP_FOTA_privateNarrate("[FOTA] STAGED OK - awaiting DONE\r\n");
}

/* =====================================================================
 *  THE CORE — poll (both modes' main-loop pump)
 * ===================================================================== */
void APP_FOTA_voidPoll(void)
{
    /* FOTA-06 note: the abort check stays disabled (the config's
       constant makes it inert by value; NET's 90 s watchdog is the
       v2 backstop). */
    if (  ((Global_u8State == FOTA_STATE_COLLECT) || (Global_u8State == FOTA_STATE_DRAIN))
       && (Global_u32LastByteMs != 0u)
       && ((MSYSTICK_GetElapsed_ms() - Global_u32LastByteMs) > FOTA_ABORT_MS) )
    {
        Global_EnumStatus = FOTA_STATUS_IDLE;   /* unreachable by value */
        Global_u8State = FOTA_STATE_DONE;
        return;
    }

    if ((Global_u8State == FOTA_STATE_COLLECT) && (Global_u8FrameReady != 0u))
    {
        APP_FOTA_privateProcessFrame();

        if ((Global_u8State == FOTA_STATE_COLLECT)
            && (Global_u32FrameCount == Global_u32FramesTotal)
            && (Global_u32FramesTotal != 0u))
        {
            APP_FOTA_privateFinalize();
        }
    }
}

/* =====================================================================
 *  BENCH MODE — the v1 API, preserved (dead code on this tree: NET
 *  mode only; kept per the seat's two-binding contract)
 * ===================================================================== */
static void APP_FOTA_privateRxByte(u8 Copy_u8Data)
{
    APP_FOTA_voidFeedByte(Copy_u8Data);   /* HEADER/COLLECT/DRAIN in the core */
}

static void APP_FOTA_privateIdle(void) { }

static u8 APP_FOTA_privateParseHeader(u32 *Copy_pu32Version,
                                      u32 *Copy_pu32Length,
                                      u32 *Copy_pu32Crc)
{
    u32 Local_u32Index = 0u;
    u32 Local_u32Value;
    u8  Local_u8Slot;

    if (  (Global_au8HeaderBuf[0] != (u8)'S') || (Global_au8HeaderBuf[1] != (u8)'E')
       || (Global_au8HeaderBuf[2] != (u8)'N') || (Global_au8HeaderBuf[3] != (u8)'D') )
    {
        return 0u;
    }
    Local_u32Index = 4u;

    for (Local_u8Slot = 0u; Local_u8Slot < 3u; Local_u8Slot++)
    {
        while (Global_au8HeaderBuf[Local_u32Index] == (u8)' ') { Local_u32Index++; }
        Local_u32Value = 0u;
        while ((Global_au8HeaderBuf[Local_u32Index] >= (u8)'0') && (Global_au8HeaderBuf[Local_u32Index] <= (u8)'9'))
        {
            Local_u32Value = (Local_u32Value * 10u) + (u32)(Global_au8HeaderBuf[Local_u32Index] - (u8)'0');
            Local_u32Index++;
        }
        if (Local_u8Slot == 0u)      { *Copy_pu32Version = Local_u32Value; }
        else if (Local_u8Slot == 1u) { *Copy_pu32Length  = Local_u32Value; }
        else                         { *Copy_pu32Crc     = Local_u32Value; }
    }

    return ((*Copy_pu32Length != 0u) && (*Copy_pu32Crc != 0u)) ? 1u : 0u;
}

void APP_FOTA_Init(void)
{
    (void)MCRC_Init();

    if (MFLS_GetSectorBounds(MFLS_SECTOR_5, &Global_u32StagingBase, &Global_u32StagingSize) != MFLS_OK)
    {
        APP_FOTA_privatePrintStr("ERR STAGING BOUNDS\r\n");
        Global_EnumStatus = FOTA_STATUS_ERR_PARAM;
        Global_u8State = FOTA_STATE_DONE;
        return;
    }
    Global_u32TrailerAddr = Global_u32StagingBase + Global_u32StagingSize - FOTA_TRAILER_SIZE;

    if (MFLS_EraseSector(MFLS_SECTOR_5) != MFLS_OK)
    {
        APP_FOTA_privatePrintStr("ERR ERASE STAGING\r\n");
        Global_EnumStatus = FOTA_STATUS_ERR_ERASE;
        Global_u8State = FOTA_STATE_DONE;
        return;
    }

    Global_u8NetMode       = 0u;
    Global_pfTx            = APP_FOTA_privateBenchTx;
    Global_EnumPrintUsart  = APP_FOTA_BENCH_USART;   /* v2.1: the knob */
    Global_u8Prepared      = 1u;
    Global_u8State         = FOTA_STATE_HEADER;
    Global_u8HeaderLen     = 0u;
    Global_u8HeaderReady   = 0u;
    Global_u16FrameIdx     = 0u;
    Global_u8FrameReady    = 0u;
    Global_u32LastByteMs   = 0u;
    Global_u32Version      = 0u;
    Global_u32Length       = 0u;
    Global_u32E2eCrc       = 0u;
    Global_u32FramesTotal  = 0u;
    Global_u32FrameCount   = 0u;
    Global_u32DupCount     = 0u;
    Global_EnumStatus      = FOTA_STATUS_RECEIVING;

    (void)MUSART_SetRxCallback  (APP_FOTA_BENCH_USART, APP_FOTA_privateRxByte);
    (void)MUSART_SetIdleCallback(APP_FOTA_BENCH_USART, APP_FOTA_privateIdle);
    (void)MUSART_ReceiveEnable   (APP_FOTA_BENCH_USART);

    /* bench fingerprint (the V4 lesson — carried from v1.6a) */
    APP_FOTA_privatePrintStr("FOTA RX ABORT_MS=");
    APP_FOTA_privatePrintDec(FOTA_ABORT_MS);
    APP_FOTA_privatePrintStr(" FRAME=");
    APP_FOTA_privatePrintDec(FOTA_FRAME_SIZE);
    APP_FOTA_privatePrintStr("\r\n");

    APP_FOTA_privatePrintStr("READY\r\n");
}

void APP_FOTA_Run(void)
{
    u32 Local_u32Length;
    u32 Local_u32Version;
    u32 Local_u32E2eCrc;
    u32 Local_u32Frames;

    if (Global_u8HeaderReady != 0u)
    {
        Global_u8HeaderReady = 0u;
        if (APP_FOTA_privateParseHeader(&Local_u32Version, &Local_u32Length, &Local_u32E2eCrc) != 0u)
        {
            Local_u32Frames = (Local_u32Length + (FOTA_DATA_SIZE - 1u)) / FOTA_DATA_SIZE;
            if ((Local_u32Frames * FOTA_DATA_SIZE) <= (Global_u32StagingSize - FOTA_TRAILER_SIZE))
            {
                Global_u32Version     = Local_u32Version;
                Global_u32Length      = Local_u32Length;
                Global_u32E2eCrc      = Local_u32E2eCrc;
                Global_u32FramesTotal = Local_u32Frames;
            }
            else
            {
                APP_FOTA_privatePrintStr("ERR LENGTH TOO BIG\r\n");
                Global_EnumStatus = FOTA_STATUS_ERR_LENGTH;
                Global_u8State = FOTA_STATE_DONE;
            }
        }
        else
        {
            APP_FOTA_privatePrintStr("ERR BAD HEADER\r\n");
            Global_EnumStatus = FOTA_STATUS_ERR_PARAM;
            Global_u8State = FOTA_STATE_DONE;
        }
    }

    APP_FOTA_voidPoll();
}

/* =====================================================================
 *  NET MODE — the v2 API (this tree's live path)
 * ===================================================================== */
APP_FOTA_Status_t APP_FOTA_enumPrepare(void)
{
    (void)MCRC_Init();

    if (MFLS_GetSectorBounds(MFLS_SECTOR_5, &Global_u32StagingBase, &Global_u32StagingSize) != MFLS_OK)
    {
        Global_EnumStatus = FOTA_STATUS_ERR_PARAM;
        return Global_EnumStatus;
    }
    Global_u32TrailerAddr = Global_u32StagingBase + Global_u32StagingSize - FOTA_TRAILER_SIZE;

    if (MFLS_EraseSector(MFLS_SECTOR_5) != MFLS_OK)
    {
        Global_EnumStatus = FOTA_STATUS_ERR_ERASE;
        return Global_EnumStatus;
    }

    Global_u8Prepared = 1u;
    Global_EnumStatus = FOTA_STATUS_IDLE;
    return FOTA_STATUS_IDLE;
}

APP_FOTA_Status_t APP_FOTA_enumBeginNet(u32 Copy_u32Version,
                                        u32 Copy_u32Length,
                                        u32 Copy_u32E2eCrc,
                                        u16 Copy_u16FrameSizeEcho,
                                        APP_FOTA_TxByte_t Copy_pfTxByte)
{
    u32 Local_u32Frames;

    if ((Copy_pfTxByte == 0) || (Copy_u32Length == 0u) || (Copy_u32E2eCrc == 0u))
    {
        Global_EnumStatus = FOTA_STATUS_ERR_PARAM;
        return Global_EnumStatus;
    }
    if (Copy_u16FrameSizeEcho != FOTA_DATA_SIZE)
    {
        /* The field is carried by the bracket; this build supports
           128 only — honest refusal, never a silent clamp. */
        Global_EnumStatus = FOTA_STATUS_ERR_SIZE;
        return Global_EnumStatus;
    }
    if (Global_u8Prepared == 0u)
    {
        Global_EnumStatus = FOTA_STATUS_ERR_NOT_ARMED;
        return Global_EnumStatus;
    }

    Local_u32Frames = (Copy_u32Length + (FOTA_DATA_SIZE - 1u)) / FOTA_DATA_SIZE;
    if ((Local_u32Frames * FOTA_DATA_SIZE) > (Global_u32StagingSize - FOTA_TRAILER_SIZE))
    {
        Global_EnumStatus = FOTA_STATUS_ERR_LENGTH;
        return Global_EnumStatus;
    }

    Global_u8NetMode      = 1u;
    Global_pfTx           = Copy_pfTxByte;
    Global_EnumPrintUsart = MUSART_USART2;   /* NET narration = the console */
    Global_u32Version    = Copy_u32Version;
    Global_u32Length     = Copy_u32Length;
    Global_u32E2eCrc     = Copy_u32E2eCrc;
    Global_u32FramesTotal = Local_u32Frames;
    Global_u32FrameCount = 0u;
    Global_u32DupCount = 0u;
    Global_u16FrameIdx   = 0u;
    Global_u8FrameReady  = 0u;
    Global_u8State       = FOTA_STATE_COLLECT;
    Global_EnumStatus    = FOTA_STATUS_RECEIVING;

    APP_FOTA_privateNarrate("[FOTA] ARMED v=");
    APP_FOTA_privatePrintDec(Copy_u32Version);
    APP_FOTA_privateNarrate(" len=");
    APP_FOTA_privatePrintDec(Copy_u32Length);
    APP_FOTA_privateNarrate("\r\n");
    return FOTA_STATUS_RECEIVING;
}

APP_FOTA_Status_t APP_FOTA_enumGetStatus(void)  { return Global_EnumStatus; }
u32 APP_FOTA_u32GetFramesDone(void)             { return Global_u32FrameCount; }
u32 APP_FOTA_u32GetFramesTotal(void)            { return Global_u32FramesTotal; }

const char * APP_FOTA_pchGetStatusToken(void)
{
    /* The REGISTERED DONE-transaction set: OK / E2E_FAIL / ABORTED /
       WATCHDOG. Flash/CRC/trailer failures report ABORTED (the console
       carries the detail via enumGetStatus). The pre-BEGIN statuses
       never ride DONE — NET refuses before BEGIN — and fall to
       ABORTED as the safety default. WATCHDOG is NET's own token. */
    switch (Global_EnumStatus)
    {
        case FOTA_STATUS_STAGED_OK: return "OK";
        case FOTA_STATUS_ERR_E2E:   return "E2E_FAIL";
        default:                    return "ABORTED";
    }
}

void APP_FOTA_voidNetReset(void)
{
    /* NET called this on the RESP_FOTA_DONE terminal — the ESP has
       dropped its gate, the referee has the report. Reset now;
       the bootloader commits. Crude delay (FOTA-06). */
    volatile u32 Local_u32Count = 500u * 3000u;
    while (Local_u32Count != 0u) { Local_u32Count--; }
    MNVIC_SystemReset();          /* never returns */
}
