# Mini AUTOSAR BSW Demo

A minimal implementation of AUTOSAR Classic BSW concepts on STM32F4 Discovery + FreeRTOS.

**This is NOT a real AUTOSAR stack.** It demonstrates the *mechanisms* and *architectural patterns* that AUTOSAR BSW modules use — implemented from scratch for learning and portfolio purposes.

## Why This Project?

AUTOSAR Classic BSW is commercially licensed and cannot be used individually. This project implements the core *concepts* of key BSW modules to demonstrate architectural understanding:

- **How COM packs signals into I-PDUs** (bit-level packing/unpacking with endianness)
- **How DEM manages diagnostic events** (debounce, DTC status byte per ISO 14229, freeze frame)
- **How SchM dispatches MainFunctions** (periodic BSW scheduling, exclusive areas)
- **How RTE provides data consistency** (implicit vs explicit data access patterns)
- **Why race conditions occur in shared data** and how AUTOSAR solves them

The project recreates a real EPS (Electric Power Steering) debug scenario end-to-end.

## Architecture

```
┌─────────────────────────────────────────┐
│  Application Layer (SWC Simulation)     │
│  - Torque calculation (1ms task)        │
│  - Sensor reading (10ms task)           │
│  - Diagnostic monitor (100ms task)      │
├─────────────────────────────────────────┤
│  Mini RTE (Data Access Layer)           │
│  - Rte_IRead / Rte_IWrite (implicit)    │
│  - Rte_Read / Rte_Write (explicit)      │
│  - Task-begin snapshot / task-end flush │
├──────────┬──────────┬───────────────────┤
│ Mini COM │ Mini DEM │ Mini SchM         │
│ Signal   │ Event    │ MainFunction      │
│ packing  │ debounce │ dispatch          │
│ I-PDU    │ Status   │ Exclusive area    │
│ Tx/Rx    │ byte     │ (IRQ disable)     │
├──────────┴──────────┴───────────────────┤
│  HAL — UART Tx (DMA-based)              │
├─────────────────────────────────────────┤
│  FreeRTOS (in place of AUTOSAR OS)      │
├─────────────────────────────────────────┤
│  STM32F407VG Discovery — ARM Cortex-M4  │
└─────────────────────────────────────────┘
```

## Module Mapping to Real AUTOSAR

| This Project     | Real AUTOSAR         | What it demonstrates                    |
|-----------------|----------------------|-----------------------------------------|
| `Mini_Com`      | COM + PduR + CanIf   | Signal-to-PDU packing, byte order       |
| `Mini_Dem`      | DEM + NvM (partial)  | Debounce, DTC status byte, freeze frame |
| `Mini_SchM`     | SchM                 | MainFunction scheduling, exclusive area |
| `Mini_Rte`      | RTE                  | Implicit vs explicit data access        |
| `App_Swc`       | SWC (Runnable)       | Application logic in tasks              |
| UART Tx (DMA)   | CAN Driver + CanIf   | Simulated bus output, non-blocking      |
| RAM buffer      | NvM → Fee → Fls      | Simulated non-volatile storage          |

## Race Condition Demo

The project includes a deliberate race condition demonstration:

1. **Explicit mode**: 10ms task writes a struct member-by-member, 1ms task preempts and reads inconsistent data → wrong torque calculation
2. **Implicit mode**: RTE snapshots data at task start, guaranteeing consistency → correct calculation

An inconsistency counter quantifies the difference. In typical runs:
- Explicit mode: dozens of inconsistencies over 5 seconds
- Implicit mode: zero inconsistencies

This mirrors a real EPS debug scenario I encountered on a production project.

## DMA-Based UART Output

Demo output is streamed over USART2 using **DMA** rather than blocking or interrupt-driven transmission. This matches safety-critical system design practice:

- The 1ms task does **not** block waiting for UART
- CPU is free to run torque calculations while DMA ships bytes in the background
- No priority inversion through the UART driver
- Transmission completion is signaled via DMA TC interrupt

This is how a real production AUTOSAR system would handle trace/diagnostic output — minimizing impact on safety-critical timing.

## Build & Debug Workflow

### Prerequisites
- `arm-none-eabi-gcc` toolchain
- `cmake` (3.20+)
- ST-Link v2 (on-board on the Discovery board)
- VS Code with the **Cortex-Debug** extension
- `st-util` or `stlink-tools` (for direct ST-Link access — no OpenOCD required)

### Build
```bash
mkdir build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/arm-none-eabi.cmake ..
make -j$(nproc)
```

### Debug (VS Code + Cortex-Debug)

Open the project in VS Code and press **F5**. Cortex-Debug handles everything:
- Starts `st-util` (GDB server) automatically
- Flashes the `.elf` to the board
- Stops at `main()`
- Provides SWV/ITM console for printf-style output

**No OpenOCD is used in this project.** The Cortex-Debug extension talks directly to the ST-Link probe via `st-util`. This keeps the toolchain lean and avoids the configuration complexity of OpenOCD's config files.

### Flash Only (no debug)
```bash
st-flash write build/mini_autosar_bsw.bin 0x08000000
```

## Hardware
- STM32F407VG Discovery Board (on-board ST-Link v2)
- USB-UART adapter or terminal connected to PA2 (USART2 TX) — for DMA output
- No additional hardware required

## Project Layout

```
mini-autosar-bsw/
├── README.md / ARCHITECTURE.md / SETUP.md
├── CMakeLists.txt
├── cmake/arm-none-eabi.cmake
├── .vscode/                     ← Cortex-Debug launch config
├── include/
│   ├── Std_Types.h              ← AUTOSAR std types
│   ├── app/App_Swc.h
│   └── bsw/
│       ├── com/Mini_Com.h
│       ├── dem/Mini_Dem.h
│       ├── rte/Mini_Rte.h       ← Key file: implicit/explicit access
│       └── schm/Mini_SchM.h
└── src/
    ├── main.c                   ← FreeRTOS task creation
    ├── app/App_Swc.c            ← EPS runnables + race condition setup
    └── bsw/
        ├── com/Mini_Com.c       ← Bit-level signal packing
        ├── dem/Mini_Dem.c       ← Debounce, DTC status, freeze frame
        ├── rte/Mini_Rte.c       ← Core of the demo
        └── schm/Mini_SchM.c     ← Exclusive area + MainFunction dispatch
```

## License
MIT — Educational use. Not affiliated with AUTOSAR consortium or any vehicle manufacturer.