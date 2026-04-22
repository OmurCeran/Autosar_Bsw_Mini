/**
 * @file    Mini_SchM.h
 * @brief   Mini Schedule Manager — Exclusive Areas & MainFunction Dispatch
 *
 * Maps to real AUTOSAR: SchM (BSW Scheduler)
 *
 * In real AUTOSAR, SchM provides:
 * - SchM_Enter/Exit exclusive areas (interrupt disable/enable for critical sections)
 * - Triggering BSW MainFunctions from OS tasks at configured periods
 *
 * Here we implement both concepts using FreeRTOS critical sections.
 */

#ifndef MINI_SCHM_H
#define MINI_SCHM_H

#include "Std_Types.h"

/* --- Exclusive Area API --- */
/* In real AUTOSAR: SchM_Enter_<Module>_<EA>() / SchM_Exit_<Module>_<EA>()
 * These disable/enable interrupts to protect shared data.
 * Critical: keep exclusive area duration MINIMAL — long disable = timing violation */
void SchM_Enter_ExclusiveArea(void);
void SchM_Exit_ExclusiveArea(void);

/* --- MainFunction Dispatch --- */
/* In real AUTOSAR: SchM triggers Com_MainFunctionTx(), Dem_MainFunction(),
 * NvM_MainFunction() etc. from OS tasks at configured periods.
 * Here we call them from FreeRTOS tasks. */
void SchM_Init(void);
void SchM_MainFunction_1ms(void);    /* High-priority BSW processing */
void SchM_MainFunction_10ms(void);   /* COM Tx, signal routing */
void SchM_MainFunction_100ms(void);  /* DEM processing, NvM writes */

#endif /* MINI_SCHM_H */