# Autosar_Bsw_Mini

A minimal, educational implementation of AUTOSAR Classic BSW concepts on STM32F407 Discovery with FreeRTOS.

This project demonstrates the **mechanisms** behind AUTOSAR Basic Software — not a production stack. It recreates a real Electric Power Steering (EPS) race condition end-to-end and shows how the AUTOSAR RTE solves it.

For deep design rationale and module-by-module discussion, see [`ARCHITECTURE.md`](./ARCHITECTURE.md).

---

## What This Project Demonstrates

- **Bit-level signal packing** in a COM-like module (endianness, PDU layout)
- **Diagnostic event handling** in a DEM-like module (counter-based debounce, ISO 14229 DTC status byte, freeze frame, async NvM storage)
- **BSW scheduling** in a SchM-like module (MainFunction dispatch, exclusive areas)
- **RTE data consistency** — the core of the project (`Rte_IRead/IWrite` vs `Rte_Read/Write`)
- **Race condition reproduction and resolution** — explicit mode shows the bug, implicit mode eliminates it

---

## Architecture Overview

```
┌─────────────────────────────────────────┐
│  Application Layer (SWC Simulation)     │
│  - Torque calculation      (1ms task)   │
│  - Sensor update           (10ms task)  │
│  - Diagnostic monitor      (100ms task) │
├─────────────────────────────────────────┤
│  Mini_Rte (Data Access Layer)           │
│  - Implicit snapshot (Rte_Task_Begin)   │
│  - Implicit flush    (Rte_Task_End)     │
│  - Explicit direct read/write           │
├──────────┬──────────┬───────────────────┤
│ Mini_Com │ Mini_Dem │ Mini_SchM         │
│ Signal   │ Event    │ Exclusive area    │
│ packing  │ debounce │ MainFunction      │
│ I-PDU    │ Status   │ dispatch          │
├──────────┴──────────┴───────────────────┤
│  HAL — USART2 with DMA (non-blocking)   │
├─────────────────────────────────────────┤
│  FreeRTOS (acts as AUTOSAR OS)          │
├─────────────────────────────────────────┤
│  STM32F407VG Discovery — Cortex-M4F     │
└─────────────────────────────────────────┘
```

---

## Repository Layout

```
Autosar_Bsw_Mini/
├── .vscode/                 ← VS Code + Cortex-Debug launch config
├── Core/                    ← STM32CubeMX-generated application layer
│   ├── Inc/                 ← main.h, FreeRTOSConfig.h, plus BSW headers
│   └── Src/                 ← main.c, Mini_* BSW sources, App_Swc.c
├── Drivers/                 ← STM32 HAL + CMSIS
├── Middlewares/             ← FreeRTOS kernel
├── USB_HOST/                ← (CubeMX scaffolding, not used in demo)
├── cmake/                   ← Toolchain & build helper files
├── AUTOSAR_Mini.ioc         ← STM32CubeMX project file
├── CMakeLists.txt           ← Main build script
├── CMakePresets.json        ← Named build configurations
├── STM32F407XX_FLASH.ld     ← Linker script
├── startup_stm32f407xx.s    ← Reset handler & vector table
├── README.md                ← You are here
└── ARCHITECTURE.md          ← Module-by-module design rationale
```

---

## DMA-Based UART Output

Demo output is streamed over USART2 using **DMA** rather than blocking or interrupt-driven transmission. This matches safety-critical system design practice:

- The 1ms task never blocks waiting for UART
- CPU stays free for torque calculations while DMA ships bytes in the background
- No priority inversion through the UART driver
- Transfer completion is signaled via DMA Transfer Complete interrupt

This is how a real production AUTOSAR system handles trace and diagnostic output — minimizing impact on safety-critical task timing.

---

## Race Condition Demo

The project includes a deliberate, measurable race condition:

1. **Explicit mode** — the 10ms task writes the shared struct member-by-member; the 1ms task preempts mid-write and reads a mix of old + new values → wrong torque calculation.
2. **Implicit mode** — the RTE takes an atomic snapshot at task start and flushes atomically at task end → consistent data on every task execution.

An inconsistency counter quantifies the difference:

```
[DEMO] EXPLICIT mode — 5 second window
[DEMO] Inconsistencies detected: 47
[DEMO] Switching to IMPLICIT mode
[DEMO] IMPLICIT mode — 5 second window
[DEMO] Inconsistencies detected: 0
```

This mirrors a real EPS debug scenario solved on a production project.

---

## Prerequisites

| Tool | Purpose |
|------|---------|
| `arm-none-eabi-gcc` | Cross-compilation toolchain |
| `cmake` ≥ 3.22 | Build system (with CMakePresets support) |
| `ninja` or `make` | Build driver |
| ST-Link v2 | On-board on the Discovery — no external probe needed |
| VS Code + **Cortex-Debug** extension | Debug IDE |

OpenOCD is **not used** — the project debugs via Cortex-Debug's direct ST-Link integration.

---

## Build

Using CMake presets (recommended):
```bash
cmake --preset Debug
cmake --build build/Debug
```

Or from VS Code: **Ctrl+Shift+B**

Artifacts are produced in `build/Debug/`:
- `Autosar_Bsw_Mini.elf` — debug image
- `Autosar_Bsw_Mini.bin` / `.hex` — programmable binaries

---

## Debug

1. Connect the Discovery board via USB
2. Open the project in VS Code
3. Press **F5**

Cortex-Debug automatically flashes the ELF, halts at `main()`, and opens the register/memory/variable views. No OpenOCD config files, no separate GDB server launch — the extension handles everything.

### Observing the Race Condition

Set a breakpoint in `Core/Src/Mini_Rte.c` on the line:
```c
rte_inconsistencyCount++;
```

- In **explicit mode** this breakpoint fires repeatedly → race condition confirmed
- In **implicit mode** it never fires → AUTOSAR solution proven

For a non-intrusive observation, watch the DMA UART console — the counters update without halting the CPU, which is the same technique used with Trace32 ITM in real safety work.

---

## Module Mapping to Real AUTOSAR

| This Project | Real AUTOSAR | What it demonstrates |
|---|---|---|
| `Mini_Rte` | RTE | Implicit vs explicit data access, task-boundary snapshot |
| `Mini_Com` | COM + PduR + CanIf | Signal-to-PDU packing, endianness, I-PDU buffering |
| `Mini_Dem` | DEM + NvM (partial) | Debounce, DTC status byte, freeze frame, async storage |
| `Mini_SchM` | SchM | MainFunction dispatch, exclusive areas |
| `App_Swc` | SWC + Runnables | Application logic mapped to periodic tasks |
| USART2 + DMA | CAN driver + CanIf | Non-blocking bus output |

---

## License
MIT — Educational use only. Not affiliated with the AUTOSAR consortium or any vehicle manufacturer.