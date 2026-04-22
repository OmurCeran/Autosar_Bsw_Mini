# ARCHITECTURE.md — Design Decisions and AUTOSAR Mapping

This document explains **why** each module in `Autosar_Bsw_Mini` is designed the way it is, and **what it corresponds to in a real-world AUTOSAR Classic architecture**.

Use this as a technical reference to understand the engineering trade-offs and mechanisms simulated by the project. For a quick project overview and build/debug instructions, see [`README.md`](./README.md).

---

## Overall Approach

This project is **not** a 1:1 replica of a production-grade AUTOSAR Classic Basic Software (BSW) stack — it is a **conceptual simulation**. The primary goals are:

1. **Understand the core mechanisms** — Strip AUTOSAR's extreme complexity down to readable, fundamental mechanisms (e.g., how RTE implicit communication actually prevents race conditions).
2. **Demonstrate a real engineering problem** — Implement a tangible, hardware-level problem (EPS race condition causing torque miscalculation) with working, executable code.
3. **Provide a portfolio reference** — A presentable project for technical interviews that demonstrates architectural awareness, not just buzzword familiarity.
4. **Teach through comments** — Source code is documented not only to explain *what* a function does, but *why* it exists and *what its real AUTOSAR equivalent is* (e.g., `Rte_IRead` vs. `Rte_Read`).

---

## Target Platform Rationale

The project runs on **STM32F407VG Discovery + FreeRTOS**, built with CMake and debugged via VS Code's Cortex-Debug extension (no OpenOCD).

**Why this stack?**

- **STM32F407** — ARM Cortex-M4F with FPU, common and cheap, widely used in automotive prototyping.
- **FreeRTOS** — Conceptually closest open-source analogue to AUTOSAR OS: priority-based preemptive scheduling, static task priorities, no POSIX abstraction. FreeRTOS tasks map cleanly onto AUTOSAR OS tasks.
- **CMake + CMakePresets** — Modern, toolchain-agnostic build. Mirrors how real automotive tiered suppliers are moving away from vendor-locked IDE project files.
- **Cortex-Debug + ST-Link direct** — Avoids OpenOCD configuration overhead. Uses the on-board ST-Link probe through the extension's native GDB server integration. Leaner and more reproducible than OpenOCD config trees.
- **STM32CubeMX** (`.ioc` file kept in repo) — Generates HAL init, FreeRTOS scaffolding, DMA/USART configuration. Keeping the `.ioc` in version control means anyone can regenerate the HAL layer from a known-good configuration.

---

## Non-Blocking UART Design — DMA Over Polling/IRQ

One deliberate design decision worth calling out: **UART output uses DMA, not `HAL_UART_Transmit` blocking calls or interrupt-driven per-byte transmission.**

**Why this matters in a safety-critical context:**

- The 1ms task must never block. A blocking `HAL_UART_Transmit` could stall the CPU for milliseconds at 115200 baud, violating ASIL-D deadline monitoring.
- Interrupt-per-byte transmission generates hundreds of interrupts per message, each stealing cycles from the 1ms task and causing jitter.
- DMA offloads the entire transfer to hardware. The CPU configures the transfer once, then the DMA controller ships bytes autonomously. The task returns immediately.

**Implementation pattern:**
- `HAL_UART_Transmit_DMA()` kicks off a transfer
- The DMA TC (Transfer Complete) callback signals readiness for the next message
- If a message is requested while DMA is busy, it is queued (simple ring buffer)

This mirrors how real AUTOSAR CAN drivers work — `Can_Write()` queues the frame; the CAN controller ships it autonomously; the `Can_TxConfirmation` callback signals completion. The pattern is identical; only the peripheral differs.

---

## Module-by-Module Design Notes

### 1. Mini_Rte — The Most Important Module

**Why is this the core of the project?**
The EPS debug story lives here. Both the root cause and the architectural solution of the race condition are implemented in this file.

**Two supported modes:**

- **`RTE_ACCESS_EXPLICIT`** — Software Components (SWCs) read/write directly from/to the global buffer. Preemption during a multi-field write leads to data corruption (the race condition).
- **`RTE_ACCESS_IMPLICIT`** — A snapshot (local copy) of the shared data is taken at the start of the task (`Rte_Task_Begin`), and outputs are flushed atomically at the end of the task (`Rte_Task_End`). Data stays strictly consistent for the entire task execution.

**In real AUTOSAR:**
- The choice is made at port level in DaVinci Configurator (via the `dataElement.isReentrant` attribute on `SenderReceiverInterface`).
- The RTE generator produces entirely different C macros for each mode.
- The mode is fixed at compile time — it cannot be changed at runtime.

