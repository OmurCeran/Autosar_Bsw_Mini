/**
 * @file    Mini_NvM.c
 * @brief   Mini NvM Implementation — Async flash simulation
 */

#include "Mini_NvM.h"
#include "Mini_Timestamp.h"
#include "Mini_SchM.h"
#include <string.h>

/* ============================================================
 * SIMULATED FLASH STORAGE
 *
 * In real system: physical flash accessed via Fee/Fls chain.
 * Here: RAM buffers that persist across the demo run.
 *
 * NOTE: Marked as static const-like regions. In real NvM, these
 * would be memory-mapped flash sectors.
 * ============================================================ */
static uint8 nvm_flash_dem_dtcs[NVM_BLOCK_SIZE_DEM_DTCS];
static uint8 nvm_flash_calibration[NVM_BLOCK_SIZE_CALIBRATION];
static uint8 nvm_flash_user_config[NVM_BLOCK_SIZE_USER_CONFIG];

/* ============================================================
 * REQUEST QUEUE — simulates NvM's internal async queue
 * ============================================================ */
typedef enum {
    NVM_OP_NONE = 0,
    NVM_OP_WRITE,
    NVM_OP_READ,
    NVM_OP_ERASE
} NvM_OperationType;

typedef struct {
    NvM_OperationType     operation;
    NvM_RequestResultType status;
    uint8                 dataBuffer[NVM_BLOCK_SIZE_DEM_DTCS];  /* largest block */
    uint8                 dataLength;
    void                 *userDstPtr;      /* for read requests */
    uint8                 processingTicks; /* simulates flash latency */
} NvM_BlockRuntimeType;

static NvM_BlockRuntimeType nvm_blocks[NVM_BLOCK_COUNT];

/* Statistics */
static uint32 nvm_restoredBlocks = 0U;
static uint32 nvm_totalWrites    = 0U;
static uint32 nvm_totalReads     = 0U;

/* Flash write latency: 3 MainFunction ticks (3 x 10ms = 30ms total) */
#define NVM_FLASH_WRITE_TICKS   (3U)
#define NVM_FLASH_READ_TICKS    (1U)

/* ============================================================
 * INTERNAL HELPERS
 * ============================================================ */

static uint8* NvM_GetFlashPtr(NvM_BlockIdType blockId, uint8 *length)
{
    switch (blockId)
    {
        case NVM_BLOCK_DEM_DTCS:
            *length = NVM_BLOCK_SIZE_DEM_DTCS;
            return nvm_flash_dem_dtcs;
        case NVM_BLOCK_CALIBRATION:
            *length = NVM_BLOCK_SIZE_CALIBRATION;
            return nvm_flash_calibration;
        case NVM_BLOCK_USER_CONFIG:
            *length = NVM_BLOCK_SIZE_USER_CONFIG;
            return nvm_flash_user_config;
        default:
            *length = 0U;
            return NULL_PTR;
    }
}

static const char* NvM_BlockName(NvM_BlockIdType blockId)
{
    static const char *names[] = {
        [NVM_BLOCK_DEM_DTCS]     = "DEM_DTCS",
        [NVM_BLOCK_CALIBRATION]  = "CALIBRATION",
        [NVM_BLOCK_USER_CONFIG]  = "USER_CONFIG"
    };
    if (blockId < NVM_BLOCK_COUNT) return names[blockId];
    return "UNKNOWN";
}

/* ============================================================
 * PUBLIC API
 * ============================================================ */

void NvM_Init(void)
{
    /* Zero-initialize simulated flash (first boot only) */
    memset(nvm_flash_dem_dtcs,    0, NVM_BLOCK_SIZE_DEM_DTCS);
    memset(nvm_flash_calibration, 0, NVM_BLOCK_SIZE_CALIBRATION);
    memset(nvm_flash_user_config, 0, NVM_BLOCK_SIZE_USER_CONFIG);

    /* Clear request queue */
    memset(nvm_blocks, 0, sizeof(nvm_blocks));

    nvm_restoredBlocks = 0U;
    nvm_totalWrites    = 0U;
    nvm_totalReads     = 0U;
}

void NvM_ReadAll(void)
{
    /* In real NvM_ReadAll: reads all blocks from flash to RAM at startup.
     * Runs BEFORE scheduler starts, so can be synchronous (blocking).
     *
     * Here: we count "restored" blocks. Since this is first boot,
     * no real data exists yet — but the mechanism is shown.
     */
    uint8 i;
    for (i = 0U; i < NVM_BLOCK_COUNT; i++)
    {
        uint8 length;
        uint8 *flashPtr = NvM_GetFlashPtr((NvM_BlockIdType)i, &length);

        if (flashPtr != NULL_PTR)
        {
            /* Check if block has valid data (simplified: non-zero means valid) */
            uint8 hasData = 0U;
            for (uint8 j = 0; j < length; j++)
            {
                if (flashPtr[j] != 0U) { hasData = 1U; break; }
            }

            if (hasData)
            {
                nvm_restoredBlocks++;
                Log_Write(LOG_TAG_NVM, "Block %s restored from flash",
                          NvM_BlockName((NvM_BlockIdType)i));
            }
        }
    }
}

uint32 NvM_GetRestoredBlockCount(void)
{
    return nvm_restoredBlocks;
}

