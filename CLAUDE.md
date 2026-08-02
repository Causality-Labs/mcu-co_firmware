# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Bare-metal firmware for the STM32G474RE (Cortex-M4F). Registers are driven directly
via CMSIS device headers — **there is intentionally no STM32 HAL**. The goal is to
understand the hardware, so do not introduce HAL/LL dependencies. C99, `arm-none-eabi-gcc`.

CMSIS Core and the STM32G4 device headers are pulled by CMake `FetchContent` at
configure time (see `CMakeLists.txt`); they are not vendored.

## Build / flash / analyze

All workflows go through `./buildmcu-co.sh` (wraps CMake; build dir is `build/`):

- `./buildmcu-co.sh -c` — clean, then `./buildmcu-co.sh -b` — configure + build.
  Always clean before building; do not rely on incremental builds.
- `./buildmcu-co.sh -l <error|warn|info|debug|off> -b` — set log verbosity for the
  build. `-l` must come **before** `-b`. Default is `debug`.
- `./buildmcu-co.sh -s <clang|cpp|both>` — run static analysis (clang-tidy CERT-C /
  cppcheck). Analysis is **off in a normal `-b` build** and only runs via `-s`.
  It must pass clean before code is considered done.
- `./buildmcu-co.sh -F` — format all sources with clang-format.
- `./buildmcu-co.sh -f st` — flash `mcu-co-firmware.bin` to `0x08000000` via st-flash.

Output is `-Werror` with `-Wconversion -Wsign-conversion -Wshadow -Wmissing-prototypes`
etc. — warnings fail the build. `serial_test.py` reads UART output over a serial port.

## Unit testing

Host-native tests using **CppUTest** (fetched via CMake `FetchContent`, pinned to
`v4.0`) live in `tests/unit-tests/`, a standalone CMake project separate from the
root one — the root `CMakeLists.txt` force-sets the ARM cross toolchain, so tests
build and run with the system `gcc`/`g++`, not `arm-none-eabi-gcc`.

