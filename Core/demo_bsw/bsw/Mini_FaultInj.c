/**
 * @file    Mini_FaultInj.c
 * @brief   Fault Injection implementation
 */

#include "Mini_FaultInj.h"
#include "Mini_Timestamp.h"

static FaultInj_TypeType faultInj_active = FAULT_INJ_NONE;

static const char* FaultInj_Name(FaultInj_TypeType fault)
{
    switch (fault)
    {
        case FAULT_INJ_NONE:                return "NONE";
        case FAULT_INJ_TORQUE_OUT_OF_RANGE: return "TORQUE_OUT_OF_RANGE";
        case FAULT_INJ_SPEED_FROZEN:        return "SPEED_FROZEN";
        case FAULT_INJ_COMMUNICATION_LOSS:  return "COMMUNICATION_LOSS";
        default:                            return "UNKNOWN";
    }
}

void FaultInj_Init(void)
{
    faultInj_active = FAULT_INJ_NONE;
}

void FaultInj_Inject(FaultInj_TypeType fault)
{
    faultInj_active = fault;
    Log_Write(LOG_TAG_FAULT, ">>> INJECTING FAULT: %s <<<",
              FaultInj_Name(fault));
}

void FaultInj_Clear(void)
{
    if (faultInj_active != FAULT_INJ_NONE)
    {
        Log_Write(LOG_TAG_FAULT, ">>> CLEARING FAULT: %s <<<",
                  FaultInj_Name(faultInj_active));
    }
    faultInj_active = FAULT_INJ_NONE;
}

FaultInj_TypeType FaultInj_GetActive(void)
{
    return faultInj_active;
}

float32 FaultInj_GetCorruptedTorque(float32 originalValue)
{
    if (faultInj_active == FAULT_INJ_TORQUE_OUT_OF_RANGE)
    {
        return 250.0f;  /* Out of valid range (0..100 Nm) */
    }
    return originalValue;
}

float32 FaultInj_GetCorruptedSpeed(float32 originalValue)
{
    if (faultInj_active == FAULT_INJ_SPEED_FROZEN)
    {
        return 0.0f;  /* Frozen value */
    }
    return originalValue;
}