# Mini AUTOSAR BSW Demo

A minimal implementation of AUTOSAR Classic BSW concepts on STM32F4 + FreeRTOS.

**This is NOT a real AUTOSAR stack.** It demonstrates the *mechanisms* and *architectural patterns* that AUTOSAR BSW modules use — implemented from scratch for learning purposes.

## Why This Project?

AUTOSAR Classic BSW is commercially licensed and cannot be used individually. This project implements the core *concepts* of key BSW modules to demonstrate architectural understanding:

- **How COM packs signals into I-PDUs** (bit-level packing/unpacking)
- **How DEM manages diagnostic events** (debounce, status byte, freeze frame)
- **How SchM dispatches MainFunctions** (periodic BSW scheduling)
- **How RTE provides data consistency** (implicit vs explicit access patterns)
- **Why race conditions occur** and how AUTOSAR solves them

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
│  HAL (UART for CAN simulation)          │
├─────────────────────────────────────────┤
│  FreeRTOS (replacing AUTOSAR OS)        │
├─────────────────────────────────────────┤
│  STM32F4 Discovery — ARM Cortex-M4     │
└─────────────────────────────────────────┘
```

## Module Mapping to Real AUTOSAR

| This Project     | Real AUTOSAR         | What it demonstrates                    |
|-----------------|----------------------|-----------------------------------------|
| `mini_com`      | COM + PduR + CanIf   | Signal-to-PDU packing, byte order       |
| `mini_dem`      | DEM + NvM (partial)  | Debounce, DTC status byte, freeze frame |
| `mini_schm`     | SchM                 | MainFunction scheduling, exclusive area |
| `mini_rte`      | RTE                  | Implicit vs explicit data access        |
| `app_swc`       | SWC (Runnable)       | Application logic in tasks              |
| UART Tx         | CAN Driver           | Simulated bus output                    |
| RAM buffer      | NvM → Fee → Fls      | Simulated non-volatile storage          |

## Race Condition Demo

The project includes a deliberate race condition demonstration:
1. **Explicit mode**: 10ms task writes a struct member-by-member, 1ms task preempts and reads inconsistent data → wrong torque calculation
2. **Implicit mode**: RTE snapshots data at task start, guaranteeing consistency → correct calculation

This mirrors a real EPS (Electric Power Steering) debug scenario.

## Build & Flash

### Prerequisites
- `arm-none-eabi-gcc` toolchain
- `cmake` (3.20+)
- `openocd`
- VS Code + Cortex-Debug extension

### Build
```bash
mkdir build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/arm-none-eabi.cmake ..
make -j$(nproc)
```

### Flash
```bash
openocd -f board/stm32f4discovery.cfg -c "program build/mini_autosar_bsw.elf verify reset exit"
```

### Debug (VS Code)
Use the provided `.vscode/launch.json` configuration with Cortex-Debug.

## Hardware
- STM32F407VG Discovery Board
- USB-UART adapter (for COM Tx simulation output)
- No additional hardware required

## License
MIT — Educational use. Not affiliated with AUTOSAR consortium.
