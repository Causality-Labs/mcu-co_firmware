# command_dispatcher Test List

Plain-language scenarios to cover for `src/command_dispatcher.c` — per *Test-Driven
Development for Embedded C* (Ch. 3.3, "Write a Test List"). Not a spec, just a working
checklist; cross items off (or add new ones) as tests get written.

The 13 items already checked below were written against the original `switch`
implementation and survived the move to `COMMAND_TABLE` unedited — that is what proves
the restructure was behaviour-neutral.

## What this module is

`dispatch_command()` owns no command logic. Every opcode is one row in `COMMAND_TABLE`,
and the function does four things: look the opcode up, call the row's handler, serialise
the value a read produced, and set ACK or NACK. So the tests are about *routing and
reply shape*, never about what a command does to the hardware — that belongs to
`test_gpio_controller` / `test_pwm_controller`.

```c
typedef status_t (*command_action_fn)(const uint8_t *payload, uint8_t length);
typedef status_t (*command_read_fn)(const uint8_t *payload, uint8_t length, uint32_t *value);
```

A row sets exactly one of the two. Action rows point straight at their controller
function. Read rows go through a small static adapter — `read_gpio_pin()` for
`gpio_controller_read()`'s `bool *`, and one to come for `pwm_controller_channel_get()`'s
`uint16_t *` — because the controllers keep their own domain types rather than widening
everything to the table's transport width. `pwm_controller_group_get()` is already
`uint32_t *` and needs no adapter.

The adapters are worth one test each (below); they are the only place a read's value is
converted, so a wrong conversion there is invisible everywhere else.

Tests run in the **separate `test_command_dispatcher` binary**, against
`fakes/gpio_controller_spy.c` and `fakes/pwm_controller_spy.c` — the real controllers are
linked into `unit_tests`, and one binary can't hold both a fake and the real definition
of the same symbol.

## Shared across every opcode

- [x] Rejects a NULL frame
- [x] Rejects a NULL response
- [x] An unknown opcode NACKs with `STATUS_ERR_UNSUPPORTED` and calls **no** controller
      at all (the spy must record zero calls, not just a failing one)
- [x] A controller failure is propagated as the return value and leaves `ack` false
- [x] A NACK carries the failing `status_t` as its single DATA byte
- [x] An action opcode ACKs with `data_len` 0

## Routing — one per opcode

Each of these asserts the same three things: the right controller function was called,
the frame's payload and length reached it unchanged, and the reply ACKed.

- [x] `GPIO_CFG` 0x30 → `gpio_controller_io_cfg`  *(also the payload-forwarding case)*
- [x] `GPIO_WRITE` 0x31 → `gpio_controller_write`
- [x] `GPIO_READ` 0x32 → `gpio_controller_read`, via `read_gpio_pin()`
- [x] `GPIO_IRQ_BIND` 0x33 → `gpio_controller_irq_bind`
- [x] `GPIO_IRQ_CFG` 0x34 → `gpio_controller_irq_cfg`
- [x] `GPIO_IRQ_UNBIND` 0x35 → `gpio_controller_irq_unbind`
- [x] `PWM_GROUP_CFG` 0x40 → `pwm_controller_group_cfg`
- [x] `PWM_CFG` 0x41 → `pwm_controller_channel_cfg`
- [x] `PWM_SET` 0x42 → `pwm_controller_channel_set`
- [x] `PWM_RELEASE` 0x43 → `pwm_controller_channel_release`
- [x] `PWM_GET` 0x44 → `pwm_controller_channel_get`, via its adapter
- [x] `PWM_GROUP_GET` 0x45 → `pwm_controller_group_get`
- [x] `PWM_GROUP_RELEASE` 0x46 → `pwm_controller_group_release`

Only one of these needs to assert payload forwarding in detail; the rest would be
asserting `record_call()` twice. The forwarding case is already `GPIO_CFG`.

## Read replies — the DATA field

`data_len` in a row is the **DATA width**, not the frame's `LEN` (which is `1 + data_len`,
the ACK byte included). Values below are chosen so a byte-swapped or truncated
serialisation cannot pass.

- [x] `GPIO_READ` ACK carries 1 byte, `0x01` when the pin reads high
- [x] `GPIO_READ` ACK carries `0x00` when the pin reads low — the existing test only
      covers high, so a hardcoded `1` still passes today
- [x] `PWM_GET` ACK carries 2 bytes, little-endian (duty 375 → `77 01`, never `01 77`)
- [x] `PWM_GROUP_GET` ACK carries 4 bytes, little-endian (100000 Hz → `A0 86 01 00`)
- [x] A failed read replies with the reason byte, not the value it would have read
- [x] A failed read leaves `data_len` at 1 (the reason), not the row's read width — a
      host sizing the frame off `LEN` must not be told to expect 4 bytes on a NACK

## Table invariants — dropped

Four properties of `COMMAND_TABLE` itself were written, passed, and then removed: every
row sets exactly one of `action`/`read`; no opcode appears twice; every `data_len` is
`<= TX_DATA_MAX`; every `data_len` matches the protocol's per-opcode width.

Reaching a `static const` table from a test needs either a `STATIC` macro that drops the
keyword under a `UNIT_TEST` build, a production accessor, or including the `.c` in the
test. All three put test scaffolding into production code for four checks a reviewer can
make by eye, so the table is reviewed rather than tested.

What this leaves uncovered: a `data_len` that drifts from `mcu-co_Protocol.md` misparses
every frame for that opcode on the wire while the tests above still pass. The routing and
serialisation tests each pin one row's width, so a drift is caught for any opcode that has
a test — it is a *new* row landing with the wrong width that nothing catches.

## Open questions — resolve before writing the tests they touch

1. ~~**How do the invariant tests reach `COMMAND_TABLE`?**~~ Resolved: the `STATIC` macro
   in `common/unit_test_access.h`, which expands to `static` everywhere except a build
   that defines `UNIT_TEST`. The table and its length carry `STATIC`; the typedefs and the
   guarded `extern` declarations moved to `src/command_dispatcher_internal.h`, which is not
   part of the module's public interface. `include/command_dispatcher.h` is unchanged, and
   `nm` on the firmware object confirms `COMMAND_TABLE` is still a local symbol there.
2. **Should a value wider than its row's `data_len` be an error?** `store_le()` currently
   truncates silently. It cannot happen with today's rows (duty is 0–1000, frequency is
   32-bit into 4 bytes), so this may be a decision to record rather than code to write.
3. **Extending `PwmControllerSpy_SetDuty()` to `uint32_t`** — it stores `uint16_t`, which
   matches `pwm_controller_channel_get()` and covers every duty the protocol allows. Only
   needed if question 2 turns into a truncation test. Its own step if so.

## Not this module's job

- Payload length and field ranges — every controller validates its own, and
  `dispatch_command()` forwards `frame->length` untouched. Testing a bad length here
  would only re-test the spy.
- Frame framing, CRC, and SOF resync — `frame_parser`.
- Serialising the response to bytes — `frame_parser_serialize_response()`.
