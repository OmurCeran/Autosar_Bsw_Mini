# ARCHITECTURE.md — Design Decisions and AUTOSAR Mapping

This document explains **why** each module is designed the way it is, and **what it corresponds to in real AUTOSAR**. Use this as a reference when discussing the project in interviews.

## Overall Approach

This project is **not** a replica of real AUTOSAR Classic BSW — it is a **conceptual summary**. The goals are:

1. Understand the *mechanisms* behind BSW modules
2. Demonstrate a real engineering problem (EPS race condition) with working code
3. Create a tangible, presentable reference project for interviews and portfolio

## Module-by-Module Design Notes

### Mini_Rte — The Most Important Module

**Why is this module the most important?**
The core of the EPS debug story lives here. Both the cause and the solution of the race condition are implemented in this file.

**Two supported modes:**
- `RTE_ACCESS_EXPLICIT`: SWCs read/write directly from/to the global buffer — race condition is possible
- `RTE_ACCESS_IMPLICIT`: A snapshot is taken at task start, flushed at task end — always consistent

**In real AUTOSAR:**
- This choice is made at port level in DaVinci (via the `dataElement.isReentrant` attribute on `SenderReceiverInterface`)
- The RTE generator produces different code for each mode
- The mode is fixed at configuration time, not runtime

**In this demo:**
- The mode can be switched at runtime (purely for educational purposes)
- An inconsistency counter proves the race condition really happens
- Explicit mode shows dozens of errors, implicit mode shows zero — the difference is dramatic and visible

### Mini_SchM — Exclusive Area

**Critical design rule:** exclusive area duration must be MINIMAL.

This is called out multiple times in the code:
```c
/* Keep the code between Enter and Exit as SHORT as possible.
 * Long exclusive areas cause timing violations in safety-critical systems.
 * In EPS: max exclusive area duration ~ 5-10 microseconds. */
```

This constraint comes directly from real EPS project experience. Disabling interrupts for too long causes the 1ms task to miss its deadline, which triggers WdgM deadline monitoring, which in turn triggers a safety reaction.

**Interviewer may ask:** "Why didn't you use a FreeRTOS mutex?"
**Answer:** AUTOSAR OS does not have task-level mutexes. Shared data is protected via SchM exclusive areas (interrupt disable) or OS resources (priority inheritance). For short critical sections, exclusive areas are much faster — there is no context switch overhead.

### Mini_Com — Signal Packing

**Focus:** bit-level packing and endianness handling.

Motorola (big-endian) byte order is standard in automotive CAN. The code supports both big-endian and little-endian — the choice is made through the configuration table.

**Simplifications made:**
- Only 16-bit byte-aligned signals (real COM supports arbitrary bit-lengths)
- PduR and CanIf are not separate modules — they are simulated inside COM
- Transmission mode is periodic only (real COM also supports direct, mixed, and none)

**Concepts demonstrated:**
- Signal-to-PDU mapping (startBit, bitLength, endianness)
- MainFunction-triggered transmission (PDU is sent on the 10ms cycle, not immediately when a signal is written)
- Rx indication callback pattern

### Mini_Dem — Debounce and DTC Status

**Counter-based debounce algorithm:**
- PREFAILED report: counter += step_up
- PREPASSED report: counter += step_down (negative value)
- When counter reaches threshold_failed → FAILED transition
- When counter drops to threshold_passed → PASSED transition

**ISO 14229 DTC status byte (8 bits):**
Each bit has a specific meaning. The code defines a macro for each:
- Bit 0 (TF): testFailed — fault is currently active
- Bit 3 (CDTC): confirmedDTC — confirmed and stored in memory
- Bit 7 (WIR): warningIndicatorRequested — MIL lamp

**Freeze frame mechanism:**
- The SWC continuously updates the freeze frame source data via `Dem_SetFreezeFrameData()`
- When an event transitions to FAILED, the current source data is copied into that event's freeze frame
- A UDS tester can later read this data with service `0x19 04`

**Asynchronous NvM write:**
- `Dem_SetEventStatus()` returns immediately (synchronous call)
- In the MainFunction, the `isStored` flag is checked and `NvM_WriteBlock()` is called if needed
- NvM's own MainFunction then writes to flash asynchronously
- The full chain (DEM → NvM → MemIf → Fee → Fls) takes milliseconds in a real system

### App_Swc — Runnable Simulation

**Three runnables, three tasks:**
- **1ms (high priority)**: Torque calculation — safety critical, ASIL-D
- **10ms (medium priority)**: Sensor update — the source of the race condition
- **100ms (low priority)**: Diagnostic monitor — sensor plausibility checks

**The preemption window is deliberately wide:**
```c
/* === PREEMPTION WINDOW === */
volatile uint32 delay;
for (delay = 0; delay < 100; delay++) { /* Simulate sensor read time */ }
```

This delay is intentional. In real hardware, this gap would be filled by an ADC conversion (10–50µs) or an I2C transfer (100–500µs). The demo amplifies it so that the race condition is observable.

**Torque calculation formula:**
```c
motor_cmd = torque_input * calib_coeff * (100.0f / vehicle_speed);
```

Low speed → high assist (parking maneuvers); high speed → low assist (highway). When a race condition causes the wrong `vehicle_speed` to be read, the assist level is miscalculated — this is the exact mechanism behind the "momentary steering heaviness" that the driver feels in a real EPS fault.

## Lessons Reflected in the Design

Things this project is deliberately careful about:

1. **Minimize global state** — variables are module-local `static` whenever possible
2. **Use const configuration tables** — mirrors real AUTOSAR generator output: data is constant, code is generic
3. **Use exclusive areas deliberately** — not everywhere, only where real shared-data access needs protection
4. **Mark TODOs clearly** — every place awaiting FreeRTOS integration is flagged
5. **Teach through comments** — not just "what it does" but "why" and "what the real AUTOSAR equivalent is"

## How to Present This Project in an Interview

Use this structure when walking through the project:

**Opening (30 seconds):**
"I wanted to deepen my understanding of the internal mechanisms of AUTOSAR Classic BSW. Since commercial stacks aren't available individually, I implemented conceptual versions of the core modules on STM32F4 + FreeRTOS."

**Demo (1 minute):**
"It includes a working demonstration of the race condition I encountered in my EPS project. The demo switches between explicit and implicit data access modes and uses an inconsistency counter to show the difference clearly."

**Depth (2 minutes):**
"COM performs bit-level signal packing, DEM manages debounce counters and DTC status bytes, SchM provides exclusive areas, and RTE implements the task-boundary snapshot mechanism. Each module is documented with its real AUTOSAR counterpart in the README."

**Prepare for follow-ups:**
- *"Why not Zephyr?"* → FreeRTOS is conceptually closer to AUTOSAR OS (priority-based preemptive scheduling, no POSIX abstraction)
- *"How did you test it?"* → The race condition counter measures the difference between explicit and implicit modes quantitatively
- *"What's different from real AUTOSAR?"* → MCAL, PduR, EcuM, BswM, NvM, Fee, Fls are missing; only the core mechanisms are shown

This project sends a stronger signal than "I studied AUTOSAR" — it says **"I understood it, I implemented it, and I know the nuances that matter."**
