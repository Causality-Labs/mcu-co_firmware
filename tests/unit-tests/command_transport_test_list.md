# command_transport Test List

Plain-language scenarios to cover for `command_transport.c`, written before any
test code — per *Test-Driven Development for Embedded C* (Ch. 3.3, "Write a
Test List"). Not a spec, just a working checklist; cross items off (or add new
ones) as tests get written.

Interface: `command_transport_init(instance)`, `command_transport_deinit(void)`,
`command_transport_receive(data)`, `command_transport_send(frame, length)`.
`send()` is pure I/O — it forwards bytes to `uart_write_buffer()`
unmodified, symmetric with `receive()` wrapping `uart_read_byte()`. It does not
build/encode the response frame itself; that's `frame_parser`'s job (not yet
implemented — see below).

## command_transport_init(instance)

- [x] Calls `uart_init()` with the given instance
- [x] Returns `STATUS_OK` on success
- [x] Propagates `uart_init()`'s failure status if it fails
- [x] Stores the instance for `receive()`/`send()` to use afterward
      (implicit — covered by the calls-with-stored-instance tests below)

## command_transport_deinit(void)

- [x] Calls `uart_deinit()` and clears the initialised state, when previously
      initialised
- [x] Returns `STATUS_OK` even when never initialised (idempotent no-op, not
      an error) — specifically so test `setup()` can call it unconditionally
- [x] Propagates `uart_deinit()`'s failure status when it was initialised and
      the underlying deinit fails

## command_transport_receive(data)

- [x] Returns `STATUS_ERR_NOT_INIT` if called before `init()` (or after
      `deinit()`)
- [x] Rejects a NULL `data` pointer (once initialised)
- [x] Calls `uart_read_byte()` using the instance stored at init
- [x] Returns `STATUS_OK` and the byte when one is available
- [x] Returns `STATUS_ERR_EMPTY` when nothing's available

## command_transport_send(frame, length)

- [x] Returns `STATUS_ERR_NOT_INIT` if called before `init()` (or after
      `deinit()`)
- [x] Rejects a NULL `frame` pointer (once initialised)
- [x] Writes exactly the given bytes via `uart_write_buffer()`, unmodified
- [x] Propagates a UART write failure

## Still open — not part of command_transport

- [ ] `frame_parser_serialize_response()`: builds the outgoing wire frame
      (`SOF · LEN · ACK/NACK [· STATE] · CRC_L · CRC_H` per
      `mcu-co_Protocol.md`) from a `response_t`. Deliberately deferred — the
      encoding math was worked out (bare ACK `A5 01 01 1F 3E`, bare NACK
      `A5 01 00 3E 2E`, `GPIO_READ` ack+high `A5 02 01 01 EC 81`, all
      CRC16-CCITT-FALSE) but not yet implemented in `frame_parser.c`.
- [ ] Wiring `command_transport` into `main.c` in place of its inline
      UART/config/RX-buffer management, and actually calling
      `command_transport_send()` with the serialized bytes (today
      `main.c` computes a `response_t` but never sends it — see the
      `// Build return response` no-op around `main.c:102`).
