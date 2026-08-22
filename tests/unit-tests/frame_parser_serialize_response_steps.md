# frame_parser_serialize_response() — implementation steps

Working notes for implementing the response-serialization side of `frame_parser`
by hand, following the TDD workflow in `CLAUDE.md`.

Split into two functions, deliberately keeping `frame_parser.c` free of any
dependency on `crc16.h` — same as it is today (only `main.c` ties framing and
CRC together, for the incoming direction):

- `frame_parser_serialize_response()` — builds `SOF · LEN · ACK/NACK [· STATE]`
  from a `response_t`. Owns the byte layout, doesn't touch CRC at all.
- `frame_parser_append_crc()` — appends a pre-computed CRC (`CRC_L · CRC_H`) to
  an existing buffer at a given offset. Takes the CRC as a plain `uint16_t`
  argument; the caller (`main.c`) computes it with `crc16_compute()`, same as
  it already does when validating incoming frames.

## 0. Watch out: the two serialize functions disagree about SOF

`frame_parser_serialize()` (existing, incoming path) returns **exactly the
CRC-covered bytes** — `OPCODE · LEN · PAYLOAD`, no SOF — so `main.c` can call
`crc16_compute(buf, size)` with no offset math.

`frame_parser_serialize_response()` (new, outgoing path) **includes SOF**, which
is not CRC-covered. So its caller must skip the first byte when computing the
CRC. Two functions in one module with opposite conventions is an easy place to
introduce a silent CRC bug — mitigate by naming the offset rather than hardcoding
`1`, alongside the existing index constants in `frame_parser.h`:

```c
#define SOF_BYTE          0xA5U /* also replaces the hardcoded 0xA5 in frame_parser_feed() */
#define RESPONSE_BODY_IDX 1     /* CRC covers [RESPONSE_BODY_IDX .. len), i.e. everything but SOF */
#define RESPONSE_FRAME_MAX 6    /* SOF + LEN + ACK/NACK + STATE + CRC_L + CRC_H */
```

## 1. Resolve the header dependency — done

`frame_parser_serialize_response()` needs `response_t`, but it used to live in
`command_dispatcher.h`, which itself `#include`s `frame_parser.h` — a cycle if
`frame_parser.h` needed it back. Resolved by moving `response_t`'s definition into
`include/frame_parser.h` (next to `frame_t`) and deleting it from
`command_dispatcher.h`, which keeps working unchanged since it already
`#include`s `frame_parser.h`.

## 2. Declare both functions

In `frame_parser.h`, matching `frame_parser_serialize()`'s existing convention
(`int`, `-1` on error, buffer + size args):

```c
int frame_parser_serialize_response(const response_t *resp, uint8_t *serialized_frame_buffer, uint8_t serialized_frame_size);

int frame_parser_append_crc(uint8_t *serialized_frame_buffer, uint8_t current_length, uint8_t buffer_size, uint16_t crc);
```

## 3. Write the test list first

Scenarios to cover for `frame_parser_serialize_response()`:

- [ ] Rejects NULL `resp`
- [ ] Rejects NULL buffer
- [ ] Rejects a buffer too small for `SOF + LEN + ACK/NACK [+ STATE]`
- [ ] Accepts a buffer that fits exactly (boundary counterpart to the
      too-small test — mirrors `FeedAcceptsLengthAtMaxPayloadBoundary`)
- [ ] Bare ACK (`has_state=false, ack=true`) → `A5 01 01`
- [ ] Bare NACK (`ack=false`) → `A5 01 00`
- [ ] With state (`has_state=true`) → `A5 02 01 01` (LEN=2, STATE included)
- [ ] `has_state=false` does **not** write a 4th byte (guards the easy bug of
      always writing STATE regardless of the flag)
- [ ] Returns the correct byte count on success: **3** bare (SOF + LEN +
      ACK/NACK), **4** with state. Note SOF *is* counted here, unlike
      `frame_parser_serialize()` — see section 0.

Scenarios to cover for `frame_parser_append_crc()`:

- [ ] Rejects NULL buffer
- [ ] Rejects a buffer too small to fit 2 more bytes at `current_length`
- [ ] Appends CRC low byte first, then high byte (little-endian, per
      `mcu-co_Protocol.md`)
- [ ] Leaves the bytes before `current_length` untouched
- [ ] Returns `current_length + 2` on success

## 4. One test at a time, red then green

Same cycle as `command_transport`: write one failing test, confirm red (link error
until the function exists in `frame_parser.c`), write minimal code, confirm green,
repeat. Do `frame_parser_serialize_response()` fully before starting
`frame_parser_append_crc()` — they're independent, no reason to interleave.

## 5. CERT-C: don't overflow the bounds check in append_crc

The obvious check is wrong:

```c
if (buffer_size < (uint8_t)(current_length + 2U)) /* BUG */
```

Both operands are `uint8_t`. At `current_length = 254, buffer_size = 255` the sum
wraps to 0, the check passes, and the function writes `frame[255]` out of bounds
(CERT-C INT30-C / ARR30-C — `cert-*` is enforced as `-Werror` here). Unreachable
with today's 6-byte buffers, but compare in a wider type anyway:

```c
if (((uint16_t)current_length + 2U) > (uint16_t)buffer_size)
{
    return -1;
}
```

## 6. Known-good end-to-end test vectors

For wiring/integration sanity once both functions exist (already derived from
`mcu-co_Protocol.md`, reuse rather than recompute):

| Case                          | Full wire bytes         | serialize_response() returns |
|--------------------------------|-------------------------|------------------------------|
| Bare ACK                       | `A5 01 01 1F 3E`        | 3                            |
| Bare NACK                      | `A5 01 00 3E 2E`        | 3                            |
| With state (ack=1, state=1)    | `A5 02 01 01 EC 81`     | 4                            |

## 7. Wire it into main.c

Once all tests pass, replace the `TODO` in `src/main.c`'s main loop. `main.c`
stays the one place that owns CRC computation (same as it already does for
incoming frames) — it just hands the result to `frame_parser` instead of
poking bytes into an array itself.

Both return codes must be checked before use: on `-1`, `(uint8_t)(len - 1)`
becomes `254` and `crc16_compute()` would read ~254 bytes off a 6-byte stack
buffer.

```c
uint8_t response_frame[RESPONSE_FRAME_MAX];

int len = frame_parser_serialize_response(&resp, response_frame, sizeof(response_frame));
if (len < 0)
{
    LOG_ERROR(MODULE_NAME, "frame_parser_serialize_response() failed.");
    continue;
}

/* CRC covers everything but SOF - see section 0. */
uint16_t crc = crc16_compute(&response_frame[RESPONSE_BODY_IDX], (uint8_t)(len - RESPONSE_BODY_IDX));

len = frame_parser_append_crc(response_frame, (uint8_t)len, sizeof(response_frame), crc);
if (len < 0)
{
    LOG_ERROR(MODULE_NAME, "frame_parser_append_crc() failed.");
    continue;
}

if (command_transport_send(response_frame, (uint16_t)len) != STATUS_OK)
{
    LOG_ERROR(MODULE_NAME, "command_transport_send() failed.");
}
```