**In this demo:**
- The mode can be switched at runtime, purely for educational visibility.
- An inconsistency counter proves the race condition is real, not theoretical.
- Explicit mode produces dozens of inconsistencies per 5-second window; implicit mode produces zero. The contrast is immediate.

---

### 2. Mini_SchM — Exclusive Areas

**Critical design rule:** Exclusive area duration must be **MINIMAL**.

This constraint is documented throughout the code:

> *"Keep the code between Enter and Exit as SHORT as possible. Long exclusive areas cause timing violations in safety-critical systems. In EPS: max exclusive area duration ~ 5–10 microseconds."*

The constraint comes directly from real EPS safety requirements. Disabling interrupts for too long causes the 1ms safety task to miss its deadline, which triggers WdgM (Watchdog Manager) deadline monitoring and forces the MCU into a safe state.

**Interview FAQ — *"Why didn't you use a FreeRTOS mutex?"***
AUTOSAR OS does not use task-level blocking mutexes for BSW data protection. Shared BSW data is protected via SchM Exclusive Areas (which map to hardware interrupt disabling) or OS Resources (Priority Ceiling Protocol). For extremely short critical sections — such as copying a struct of 20 bytes — exclusive areas are deterministic and much faster, avoiding the RTOS context-switch overhead that mutex acquisition implies.

---

### 3. Mini_Com — Signal Packing & Endianness

**Focus:** bit-level payload packing and endianness handling.

Motorola (Big-Endian) byte order is the standard in automotive CAN networks. The code supports both Big-Endian and Little-Endian ordering, selectable from the configuration table.

**Simplifications made for the demo:**
- Only 16-bit byte-aligned signals are implemented (real COM supports arbitrary bit-lengths across byte boundaries).
- `PduR` (PDU Router) and `CanIf` (CAN Interface) are abstracted and simulated inside COM — the demo outputs via DMA UART instead of a real CAN bus.
- Transmission mode is `Periodic` only (real COM also supports Direct, Mixed, and None).

**Key concepts demonstrated:**
- Signal-to-PDU mapping (`startBit`, `bitLength`, `endianness`).
- MainFunction-triggered transmission — a PDU is sent on the 10ms cycle, not instantly when a SWC writes a signal. This decouples signal production from bus scheduling and matches real AUTOSAR timing behavior.

---

### 4. Mini_Dem — Debounce and DTC Status

**Counter-based debounce algorithm:**
- `PREFAILED` report: counter += `step_up`
- `PREPASSED` report: counter += `step_down` (negative value)
- When the counter reaches `threshold_failed` → **FAILED** status transition
- When the counter drops to `threshold_passed` → **PASSED** status transition

**ISO 14229 DTC Status Byte (8 bits):**
The module simulates the standard UDS status byte. Each bit has a distinct meaning:

| Bit | Name | Meaning |
|-----|------|---------|
| 0 | TF | testFailed — fault is currently active |
| 1 | TFTOC | testFailedThisOperationCycle |
| 2 | PDTC | pendingDTC |
| 3 | CDTC | confirmedDTC — matured and stored in NV memory |
| 4 | TNCSLC | testNotCompletedSinceLastClear |
| 5 | TFSLC | testFailedSinceLastClear |
| 6 | TNCTOC | testNotCompletedThisOperationCycle |
| 7 | WIR | warningIndicatorRequested — MIL lamp |

**Freeze frame mechanism:**
- The SWC continuously updates the freeze frame source data via `Dem_SetFreezeFrameData()`.
- When an event transitions to FAILED, the current source data is captured into that event's freeze frame.
- A UDS tester can later read this via service `0x19 04`.

**Asynchronous NvM write (delayed-write pattern):**
- `Dem_SetEventStatus()` returns immediately — it does not touch flash.
- In `Dem_MainFunction`, the `isStored` flag triggers an (simulated) `NvM_WriteBlock()` request.
- In a real system, NvM's own MainFunction would asynchronously push the data through MemIf → Fee → Fls to flash.
- This prevents the CPU from stalling during multi-millisecond flash write cycles — a critical property in real-time systems.

---

### 5. App_Swc — Runnable Simulation & Hardware Latency

**Three runnables mapped to three RTOS tasks:**

| Task | Priority | Runnable | Role |
|------|----------|----------|------|
| `Task_1ms`   | High (4)   | `App_Runnable_TorqueCalc_1ms`      | Safety-critical torque calculation (ASIL-D) |
| `Task_10ms`  | Medium (3) | `App_Runnable_SensorUpdate_10ms`   | Sensor read & RTE write — source of the race condition |
| `Task_100ms` | Low (2)    | `App_Runnable_DiagMonitor_100ms`   | Sensor plausibility checks, DEM reporting |

**The intentional preemption window:**

