# Autosar_Bsw_Mini

A minimal, educational implementation of AUTOSAR Classic BSW concepts on STM32F407 Discovery with FreeRTOS — featuring a working UDS diagnostic interface, static memory allocation, RTOS-aware trace profiling, and a live ECU fault injection scenario.

This project demonstrates the **mechanisms** behind AUTOSAR Basic Software — not a production stack. It recreates real ECU behavior end-to-end: startup sequence, normal operation, fault detection, DTC storage, diagnostic tester session, and recovery.

For deep design rationale and module-by-module discussion, see [`ARCHITECTURE.md`](./ARCHITECTURE.md).

---

## What This Project Demonstrates

### BSW Stack (7 modules)
- **Mini_EcuM** — ECU lifecycle orchestration (STARTUP → RUN)
- **Mini_SchM** — Exclusive areas, MainFunction dispatch
- **Mini_Rte** — Implicit vs explicit data access (race condition demo)
- **Mini_Com** — Bit-level signal packing, endianness handling
- **Mini_Dem** — Debounce, ISO 14229 DTC status byte, freeze frame
- **Mini_NvM** — Async flash simulation (write/read/erase)
- **Mini_Dcm** — UDS service dispatcher (ISO 14229)

### Supporting Infrastructure
- **Mini_Timestamp** — Module-tagged logging with boot-relative timestamps
- **Mini_FaultInj** — Controlled fault injection for demo scenarios
- **Log queue + dedicated diagnostic task** — Producer-consumer pattern for deterministic UART TX
- **SEGGER SystemView integration** — RTOS-aware trace profiling

### Diagnostic Interface (UDS / ISO 14229)
Full request-response cycle over UART (simulates ISO-TP over CAN):
- `0x10` — Diagnostic Session Control (Default / Extended / Programming)
- `0x22` — Read Data By Identifier (VIN, Hardware Version, Serial Number, Software Version)
- `0x19` — Read DTC Information (subfunction 0x02: reportDTCByStatusMask)
- `0x14` — Clear Diagnostic Information
- `0x3E` — Tester Present
- Negative Response codes (0x7F + SID + NRC)

---

## Architecture Overview

```
┌──────────────────────────────────────────────────┐
│  Application Layer (SWC Simulation)              │
│  - Torque calculation        (1ms task)          │
│  - Sensor update             (10ms task)         │
│  - Diagnostic monitor        (100ms task)        │
│  - UDS request handler       (DiagnosticTask)    │
├──────────────────────────────────────────────────┤
│  Mini_Rte (Data Access Layer)                    │
│  - Implicit snapshot         (Rte_Task_Begin)    │
│  - Implicit flush            (Rte_Task_End)      │
│  - Explicit direct read/write                    │
├─────────┬─────────┬─────────┬────────────────────┤
│ Mini_Com│ Mini_Dem│ Mini_NvM│ Mini_Dcm           │
│ Signal  │ Event   │ Async   │ UDS service        │
│ packing │ debounce│ flash   │ dispatcher         │
│ I-PDU   │ Status  │ sim     │ (ISO 14229)        │
├─────────┴─────────┴─────────┴────────────────────┤
│              Mini_SchM                           │
│  Exclusive areas · MainFunction dispatch         │
├──────────────────────────────────────────────────┤
│              Mini_EcuM                           │
│        Lifecycle orchestration                   │
├──────────────────────────────────────────────────┤
│  HAL — USART2 TX+RX (both DMA, non-blocking)     │
│  + Log queue (producer-consumer serialization)   │
├──────────────────────────────────────────────────┤
│  FreeRTOS with CMSIS-RTOS v2                     │
│  (fully static allocation — no malloc)           │
│  + SEGGER SystemView RTOS-aware profiling        │
├──────────────────────────────────────────────────┤
│  STM32F407VG Discovery — ARM Cortex-M4F          │
└──────────────────────────────────────────────────┘
```

---

## Repository Layout

