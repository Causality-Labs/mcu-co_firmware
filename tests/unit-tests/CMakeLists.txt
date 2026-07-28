cmake_minimum_required(VERSION 3.22)
project(mcu-co-firmware-tests LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(CppUTest
  GIT_REPOSITORY https://github.com/cpputest/cpputest.git
  GIT_TAG        v4.0
  GIT_SHALLOW    TRUE
)
set(CPPUTEST_PLATFORM Gcc CACHE STRING "Platform implementation")
set(TESTS OFF CACHE BOOL "Compile and make tests for CppUTest itself" FORCE)
FetchContent_MakeAvailable(CppUTest)

# gpio.h pulls in the CMSIS device header for its register types. We never
# call the register-touching functions ourselves (gpio_spy.c stands in for
# all of them), so this only needs to parse, not generate real ARM code -
# confirmed it compiles clean under the host compiler.
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
set(CMSIS_CORE   ${cmsis_5_SOURCE_DIR}/CMSIS/Core/Include)
set(CMSIS_DEVICE ${cmsis_device_g4_SOURCE_DIR}/Include)

enable_testing()

set(FIRMWARE_ROOT ${CMAKE_SOURCE_DIR}/../..)

# All TEST_GROUP/TEST cases self-register with CppUTest, so every test file and
# every production source it exercises links into one AllTests runner. Add a
# line to each list below as coverage grows.
add_executable(unit_tests
  AllTests.cpp
  test_ring_buffer.cpp
  test_status.cpp
  test_crc16.cpp
  test_frame_parser.cpp
  test_gpio_controller.cpp
  test_gpio_irq_bindings.cpp
  test_logger.cpp

  ${FIRMWARE_ROOT}/data_structures/ring-buffer.c
  ${FIRMWARE_ROOT}/common/status.c
  ${FIRMWARE_ROOT}/common/crc16.c
  ${FIRMWARE_ROOT}/src/frame_parser.c

  ${FIRMWARE_ROOT}/src/gpio_controller.c
  ${FIRMWARE_ROOT}/src/gpio_irq_bindings.c
  fakes/gpio_spy.c

  ${FIRMWARE_ROOT}/src/logger.c
  fakes/uart_spy.c
)

target_include_directories(unit_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/fakes
  ${FIRMWARE_ROOT}/data_structures
  ${FIRMWARE_ROOT}/common
  ${FIRMWARE_ROOT}/include
  ${FIRMWARE_ROOT}/peripherals
  ${CMSIS_CORE}
  ${CMSIS_DEVICE}
)

# gpio_controller.c calls LOG_ERROR(); stripping the logger keeps this test
# scoped to gpio_controller's one real collaborator (gpio.h) instead of also
# depending on the logger/ring-buffer chain.
target_compile_definitions(unit_tests PRIVATE STM32G474xx LOG_ENABLED=0)

target_link_libraries(unit_tests PRIVATE CppUTest CppUTestExt)

add_test(NAME unit_tests COMMAND unit_tests -v)

# command_dispatcher.c's tests need gpio_controller.h faked (GpioControllerSpy),
# but gpio_controller.c is REAL in unit_tests above - a fake and the real file
# can't both define gpio_controller_io_cfg() in one binary. Separate executable,
# per TDD for Embedded C's own "Smaller Test Builds" (Appendix A1.3): split into
# multiple test binaries when different test subjects need conflicting fakes for
# the same collaborator.
add_executable(test_command_dispatcher
  command_dispatcher_main.cpp
  test_command_dispatcher.cpp

  ${FIRMWARE_ROOT}/src/command_dispatcher.c
  fakes/gpio_controller_spy.c
)

target_include_directories(test_command_dispatcher PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/fakes
  ${FIRMWARE_ROOT}/include
  ${FIRMWARE_ROOT}/common
)

target_compile_definitions(test_command_dispatcher PRIVATE LOG_ENABLED=0)

target_link_libraries(test_command_dispatcher PRIVATE CppUTest CppUTestExt)

add_test(NAME test_command_dispatcher COMMAND test_command_dispatcher -v)
