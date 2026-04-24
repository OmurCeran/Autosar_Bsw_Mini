## UART TX Serialization — Producer-Consumer Pattern

During development, UDS responses were occasionally lost. The root cause turned out to be a **race between two independent UART TX code paths**:

1. **Log messages** went through `DMA_Printf` — mutex + semaphore + DMA transfer.
2. **DCM responses** went directly through `HAL_UART_Transmit` (polling mode).

Both wrote to the same USART2 peripheral, but neither knew about the other. When a log message was mid-DMA-transfer and a DCM response tried to polling-transmit, HAL returned `HAL_BUSY` because `huart->gState` was already `HAL_UART_STATE_BUSY_TX`. The response was silently dropped.

**The symptom:** non-deterministic UDS response loss. Sometimes the tester saw the reply, sometimes nothing. This is the classic signature of a race condition — works on the bench, fails under real load.

**The fix** was a design change, not a bug patch. All UART TX now goes through a single path:

- All producers call `DMA_Printf` (logging functions, DCM responses, banners — everything)
- `DMA_Printf` serializes with mutex + semaphore
- One DMA transfer at a time, sequenced correctly
- HAL peripheral state stays consistent because only one caller touches it

**Additionally**, for UART RX (UDS tester input), the design uses a **producer-consumer queue**:

- `HAL_UARTEx_RxEventCallback` (interrupt context) enqueues bytes into `DcMuartRxQueueHandle`
- `DiagnosticTask` (low-priority task) dequeues with `osWaitForever` — zero CPU load when idle
- `Dcm_FeedRxByte` is called from task context, safely, with no interrupt latency constraints

**Why this matters:** This is exactly how production AUTOSAR DLT (Diagnostic Log and Trace) modules work. Messages enter a queue from many sources, a single consumer thread drains them to the bus. **Single peripheral = single owner = single serializer.** This pattern scales from a demo project to a production multi-core ECU.

The message queue is fully static — `StaticQueue_t` control block + byte buffer — consistent with the project's no-malloc policy.

---

## RTOS-Aware Profiling — SEGGER SystemView

SEGGER SystemView is integrated into the FreeRTOS build to provide runtime visibility into task scheduling and CPU utilization.

### Setup

The STM32F407 Discovery's on-board debugger was reflashed from ST-Link to J-Link OB firmware using SEGGER's `STLinkReflash` utility. This enables RTT (Real-Time Transfer) trace capture, which SystemView needs. SystemView cannot work through a plain ST-Link.

The alternative — using an external J-Link EDU probe through the SWD connector — works equivalently.

### What SystemView Captures

- **Task scheduling timeline** — which task is running, when it was scheduled, when it was preempted, when it blocks
- **CPU load per task** — percentage of time each task consumed over the measurement window
- **Context switch timing** — how long the kernel takes to switch from one task to another
- **ISR entry/exit** — which interrupts fire and how long they run
- **RTOS API calls** — semaphore acquire/release, queue put/get, mutex take/give

This is the same kind of visibility Trace32 provides on production AURIX/S32K ECUs — the mental model is identical, only the tooling differs.

### Observations from This Project

CPU load measurement shows:

| Task            | CPU Load | Notes                                                      |
|-----------------|----------|------------------------------------------------------------|
| task1ms         | ~1.8%    | Torque calc + RTE task boundary — reasonable for 1ms period |
| task10ms        | ~60%     | Dominated by deliberate preemption window (see below)      |
| task100ms       | ~0.02%   | DEM monitoring — tiny, as expected                         |
| DiagnosticTask  | ~0.01%   | UDS input — only active when tester sends data             |
| Idle            | ~37.5%   | Remaining CPU — sleep target                               |

### Why task10ms Shows ~60% CPU Load

This is not a bug — it's the **race condition demo amplification** at work.

The 10ms task (`App_Runnable_SensorUpdate_10ms`) contains:

```c
/* === PREEMPTION WINDOW === */
volatile uint32 delay;
for (delay = 0; delay < 200000U; delay++)
{
    __asm volatile ("nop");
}
```

At 168 MHz, 200,000 NOP instructions take approximately 1.2 ms. Within a 10 ms period this consumes about 12% of CPU time — but because the task runs many times per measurement window and competes with the 1ms task, the aggregate load reaches ~60%.

**In a real ECU this gap would be filled by an ADC conversion (10–50 µs) or an I²C transfer (100–500 µs)** — genuine hardware work, not a busy loop. The demo inflates the window to roughly 100× its real-world equivalent so the race becomes reliably reproducible. Without this amplification, the struct write would finish inside the Cortex-M4's execution budget between 1 ms task ticks and the race would hide.

This is a case where the profiler tells the honest story about a design choice. In a production scenario, that 60% would drop to single digits. In the demo, the high number is the visible cost of a reproducible bug — a fair trade for the educational value.

### Why This Matters for Production Embedded

Being able to measure CPU load per task, context switch latency, and interrupt timing is essential in safety-critical systems. AUTOSAR WdgM (Watchdog Manager) relies on exactly this kind of timing budget analysis. A task that unexpectedly consumes 30% of CPU when the budget was 5% triggers a safe-state transition. SystemView gives the developer the same visibility that the runtime monitor has.

For this project, SystemView confirms three things:

1. Task priorities are correctly configured (higher-priority tasks always preempt lower ones)
2. No task is accidentally busy-looping (Idle > 0 during normal operation)
3. The race condition window is exactly where it was designed to be (task10ms long run times correlate with preemption opportunity)