/*
 * FreeRTOSConfig.h — Project 4, the RTOS copy tree, Stage 1
 *
 *  Target : STM32F401CCU6 (Cortex-M4), soft-float build
 *  Kernel : FreeRTOS V11.1.0, GCC/ARM_CM4F port — OWNER RULING: stays
 *           CM4F for the future FPU flip. Today's soft-float build
 *           runs it inert-but-correct (no FP instruction exists in
 *           this image, so the extended context path is never taken);
 *           flip-day is flags-only: -mfloat-abi=hard + rebuild, zero
 *           file changes.
 *  Clock  : consumed from MRCC's PUBLISHED MODEL — the 16->84 MHz
 *           flip recomputes the tick reload at compile time, one file
 *
 *  GROUND TRUTHS this config is built on (all verified this session):
 *    - 4 implemented priority bits (MNVIC/MSYSTICK, PM0214) -> 16 levels
 *    - Priority plan: EXTI=1, DMA=2, USART=3 sit ABOVE the syscall
 *      mask -> they NEVER call FromISR APIs (they don't); TIM4=4 /
 *      TIM5=5 sit at/below it -> FromISR-eligible at Stage 2
 *    - Handler symbols weak in system/src/cortexm/ExceptionHandlers.c:
 *      SVC_Handler / PendSV_Handler / SysTick_Handler — exact
 *      spellings, overridable (the port's renames land on them)
 *    - MSYSTICK_program.c is EXCLUDED from this build; its API
 *      survives as src/MSYSTICK_shim.c
 *
 *  Created on: Oct 1, 2026
 *      Author: dell
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <MCAL/RCC/MRCC_interface.h>   /* the published clock model */

/* ===================== 1. Basic kernel ===================== */

#define configUSE_PREEMPTION                    1
/* Irrelevant at Stage 1 (one task, no blocking) but already correct
   for Stage 2 — the config never changes when decomposition begins. */

#define configCPU_CLOCK_HZ                      ( MRCC_HCLK_KHZ * 1000UL )

/* 1 ms tick == the retired SysTick timebase, 1:1. THE load-bearing
   equality: the GetElapsed_ms shim is tick-count->milliseconds with
   no scaling, and every game deadline assumes it. */
#define configTICK_RATE_HZ                      ( 1000UL )
#if ( configTICK_RATE_HZ != 1000UL )
#error "FreeRTOSConfig: tick rate must stay 1000 Hz - the GetElapsed_ms shim (xTaskGetTickCount) is 1:1 only at 1 kHz"
#endif

/* V11 tick width. (On a V10.5.x LTS kernel this would instead be
   configUSE_16_BIT_TICKS 0.) */
#define configTICK_TYPE_WIDTH_IN_BITS           configTICK_TYPE_WIDTH_IN_BITS_32

/* Idle(0) + 4 application levels — the Stage-2 ladder (Input > App >
   Comm > Persistence) fits with headroom. */
#define configMAX_PRIORITIES                    5

/* Idle task stack, WORDS (= 512 B). Idle is starved at Stage 1 (the
   hub task never blocks) and does nothing here — no tickless, no
   hooks, no cleanup. */
#define configMINIMAL_STACK_SIZE                ( ( unsigned short ) 128 )

#define configMAX_TASK_NAME_LEN                 ( 8 )

/* ===================== 2. Memory — heap_4 =====================
 *  Budget arithmetic (64 KB SRAM, measured on the Gate D build):
 *  bss 2.2 KB + this heap 16 KB + hub stack 2 KB + idle 0.5 KB +
 *  TCB overhead ~= 21-22 KB committed, ~42 KB free. Refine the heap
 *  DOWN from Stage-2 high-water data, never guess smaller now.
 * ===================== 2. Memory ===================== */
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configSUPPORT_STATIC_ALLOCATION         0
#define configTOTAL_HEAP_SIZE                   ( 16UL * 1024UL )

