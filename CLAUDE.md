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

There is no unit test suite yet (`tests/` is empty); ring-buffer tests are planned (`TODO.md`).

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