- `./buildmcu-co.sh -t` — configure, build, and run the suite (in `build-tests/`,
  separate from the firmware's `build/`). Uses `ctest --verbose` so every
  individual `TEST(...)` name always prints, not just failures.
- All test files link into one `unit_tests` binary via `AllTests.cpp`
  (`CommandLineTestRunner::RunAllTests`) — CppUTest convention: one runner, not
  one binary per module. `add_test()` in `tests/unit-tests/CMakeLists.txt` bakes
  in `-v` permanently.
- Only pure-logic modules with **no CMSIS/register includes** are tested this
  way: `data_structures/ring-buffer.c`, `common/status.c`, `common/crc16.c`
  (`src/frame_parser.c` planned next). Peripheral drivers (`gpio.c`, `uart.c`,
  `rcc.c`) touch registers directly and need mocking
  (`CppUTestExt::MockSupportPlugin`, via `tests/unit-tests/fakes/`) before they
  can be tested this way.
- Test naming: `TEST(GroupName, FunctionBehaviorDescription)`, e.g.
  `WriteFailsWhenBufferIsFull`. Group every test file's cases under a
  `/* --- function_name --- */` comment per production function, with a
  one-line comment directly above each `TEST(...)` stating what it checks.
- Prioritize tests that guard the function's documented `@return`/error
  contract (NULL/zero/invalid-arg rejection) plus one happy-path test, over
  exhaustive coverage of every internal field or edge case.

### TDD workflow for new modules

When building a new module test-first (as opposed to retrofitting tests onto
existing code), follow *Test-Driven Development for Embedded C*'s cycle:

1. **Write a test list before any test code.** A plain-language checklist of
   scenarios per function, saved as `tests/unit-tests/<module>_test_list.md`
   (see `command_transport_test_list.md` for the shape). Not a spec — cross
   items off, or add new ones, as work proceeds. Flag any open design
   question directly in the list rather than guessing; resolve it in
   discussion before writing tests against it.
2. **One test at a time, red then green.** Write exactly one failing test,
   build and confirm it fails for the expected reason (compile error, link
   error, or assertion failure — not just "some" error), then write the
   *minimal* implementation to pass that one test — not code that
   anticipates tests not yet written. Confirm green before moving on.
3. **Default to single-step pacing.** Stop after each individual step (write
   test / confirm red / write implementation / confirm green) and wait for
   confirmation rather than chaining the whole cycle unprompted in one pass —
   even if a prior message said "start on X," that authorizes one step, not
   the rest of the list. The user may explicitly grant broader autonomy
   ("make the rest of the module with this approach") to proceed through
   remaining list items without pausing at each one; that grant doesn't
   carry over to the next module or session by default.
4. **Extending a fake is its own step.** If a test needs more from a spy
   (`tests/unit-tests/fakes/`) than it currently exposes, add that capability
   first, confirm it doesn't break existing tests, *then* write the new test
   against it — don't bundle fake changes into the same step as the test
   that needs them.
5. **Interface/protocol changes get discussed, not assumed.** If implementing
   a test reveals a real gap or ambiguity in a locked-in interface or wire
   protocol (e.g. a struct field needed that doesn't exist yet), stop and
   raise it rather than silently extending the design — these are
   collaborative decisions, and "locked in" interfaces have been revisited
   more than once when a test exposed a real gap.

## Compliance — this constrains how you write code

Target is **CERT-C** (enforced by clang-tidy `cert-*` as errors) plus cppcheck
`--enable=all`. MISRA-C is explicitly *not* a goal, which is why the generic ring
buffer's `void *` + `memcpy` pattern is acceptable. Practically this means:
bounds-check every index, validate all port/pin/enum arguments before use, avoid
implicit conversions, and prefer return-code error handling (`0` ok, `-1` error) —
the existing drivers all follow this.

## Architecture

Layered, with hardware access funnelled through single owners:

- **`peripherals/rcc.c`** owns *all* RCC register access. Other drivers enable their
  clock via `rcc_periph_enable()` / `rcc_periph_set_clock_source()` using the
  `rcc_periph_t` handle. **Never write `RCC->...` outside rcc.c** — the register/bit
  mapping lives only there. `rcc_init()` brings SYSCLK to 170 MHz (HSI16+PLL).
- **`peripherals/gpio.c`**, **`uart.c`** — drivers that take config structs and call
  into rcc for clocking. GPIO protects reserved pins (PA13–15, PB3/4) and validates
  pin direction on every operation.
- **`src/log/logger.c`** (`include/logger.h`) — single-producer bounded-queue logger.
  `LOG_INFO("MODULE", "msg")` etc. push to a ring buffer; `logger_flush()` drains it
  to UART and **must be called from the main loop**. Levels are compile-time macros:
  a call above `LOG_LEVEL` (or with `LOG_ENABLED=0`) compiles to `((void)0)` with
  arguments unevaluated. Set via the `-l` build flag above.
- **`data_structures/ring-buffer.c`** — generic `void *`/`memcpy` ring buffer used by
  the logger. Capacity must be a power of two (it uses a bitmask for wraparound).
- **`vendor/`** — startup `.s`, linker script (`STM32G474RETX_FLASH.ld`), and
  `system_stm32g4xx.c`. Compiled as a separate CMake OBJECT library specifically so
  static-analysis tools never run on it. **Treat `vendor/` as read-only.**

**ISR rule:** never call `logger_log`/`LOG_*` from an ISR (it is not ISR-safe and is
single-producer). An ISR sets a `volatile` flag/counter; the main loop reads it and
emits the log. See `button_ISR` + the main loop in `src/main.c` for the pattern.

When adding a new source file, register it in the `add_executable(...)` list in
`CMakeLists.txt` — there is no glob for build sources.

## Known config quirk

`.clang-tidy` `HeaderFilterRegex` and the clang-format `file(GLOB...)` in
`CMakeLists.txt` both reference a `drivers/` path, but driver code actually lives in
`peripherals/`. Header-level lint and auto-format currently miss `peripherals/*.h`;
fix the path rather than moving files if you touch this.
