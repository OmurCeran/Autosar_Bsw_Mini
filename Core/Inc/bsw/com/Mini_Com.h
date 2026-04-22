/**
 * @file    Mini_Com.h
 * @brief   Mini Communication Module — Signal Packing & I-PDU Management
 *
 * Maps to real AUTOSAR: COM + PduR + CanIf (simplified into one module)
 *
 * In real AUTOSAR, COM does:
 * - Pack application signals into I-PDUs (bit position, byte order, scaling)
 * - Unpack received I-PDUs back to signals
 * - Route I-PDUs through PduR to CanIf for transmission
 *
 * Here we demonstrate the packing/unpacking mechanism and simulate
 * CAN transmission via UART output.
 */

#ifndef MINI_COM_H
#define MINI_COM_H

#include "Std_Types.h"

/* --- Signal IDs --- */
/* In real AUTOSAR: these come from DaVinci Configurator ARXML generation */
typedef enum {
    COM_SIG_TORQUE_INPUT = 0U,    /* 16-bit, offset 0,  big-endian */
    COM_SIG_VEHICLE_SPEED,        /* 16-bit, offset 16, big-endian */
    COM_SIG_MOTOR_TORQUE_CMD,     /* 16-bit, offset 32, big-endian */
    COM_SIG_STEERING_ANGLE,       /* 16-bit, offset 48, big-endian */
    COM_SIG_COUNT                 /* Total number of signals */
} Com_SignalIdType;

/* --- I-PDU IDs --- */
typedef enum {
    COM_PDU_EPS_TX = 0U,          /* EPS → CAN bus (torque cmd + steering angle) */
    COM_PDU_SENSOR_RX,            /* CAN bus → EPS (torque input + vehicle speed) */
    COM_PDU_COUNT
} Com_PduIdType;

/* --- Signal Configuration --- */
typedef struct {
    Com_SignalIdType signalId;
    Com_PduIdType    pduId;       /* Which I-PDU this signal belongs to */
    uint8            startBit;    /* Bit position within I-PDU */
    uint8            bitLength;   /* Signal width in bits */
    boolean          isBigEndian; /* Byte order: TRUE = Motorola, FALSE = Intel */
} Com_SignalConfigType;

/* --- I-PDU Buffer --- */
#define COM_PDU_MAX_LENGTH  8U    /* Classic CAN: 8 bytes max */

typedef struct {
    uint8  data[COM_PDU_MAX_LENGTH];
    uint8  length;
    boolean updated;              /* Flag: new data available for Tx */
} Com_PduBufferType;

/* --- API --- */
void Com_Init(void);

/* Signal write: SWC calls this via RTE to send a signal value */
Std_ReturnType Com_SendSignal(Com_SignalIdType signalId, const void *signalDataPtr);

/* Signal read: SWC calls this via RTE to receive a signal value */
Std_ReturnType Com_ReceiveSignal(Com_SignalIdType signalId, void *signalDataPtr);

/* MainFunction: called by SchM every 10ms — triggers I-PDU transmission */
void Com_MainFunctionTx(void);

/* Simulate receiving a CAN frame (called from test/debug) */
void Com_RxIndication(Com_PduIdType pduId, const uint8 *data, uint8 length);

#endif /* MINI_COM_H */