cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME AUTOSAR_Mini)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/demo_bsw/bsw/Mini_Rte.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/demo_bsw/bsw/Mini_SchM.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/demo_bsw/bsw/Mini_Dem.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/demo_bsw/bsw/Mini_Com.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/demo_bsw/bsw/Mini_Timestamp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/demo_bsw/bsw/Mini_EcuM.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/demo_bsw/bsw/Mini_Nvm.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/demo_bsw/bsw/Mini_FaultInj.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/demo_bsw/bsw/Mini_Dcm.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/app/App_Swc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Segger_Lib/Src/SEGGER_SYSVIEW.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Segger_Lib/Src/SEGGER_SYSVIEW_FreeRTOS.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Segger_Lib/Src/SEGGER_SYSVIEW_Config_FreeRTOS.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Segger_Lib/Src/SEGGER_RTT.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Segger_Lib/Src/SEGGER_RTT_printf.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Segger_Lib/Src/SEGGER_RTT_ASM_ARMv7M.S
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/app/freertos_Helper.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/app/Debug_Helper.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/app
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/bsw/com/Mini_Com.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/bsw/dem/Mini_Dem.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/bsw/timestamp/Mini_Timestamp.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/bsw/schm/Mini_SchM.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/bsw/rte/Mini_Rte.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/bsw/ecum/Mini_Ecum.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/bsw/nvm/Mini_Nvm.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/bsw/fault/Mini_FaultInj.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/bsw/dcm/Mini_Dcm.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/Segger_Lib/Inc/Global.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/Segger_Lib/Inc/SEGGER.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/Segger_Lib/Inc/SEGGER_SYSVIEW.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/Segger_Lib/Inc/SEGGER_SYSVIEW_Conf.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/Segger_Lib/Inc/SEGGER_SYSVIEW_ConfDefaults.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/Segger_Lib/Inc/SEGGER_SYSVIEW_FreeRTOS.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/Segger_Lib/Inc/SEGGER_SYSVIEW_Int.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/Segger_Lib/Inc/SEGGER_RTT.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/Segger_Lib/Inc/SEGGER_RTT_Conf.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/app/freertos_Helper.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/app/Debug_Helper.h
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
