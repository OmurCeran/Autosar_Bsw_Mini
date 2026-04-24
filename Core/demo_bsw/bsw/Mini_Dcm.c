/**
 * @file    Mini_Dcm.c
 * @brief   Mini DCM Implementation — UDS Service Dispatcher
 */

#include "Mini_Dcm.h"
#include "Mini_Dem.h"
#include "Mini_Nvm.h"
#include "Mini_Timestamp.h"
#include "Mini_SchM.h"
#include "main.h"
#include <string.h>

extern UART_HandleTypeDef huart2;

/* ============================================================
 * INTERNAL STATE
 * ============================================================ */

/* Current active diagnostic session */
static Dcm_SessionType dcm_currentSession = DCM_SESSION_DEFAULT;

/* RX buffer — accumulates incoming bytes until frame delimiter */
static uint8 dcm_rxBuffer[DCM_MAX_REQUEST_SIZE];
static uint8 dcm_rxIndex = 0U;

/* TX buffer — built by service handler, sent by transmitter */
static uint8 dcm_txBuffer[DCM_MAX_RESPONSE_SIZE];

/* Statistics */
static uint32 dcm_totalRequests  = 0U;
static uint32 dcm_totalResponses = 0U;
static uint32 dcm_totalNrc       = 0U;

/* ============================================================
 * DID TABLE — what ReadDataByIdentifier returns
 * ============================================================ */
typedef struct {
    uint16        did;
    uint8         length;
    const uint8  *data;
    const char   *name;
} Dcm_DidEntryType;

/* Simulated VIN (17 ASCII chars — ISO 3779) */
static const uint8 dcm_did_F190_vin[17] = {
    'W','A','U','Z','Z','Z','8','V','0','N','A','0','1','2','3','4','5'
};

/* Simulated ECU Serial Number */
static const uint8 dcm_did_F18C_serial[10] = {
    'S','N','1','2','3','4','5','6','7','8'
};

/* Simulated Software Version */
static const uint8 dcm_did_F195_swver[4] = {
    0x01U, 0x00U, 0x02U, 0x03U   /* v1.0.2.3 */
};

/* Simulated Hardware Version */
static const uint8 dcm_did_F191_hwver[4] = {
    'S','T','M','4'
};

static const Dcm_DidEntryType dcm_didTable[] = {
    { 0xF190U, 17U, dcm_did_F190_vin,    "VIN"             },
    { 0xF18CU, 10U, dcm_did_F18C_serial, "ECU_SERIAL_NUM"  },
    { 0xF195U,  4U, dcm_did_F195_swver,  "SW_VERSION"      },
    { 0xF191U,  4U, dcm_did_F191_hwver,  "HW_VERSION"      }
};

#define DCM_DID_TABLE_SIZE  (sizeof(dcm_didTable) / sizeof(dcm_didTable[0]))

/* ============================================================
 * HELPERS — hex parsing from ASCII
 *
 * Input format: ASCII hex bytes separated by spaces, ended with \r or \n
 * Example: "22 F1 90\r" -> parses to [0x22, 0xF1, 0x90]
 * ============================================================ */

static sint16 Dcm_AsciiHexToByte(uint8 high, uint8 low)
{
    uint8 result = 0U;

    /* High nibble */
    if      (high >= '0' && high <= '9') result = (uint8)((high - '0') << 4U);
    else if (high >= 'A' && high <= 'F') result = (uint8)((high - 'A' + 10) << 4U);
    else if (high >= 'a' && high <= 'f') result = (uint8)((high - 'a' + 10) << 4U);
    else return -1;

    /* Low nibble */
    if      (low >= '0' && low <= '9') result |= (uint8)(low - '0');
    else if (low >= 'A' && low <= 'F') result |= (uint8)(low - 'A' + 10);
    else if (low >= 'a' && low <= 'f') result |= (uint8)(low - 'a' + 10);
    else return -1;

    return (sint16)result;
}

