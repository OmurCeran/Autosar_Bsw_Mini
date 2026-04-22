# 🏛️ ARCHITECTURE.md — Design Decisions and AUTOSAR Mapping

This document explains **why** each module in the `Autosar_Bsw_Mini` project is designed the way it is, and **what it corresponds to in a real-world AUTOSAR Classic architecture**. 

Use this document as a technical reference to understand the engineering trade-offs and mechanisms simulated in this project.

## 🎯 Overall Approach

This project is **not** a 1:1 replica of a production-grade AUTOSAR Classic Basic Software (BSW) stack — it is a **conceptual simulation**. The primary goals are:

1. **Understand the Core Mechanisms:** Abstract the extreme complexity of AUTOSAR into readable, fundamental mechanisms (e.g., how RTE implicit communication actually prevents race conditions).
2. **Demonstrate a Real Engineering Problem:** Implement a tangible, hardware-level problem (an EPS race condition causing torque miscalculation) with working, executable code.
3. **Provide a Portfolio Reference:** Create a tangible, presentable project for technical interviews demonstrating architectural awareness.
4. **Teach Through Comments:** The source code is documented not just to explain *"what"* a function does, but *"why"* it exists and *"what"* its real AUTOSAR equivalent is (e.g., `Rte_IRead` vs. `Rte_Read`).

---

## 🧩 Module-by-Module Design Notes

### 1. Mini_Rte — The Most Important Module

**Why is this module the core of the project?**
The core of the EPS debug story lives here. Both the root cause and the architectural solution of the race condition are implemented in this file.

**Two Supported Modes:**
- **`RTE_ACCESS_EXPLICIT`:** Software Components (SWCs) read/write directly from/to the global buffer. Preemption during these reads/writes leads to data corruption (Race Condition).
- **`RTE_ACCESS_IMPLICIT`:** A snapshot (local copy) of the data is taken at the start of the task (`Rte_Task_Begin`), and outputs are flushed at the end of the task (`Rte_Task_End`). The data remains strictly consistent throughout the task execution.

**In Real AUTOSAR:**
- This choice is made at the port level in tools like DaVinci Configurator (via the `dataElement.isReentrant` attribute).
- The RTE generator produces entirely different C macros based on this choice.
- The mode is fixed at compile-time.

**In This Demo:**
- The mode can be switched dynamically (purely for educational visibility).
- An inconsistency counter proves the race condition physically happens. Explicit mode shows continuous errors, while implicit mode completely eliminates them.

---

### 2. Mini_SchM — Exclusive Areas

**Critical Design Rule:** Exclusive area duration must be **MINIMAL**.

You will see this constraint documented in the code:
> *"Keep the code between Enter and Exit as SHORT as possible. Long exclusive areas cause timing violations in safety-critical systems."*

This constraint comes directly from real EPS (Electric Power Steering) safety requirements. Disabling interrupts for too long causes the 1ms safety task to miss its deadline, triggering WdgM (Watchdog Manager) deadline monitoring, which ultimately forces the MCU into a safe state (Reset).

**Interview FAQ:** *"Why didn't you use a FreeRTOS mutex to protect the data?"*
**Answer:** AUTOSAR OS does not typically use task-level blocking mutexes for BSW data protection. Shared BSW data is protected via SchM Exclusive Areas (which map to hardware interrupt disabling) or OS Resources (Priority Ceiling Protocol). For extremely short critical sections (e.g., copying 4 bytes), exclusive areas are deterministic and much faster, as they avoid RTOS context switch overhead.

---

### 3. Mini_Com — Signal Packing & Endianness

**Focus:** Bit-level payload packing and endianness handling.

Motorola (Big-Endian) byte order is the standard in automotive CAN networks. The code supports both Big-Endian and Little-Endian byte ordering, selectable via the configuration table.

**Simplifications Made for the Demo:**
- Only 16-bit byte-aligned signals are implemented (Real COM supports arbitrary bit-lengths across byte boundaries).
- The `PduR` (PDU Router) and `CanIf` (CAN Interface) modules are abstracted and simulated directly inside COM.
- Transmission mode is limited to `Periodic` (Real COM supports Direct, Mixed, and None).

**Key Concepts Demonstrated:**
- Signal-to-PDU mapping (`startBit`, `bitLength`, `endianness`).
- MainFunction-triggered transmission (A PDU is sent synchronously on the 10ms cycle, not instantly when a SWC writes a signal).

---

### 4. Mini_Dem — Debounce and DTC Status

**Counter-Based Debounce Algorithm:**
- `PREFAILED` report: counter += `step_up`
- `PREPASSED` report: counter -= `step_down`
- When the counter reaches `threshold_failed` ➔ **FAILED** status transition.
- When the counter drops to `threshold_passed` ➔ **PASSED** status transition.

**ISO 14229 DTC Status Byte (8-bit):**
The module simulates the standard UDS status byte. Each bit has a distinct meaning:
- `Bit 0 (TF)`: **TestFailed** — The fault is currently active.
- `Bit 3 (CDTC)`: **ConfirmedDTC** — The fault has matured and is stored in non-volatile memory.
- `Bit 7 (WIR)`: **WarningIndicatorRequested** — Turn on the dashboard MIL (Malfunction Indicator Lamp).

**Freeze Frame & NvM Interaction:**
- `Dem_SetEventStatus()` is a synchronous call that returns immediately.
- In the `Dem_MainFunction`, the `isStored` flag triggers an asynchronous `NvM_WriteBlock()` request.
- This demonstrates the standard AUTOSAR delayed-write pattern, preventing the CPU from stalling during Flash/EEPROM write cycles.

---

### 5. App_Swc — Runnable Simulation & Hardware Latency

**Three Runnables Mapped to Three RTOS Tasks:**
- **`1ms Task` (High Priority):** Torque Calculation (Safety-Critical, ASIL-D).
- **`10ms Task` (Medium Priority):** Sensor Update (The victim/source of the race condition).
- **`100ms Task` (Low Priority):** Diagnostic Monitor (Plausibility checks).

**The Intentional Preemption Window:**
```c
/* === PREEMPTION WINDOW === */
volatile uint32 delay;
for (delay = 0; delay < 100; delay++) { /* Simulate hardware read time */ }
