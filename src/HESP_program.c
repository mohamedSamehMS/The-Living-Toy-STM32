/*
 * HESP_program.c � ESP8266 link HAL (L1 v1), implementation.
 *
 *  SECTION 1 (RX transport) and SECTION 3 (TX builder): adopted
 *  VERBATIM from the validated link bench � deliverables 1 & 3 �
 *  with Patch 0 (junk announcements), the D10 separator fix, and
 *  Patch B (junk content) folded into the assembler, plus the D18
 *  stuck-line flush (30 ms). The invariant comments stay (see
 *  HESP_private.h for the full set).
 *
 *  SECTION 2 (public plumbing): counters, handler setters, the
 *  TransportInit integration patch, Patch A (HESP_TxSnapshot).
 *
 *  FOTA-CS (this revision): the carrier extension � the router in
 *  TransportPoll's drain (binary mode -> raw handler instead of the
 *  assembler; placed BEFORE Assemble so the line state never moves
 *  and D18 self-disables), the D18 explicit gate, the raw-handler /
 *  binary-mode state, the public raw TX, the last-byte getter.
 *  The ISR and the ring are UNTOUCHED � byte-agnostic by design.
 *
 *  No MUSART register access anywhere � HESP is a pure consumer.
 */

#include <MCAL/USART/MUSART_interface.h>
#include <MCAL/SYSTICK/MSYSTICK_interface.h>   /* D18: flush timebase � the
                                                   getter is a non-blocking
                                                   counter read, ISR-safe per
                                                   the driver contract        */

#include "HAL/ESP/HESP_interface.h"
#include "HAL/ESP/HESP_private.h"
#include "HAL/ESP/HESP_config.h"
/* ================================================================= *
 *  SECTION 1 � RX TRANSPORT  [BENCH, adopted verbatim + Patches 0/B *
 *                              + D5 resync fix + D18 flush]           *
 * ================================================================= */

static u8            HespRing[HESP_RING_SIZE];
static volatile u16  HespHead      = 0u;   /* written ONLY in ISR    */
static volatile u16  HespTail      = 0u;   /* written ONLY in loop   */
static volatile u32  HespDropped   = 0u;   /* ISR ++, loop reads     */
static volatile u32  HespIdleTicks = 0u;
static u8            HespLine[HESP_LINE_MAX];
static u16           HespLineLen   = 0u;
static u8            HespOverflow  = 0u;
static u32           HespJunkLines = 0u;
static HESP_LineHandler_t HespHandler     = NULL;
static HESP_JunkHandler_t HespJunkHandler = NULL;   /* Patch 0      */
static volatile u32  HespLastByteMs = 0u;  /* D18: last RX byte time,
                                              written in ISR, read in
                                              loop � aligned u32 is
                                              atomic (invariant 1)    */

/* FOTA-CS: the carrier-extension state. LOOP context only � written
   from NET's parser/task, read in TransportPoll's drain. No ISR
   contact, no volatile needed (single context, the TX-state
   discipline � NOT the RX ring's). */
static HESP_RawByteHandler_t HespRawHandler = NULL;
static u8                    HespBinaryMode = 0u;

/* ---- ISR CONTEXT (MUSART calls per received byte) � invariant 3 --- */
static void HESP_RxByteCallback(u8 Copy_u8Byte)
{
    u16 Local_u16Next = (u16)((HespHead + 1u) & HESP_RING_MASK);

    HespLastByteMs = MSYSTICK_GetElapsed_ms();   /* D18 � counter read,
                                                    ISR-safe, non-blocking.
                                                    FOTA-CS: unchanged in
                                                    binary mode � this is
                                                    NET's watchdog clock */

    if (Local_u16Next != HespTail)          /* room available */
    {
        HespRing[HespHead] = Copy_u8Byte;
        HespHead           = Local_u16Next;
    }
    else                                    /* FULL: drop-NEWEST,
                                                 count, NEVER block */
    {
        HespDropped++;
    }
}

