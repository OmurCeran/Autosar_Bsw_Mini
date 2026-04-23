/**
 * @file    Mini_Timestamp.h
 * @brief   Unified logging with timestamps and module tags
 *
 * All BSW and application logs go through this module to provide
 * consistent formatting: [t=XXXXms][MODULE] message
 *
 * Uses the existing DMA_Printf infrastructure — thread-safe and non-blocking.
 */

#ifndef MINI_TIMESTAMP_H
#define MINI_TIMESTAMP_H

#include "Std_Types.h"

/* Module tags — fixed width for aligned output */
#define LOG_TAG_ECUM   "EcuM "
#define LOG_TAG_SCHM   "SchM "
#define LOG_TAG_RTE    "RTE  "
#define LOG_TAG_COM    "COM  "
#define LOG_TAG_DEM    "DEM  "
#define LOG_TAG_NVM    "NvM  "
#define LOG_TAG_DCM    "DCM  "
#define LOG_TAG_SWC    "SWC  "
#define LOG_TAG_UDS    "UDS  "
#define LOG_TAG_FAULT  "FAULT"
#define LOG_TAG_INIT   "INIT "
#define LOG_TAG_STATS  "STATS"

/* Initialize the logging subsystem (call after RTOS starts) */
void Log_Init(void);

/* Get current timestamp in milliseconds since boot */
uint32 Log_GetTimestampMs(void);

/* Main logging function — variadic, like printf
 * Output format: [t=XXXXms][TAG  ] message
 *
 * Example: Log_Write(LOG_TAG_RTE, "Rte_Init complete");
 *          → [t=0028ms][RTE  ] Rte_Init complete
 */
void Log_Write(const char *moduleTag, const char *format, ...);

/* Log a raw line without prefix (for banners, separators) */
void Log_Raw(const char *format, ...);

/* Log a banner/separator line — used between phases */
void Log_Banner(const char *title);
void Log_Separator(void);

#endif /* MINI_TIMESTAMP_H */