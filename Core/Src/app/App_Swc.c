/**
 * @file    App_Swc.c
 * @brief   Application SWC — EPS Torque Calculation & Race Condition Demo
 */

#include "App_Swc.h"
#include "Mini_Rte.h"
#include "Mini_Dem.h"
#include "Mini_Com.h"

/* --- Sensor simulation state --- */
static float32 sim_torqueBase     = 5.0f;    /* Base torque (Nm) */
static float32 sim_speedBase      = 60.0f;   /* Base speed (km/h) */
static float32 sim_calibCoeff     = 0.8f;    /* Calibration coefficient */
static uint32  sim_cycleCounter   = 0U;
static uint32  app_raceDetections = 0U;

/* --- Valid ranges for diagnostic monitoring --- */
#define TORQUE_MIN      0.0f
#define TORQUE_MAX      100.0f
#define SPEED_MIN       0.0f
#define SPEED_MAX       300.0f
#define MOTOR_CMD_MIN   (-50.0f)
#define MOTOR_CMD_MAX   50.0f

void App_Swc_Init(void)
{
    sim_cycleCounter   = 0U;
    app_raceDetections = 0U;
}

/**
 * @brief 10ms Runnable — Sensor Data Update (LOW priority task)
 *
 * Simulates reading sensors and writing to RTE.
 * In EXPLICIT mode: writes torque first, then speed — with a deliberate
 * gap between writes where preemption CAN occur.
 *
 * The volatile keyword and NOP loop simulate the real-world scenario
 * where sensor reads take time (ADC conversion, I2C transfer, etc.)
 */
void App_Runnable_SensorUpdate_10ms(void)
{
    Rte_AccessModeType mode = Rte_GetAccessMode();

    /* Simulate sensor values changing over time */
    sim_cycleCounter++;
    float32 torque = sim_torqueBase + (float32)(sim_cycleCounter % 20) * 0.5f;
    float32 speed  = sim_speedBase  + (float32)(sim_cycleCounter % 10) * 2.0f;

    if (mode == RTE_ACCESS_EXPLICIT)
    {
        /*
         * EXPLICIT MODE — DANGEROUS
         *
         * Write torque_input first...
         */
        Rte_Write_TorqueInput(torque);

        /*
         * === PREEMPTION WINDOW ===
         * Right here, the 1ms task (higher priority) can preempt us.
         * If it does, it will read:
         *   - torque_input  = NEW value (just written above)
         *   - vehicle_speed = OLD value (not yet written below)
         * This is the RACE CONDITION.
         *
         * In real hardware, this gap is where ADC conversion or
         * I2C read for the speed sensor would happen.
         */
        volatile uint32 delay;
        for (delay = 0; delay < 100; delay++) { /* Simulate sensor read time */ }

        /* ...then write vehicle_speed */
        Rte_Write_VehicleSpeed(speed);
        Rte_Write_CalibCoeff(sim_calibCoeff);
    }
    else /* RTE_ACCESS_IMPLICIT */
    {
        /*
         * IMPLICIT MODE — SAFE
         *
         * All writes go to LOCAL buffer. They are flushed to global
         * ATOMICALLY at task end (Rte_Task_End).
         * Even if 1ms task preempts between these writes,
         * it reads from its OWN snapshot taken at Rte_Task_Begin.
         */
        Rte_IWrite_SteeringAngle(torque * 2.0f);  /* Example derived value */

        /* The actual sensor data is written via explicit in the writer task.
         * In implicit mode, the READER (1ms task) is protected by snapshot.
         * Writer still uses explicit to update global, but reader never sees
         * partial updates because it reads from its local copy. */
        Rte_Write_TorqueInput(torque);
        Rte_Write_VehicleSpeed(speed);
        Rte_Write_CalibCoeff(sim_calibCoeff);
    }

    /* Update DEM freeze frame data source */
    Dem_SetFreezeFrameData(
        (uint16)(torque * 10.0f),
        (uint16)(speed * 10.0f),
        0U,  /* Motor current — not simulated */
        (uint16)(torque * 20.0f)  /* Steering angle approximation */
    );
}

/**
 * @brief 1ms Runnable — Torque Calculation (HIGH priority task)
 *
 * Reads sensor data and calculates motor torque command.
 * In EXPLICIT mode: may read inconsistent data → wrong output.
 * In IMPLICIT mode: always consistent → correct output.
 */