/* ---- ISR CONTEXT � diagnostic (invariant 7: '\n' is the boundary) -- */
static void HESP_IdleCallback(void)
{
    HespIdleTicks++;
}
/* ---- LOOP CONTEXT: byte -> line assembler --------------------------
 * '\n' ends a frame; one trailing '\r' stripped; empty lines skipped;
 * over-length (cap 192 / content > 158) junked � invariant 6.
 *
 * F1 FOLD: a line of exactly "\r\n" was DELIVERED to
 * the line handler as a zero-length line. Invariant 6 says empty
 * lines are skipped; the bare-'\n' case is, the '\r'-then-'\n' case
 * was not. Fix: skip content-empty lines AFTER the strip � no handler
 * call, no junk count, silently uncounted � matching the ESP-side
 * rxLen==0 semantics (countersigned by both seats). Proof trio:
 * J3 "\r\n" -> nothing; W1 " \r\n" -> junk len=1 (whitespace-only is
 * NOT empty); P3 "A\r\r\n" -> junk len=2 (exactly ONE '\r' stripped).
 * F2: the "empty line: skip" comment moved to its true branch.      */
static u8 HESP_Assemble(u8 Copy_u8Byte)
{
    u8 Local_u8LineReady = 0u;
    if (Copy_u8Byte == (u8)'\n')
    {
        if (HespOverflow != 0u)              /* over-length line */
        {
            HespJunkLines++;
            if (HespJunkHandler != NULL) { HespJunkHandler(HespLine, HespLineLen); }
            HespOverflow = 0u;
            HespLineLen  = 0u;   /* D5 � post-junk resync (J2->V9 pair) */
        }
        else if (HespLineLen != 0u)          /* non-empty line (F2 label) */
        {
            if (HespLine[HespLineLen - 1u] == (u8)'\r')
            {   HespLineLen--;   }           /* strip ONE '\r' */

            if (HespLineLen > HESP_CONTENT_MAX)
            {
                HespJunkLines++;
                if (HespJunkHandler != NULL) { HespJunkHandler(HespLine, HespLineLen); }
            }
            else if (HespLineLen != 0u)      /* F1: deliver only if     */
            {                                /* non-empty AFTER the strip */
                HespLine[HespLineLen] = 0u;
                if (HespHandler != NULL)
                {   HespHandler(HespLine, HespLineLen);   }
                Local_u8LineReady = 1u;
            }
            /* else: len==0 after strip � skipped like the bare '\n'   */
            HespLineLen = 0u;
        }
        else                                 /* empty line: skip (F2) */
        {   HespLineLen = 0u;   }
    }
    else if (HespOverflow == 0u)
    {
        if (HespLineLen < HESP_LINE_MAX)
        {
            HespLine[HespLineLen] = Copy_u8Byte;
            HespLineLen++;
            if (HespLineLen == HESP_LINE_MAX)
            {   HespOverflow = 1u;   }
        }
    }
    return Local_u8LineReady;
}

/* ---- LOOP CONTEXT: drain everything available, then the D18 flush -- */
void HESP_TransportPoll(void)
{
    u8 Local_u8Byte;
    while (HespTail != HespHead)
    {
        Local_u8Byte = HespRing[HespTail];
        HespTail     = (u16)((HespTail + 1u) & HESP_RING_MASK);

        /* FOTA-CS: THE ROUTER. While binary mode is on AND a raw
           handler is registered, every drained byte goes to the raw
           consumer INSTEAD of the assembler � the bracket's whole
           mechanism. Placed BEFORE Assemble so the line state never
           moves in binary mode (D18 self-disables by construction).
           NULL handler = the flag is inert � normal text behavior
           (the safe pre-integration default). Loop context (the
           FOTA-12 ruling): the handler may narrate on the console. */
        if ((HespBinaryMode != 0u) && (HespRawHandler != NULL))
        {
            HespRawHandler(Local_u8Byte);
        }
        else
        {
            (void)HESP_Assemble(Local_u8Byte);
        }
    }

    /* D18: stuck-line flush. A partial/overflow line with no bytes for
       HESP_IDLE_FLUSH_MS is a dead line (boot garbage, a burst cut
       short, a torn frame): junk it and reset, so the NEXT frame starts
       clean. '\n' remains THE protocol boundary: this only retires
       lines that are already dead. FOTA-CS: explicitly gated on TEXT
       mode � belt-and-braces (the router already starves the line
       state in binary mode; this makes the gate visible, not implied). */
    if (  (HespBinaryMode == 0u)
       && (  (HespLineLen != 0u) || (HespOverflow != 0u) )
       && ((MSYSTICK_GetElapsed_ms() - HespLastByteMs) >= HESP_IDLE_FLUSH_MS) )
    {
        HespJunkLines++;
        if (HespJunkHandler != NULL) { HespJunkHandler(HespLine, HespLineLen); }
        HespLineLen  = 0u;
        HespOverflow = 0u;
    }
}

