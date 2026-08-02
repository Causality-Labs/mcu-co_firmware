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
 SOF · LEN · ACK/NACK [ · STATE ] · CRC_L · CRC_H
```

| Field      | Size | Notes                                                                                                   |
|------------|------|-----------------------------------------------------------------------------------------------------------|
| SOF        | 1    | Start-of-frame sync = `0xA5`.                                                                              |
| LEN        | 1    | `0x01` for most opcodes, `0x02` for `GPIO_READ`.                                                          |
| ACK/NACK   | 1    | `0x00` = NACK (command failed), `0x01` = ACK (command succeeded). This is the *only* success/fail signal — there is no separate NAK reason code. |
| STATE      | 0/1  | `GPIO_READ` only: pin level, `0x00` low / `0x01` high. Omitted (LEN=1) for every other opcode. Sent as filler `0x00` when ACK/NACK is NACK. |
| CRC16      | 2    | CRC16-CCITT, little-endian, computed over LEN and ACK/NACK[, STATE].                                       |

Response frame size is `5` bytes for most commands, `6` bytes for `GPIO_READ`.

No `OPCODE` field — the model is strict master-slave with one command in flight at a time
(see top), so the host always knows which command a response answers without an echo. One
consequence: every bare ACK (`A5 01 01 · CRC`) and every bare NACK (`A5 01 00 · CRC`) is
byte-identical regardless of which opcode it's answering.

### CRC coverage

CRC16-CCITT is computed over everything except SOF and the CRC bytes — `OPCODE`, `LEN`,
`PAYLOAD` for command frames; `LEN`, `ACK/NACK`[, `STATE`] for response frames (no `OPCODE`,
see [Response frame](#response-frame)). On the wire the low byte is sent first (little-endian),
e.g. CRC `0xE433` is transmitted as `33 E4`.

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
| EDGE_SELECT | rising=1, falling=2, both=3 (no `off` — see `GPIO_IRQ_BIND` below) |
| ACTION  | low=0, high=1, toggle=2 |

---

## Command reference

| CLI | Opcode | Payload | Response payload |
|-----|--------|---------|------------------|
| `gpio cfg input\|output <port> <pin>` | `GPIO_CFG` `0x30` | `[DIR, PORT, PIN]` | `[ACK/NACK]` |
| `gpio set high\|low <port> <pin>` | `GPIO_WRITE` `0x31` | `[LEVEL, PORT, PIN]` | `[ACK/NACK]` |
| `gpio get <port> <pin>` | `GPIO_READ` `0x32` | `[PORT, PIN]` | `[ACK/NACK, STATE]` |
| `gpio irq bind <edge> <inp> <inpin> <level> <outp> <outpin>` | `GPIO_IRQ_BIND` `0x33` | `[EDGE_SELECT, IN_PORT, IN_PIN, ACTION, OUT_PORT, OUT_PIN]` | `[ACK/NACK]` |
| `gpio irq cfg <edge> <port> <pin>` | `GPIO_IRQ_CFG` `0x34` | `[EDGE, PORT, PIN]` | `[ACK/NACK]` |
| `gpio irq cfg off <port> <pin>` | `GPIO_IRQ_CFG` `0x34` | `[0, PORT, PIN]` | `[ACK/NACK]` |
| `gpio irq unbind <port> <pin>` | `GPIO_IRQ_UNBIND` `0x35` | `[PORT, PIN]` | `[ACK/NACK]` |

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
ack   A5 01  01        1F 3E
```

## 2. `gpio set` — drive an output pin

```
gpio set high|low <port> <pin>
```

Payload `[LEVEL, PORT, PIN]`. Example — drive PA5 high:

```
cmd   A5 31 03  01 00 05  FA 4B
ack   A5 01  01        1F 3E
```

## 3. `gpio get` — read an input pin

```
gpio get <port> <pin>
```

Payload `[PORT, PIN]` (no qualifier). Response is `[ACK/NACK, STATE]`, where STATE carries the pin level. Example — read PA5, reads high:

```
cmd   A5 32 02  00 05     84 7B
resp  A5 02  01 01     EC 81
            │  └ STATE    = 1 (high)
            └ ACK/NACK = 1 (success)
```

## 4. `gpio irq cfg` — arm or disarm an interrupt trigger

```
gpio irq cfg <edge> <port> <pin>
```

