/**
 * @file    App_Swc.c
 * @brief   Application SWC — EPS Torque Calculation & Race Condition Demo
 *
 * Race condition setup:
 *   10ms task is the WRITER. It maintains the invariant:
 *       vehicle_speed = torque_input * 10
 *   The writer updates torque_input first, inserts a preemption window,
 *   then updates vehicle_speed. During the window, the invariant is
 *   temporarily broken in the global buffer.
 *
 *   1ms task is the READER. If it preempts during the window, it sees:
 *       new torque_input, old vehicle_speed
 *   → invariant broken → race condition detected.
 *
 *   Implicit mode protects the reader via task-boundary snapshot.
 */

#include "App_Swc.h"
#include "Mini_Rte.h"
#include "Mini_Dem.h"
#include "Mini_Com.h"
#include "Mini_FaultInj.h"

/* Race condition invariant — writer must preserve speed = torque * 10 */
#define SPEED_TO_TORQUE_RATIO   (10.0f)

/* Healing countdown — after fault clears, report PREPASSED N times
 * to let DEM heal, then go silent */
#define DIAG_HEAL_COUNT  (6U)   /* 6 x 100ms = 600ms, enough for debounce */
 
static uint8 diag_healCounter_torque = 0U;
static uint8 diag_healCounter_speed  = 0U;

/* Reader/writer state */
static uint32 sim_cycleCounter = 0U;

/* Rte_CheckConsistency is declared in Mini_Rte.c but not in the header,
 * since it's a demo-only function. Forward declare here. */
extern void Rte_CheckConsistency(float32 readTorque, float32 readSpeed);

void App_Swc_Init(void)
{
    sim_cycleCounter = 0U;
}

/**
 * @brief 10ms Runnable — WRITER task (LOW priority)
 *
 * Updates the shared struct while maintaining the invariant
 * vehicle_speed = torque_input * 10.
 *
 * In EXPLICIT mode: updates fields one at a time with a preemption
 * window in between → 1ms reader can catch a torn write.
 */
void App_Runnable_SensorUpdate_10ms(void)
{
    Rte_AccessModeType mode = Rte_GetAccessMode();

    /* Generate a new valid (torque, speed) pair each cycle */
    sim_cycleCounter++;
    float32 newTorque = 1.0f + (float32)(sim_cycleCounter % 50);
    float32 newSpeed  = newTorque * SPEED_TO_TORQUE_RATIO;

    if (mode == RTE_ACCESS_EXPLICIT)
    {
        /*
         * EXPLICIT — DANGEROUS
         * Write torque first...
         */
        Rte_Write_TorqueInput(newTorque);

        /*
         * === PREEMPTION WINDOW ===
         * The invariant is BROKEN here: new torque, old speed.
         * The 1ms task (higher priority) preempts during this window
         * and reads the broken state → race condition detected.
         *
         * The delay is tuned to exceed one 1ms tick so preemption
         * is nearly guaranteed at least once every few 10ms cycles.
         */
        volatile uint32 delay;
        for (delay = 0; delay < 200000U; delay++)
        {
            __asm volatile ("nop");
        }

        /* ...then write vehicle_speed to restore invariant */
        Rte_Write_VehicleSpeed(newSpeed);
    }
    else /* RTE_ACCESS_IMPLICIT */
    {
        /*
         * IMPLICIT — SAFE
         * Direct global writes here are still done by the writer,
         * but the reader is protected by the task-boundary snapshot
         * taken in Rte_Task_Begin(). Reader never sees a partial update.
         */
        Rte_Write_TorqueInput(newTorque);

        /* Same preemption window for fair comparison */
        volatile uint32 delay;
        for (delay = 0; delay < 200000U; delay++)
        {
            __asm volatile ("nop");
        }

        Rte_Write_VehicleSpeed(newSpeed);
    }
}

/**
 * @brief 1ms Runnable — READER task (HIGH priority)
 *
 * Reads shared data and checks the invariant.
 * In EXPLICIT mode: reads directly from global → may catch torn writes.
 * In IMPLICIT mode: reads from local snapshot → always consistent.
 */
void App_Runnable_TorqueCalc_1ms(void)
{
    float32 torque, speed;
    Rte_AccessModeType mode = Rte_GetAccessMode();

    if (mode == RTE_ACCESS_EXPLICIT)
    {
        /* Direct global reads — may be torn */
        Rte_Read_TorqueInput(&torque);
        Rte_Read_VehicleSpeed(&speed);

        /* Check invariant: speed should equal torque * 10 */
        Rte_CheckConsistency(torque, speed);
    }
    else /* RTE_ACCESS_IMPLICIT */
    {
        /* Snapshot reads — always consistent */
        torque = Rte_IRead_TorqueInput();
        speed  = Rte_IRead_VehicleSpeed();

        /* Still check invariant — should always hold in implicit mode */
        Rte_CheckConsistency(torque, speed);
    }

    /* Simple torque calculation (output ignored for demo) */
    (void)torque;
    (void)speed;
}

/**
 * @brief 100ms Runnable — Diagnostic Monitor (lowest priority)
 *
 * Light-weight for the demo — real version would call Dem_SetEventStatus.
 */
void App_Runnable_DiagMonitor_100ms(void)
{
    /* Update freeze frame source — always, so DEM captures current state */
    const Rte_SteeringDataType *data = Rte_Debug_GetGlobalData();
 
    float32 torque = FaultInj_GetCorruptedTorque(data->torque_input);
    float32 speed  = FaultInj_GetCorruptedSpeed(data->vehicle_speed);
 
    Dem_SetFreezeFrameData(
        (uint16)(torque * 10.0f),
        (uint16)(speed * 10.0f),
        0U,
        (uint16)(torque * 20.0f)
    );
 
    /* ============ TORQUE SENSOR MONITORING ============ */
    FaultInj_TypeType activeFault = FaultInj_GetActive();
    boolean torqueFaultActive = (activeFault == FAULT_INJ_TORQUE_OUT_OF_RANGE);
 
    if (torqueFaultActive)
    {
        /* Fault is actively injected — report PREFAILED */
        Dem_SetEventStatus(DEM_EVENT_TORQUE_SENSOR_FAULT, DEM_EVENT_STATUS_PREFAILED);
        diag_healCounter_torque = DIAG_HEAL_COUNT;  /* arm healer for when fault clears */
    }
    else if (diag_healCounter_torque > 0U)
    {
        /* Fault recently cleared — heal the debounce counter a few times */
        Dem_SetEventStatus(DEM_EVENT_TORQUE_SENSOR_FAULT, DEM_EVENT_STATUS_PREPASSED);
        diag_healCounter_torque--;
    }
    /* else: no fault, no reporting — silent */
 
    /* ============ VEHICLE SPEED MONITORING ============ */
    boolean speedFaultActive = (activeFault == FAULT_INJ_SPEED_FROZEN);
 
    if (speedFaultActive)
    {
        Dem_SetEventStatus(DEM_EVENT_VEHICLE_SPEED_FAULT, DEM_EVENT_STATUS_PREFAILED);
        diag_healCounter_speed = DIAG_HEAL_COUNT;
    }
    else if (diag_healCounter_speed > 0U)
    {
        Dem_SetEventStatus(DEM_EVENT_VEHICLE_SPEED_FAULT, DEM_EVENT_STATUS_PREPASSED);
        diag_healCounter_speed--;
    }
    /* else: silent */
}
 

uint32 App_GetRaceConditionDetections(void)
{
    return Rte_GetInconsistencyCount();
}