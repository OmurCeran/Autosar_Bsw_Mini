# Autosar_Bsw_Mini

A minimal, educational implementation of AUTOSAR Classic BSW concepts on STM32F407 Discovery with FreeRTOS — featuring a working UDS diagnostic interface, static memory allocation, and a live ECU fault injection scenario.

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
├──────────────────────────────────────────────────┤
│  FreeRTOS with CMSIS-RTOS v2                     │
│  (fully static allocation — no malloc)           │
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
├── Middlewares/             ← FreeRTOS kernel
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
- Tasks never block on I/O

**RX (UDS tester input):**
- `HAL_UARTEx_ReceiveToIdle_DMA` with circular buffer
- IDLE-line interrupt signals "frame complete"
- No per-byte interrupts — CPU free for real-time tasks
- Byte-by-byte frame assembly handled in `Dcm_FeedRxByte`

This mirrors how AUTOSAR CAN drivers work — DMA transfers frames autonomously while the CPU runs safety-critical logic undisturbed.

---

## Static Memory Allocation — ASIL-D Alignment

This project uses **fully static allocation** — no malloc, no dynamic memory. All task TCBs, stacks, mutexes, and semaphores are allocated at compile time.

**FreeRTOS configuration:**
```c
#define configSUPPORT_STATIC_ALLOCATION   1
#define configCHECK_FOR_STACK_OVERFLOW    2    /* runtime guard */
#define configUSE_MALLOC_FAILED_HOOK      1
```

**Example — task buffer pattern:**
```c
static uint8_t      task1msStack[512 * 4] __attribute__((aligned(8)));
static StaticTask_t task1msControlBlock;

const osThreadAttr_t task1ms_attributes = {
    .name       = "task1ms",
    .stack_mem  = &task1msStack[0],           /* pre-allocated stack */
    .stack_size = sizeof(task1msStack),
    .cb_mem     = &task1msControlBlock,       /* pre-allocated TCB */
    .cb_size    = sizeof(task1msControlBlock),
    .priority   = osPriorityHigh,
};
```

This mirrors how AUTOSAR OS generators produce code: every task, every stack, every resource is declared statically from ARXML configuration. Runtime allocation is prohibited by MISRA-C:2012 Rule 21.3 and AUTOSAR C++14 Guideline A18-5-1.

**Stack overflow detection** is enabled — if any task exceeds its allocated stack, the system halts with a readable error message rather than silently corrupting memory.

**HardFault handler** dumps the full Cortex-M4 stack frame (R0-R12, LR, PC, PSR) plus fault status registers (CFSR, HFSR, MMFAR, BFAR) to UART. The PC value can be fed to `arm-none-eabi-addr2line` to locate the crashing instruction at file:line level — the same post-mortem debug technique used with Trace32 on production hardware.

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
Runs two 5-second windows — explicit vs implicit data access — to demonstrate how AUTOSAR's implicit RTE pattern eliminates torn-read races:

```
Cycle 1 | Phase 1: EXPLICIT mode (5s window)
EXPLICIT: 3000 inconsistencies detected
Result: RACE CONDITION

Cycle 1 | Phase 2: IMPLICIT mode (5s window)
IMPLICIT: 0 inconsistencies detected  [OK]
Result: AUTOSAR RTE snapshot works

==> Cycle 1 summary: EXPLICIT=3000, IMPLICIT=0
```

The writer task (10ms, medium priority) updates two related fields. The reader task (1ms, high priority) preempts mid-update. In EXPLICIT mode the reader sees a mix of old and new values (torn write). In IMPLICIT mode the reader reads from a task-boundary snapshot — always consistent. An invariant checker counts violations to quantify the difference.

### Scenario 3: DEM Lifecycle with Fault Injection
Injects a torque sensor fault, observes the full diagnostic sequence:

```
Step 1: Injecting torque sensor fault
[FAULT] >>> INJECTING FAULT: TORQUE_OUT_OF_RANGE <<<
[DEM ] Event TORQUE_SENSOR_FAULT: PREFAILED, debounce=1/5
[DEM ] Event TORQUE_SENSOR_FAULT: PREFAILED, debounce=2/5
[DEM ] Event TORQUE_SENSOR_FAULT: PREFAILED, debounce=3/5
[DEM ] Event TORQUE_SENSOR_FAULT: PREFAILED, debounce=4/5
[DEM ] Event TORQUE_SENSOR_FAULT: PREFAILED, debounce=5/5

[DEM ] >>> EVENT FAILED: TORQUE_SENSOR_FAULT (DTC 0x4A12) <<<
[DEM ] DTC status byte: 0x2F
[DEM ]   bit 0 (TF)     = 1  testFailed
[DEM ]   bit 1 (TFTOC)  = 1  testFailedThisOpCycle
[DEM ]   bit 2 (PDTC)   = 1  pendingDTC
[DEM ]   bit 3 (CDTC)   = 1  confirmedDTC
[DEM ]   bit 5 (TFSLC)  = 1  testFailedSinceLastClear

Step 2: Observing NvM async write completion
[NvM ] Block DEM_DTCS write QUEUED (async, 64 bytes)
[NvM ] Block DEM_DTCS processing STARTED (op=WRITE)
[NvM ] Block DEM_DTCS write COMPLETE (flash latency 30ms)

Step 4: Removing fault, observing healing
[FAULT] >>> CLEARING FAULT: TORQUE_OUT_OF_RANGE <<<
[DEM ] Event TORQUE_SENSOR_FAULT: PREPASSED, debounce=4/-5
[DEM ] Event TORQUE_SENSOR_FAULT: PREPASSED, debounce=3/-5
[DEM ] Event TORQUE_SENSOR_FAULT: PREPASSED, debounce=2/-5
[DEM ] Event TORQUE_SENSOR_FAULT: PREPASSED, debounce=1/-5
[DEM ] >>> EVENT HEALED: TORQUE_SENSOR_FAULT (status now 0x2A) <<<
```

