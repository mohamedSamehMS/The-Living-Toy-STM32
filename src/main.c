/*
 * main.c — STAGE 2 + ABSORPTION: hub, input seam, buzzer — and the
 *           NET task, idle until Tic-Tac-Toe's online entry.
 *  LOCATION: src/ (the copy tree — the RTOS tree).
 *
 *  THE IMAGE'S LINEAGE (every image in this tree, newest last):
 *    Rev 1/2       — the reference hub boot (glass-verified)
 *    Gate D        — + VTOR remap, capture-edition IR (glass-verified)
 *    Stage 1       — the kernel handoff: MSYSTICK retired, hub loop
 *                    verbatim inside ONE FreeRTOS task (glass-verified)
 *    Stage 2 (R2)  — + the input seam: input task + event queue, and
 *                    HBUZZER (the input blip) (glass-verified)
 *    ABSORPTION (R4, this build) — + the NET task (the comm class,
 *                    idle until TTT's online entry brings the link up)
 *                    and the versioned-image pair (APP_BUILD_VERSION_STR
 *                    and NET_FW_ID). The TTT
 *                    module (offline + online) rides in the hub's
 *                    dispatch row, inside the HUB task.
 *
 *  WHAT THIS IMAGE RUNS (four tasks, the config's exact ladder):
 *    INPUT (prio 3) — drains HNEC every 10 ms, posts to the event
 *                     queue; the blip fires inside GetEvent
 *    HUB   (prio 2) — HUB_enumRun → menus → games (incl. TTT's Run,
 *                     which owns the net states + the session drain)
 *    NET   (prio 1) — the wire owner: pumps HESP, parses, executes
 *                     commands, sweeps timeouts + the FOTA watchdog.
 *                     IDLE until NET_enumBringup (TTT's online entry)
 *                     — USART1 stays dark at boot, by structure
 *    IDLE  (prio 0) — kernel-created
 *
 *  THE ESP-SILENT RULING, NOW STRUCTURAL: no boot path anywhere in
 *  this file touches USART1 — no clock enable, no pin config, no
 *  MUSART_Init. It all lives inside NET's bring-up command, issued
 *  by the TTT module's online entry. Offline play = literally zero
 *  wire traffic, now guaranteed by the absence of the code.
 *
 *  ADDITIONS IN THIS REVISION (R4 — the absorption, File 4 of 5):
 *    + APP_BUILD_VERSION_STR / NET_FW_ID — the versioned-image pair's
 *      hub half; NET_FW_ID (NET_config.h) is the other half; the
 *      array-size typedef pins length-equality at compile time, and
 *      the boot banner prints both side by side (the runtime half of
 *      the tie — a mismatch is visible on every first console line).
 *    + NET_enumSpawn() — the two queues + the NET task, pre-scheduler.
 *    + The identity line: R4's banner + the FW/NET version line.
 *  UNCHANGED FROM R2: everything else — the boot sequence's order,
 *  the fail/heartbeat pins, TIM5 capture IR, console, TFT chain,
 *  the store, the hub handoff. One variable per flash held here too:
 *  this image's only new runtime element is the (idle) NET task.
 *
 *  STAGE 1/2 notes stand (see the git history / prior banners):
 *    - MSYSTICK_program.c is EXCLUDED from this build; the API lives
 *      in MSYSTICK_shim.c (GetElapsed_ms -> xTaskGetTickCount).
 *    - The kernel hooks + HardFault signature: PA1 SOLID = runtime
 *      death; PA1 BLINKING = a boot init failure (raw loops).
 *    - [ST2] every 30 s: stack high-water marks + the queue-drop
 *      health line (flat between lines = healthy).
 *
 *  Revision history (this file):
 *    Rev 1/2 — reference hub main (approved, glass-verified)
 *    Gate D  — capture-edition boot delta (glass-verified, CLOSED)
 *    Stage 1 — kernel handoff (glass-verified, CLOSED)
 *    Stage 2 — input seam + buzzer blip (glass-verified, CLOSED)
 *    R4      — TTT absorption: the NET task + the version pair (THIS)
 */

