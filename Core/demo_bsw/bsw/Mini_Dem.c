/**
 * @file    Mini_Dem.c
 * @brief   Mini DEM Implementation — Debounce & DTC Status Management
 *
 * Demonstrates how AUTOSAR DEM processes diagnostic events:
 * 1. SWC calls Dem_SetEventStatus(eventId, PREFAILED/PREPASSED)
 * 2. DEM updates debounce counter (counter-based algorithm)
 * 3. When counter crosses threshold → event transitions to FAILED/PASSED
 * 4. On FAILED transition: capture freeze frame, update DTC status byte
 * 5. On next MainFunction: trigger NvM write (simulated here)
 */

#include "Mini_Dem.h"
#include "Mini_SchM.h"

/* ============================================================
 * DEBOUNCE CONFIGURATION
 *
 * In real AUTOSAR: configured in DaVinci per event.
 * Typical ASIL-D config: 5 consecutive fails to confirm, 5 passes to heal.
 * ============================================================ */
static const Dem_DebounceConfigType dem_debounceConfig[DEM_EVENT_COUNT] = {
    /* Event,                        TFail, TPass, StepUp, StepDown */
    [DEM_EVENT_TORQUE_SENSOR_FAULT] = {  5,   -5,      1,      -1 },
    [DEM_EVENT_VEHICLE_SPEED_FAULT] = {  5,   -5,      1,      -1 },
    [DEM_EVENT_MOTOR_OVERCURRENT]   = {  3,   -3,      1,      -1 }, /* Faster */
    [DEM_EVENT_COMMUNICATION_LOSS]  = { 10,  -10,      1,      -1 }  /* Slower */
};

/* ============================================================
 * EVENT RUNTIME DATA
 * ============================================================ */
static Dem_EventDataType dem_eventData[DEM_EVENT_COUNT];

/* ============================================================
 * FREEZE FRAME SOURCE DATA
 *
 * Updated by SWC (via Dem_SetFreezeFrameData).
 * When event transitions to FAILED, this data is captured into
 * the event's freeze frame.
 * ============================================================ */
static Dem_FreezeFrameType dem_ffSource = {0};

/* Statistics */
static uint32 dem_totalNvmWrites = 0U;

/* ============================================================
 * INTERNAL HELPERS
 * ============================================================ */

/**
 * @brief Capture current freeze frame source into event's freeze frame
 * Called when event transitions to FAILED state.
 */
static void Dem_CaptureFreezeFrame(Dem_EventIdType eventId)
{
    SchM_Enter_ExclusiveArea();
    dem_eventData[eventId].freezeFrame = dem_ffSource;
    /* TODO: In real DEM, timestamp would come from OS time service */
    dem_eventData[eventId].freezeFrame.timestamp_ms = 0U;
    SchM_Exit_ExclusiveArea();
}

/**
 * @brief Update DTC status byte when event transitions
 *
 * Per ISO 14229 (UDS) status byte bits:
 * - testFailed: event is currently failing
 * - confirmedDTC: event has been confirmed (stored in memory)
 * - testFailedSinceLastClear: failed at least once since last clear
 */
static void Dem_UpdateStatusOnFailed(Dem_EventIdType eventId)
{
    uint8 *status = &dem_eventData[eventId].statusByte;

    *status |= DEM_UDS_STATUS_TF;         /* testFailed */
    *status |= DEM_UDS_STATUS_TFTOC;      /* testFailedThisOperationCycle */
    *status |= DEM_UDS_STATUS_CDTC;       /* confirmedDTC */
    *status |= DEM_UDS_STATUS_TFSLC;      /* testFailedSinceLastClear */
    *status &= ~DEM_UDS_STATUS_TNCTOC;    /* Clear testNotCompleted */
    *status &= ~DEM_UDS_STATUS_TNCSLC;    /* Clear testNotCompleted */
}

