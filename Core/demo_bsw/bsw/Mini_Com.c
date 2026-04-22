/**
 * @file    Mini_Com.c
 * @brief   Mini COM Implementation — Signal Packing & I-PDU Transmission
 *
 * Demonstrates how AUTOSAR COM packs application signals into
 * bit-level positions within I-PDUs (CAN frames).
 *
 * Key concepts:
 * - Big-endian (Motorola) vs Little-endian (Intel) byte order
 * - Bit-level packing with startBit and bitLength
 * - I-PDU Tx triggering via MainFunction (not per-signal write)
 */

#include "Mini_Com.h"
#include "Mini_SchM.h"

/* ============================================================
 * SIGNAL CONFIGURATION TABLE
 *
 * In real AUTOSAR: auto-generated from DaVinci Configurator ARXML.
 * Here we define manually as a const table.
 *
 * Example I-PDU layout for COM_PDU_EPS_TX (8 bytes):
 * Byte: |  0  |  1  |  2  |  3  |  4  |  5  |  6  |  7  |
 * Bit:   0-7   8-15  16-23 24-31 32-39 40-47 48-55 56-63
 * Sig:   [ MotorTorqueCmd  ][ SteeringAngle  ][           ]
 * ============================================================ */
static const Com_SignalConfigType com_signalConfig[COM_SIG_COUNT] = {
    /* Signal ID,                 PDU ID,              StartBit, BitLen, BigEndian */
    { COM_SIG_TORQUE_INPUT,       COM_PDU_SENSOR_RX,   0U,       16U,    TRUE  },
    { COM_SIG_VEHICLE_SPEED,      COM_PDU_SENSOR_RX,   16U,      16U,    TRUE  },
    { COM_SIG_MOTOR_TORQUE_CMD,   COM_PDU_EPS_TX,      0U,       16U,    TRUE  },
    { COM_SIG_STEERING_ANGLE,     COM_PDU_EPS_TX,      16U,      16U,    TRUE  }
};

/* ============================================================
 * I-PDU BUFFERS
 *
 * In real AUTOSAR: allocated by COM generator, accessed through PduR.
 * ============================================================ */
static Com_PduBufferType com_pduBuffer[COM_PDU_COUNT] = {
    /* EPS_TX:    8 bytes, initially empty */
    { .data = {0}, .length = 8U, .updated = FALSE },
    /* SENSOR_RX: 8 bytes, initially empty */
    { .data = {0}, .length = 8U, .updated = FALSE }
};

/* Statistics for debug */
static uint32 com_txCounter = 0U;

/* ============================================================
 * INTERNAL: Bit-level packing helpers
 * ============================================================ */

/**
 * @brief Pack a value into an I-PDU at specific bit position
 *
 * Handles big-endian (Motorola) byte order as used in automotive CAN.
 * For simplicity, handles only 16-bit signals aligned to byte boundaries.
 *
 * Real AUTOSAR COM handles any bit-length and alignment — this is simplified.
 */
static void Com_PackSignal_u16(uint8 *pduData, uint8 startBit, uint16 value, boolean bigEndian)
{
    uint8 byteOffset = startBit / 8U;

    if (bigEndian)
    {
        /* Motorola: MSB first */
        pduData[byteOffset]     = (uint8)((value >> 8U) & 0xFFU);
        pduData[byteOffset + 1] = (uint8)(value & 0xFFU);
    }
    else
    {
        /* Intel: LSB first */
        pduData[byteOffset]     = (uint8)(value & 0xFFU);
        pduData[byteOffset + 1] = (uint8)((value >> 8U) & 0xFFU);
    }
}

/**
 * @brief Unpack a 16-bit value from an I-PDU at specific bit position
 */
static uint16 Com_UnpackSignal_u16(const uint8 *pduData, uint8 startBit, boolean bigEndian)
{
    uint8 byteOffset = startBit / 8U;
    uint16 value;

    if (bigEndian)
    {
        value = ((uint16)pduData[byteOffset] << 8U) | (uint16)pduData[byteOffset + 1];
    }
    else
    {
        value = ((uint16)pduData[byteOffset + 1] << 8U) | (uint16)pduData[byteOffset];
    }

    return value;
}

