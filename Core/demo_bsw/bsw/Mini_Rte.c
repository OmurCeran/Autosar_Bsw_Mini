/**
 * @file    Mini_Rte.c
 * @brief   Mini RTE Implementation — Implicit vs Explicit Data Access
 *
 * THIS IS THE MOST IMPORTANT FILE IN THE PROJECT.
 * It demonstrates the exact race condition from the EPS debug story
 * and how AUTOSAR's implicit data access solves it.
 */

#include "Mini_Rte.h"
#include "Mini_SchM.h"

/* ============================================================
 * GLOBAL DATA — shared between all tasks
 * In real AUTOSAR: RTE-internal buffers, auto-generated , defined interface
 * ============================================================ */
static Rte_SteeringDataType rte_globalData = {
    .torque_input     = 0.0f,
    .vehicle_speed    = 60.0f,   /* Default: 60 km/h to avoid div-by-zero */
    .calib_coeff      = 1.0f,
    .motor_torque_cmd = 0.0f,
    .steering_angle   = 0.0f
};

/* ============================================================
 * LOCAL COPY — used by implicit mode (per-task snapshot)
 * In real AUTOSAR: one copy per task that uses implicit access
 * For simplicity, we use a single local copy here.
 * ============================================================ */
static Rte_SteeringDataType rte_localCopy;

/* ============================================================
 * ACCESS MODE — switch between explicit (dangerous) and implicit (safe)
 * ============================================================ */
static Rte_AccessModeType rte_accessMode = RTE_ACCESS_IMPLICIT;

/* ============================================================
 * INCONSISTENCY DETECTION — counts how many times race condition occurs
 * Used for demo: run in explicit mode, count errors, switch to implicit,
 * show errors drop to zero.
 * ============================================================ */
static volatile uint32 rte_inconsistencyCount = 0U;

/* Previous values for detecting inconsistency */
static float32 rte_lastWrittenTorque = 0.0f;
static float32 rte_lastWrittenSpeed  = 60.0f;

/* ============================================================
 * INITIALIZATION
 * ============================================================ */
void Rte_Init(void)
{
    rte_globalData.torque_input     = 0.0f;
    rte_globalData.vehicle_speed    = 60.0f;
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
 *
 * DANGER: If 10ms task is writing torque_input and vehicle_speed
 * to the global buffer, and 1ms task preempts between the two
 * writes, 1ms task reads NEW torque_input but OLD vehicle_speed.
 * Result: inconsistent data → wrong torque calculation.
 *
 * This is EXACTLY what happened in the EPS debug story.
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
    rte_lastWrittenTorque = value;
    /* NO PROTECTION — 1ms task can preempt right here! */
    return E_OK;
}

Std_ReturnType Rte_Write_VehicleSpeed(float32 value)
{
    rte_globalData.vehicle_speed = value;
    rte_lastWrittenSpeed = value;
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
 *
 * SAFE: 1ms task always sees a CONSISTENT set of values.
 * Either ALL old or ALL new — never a mix.
 *
 * How it works in real AUTOSAR RTE (generated code):
 * 1. OS calls Rte_Task_Begin() before any runnable executes
 * 2. RTE copies global → local inside exclusive area (fast!)
 * 3. Runnables use Rte_IRead/IWrite on LOCAL copy
 * 4. OS calls Rte_Task_End() after all runnables complete
 * 5. RTE copies local → global inside exclusive area (fast!)
 *
 * The exclusive area duration is VERY short — only the memcpy.
 * NOT the entire task execution. This is why it doesn't cause
 * timing violations even in EPS (unlike disabling IRQ for the
 * whole task, which would be unacceptable).
 * ============================================================ */
void Rte_Task_Begin(void)
{
    if (rte_accessMode == RTE_ACCESS_IMPLICIT)
    {
        SchM_Enter_ExclusiveArea();
        rte_localCopy = rte_globalData;  /* Fast struct copy */
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
 * @brief Called by 1ms task to check data consistency
 *
 * In explicit mode: compares read values against last-written values.
 * If torque is new but speed is old (or vice versa) → inconsistency detected.
 *
 * This function exists purely for the demo — real AUTOSAR doesn't need
 * this because implicit access prevents the problem entirely.
 */
void Rte_CheckConsistency(float32 readTorque, float32 readSpeed)
{
    if (rte_accessMode == RTE_ACCESS_EXPLICIT)
    {
        /* Detect if we read a mix of old and new values */
        boolean torqueIsNew = (readTorque == rte_lastWrittenTorque);
        boolean speedIsNew  = (readSpeed == rte_lastWrittenSpeed);

        if (torqueIsNew != speedIsNew)
        {
            /* One is new, other is old → INCONSISTENCY (race condition!) */
            rte_inconsistencyCount++;
        }
    }
}