### Scenario 4: Interactive UDS Session
After the automated demo completes, the ECU idles for 60 seconds accepting UDS requests from the terminal:

```
>> 10 03
[DCM ] Request received: SID=0x10, length=2
[DCM ] Session change: DEFAULT -> EXTENDED
[ECU] -> 50 03 00 32 01 F4

>> 22 F1 90
[DCM ] ReadDID 0xF190 (VIN) - returning 17 bytes
[ECU] -> 62 F1 90 57 41 55 5A 5A 5A 38 56 30 4E 41 30 31 32 33 34 35

>> 22 F1 91
[DCM ] ReadDID 0xF191 (HW_VERSION) - returning 4 bytes
[ECU] -> 62 F1 91 53 54 4D 34

>> 19 02 09
[DCM ] ReadDTC reportByStatusMask: mask=0x09
[ECU] -> 59 02 FF 00 4A 10 2F

>> 14 FF FF FF
[DCM ] ClearDTC: clearing all diagnostic information
[ECU] -> 54

>> 11 01
[DCM ] Service 0x11 not supported
[ECU] -> 7F 11 11
```

---

## UDS Services Reference

### 0x10 — Diagnostic Session Control
**Request:** `10 <sessionType>`
- `01` = Default, `02` = Programming, `03` = Extended

**Response:** `50 <session> 00 32 01 F4` (P2=50ms, P2*=5000ms)

### 0x22 — Read Data By Identifier
**Request:** `22 <DID_high> <DID_low>`

Supported DIDs:

| DID    | Content          | Length |
|--------|------------------|--------|
| 0xF190 | VIN              | 17 B   |
| 0xF18C | ECU Serial       | 10 B   |
| 0xF191 | HW Version       | 4 B    |
| 0xF195 | SW Version       | 4 B    |

### 0x19 — Read DTC Information
**Request:** `19 02 <statusMask>`  (subfunction 02 = reportDTCByStatusMask)

**Response:** `59 02 <AvailabilityMask> <DTC1_hi> <DTC1_mid> <DTC1_lo> <DTC1_status> ...`

### 0x14 — Clear Diagnostic Information
**Request:** `14 FF FF FF` (clear all)

**Response:** `54`

### 0x3E — Tester Present
**Request:** `3E 00`

**Response:** `7E 00`

### Negative Response
Format: `7F <original SID> <NRC>`

Supported NRCs:
- `0x11` Service Not Supported
- `0x12` Subfunction Not Supported
- `0x13` Incorrect Message Length
- `0x22` Conditions Not Correct
- `0x31` Request Out Of Range
- `0x33` Security Access Denied
- `0x78` Response Pending

---

## Build & Debug Workflow

### Prerequisites

| Tool | Purpose |
|------|---------|
| `arm-none-eabi-gcc` | Cross-compilation toolchain |
| `cmake` ≥ 3.22 | Build system (CMakePresets) |
| `ninja` or `make` | Build driver |
| ST-Link v2 | On-board on Discovery — no external probe needed |
| VS Code + **Cortex-Debug** extension | Debug IDE |

OpenOCD is **not used** — the project debugs via Cortex-Debug's direct ST-Link integration.

### Build

Using CMake presets:
```bash
cmake --preset Debug
cmake --build build/Debug
```

Or from VS Code: **Ctrl+Shift+B**

Artifacts in `build/Debug/`:
- `Autosar_Bsw_Mini.elf` — debug image
- `Autosar_Bsw_Mini.bin` / `.hex` — programmable binaries

### Debug

1. Connect the Discovery board via USB
2. Open the project in VS Code
3. Press **F5**

Cortex-Debug automatically flashes the ELF, halts at `main()`, and opens the register/memory/variable views. No OpenOCD config files, no separate GDB server launch.

### Terminal for UDS Interaction

Any terminal emulator over USART2 (115200, 8N1) works. Example with Tera Term:

1. **Setup → Serial port**: 115200, 8, none, 1, none
2. **Setup → Terminal → New-line**: Receive = CR+LF, Transmit = CR
3. **Setup → Terminal → Local echo**: ON (to see your input)

### Observing the Race Condition

Set a breakpoint in `Core/Src/Mini_Rte.c` on the line:
```c
rte_inconsistencyCount++;
```

- In **explicit mode** this breakpoint fires repeatedly → race condition confirmed
- In **implicit mode** it never fires → AUTOSAR solution proven

For non-intrusive observation, watch the DMA UART console. The counters update without halting the CPU — the same technique used with Trace32 ITM on real safety work.

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