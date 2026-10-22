/*
 * NET_private.h — internal definitions. Included ONLY by
 * NET_program.c (plus NET_interface.h for the types below).
 *
 *  THE EIGHT INVARIANTS — from the delivered, Phase-1-proven
 *  TTT_session; keep with the code they govern; removal is a
 *  review-flagged change.
 *
 *  1. LOOP CONTEXT ONLY (now: NET-TASK context — the RTOS conversion;
 *     the hook, the parser, the executor, TX, and the narration all
 *     run in the NET task. The seam restored single-context by
 *     construction).
 *  2. COPY-BEFORE-RETURN (HESP invariant 5 rides here).
 *  3. ONE PENDING TRANSACTION. A new command ABANDONS a live
 *     entry — its reply lands in the late counter. The 20 s timeout
 *     QUARANTINES the TID. Live and quarantined TIDs are never
 *     reused. TIDs 1-255, wrap.
 *  4. CLASSIFICATION BY PENDING-ENTRY LOOKUP, NEVER ADJACENCY (B5).
 *  5. NET KNOWS TRANSACTIONS, NOT MEANING.
 *  6. THE SNAPSHOT IS PROTOCOL STATE.
 *  7. QUEUE OVERFLOW IS COUNTED, never silent.
 *  8. KNOWN TOLERANCES (documented, deliberate).
 *
 *  THE RTOS CONVERSION (the absorption): the command descriptor —
 *  crosses HUB -> NET by value (the queue copy).
 *
 *  R5 ADDITIONS: NET_CMD_SOLICIT (the offer solicitation — the D15
 *  re-emit; the ESP re-offers at STM_READY/reconnect ONLY, so a
 *  re-entry with the link up must wake the trigger) and NET_CMD_
 *  ESP_FRESET (the hub-day stuck-stage recovery — the ESP seat's
 *  bench lines, originated on the wire; both confirmations granted,
 *  v0.4.1-traced: the bare word passes the console gate with debug
 *  narration armed; zero settle time; the RAM offer survives).
 */

#ifndef NET_PRIVATE_H_
#define NET_PRIVATE_H_

#include "STD_TYPES.h"
#include "APP/NET_interface.h"

/* internal prototypes (static — program.c only) */
static void NET_voidLineHook(const u8 *Copy_pu8Line, u16 Copy_u16Len);
static u8   NET_u8StrEq(const char *Copy_chA, const char *Copy_chB);
static void NET_voidCopyBounded(char *Copy_chDst, u8 Copy_u8DstSize, const char *Copy_chSrc);
static u8   NET_u8AllocTid(void);
static NET_EnumStatus_t NET_ENUM_SEND_CMD(const char *Copy_chName, u8 Copy_u8Tid,
                                          const char *Copy_chField1,
                                          const char *Copy_chField2);
static void NET_voidParseLine(const char *Copy_chLine);
static void NET_voidEnqueue(NET_Event_t *Copy_pstEvent);
static u8   NET_u8AppendU32(char *Copy_chDst, u8 Copy_u8Pos, u32 Copy_u32Val);

/* FOTA-CS */
static u32  NET_u32ParseDec(const char *Copy_chSrc);   /* decimal field -> u32 */
static void NET_voidFotaClose(void);                    /* epoch -> text mode  */

/* ---- THE RTOS CONVERSION (absorption): the command descriptor ----
 * Crosses HUB -> NET by value (the queue copy). Sizes cover the
 * largest args: the reconnect token (32+1) and the player name
 * (20+1); the FOTA version rides Arg1, its numbers ride the u32s. */
typedef enum
{
    NET_CMD_BRINGUP = 0,
    NET_CMD_SOLICIT,          /* R5: re-emit D15 — the offer solicitation */
    NET_CMD_ESP_FRESET,       /* R5: the hub-day stuck-stage recovery     */
    NET_CMD_PROBE,
    NET_CMD_HOST,
    NET_CMD_JOIN,
    NET_CMD_RECONNECT,
    NET_CMD_MOVE,
    NET_CMD_STATE_GET,
    NET_CMD_LEAVE,
    NET_CMD_FOTA_BEGIN,
    NET_CMD_FOTA_DONE
} NET_CmdType_t;

typedef struct
{
    NET_CmdType_t Type;
    u8   Cell;          /* MOVE: the cell index           */
    u32  Size;         /* FOTA_BEGIN: the image size     */
    u32  E2eCrc;       /* FOTA_BEGIN: the e2e CRC        */
    char Arg1[36];     /* game / code / version / token  */
    char Arg2[36];     /* name / token                   */
} NET_Cmd_t;

static void NET_PRIVATE_Bringup(void);
static void NET_PRIVATE_ExecuteCmd(const NET_Cmd_t *Copy_pstCmd);
static void NET_PRIVATE_Task(void *pvParameters);
static void NET_PRIVATE_ZeroCmd(NET_Cmd_t *Copy_pstCmd);
static NET_EnumStatus_t NET_PRIVATE_EnqueueCmd(NET_Cmd_t *Copy_pstCmd);

#endif /* NET_PRIVATE_H_ */
