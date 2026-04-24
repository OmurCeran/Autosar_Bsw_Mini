/**
 * @file    Mini_EcuM.c
 * @brief   Mini EcuM Implementation
 */

#include "Mini_EcuM.h"
#include "Mini_Timestamp.h"
#include "Mini_SchM.h"
#include "Mini_Rte.h"
#include "Mini_Com.h"
#include "Mini_Dem.h"
#include "Mini_Nvm.h"
#include "Mini_FaultInj.h"
#include "App_Swc.h"

/* Current state — protected read/write via exclusive area */
static volatile EcuM_StateType ecum_currentState = ECUM_STATE_OFF;

/* State name table for logging */
static const char* const ecum_stateNames[] = {
    [ECUM_STATE_OFF]      = "OFF",
    [ECUM_STATE_STARTUP]  = "STARTUP",
    [ECUM_STATE_RUN]      = "RUN",
    [ECUM_STATE_SHUTDOWN] = "SHUTDOWN",
    [ECUM_STATE_ERROR]    = "ERROR"
};

/* ============================================================
 * INTERNAL HELPERS
 * ============================================================ */
static void EcuM_ChangeState(EcuM_StateType newState)
{
    EcuM_StateType oldState = ecum_currentState;
    ecum_currentState = newState;

    Log_Write(LOG_TAG_ECUM, "State transition: %s -> %s",
              ecum_stateNames[oldState], ecum_stateNames[newState]);
}

/* ============================================================
 * PUBLIC API
 * ============================================================ */

void EcuM_Init(void)
{
    ecum_currentState = ECUM_STATE_STARTUP;
}

void EcuM_StartupSequence(void)
{
    Log_Init();  /* Timestamp zero point */

    Log_Banner("ECU STARTUP SEQUENCE");
    Log_Write(LOG_TAG_ECUM, "Power-on reset, entering STARTUP state");

    /* --- Phase 1: EcuM itself --- */
    EcuM_Init();
    Log_Write(LOG_TAG_ECUM, "EcuM_Init complete");

    /* --- Phase 2: Core BSW (order matters!) --- */
    Log_Write(LOG_TAG_ECUM, "Initializing BSW stack...");

    SchM_Init();
    Log_Write(LOG_TAG_SCHM, "SchM_Init complete - scheduler ready");

    /* NvM must init before DEM (DEM reads stored DTCs from NvM) */
    NvM_Init();
    Log_Write(LOG_TAG_NVM, "NvM_Init complete - flash simulation ready");

    NvM_ReadAll();
    Log_Write(LOG_TAG_NVM, "NvM_ReadAll complete - %lu blocks restored",
              NvM_GetRestoredBlockCount());

    Rte_Init();
    Log_Write(LOG_TAG_RTE, "Rte_Init complete - mode=IMPLICIT");

    Com_Init();
    Log_Write(LOG_TAG_COM, "Com_Init complete - 4 signals, 2 PDUs");

    Dem_Init();
    Log_Write(LOG_TAG_DEM, "Dem_Init complete - 4 events registered");

    /* --- Phase 3: Application --- */
    App_Swc_Init();
    Log_Write(LOG_TAG_INIT, "Application SWC initialized");

    FaultInj_Init();
    Log_Write(LOG_TAG_INIT, "Fault injection system initialized");

    Log_Write(LOG_TAG_ECUM, "BSW stack fully initialized");

}

void EcuM_OnSchedulerStart(void)
{
    EcuM_ChangeState(ECUM_STATE_RUN);
    Log_Write(LOG_TAG_ECUM, "All tasks running, ECU in operational state");
}

EcuM_StateType EcuM_GetState(void)
{
    return ecum_currentState;
}

const char* EcuM_GetStateName(EcuM_StateType state)
{
    if (state >= (sizeof(ecum_stateNames) / sizeof(ecum_stateNames[0])))
    {
        return "UNKNOWN";
    }
    return ecum_stateNames[state];
}