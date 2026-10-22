/*
 * APP_FOTA.h - FOTA receiver, application layer (FOTA seat, v2.1)
 *
 *  ONE TRANSPORT-AGNOSTIC CORE, TWO BINDINGS:
 *
 *  [BENCH] APP_FOTA_Init()/Run() - the v1 contract: the wire is
 *          APP_FOTA_BENCH_USART (config; default USART1), the
 *          fota_send.py wire protocol (text header + frames),
 *          console-style prints on the SAME wire, self-reset
 *          after OK STAGED. main.c V8 and the PC sender keep
 *          working without modification - the regression suite.
 *
 *  [NET]   The bracket seam (L1-REPLY-02/04 rulings):
 *          1. hub/NET sees EVENT_FOTA_AVAILABLE, decides
 *          2. APP_FOTA_enumPrepare() - erases staging NOW
 *             (~2.3 s core stall: runs in TEXT mode, before
 *             CMD_FOTA_BEGIN, so nothing is in flight)
 *          3. NET sends CMD_FOTA_BEGIN; on RESP_FOTA_READY:
 *             NET switches HESP to binary mode and calls
 *             APP_FOTA_enumBeginNet(version, length, e2e_crc,
 *             size_echo, raw_tx) - the receiver arms
 *          4. HESP's raw router (loop context, before the
 *             assembler) feeds APP_FOTA_voidFeedByte(); ACK/NAK
 *             go out through the registered raw-TX function; NO
 *             text leaves USART1 during the epoch (quiesce law);
 *             USART2 narration is the diagnostics wire
 *          5. APP_FOTA_voidPoll() processes frames; on
 *             completion the status becomes terminal
 *          6. NET closes binary mode, runs CMD_FOTA_DONE with
 *             APP_FOTA_pchGetStatusToken() (the REGISTERED set:
 *             OK / E2E_FAIL - ABORTED is the default; WATCHDOG
 *             is NET's own token); on the DONE terminal +
 *             success it calls APP_FOTA_voidNetReset()
 *
 *  WIRE PROTOCOL v1.6 (frozen - FOTA-SPEC-1): 128-byte frames +
 *  4-byte big-endian CRC, ACK 0x06 / NAK 0x15, 0xAA resync,
 *  stop-and-wait. The negotiated-size field is carried by the
 *  bracket; this receiver supports 128 ONLY and returns a loud
 *  FOTA_STATUS_ERR_SIZE on any other echo (buffers grow the day
 *  a size is actually wanted - honest now).
 *
 *  Revision history:
 *    v2.1 - config file added (bench-wire + narration knobs);
 *           DONE token map aligned to the registered set
 *           (L1-REPLY-02/FOTA-13); bench error lines restored
 *           to v1.7-exact strings (wire-compat).
 *    v2.0 - transport-agnostic refactor: net-mode API, v1 bench
 *           API preserved, error paths become statuses.
 *    v1.7 - auto-reset after staging (bench mode keeps it).
 *    v1.6/1.5 - the proven receiver.
 *	  v2.2 - wire protocol v1.7: the sequence byte (FOTA-SPEC-2); duplicate re-ACK; DUP counter in the bench completion line.
 *  Placement (app-layer 3-file convention): include/APP/ +
 *  src/APP/ - config (policy) / interface / implementation.
 */

#ifndef APP_FOTA_H_
#define APP_FOTA_H_

#include "STD_TYPES.h"

/* ---- terminal + local statuses ----
 * The first two map to the REGISTERED DONE wire tokens; the rest
 * are pre-epoch or local failures (NET refuses before BEGIN, or
 * reports them via the ABORTED default + console detail). */
typedef enum
{
    FOTA_STATUS_IDLE = 0,
    FOTA_STATUS_RECEIVING,
    FOTA_STATUS_STAGED_OK,        /* wire token: OK             */
    FOTA_STATUS_ERR_E2E,          /* wire token: E2E_FAIL       */
    FOTA_STATUS_ERR_SIZE,         /* size echo != 128            */
    FOTA_STATUS_ERR_LENGTH,       /* footprint > staging - trailer */
    FOTA_STATUS_ERR_ERASE,        /* staging erase failed        */
    FOTA_STATUS_ERR_FLASH,        /* frame write failed          */
    FOTA_STATUS_ERR_CRC_ENGINE,   /* MCRC rejected a call        */
    FOTA_STATUS_ERR_TRAILER,      /* trailer write failed        */
    FOTA_STATUS_ERR_PARAM,
    FOTA_STATUS_ERR_NOT_ARMED     /* Begin/Prepare order violated */
} APP_FOTA_Status_t;

/* The raw-TX primitive NET registers (HESP raw-send wrapper).
 * LOOP context; one byte; must not block meaningfully. */
typedef void (*APP_FOTA_TxByte_t)(u8 Copy_u8Byte);

/* ================= v1 BENCH MODE (unchanged) ================= */
void APP_FOTA_Init(void);   /* erase staging, print READY, arm   */
void APP_FOTA_Run(void);    /* main-loop pump (bench binding)   */

/* ===================== v2 NET MODE =========================== */

/* Erase staging - call in TEXT mode, BEFORE NET sends
 * CMD_FOTA_BEGIN (the ~2.3 s stall must never meet traffic). */
APP_FOTA_Status_t APP_FOTA_enumPrepare(void);

/* Arm the receiver with the bracket's parameters. Call from
 * NET's RESP_FOTA_READY handling, before/at binary-mode entry. */
APP_FOTA_Status_t APP_FOTA_enumBeginNet(u32 Copy_u32Version,
                                        u32 Copy_u32Length,
                                        u32 Copy_u32E2eCrc,
                                        u16 Copy_u16FrameSizeEcho,
                                        APP_FOTA_TxByte_t Copy_pfTxByte);

/* The raw-byte consumer. LOOP context (HESP router). ISR-safe
 * by construction (the bench binding exercises it from the
 * MUSART ISR) - the volatile discipline is kept for that path. */
void APP_FOTA_voidFeedByte(u8 Copy_u8Data);

/* Main-loop pump: process complete frames, finalize. */
void APP_FOTA_voidPoll(void);

/* Status + progress (the UI/N narration source). */
APP_FOTA_Status_t APP_FOTA_enumGetStatus(void);
u32 APP_FOTA_u32GetFramesDone(void);
u32 APP_FOTA_u32GetFramesTotal(void);

/* The amendment's DONE-transaction token for the current status
 * (single mapping point - NET builds the frame). */
const char * APP_FOTA_pchGetStatusToken(void);

/* NET calls this on the RESP_FOTA_DONE terminal after a
 * successful update: brief pause, then the board resets and
 * the bootloader commits. NEVER RETURNS. */
void APP_FOTA_voidNetReset(void);

#endif /* APP_FOTA_H_ */