static void Dem_UpdateStatusOnPassed(Dem_EventIdType eventId)
{
    uint8 *status = &dem_eventData[eventId].statusByte;

    *status &= ~DEM_UDS_STATUS_TF;        /* Clear testFailed */
    *status &= ~DEM_UDS_STATUS_PDTC;      /* Clear pending */
    *status &= ~DEM_UDS_STATUS_TNCTOC;    /* Test completed */
    *status &= ~DEM_UDS_STATUS_TNCSLC;    /* Test completed */
    /* Note: CDTC and TFSLC bits REMAIN — they represent historical state */
}

/* ============================================================
 * PUBLIC API
 * ============================================================ */

void Dem_Init(void)
{
    uint8 i;

    for (i = 0U; i < DEM_EVENT_COUNT; i++)
    {
        dem_eventData[i].statusByte      = DEM_UDS_STATUS_TNCSLC | DEM_UDS_STATUS_TNCTOC;
        dem_eventData[i].debounceCounter = 0;
        dem_eventData[i].isStored        = FALSE;

        dem_eventData[i].freezeFrame.torque_input   = 0U;
        dem_eventData[i].freezeFrame.vehicle_speed  = 0U;
        dem_eventData[i].freezeFrame.motor_current  = 0U;
        dem_eventData[i].freezeFrame.steering_angle = 0U;
        dem_eventData[i].freezeFrame.timestamp_ms   = 0U;
    }

    dem_ffSource.torque_input   = 0U;
    dem_ffSource.vehicle_speed  = 0U;
    dem_ffSource.motor_current  = 0U;
    dem_ffSource.steering_angle = 0U;

    dem_totalNvmWrites = 0U;
}

/**
 * @brief Report event status — main entry point for SWCs
 *
 * In real AUTOSAR: SWC calls Rte_Call_Event_SetEventStatus()
 * which routes to Dem_SetEventStatus() via Client-Server port.
 */
Std_ReturnType Dem_SetEventStatus(Dem_EventIdType eventId, Dem_EventStatusType status)
{
    if (eventId >= DEM_EVENT_COUNT)
    {
        return E_NOT_OK;
    }

    const Dem_DebounceConfigType *cfg = &dem_debounceConfig[eventId];
    Dem_EventDataType *evt = &dem_eventData[eventId];

    SchM_Enter_ExclusiveArea();

    sint16 oldCounter = evt->debounceCounter;

    /* Update debounce counter based on report */
    switch (status)
    {
        case DEM_EVENT_STATUS_PREFAILED:
            evt->debounceCounter += cfg->step_up;
            if (evt->debounceCounter > cfg->threshold_failed)
            {
                evt->debounceCounter = cfg->threshold_failed;
            }
            break;

        case DEM_EVENT_STATUS_PREPASSED:
            evt->debounceCounter += cfg->step_down;
            if (evt->debounceCounter < cfg->threshold_passed)
            {
                evt->debounceCounter = cfg->threshold_passed;
            }
            break;

        case DEM_EVENT_STATUS_FAILED:
            evt->debounceCounter = cfg->threshold_failed;
            break;

        case DEM_EVENT_STATUS_PASSED:
            evt->debounceCounter = cfg->threshold_passed;
            break;

        default:
            SchM_Exit_ExclusiveArea();
            return E_NOT_OK;
    }

    /* Check for state transitions */
    boolean transitionToFailed = (oldCounter < cfg->threshold_failed) &&
                                  (evt->debounceCounter >= cfg->threshold_failed);
    boolean transitionToPassed = (oldCounter > cfg->threshold_passed) &&
                                  (evt->debounceCounter <= cfg->threshold_passed);

    SchM_Exit_ExclusiveArea();

    /* Handle transitions (outside exclusive area — these can be long operations) */
    if (transitionToFailed)
    {
        Dem_UpdateStatusOnFailed(eventId);
        Dem_CaptureFreezeFrame(eventId);
        /* Mark for NvM write in next MainFunction */
        evt->isStored = FALSE;
    }
    else if (transitionToPassed)
    {
        Dem_UpdateStatusOnPassed(eventId);
    }

    return E_OK;
}

