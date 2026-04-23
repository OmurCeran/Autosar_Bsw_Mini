/**
 * @file    Mini_FaultInj.h
 * @brief   Fault Injection Manager — deliberately corrupts sensor data
 *
 * Used for the DEM lifecycle demo. Overrides the value returned by
 * App_Swc sensor reads when a fault is "injected".
 *
 * In a real ECU this doesn't exist — faults come from real hardware issues.
 * In our demo, we need a controlled way to trigger a fault.
 */

#ifndef MINI_FAULTINJ_H
#define MINI_FAULTINJ_H

#include "Std_Types.h"

/* Fault types that can be injected */
typedef enum {
    FAULT_INJ_NONE = 0,
    FAULT_INJ_TORQUE_OUT_OF_RANGE,   /* Torque > 100 Nm (plausibility fail) */
    FAULT_INJ_SPEED_FROZEN,          /* Speed stuck at one value */
    FAULT_INJ_COMMUNICATION_LOSS     /* Simulated CAN timeout */
} FaultInj_TypeType;

void FaultInj_Init(void);

/**
 * @brief Enable a fault injection
 * Once injected, sensor reads return corrupted values until FaultInj_Clear().
 */
void FaultInj_Inject(FaultInj_TypeType fault);

/**
 * @brief Clear all fault injections (recovery)
 */
void FaultInj_Clear(void);

/**
 * @brief Query current injected fault (SWC uses this)
 */
FaultInj_TypeType FaultInj_GetActive(void);

/**
 * @brief Get fault-corrupted sensor values (called by SWC when fault active)
 */
float32 FaultInj_GetCorruptedTorque(float32 originalValue);
float32 FaultInj_GetCorruptedSpeed(float32 originalValue);

#endif /* MINI_FAULTINJ_H */