/**
 * @file    freertos_Helper.c
 * @brief   FreeRTOS Helper Implementation
 */
#include "freertos_Helper.h"
/* ============================================================
 * Static Allocation Support — Idle & Timer Task Memory
 *
 * FreeRTOS needs buffers for the internal idle task and (if enabled)
 * the software timer task. With configSUPPORT_STATIC_ALLOCATION=1,
 * the kernel calls back into the application to get these buffers.
 * ============================================================ */

/* Idle Task — always runs when no other task is ready */
static StaticTask_t xIdleTaskTCBBuffer;
static StackType_t  xIdleStack[configMINIMAL_STACK_SIZE];

void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                   StackType_t **ppxIdleTaskStackBuffer,
                                   uint32_t *pulIdleTaskStackSize)
{
    *ppxIdleTaskTCBBuffer   = &xIdleTaskTCBBuffer;
    *ppxIdleTaskStackBuffer = &xIdleStack[0];
    *pulIdleTaskStackSize   = configMINIMAL_STACK_SIZE;
}

/* Timer Task — only needed if configUSE_TIMERS is enabled in FreeRTOSConfig.h
 * CMSIS-RTOS v2 typically enables this for osTimer_* APIs */
#if ( configUSE_TIMERS == 1 )
static StaticTask_t xTimerTaskTCBBuffer;
static StackType_t  xTimerStack[configTIMER_TASK_STACK_DEPTH];

void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
                                    StackType_t **ppxTimerTaskStackBuffer,
                                    uint32_t *pulTimerTaskStackSize)
{
    *ppxTimerTaskTCBBuffer   = &xTimerTaskTCBBuffer;
    *ppxTimerTaskStackBuffer = &xTimerStack[0];
    *pulTimerTaskStackSize   = configTIMER_TASK_STACK_DEPTH;
}
#endif