# Setup Guide — Mini AUTOSAR BSW Demo

This guide walks you through getting the project to compile and run on your STM32F4 Discovery board.

## 1. Prerequisites (Ubuntu/Linux)

```bash
# ARM cross-compiler
sudo apt install gcc-arm-none-eabi gdb-multiarch

# Build tools
sudo apt install cmake make

# Flash & debug tool
sudo apt install openocd
```

**Windows**: Install GNU Arm Embedded Toolchain (from ARM website), CMake, and OpenOCD. Add all three to your PATH.

**macOS**: Use Homebrew:
```bash
brew install --cask gcc-arm-embedded
brew install cmake openocd
```

## 2. Required VS Code Extensions

- **C/C++** (Microsoft) — IntelliSense and code navigation
- **CMake Tools** (Microsoft) — build integration
- **Cortex-Debug** (marus25) — **REQUIRED** for STM32 debugging

Install all three from the VS Code Extensions marketplace before proceeding.

## 3. FreeRTOS and STM32 HAL Integration

The project ships as a **skeleton**. To make it run on real hardware, you need to add FreeRTOS and the STM32 HAL layer. Two options:

### Option A: Generate with STM32CubeMX (RECOMMENDED — fastest)

1. Open STM32CubeMX and start a new project for **STM32F407VG**
2. Configure peripherals:
   - **RCC**: HSE = Crystal/Ceramic Resonator
   - **SYS**: Debug = Serial Wire, Timebase Source = **TIM6** (critical for FreeRTOS — do not use SysTick)
   - **Clock Configuration**: Set system clock to 168 MHz
   - **USART2**: Asynchronous mode (PA2=TX, PA3=RX, baud 115200) — used for simulated COM Tx output
   - **Middleware → FreeRTOS**: Enable, Interface = CMSIS_V2
3. In Project Manager:
   - Toolchain/IDE: **Makefile**
   - Click Generate Code
4. From CubeMX's output folder, copy these into the project root:
   - `Core/Inc/` and `Core/Src/` → `stm32/`
   - `Drivers/` → `stm32/Drivers/`
   - `Middlewares/` → `stm32/Middlewares/`
   - `startup_stm32f407xx.s` → project root
   - `STM32F407VGTx_FLASH.ld` → project root

### Option B: Manual setup (longer but educational)

Download STM32CubeF4 from: https://github.com/STMicroelectronics/STM32CubeF4

Copy these folders into `stm32/`:
- `Drivers/STM32F4xx_HAL_Driver/`
- `Drivers/CMSIS/Device/ST/STM32F4xx/`
- `Drivers/CMSIS/Include/`
- `Middlewares/Third_Party/FreeRTOS/`

## 4. Update CMakeLists.txt

Uncomment the TODO sections in `CMakeLists.txt`:

```cmake
include_directories(
    # ... existing includes
    ${CMAKE_SOURCE_DIR}/stm32/Drivers/STM32F4xx_HAL_Driver/Inc
    ${CMAKE_SOURCE_DIR}/stm32/Drivers/CMSIS/Device/ST/STM32F4xx/Include
    ${CMAKE_SOURCE_DIR}/stm32/Drivers/CMSIS/Include
    ${CMAKE_SOURCE_DIR}/stm32/Middlewares/Third_Party/FreeRTOS/Source/include
    ${CMAKE_SOURCE_DIR}/stm32/Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2
    ${CMAKE_SOURCE_DIR}/stm32/Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F
    ${CMAKE_SOURCE_DIR}/stm32/Core/Inc
)

file(GLOB_RECURSE SOURCES
    ${CMAKE_SOURCE_DIR}/src/*.c
    ${CMAKE_SOURCE_DIR}/stm32/Core/Src/*.c
    ${CMAKE_SOURCE_DIR}/stm32/Drivers/STM32F4xx_HAL_Driver/Src/*.c
    ${CMAKE_SOURCE_DIR}/stm32/Middlewares/Third_Party/FreeRTOS/Source/*.c
    ${CMAKE_SOURCE_DIR}/stm32/Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F/port.c
    ${CMAKE_SOURCE_DIR}/stm32/Middlewares/Third_Party/FreeRTOS/Source/portable/MemMang/heap_4.c
    ${CMAKE_SOURCE_DIR}/startup_stm32f407xx.s
)
```

## 5. Enable TODOs in main.c

In `src/main.c`, uncomment the FreeRTOS-related sections:

```c
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f4xx_hal.h"

// Inside main():
HAL_Init();
SystemClock_Config();

BSW_Init();

xTaskCreate(Task_1ms,   "Task_1ms",   256, NULL, 4, NULL);
xTaskCreate(Task_10ms,  "Task_10ms",  256, NULL, 3, NULL);
xTaskCreate(Task_100ms, "Task_100ms", 256, NULL, 2, NULL);
xTaskCreate(Task_Demo,  "Task_Demo",  512, NULL, 1, NULL);

vTaskStartScheduler();
```

Also uncomment the `for(;;)` task loops in each `Task_*` function.

## 6. Build & Flash

```bash
# Configure and build
mkdir -p build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/arm-none-eabi.cmake ..
make -j$(nproc)

# Flash (ST-Link USB connected)
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
        -c "program mini_autosar_bsw.elf verify reset exit"
```

Or from VS Code:
- **Ctrl+Shift+B** → build
- **F5** → start debug (auto-flash + break at main)

## 7. UART Monitor

To see demo output, connect a USB-UART adapter to PA2 (TX) and GND, then:

```bash
# Linux
sudo screen /dev/ttyUSB0 115200

# or minicom
sudo minicom -D /dev/ttyUSB0 -b 115200
```

Expected output:
```
[DEMO] EXPLICIT mode — 5 seconds observation
[DEMO] Inconsistencies detected: 47
[DEMO] Switching to IMPLICIT mode
[DEMO] IMPLICIT mode — 5 seconds observation
[DEMO] Inconsistencies detected: 0
```

## 8. Observing the Race Condition in the Debugger

Start a debug session in VS Code and set a breakpoint on this line:
- `src/bsw/rte/Mini_Rte.c` → `rte_inconsistencyCount++;`

In **explicit mode**, this breakpoint will hit repeatedly — proving the race condition occurs.
In **implicit mode**, it will never hit — proving the AUTOSAR solution works.

You can also use ITM trace via the Cortex-Debug SWO console to get `printf`-style output without halting the CPU (a non-intrusive debug technique commonly used with Trace32).

## Troubleshooting

- **Build error**: verify all include paths and source file globs in `CMakeLists.txt`
- **Flash fails**: confirm ST-Link drivers are installed and the board is detected (`lsusb` should show ST-Link)
- **Debug won't start**: make sure OpenOCD version is 0.11 or newer
- **FreeRTOS hangs at startup**: check `SystemCoreClock` is set correctly and HSE is wired
- **Tasks don't run**: verify FreeRTOS timebase is **TIM6**, not SysTick (SysTick conflicts with HAL)
- **Hard fault on `malloc`**: FreeRTOS heap size too small — increase `configTOTAL_HEAP_SIZE` in `FreeRTOSConfig.h`

If you get stuck, paste the error message and we'll work through it together.
