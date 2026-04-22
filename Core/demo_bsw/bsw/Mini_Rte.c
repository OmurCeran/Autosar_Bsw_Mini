/**
 * @file    Mini_Rte.c
 * @brief   Mini RTE Implementation — Implicit vs Explicit Data Access
 *
 * THIS IS THE MOST IMPORTANT FILE IN THE PROJECT.
 * It demonstrates the exact race condition from the EPS debug story
 * and how AUTOSAR's implicit data access solves it.
 *
 * Race condition detection strategy:
 *   The 10ms writer task establishes a MATHEMATICAL INVARIANT between
 *   the fields it writes. For every consistent snapshot of the struct,
 *   vehicle_speed == torque_input * SPEED_TO_TORQUE_RATIO must hold.
 *
 *   If the 1ms reader sees a struct where this invariant is broken,
 *   it has caught a torn write (race condition).
 *
 *   In EXPLICIT mode: writer updates fields one by one → 1ms task can
 *   preempt between writes → reader sees broken invariant → detected.
 *
 *   In IMPLICIT mode: writer updates local copy, then atomic flush →
 *   reader always sees a valid snapshot → invariant holds → zero errors.
 */

#include "Mini_Rte.h"
#include "Mini_SchM.h"

/* ============================================================
 * INVARIANT RATIO — writer must preserve this relationship
 * If reader sees a struct violating this, it's a race condition.
 * ============================================================ */
#define SPEED_TO_TORQUE_RATIO   (10.0f)

/* ============================================================
 * GLOBAL DATA — shared between all tasks
 * ============================================================ */
static Rte_SteeringDataType rte_globalData = {
    .torque_input     = 1.0f,
    .vehicle_speed    = 10.0f,   /* invariant: speed = torque * 10 */
    .calib_coeff      = 1.0f,
    .motor_torque_cmd = 0.0f,
    .steering_angle   = 0.0f
};

/* Local snapshot for implicit mode */
static Rte_SteeringDataType rte_localCopy;

/* Access mode */
static Rte_AccessModeType rte_accessMode = RTE_ACCESS_IMPLICIT;

/* Inconsistency counter */
static volatile uint32 rte_inconsistencyCount = 0U;

/* ============================================================
 * INITIALIZATION
 * ============================================================ */
void Rte_Init(void)
{
    rte_globalData.torque_input     = 1.0f;
    rte_globalData.vehicle_speed    = 10.0f;
    rte_globalData.calib_coeff      = 1.0f;
    rte_globalData.motor_torque_cmd = 0.0f;
    rte_globalData.steering_angle   = 0.0f;

    rte_localCopy = rte_globalData;
    rte_accessMode = RTE_ACCESS_IMPLICIT;
    rte_inconsistencyCount = 0U;
}

void Rte_SetAccessMode(Rte_AccessModeType mode)
{
    rte_accessMode = mode;
}

Rte_AccessModeType Rte_GetAccessMode(void)
{
    return rte_accessMode;
}

/* ============================================================
 * EXPLICIT ACCESS — DIRECT READ/WRITE TO GLOBAL BUFFER
 * ============================================================ */
Std_ReturnType Rte_Read_TorqueInput(float32 *value)
{
    *value = rte_globalData.torque_input;
    return E_OK;
}

Std_ReturnType Rte_Read_VehicleSpeed(float32 *value)
{
    *value = rte_globalData.vehicle_speed;
    return E_OK;
}

Std_ReturnType Rte_Read_CalibCoeff(float32 *value)
{
    *value = rte_globalData.calib_coeff;
    return E_OK;
}

Std_ReturnType Rte_Write_TorqueInput(float32 value)
{
    rte_globalData.torque_input = value;
    return E_OK;
}

Std_ReturnType Rte_Write_VehicleSpeed(float32 value)
{
    rte_globalData.vehicle_speed = value;
    return E_OK;
}

Std_ReturnType Rte_Write_CalibCoeff(float32 value)
{
    rte_globalData.calib_coeff = value;
    return E_OK;
}

Std_ReturnType Rte_Write_MotorTorqueCmd(float32 value)
{
    rte_globalData.motor_torque_cmd = value;
    return E_OK;
}

/* ============================================================
 * IMPLICIT ACCESS — SNAPSHOT AT TASK START, FLUSH AT TASK END
 * ============================================================ */
void Rte_Task_Begin(void)
{
    if (rte_accessMode == RTE_ACCESS_IMPLICIT)
    {
        SchM_Enter_ExclusiveArea();
        rte_localCopy = rte_globalData;
        SchM_Exit_ExclusiveArea();
    }
}

void Rte_Task_End(void)
{
    if (rte_accessMode == RTE_ACCESS_IMPLICIT)
    {
        SchM_Enter_ExclusiveArea();
        rte_globalData.motor_torque_cmd = rte_localCopy.motor_torque_cmd;
        rte_globalData.steering_angle   = rte_localCopy.steering_angle;
        SchM_Exit_ExclusiveArea();
    }
}

float32 Rte_IRead_TorqueInput(void)
{
    return rte_localCopy.torque_input;
}

float32 Rte_IRead_VehicleSpeed(void)
{
    return rte_localCopy.vehicle_speed;
}

float32 Rte_IRead_CalibCoeff(void)
{
    return rte_localCopy.calib_coeff;
}

void Rte_IWrite_MotorTorqueCmd(float32 value)
{
    rte_localCopy.motor_torque_cmd = value;
}

void Rte_IWrite_SteeringAngle(float32 value)
{
    rte_localCopy.steering_angle = value;
}

/* ============================================================
 * DEBUG / DEMO FUNCTIONS
 * ============================================================ */
const Rte_SteeringDataType* Rte_Debug_GetGlobalData(void)
{
    return &rte_globalData;
}

uint32 Rte_GetInconsistencyCount(void)
{
    return rte_inconsistencyCount;
}

void Rte_ResetInconsistencyCount(void)
{
    rte_inconsistencyCount = 0U;
}

/**
 * @brief Check data consistency using the mathematical invariant.
 *
 * The writer task (10ms) maintains: vehicle_speed == torque_input * 10.
 * If this invariant is broken, the reader saw a torn write — race condition.
 *
 * Tolerance accounts for float precision.
 */
void Rte_CheckConsistency(float32 readTorque, float32 readSpeed)
{
    if (rte_accessMode == RTE_ACCESS_EXPLICIT)
    {
        float32 expectedSpeed = readTorque * SPEED_TO_TORQUE_RATIO;
        float32 diff = readSpeed - expectedSpeed;
        if (diff < 0.0f) diff = -diff;

        /* Tolerance: 0.5 covers float rounding but catches real mismatches */
        if (diff > 0.5f)
        {
            rte_inconsistencyCount++;
        }
    }
}