void App_Runnable_TorqueCalc_1ms(void)
{
    float32 torque, speed, coeff;
    float32 motor_cmd;
    Rte_AccessModeType mode = Rte_GetAccessMode();

    if (mode == RTE_ACCESS_EXPLICIT)
    {
        /* Direct read from global — may be inconsistent! */
        Rte_Read_TorqueInput(&torque);
        Rte_Read_VehicleSpeed(&speed);
        Rte_Read_CalibCoeff(&coeff);

        /* Check for inconsistency (demo only) */
        Rte_CheckConsistency(torque, speed);
    }
    else /* RTE_ACCESS_IMPLICIT */
    {
        /* Read from local snapshot — always consistent */
        torque = Rte_IRead_TorqueInput();
        speed  = Rte_IRead_VehicleSpeed();
        coeff  = Rte_IRead_CalibCoeff();
    }

    /* Torque calculation — simplified EPS assist formula */
    if (speed > 0.1f)
    {
        /*
         * At low speed → high assist (parking)
         * At high speed → low assist (highway)
         * In race condition: wrong speed value → wrong assist level
         * → momentary torque drop → driver feels steering become heavy
         */
        motor_cmd = torque * coeff * (100.0f / speed);
    }
    else
    {
        motor_cmd = torque * coeff * 1000.0f;  /* Max assist at standstill */
    }

    /* Clamp output */
    if (motor_cmd > MOTOR_CMD_MAX) motor_cmd = MOTOR_CMD_MAX;
    if (motor_cmd < MOTOR_CMD_MIN) motor_cmd = MOTOR_CMD_MIN;

    /* Write output */
    if (mode == RTE_ACCESS_EXPLICIT)
    {
        Rte_Write_MotorTorqueCmd(motor_cmd);
    }
    else
    {
        Rte_IWrite_MotorTorqueCmd(motor_cmd);
    }

    /* Pack torque command into COM for CAN transmission */
    uint16 cmd_raw = (uint16)((motor_cmd + 50.0f) * 100.0f);  /* Scale + offset */
    Com_SendSignal(COM_SIG_MOTOR_TORQUE_CMD, &cmd_raw);
}

/**
 * @brief 100ms Runnable — Diagnostic Monitor
 *
 * Checks sensor plausibility and reports to DEM.
 * In real AUTOSAR: monitor functions are SWC runnables that
 * call Dem_SetEventStatus() based on runtime checks.
 */
void App_Runnable_DiagMonitor_100ms(void)
{
    const Rte_SteeringDataType *data = Rte_Debug_GetGlobalData();

    /* Check torque sensor range */
    if (data->torque_input < TORQUE_MIN || data->torque_input > TORQUE_MAX)
    {
        Dem_SetEventStatus(DEM_EVENT_TORQUE_SENSOR_FAULT, DEM_EVENT_STATUS_PREFAILED);
    }
    else
    {
        Dem_SetEventStatus(DEM_EVENT_TORQUE_SENSOR_FAULT, DEM_EVENT_STATUS_PREPASSED);
    }

    /* Check vehicle speed range */
    if (data->vehicle_speed < SPEED_MIN || data->vehicle_speed > SPEED_MAX)
    {
        Dem_SetEventStatus(DEM_EVENT_VEHICLE_SPEED_FAULT, DEM_EVENT_STATUS_PREFAILED);
    }
    else
    {
        Dem_SetEventStatus(DEM_EVENT_VEHICLE_SPEED_FAULT, DEM_EVENT_STATUS_PREPASSED);
    }

    /* Check motor torque command range */
    if (data->motor_torque_cmd < MOTOR_CMD_MIN || data->motor_torque_cmd > MOTOR_CMD_MAX)
    {
        Dem_SetEventStatus(DEM_EVENT_MOTOR_OVERCURRENT, DEM_EVENT_STATUS_PREFAILED);
    }
    else
    {
        Dem_SetEventStatus(DEM_EVENT_MOTOR_OVERCURRENT, DEM_EVENT_STATUS_PREPASSED);
    }

    /* Check race condition counter */
    uint32 inconsistencies = Rte_GetInconsistencyCount();
    if (inconsistencies > app_raceDetections)
    {
        app_raceDetections = inconsistencies;
        /* In a real system this would trigger a safety reaction */
    }
}

uint32 App_GetRaceConditionDetections(void)
{
    return Rte_GetInconsistencyCount();
}