/* ===================== 3. Interrupt priorities ==================
 *  Raw register values (logical << 4, the F401's 4 implemented bits):
 *    kernel (SysTick/PendSV) = 15 << 4 = 240 — lowest; the SAME value
 *      MSYSTICK Rev 2 used to write — ownership transfers kernel-ward
 *      with the same byte
 *    syscall mask (BASEPRI)  =  4 << 4 =  64 — everything at logical
 *      priority >= 4 is masked in kernel critical sections and MAY
 *      call FromISR APIs; everything above (EXTI=1/DMA=2/USART=3) is
 *      never masked and must NEVER call them — true by construction
 *      today (no ISR in this tree touches the kernel)
 * ===================== 3. Interrupt priorities ================== */
#define configPRIO_BITS                         4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 4

#define configKERNEL_INTERRUPT_PRIORITY         ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )

/* ===================== 4. Hooks and diagnostics ==================
 *  The loud-failure Constitution, kernel edition. Bodies live in the
 *  Stage-1 main.c: PA1 SOLID = runtime death (assert / malloc-failed
 *  / stack-overflow / hard fault all share the one signature).
 * ===================== 4. Hooks and diagnostics ================== */
extern void vAssertCalled( const char * const pcFile, const int iLine );
#define configASSERT( x )   do { if( ( x ) == 0 ) { vAssertCalled( __FILE__, __LINE__ ); } } while( 0 )

#define configUSE_MALLOC_FAILED_HOOK            1
#define configCHECK_FOR_STACK_OVERFLOW          2

/* NOTE (honest scope, Stage 1): method-2 checking runs on context
   switches — a single never-blocking task switches exactly once (at
   scheduler start). The high-water instrument lands at Stage 2 — the
   hub loop is verbatim at Stage 1, so there is no insertion point
   without modifying app code; the 2 KB stack is deliberately generous
   meanwhile. */

/* ===================== 5. Features OFF =====================
 *  Nothing exists to consume them; each flips on when its consumer
 *  lands. Smallest auditable surface.
 * ===================== 5. Features OFF ===================== */
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configUSE_TICKLESS_IDLE                 0
#define configUSE_TIMERS                        0
#define configUSE_MUTEXES                       0
#define configUSE_RECURSIVE_MUTEXES             0
#define configUSE_COUNTING_SEMAPHORES           0

/* Stage-2 mechanisms, enabled now (in tasks.c, near-free):
   notifications = the TIM5-ISR -> input-task wakeup path; vTaskDelay
   = the busy-wait conversion list (short: nothing in-loop at Stage 1). */
#define configUSE_TASK_NOTIFICATIONS            1

/* CLZ-based task selection (ARMv7-M instruction) */
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1

/* ===================== 6. API surface actually used ===================== */
#define INCLUDE_xTaskGetTickCount               1    /* THE SHIM (load-bearing) */
#define INCLUDE_uxTaskGetStackHighWaterMark     1    /* Stage 2 instrument     */
#define INCLUDE_vTaskDelay                      1    /* Stage 2 conversion      */
#define INCLUDE_vTaskDelayUntil                 1    /* Stage 2 periodic beats  */
#define INCLUDE_vTaskDelete                     0    /* tasks are permanent     */
#define INCLUDE_vTaskSuspend                    0
#define INCLUDE_vTaskPrioritySet                0
#define INCLUDE_uxTaskPriorityGet               0
#define INCLUDE_eTaskGetState                   0


#define INCLUDE_xTaskGetTickCountFromISR       1    /* R4D-5: the shim's ISR twin */

/* ===================== 7. Kernel-exception wiring =====================
 *  Renames the port's handlers onto the vector-table symbols; strong
 *  beats the weak defaults in ExceptionHandlers.c. If
 *  MSYSTICK_program.c is NOT excluded, its strong SysTick_Handler
 *  collides — a deliberate, loud link error (the forgetting-detector).
 * ===================== 7. Kernel-exception wiring ===================== */
#define vPortSVCHandler                         SVC_Handler
#define xPortPendSVHandler                      PendSV_Handler
#define xPortSysTickHandler                     SysTick_Handler

#endif /* FREERTOS_CONFIG_H */
