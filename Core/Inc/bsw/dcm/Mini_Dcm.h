/**
 * @file    Mini_Dcm.h
 * @brief   Mini Diagnostic Communication Manager — UDS Service Handler
 *
 * Maps to real AUTOSAR: DCM (Diagnostic Communication Manager)
 *
 * In real AUTOSAR, DCM handles:
 * - DSD (Diagnostic Service Dispatcher): routes requests to service handlers
 * - DSL (Diagnostic Session Layer): manages sessions, timers, security
 * - DSP (Diagnostic Service Processor): individual service implementations
 *
 * Here we implement the core dispatcher pattern plus 4 key services:
 *   0x10 — DiagnosticSessionControl
 *   0x22 — ReadDataByIdentifier
 *   0x19 — ReadDTCInformation (subfunction 0x02)
 *   0x14 — ClearDiagnosticInformation
 *
 * Transport: UART (simulates ISO-TP over CAN). Single-frame only for simplicity.
 */

#ifndef MINI_DCM_H
#define MINI_DCM_H

#include "Std_Types.h"

/* --- UDS Service IDs (ISO 14229) --- */
#define UDS_SID_SESSION_CONTROL       (0x10U)
#define UDS_SID_ECU_RESET             (0x11U)
#define UDS_SID_CLEAR_DTC             (0x14U)
#define UDS_SID_READ_DTC              (0x19U)
#define UDS_SID_READ_DID              (0x22U)
#define UDS_SID_SECURITY_ACCESS       (0x27U)
#define UDS_SID_TESTER_PRESENT        (0x3EU)

/* --- Positive Response bit: add 0x40 to SID --- */
#define UDS_POSITIVE_RESPONSE_MASK    (0x40U)

/* --- Negative Response SID --- */
#define UDS_NRC_SID                   (0x7FU)

/* --- Negative Response Codes (NRC) --- */
#define UDS_NRC_SERVICE_NOT_SUPPORTED       (0x11U)
#define UDS_NRC_SUBFUNCTION_NOT_SUPPORTED   (0x12U)
#define UDS_NRC_INCORRECT_MESSAGE_LENGTH    (0x13U)
#define UDS_NRC_CONDITIONS_NOT_CORRECT      (0x22U)
#define UDS_NRC_REQUEST_OUT_OF_RANGE        (0x31U)
#define UDS_NRC_SECURITY_ACCESS_DENIED      (0x33U)
#define UDS_NRC_RESPONSE_PENDING            (0x78U)

/* --- Session Types --- */
typedef enum {
    DCM_SESSION_DEFAULT      = 0x01U,
    DCM_SESSION_PROGRAMMING  = 0x02U,
    DCM_SESSION_EXTENDED     = 0x03U
} Dcm_SessionType;

/* --- Buffer Sizes --- */
#define DCM_MAX_REQUEST_SIZE    (32U)
#define DCM_MAX_RESPONSE_SIZE   (64U)

/* --- Public API --- */

void Dcm_Init(void);

/**
 * @brief Process a received UDS frame
 * Called from UART RX handler when a complete frame arrives.
 * Builds and transmits a response automatically.
 */
void Dcm_ProcessRequest(const uint8 *request, uint8 length);

/**
 * @brief Get current diagnostic session
 */
Dcm_SessionType Dcm_GetCurrentSession(void);

/**
 * @brief Feed a byte from UART RX (called by HAL UART RxCpltCallback)
 * Frame delimiters: request ends on CR (0x0D) or LF (0x0A).
 * In real CAN-ISO-TP this would be length-prefix + timeout based.
 */
void Dcm_FeedRxByte(uint8 rxByte);

#endif /* MINI_DCM_H */