/**
 * @brief Parse an ASCII hex request string into binary bytes
 * Returns number of parsed bytes, or 0 on parse error.
 */
static uint8 Dcm_ParseAsciiRequest(const uint8 *ascii, uint8 asciiLen,
                                   uint8 *binOut, uint8 binMaxLen)
{
    uint8 binLen = 0U;
    uint8 i = 0U;

    while (i < asciiLen && binLen < binMaxLen)
    {
        /* Skip whitespace */
        while (i < asciiLen && (ascii[i] == ' ' || ascii[i] == '\t'))
        {
            i++;
        }

        /* Need at least 2 chars for a hex byte */
        if (i + 1U >= asciiLen) break;

        sint16 byte = Dcm_AsciiHexToByte(ascii[i], ascii[i + 1U]);
        if (byte < 0) return 0U;   /* parse error */

        binOut[binLen++] = (uint8)byte;
        i += 2U;
    }

    return binLen;
}

/* ============================================================
 * RESPONSE TRANSMISSION
 *
 * In real ECU: DCM hands the response to PduR → CanIf → CAN driver.
 * Here: format as ASCII hex and send via DMA UART.
 * ============================================================ */

static void Dcm_SendResponse(const uint8 *response, uint8 length)
{
    /* Build ASCII output with separator */
    char txAscii[DCM_MAX_RESPONSE_SIZE * 3 + 16];
    int txLen = 0;

    const char hex[] = "0123456789ABCDEF";

    /* Prefix for readability */
    const char *prefix = "[ECU]  -> ";
    uint8 p = 0U;
    while (prefix[p] != '\0') { txAscii[txLen++] = prefix[p++]; }

    /* Each byte as "HH " */
    for (uint8 i = 0U; i < length; i++)
    {
        txAscii[txLen++] = hex[(response[i] >> 4U) & 0x0FU];
        txAscii[txLen++] = hex[response[i] & 0x0FU];
        txAscii[txLen++] = ' ';
    }

    txAscii[txLen++] = '\r';
    txAscii[txLen++] = '\n';
    txAscii[txLen++] = '\0';
    /* Log structured message first (via normal logging) */
    if (response[0] == (UDS_NRC_SID))
    {
        Log_Write(LOG_TAG_DCM, "Response: NRC 0x%02X on SID 0x%02X",
                  response[2], response[1]);
        dcm_totalNrc++;
    }
    else
    {
        Log_Write(LOG_TAG_DCM, "Response: positive (SID 0x%02X, %u bytes)",
                  response[0] - UDS_POSITIVE_RESPONSE_MASK, (uint32)length);
        dcm_totalResponses++;
    }
    /*Raw bytes*/
    Log_Raw(txAscii);
    //DMA_Printf("%s", txAscii);
    /* Then send the raw bytes */
    //HAL_UART_Transmit(&huart2, (uint8_t*)txAscii, txLen, HAL_MAX_DELAY);
}

static void Dcm_SendNegativeResponse(uint8 sid, uint8 nrc)
{
    dcm_txBuffer[0] = UDS_NRC_SID;
    dcm_txBuffer[1] = sid;
    dcm_txBuffer[2] = nrc;
    Dcm_SendResponse(dcm_txBuffer, 3U);
}

/* ============================================================
 * SERVICE HANDLERS
 * ============================================================ */

/**
 * @brief 0x10 — DiagnosticSessionControl
 * Request format:  10 <sessionType>
 * Response format: 50 <sessionType> <P2Server> <P2StarServer>
 */
