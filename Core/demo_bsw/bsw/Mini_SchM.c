/**
 * @file    Mini_SchM.c
 * @brief   Mini Schedule Manager Implementation
 */

#include "Mini_SchM.h"
#include "Mini_Com.h"
#include "Mini_Dem.h"

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
 * @brief Enter exclusive area — disable interrupts
 *
 * In real AUTOSAR: SchM_Enter_Com_COM_EXCLUSIVE_AREA_0()
 * Supports nesting: first call disables, subsequent calls increment counter.
 *
 * CRITICAL: Keep the code between Enter and Exit as SHORT as possible.
 * Long exclusive areas cause timing violations in safety-critical systems.
 * In EPS: max exclusive area duration ~ 5-10 microseconds.
 */
void SchM_Enter_ExclusiveArea(void)
{
    /* TODO: Replace with taskENTER_CRITICAL() from FreeRTOS */
    __asm volatile ("cpsid i" ::: "memory");
    schm_nestingCounter++;
}

/**
 * @brief Exit exclusive area — re-enable interrupts
 */
void SchM_Exit_ExclusiveArea(void)
{
    schm_nestingCounter--;
    if (schm_nestingCounter == 0U)
    {
        /* TODO: Replace with taskEXIT_CRITICAL() from FreeRTOS */
        __asm volatile ("cpsie i" ::: "memory");
    }
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