```
Autosar_Bsw_Mini/
├── .vscode/                 ← VS Code + Cortex-Debug launch config
├── Core/
│   ├── Inc/                 ← STM32CubeMX-generated + BSW headers
│   │   ├── main.h, Std_Types.h
│   │   ├── App_Swc.h
│   │   ├── Mini_EcuM.h, Mini_SchM.h, Mini_Rte.h
│   │   ├── Mini_Com.h, Mini_Dem.h, Mini_NvM.h, Mini_Dcm.h
│   │   ├── Mini_Timestamp.h, Mini_FaultInj.h
│   │   └── FreeRTOSConfig.h
│   └── Src/                 ← main.c + Mini_* BSW sources + App_Swc.c
├── Drivers/                 ← STM32 HAL + CMSIS
├── Middlewares/             ← FreeRTOS kernel + SEGGER SystemView
├── cmake/                   ← Toolchain & build helpers
├── AUTOSAR_Mini.ioc         ← STM32CubeMX project file
├── CMakeLists.txt
├── CMakePresets.json
├── STM32F407XX_FLASH.ld     ← Linker script
├── startup_stm32f407xx.s    ← Reset handler & vector table
├── README.md                ← You are here
└── ARCHITECTURE.md          ← Deep design rationale
```

---

## Non-Blocking UART — DMA Both Directions

Both UART directions use DMA, following production safety-critical design patterns:

**TX (application logging + UDS responses):**
- `HAL_UART_Transmit_DMA` with mutex + semaphore coordination
- CMSIS-RTOS v2 timeouts prevent deadlock
- All TX producers go through a single serialization channel
- Tasks never block on I/O

**RX (UDS tester input):**
- `HAL_UARTEx_ReceiveToIdle_DMA` with circular buffer
- IDLE-line interrupt signals "frame complete"
- RX callback enqueues bytes into a FreeRTOS message queue
- Dedicated `DiagnosticTask` drains the queue (CPU cost = 0 when idle)
- No per-byte interrupts — CPU free for real-time tasks

This mirrors how AUTOSAR CAN drivers work — DMA transfers frames autonomously while the CPU runs safety-critical logic undisturbed.

---

## Log Queue — Producer-Consumer Pattern

During development, a subtle bug surfaced: UDS responses were occasionally lost. Analysis showed two code paths competing for UART TX: `DMA_Printf` (mutex+semaphore+DMA) for logs, and `HAL_UART_Transmit` (polling) for DCM responses. Under load, the polling transmit hit `HAL_BUSY` because the DMA transfer was still active.

**The fix:** all TX producers enqueue messages, a dedicated consumer task drains the queue to UART. This is the same pattern production AUTOSAR DLT modules use for trace output. Single peripheral = single owner = single serializer.

---

## Static Memory Allocation — ASIL-D Alignment

This project uses **fully static allocation** — no malloc, no dynamic memory. All task TCBs, stacks, mutexes, semaphores, and message queues are allocated at compile time.

**FreeRTOS configuration:**
```c
#define configSUPPORT_STATIC_ALLOCATION   1
#define configCHECK_FOR_STACK_OVERFLOW    2    /* runtime guard */
#define configUSE_MALLOC_FAILED_HOOK      1
```

**Example — task buffer pattern:**
```c
uint32_t           task1msStack[2048];
StaticTask_t       task1msControlBlock;

const osThreadAttr_t task1ms_attributes = {
    .name       = "task1ms",
    .cb_mem     = &task1msControlBlock,
    .cb_size    = sizeof(task1msControlBlock),
    .stack_mem  = &task1msStack[0],
    .stack_size = sizeof(task1msStack),
    .priority   = osPriorityHigh,
};
```

This mirrors how AUTOSAR OS generators produce code: every task, every stack, every resource is declared statically from ARXML configuration. Runtime allocation is prohibited by MISRA-C:2012 Rule 21.3 and AUTOSAR C++14 Guideline A18-5-1.

**Stack overflow detection** is enabled — if any task exceeds its allocated stack, the system halts with a readable error message rather than silently corrupting memory.

**HardFault handler** dumps the full Cortex-M4 stack frame (R0-R12, LR, PC, PSR) plus fault status registers (CFSR, HFSR, MMFAR, BFAR) to UART. The PC value feeds into `arm-none-eabi-addr2line` to locate the crashing instruction at file:line level — the same post-mortem debug technique used with Trace32 on production hardware.

---

## RTOS-Aware Profiling with SEGGER SystemView

SEGGER SystemView is integrated for runtime task profiling. The Discovery board's ST-Link was reflashed to J-Link OB firmware, enabling RTT-based trace capture with a J-Link EDU.