static void Dcm_HandleSessionControl(const uint8 *req, uint8 len)
{
    if (len != 2U)
    {
        Dcm_SendNegativeResponse(UDS_SID_SESSION_CONTROL, UDS_NRC_INCORRECT_MESSAGE_LENGTH);
        return;
    }

    uint8 requestedSession = req[1];

    if (requestedSession != DCM_SESSION_DEFAULT  &&
        requestedSession != DCM_SESSION_EXTENDED &&
        requestedSession != DCM_SESSION_PROGRAMMING)
    {
        Dcm_SendNegativeResponse(UDS_SID_SESSION_CONTROL, UDS_NRC_SUBFUNCTION_NOT_SUPPORTED);
        return;
    }

    const char *sessionName =
        (requestedSession == DCM_SESSION_DEFAULT)     ? "DEFAULT"     :
        (requestedSession == DCM_SESSION_EXTENDED)    ? "EXTENDED"    :
        (requestedSession == DCM_SESSION_PROGRAMMING) ? "PROGRAMMING" : "UNKNOWN";

    Log_Write(LOG_TAG_DCM, "Session change: %s -> %s",
              (dcm_currentSession == DCM_SESSION_DEFAULT)  ? "DEFAULT"  :
              (dcm_currentSession == DCM_SESSION_EXTENDED) ? "EXTENDED" : "PROGRAMMING",
              sessionName);

    dcm_currentSession = (Dcm_SessionType)requestedSession;

    /* Build positive response: 50 <session> 00 32 01 F4 (P2=50ms, P2*=5000ms) */
    dcm_txBuffer[0] = UDS_SID_SESSION_CONTROL | UDS_POSITIVE_RESPONSE_MASK;
    dcm_txBuffer[1] = requestedSession;
    dcm_txBuffer[2] = 0x00U;  /* P2Server_max high byte */
    dcm_txBuffer[3] = 0x32U;  /* P2Server_max low byte (50ms) */
    dcm_txBuffer[4] = 0x01U;  /* P2*Server_max high byte */
    dcm_txBuffer[5] = 0xF4U;  /* P2*Server_max low byte (5000ms x 10) */

    Dcm_SendResponse(dcm_txBuffer, 6U);
}

/**
 * @brief 0x22 — ReadDataByIdentifier
 * Request format:  22 <DID_high> <DID_low>
 * Response format: 62 <DID_high> <DID_low> <data...>
 */
static void Dcm_HandleReadDid(const uint8 *req, uint8 len)
{
    if (len != 3U)
    {
        Dcm_SendNegativeResponse(UDS_SID_READ_DID, UDS_NRC_INCORRECT_MESSAGE_LENGTH);
        return;
    }

    uint16 requestedDid = ((uint16)req[1] << 8U) | (uint16)req[2];

    /* Search DID table */
    for (uint8 i = 0U; i < DCM_DID_TABLE_SIZE; i++)
    {
        if (dcm_didTable[i].did == requestedDid)
        {
            Log_Write(LOG_TAG_DCM, "ReadDID 0x%04X (%s) - returning %u bytes",
                      (uint32)requestedDid, dcm_didTable[i].name,
                      (uint32)dcm_didTable[i].length);

            dcm_txBuffer[0] = UDS_SID_READ_DID | UDS_POSITIVE_RESPONSE_MASK;
            dcm_txBuffer[1] = req[1];
            dcm_txBuffer[2] = req[2];
            memcpy(&dcm_txBuffer[3], dcm_didTable[i].data, dcm_didTable[i].length);

            Dcm_SendResponse(dcm_txBuffer, 3U + dcm_didTable[i].length);
            return;
        }
    }

    Log_Write(LOG_TAG_DCM, "ReadDID 0x%04X - NOT SUPPORTED", (uint32)requestedDid);
    Dcm_SendNegativeResponse(UDS_SID_READ_DID, UDS_NRC_REQUEST_OUT_OF_RANGE);
}

/**
 * @brief 0x19 — ReadDTCInformation (subfunction 0x02: reportDTCByStatusMask)
 * Request format:  19 02 <statusMask>
 * Response format: 59 02 <DTCStatusAvailabilityMask> <DTC1_hi> <DTC1_mid> <DTC1_lo> <DTC1_status> ...
 */
