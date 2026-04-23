/**
 * @file    Mini_Timestamp.c
 * @brief   Logging implementation — timestamp + module tag + message
 *
 * Wraps DMA_Printf (defined in main.c) to provide consistent formatting.
 * Timestamps come from HAL_GetTick() which FreeRTOS feeds from TIM2.
 */

#include "Mini_Timestamp.h"
#include "main.h"
#include "cmsis_os.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* External DMA_Printf from main.c */
extern void DMA_Printf(const char *format, ...);

/* Boot timestamp — captured at Log_Init to provide relative time */
static uint32 log_bootTick = 0U;

/* Internal format buffer */
static char log_formatBuffer[256];

/* ============================================================
 * PUBLIC API
 * ============================================================ */

void Log_Init(void)
{
    log_bootTick = HAL_GetTick();
}

uint32 Log_GetTimestampMs(void)
{
    /* Time since Log_Init() — relative to boot */
    return (uint32)(HAL_GetTick() - log_bootTick);
}

void Log_Write(const char *moduleTag, const char *format, ...)
{
    va_list args;
    va_start(args, format);

    /* Format user message first */
    vsnprintf(log_formatBuffer, sizeof(log_formatBuffer), format, args);
    va_end(args);

    /* Emit: [t=XXXXms][TAG  ] message */
    DMA_Printf("[t=%04lums][%s] %s\r\n",
               Log_GetTimestampMs(), moduleTag, log_formatBuffer);
}

void Log_Raw(const char *format, ...)
{
    va_list args;
    va_start(args, format);

    vsnprintf(log_formatBuffer, sizeof(log_formatBuffer), format, args);
    va_end(args);

    DMA_Printf("%s\r\n", log_formatBuffer);
}

void Log_Banner(const char *title)
{
    DMA_Printf("\r\n");
    DMA_Printf("================================================================\r\n");
    DMA_Printf(" %s\r\n", title);
    DMA_Printf("================================================================\r\n");
}

void Log_Separator(void)
{
    DMA_Printf("----------------------------------------------------------------\r\n");
}