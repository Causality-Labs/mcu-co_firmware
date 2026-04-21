cmake_minimum_required(VERSION 3.22)

set(CMAKE_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/cmake/toolchain-arm-none-eabi.cmake"
    CACHE PATH "Toolchain file" FORCE)

project(mcu-co-firmware VERSION 1.0
        DESCRIPTION "Firmware for mcu-co"
        LANGUAGES C ASM)

set(CMAKE_C_STANDARD 99)
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
  -std=c99
  -Wall
  -Wextra
  -Wconversion
  -Wsign-conversion
  -Wshadow
  -Wstrict-prototypes
  -Wmissing-prototypes
  -Werror
  -O0
  -g
  -fdata-sections
  -ffunction-sections
)

add_compile_definitions(STM32G474xx)

set(CMSIS_CORE    ${cmsis_5_SOURCE_DIR}/CMSIS/Core/Include)
set(CMSIS_DEVICE  ${cmsis_device_g4_SOURCE_DIR}/Include)

# Vendor code compiled separately so analysis tools are never applied to it
add_library(vendor OBJECT
  vendor/system_stm32g4xx.c
  vendor/startup_stm32g474xx.s
)
target_include_directories(vendor PRIVATE ${CMSIS_CORE} ${CMSIS_DEVICE})

# Enable analysis tools before add_executable so they apply only to our sources
option(ENABLE_CERT_CHECK "Run CERT-C compliance checks with clang-tidy" OFF)
option(ENABLE_CPPCHECK   "Run general static analysis with cppcheck"    OFF)

if(ENABLE_CERT_CHECK)
    find_program(CLANG_TIDY clang-tidy REQUIRED)
    set(CMAKE_C_CLANG_TIDY
        ${CLANG_TIDY}
        --extra-arg=-Wno-unused-command-line-argument
    )
endif()

if(ENABLE_CPPCHECK)
    find_program(CPPCHECK cppcheck REQUIRED)
    set(CMAKE_C_CPPCHECK
        ${CPPCHECK}
        --std=c99
        --enable=all
        --error-exitcode=1
        "--suppressions-list=${CMAKE_SOURCE_DIR}/cppcheck.supp"
        "--suppress=*:${CMAKE_BINARY_DIR}/_deps/*"
        -D__GNUC__
    )
endif()


add_executable(${PROJECT_NAME}
  src/main.c
  src/log/logger.c
  peripherals/uart.c
  peripherals/gpio.c
  peripherals/clock.c
  data_structures/ring-buffer.c
  $<TARGET_OBJECTS:vendor>
)

set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".elf")

target_include_directories(${PROJECT_NAME} PRIVATE
  include
  peripherals
  data_structures
  ${CMSIS_CORE}
  ${CMSIS_DEVICE}
)

target_link_options(${PROJECT_NAME} PRIVATE
  -T${CMAKE_SOURCE_DIR}/vendor/STM32G474RETX_FLASH.ld
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

find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
    file(GLOB_RECURSE FORMAT_SOURCES
        "${CMAKE_SOURCE_DIR}/src/*.c"
        "${CMAKE_SOURCE_DIR}/drivers/*.c"
        "${CMAKE_SOURCE_DIR}/drivers/*.h"
        "${CMAKE_SOURCE_DIR}/include/*.h"
    )
    add_custom_target(format
        COMMAND ${CLANG_FORMAT} -i ${FORMAT_SOURCES}
        COMMENT "Formatting sources with clang-format"
    )
    add_custom_target(format-check
        COMMAND ${CLANG_FORMAT} --dry-run --Werror ${FORMAT_SOURCES}
        COMMENT "Checking source formatting with clang-format"
    )
endif()