#if HESP_DEBUG_INJECT_ENABLE
void HESP_DebugInjectByte(u8 Copy_u8Byte)   /* TEST ONLY */
{   (void)HESP_Assemble(Copy_u8Byte);   }
#endif

/* ================================================================= *
 *  SECTION 2 � public plumbing                                      *
 * ================================================================= */

void HESP_SetJunkHandler(HESP_JunkHandler_t Copy_pfHandler)
{   HespJunkHandler = Copy_pfHandler;   }

/* FOTA-CS: the carrier-extension setters � the SetJunkHandler shape */
void HESP_SetRawByteHandler(HESP_RawByteHandler_t Copy_pfHandler)
{   HespRawHandler = Copy_pfHandler;   }

void HESP_SetBinaryMode(u8 Copy_u8Enable)
{   HespBinaryMode = (Copy_u8Enable != 0u) ? 1u : 0u;   }

u32 HESP_GetLastByteMs(void)
{   return HespLastByteMs;   }

u32 HESP_GetDropped(void)   { return HespDropped;   }
u32 HESP_GetJunkLines(void) { return HespJunkLines; }
u32 HESP_GetIdleTicks(void) { return HespIdleTicks; }

void HESP_TransportInit(MUSART_USART_t Copy_USART, HESP_LineHandler_t Copy_pfHandler)
{
    HESP_TxBindLink(Copy_USART);   /* deliverable-3 integration patch:
                                      one line, the only change to the
                                      adopted skeleton                  */
    HespHandler = Copy_pfHandler;
    (void)MUSART_SetRxCallback  (Copy_USART, HESP_RxByteCallback);
    (void)MUSART_SetIdleCallback(Copy_USART, HESP_IdleCallback);
    (void)MUSART_ReceiveEnable   (Copy_USART);
}

/* ================================================================= *
 *  SECTION 3 � TX BUILDER  [BENCH, adopted verbatim + D10 fix]     *
 *  No volatile anywhere in TX state � loop context only, single
 *  writer (contrast with the RX ring: invariant discipline differs
 *  BY DESIGN � do not copy RX's volatile here).
 * ================================================================= */

static u8              HespTxBuffer[HESP_TX_BUFFER_MAX];
static u16             HespTxLen        = 0u;
static u8              HespTxFieldCount = 0u;
static u8              HespTxPoisoned   = 0u;
static MUSART_USART_t  HespTxLink       = (MUSART_USART_t)0u;
static u8              HespTxLinkBound  = 0u;

/* [BENCH] port � validated as Uart_SendString on the link bench  */
static void HESP_TxRawString(MUSART_USART_t Copy_USART, const char *Copy_pchText)
{
    while (*Copy_pchText != '\0')
    {
        if (MUSART_SendByteBlocking(Copy_USART, (u8)*Copy_pchText) != MUSART_OK)
        {
            return;   /* timeout / not enabled: abort the string */
        }
        Copy_pchText++;
    }
}

/* [BENCH] port � validated as Uart_SendBytes                      */
static void HESP_TxRawBytes(MUSART_USART_t Copy_USART, const u8 *Copy_pu8Data, u16 Copy_u16Length)
{
    u16 Local_u16Index;
    for (Local_u16Index = 0u; Local_u16Index < Copy_u16Length; Local_u16Index++)
    {
        if (MUSART_SendByteBlocking(Copy_USART, Copy_pu8Data[Local_u16Index]) != MUSART_OK)
        {
            return;
        }
    }
}

/* FOTA-CS: the public raw-TX � one byte on the bound link. The
   epoch's ACK/NAK path. Bind-checked; NO [TX] mirror (the documented
   mirror gap � the receiver's USART2 narration is the epoch's bench
   evidence instead). */
HESP_Error_t HESP_TxRawByte(u8 Copy_u8Byte)
{
    if (HespTxLinkBound == 0u)
    {
        return HESP_NOT_INIT;
    }
    if (MUSART_SendByteBlocking(HespTxLink, Copy_u8Byte) != MUSART_OK)
    {
        return HESP_TX_MUSART;
    }
    return HESP_OK;
}

