# mcu-co Commands

CLI commands, opcodes, and wire frames for GPIO control over the `mcu-co` UART link.

- **Model:** strict master-slave. Host sends a command; MCU answers. One command in flight at a time.
- **Transport:** UART, 8N1, little-endian.
- **Payload convention:** payload byte order follows CLI token order — *value/qualifier first, then port, then pin* — so a frame can be read straight off the command that produced it.

---

## Frame format

No version field — host and MCU firmware are always built and flashed together from this
repo, so there's nothing to negotiate. Commands and responses are structured differently:
commands carry a full argument payload; responses carry only a success/fail indicator (plus
pin state, for `gpio get`).

### Command frame

```
 SOF · OPCODE · LEN · PAYLOAD · CRC_L · CRC_H
```

| Field      | Size | Notes                                                        |
|------------|------|-------------------------------------------------------------|
| SOF        | 1    | Start-of-frame sync = `0xA5`. Resync anchor.                |
| OPCODE     | 1    | command opcode.                                              |
| LEN        | 1    | payload length in bytes.                                    |
| PAYLOAD    | LEN  | command fields.                                              |
| CRC16      | 2    | CRC16-CCITT, **little-endian** (CRC_L first, then CRC_H).   |

Frame size = `5 + LEN` bytes.

### Response frame

```
 SOF · OPCODE · LEN · ACK/NACK [ · STATE ] · CRC_L · CRC_H
```

| Field      | Size | Notes                                                                                                   |
|------------|------|-----------------------------------------------------------------------------------------------------------|
| SOF        | 1    | Start-of-frame sync = `0xA5`.                                                                              |
| OPCODE     | 1    | echoed from the command it answers.                                                                       |
| LEN        | 1    | `0x01` for most opcodes, `0x02` for `GPIO_READ`.                                                          |
| ACK/NACK   | 1    | `0x00` = NACK (command failed), `0x01` = ACK (command succeeded). This is the *only* success/fail signal — there is no separate NAK reason code. |
| STATE      | 0/1  | `GPIO_READ` only: pin level, `0x00` low / `0x01` high. Omitted (LEN=1) for every other opcode. Sent as filler `0x00` when ACK/NACK is NACK. |
| CRC16      | 2    | CRC16-CCITT, little-endian, computed over OPCODE·LEN·payload.                                              |

Response frame size is `6` bytes for most commands, `7` bytes for `GPIO_READ`.

### CRC coverage

CRC16-CCITT is computed over everything except SOF and the CRC bytes (`OPCODE`, `LEN`, `PAYLOAD`). On the wire the low byte is sent first (little-endian), e.g. CRC `0xE433` is transmitted as `33 E4`.

> **CRC variant:** examples here use **CCITT-FALSE** — polynomial `0x1021`, init `0xFFFF`, no reflection, final XOR `0x0000`. Firmware and host must agree bit-for-bit.

---

## Field encodings

| Field   | Values |
|---------|--------|
| PORT    | A=0, B=1, C=2, D=3, E=4, F=5, G=6 |
| PIN     | 0–15 |
| DIR     | input=0, output=1 |
| LEVEL   | low=0, high=1 |
| EDGE    | off/disabled=0, rising=1, falling=2, both=3 |
| ACTION  | low=0, high=1  (toggle=2, optional) |

---

## Command reference

| CLI | Opcode | Payload | Response payload |
|-----|--------|---------|------------------|
| `gpio cfg input\|output <port> <pin>` | `GPIO_CFG` `0x30` | `[DIR, PORT, PIN]` | `[ACK/NACK]` |
| `gpio set high\|low <port> <pin>` | `GPIO_WRITE` `0x31` | `[LEVEL, PORT, PIN]` | `[ACK/NACK]` |
| `gpio get <port> <pin>` | `GPIO_READ` `0x32` | `[PORT, PIN]` | `[ACK/NACK, STATE]` |
| `gpio irq <edge> <inp> <inpin> bind <level> <outp> <outpin>` | `GPIO_IRQ_BIND` `0x33` | `[EDGE, IN_PORT, IN_PIN, ACTION, OUT_PORT, OUT_PIN]` | `[ACK/NACK]` |
| `gpio irq off <port> <pin>` | `GPIO_IRQ_BIND` `0x33` | `[0, PORT, PIN, 0, 0, 0]` | `[ACK/NACK]` |

---

## 1. `gpio cfg` — configure pin direction

```
gpio cfg input|output <port> <pin>
```

Payload `[DIR, PORT, PIN]`. Example — configure PA5 as output:

