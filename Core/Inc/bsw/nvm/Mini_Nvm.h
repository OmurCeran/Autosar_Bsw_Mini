/**
 * @file    Mini_NvM.h
 * @brief   Mini Non-Volatile Memory Manager
 *
 * Maps to real AUTOSAR: NvM + MemIf + Fee + Fls chain (simplified)
 *
 * In real AUTOSAR, the chain is:
 *   DEM -> NvM_WriteBlock() [async, queues request, returns immediately]
 *   NvM MainFunction -> picks up queued request
 *   NvM -> MemIf -> Fee -> Fls -> physical flash
 *   Flash write takes ~10-30ms, during which NvM is BUSY
 *   When done, notification callback fires
 *
 * Here we simulate:
 *   - Async request queuing
 *   - MainFunction-based processing
 *   - Simulated flash write latency (30ms)
 *   - RAM-based "flash" buffer (loses data on reset, but demo doesn't care)
 *
 * The ASYNC behavior is the important part — this is what prevents
 * CPU stall during long flash operations in safety-critical systems.
 */

#ifndef MINI_NVM_H
#define MINI_NVM_H

#include "Std_Types.h"

/* --- Block IDs --- */
/* Each BSW module that needs persistent storage gets a block ID.
 * In real AUTOSAR: configured in DaVinci NvM_BlockDescriptor */
typedef enum {
    NVM_BLOCK_DEM_DTCS = 0,      /* DEM stores DTC status + freeze frames */
    NVM_BLOCK_CALIBRATION,        /* Calibration parameters */
    NVM_BLOCK_USER_CONFIG,        /* User configuration */
    NVM_BLOCK_COUNT
} NvM_BlockIdType;

/* --- Request Status --- */
typedef enum {
    NVM_REQ_OK = 0,
    NVM_REQ_PENDING,             /* Queued but not processed yet */
    NVM_REQ_IN_PROGRESS,         /* MainFunction working on it */
    NVM_REQ_FAILED
} NvM_RequestResultType;

/* --- Block Size --- */
#define NVM_BLOCK_SIZE_DEM_DTCS      (64U)
#define NVM_BLOCK_SIZE_CALIBRATION   (32U)
#define NVM_BLOCK_SIZE_USER_CONFIG   (32U)

/* --- Public API --- */

void NvM_Init(void);

/**
 * @brief Read all blocks from flash at startup (EcuM calls this)
 * In real AUTOSAR: NvM_ReadAll()
 */
void NvM_ReadAll(void);

/**
 * @brief Get count of successfully restored blocks
 */
uint32 NvM_GetRestoredBlockCount(void);

/**
 * @brief Queue an async write request
 *
 * Returns IMMEDIATELY after queuing. Actual write happens in
 * NvM_MainFunction later. Use NvM_GetRequestStatus() to check.
 *
 * In real AUTOSAR: Std_ReturnType NvM_WriteBlock(BlockId, SrcPtr)
 */
Std_ReturnType NvM_WriteBlock(NvM_BlockIdType blockId, const void *srcData);

/**
 * @brief Queue an async read request (for specific block)
 */
Std_ReturnType NvM_ReadBlock(NvM_BlockIdType blockId, void *dstData);

/**
 * @brief Get the status of the last request for a block
 */
NvM_RequestResultType NvM_GetRequestStatus(NvM_BlockIdType blockId);

/**
 * @brief MainFunction — called periodically by SchM.
 * Processes one queued request per call.
 *
 * Simulates the async chain by splitting flash write across multiple
 * MainFunction calls (30ms total latency = 3 x 10ms MainFunction calls).
 */
void NvM_MainFunction(void);

/**
 * @brief Erase a block (used by UDS 0x14 ClearDiagnosticInformation)
 */
Std_ReturnType NvM_EraseBlock(NvM_BlockIdType blockId);

#endif /* MINI_NVM_H */