**What it reveals:**
- Task scheduling timeline — which task runs, when, and for how long
- CPU load per task (in the screenshot below: task1ms = 1.8%, task10ms = 60.6%, task100ms = 0.02%, Idle = 37.5%)
- Context switch latency
- Interrupt handler timing
- API calls (semaphore acquire/release, queue put/get)

**Why task10ms shows ~60% CPU load:**
The 10ms task contains a deliberate ~1ms NOP delay implementing a **preemption window** for the race condition demo. In a real ECU this gap would be filled by an ADC conversion (10-50µs) or an I²C transfer (100-500µs). The demo amplifies it so the race is reliably reproducible — without this window the Cortex-M4 would finish the struct write between 1ms task ticks and the bug would hide. This is documented in `App_Swc.c`.

Outside this deliberate delay, actual task CPU usage is in line with expectations: task1ms at ~1.8% and task100ms at ~0.02% CPU load.

This profiling capability matches what Trace32 provides for production ECUs — the mental model is identical, only the tooling differs.

---

## Demo Scenarios

The system runs a single end-to-end scenario on startup, then idles for 60 seconds accepting UDS interaction before the demo cycle repeats.

### Scenario 1: ECU Startup Sequence
```
[t=0000ms][EcuM ] Power-on reset, entering STARTUP state
[t=0018ms][EcuM ] EcuM_Init complete
[t=0022ms][EcuM ] Initializing BSW stack...
[t=0026ms][SchM ] SchM_Init complete - scheduler ready
[t=0031ms][NvM  ] NvM_Init complete - flash simulation ready
[t=0036ms][NvM  ] NvM_ReadAll complete - 0 blocks restored
[t=0041ms][RTE  ] Rte_Init complete - mode=IMPLICIT
[t=0046ms][COM  ] Com_Init complete - 4 signals, 2 PDUs
[t=0051ms][DEM  ] Dem_Init complete - 4 events registered
[t=0060ms][EcuM ] BSW stack fully initialized
[t=0337ms][EcuM ] State transition: STARTUP -> RUN
```

### Scenario 2: Race Condition Demo (RTE Access Pattern)
Runs two 5-second windows — explicit vs implicit data access:

```
Cycle 1 | Phase 1: EXPLICIT mode (5s window)
EXPLICIT: 3000 inconsistencies detected
Result: RACE CONDITION

Cycle 1 | Phase 2: IMPLICIT mode (5s window)
IMPLICIT: 0 inconsistencies detected  [OK]
Result: AUTOSAR RTE snapshot works

==> Cycle 1 summary: EXPLICIT=3000, IMPLICIT=0
```

### Scenario 3: DEM Lifecycle with Fault Injection
```
Step 1: Injecting torque sensor fault
[FAULT] >>> INJECTING FAULT: TORQUE_OUT_OF_RANGE <<<
[DEM ] Event TORQUE_SENSOR_FAULT: PREFAILED, debounce=1/5
[DEM ] Event TORQUE_SENSOR_FAULT: PREFAILED, debounce=2/5
...
[DEM ] Event TORQUE_SENSOR_FAULT: PREFAILED, debounce=5/5

[DEM ] >>> EVENT FAILED: TORQUE_SENSOR_FAULT (DTC 0x4A12) <<<
[DEM ] DTC status byte: 0x2F
[DEM ]   bit 0 (TF)     = 1  testFailed
[DEM ]   bit 1 (TFTOC)  = 1  testFailedThisOpCycle
[DEM ]   bit 2 (PDTC)   = 1  pendingDTC
[DEM ]   bit 3 (CDTC)   = 1  confirmedDTC
[DEM ]   bit 5 (TFSLC)  = 1  testFailedSinceLastClear

[NvM ] Block DEM_DTCS write QUEUED (async, 64 bytes)
[NvM ] Block DEM_DTCS processing STARTED (op=WRITE)
[NvM ] Block DEM_DTCS write COMPLETE (flash latency 30ms)
```

### Scenario 4: Interactive UDS Session
```
>> 22 F1 90
[DCM ] Request received: SID=0x22, length=3
[DCM ] ReadDID 0xF190 (VIN) - returning 17 bytes
[DCM ] Response: positive (SID 0x22, 20 bytes)
[ECU] -> 62 F1 90 57 41 55 5A 5A 5A 38 56 30 4E 41 30 31 32 33 34 35

>> 11 01
[DCM ] Service 0x11 not supported
[ECU] -> 7F 11 11
```