Separate from binding: this only arms the pin's EXTI trigger direction (or disarms it). It
does **not** configure pin direction (see [open decision #2](#open-decisions) — that's manual,
via `gpio cfg`) and it does **not** attach any output action — that's `gpio irq bind`, below.
`GPIO_IRQ_CFG` must run *before* any `gpio irq bind` for that pin/edge; bind NACKs if the edge
it's binding to isn't currently armed.

Payload `[EDGE, PORT, PIN]`. Example — arm PA5 for both edges:

```
cmd   A5 34 03  03 00 05  CD 06
               │  │  └ PIN  = 5
               │  └ PORT = A
               └ EDGE = both
ack   A5 01  01        1F 3E
```

### STM32 EXTI constraint

On the STM32G4, EXTI lines are shared by pin *number* across ports: PA5, PB5, PC5 all map to EXTI5, and only **one port may own a line at a time**. Arming a pin whose EXTI line is already owned by another port is rejected with a NACK rather than silently stealing the line.

### `gpio irq cfg off` — disarm

```
gpio irq cfg off <port> <pin>
```

`EDGE = 0`. Tears the interrupt down entirely (`gpio_deinit_interrupt()`) and clears *every*
binding on that pin — since a binding with no armed trigger behind it can never fire, there's
nothing meaningful left to keep. There is currently no way to remove a single edge's binding
without disarming the whole pin; see [open decisions](#open-decisions). Example — disarm PA5:

```
cmd   A5 34 03  00 00 05  9D 5F
ack   A5 01  01        1F 3E
```

## 5. `gpio irq bind` — attach an output action to an armed edge

```
gpio irq bind <edge> <in_port> <in_pin> <level> <out_port> <out_pin>
```

Binds one already-armed edge of an input pin to an output-pin action **entirely on the MCU**.
Once the ACK returns, the host is out of the loop: when the edge fires, the MCU's ISR drives
the output pin directly. No host notification, no attention line for this path.

`EDGE_SELECT` here is rising/falling/both — never `off` (that's `gpio irq cfg off`, which
disarms the whole pin; `gpio irq unbind`, below, drops just the action). A pin has at most one
active binding at a time (see the overwrite rule below), so `EDGE_SELECT` picks which edge(s)
trigger *that one* action, not "one binding per edge."

**`bind` never overwrites an existing binding.** If the pin already has an active binding,
`bind` NACKs — the host must `gpio irq unbind` it first, then bind again. This is deliberate:
letting a second `bind` silently replace the first would mean a typo'd or stale command could
quietly swap out a binding the host thinks is still doing what it originally asked for.

**Want the output to track the input's transitions (e.g. a button driving an LED)? Use
`ACTION = toggle`, not two separate rising/falling bindings.** Toggle doesn't care which edge
fired — it just flips the output — so `EDGE_SELECT = both` with a toggle action mirrors the
input with a single `bind` call, and sidesteps needing to know which edge occurred at all. This
is why `ACTION` has a `toggle` value: it was added specifically so this case doesn't need
per-edge bindings or edge disambiguation in the ISR. (Caveat: it's stateful — a missed or extra
edge, e.g. from switch bounce, desyncs it permanently until re-bound. See the debounce
discussion — not yet implemented.)

Payload `[EDGE_SELECT, IN_PORT, IN_PIN, ACTION, OUT_PORT, OUT_PIN]`. Example — PC13 (Nucleo B1
button) armed `both`, toggling PA5 (the LED) on every press and release:

```
cmd   A5 33 06  03 02 0D  02 00 05  92 93
               │  │  │   │  │  └ OUT_PIN  = 5 (A5)
               │  │  │   │  └ OUT_PORT = A
               │  │  │   └ ACTION   = toggle
               │  │  └ IN_PIN  = 13 (C13)
               │  └ IN_PORT = C
               └ EDGE_SELECT = both
ack   A5 01  01        1F 3E
```

## 6. `gpio irq unbind` — drop a bound action without disarming

```
gpio irq unbind <port> <pin>
```

Clears whatever's bound to this pin's interrupt, but — unlike `gpio irq cfg off` — leaves the
EXTI trigger armed. After `unbind`, the pin keeps generating interrupts (and consuming its EXTI
line/NVIC slot); they just fire into a no-op until a new `bind` is issued. NACKs if there's no
active binding on that pin.

Payload `[PORT, PIN]`. Example — unbind PA5:

```
cmd   A5 35 02  00 05     A9 2A
ack   A5 01  01        1F 3E
```

---

## NACK

A NACK (`ACK/NACK = 0x00`) only signals that the command failed — there is no reason code in
the payload. Firmware still validates length, opcode, FSM state, port/pin/edge/level ranges,
and EXTI-line ownership before executing a command; any of those failures produces the same
bare NACK. Example — `gpio irq cfg` rejected because PA5's EXTI line is already owned by
another port:

```
cmd   A5 34 03  01 00 05  AD 68
nak   A5 01  00        3E 2E
```

`gpio irq bind` NACKs the same bare way if the edge it's targeting isn't currently armed on
that pin (e.g. `bind falling` when the pin was only armed `rising`), or if the pin already has
an active binding (see [§5](#5-gpio-irq-bind--attach-an-output-action-to-an-armed-edge)) —
`gpio irq unbind` it first:

```
cmd   A5 33 06  01 00 05  01 01 00  56 E3
nak   A5 01  00        3E 2E
```

---

## CLI parsing notes

- `bind` is a reserved pivot token in the old single-command grammar; now that `cfg`/`bind` are separate subcommands, the pivot to guard is the `irq` subcommand word itself (`cfg` vs `bind`) — reject anything else there so a typo fails loudly instead of mis-slotting arguments.
- `off` in the edge slot of `gpio irq cfg` maps to `EDGE = 0` (disarm); reject a trailing action tail (`gpio irq cfg off A 5 high C 2 …`) since `cfg` never takes output-action args.
- `gpio irq bind`'s edge slot never accepts `off` — dropping a binding is `gpio irq unbind` (keeps the trigger armed), disarming the whole pin is `gpio irq cfg off` (drops the binding too, as a side effect).
- CLI and wire are both value-first. Decide whether the C SDK is value-first (`mcuco_gpio_set(ctx, level, port, pin)`, uniform with CLI/wire) or target-first (`mcuco_gpio_set(ctx, port, pin, level)`, C idiom with a reorder in the frame builder). Apply the choice uniformly.

## Open decisions

1. **CRC variant** — confirm CCITT-FALSE vs XMODEM/reflected; match firmware to this doc.
2. **`irq bind` auto-config** — decided: **manual**. Neither `gpio irq cfg` nor `gpio irq bind` configure pin direction; the host must `gpio cfg input <in>` and `gpio cfg output <out>` first — `bind` NACKs (via the existing `is_pin_an_input()`/`is_pin_an_output()` checks) if either pin isn't already in the right mode. Rationale: auto-config can silently reconfigure a pin the host is using for something else; manual config keeps pin behavior changes explicit and host-visible.
3. **SDK argument order** — value-first vs target-first (see CLI notes).
4. **No NACK reason code** — a NACK currently only says a command failed, not why (see [NACK](#nack)). Revisit if the host/CLI needs to surface a specific cause to the user rather than just "command rejected."
5. **Rising/falling race on `both`-armed pins with different actions per edge** — moot, not just deferred: a pin can only have one active binding (see the overwrite rule in [§5](#5-gpio-irq-bind--attach-an-output-action-to-an-armed-edge)), so "different action per edge" isn't reachable through `bind` at all anymore. The `toggle` action is the intended way to get edge-agnostic behavior (e.g. LED tracking a button) without ever needing the ISR to know which edge fired. If a future need for genuinely independent rising/falling bindings comes up, this problem — and its fix (arm one direction at a time, flip `RTSR1`/`FTSR1` after each fire) — comes back with it.
6. **Unbind is per-pin, not per-edge** — resolved: `gpio irq unbind` (`0x35`) drops whatever's bound to a pin without disarming it. It's still whole-pin, not whole-edge — since a pin has at most one active binding today (see the `both` note in [§5](#5-gpio-irq-bind--attach-an-output-action-to-an-armed-edge)), there's nothing narrower to target yet. Revisit if a pin ever gets independent rising/falling bindings.

## Test vectors

The worked frames above are valid CCITT-FALSE frames and can be used directly as parser test vectors. Additional cases worth fuzzing: bytes delivered one-per-interrupt, partial frame then timeout, single-bit CRC flips, `0xA5` embedded in payload, unknown opcode, out-of-range port/pin, EXTI-line conflicts (now signaled as a bare NACK), `gpio irq bind` targeting an edge that isn't currently armed, `gpio irq bind` on a pin that's already bound, and `gpio irq unbind` on a pin with no active binding.
