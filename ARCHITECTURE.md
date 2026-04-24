# ARCHITECTURE.md — Design Decisions and AUTOSAR Mapping

This document explains **why** each module in `Autosar_Bsw_Mini` is designed the way it is, and **what it corresponds to in a real-world AUTOSAR Classic architecture**.

Use this as a technical reference to understand the engineering trade-offs and mechanisms simulated by the project. For a quick project overview and build/debug instructions, see [`README.md`](./README.md).

---

## Overall Approach

This project is **not** a 1:1 replica of a production-grade AUTOSAR Classic Basic Software (BSW) stack — it is a **conceptual simulation**. The primary goals are:

1. **Understand the core mechanisms** — Strip AUTOSAR's extreme complexity down to readable, fundamental mechanisms (e.g., how RTE implicit communication actually prevents race conditions, how DEM manages DTC status bytes, how DCM dispatches UDS services).
2. **Demonstrate tangible engineering problems** — Implement hardware-level scenarios with working, executable code: an EPS race condition, a DTC lifecycle with async NvM write, a UDS tester session.
3. **Teach through code and comments** — Source code is documented not only to explain *what* a function does, but *why* it exists and *what its real AUTOSAR equivalent is*.

---

## Target Platform Rationale

The project runs on **STM32F407VG Discovery + FreeRTOS**, built with CMake and debugged via VS Code's Cortex-Debug extension (no OpenOCD).

**Why this stack?**

- **STM32F407** — ARM Cortex-M4F with FPU, common and cheap, widely used in automotive prototyping.
- **FreeRTOS via CMSIS-RTOS v2** — Conceptually closest open-source analogue to AUTOSAR OS: priority-based preemptive scheduling, static task priorities, no POSIX abstraction. CMSIS-RTOS v2 provides a portable RTOS API layer similar to how AUTOSAR OS presents a standardized interface on top of different kernels.
- **Fully static allocation** — no `pvPortMalloc`, no heap usage. Matches ASIL-D constraints (see below).
- **CMake + CMakePresets** — Modern, toolchain-agnostic build. Mirrors how automotive tiered suppliers are moving away from vendor-locked IDE project files.
- **Cortex-Debug + ST-Link direct** — Avoids OpenOCD configuration overhead. Uses the on-board ST-Link probe through the extension's native GDB server integration. Leaner and more reproducible.
- **STM32CubeMX** (`.ioc` kept in repo) — Generates HAL init, FreeRTOS scaffolding, DMA/USART configuration. Keeping the `.ioc` in version control means anyone can regenerate the HAL layer from a known-good configuration.

---

## Memory Allocation — ASIL-D Alignment

One of the most important architectural decisions: **no dynamic memory allocation anywhere.**

Default CMSIS-RTOS v2 usage allocates task TCBs and stacks via `pvPortMalloc` — this is non-deterministic, can fragment, and can fail at runtime. ISO 26262 ASIL-D explicitly restricts such behavior. MISRA-C:2012 Rule 21.3 and AUTOSAR C++14 Guideline A18-5-1 prohibit stdlib allocation.

**Configuration:**
```c
#define configSUPPORT_STATIC_ALLOCATION   1
#define configCHECK_FOR_STACK_OVERFLOW    2   /* full stack pattern check */
#define configUSE_MALLOC_FAILED_HOOK      1   /* trap any malloc usage */
```

Every task, mutex, and semaphore is declared with statically-allocated control blocks and buffers. The FreeRTOS kernel also requires `vApplicationGetIdleTaskMemory` and `vApplicationGetTimerTaskMemory` hooks — these supply static buffers for its internal idle and timer tasks.