```c
/* === PREEMPTION WINDOW === */
volatile uint32 delay;
for (delay = 0; delay < 100; delay++) { /* Simulate hardware read time */ }
```

This delay is intentional. In real hardware this gap is filled by an ADC conversion (10–50 µs) or an I²C transfer (100–500 µs). The demo amplifies it so the race condition becomes reliably reproducible — without this window the Cortex-M4 would finish the struct write between 1ms task ticks and the bug would hide.

**Torque calculation formula:**
```c
motor_cmd = torque_input * calib_coeff * (100.0f / vehicle_speed);
```

Low speed → high assist (parking); high speed → low assist (highway). When a race condition causes the wrong `vehicle_speed` to be read, the assist level is miscalculated — this is the exact mechanism behind the "momentary steering heaviness" that drivers feel in a real EPS fault.

---

## Non-Intrusive Debug Strategy

The project is designed to be inspected via **non-halting** techniques — the same approach used with Trace32 in real safety work:

**DMA UART streaming** — The demo task prints counters continuously via DMA. The 1ms safety task is never stalled by the print output. Watching the UART console gives real-time insight without any breakpoint interference.

**Cortex-Debug live variable watches** — VS Code's Cortex-Debug extension can poll variables while the CPU runs. Add `rte_inconsistencyCount` and `rte_accessMode` to the live watch; they update in real time without halting the CPU.

**ITM/SWO trace** (optional, where enabled) — Single-byte trace writes via ITM are non-intrusive and mirror what Trace32 does in EPS debugging. The Cortex-Debug extension's SWO console supports this directly without any external tooling.

These approaches are deliberate. Halting a 1ms task on a breakpoint in a safety-critical system alters timing enough to mask or create bugs. Non-intrusive observation is the only way to catch timing-sensitive issues — and this project uses the exact same mental model.

---

## Lessons Reflected in the Design

Things the project is deliberately careful about:

1. **Minimize global state** — module-local `static` variables wherever possible.
2. **Use `const` configuration tables** — mirrors real AUTOSAR generator output: data is constant and ROM-resident, code is generic.
3. **Use exclusive areas sparingly** — not everywhere, only where real shared-data protection is needed.
4. **Don't block** — UART via DMA, NvM writes queued asynchronously. The CPU never waits on I/O.
5. **Teach through comments** — every non-trivial function explains not just *what* it does, but *why*, and *what the real AUTOSAR equivalent is*.

---

## Presenting This Project in an Interview

A suggested 3-minute walkthrough:

**Opening (30 seconds)**
> "I wanted to deepen my understanding of the internal mechanisms of AUTOSAR Classic BSW. Since commercial stacks aren't available outside of OEM licensing, I implemented conceptual versions of the core modules on STM32F4 + FreeRTOS, built with CMake, debugged via Cortex-Debug."

**Demo (1 minute)**
> "It includes a working demonstration of the race condition I encountered in my EPS project. The demo switches between explicit and implicit RTE access modes and uses an inconsistency counter streamed via DMA UART to show the difference quantitatively — explicit mode produces dozens of errors per 5 seconds, implicit mode produces zero."

**Depth (1.5 minutes)**
> "COM performs bit-level signal packing with configurable endianness. DEM manages counter-based debounce and DTC status bytes per ISO 14229, with asynchronous NvM writes. SchM provides exclusive areas with minimal interrupt-disable durations. RTE implements the task-boundary snapshot mechanism that's the core AUTOSAR solution to shared-data races. Each module's real AUTOSAR counterpart is documented in the README."

**Anticipating follow-ups:**
- *"Why not Zephyr?"* — FreeRTOS is conceptually closer to AUTOSAR OS (static priorities, preemptive scheduling, no POSIX abstraction). Zephyr adds abstractions that hide the scheduling model I wanted to make visible.
- *"How did you test it?"* — The race condition counter and DMA UART logging quantify the difference between explicit and implicit modes. Non-intrusive observation — the same technique Trace32 uses on real EPS hardware.
- *"What's different from real AUTOSAR?"* — MCAL, PduR, EcuM, BswM, NvM, Fee, Fls are intentionally simplified or omitted. Only the core mechanisms that *matter for understanding* are implemented.
- *"Why DMA for UART?"* — Non-blocking, deterministic, no priority inversion. The same reasons real AUTOSAR CAN drivers are DMA/hardware-driven.
- *"Why not OpenOCD?"* — The Cortex-Debug extension's direct ST-Link integration is sufficient and avoids OpenOCD's configuration overhead. Fewer moving parts, faster iteration.

The message this project sends is stronger than *"I studied AUTOSAR"* — it says **"I understood it, I implemented it, and I know the nuances that matter."**