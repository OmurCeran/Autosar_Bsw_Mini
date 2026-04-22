/**
 * @file    Mini_Dem.h
 * @brief   Mini Diagnostic Event Manager — Event Debounce & DTC Status
 *
 * Maps to real AUTOSAR: DEM (Diagnostic Event Manager)
 *
 * In real AUTOSAR, DEM does:
 * - Receive event reports from SWCs (Passed/Failed)
 * - Apply debounce algorithm (counter-based or time-based)
 * - Manage DTC status byte (8 bits per ISO 14229)
 * - Store freeze frame data when event transitions to Failed
 * - Store event data to NvM (asynchronous, via MainFunction)
 *
 * Here we implement counter-based debounce and status byte management.
 */

#ifndef MINI_DEM_H
#define MINI_DEM_H

#include "Std_Types.h"

/* --- Event IDs --- */
typedef enum {
    DEM_EVENT_TORQUE_SENSOR_FAULT = 0U,
    DEM_EVENT_VEHICLE_SPEED_FAULT,
    DEM_EVENT_MOTOR_OVERCURRENT,
    DEM_EVENT_COMMUNICATION_LOSS,
    DEM_EVENT_COUNT
} Dem_EventIdType;

/* --- Event Status (reported by SWC monitor) --- */
typedef enum {
    DEM_EVENT_STATUS_PASSED  = 0U,
    DEM_EVENT_STATUS_FAILED  = 1U,
    DEM_EVENT_STATUS_PREPASSED = 2U,
    DEM_EVENT_STATUS_PREFAILED = 3U
} Dem_EventStatusType;

/* --- DTC Status Byte (ISO 14229-1:2013), page 222 --- */
/* Each bit has a specific meaning — this is what tester reads via UDS 0x19 */
#define DEM_UDS_STATUS_TF        (0x01U)  /* Bit 0: testFailed */
#define DEM_UDS_STATUS_TFTOC     (0x02U)  /* Bit 1: testFailedThisOperationCycle */
#define DEM_UDS_STATUS_PDTC      (0x04U)  /* Bit 2: pendingDTC */
#define DEM_UDS_STATUS_CDTC      (0x08U)  /* Bit 3: confirmedDTC */
#define DEM_UDS_STATUS_TNCSLC    (0x10U)  /* Bit 4: testNotCompletedSinceLastClear */
#define DEM_UDS_STATUS_TFSLC     (0x20U)  /* Bit 5: testFailedSinceLastClear */
#define DEM_UDS_STATUS_TNCTOC    (0x40U)  /* Bit 6: testNotCompletedThisOperationCycle */
#define DEM_UDS_STATUS_WIR       (0x80U)  /* Bit 7: warningIndicatorRequested (MIL) */

/* --- Debounce Configuration --- */
/* Counter-based: increment on PREFAILED, decrement on PREPASSED
 * When counter >= threshold_failed → event FAILED
 * When counter <= threshold_passed → event PASSED */
typedef struct {
    sint16 threshold_failed;   /* e.g., +5: need 5 consecutive fails */
    sint16 threshold_passed;   /* e.g., -5: need 5 consecutive passes */
    sint16 step_up;            /* increment per PREFAILED report */
    sint16 step_down;          /* decrement per PREPASSED report */
} Dem_DebounceConfigType;

/* --- Freeze Frame Data --- */
/* Snapshot of application data at the moment event becomes FAILED */
typedef struct {
    uint16 torque_input;
    uint16 vehicle_speed;
    uint16 motor_current;
    uint16 steering_angle;
    uint32 timestamp_ms;
} Dem_FreezeFrameType;

/* --- Event Runtime Data --- */
typedef struct {
    uint8                statusByte;       /* UDS DTC status byte */
    sint16               debounceCounter;  /* Current debounce value */
    boolean              isStored;         /* Already written to NvM? */
    Dem_FreezeFrameType  freezeFrame;      /* Captured at FAILED transition */
} Dem_EventDataType;

/* --- API --- */
void Dem_Init(void);

/* SWC calls this to report event status — main entry point , AUTOSAR_SWS_DiagnosticEventManager.pdf page 237 */
Std_ReturnType Dem_SetEventStatus(Dem_EventIdType eventId, Dem_EventStatusType status);

/* Read DTC status byte (used by DCM for UDS 0x19 response) */
uint8 Dem_GetDtcStatusByte(Dem_EventIdType eventId);

/* Read freeze frame (used by DCM for UDS 0x19 04 response) */
Std_ReturnType Dem_GetFreezeFrame(Dem_EventIdType eventId, Dem_FreezeFrameType *ffData);

/* Clear DTC (used by DCM for UDS 0x14 service) */
Std_ReturnType Dem_ClearDtc(Dem_EventIdType eventId);

/* MainFunction: process debounce, write to NvM (simulated) */
void Dem_MainFunction(void);

/* Set freeze frame source data (called by app before reporting) */
void Dem_SetFreezeFrameData(uint16 torque, uint16 speed, uint16 current, uint16 angle);

#endif /* MINI_DEM_H */