**What this gives us:**
- Predictable memory layout at build time
- No heap fragmentation
- Deterministic task creation (can't fail at runtime)
- Static analysis tools can verify stack usage
- Mirrors how AUTOSAR OS generators produce code: everything declared from ARXML

**Trade-offs:**
- Can't create tasks dynamically — but production safety-critical systems never should anyway
- Slightly more verbose declarations — but eliminates an entire class of runtime bugs

---

## Non-Blocking UART Design — DMA Over Polling/IRQ

Both UART directions use DMA, following production safety-critical design patterns.

### TX — Application logging + UDS responses

- `HAL_UART_Transmit_DMA` with mutex + semaphore coordination
- Mutex protects the shared TX buffer (multiple tasks can log)
- Semaphore blocks the caller until the previous DMA transfer completes
- CMSIS-RTOS v2 timeouts on both (50ms) prevent deadlock

**Why this matters in a safety-critical context:**
- The 1ms task must never block. Blocking `HAL_UART_Transmit` could stall the CPU for milliseconds at 115200 baud, violating ASIL-D deadline monitoring.
- Interrupt-per-byte transmission generates hundreds of interrupts per message, each stealing cycles from the 1ms task and causing jitter.
- DMA offloads the entire transfer to hardware. The CPU configures once, then the DMA controller ships bytes autonomously. The task returns immediately.

### RX — UDS tester input

- `HAL_UARTEx_ReceiveToIdle_DMA` with circular buffer
- IDLE-line interrupt signals "frame complete"
- No per-byte interrupts — CPU free for real-time tasks

**Frame assembly pattern:**
1. DMA continuously fills a circular buffer with incoming bytes — zero CPU involvement
2. When the UART line goes idle (gap between frames), hardware triggers an IDLE interrupt
3. `HAL_UARTEx_RxEventCallback` fires with the number of bytes received since the last callback
4. We compute which part of the buffer is new data (handling wrap-around) and feed each byte to `Dcm_FeedRxByte`
5. `Dcm_FeedRxByte` accumulates the ASCII hex bytes until it sees CR/LF, then parses and dispatches

**Why this matters:**
- UDS frames arrive as bursts (tester types and hits Enter)
- IDLE detection gives a clean "frame boundary" signal
- DMA never drops bytes even under heavy CPU load
- This mirrors how AUTOSAR CAN drivers work — DMA transfers frames autonomously, the controller signals completion, the stack processes

This pattern (DMA + IDLE) is standard practice in production STM32 UART drivers for safety-critical systems.

---

## HardFault Analysis

The project includes a full hard fault handler that dumps the Cortex-M4 exception stack frame over polling UART (safe during fault, IRQs disabled):

```
!!! HARD FAULT !!!
R0    = 0x00000000
R1    = 0x20001C40
...
LR    = 0x080012A3
PC    = 0x08001458   ← address of the crashing instruction
PSR   = 0x21000000
CFSR  = 0x00020000   ← why it crashed (Configurable Fault Status)
HFSR  = 0x40000000
MMFAR = 0xE000ED34
BFAR  = 0xE000ED38
```

The PC value is then fed to `arm-none-eabi-addr2line -e build/Debug/Autosar_Bsw_Mini.elf -f <PC>` to locate the exact source file and line number where the fault occurred.

This is the same post-mortem crash analysis workflow used on production ECUs with Trace32. Real AUTOSAR platforms log exception context to non-volatile memory for later analysis. We do the same — just write it to UART instead of NvM for demo visibility.

One real crash scenario caught during development: a task stack overflowed and corrupted FreeRTOS's internal ready-list. The hard fault handler showed `PC` pointing inside `vListInsertEnd` — impossible to reach that address unless the list was already corrupted. Traced the overflow, enlarged the stack, moved to static allocation with `configCHECK_FOR_STACK_OVERFLOW = 2`. This is exactly the kind of sinister memory corruption bug that static allocation and runtime stack guards prevent.

---

## Module-by-Module Design Notes

### 1. Mini_Rte — The Core of the Project

**Why is this the most important module?**

The EPS race condition scenario lives here. Both the root cause and the architectural solution of the race condition are implemented in this file.

**Two supported modes:**

- **`RTE_ACCESS_EXPLICIT`** — Software Components (SWCs) read/write directly from/to the global buffer. Preemption during a multi-field write leads to a torn read (the race condition).
- **`RTE_ACCESS_IMPLICIT`** — A snapshot (local copy) of the shared data is taken at the start of the task (`Rte_Task_Begin`), and outputs are flushed atomically at the end of the task (`Rte_Task_End`). Data stays strictly consistent for the entire task execution.

**In real AUTOSAR:**
- The choice is made at port level in DaVinci Configurator (via the `dataElement.isReentrant` attribute on `SenderReceiverInterface`)
- The RTE generator produces entirely different C macros for each mode
- The mode is fixed at compile time — it cannot be changed at runtime

**In this demo:**
- The mode can be switched at runtime, purely for educational visibility
- An inconsistency counter proves the race condition is real, not theoretical
- Explicit mode produces thousands of inconsistencies per 5-second window; implicit mode produces zero. The contrast is immediate and deterministic

---

### 2. Mini_SchM — Exclusive Areas and MainFunction Dispatch

**Critical design rule:** Exclusive area duration must be **MINIMAL**.

This constraint is documented throughout the code:

> *"Keep the code between Enter and Exit as SHORT as possible. Long exclusive areas cause timing violations in safety-critical systems. In EPS: max exclusive area duration ~ 5–10 microseconds."*

The constraint comes directly from real EPS safety requirements. Disabling interrupts for too long causes the 1ms safety task to miss its deadline, which triggers WdgM (Watchdog Manager) deadline monitoring and forces the MCU into a safe state.

**Why not a FreeRTOS mutex for BSW exclusive areas?**

AUTOSAR OS does not use task-level blocking mutexes for BSW data protection. Shared BSW data is protected via SchM Exclusive Areas (which map to hardware interrupt disabling) or OS Resources (Priority Ceiling Protocol). For extremely short critical sections — such as copying a struct of 20 bytes — exclusive areas are deterministic and much faster than mutex acquisition, which implies RTOS context-switch overhead.

In this project, exclusive areas are implemented using CMSIS-RTOS v2 critical section macros (which wrap FreeRTOS `taskENTER_CRITICAL` → hardware interrupt disable).

**MainFunction dispatch** is simpler: `SchM_MainFunction_10ms` calls `Com_MainFunctionTx`, `SchM_MainFunction_100ms` calls `Dem_MainFunction` and `NvM_MainFunction`. In real AUTOSAR, SchM is a code generator output that wires MainFunction calls to OS tasks according to the `.arxml` timing configuration. Here we do it by hand, but the pattern is identical.

---

### 3. Mini_EcuM — Lifecycle Orchestration

Real AUTOSAR EcuM handles startup, run, shutdown, sleep, and wake-up states, plus callout hooks at each transition. This project implements a minimal subset:

- **STARTUP state** — entered on power-on, drives BSW initialization
- **RUN state** — entered after scheduler starts

The startup sequence is explicit and ordered:
1. `EcuM_Init` — EcuM itself
2. `SchM_Init` — must come first because other modules use exclusive areas
3. `NvM_Init` + `NvM_ReadAll` — restore persistent data
4. `Rte_Init` — data buffers ready
5. `Com_Init` — communication infrastructure
6. `Dem_Init` — diagnostic event manager
7. `Dcm_Init` — diagnostic service dispatcher
8. `App_Swc_Init` — application components
9. `FaultInj_Init` — demo-specific fault injection system

**Why order matters:** DEM needs NvM to read stored DTCs on startup. COM needs PduR (we simulate this internally). DCM needs DEM for DTC queries. Getting the order wrong causes use-before-init bugs.

In real AUTOSAR this ordering is enforced by generator tools reading the BSW dependency graph from ARXML. Here we hand-code the sequence but document the reasoning at each step.

---

### 4. Mini_Com — Signal Packing & Endianness

**Focus:** bit-level payload packing and endianness handling.

Motorola (Big-Endian) byte order is the standard in automotive CAN networks. The code supports both Big-Endian and Little-Endian ordering, selectable from the configuration table.

**Simplifications made for the demo:**
- Only 16-bit byte-aligned signals are implemented (real COM supports arbitrary bit-lengths across byte boundaries)
- `PduR` (PDU Router) and `CanIf` (CAN Interface) are abstracted and simulated inside COM — the demo outputs via DMA UART instead of a real CAN bus
- Transmission mode is `Periodic` only (real COM also supports Direct, Mixed, and None)

**Key concepts demonstrated:**
- Signal-to-PDU mapping (`startBit`, `bitLength`, `endianness`)
- MainFunction-triggered transmission — a PDU is sent on the 10ms cycle, not instantly when a SWC writes a signal. This decouples signal production from bus scheduling and matches real AUTOSAR timing behavior.

---

### 5. Mini_Dem — Debounce, DTC Status, Freeze Frame

**Counter-based debounce algorithm:**
- `PREFAILED` report: counter += `step_up`
- `PREPASSED` report: counter += `step_down` (negative value)
- When the counter reaches `threshold_failed` → **FAILED** status transition
- When the counter drops to `threshold_passed` → **PASSED** status transition

**ISO 14229 DTC Status Byte (8 bits):**
The module simulates the standard UDS status byte. Each bit has a distinct meaning:

| Bit | Name   | Meaning                                      |
|-----|--------|----------------------------------------------|
| 0   | TF     | testFailed — fault is currently active       |
| 1   | TFTOC  | testFailedThisOperationCycle                 |
| 2   | PDTC   | pendingDTC                                   |
| 3   | CDTC   | confirmedDTC — matured and stored in NV      |
| 4   | TNCSLC | testNotCompletedSinceLastClear               |
| 5   | TFSLC  | testFailedSinceLastClear                     |
| 6   | TNCTOC | testNotCompletedThisOperationCycle           |
| 7   | WIR    | warningIndicatorRequested — MIL lamp         |

**Freeze frame mechanism:**
- The SWC continuously updates the freeze frame source data via `Dem_SetFreezeFrameData`
- When an event transitions to FAILED, the current source data is captured into that event's freeze frame
- A UDS tester can later read this via service `0x19 04` (implemented as a simpler `0x19 02` subfunction in this project)

**Asynchronous NvM write (delayed-write pattern):**
- `Dem_SetEventStatus` returns immediately — it does not touch flash
- In `Dem_MainFunction`, the `isStored` flag triggers an `NvM_WriteBlock` request
- NvM's own MainFunction asynchronously pushes the data to simulated flash
- This prevents the CPU from stalling during multi-millisecond flash write cycles — a critical property in real-time systems

**Smart logging — no spam:**
The `Dem_SetEventStatus` function only logs when the debounce counter **actually moves**. Continuous PREPASSED reporting during normal operation is silent. Counter progression is logged (1/5, 2/5, ..., 5/5) during fault injection. State transitions (FAILED, HEALED) get banner-style logs with full DTC status byte decomposition. This produces clean, event-driven output instead of terminal flooding.

---

### 6. Mini_NvM — Async Flash Simulation

In real AUTOSAR, the memory chain is:
```
DEM → NvM_WriteBlock()          [async, queues request, returns immediately]
NvM MainFunction                [picks up queued request]
NvM → MemIf → Fee → Fls         [flash driver layers]
Flash write                     [10-30ms hardware operation]
```

**What this project simulates:**
- Async request queuing in `NvM_WriteBlock`
- MainFunction-based processing
- Multi-tick flash write latency (3 × 10ms = 30ms for a write, simulating real hardware)
- RAM-based "flash" buffer (persists across the demo run; doesn't survive reset, but the demo doesn't need that)

**Three operations supported:**
- `NvM_WriteBlock` — queued, async, MainFunction completes it
- `NvM_ReadBlock` — faster (1 tick) since we can read without erase cycles
- `NvM_EraseBlock` — called by UDS `0x14` ClearDiagnosticInformation

**Why the async behavior matters:**
A synchronous flash write would halt the CPU for 30 milliseconds — enough for the 1ms safety task to miss 30 consecutive deadlines. That would trigger WdgM and force a safe state. By making writes async, the rest of the system keeps running — only NvM is "busy" in the background. This is how production safety-critical ECUs must be designed.

---

### 7. Mini_Dcm — UDS Service Dispatcher

DCM implements the diagnostic communication layer.

**Real AUTOSAR DCM has three internal layers:**
- **DSD** (Diagnostic Service Dispatcher) — routes requests by SID
- **DSL** (Diagnostic Session Layer) — manages sessions, timers, S3Server timeout, security state
- **DSP** (Diagnostic Service Processor) — individual service implementations

**This project collapses them into a single module** with the following structure:

**Reception path:**
```
UART RX (DMA + IDLE)
  → HAL_UARTEx_RxEventCallback
  → Dcm_FeedRxByte         [accumulates ASCII hex until CR/LF]
  → Dcm_ParseAsciiRequest  [ASCII → binary]
  → Dcm_ProcessRequest     [SID dispatch]
  → Service handler        [e.g., Dcm_HandleReadDid]
  → Dcm_SendResponse       [binary → ASCII + UART TX]
```

**Why ASCII hex over UART (not binary over CAN)?**
- No ISO-TP implementation needed (single-frame only anyway)
- Human-readable in a terminal — anyone can type `22 F1 90` and see the VIN
- Debug-friendly — you can see both sides of the conversation
- Direct translation to real CAN frames is trivial (replace UART TX with `Can_Write`)

**Supported services:**

| SID  | Service                         | Real AUTOSAR mapping |
|------|--------------------------------|----------------------|
| 0x10 | DiagnosticSessionControl       | DSL session transitions |
| 0x22 | ReadDataByIdentifier           | DSP DID handlers |
| 0x19 | ReadDTCInformation (02 only)   | DEM query via DCM_DEM interface |
| 0x14 | ClearDiagnosticInformation     | DEM clear via DCM_DEM interface |
| 0x3E | TesterPresent                  | DSL keep-alive |

**DID table** is a const array — mirrors how DaVinci generates DCM configuration from ARXML:

```c
static const Dcm_DidEntryType dcm_didTable[] = {
    { 0xF190U, 17U, dcm_did_F190_vin,    "VIN"             },
    { 0xF18CU, 10U, dcm_did_F18C_serial, "ECU_SERIAL_NUM"  },
    { 0xF195U,  4U, dcm_did_F195_swver,  "SW_VERSION"      },
    { 0xF191U,  4U, dcm_did_F191_hwver,  "HW_VERSION"      }
};
```

**Negative Response Codes** follow ISO 14229:
- `0x11` Service Not Supported — unknown SID
- `0x12` Subfunction Not Supported — unsupported subfunction (e.g., `19 01`)
- `0x13` Incorrect Message Length — wrong byte count for this SID
- `0x31` Request Out Of Range — unknown DID or invalid parameter

**What's NOT implemented:**
- Session timeout (S3Server) — a real ECU falls back to Default session after 5 seconds of silence
- Security access (0x27) — no seed/key exchange
- Response pending (NRC 0x78) — all handlers respond synchronously
- Multi-frame messaging (ISO-TP segmentation) — frames > 7 bytes would need segmentation in real CAN

These are intentional scope limits.

---

### 8. App_Swc — Runnable Simulation

**Three runnables mapped to three RTOS tasks:**

| Task          | Priority       | Runnable                             | Role                                              |
|---------------|---------------|--------------------------------------|---------------------------------------------------|
| Task_1ms      | High           | App_Runnable_TorqueCalc_1ms          | Safety-critical torque calculation (ASIL-D)       |
| Task_10ms     | Normal         | App_Runnable_SensorUpdate_10ms       | Sensor read & RTE write — source of race         |
| Task_100ms    | BelowNormal    | App_Runnable_DiagMonitor_100ms       | Sensor plausibility checks, DEM reporting         |

**The intentional preemption window:**

```c
/* === PREEMPTION WINDOW === */
volatile uint32 delay;
for (delay = 0; delay < 200000; delay++)
{
    __asm volatile ("nop");
}
```

This delay is intentional. In real hardware this gap is filled by an ADC conversion (10–50 µs) or an I²C transfer (100–500 µs). The demo amplifies it so the race condition becomes reliably reproducible — without this window the Cortex-M4 would finish the struct write between 1ms task ticks and the bug would hide.

**Smart fault reporting pattern:**
The diagnostic monitor uses a "heal counter" to avoid log spam. When a fault is actively injected it reports PREFAILED continuously. When the fault clears it reports PREPASSED a few times to let the DEM counter heal back to zero, then goes silent. This eliminates terminal spam while preserving correct DEM state transitions.

---

### 9. Mini_Timestamp — Unified Logging

All output goes through three functions:

- `Log_Write(tag, fmt, ...)` — produces `[t=XXXXms][TAG  ] message`
- `Log_Raw(fmt, ...)` — raw line without prefix (for banners)
- `Log_Banner(title)` — separator + title + separator

The `[TAG ]` field is padded to fixed width for aligned output. The timestamp is relative to `Log_Init()` (called at the start of BSW init), so each boot starts from 0.

**Why fixed-width tags and aligned timestamps?**
- Readable in a terminal
- Grep-friendly (`grep "\[DEM "` extracts all DEM messages)
- Matches the format Trace32 uses for ITM output — same mental model

---

### 10. Mini_FaultInj — Controlled Fault Injection

A demo-only module that doesn't exist in production ECUs. It lets the demo scenario deliberately inject specific sensor faults to trigger DEM events:

```c
FaultInj_Inject(FAULT_INJ_TORQUE_OUT_OF_RANGE);  /* torque sensor reads 250 Nm */
/* ...DEM detects, debounces, FAILED... */
FaultInj_Clear();  /* back to normal, DEM heals */
```

In real ECU development, you'd trigger equivalent faults via:
- CANoe/CANalyzer injecting out-of-range signal values
- Physical sensor short-circuit or disconnect on the test bench
- Software fault injection frameworks (e.g., AUTOSAR's DLT trace + manual signal manipulation)

For a self-contained demo, a simple in-process switch is the cleanest approach.

---

## Design Principles Applied Throughout

Things the project is deliberately careful about:

1. **Minimize global state** — variables are module-local `static` whenever possible
2. **Use const configuration tables** — mirrors real AUTOSAR generator output: data is constant and ROM-resident, code is generic
3. **Use exclusive areas deliberately** — not everywhere, only where real shared-data access needs protection
4. **Don't block** — UART via DMA in both directions, NvM writes queued asynchronously. The CPU never waits on I/O
5. **No malloc** — everything static, aligned with ASIL-D practice
6. **Readable logs** — timestamped, tagged, event-driven (not spam-driven)
7. **Teach through comments** — every non-trivial function explains not just *what* it does, but *why*, and *what the real AUTOSAR equivalent is*

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