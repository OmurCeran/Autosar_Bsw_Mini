/**
 * @file    App_Swc.h
 * @brief   Application Software Component — EPS Torque Calculation
 *
 * Maps to real AUTOSAR: SWC with Runnables
 *
 * Simulates an EPS (Electric Power Steering) application:
 * - 10ms Runnable: reads sensors, updates steering data struct
 * - 1ms Runnable:  reads data, calculates motor torque command
 * - 100ms Runnable: monitors sensor health, reports to DEM
 *
 * The race condition demo:
 * - In EXPLICIT mode: 10ms task writes struct member-by-member,
 *   1ms task preempts mid-write → reads inconsistent data → wrong torque
 * - In IMPLICIT mode: RTE snapshots at task start → always consistent
 */

#ifndef APP_SWC_H
#define APP_SWC_H

#include "Std_Types.h"

/* --- Initialization --- */
void App_Swc_Init(void);

/* --- Runnables (called from FreeRTOS tasks) --- */

/**
 * @brief 10ms Runnable — Sensor Data Update (LOW priority)
 *
 * Simulates reading torque sensor, vehicle speed sensor.
 * Writes values to RTE (via Rte_Write or Rte_IWrite depending on mode).
 *
 * THIS is where the race condition originates:
 * If preempted between writing torque_input and vehicle_speed,
 * the 1ms task sees inconsistent data.
 */
void App_Runnable_SensorUpdate_10ms(void);

/**
 * @brief 1ms Runnable — Torque Calculation (HIGH priority)
 *
 * Reads sensor data from RTE, calculates motor torque command.
 * Formula: motor_torque = torque_input * calib_coeff / vehicle_speed
 *
 * In EXPLICIT mode: may read partially-updated data (race condition)
 * In IMPLICIT mode: always reads consistent snapshot
 */
void App_Runnable_TorqueCalc_1ms(void);

/**
 * @brief 100ms Runnable — Diagnostic Monitor
 *
 * Checks sensor plausibility:
 * - Torque input within valid range?
 * - Vehicle speed within valid range?
 * - Motor torque command reasonable?
 *
 * Reports PASSED/FAILED to DEM for each check.
 */
void App_Runnable_DiagMonitor_100ms(void);

/* --- Debug --- */
uint32 App_GetRaceConditionDetections(void);

#endif /* APP_SWC_H */