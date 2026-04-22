/**
 * @file    Mini_Rte.h
 * @brief   Mini Runtime Environment — Implicit vs Explicit Data Access
 *
 * Maps to real AUTOSAR: RTE (Runtime Environment)
 *
 * This is the KEY module for demonstrating the race condition problem
 * and its AUTOSAR solution. The EPS debug story is built on this concept.
 *
 * EXPLICIT mode (Rte_Read/Write):
 *   - Reads/writes directly from/to global buffer
 *   - If preemption occurs mid-update → inconsistent data (RACE CONDITION)
 *
 * IMPLICIT mode (Rte_IRead/IWrite):
 *   - Task start: snapshot global → local copy
 *   - Task runs: reads/writes local copy only
 *   - Task end: flush local → global (atomic, inside exclusive area)
 *   - Result: ALWAYS consistent data within a task
 */

#ifndef MINI_RTE_H
#define MINI_RTE_H

#include "Std_Types.h"

/* --- Steering Data Structure --- */
/* This is what SWCs share — the struct that caused the race condition */
typedef struct {
    float32 torque_input;       /* From torque sensor (Nm) */
    float32 vehicle_speed;      /* From vehicle speed sensor (km/h) */
    float32 calib_coeff;        /* Calibration coefficient */
    float32 motor_torque_cmd;   /* Calculated output to motor (Nm) */
    float32 steering_angle;     /* Steering wheel angle (degrees) */
} Rte_SteeringDataType;

/* --- Access Mode --- */
typedef enum {
    RTE_ACCESS_EXPLICIT = 0U,   /* Direct access — race condition possible */
    RTE_ACCESS_IMPLICIT         /* Snapshot-based — always consistent */
} Rte_AccessModeType;

/* --- API --- */
void Rte_Init(void);

/* Set access mode (for demo: switch between explicit and implicit) */
void Rte_SetAccessMode(Rte_AccessModeType mode);
Rte_AccessModeType Rte_GetAccessMode(void);

/* --- Explicit Access (DANGEROUS — race condition possible) --- */
/* In real AUTOSAR: Rte_Read_<port>_<element>() */
Std_ReturnType Rte_Read_TorqueInput(float32 *value);
Std_ReturnType Rte_Read_VehicleSpeed(float32 *value);
Std_ReturnType Rte_Read_CalibCoeff(float32 *value);

/* In real AUTOSAR: Rte_Write_<port>_<element>() */
Std_ReturnType Rte_Write_TorqueInput(float32 value);
Std_ReturnType Rte_Write_VehicleSpeed(float32 value);
Std_ReturnType Rte_Write_CalibCoeff(float32 value);
Std_ReturnType Rte_Write_MotorTorqueCmd(float32 value);

/* --- Implicit Access (SAFE — consistent snapshot) --- */
/* Must be called at task BEGIN — takes snapshot of global data */
void Rte_Task_Begin(void);

/* Must be called at task END — flushes local output to global */
void Rte_Task_End(void);

/* In real AUTOSAR: Rte_IRead_<runnable>_<port>_<element>() */
float32 Rte_IRead_TorqueInput(void);
float32 Rte_IRead_VehicleSpeed(void);
float32 Rte_IRead_CalibCoeff(void);

/* In real AUTOSAR: Rte_IWrite_<runnable>_<port>_<element>() */
void Rte_IWrite_MotorTorqueCmd(float32 value);
void Rte_IWrite_SteeringAngle(float32 value);

/* --- Debug: read current global values (for monitoring) --- */
const Rte_SteeringDataType* Rte_Debug_GetGlobalData(void);

/* --- Race condition counter (for demo) --- */
uint32 Rte_GetInconsistencyCount(void);
void   Rte_ResetInconsistencyCount(void);

/* --- Consistency check (for demo) --- */
void Rte_CheckConsistency(float32 readTorque, float32 readSpeed);

#endif /* MINI_RTE_H */