/* [EXT] case-sensitive exact prefix compare                       */
static u8 HESP_NameHasPrefix(const char *Copy_pchName, const char *Copy_pchPrefix)
{
    while (*Copy_pchPrefix != '\0')
    {
        if (*Copy_pchName != *Copy_pchPrefix)
        {
            return 0u;
        }
        Copy_pchName++;
        Copy_pchPrefix++;
    }
    return 1u;
}

/* [EXT] bounded append � POISONS on overflow instead of truncating.
   A truncated frame on the wire is worse than a refused frame.     */
static HESP_Error_t HESP_TxAppendChar(u8 Copy_u8Char)
{
    if (HespTxPoisoned != 0u)
    {
        return HESP_TX_OVERFLOW;
    }
    if (HespTxLen >= HESP_TX_CONTENT_MAX)
    {
        HespTxPoisoned = 1u;
        return HESP_TX_OVERFLOW;
    }
    HespTxBuffer[HespTxLen] = Copy_u8Char;
    HespTxLen++;
    return HESP_OK;
}

static HESP_Error_t HESP_TxAppendString(const char *Copy_pchText)
{
    while (*Copy_pchText != '\0')
    {
        HESP_Error_t Local_Error = HESP_TxAppendChar((u8)*Copy_pchText);
        if (Local_Error != HESP_OK)
        {
            return Local_Error;
        }
        Copy_pchText++;
    }
    return HESP_OK;
}

/* [BENCH algorithm, retargeted] u16 -> decimal digits            */
static HESP_Error_t HESP_TxAppendU16(u16 Copy_u16Value)
{
    u8  Local_au8Digits[5u];
    u8  Local_u8Count = 0u;
    u8  Local_u8Index;

    if (Copy_u16Value == 0u)
    {
        return HESP_TxAppendChar((u8)'0');
    }
    while ((Copy_u16Value > 0u) && (Local_u8Count < 5u))
    {
        Local_au8Digits[Local_u8Count] = (u8)('0' + (Copy_u16Value % 10u));
        Local_u8Count++;
        Copy_u16Value = (u16)(Copy_u16Value / 10u);
    }
    for (Local_u8Index = Local_u8Count; Local_u8Index > 0u; Local_u8Index--)
    {
        HESP_Error_t Local_Error = HESP_TxAppendChar(Local_au8Digits[Local_u8Index - 1u]);
        if (Local_Error != HESP_OK)
        {
            return Local_Error;
        }
    }
    return HESP_OK;
}

/* Separator rule: FIRST field ':' (after TID, or after the name for
   EVENTs); every further field ','. The separator is appended
   exactly once per field � THIS is the increment point (D10). */
static HESP_Error_t HESP_TxAppendSeparator(void)
{
    HESP_Error_t Local_Error;
    if (HespTxFieldCount == 0u)
    {
        Local_Error = HESP_TxAppendChar((u8)':');
    }
    else
    {
        Local_Error = HESP_TxAppendChar((u8)',');
    }
    if (Local_Error == HESP_OK)
    {
        HespTxFieldCount++;
    }
    return Local_Error;
}

void HESP_TxBindLink(MUSART_USART_t Copy_USART)
{
    HespTxLink      = Copy_USART;
    HespTxLinkBound = 1u;
}

HESP_Error_t HESP_TxBegin(const char *Copy_pchName, u8 Copy_u8Tid)
{
    HESP_Error_t Local_Error;

    /* Begin implicitly abandons any unsent build. */
    HespTxLen        = 0u;
    HespTxFieldCount = 0u;
    HespTxPoisoned   = 0u;

    if (Copy_pchName == NULL)
    {
        return HESP_TX_ARGS;
    }

    /* Misuse guards � mirror the RECEIVER's classification rule    */
    if (HESP_NameHasPrefix(Copy_pchName, "EVENT_") != 0u)
    {
        if (Copy_u8Tid != 0u)          /* EVENTs carry no TID */
        {
            return HESP_TX_ARGS;
        }
    }
    else if ((HESP_NameHasPrefix(Copy_pchName, "CMD_")  != 0u) ||
             (HESP_NameHasPrefix(Copy_pchName, "RESP_") != 0u))
    {
        if (Copy_u8Tid == 0u)          /* CMD/RESP require a TID */
        {
            return HESP_TX_ARGS;
        }
    }
    else
    {
        return HESP_TX_ARGS;           /* not one of the three types */
    }

    Local_Error = HESP_TxAppendString(Copy_pchName);
    if (Local_Error != HESP_OK)
    {
        return Local_Error;
    }
    if (Copy_u8Tid != 0u)
    {
        Local_Error = HESP_TxAppendChar((u8)':');
        if (Local_Error == HESP_OK)
        {
            Local_Error = HESP_TxAppendU16((u16)Copy_u8Tid);
        }
    }
    return Local_Error;
}

