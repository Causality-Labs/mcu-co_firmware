# logger Test List

Plain-language scenarios for the deferred-format logger, written before the
test code — per *Test-Driven Development for Embedded C* (Ch. 3.3, "Write a
Test List"). Not a spec, just a working checklist; cross items off (or add new
ones) as tests get written.

This retrofits onto existing code: `tests/unit-tests/test_logger.cpp` already
has six passing tests and gets **extended**, not replaced. The existing
bracketed-format, level-label, FIFO-order and drain tests must keep passing
unchanged.

**Scope of this list is the deferred-format change only.** Flush latency (the
~3.5 ms the main loop spends spinning on the UART per log line) is separate
work with its own list.

## The design under test

`logger_log()` stores pointers to the module tag and format string — both
string literals, which live in flash — plus up to `LOG_MAX_ARGS` (6) argument
words copied by value. Nothing is formatted at enqueue. `logger_flush()` runs
`snprintf` into one shared line buffer and emits the result.

```c
typedef struct {
    log_level_t level;
    const char *module;
    const char *fmt;
    uint32_t    args[LOG_MAX_ARGS];
} log_entry_t;                        /* 36 B; 16-deep queue = 576 B */
```

Verified while designing (arm-none-eabi, project flags, `-Werror`):

- `argc` must precede `fmt` in the signature — `fmt` rides inside
  `__VA_ARGS__` and cannot be split out without the GNU `##__VA_ARGS__`
  extension. Attribute is `format(printf, 4, 5)`; type checking still works.
- The argument-counting macro **silently returns a caller's argument value**
  when it overflows, so the slot list is padded with an undeclared
  `LOG_TOO_MANY_ARGS` to turn overflow into a build error. Confirmed: 0/1/3/6
  args count correctly, 7 fails to compile.
- `-Wformat-nonliteral` must be scoped off around the one `snprintf` call in
  `logger_flush()`, since the stored `fmt` is not a literal *there*.
- The whole sketch compiles clean under every project flag.

## Argument capture at enqueue

The point of the design: values are copied when logged, read when flushed.

- [ ] A variable changed *after* the log call but *before* the flush still
      emits the value it had at log time
- [ ] A value logged from a local that goes out of scope before the flush is
      still emitted correctly
- [ ] Two entries logged from the same variable at different values emit their
      own distinct values, not the last one twice

## Formatting at flush

- [ ] A format string with no conversions emits verbatim (covers all ~41
      existing call sites — already tested, must not regress)
- [ ] One `%u` argument substitutes
- [ ] One `%d` argument substitutes, including a negative value
- [ ] Several conversions in one format all substitute, in order
- [ ] A call using all six argument slots substitutes all six
- [ ] `%%` emits a literal `%`
- [ ] `%08lx` zero-pads a register-style hex dump
- [ ] A `%s` argument whose pointer is a literal substitutes correctly

## Unused argument slots

`logger_flush()` always passes all six slots to `snprintf`, so the unused ones
must be reliably zero and must never carry data from a previous entry.

- [ ] An entry logged with fewer arguments than the previous entry does not
      emit the previous entry's values
- [ ] A zero-argument entry queued behind a six-argument entry emits cleanly

## Argument count handling

- [ ] `LOG_ARGC()` counts correctly for 0, 1, and 6 arguments (a pure
      compile-time expression, so it can be asserted directly in a test)
- [ ] `logger_log()` called directly with an `argc` larger than
      `LOG_MAX_ARGS` clamps instead of over-reading the `va_list`
- [ ] `logger_log()` called directly with `argc` smaller than the format's
      conversion count emits zeros rather than reading past the arguments

## Truncation

- [ ] A formatted line longer than `LOG_LINE_MAX` is cut and ends in `"..."`
- [ ] A line that fits exactly is **not** marked
- [ ] A truncated line is still NUL-terminated and the emitted output is still
      `\r\n` terminated with no stray bytes
- [ ] The module tag is emitted directly, not through the line buffer, so it
      is never truncated regardless of length (confirm this is intended)

## Argument validation — existing behaviour, must not regress

- [ ] NULL module is ignored — nothing enqueued, nothing emitted
- [ ] NULL format string is ignored the same way
- [ ] Logging before `logger_init()` is ignored
- [ ] A negative `snprintf` return drops the entry rather than emitting garbage

## Open questions — resolve before writing tests against them

- [ ] **The test binary sets `LOG_ENABLED=0`**
      (`tests/unit-tests/CMakeLists.txt:82`, so `gpio_controller.c`'s logging
      is stripped). Every `LOG_*` macro is therefore a no-op in `unit_tests`,
      which is why `test_logger.cpp` calls `logger_log()` directly. That means
      the macro layer — variadic expansion, the two-argument form, arguments
      left unevaluated when filtered by level — **cannot be tested in this
      binary at all**. Options: a separate test executable built with
      `LOG_ENABLED=1` (there is precedent — `test_command_dispatcher` exists
      for a similar conflict), or accept macro behaviour as compile-time-only
      and cover just `LOG_ARGC()`.
- [ ] **The host tests do not validate the ABI assumption.** Pulling every
      argument as `uint32_t` and letting `snprintf` reinterpret it works
      because `int`, `unsigned` and pointers share one 4-byte slot on
      arm-none-eabi. The unit tests run on x86-64, where varargs use 8-byte
      slots — passing there does *not* prove it works on target. Needs a
      hardware smoke test (or at least one flashed run) before this is
      trusted.
- [ ] `LOG_LINE_MAX` value — 96 was the working number, but it is now a single
      shared stack buffer in `logger_flush()` rather than per-entry storage, so
      a larger value costs almost nothing.
- [ ] `test_logger.cpp:9` documents that `logger_initialized` is a private
      static with no reset hook, so "log before init" is not independently
      testable. Add a `logger_deinit()` (or test-only reset) while we are in
      this file, or leave the gap?
- [ ] Should `%s` arguments be documented as literal-only, and is there any way
      to enforce it? `-Wformat-nonliteral` constrains the *format string*, not
      a `%s` argument, so a stack string passed to `%s` would still dangle.

## Follow-on, once green

- [ ] Enable `-Wformat-nonliteral` project-wide, with the scoped pragma in
      `logger_flush()`.
- [ ] Enrich existing call sites with the values they currently describe only
      in prose — `gpio_controller.c`'s `"Invalid length"` becomes
      `"invalid length %u"`, dispatcher errors gain the opcode.
- [ ] Measure enqueue cost on hardware with the DWT cycle counter, before and
      after, to confirm the design actually met the latency goal.
