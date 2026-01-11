# Generated Cmake ES Lab-Kit project file for RP2350, based on Pico project.

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2 CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

# Pull in the FreeRTOS kernel
set(FREERTOS_KERNEL_PATH "../../../FreeRTOS-Kernel")
include(FreeRTOS_Kernel_import.cmake)

project(Lab2_B C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Create a variable with all BSP source files and print the list when running CMake.
file(GLOB BSP_SOURCES "../../bsp/*.c")
message(BSP_SOURCES="${BSP_SOURCES}")

# Add executable. Default name is the project name, version 0.1
include_directories(../../bsp) # Add include files for the bsp
add_executable(Lab2_B main.c ${BSP_SOURCES} dijkstra.c input.c)

pico_set_program_name(Lab2_B "Lab2_B")
pico_set_program_version(Lab2_B "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(Lab2_B 1)
pico_enable_stdio_usb(Lab2_B 0)

# Add the standard library to the build
target_link_libraries(Lab2_B
        pico_stdlib
        pico_multicore
        hardware_spi
        hardware_i2c
        hardware_gpio
        hardware_pwm
        hardware_uart
        FreeRTOS-Kernel-Heap4
        pico_rand)

# Add the standard include files to the build
target_include_directories(Lab2_B PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

pico_add_extra_outputs(Lab2_B)