#include "STD_TYPES.h"
#include <MCAL/RCC/MRCC_interface.h>
#include <MCAL/GPIO/MGPIO_interface.h>
#include <MCAL/SPI/MSPI_interface.h>
#include <MCAL/TIM/MTIM_interface.h>
#include <MCAL/FLS/MFLS_interface.h>
#include "MNVIC_interface.h"
#include <MCAL/USART/MUSART_interface.h>
#include <HAL/TFT/HTFT_interface.h>
#include "HAL/NEC/HNEC_REMOTE_interface.h"
#include "HAL/BUZZER/HBUZZER_interface.h"      /* R2: the shared buzzer */
#include "HAL/AUDIO/HAUDIO_interface.h"        /* sampled 74HC595 audio */

#include "APP/APP_FOTA_NET.h"       /* R5: the orchestrator (File 4)      */
#include "APP/APP_FOTA_UI.h"        /* R5: the update UI (the boot init)  */

#include "APP/HUB_interface.h"
#include "APP/APP_STORE.h"
#include "APP/APP_INPUT.h"          /* Stage 2: the input seam            */
#include "APP/NET_interface.h"      /* R4: the NET task's spawn           */
#include "APP/NET_config.h"         /* R4: NET_FW_ID (the version tie)    */

#include "OS/FreeRTOS.h"
#include "OS/task.h"

/* =====================================================================
 *  R4 ADDITION — THE VERSIONED-IMAGE PAIR (the versioned-image rule,
 *  the FOTA discipline): the build's identity string. NET_FW_ID
 *  (NET_config.h) MUST equal this. The pin: the array-size typedef
 *  catches length drift at compile time; the boot banner prints both
 *  strings side by side, making content drift visible on every boot.
 *  (The honest scope: the typedef is length-only — a same-length
 *  typo slips it; the banner line is the content check. Both cheap.)
 * ===================================================================== */
#define APP_BUILD_VERSION_STR  "14.4.0"

typedef char NET_fw_id_len_check_t[
    (sizeof(NET_FW_ID) == sizeof(APP_BUILD_VERSION_STR)) ? 1 : -1];

/* ---- boot-failure loop (verbatim from the reference) ---------------- */
static void App_voidInitFail(void)
{
    volatile u32 Local_u32Delay;
    (void)MRCC_EnablePeripheralClock(MRCC_GPIOA);
    (void)MGPIO_SetPinMode(MGPIO_PORTA, MGPIO_PIN_1, MGPIO_MODE_OUTPUT);
    (void)MGPIO_SetPinOutputConfig(MGPIO_PORTA, MGPIO_PIN_1, MGPIO_OUTPUT_PUSH_PULL, MGPIO_SPEED_LOW);
    for (;;)
    {
        (void)MGPIO_SetPinValue(MGPIO_PORTA, MGPIO_PIN_1, MGPIO_PIN_HIGH);
        for (Local_u32Delay = 0u; Local_u32Delay < 600000u; Local_u32Delay++) { }
        (void)MGPIO_SetPinValue(MGPIO_PORTA, MGPIO_PIN_1, MGPIO_PIN_LOW);
        for (Local_u32Delay = 0u; Local_u32Delay < 600000u; Local_u32Delay++) { }
    }
}

/* ---- THE task: the whole hub, verbatim -------------------------------- */
static void HubTask(void *pvParameters)
{
    (void)pvParameters;
    /* The Stage-1 premise made literal: HUB_enumRun owns the hub's
       persistent application loop and its service instrumentation. */
    HUB_enumRun();            /* never returns */
    for (;;) { }              /* a FreeRTOS task must not return */
}

/* ---- Kernel hooks: the loud-failure Constitution, kernel edition ------
   PA1 SOLID = runtime death. Deliberately NO console printing from these
   contexts — an assert can fire inside a kernel critical section or an
   ISR, where a blocking UART wait is the wrong tool. The LED is the
   signature; the debugger is the diagnostic.                            */
void vAssertCalled(const char * const pcFile, const int iLine)
{
    (void)pcFile;
    (void)iLine;
    (void)MGPIO_SetPinValue(MGPIO_PORTA, MGPIO_PIN_1, MGPIO_PIN_HIGH);
    for (;;) { }
}

