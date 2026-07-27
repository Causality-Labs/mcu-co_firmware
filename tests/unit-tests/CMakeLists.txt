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

enable_testing()

set(FIRMWARE_ROOT ${CMAKE_SOURCE_DIR}/../..)

# All TEST_GROUP/TEST cases self-register with CppUTest, so every test file and
# every production source it exercises links into one AllTests runner. Add a
# line to each list below as coverage grows.
add_executable(unit_tests
  AllTests.cpp
  test_ring_buffer.cpp

  # test_status.cpp
  # test_crc16.cpp
  # test_frame_parser.cpp

  ${FIRMWARE_ROOT}/data_structures/ring-buffer.c
  ${FIRMWARE_ROOT}/common/status.c

  # ${FIRMWARE_ROOT}/common/crc16.c
  # ${FIRMWARE_ROOT}/src/frame_parser.c
)

target_include_directories(unit_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/fakes
  ${FIRMWARE_ROOT}/data_structures
  ${FIRMWARE_ROOT}/common
  ${FIRMWARE_ROOT}/include
)

target_link_libraries(unit_tests PRIVATE CppUTest CppUTestExt)

add_test(NAME unit_tests COMMAND unit_tests -v)