/* ============================================================
 * PUBLIC API
 * ============================================================ */

void Com_Init(void)
{
    uint8 i, j;

    for (i = 0U; i < COM_PDU_COUNT; i++)
    {
        for (j = 0U; j < COM_PDU_MAX_LENGTH; j++)
        {
            com_pduBuffer[i].data[j] = 0U;
        }
        com_pduBuffer[i].updated = FALSE;
    }

    com_txCounter = 0U;
}

/**
 * @brief Send a signal — packs it into the configured I-PDU buffer
 *
 * Does NOT trigger transmission immediately. Transmission happens
 * periodically in Com_MainFunctionTx() — this matches AUTOSAR behavior.
 */
Std_ReturnType Com_SendSignal(Com_SignalIdType signalId, const void *signalDataPtr)
{
    if (signalId >= COM_SIG_COUNT || signalDataPtr == NULL_PTR)
    {
        return E_NOT_OK;
    }

    const Com_SignalConfigType *cfg = &com_signalConfig[signalId];
    uint16 value = *((const uint16 *)signalDataPtr);

    /* Packing modifies shared PDU buffer — protect with exclusive area.
     * Keep the critical section SHORT — just the pack operation. */
    SchM_Enter_ExclusiveArea();
    Com_PackSignal_u16(com_pduBuffer[cfg->pduId].data,
                       cfg->startBit, value, cfg->isBigEndian);
    com_pduBuffer[cfg->pduId].updated = TRUE;
    SchM_Exit_ExclusiveArea();

    return E_OK;
}

/**
 * @brief Receive a signal — unpacks from the I-PDU buffer
 */
Std_ReturnType Com_ReceiveSignal(Com_SignalIdType signalId, void *signalDataPtr)
{
    if (signalId >= COM_SIG_COUNT || signalDataPtr == NULL_PTR)
    {
        return E_NOT_OK;
    }

    const Com_SignalConfigType *cfg = &com_signalConfig[signalId];
    uint16 value;

    SchM_Enter_ExclusiveArea();
    value = Com_UnpackSignal_u16(com_pduBuffer[cfg->pduId].data,
                                 cfg->startBit, cfg->isBigEndian);
    SchM_Exit_ExclusiveArea();

    *((uint16 *)signalDataPtr) = value;
    return E_OK;
}

/**
 * @brief 10ms MainFunction — triggers I-PDU transmission
 *
 * In real AUTOSAR: COM → PduR → CanIf → CAN Driver → bus
 * Here: we simulate by printing the I-PDU contents via UART.
 *
 * Only PDUs marked as "updated" are transmitted.
 */
void Com_MainFunctionTx(void)
{
    uint8 pduId;

    for (pduId = 0U; pduId < COM_PDU_COUNT; pduId++)
    {
        /* Only transmit Tx PDUs (EPS_TX in this demo) */
        if (pduId != COM_PDU_EPS_TX)
        {
            continue;
        }

        SchM_Enter_ExclusiveArea();
        boolean needTx = com_pduBuffer[pduId].updated;
        com_pduBuffer[pduId].updated = FALSE;
        SchM_Exit_ExclusiveArea();

        if (needTx)
        {
            /* TODO: In real system, call CanIf_Transmit(pduId, pduInfo)
             * Here we simulate by incrementing a counter.
             * Add UART printf for debug: "Tx PDU %u: %02X %02X...\r\n" */
            com_txCounter++;
        }
    }
}

/**
 * @brief Simulate receiving a CAN frame
 *
 * In real AUTOSAR: called by CanIf upon frame reception, via PduR.
 * Here: called manually for testing Rx path.
 */
void Com_RxIndication(Com_PduIdType pduId, const uint8 *data, uint8 length)
{
    if (pduId >= COM_PDU_COUNT || data == NULL_PTR || length > COM_PDU_MAX_LENGTH)
    {
        return;
    }

    SchM_Enter_ExclusiveArea();
    uint8 i;
    for (i = 0U; i < length; i++)
    {
        com_pduBuffer[pduId].data[i] = data[i];
    }
    SchM_Exit_ExclusiveArea();
}