void vApplicationMallocFailedHook(void)
{
    (void)MGPIO_SetPinValue(MGPIO_PORTA, MGPIO_PIN_1, MGPIO_PIN_HIGH);
    for (;;) { }
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask; (void)pcTaskName;
    (void)MGPIO_SetPinValue(MGPIO_PORTA, MGPIO_PIN_1, MGPIO_PIN_HIGH);
    for (;;) { }
}

/* Loud hard-fault signature — overrides the template's weak handler.
   MemManage / BusFault / UsageFault all ESCALATE to HardFault when
   their own handlers are not enabled (the default) — one catchpoint
   covers every fault class. The BSRR single store is safe from fault
   context; a fault before step 2's GPIOA clock enable is simply
   ignored and we halt — still recognizable as "dead".               */
void HardFault_Handler(void)
{
    (void)MGPIO_SetPinValue(MGPIO_PORTA, MGPIO_PIN_1, MGPIO_PIN_HIGH);
    for (;;) { }
}


/* Stage-2 instrument: the stats line reads the hub task's high-water. */
TaskHandle_t App_xHubTaskHandle = NULL;


int main(void)
{
    /* ---- 0. vector table remap (first statement — proven chain) ---- */
    if (MNVIC_SetVectorTableBase(0x08004000UL) != MNVIC_OK) { App_voidInitFail(); }

    /* ---- 1. clock tree -------------------------------------------- */
    if (MRCC_Init() != MRCC_OK)                     { App_voidInitFail(); }

    /* ---- 2. peripheral clocks (R2's set — UNCHANGED in R4) ---------
     * NO MRCC_USART1 and NO GPIO work for PA9/PA10: the ESP link's
     * entire bring-up lives inside NET's bring-up command (the
     * deferred-link ruling). The absence IS the ruling, enforced.   */
    (void)MRCC_EnablePeripheralClock(MRCC_GPIOA);   /* IR PA0 (AF2), fail LED, SPI, console */
    (void)MRCC_EnablePeripheralClock(MRCC_GPIOB);   /* TFT CS/RST/DC, buzzer PB8            */
    (void)MRCC_EnablePeripheralClock(MRCC_GPIOC);   /* PC13 heartbeat                       */
    (void)MRCC_EnablePeripheralClock(MRCC_TIM5);    /* the capture engine's timer           */
    (void)MRCC_EnablePeripheralClock(MRCC_SPI1);    /* TFT bus                              */
    (void)MRCC_EnablePeripheralClock(MRCC_USART2);  /* console                              */
    (void)MRCC_EnablePeripheralClock(MRCC_TIM10);   /* R2: the buzzer's tone engine         */
    (void)MRCC_EnablePeripheralClock(MRCC_TIM9);    /* sampled audio timer                  */

    /* ---- 3. fail LED / 4. heartbeat (unchanged) --------------------- */
    (void)MGPIO_SetPinMode        (MGPIO_PORTA, MGPIO_PIN_1, MGPIO_MODE_OUTPUT);
    (void)MGPIO_SetPinOutputConfig(MGPIO_PORTA, MGPIO_PIN_1, MGPIO_OUTPUT_PUSH_PULL, MGPIO_SPEED_LOW);
    (void)MGPIO_SetPinValue       (MGPIO_PORTA, MGPIO_PIN_1, MGPIO_PIN_LOW);
    (void)MGPIO_SetPinMode        (MGPIO_PORTC, MGPIO_PIN_13, MGPIO_MODE_OUTPUT);
    (void)MGPIO_SetPinOutputConfig(MGPIO_PORTC, MGPIO_PIN_13, MGPIO_OUTPUT_PUSH_PULL, MGPIO_SPEED_LOW);
    (void)MGPIO_SetPinValue       (MGPIO_PORTC, MGPIO_PIN_13, MGPIO_PIN_HIGH);

    /* ---- 5/6. interrupt priorities + the timer table (unchanged) ---- */
    if (MNVIC_Init() != MNVIC_OK)                  { App_voidInitFail(); }
    (void)MNVIC_SetPriority(MNVIC_TIM5, 5u, 0u);   /* IR capture; kernel sets SysTick/PendSV to 15 itself */
    (void)MNVIC_SetPriority(MNVIC_TIM1_UP_TIM10, 5u, 0u);  /* R2: the buzzer's tone-end ISR —
                                    TIM10 rides TIM1's update vector (25) */
    if (MTIM_Init() != MTIM_OK)                    { App_voidInitFail(); }
    if (HBUZZER_enumInit() != HBUZZER_OK)         { App_voidInitFail(); }
    /* HAUDIO configures TIM9's shared vector priority internally to
       priority 5; this is a deliberate HAL layering deviation, and
       remains below the FreeRTOS syscall mask as required. */
    if (HAUDIO_enumInit() != HAUDIO_OK)           { App_voidInitFail(); }

    /* ---- 7. [GONE since Stage 1: MSYSTICK_Init] — the kernel owns
            SysTick from vPortStartScheduler(); the API lives in the
            shim. ------------------------------------------------------- */

    /* ---- 8. console: USART2 on PA2/PA3 (unchanged) ------------------ */
    (void)MGPIO_SetPinMode        (MGPIO_PORTA, MGPIO_PIN_2, MGPIO_MODE_ALT_FUNC);
    (void)MGPIO_SetPinOutputConfig(MGPIO_PORTA, MGPIO_PIN_2, MGPIO_OUTPUT_PUSH_PULL, MGPIO_SPEED_VERY_HIGH);
    (void)MGPIO_SetPinAltFunction (MGPIO_PORTA, MGPIO_PIN_2, MGPIO_AF7);
    (void)MGPIO_SetPinMode        (MGPIO_PORTA, MGPIO_PIN_3, MGPIO_MODE_ALT_FUNC);
    (void)MGPIO_SetPinOutputConfig(MGPIO_PORTA, MGPIO_PIN_3, MGPIO_OUTPUT_PUSH_PULL, MGPIO_SPEED_VERY_HIGH);
    (void)MGPIO_SetPinAltFunction (MGPIO_PORTA, MGPIO_PIN_3, MGPIO_AF7);
    if (MUSART_Init(MUSART_USART2) != MUSART_OK)   { App_voidInitFail(); }
    /* ---- R4 ADDITION: the flash identity + the version-tie line ------
     * The version-line method: this banner names the image; the second
     * line prints APP_BUILD_VERSION_STR and NET_FW_ID side by side —
     * the runtime half of the versioned-image tie (the compile-time
     * half is the length typedef above). A mismatch between the two
     * strings is visible HERE, on every boot, before anything else
     * runs — and the FOTA offer-compare consumes this same truth.   */
    {
        const char *Local_pchId =
            "\r\n=== HUB RTOS R6: GAME AUDIO - themes + lose stings ===\r\n";
        const char *Local_pchV;
        while (*Local_pchId != '\0')
        {
            (void)MUSART_SendByteBlocking(MUSART_USART2, (u8)(*Local_pchId));
            Local_pchId++;
        }
        {
            static const char Local_achFw[]  = "FW ";
            static const char Local_achNet[] = " / NET ";
            static const char Local_achEol[] = "\r\n";
            const char *Local_apchParts[4];
            u8 Local_u8P;

            Local_apchParts[0] = Local_achFw;
            Local_apchParts[1] = APP_BUILD_VERSION_STR;
            Local_apchParts[2] = Local_achNet;
            Local_apchParts[3] = NET_FW_ID;

            for (Local_u8P = 0u; Local_u8P < 4u; Local_u8P++)
            {
                Local_pchV = Local_apchParts[Local_u8P];
                while (*Local_pchV != '\0')
                {
                    (void)MUSART_SendByteBlocking(MUSART_USART2, (u8)(*Local_pchV));
                    Local_pchV++;
                }
            }
            Local_pchV = Local_achEol;
            while (*Local_pchV != '\0')
            {
                (void)MUSART_SendByteBlocking(MUSART_USART2, (u8)(*Local_pchV));
                Local_pchV++;
            }
        }
    }

    /* ---- 9. the IR remote stack (unchanged — TIM5 capture, kernel-free) ---- */
    if (HNEC_REMOTE_enumInit() != HNEC_REMOTE_OK)  { App_voidInitFail(); }

    /* ---- 10. the display chain (unchanged; the ~410 ms of init delays
            run through the SHIM's calibrated loop — pre-scheduler) ---- */
    (void)MGPIO_SetPinMode        (MGPIO_PORTA, MGPIO_PIN_5, MGPIO_MODE_ALT_FUNC);
    (void)MGPIO_SetPinOutputConfig(MGPIO_PORTA, MGPIO_PIN_5, MGPIO_OUTPUT_PUSH_PULL, MGPIO_SPEED_HIGH);
    (void)MGPIO_SetPinAltFunction (MGPIO_PORTA, MGPIO_PIN_5, MGPIO_AF5);
    (void)MGPIO_SetPinMode        (MGPIO_PORTA, MGPIO_PIN_6, MGPIO_MODE_ALT_FUNC);
    (void)MGPIO_SetPinPull        (MGPIO_PORTA, MGPIO_PIN_6, MGPIO_NO_PULL);
    (void)MGPIO_SetPinAltFunction (MGPIO_PORTA, MGPIO_PIN_6, MGPIO_AF5);
    (void)MGPIO_SetPinMode        (MGPIO_PORTA, MGPIO_PIN_7, MGPIO_MODE_ALT_FUNC);
    (void)MGPIO_SetPinOutputConfig(MGPIO_PORTA, MGPIO_PIN_7, MGPIO_OUTPUT_PUSH_PULL, MGPIO_SPEED_HIGH);
    (void)MGPIO_SetPinAltFunction (MGPIO_PORTA, MGPIO_PIN_7, MGPIO_AF5);
    MSPI_voidInit(MSPI1);
    HTFT_voidInit();

    /* ---- 11/12. store + input seam + NET task + hub init --------------
     * R4 ADDITION: NET_enumSpawn creates the two queues + the NET task
     * (priority 1, the comm class, 2 KB stack). The task idles at ~zero
     * cost until the first bring-up command — which the TTT module's
     * online entry issues (NET_enumBringup). Pre-scheduler creation,
     * same pattern as the input seam's: the task exists before the
     * scheduler starts; nothing runs until then.                       */
    APP_STORE_enumInit();
    if (APP_INPUT_enumInit() != APP_INPUT_OK)      { App_voidInitFail(); }
    if (NET_enumSpawn() != NET_OK)                { App_voidInitFail(); }   /* R4 */

    /* ---- R5: THE ORCHESTRATOR'S BOOT INIT (File 4's missing edit) ----
     * Stores the running version (the compare's truth — without this,
     * Global_achThisVersion is EMPTY and every offer is trivially
     * "newer"), fetches the staging bounds (the capacity pre-check's
     * truth — without this, Global_u32StagingBytes is garbage), inits
     * the UI engine, and registers HESP's raw trampoline (armed before
     * any session can flip binary mode — the READY-parse race design).
     * Pre-scheduler, after the NET task exists, before the hub runs. */
    if (APP_FOTA_NET_enumInit(APP_BUILD_VERSION_STR) != NET_OK)
    {
        App_voidInitFail();   /* the orchestrator must boot or nothing FOTA does */
    }

    HUB_enumInit();

    /* ---- 13. THE HANDOFF: tasks + scheduler --------------------------- */
    /* HUB: 1024 words = 4 KB;
       priority 2 = APP. INPUT (priority 3) was created inside
       APP_INPUT_enumInit above; NET (priority 1) inside NET_enumSpawn. */
    if (xTaskCreate(HubTask, "HUB", 1024u, NULL, 2u, &App_xHubTaskHandle) != pdPASS)
    {
        App_voidInitFail();     /* heap exhausted at creation — loud  */
    }
    vTaskStartScheduler();      /* never returns */

    for (;;) { }                /* unreachable — the scheduler owns us */
    return 0;
}
