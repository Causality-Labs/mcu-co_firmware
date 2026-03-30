cmake_minimum_required(VERSION 3.22)

set(CMAKE_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/cmake/toolchain-arm-none-eabi.cmake"
    CACHE PATH "Toolchain file" FORCE)

project(mcu-co-firmware VERSION 1.0
        DESCRIPTION "Firmware for mcu-co"
        LANGUAGES C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

FetchContent_Declare(CMSIS_5
  GIT_REPOSITORY https://github.com/ARM-software/CMSIS_5.git
  GIT_TAG        55b19837f5703e418ca37894d5745b1dc05e4c91
  GIT_SHALLOW    TRUE
)

FetchContent_Declare(cmsis_device_g4
  GIT_REPOSITORY https://github.com/STMicroelectronics/cmsis_device_g4.git
  GIT_TAG        v1.2.6
  GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(CMSIS_5 cmsis_device_g4)

add_compile_options(
  -Wall
  -Wextra
  -Werror
  -Os
  -g
  -fdata-sections
  -ffunction-sections
)

add_compile_definitions(STM32G474xx)

set(CMSIS_CORE    ${cmsis_5_SOURCE_DIR}/CMSIS/Core/Include)
set(CMSIS_DEVICE  ${cmsis_device_g4_SOURCE_DIR}/Include)

set(SOURCES
  src/main.c
  src/system_stm32g4xx.c
  startup/startup_stm32g474xx.s
  drivers/uart.c
  drivers/gpio.c
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
  include
  drivers
  ${CMSIS_CORE}
  ${CMSIS_DEVICE}
)

target_link_options(${PROJECT_NAME} PRIVATE
  -T${CMAKE_SOURCE_DIR}/linker/STM32G474RETX_FLASH.ld
  -Wl,--gc-sections
  -Wl,--print-memory-usage
  --specs=nano.specs
  --specs=nosys.specs
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:${PROJECT_NAME}> ${PROJECT_NAME}.hex
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${PROJECT_NAME}> ${PROJECT_NAME}.bin
  COMMAND ${CMAKE_SIZE}              $<TARGET_FILE:${PROJECT_NAME}>
)