Std_ReturnType NvM_WriteBlock(NvM_BlockIdType blockId, const void *srcData)
{
    if (blockId >= NVM_BLOCK_COUNT || srcData == NULL_PTR)
    {
        return E_NOT_OK;
    }

    SchM_Enter_ExclusiveArea();

    /* Check if already busy */
    if (nvm_blocks[blockId].status == NVM_REQ_IN_PROGRESS)
    {
        SchM_Exit_ExclusiveArea();
        Log_Write(LOG_TAG_NVM, "Block %s write REJECTED - busy",
                  NvM_BlockName(blockId));
        return E_NOT_OK;
    }

    /* Copy data to internal buffer */
    uint8 length;
    (void)NvM_GetFlashPtr(blockId, &length);
    memcpy(nvm_blocks[blockId].dataBuffer, srcData, length);
    nvm_blocks[blockId].dataLength      = length;
    nvm_blocks[blockId].operation       = NVM_OP_WRITE;
    nvm_blocks[blockId].status          = NVM_REQ_PENDING;
    nvm_blocks[blockId].processingTicks = 0U;

    SchM_Exit_ExclusiveArea();

    Log_Write(LOG_TAG_NVM, "Block %s write QUEUED (async, %lu bytes)",
              NvM_BlockName(blockId), (uint32)length);

    return E_OK;
}

Std_ReturnType NvM_ReadBlock(NvM_BlockIdType blockId, void *dstData)
{
    if (blockId >= NVM_BLOCK_COUNT || dstData == NULL_PTR)
    {
        return E_NOT_OK;
    }

    SchM_Enter_ExclusiveArea();
    nvm_blocks[blockId].operation       = NVM_OP_READ;
    nvm_blocks[blockId].status          = NVM_REQ_PENDING;
    nvm_blocks[blockId].userDstPtr      = dstData;
    nvm_blocks[blockId].processingTicks = 0U;
    SchM_Exit_ExclusiveArea();

    return E_OK;
}

NvM_RequestResultType NvM_GetRequestStatus(NvM_BlockIdType blockId)
{
    if (blockId >= NVM_BLOCK_COUNT) return NVM_REQ_FAILED;
    return nvm_blocks[blockId].status;
}

Std_ReturnType NvM_EraseBlock(NvM_BlockIdType blockId)
{
    if (blockId >= NVM_BLOCK_COUNT) return E_NOT_OK;

    SchM_Enter_ExclusiveArea();
    nvm_blocks[blockId].operation       = NVM_OP_ERASE;
    nvm_blocks[blockId].status          = NVM_REQ_PENDING;
    nvm_blocks[blockId].processingTicks = 0U;
    SchM_Exit_ExclusiveArea();

    Log_Write(LOG_TAG_NVM, "Block %s erase QUEUED", NvM_BlockName(blockId));
    return E_OK;
}

/**
 * @brief NvM_MainFunction — processes one queued operation.
 *
 * Simulates async flash latency by spreading a single write across
 * multiple MainFunction calls. This is what prevents CPU stall —
 * the OS keeps running, 1ms task keeps running, only NvM is "busy"
 * in the background.
 *
 * Called every 10ms by SchM_MainFunction_100ms (via SchM chain).
 */
void NvM_MainFunction(void)
{
    uint8 i;
    for (i = 0U; i < NVM_BLOCK_COUNT; i++)
    {
        NvM_BlockRuntimeType *blk = &nvm_blocks[i];

        if (blk->status == NVM_REQ_PENDING)
        {
            /* Pick up the pending request */
            blk->status = NVM_REQ_IN_PROGRESS;
            blk->processingTicks = 0U;
            Log_Write(LOG_TAG_NVM, "Block %s processing STARTED (op=%s)",
                      NvM_BlockName((NvM_BlockIdType)i),
                      (blk->operation == NVM_OP_WRITE) ? "WRITE" :
                      (blk->operation == NVM_OP_READ)  ? "READ"  :
                      (blk->operation == NVM_OP_ERASE) ? "ERASE" : "UNKNOWN");
            continue;  /* process one per MainFunction cycle */
        }

        if (blk->status == NVM_REQ_IN_PROGRESS)
        {
            blk->processingTicks++;

            uint8 requiredTicks = (blk->operation == NVM_OP_WRITE) ?
                                   NVM_FLASH_WRITE_TICKS : NVM_FLASH_READ_TICKS;

            if (blk->processingTicks >= requiredTicks)
            {
                /* Flash operation complete */
                uint8 length;
                uint8 *flashPtr = NvM_GetFlashPtr((NvM_BlockIdType)i, &length);

                if (flashPtr != NULL_PTR)
                {
                    switch (blk->operation)
                    {
                        case NVM_OP_WRITE:
                            memcpy(flashPtr, blk->dataBuffer, length);
                            nvm_totalWrites++;
                            Log_Write(LOG_TAG_NVM,
                                "Block %s write COMPLETE (flash latency %ums)",
                                NvM_BlockName((NvM_BlockIdType)i),
                                (uint32)(requiredTicks * 10U));
                            break;

                        case NVM_OP_READ:
                            if (blk->userDstPtr != NULL_PTR)
                            {
                                memcpy(blk->userDstPtr, flashPtr, length);
                            }
                            nvm_totalReads++;
                            Log_Write(LOG_TAG_NVM, "Block %s read COMPLETE",
                                      NvM_BlockName((NvM_BlockIdType)i));
                            break;

                        case NVM_OP_ERASE:
                            memset(flashPtr, 0, length);
                            Log_Write(LOG_TAG_NVM, "Block %s erase COMPLETE",
                                      NvM_BlockName((NvM_BlockIdType)i));
                            break;

                        default:
                            break;
                    }
                }

                blk->status    = NVM_REQ_OK;
                blk->operation = NVM_OP_NONE;
            }
            /* else: still in progress, wait for next MainFunction */
            return;  /* Only process one block per MainFunction tick */
        }
    }
}