/**
 * @brief Get DTC status byte — used by DCM for UDS 0x19 service
 *
 * In real AUTOSAR: DCM calls this to respond to tester's
 * "Read DTC by status mask" request.
 */
uint8 Dem_GetDtcStatusByte(Dem_EventIdType eventId)
{
    if (eventId >= DEM_EVENT_COUNT)
    {
        return 0U;
    }

    uint8 status;
    SchM_Enter_ExclusiveArea();
    status = dem_eventData[eventId].statusByte;
    SchM_Exit_ExclusiveArea();
    return status;
}

/**
 * @brief Get freeze frame — used by DCM for UDS 0x19 04 service
 */
Std_ReturnType Dem_GetFreezeFrame(Dem_EventIdType eventId, Dem_FreezeFrameType *ffData)
{
    if (eventId >= DEM_EVENT_COUNT || ffData == NULL_PTR)
    {
        return E_NOT_OK;
    }

    SchM_Enter_ExclusiveArea();
    *ffData = dem_eventData[eventId].freezeFrame;
    SchM_Exit_ExclusiveArea();
    return E_OK;
}

/**
 * @brief Clear DTC — used by DCM for UDS 0x14 service
 *
 * Resets event status and debounce counter.
 * In real AUTOSAR: also triggers NvM to clear stored data.
 */
Std_ReturnType Dem_ClearDtc(Dem_EventIdType eventId)
{
    if (eventId >= DEM_EVENT_COUNT)
    {
        return E_NOT_OK;
    }

    SchM_Enter_ExclusiveArea();
    dem_eventData[eventId].statusByte      = DEM_UDS_STATUS_TNCSLC | DEM_UDS_STATUS_TNCTOC;
    dem_eventData[eventId].debounceCounter = 0;
    dem_eventData[eventId].isStored        = FALSE;
    SchM_Exit_ExclusiveArea();

    return E_OK;
}

/**
 * @brief DEM MainFunction — async event processing
 *
 * In real AUTOSAR: called by SchM at 10-100ms period.
 * Processes events marked for NvM write (async storage).
 *
 * Chain: DEM MainFunction → NvM_WriteBlock() → NvM queues request →
 *        NvM MainFunction → MemIf → Fee → Fls → flash
 * Whole chain is async — each MainFunction processes one step per cycle.
 */
void Dem_MainFunction(void)
{
    uint8 eventId;

    for (eventId = 0U; eventId < DEM_EVENT_COUNT; eventId++)
    {
        /* Check if event needs to be stored to NvM */
        SchM_Enter_ExclusiveArea();
        boolean needStore = (dem_eventData[eventId].statusByte & DEM_UDS_STATUS_CDTC) &&
                            !dem_eventData[eventId].isStored;
        SchM_Exit_ExclusiveArea();

        if (needStore)
        {
            /* TODO: In real system: NvM_WriteBlock(blockId, &dem_eventData[eventId])
             * Here we just mark it as stored and count.
             * In real NvM, this call returns E_OK immediately (queued),
             * actual flash write happens in NvM MainFunction later. */
            dem_eventData[eventId].isStored = TRUE;
            dem_totalNvmWrites++;
        }
    }
}

/**
 * @brief Update freeze frame source data
 * Called by SWC before reporting event status, so DEM captures
 * the correct data snapshot when transitioning to FAILED.
 */
void Dem_SetFreezeFrameData(uint16 torque, uint16 speed, uint16 current, uint16 angle)
{
    SchM_Enter_ExclusiveArea();
    dem_ffSource.torque_input   = torque;
    dem_ffSource.vehicle_speed  = speed;
    dem_ffSource.motor_current  = current;
    dem_ffSource.steering_angle = angle;
    SchM_Exit_ExclusiveArea();
}