/* F4 FOLD: the field-charset guard � refuse BEFORE any append. */
HESP_Error_t HESP_TxFieldStr(const char *Copy_pchField)
{
    HESP_Error_t Local_Error;
    const char  *Local_pchScan;

    if (Copy_pchField == NULL)
    {
        Copy_pchField = "";            /* NULL tolerated as empty field */
    }

    for (Local_pchScan = Copy_pchField; *Local_pchScan != '\0'; Local_pchScan++)
    {
        if ((*Local_pchScan == ',') ||
            (*Local_pchScan < (char)0x20) || (*Local_pchScan > (char)0x7E))
        {
            return HESP_TX_ARGS;       /* F4: refuse, nothing appended */
        }
    }

    Local_Error = HESP_TxAppendSeparator();
    if (Local_Error != HESP_OK)
    {
        return Local_Error;
    }
    return HESP_TxAppendString(Copy_pchField);
}

HESP_Error_t HESP_TxFieldU16(u16 Copy_u16Value)
{
    HESP_Error_t Local_Error = HESP_TxAppendSeparator();
    if (Local_Error != HESP_OK)
    {
        return Local_Error;
    }
    return HESP_TxAppendU16(Copy_u16Value);
}

HESP_Error_t HESP_TxSend(void)
{
    HESP_Error_t Local_Error = HESP_OK;
    u16          Local_u16Index;

    if (HespTxLinkBound == 0u)
    {
        return HESP_NOT_INIT;
    }
    if (HespTxPoisoned != 0u)
    {
        HespTxLen = 0u;  HespTxFieldCount = 0u;  HespTxPoisoned = 0u;
        return HESP_TX_OVERFLOW;       /* refused, never truncated  */
    }
    if (HespTxLen == 0u)
    {
        HespTxFieldCount = 0u;
        return HESP_TX_ARGS;           /* nothing built             */
    }

    /* CRLF lives OUTSIDE the content budget � direct writes. */
    HespTxBuffer[HespTxLen]      = (u8)'\r';
    HespTxBuffer[HespTxLen + 1u] = (u8)'\n';
    HespTxLen = (u16)(HespTxLen + 2u);

    for (Local_u16Index = 0u; Local_u16Index < HespTxLen; Local_u16Index++)
    {
        if (MUSART_SendByteBlocking(HespTxLink, HespTxBuffer[Local_u16Index]) != MUSART_OK)
        {
            /* Abort. A partial frame is on the wire; the receiver's
               '\n' resync (J-vector proof) makes this safe.        */
            Local_Error = HESP_TX_MUSART;
            break;
        }
    }

#if HESP_TX_MIRROR_ENABLE
    if (Local_Error == HESP_OK)
    {
        HESP_TxRawString(HESP_TX_MIRROR_USART, "[TX] ");
    }
    else
    {
        HESP_TxRawString(HESP_TX_MIRROR_USART, "[TX-FAIL] ");
    }
    HESP_TxRawBytes  (HESP_TX_MIRROR_USART, HespTxBuffer, HespTxLen);
    HESP_TxRawString(HESP_TX_MIRROR_USART, "\r\n");
#endif

    /* Always reset � next Begin starts clean whatever happened      */
    HespTxLen = 0u;  HespTxFieldCount = 0u;  HespTxPoisoned = 0u;
    return Local_Error;
}

/* Patch A (D11 closure) � byte-true gate for the TX builder.      */
u16 HESP_TxSnapshot(u8 *Copy_pu8Out, u16 Copy_u16Max)
{
    u16 Local_u16Index;
    u16 Local_u16Len = HespTxLen;
    if (Local_u16Len > Copy_u16Max) { Local_u16Len = Copy_u16Max; }
    for (Local_u16Index = 0u; Local_u16Index < Local_u16Len; Local_u16Index++)
    {   Copy_pu8Out[Local_u16Index] = HespTxBuffer[Local_u16Index];   }
    return Local_u16Len;
}
