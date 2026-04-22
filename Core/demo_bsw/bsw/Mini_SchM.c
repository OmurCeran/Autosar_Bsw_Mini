/**
 * @file    Mini_SchM.c
 * @brief   Mini Schedule Manager Implementation
 */

#include "Mini_SchM.h"
#include "Mini_Com.h"
#include "Mini_Dem.h"
#include "FreeRTOS.h"
#include "task.h"

/* In real AUTOSAR: SchM uses OS API (SuspendAllInterrupts / ResumeAllInterrupts)
 * Here we use FreeRTOS critical section macros.
 * TODO: Replace with actual FreeRTOS includes when integrating */

/* Placeholder — replace with FreeRTOS taskENTER_CRITICAL / taskEXIT_CRITICAL */
static volatile uint32 schm_nestingCounter = 0U;

void SchM_Init(void)
{
    schm_nestingCounter = 0U;
}

/**
 * @brief Enter exclusive area — disable interrupts (nesting-safe)
 *
 * In real AUTOSAR: SchM_Enter_<Module>_<EA>()
 * Maps to FreeRTOS taskENTER_CRITICAL() which:
 *   - Saves current BASEPRI
 *   - Raises BASEPRI to configMAX_SYSCALL_INTERRUPT_PRIORITY
 *   - Manages nesting internally (no manual counter needed)
 *
 * CRITICAL: Keep the code between Enter and Exit as SHORT as possible.
 * In EPS: max exclusive area duration ~ 5–10 microseconds.
 * Long exclusive areas → 1ms task misses deadline → WdgM reaction.
 */
void SchM_Enter_ExclusiveArea(void)
{
    taskENTER_CRITICAL();
}

/**
 * @brief Exit exclusive area — re-enable interrupts
 */
void SchM_Exit_ExclusiveArea(void)
{
    taskEXIT_CRITICAL();
}

/**
 * @brief 1ms MainFunction dispatch
 *
 * In real AUTOSAR: OS calls this from a 1ms task.
 * High-priority BSW processing — currently no BSW modules at 1ms.
 * Application 1ms runnable is called directly from FreeRTOS task.
 */
void SchM_MainFunction_1ms(void)
{
    /* Reserved for future 1ms BSW modules */
}

/**
 * @brief 10ms MainFunction dispatch
 *
 * In real AUTOSAR: SchM triggers Com_MainFunctionTx() at 10ms period.
 * COM packs all updated signals into I-PDUs and triggers transmission.
 */
void SchM_MainFunction_10ms(void)
{
    Com_MainFunctionTx();
}

/**
 * @brief 100ms MainFunction dispatch
 *
 * In real AUTOSAR: SchM triggers Dem_MainFunction(), NvM_MainFunction().
 * DEM processes debounce counters and writes events to NvM.
 * NvM processes its async queue and writes to flash via Fee → Fls.
 */
void SchM_MainFunction_100ms(void)
{
    Dem_MainFunction();
    /* NvM_MainFunction() would be called here in a full stack */
}