static void Dcm_HandleReadDtc(const uint8 *req, uint8 len)
{
    if (len < 3U)
    {
        Dcm_SendNegativeResponse(UDS_SID_READ_DTC, UDS_NRC_INCORRECT_MESSAGE_LENGTH);
        return;
    }

    uint8 subfunction = req[1];

    if (subfunction != 0x02U)
    {
        Log_Write(LOG_TAG_DCM, "ReadDTC subfunction 0x%02X not supported",
                  (uint32)subfunction);
        Dcm_SendNegativeResponse(UDS_SID_READ_DTC, UDS_NRC_SUBFUNCTION_NOT_SUPPORTED);
        return;
    }

    uint8 statusMask = req[2];
    Log_Write(LOG_TAG_DCM, "ReadDTC reportByStatusMask: mask=0x%02X",
              (uint32)statusMask);

    /* Build response */
    dcm_txBuffer[0] = UDS_SID_READ_DTC | UDS_POSITIVE_RESPONSE_MASK;
    dcm_txBuffer[1] = 0x02U;                     /* subfunction echo */
    dcm_txBuffer[2] = 0xFFU;                     /* DTCStatusAvailabilityMask (all bits supported) */
    uint8 txIdx = 3U;
    uint8 dtcCount = 0U;

    /* Iterate DEM events, add those matching the status mask */
    for (uint8 evId = 0U; evId < DEM_EVENT_COUNT; evId++)
    {
        uint8 status = Dem_GetDtcStatusByte((Dem_EventIdType)evId);

        /* Match if any requested mask bit is set in status */
        if ((status & statusMask) != 0U)
        {
            /* DTC encoding: 3 bytes (24 bits) + 1 status byte */
            uint16 dtcCode = 0x4A10U + evId;     /* simple DTC numbering */
            dcm_txBuffer[txIdx++] = 0x00U;       /* DTC high byte */
            dcm_txBuffer[txIdx++] = (uint8)(dtcCode >> 8U);
            dcm_txBuffer[txIdx++] = (uint8)(dtcCode & 0xFFU);
            dcm_txBuffer[txIdx++] = status;
            dtcCount++;
        }
    }

    Log_Write(LOG_TAG_DCM, "ReadDTC: returning %u DTCs matching mask", (uint32)dtcCount);
    Dcm_SendResponse(dcm_txBuffer, txIdx);
}

/**
 * @brief 0x14 — ClearDiagnosticInformation
 * Request format:  14 <groupDTC_hi> <groupDTC_mid> <groupDTC_lo>
 *                  FF FF FF = clear all DTCs
 * Response format: 54
 */
static void Dcm_HandleClearDtc(const uint8 *req, uint8 len)
{
    if (len != 4U)
    {
        Dcm_SendNegativeResponse(UDS_SID_CLEAR_DTC, UDS_NRC_INCORRECT_MESSAGE_LENGTH);
        return;
    }

    boolean clearAll = (req[1] == 0xFFU && req[2] == 0xFFU && req[3] == 0xFFU);

    if (!clearAll)
    {
        /* Specific group clear not supported in this demo */
        Dcm_SendNegativeResponse(UDS_SID_CLEAR_DTC, UDS_NRC_REQUEST_OUT_OF_RANGE);
        return;
    }

    Log_Write(LOG_TAG_DCM, "ClearDTC: clearing all diagnostic information");

    /* Clear each DEM event */
    uint8 clearedCount = 0U;
    for (uint8 evId = 0U; evId < DEM_EVENT_COUNT; evId++)
    {
        if (Dem_ClearDtc((Dem_EventIdType)evId) == E_OK)
        {
            clearedCount++;
        }
    }

    /* Request NvM to erase stored DTCs */
    NvM_EraseBlock(NVM_BLOCK_DEM_DTCS);

    Log_Write(LOG_TAG_DCM, "ClearDTC: %u events cleared, NvM erase queued",
              (uint32)clearedCount);

    dcm_txBuffer[0] = UDS_SID_CLEAR_DTC | UDS_POSITIVE_RESPONSE_MASK;
    Dcm_SendResponse(dcm_txBuffer, 1U);
}

