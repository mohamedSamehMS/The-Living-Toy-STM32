/*
 * MSYSTICK_shim.c — the SysTick API over the FreeRTOS kernel tick
 *                    (Stage 1 of the RTOS migration — the copy tree)
 *
 *  WHY THIS FILE EXISTS: at kernel time MSYSTICK_program.c leaves the
 *  build — the FreeRTOS port owns SysTick from vPortStartScheduler(),
 *  and its strong SysTick_Handler collides with the driver's. Every
 *  CONSUMER of the SysTick API, however, ports UNCHANGED: this file
 *  re-implements exactly the consumed surface (per the whole-tree
 *  audit) over the kernel.
 *
 *    MSYSTICK_GetElapsed_ms()  -> xTaskGetTickCount()
 *        1 kHz tick, u32, monotonic, wrap-safe by subtraction — the
 *        IDENTICAL semantics of the driver's Global_u32MilliTicks.
 *        The tick==ms equality is pinned by FreeRTOSConfig.h's #error.
 *
 *    MSYSTICK_SetBusyWait_ms() -> calibrated raw delay loop
 *        NOT the kernel tick: the one live consumer is HTFT's init
 *        sequence, which runs PRE-SCHEDULER — xTaskGetTickCount()
 *        reads a frozen 0 there, and a tick-based wait would hang
 *        boot inside the panel's settle delays. Minimum-settle
 *        semantics make ~±30% accuracy irrelevant (150 ms meaning
 *        120 or 190 ms is fine for a panel reset).
 *
 *  DELIBERATELY ABSENT (zero live consumers in this build, per the
 *  whole-tree audit): Init, InterruptEnable/Disable, SetBusyWait_us,
 *  SetTickCallback, HasCounted. Any reference to one becomes a LOUD
 *  link error — the no-silent-failure rule working in our favor.
 *
 *  DOCUMENTED DEGRADATION (accepted): MUSART's internal flag-timeout
 *  helper consumes GetElapsed_ms; before the scheduler starts it
 *  reads a frozen 0, so a DEAD console UART at boot hangs the boot
 *  wait instead of timing out after 1 s. Wiring-fault-only case.
 *
 *  Created on: Oct 1, 2026
 *      Author: dell
 */
#include <MCAL/SYSTICK/MSYSTICK_interface.h>
#include <OS/FreeRTOS.h>
#include <OS/task.h>

/* ---- THE SHIM: the 1 ms timebase is the kernel tick now ---------- */
u32 MSYSTICK_GetElapsed_ms(void)
{
    u32 Local_u32IPSR;
    asm volatile("mrs %0, ipsr" : "=r"(Local_u32IPSR));
    if (Local_u32IPSR != 0u)
    {
        return xTaskGetTickCountFromISR();   /* the ISR-legal read */
    }
    return xTaskGetTickCount();
}

/* ---- The pre-scheduler delay: a calibrated raw loop --------------- */
MSYSTICK_ReturnType_t MSYSTICK_SetBusyWait_ms(u32 Copy_u32Ms)
{
    /* SHIM_LOOPS_PER_MS: calibrated for -Og at 16 MHz HSI (a volatile
       counter iterates in ~4-6 cycles). Deliberately errs LONG — the
       safe direction for minimum-settle delays. If SYSTEM_CLOCK ever
       flips to PLL_HSE, scale this from MRCC_HCLK_KHZ right here (the
       published model — no silent unit break on the clock flip).   */
#define SHIM_LOOPS_PER_MS   4000u

    while (Copy_u32Ms != 0u)
    {
        volatile u32 Local_u32Spin;
        for (Local_u32Spin = 0u; Local_u32Spin < SHIM_LOOPS_PER_MS; Local_u32Spin++)
        {
            /* empty body — the volatile counter IS the delay */
        }
        Copy_u32Ms--;
    }
    return MSYSTICK_OK;
}