```
cmd   A5 30 03  01 00 05  AB E1
               │  │  └ PIN  = 5
               │  └ PORT = A
               └ DIR  = output
ack   A5 30 01  01        29 2A
```

## 2. `gpio set` — drive an output pin

```
gpio set high|low <port> <pin>
```

Payload `[LEVEL, PORT, PIN]`. Example — drive PA5 high:

```
cmd   A5 31 03  01 00 05  FA 4B
ack   A5 31 01  01        19 1D
```

## 3. `gpio get` — read an input pin

```
gpio get <port> <pin>
```

Payload `[PORT, PIN]` (no qualifier). Response is `[ACK/NACK, STATE]`, where STATE carries the pin level. Example — read PA5, reads high:

```
cmd   A5 32 02  00 05     84 7B
resp  A5 32 02  01 01     31 08
               │  └ STATE    = 1 (high)
               └ ACK/NACK = 1 (success)
```

## 4. `gpio irq … bind …` — set-and-forget interrupt → output action

```
gpio irq <edge> <in_port> <in_pin> bind <level> <out_port> <out_pin>
```

Binds an input-pin edge to an output-pin action **entirely on the MCU**. Once the ACK returns, the host is out of the loop: when the edge fires, the MCU's ISR drives the output pin. No host notification, no attention line for this path.

Payload `[EDGE, IN_PORT, IN_PIN, ACTION, OUT_PORT, OUT_PIN]`. Example — when PA5 rises, drive PB0 high:

```
cmd   A5 33 06  01 00 05  01 01 00  56 E3
               │  │  │   │  │  └ OUT_PIN  = 0
               │  │  │   │  └ OUT_PORT = B
               │  │  │   └ ACTION   = high
               │  │  └ IN_PIN  = 5
               │  └ IN_PORT = A
               └ EDGE    = rising
ack   A5 33 01  01        79 73
```

### STM32 EXTI constraint

On the STM32G4, EXTI lines are shared by pin *number* across ports: PA5, PB5, PC5 all map to EXTI5, and only **one port may own a line at a time**. Binding a pin whose EXTI line is already owned by another port is rejected with a NACK rather than silently stealing the line.

## 5. `gpio irq off` — remove a binding

```
gpio irq off <port> <pin>
```

Same opcode as bind with `EDGE = 0`; the action bytes are ignored (sent as zero). Example — unbind PA5:

```
cmd   A5 33 06  00 00 05  00 00 00  F7 A2
ack   A5 33 01  01        79 73
```

---

## NACK

A NACK (`ACK/NACK = 0x00`) only signals that the command failed — there is no reason code in
the payload. Firmware still validates length, opcode, FSM state, port/pin/edge/level ranges,
and EXTI-line ownership before executing a command; any of those failures produces the same
bare NACK. Example — `gpio irq bind` rejected because the EXTI line is already taken:

```
cmd   A5 33 06  01 00 05  01 01 00  56 E3
nak   A5 33 01  00        58 63
```

---

## CLI parsing notes

- `bind` is a reserved pivot token; reject it as a port/edge/level so a missing pivot fails loudly instead of mis-slotting arguments.
- `off` in the edge slot maps to `EDGE = 0` (unbind); reject a trailing action tail (`gpio irq off A 5 bind …`).
- CLI and wire are both value-first. Decide whether the C SDK is value-first (`mcuco_gpio_set(ctx, level, port, pin)`, uniform with CLI/wire) or target-first (`mcuco_gpio_set(ctx, port, pin, level)`, C idiom with a reorder in the frame builder). Apply the choice uniformly.

## Open decisions

1. **CRC variant** — confirm CCITT-FALSE vs XMODEM/reflected; match firmware to this doc.
2. **`irq bind` auto-config** — recommended: the bind configures the input as EXTI+edge *and* the output as an output, so it's truly one command. Note that it will reconfigure those pins if previously used otherwise.
3. **SDK argument order** — value-first vs target-first (see CLI notes).
4. **No NACK reason code** — a NACK currently only says a command failed, not why (see [NACK](#nack)). Revisit if the host/CLI needs to surface a specific cause to the user rather than just "command rejected."

## Test vectors

The worked frames above are valid CCITT-FALSE frames and can be used directly as parser test vectors. Additional cases worth fuzzing: bytes delivered one-per-interrupt, partial frame then timeout, single-bit CRC flips, `0xA5` embedded in payload, unknown opcode, out-of-range port/pin, and EXTI-line conflicts (now signaled as a bare NACK).