/* ============================================================
 * MAIN DISPATCHER
 * ============================================================ */

void Dcm_ProcessRequest(const uint8 *request, uint8 length)
{
    if (length == 0U)
    {
        return;
    }

    dcm_totalRequests++;

    uint8 sid = request[0];
    Log_Write(LOG_TAG_DCM, "Request received: SID=0x%02X, length=%u",
              (uint32)sid, (uint32)length);

    switch (sid)
    {
        case UDS_SID_SESSION_CONTROL:
            Dcm_HandleSessionControl(request, length);
            break;

        case UDS_SID_READ_DID:
            Dcm_HandleReadDid(request, length);
            break;

        case UDS_SID_READ_DTC:
            Dcm_HandleReadDtc(request, length);
            break;

        case UDS_SID_CLEAR_DTC:
            Dcm_HandleClearDtc(request, length);
            break;

        case UDS_SID_TESTER_PRESENT:
            /* Keep-alive — send positive response, no action */
            dcm_txBuffer[0] = UDS_SID_TESTER_PRESENT | UDS_POSITIVE_RESPONSE_MASK;
            dcm_txBuffer[1] = (length >= 2U) ? request[1] : 0x00U;
            Dcm_SendResponse(dcm_txBuffer, 2U);
            break;

        default:
            Log_Write(LOG_TAG_DCM, "Service 0x%02X not supported", (uint32)sid);
            Dcm_SendNegativeResponse(sid, UDS_NRC_SERVICE_NOT_SUPPORTED);
            break;
    }
}

/* ============================================================
 * UART RX — accumulate bytes until frame terminator
 * ============================================================ */

void Dcm_FeedRxByte(uint8 rxByte)
{
    /* Frame terminator: CR or LF */
    if (rxByte == '\r' || rxByte == '\n')
    {
        if (dcm_rxIndex > 0U)
        {
            /* Parse ASCII hex to binary */
            uint8 binBuffer[DCM_MAX_REQUEST_SIZE];
            uint8 binLen = Dcm_ParseAsciiRequest(dcm_rxBuffer, dcm_rxIndex,
                                                 binBuffer, sizeof(binBuffer));

            if (binLen > 0U)
            {
                Dcm_ProcessRequest(binBuffer, binLen);
            }
            else
            {
                Log_Write(LOG_TAG_DCM, "RX frame parse error (invalid hex)");
            }

            dcm_rxIndex = 0U;  /* ready for next frame */
        }
        return;
    }

    /* Ignore non-printable except space */
    if (rxByte < 0x20U) return;

    if (dcm_rxIndex < sizeof(dcm_rxBuffer) - 1U)
    {
        dcm_rxBuffer[dcm_rxIndex++] = rxByte;
    }
    else
    {
        /* Buffer overflow — discard and reset */
        dcm_rxIndex = 0U;
        Log_Write(LOG_TAG_DCM, "RX buffer overflow, frame discarded");
    }
}

/* ============================================================
 * INIT
 * ============================================================ */

void Dcm_Init(void)
{
    dcm_currentSession = DCM_SESSION_DEFAULT;
    dcm_rxIndex = 0U;
    dcm_totalRequests  = 0U;
    dcm_totalResponses = 0U;
    dcm_totalNrc       = 0U;

    memset(dcm_rxBuffer, 0, sizeof(dcm_rxBuffer));
    memset(dcm_txBuffer, 0, sizeof(dcm_txBuffer));
}

Dcm_SessionType Dcm_GetCurrentSession(void)
{
    return dcm_currentSession;
}