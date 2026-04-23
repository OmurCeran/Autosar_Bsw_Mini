/**
 * @file    Mini_EcuM.h
 * @brief   Mini ECU State Manager — Lifecycle orchestration
 *
 * Maps to real AUTOSAR: EcuM (ECU State Manager)
 *
 * In real AUTOSAR, EcuM orchestrates:
 * - STARTUP: MCU init → BSW init → application init → RUN
 * - RUN: normal operation (application tasks active)
 * - SHUTDOWN: graceful cleanup → NvM write → sleep/reset
 * - SLEEP/WAKEUP: low-power modes (we skip this)
 *
 * Our simplified state machine covers STARTUP and RUN only.
 */

#ifndef MINI_ECUM_H
#define MINI_ECUM_H

#include "Std_Types.h"

/* --- ECU States --- */
typedef enum {
    ECUM_STATE_OFF = 0,
    ECUM_STATE_STARTUP,
    ECUM_STATE_RUN,
    ECUM_STATE_SHUTDOWN,
    ECUM_STATE_ERROR
} EcuM_StateType;

/* --- Public API --- */

/**
 * @brief Initialize EcuM itself (called first in BSW_Init)
 */
void EcuM_Init(void);

/**
 * @brief Main startup sequence — orchestrates BSW initialization.
 *
 * Called from main() before scheduler starts.
 * Performs:
 *   1. BSW module init in correct order
 *   2. NvM data restoration (reads stored DTCs from flash)
 *   3. Application init
 *   4. Transition to RUN state
 */
void EcuM_StartupSequence(void);

/**
 * @brief Signal that the OS scheduler has started.
 * Called from the first task that runs.
 */
void EcuM_OnSchedulerStart(void);

/**
 * @brief Get current ECU state
 */
EcuM_StateType EcuM_GetState(void);

/**
 * @brief Get state name as string (for logging)
 */
const char* EcuM_GetStateName(EcuM_StateType state);

#endif /* MINI_ECUM_H */