---

## UDS Services Reference

### 0x10 — Diagnostic Session Control
**Request:** `10 <sessionType>` (`01`=Default, `02`=Programming, `03`=Extended)
**Response:** `50 <session> 00 32 01 F4` (P2=50ms, P2*=5000ms)

### 0x22 — Read Data By Identifier

| DID    | Content          | Length |
|--------|------------------|--------|
| 0xF190 | VIN              | 17 B   |
| 0xF18C | ECU Serial       | 10 B   |
| 0xF191 | HW Version       | 4 B    |
| 0xF195 | SW Version       | 4 B    |

### 0x19 — Read DTC Information (subfunction 02: reportDTCByStatusMask)
### 0x14 — Clear Diagnostic Information
### 0x3E — Tester Present

**Negative Response Format:** `7F <original SID> <NRC>`

Supported NRCs: `0x11` Service Not Supported · `0x12` Subfunction Not Supported · `0x13` Incorrect Message Length · `0x22` Conditions Not Correct · `0x31` Request Out Of Range · `0x33` Security Access Denied · `0x78` Response Pending

---

## Build & Debug Workflow

### Prerequisites

| Tool | Purpose |
|------|---------|
| `arm-none-eabi-gcc` | Cross-compilation toolchain |
| `cmake` ≥ 3.22 | Build system (CMakePresets) |
| `ninja` or `make` | Build driver |
| ST-Link v2 **or J-Link OB** | On-board probe (J-Link OB for SystemView trace) |
| VS Code + Cortex-Debug | Debug IDE |
| SEGGER SystemView | RTOS-aware trace profiling (optional) |

### Build

```bash
cmake --preset Debug
cmake --build build/Debug
```

### Debug

Connect via USB, press **F5** in VS Code. Cortex-Debug flashes the ELF and halts at `main()`.

### Terminal for UDS Interaction

Tera Term over USART2 (115200, 8N1):
- **New-line**: Receive = CR+LF, Transmit = CR
- **Local echo**: ON

### Observing the Race Condition

Set a breakpoint in `Mini_Rte.c` on `rte_inconsistencyCount++`. In explicit mode this fires continuously; in implicit mode it never fires.

---

## Module Mapping to Real AUTOSAR

| This Project    | Real AUTOSAR         | What it demonstrates                         |
|-----------------|----------------------|----------------------------------------------|
| `Mini_EcuM`     | EcuM                 | Startup sequence, state management           |
| `Mini_SchM`     | SchM                 | Exclusive areas, MainFunction dispatch       |
| `Mini_Rte`      | RTE                  | Implicit vs explicit data access             |
| `Mini_Com`      | COM + PduR + CanIf   | Signal-to-PDU packing, endianness            |
| `Mini_Dem`      | DEM                  | Debounce, DTC status byte, freeze frame      |
| `Mini_NvM`      | NvM + MemIf + Fee    | Async storage, MainFunction-driven writes    |
| `Mini_Dcm`      | DCM (DSD/DSL/DSP)    | UDS service dispatch, session control        |
| `App_Swc`       | SWC + Runnables      | Application mapped to periodic tasks         |
| USART2 RX (DMA) | CAN driver + CanIf Rx| Frame reception with IDLE detection          |
| USART2 TX (DMA) | CAN driver + CanIf Tx| Non-blocking bus output                      |
| DiagnosticTask  | DCM RX path           | Dedicated UDS frame consumer from queue      |

---

## What This Project Does NOT Implement

By design, several AUTOSAR concepts are omitted or heavily simplified:

- **Full ISO-TP** (CanTp) — single-frame only, no segmented transfers
- **Security Access** (0x27) — not implemented (demo is open)
- **Real MCAL** — uses STM32 HAL directly
- **Wake-up / Sleep modes** — EcuM STARTUP and RUN only
- **BswM rule engine** — simplified to direct function calls
- **WdgM deadline supervision** — not implemented
- **Real CAN hardware** — transport simulated over UART
- **ARXML / DaVinci Configurator** — configuration is hand-coded tables

---

## License
MIT — Educational use only. Not affiliated with the AUTOSAR consortium or any vehicle manufacturer.