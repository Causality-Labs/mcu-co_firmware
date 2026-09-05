# mcu-co Commands

CLI commands, opcodes, and wire frames for GPIO and PWM control over the `mcu-co` UART link.

- **Model:** strict master-slave. Host sends a command; MCU answers. One command in flight at a time.
- **Transport:** UART, 8N1, little-endian.
- **Payload convention:** payload byte order follows CLI token order — *value/qualifier first, then port, then pin* — so a frame can be read straight off the command that produced it.

---

## Frame format

No version field — host and MCU firmware are always built and flashed together from this
repo, so there's nothing to negotiate. Commands and responses are structured differently:
commands carry a full argument payload; responses carry a success/fail indicator plus, for the
read commands only, the value that was read.

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
 SOF · LEN · ACK/NACK [ · DATA ] · CRC_L · CRC_H
```

| Field      | Size | Notes                                                                                                   |
|------------|------|-----------------------------------------------------------------------------------------------------------|
| SOF        | 1    | Start-of-frame sync = `0xA5`.                                                                              |
| LEN        | 1    | `1 + DATA` length. `0x01` when there is no DATA.                                                           |
| ACK/NACK   | 1    | `0x00` = NACK (command failed), `0x01` = ACK (command succeeded). |
| DATA       | 0–4  | Meaning depends on ACK/NACK. On **ACK** it is the value a read command returns — width fixed per opcode (see table below), little-endian, absent (LEN=1) for every other opcode. On **NACK** it is always exactly one byte: the [reason code](#nack-reason-codes). |
| CRC16      | 2    | CRC16-CCITT, little-endian, computed over LEN, ACK/NACK and DATA.                                          |

Response frame size = `4 + LEN`, so 5 bytes for a bare ACK, 6 bytes for any NACK, and up to
9 bytes for a successful read. `LEN` arrives *before* the ACK/NACK byte, so the host always
knows how many bytes are coming without having to know the outcome first.

| Opcode | LEN | DATA |
|--------|-----|------|
| `GPIO_READ` | `0x02` | 1 byte — pin level, `0x00` low / `0x01` high (called `STATE` in [§3](#3-gpio-get--read-an-input-pin)) |
| `PWM_GET` | `0x03` | 2 bytes — duty, uint16 LE |
| `PWM_GROUP_GET` | `0x05` | 4 bytes — achieved frequency in Hz, uint32 LE |
| all others | `0x01` | none |
| *any opcode, on NACK* | `0x02` | 1 byte — [reason code](#nack-reason-codes) |

### NACK reason codes

Every NACK carries one DATA byte saying why the command failed, so a host can tell "the pin is
already in use" from "that pin number doesn't exist" instead of only seeing a refusal. The
codes are the firmware's own `status_t` values (`common/status.h`) sent verbatim — one list,
not a second protocol-only vocabulary that would drift from it.

| Code | Name | Meaning |
|------|------|---------|
| `0x01` | `ERR` | Unspecified failure. |
| `0x02` | `ERR_INVALID_ARG` | A field is out of range (bad `GROUP`, `DUTY` above 1000, `FREQ` outside 1–1000000, malformed payload length). |
| `0x03` | `ERR_INVALID_PIN` | Pin number out of range, or a reserved pin (PA13–15, PB3/4). |
| `0x04` | `ERR_INVALID_STATE` | The resource isn't configured for this operation — `pwm channel set` on a pin never claimed by `pwm channel cfg`, `pwm channel cfg` on a group with no frequency, `gpio irq bind` on an edge that isn't armed. |
| `0x05` | `ERR_NOT_INIT` | Peripheral or clock not brought up. |
| `0x06` | `ERR_BUSY` | **The resource is already in use.** The pin is owned by another driver, or the PWM group is already configured and must be torn down before it can be reconfigured. |
| `0x07` | `ERR_TIMEOUT` | Hardware did not respond in time. |
| `0x08` | `ERR_UNSUPPORTED` | Unknown opcode, or a valid request the hardware can't satisfy (e.g. a pin with no PWM channel). |

Codes are append-only: existing values are fixed once shipped, since they are on the wire.

No `OPCODE` field — the model is strict master-slave with one command in flight at a time
(see top), so the host always knows which command a response answers without an echo. One
consequence: every bare ACK (`A5 01 01 · CRC`) is byte-identical regardless of which opcode
it's answering, as is every NACK sharing a reason code (`A5 02 00 · REASON · CRC`).

### CRC coverage

CRC16-CCITT is computed over everything except SOF and the CRC bytes — `OPCODE`, `LEN`,
`PAYLOAD` for command frames; `LEN`, `ACK/NACK`[, `DATA`] for response frames (no `OPCODE`,
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
| GROUP   | 0=TIM2, 1=TIM3, 2=TIM4 — see [PWM groups](#pwm-groups) |
| FREQ    | uint32 LE, Hz. Valid 1–1000000; anything else is `ERR_INVALID_ARG` |
| DUTY    | uint16 LE, tenths of a percent: 0 = 0.0%, 250 = 25.0%, 1000 = 100.0% |
| POL     | active-high=0, active-low=1 |

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
| `pwm group cfg <freq_hz> <group>` | `PWM_GROUP_CFG` `0x40` | `[FREQ_LE32, GROUP]` | `[ACK/NACK]` |
| `pwm channel cfg high\|low <port> <pin>` | `PWM_CFG` `0x41` | `[POL, PORT, PIN]` | `[ACK/NACK]` |
| `pwm channel set <duty> <port> <pin>` | `PWM_SET` `0x42` | `[DUTY_LE16, PORT, PIN]` | `[ACK/NACK]` |
| `pwm channel release <port> <pin>` | `PWM_RELEASE` `0x43` | `[PORT, PIN]` | `[ACK/NACK]` |
| `pwm channel get <port> <pin>` | `PWM_GET` `0x44` | `[PORT, PIN]` | `[ACK/NACK, DUTY_LE16]` |
| `pwm group get <group>` | `PWM_GROUP_GET` `0x45` | `[GROUP]` | `[ACK/NACK, FREQ_LE32]` |
| `pwm group release <group>` | `PWM_GROUP_RELEASE` `0x46` | `[GROUP]` | `[ACK/NACK]` |

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

## PWM groups

The twelve PWM outputs are divided into three **groups** of four. A group is one hardware
timer: the frequency comes from that timer's prescaler and reload, which all four channels
share, so **every pin in a group runs at the same frequency**. Duty cycle is per-pin.

| GROUP | Timer | CH1 | CH2 | CH3 | CH4 |
|-------|-------|-----|-----|-----|-----|
| 0 | TIM2 | PA5 | PA1 | PB10 | PB11 |
| 1 | TIM3 | PC6 | PC7 | PC8 | PC9 |
| 2 | TIM4 | PB6 | PB7 | PB8 | PB9 |

**A group is not a port.** Group 0 spans ports A and B, and port B is split across groups 0
and 2 — the map comes from the STM32G4 alternate-function table, not from any port grouping.
So the host cannot derive a group from a port, which is why frequency is addressed by an
explicit `GROUP` number: `pwm group cfg` sets the frequency for four pins at once, and hiding
that behind one-pin syntax would make the blast radius invisible. Duty, by contrast, is addressed by
pin — firmware resolves pin → (timer, channel) itself, so the map lives in exactly one place
(`peripherals/timer.c`) and neither the CLI nor the host re-encodes it.

Ordering: `pwm group cfg` must succeed for a group before `pwm channel cfg` will claim any
pin in it, and `pwm channel cfg` must claim a pin before `pwm channel set` will drive it.

## 7. `pwm group cfg` — set a group's frequency

```
pwm group cfg <freq_hz> <group>
```

The first call brings the group up and starts its counter. A **second call on a group that is
already configured is refused** with `ERR_BUSY` (`0x06`) and changes nothing — the group's
frequency is shared by all four of its pins, so moving it is destructive to every channel
already running there, and the host has to say so explicitly. To change a live group's
frequency, release it first with [`pwm group release`](#75-pwm-group-release--tear-a-group-down)
and configure it again; the channels on it must then be re-claimed with `pwm channel cfg`.

`ERR_BUSY` is the one NACK from this command that leaves the group **running and untouched**.
Every other NACK means the group ended up off: if the time base programs but the counter
fails to start, the firmware tears the group back down rather than leaving it
configured-but-silent, where a later `pwm channel cfg` would ACK a pin that produces no output.

Valid range is 1 Hz to 1 MHz; `0` and anything above the range are `ERR_INVALID_ARG`, not a
shorthand for anything. Frequencies inside that range can still be rejected if the 16-bit
prescaler and reload can't divide the 170 MHz timer clock down to them.

Payload `[FREQ_LE32, GROUP]`. Example — group 0 (TIM2) to 1 kHz:

```
cmd   A5 40 05  E8 03 00 00  00  DE CD
                │            └ GROUP = 0 (TIM2)
                └ FREQ = 0x000003E8 = 1000 Hz
ack   A5 01  01        1F 3E
```

### 7.5 `pwm group release` — tear a group down

```
pwm group release <group>
```

Undoes `pwm group cfg`: counter stopped, all four channels deconfigured, their pins released,
timer clock gated off. This is the only way to change a configured group's frequency — release
it, then configure it again.

Releasing a group that was never configured NACKs with `ERR_NOT_INIT` (`0x05`) rather than
ACKing. There is no state to undo, and a host that asked to release a group it never set up has
misunderstood something; a host doing unconditional startup cleanup can ignore the code, but a
host that would rather know can't recover information that was never sent.

Payload `[GROUP]`. Example — release group 0 (TIM2):

```
cmd   A5 46 01  00  A0 50
                └ GROUP = 0 (TIM2)
ack   A5 01  01        1F 3E
```

## 8. `pwm channel cfg` — claim a pin for PWM

```
pwm channel cfg high|low <port> <pin>
```

Claims the pin: switches it to alternate-function mode, takes ownership so no other driver can
use it, and enables the channel output. It carries **no duty** — the pin comes up silent at
0.0% and stays there until `pwm channel set`. Same split as `gpio cfg`/`gpio set`: `cfg` gives
a pin its unchanging property, `set` carries the value.

`high|low` is the output polarity — the level held during the active part of the period.

Payload `[POL, PORT, PIN]`. Example — claim PA5 (LD2) active-high:

```
cmd   A5 41 03  00 00 05  4C 61
               │  │  └ PIN  = 5
               │  └ PORT = A
               └ POL  = active-high
ack   A5 01  01        1F 3E
```

NACKs if the pin has no PWM channel mapped, if its group has no frequency yet, if the channel
is already configured, or if the pin is owned by another driver — a pin previously set up with
`gpio cfg` has to be released before PWM can take it.

## 9. `pwm channel set` — set a claimed pin's duty cycle

```
pwm channel set <duty> <port> <pin>
```

Writes the duty cycle of one claimed pin. The frequency and every other channel are untouched.
The hardware buffers the write and applies it at the next period boundary, so it can never
produce a partial pulse — this is the command to call repeatedly for dimming ramps or servo
sweeps, and it is far cheaper than re-running `pwm channel cfg` (which would NACK anyway).

`DUTY` is in tenths of a percent, so `250` is 25.0% and `1000` is 100.0%. The CLI accepts
percent with one decimal (`pwm channel set 25.0 A 5`) and converts.

**To silence an output use `pwm channel set 0`**, not a group teardown or a counter stop —
stopping a counter freezes each pin at whatever level it held mid-period, which may be high.

Payload `[DUTY_LE16, PORT, PIN]`. Example — drive PA5 at 25.0%:

```
cmd   A5 42 04  FA 00  00 05  05 C1
               │       │  └ PIN  = 5
               │       └ PORT = A
               └ DUTY = 250 = 25.0%
ack   A5 01  01        1F 3E
```

NACKs if the pin isn't claimed, or if `DUTY` exceeds 1000.

## 10. `pwm channel release` — release a claimed pin

```
pwm channel release <port> <pin>
```

Deconfigures the channel and hands the pin back, leaving the rest of the group running. This
needs its own opcode rather than folding into `pwm channel cfg` with a sentinel: duty `0` is a
legitimate value, so unlike `gpio irq cfg off`'s `EDGE = 0` there is no spare encoding to
overload.

Payload `[PORT, PIN]`. Example — release PA5:

```
cmd   A5 43 02  00 05     45 4F
ack   A5 01  01        1F 3E
```

NACKs if the pin isn't currently configured for PWM.

## 11. `pwm channel get` — read back a pin's duty cycle

```
pwm channel get <port> <pin>
```

Payload `[PORT, PIN]`, response `[ACK/NACK, DUTY_LE16]`. Example — read PA5, sitting at 25.0%:

```
cmd   A5 44 02  00 05        68 1E
resp  A5 03  01  FA 00     26 D4
            │   └ DUTY     = 250 = 25.0%
            └ ACK/NACK  = 1 (success)
```

A NACK replaces the duty with the reason byte instead — e.g. `pwm channel get` on a pin that was
never claimed: `A5 02 00 04 78 E2` (`ERR_INVALID_STATE`).

## 12. `pwm group get` — read back a group's achieved frequency

```
pwm group get <group>
```

Reports the frequency the hardware **actually** produces, which is not always the one that was
requested: the prescaler and reload are integers, so e.g. 60000 Hz asked of a 170 MHz timer
clock comes back as 60007 Hz. This command is the only way for the host to learn the real
figure, and is the reason the response frame carries data at all.

Payload `[GROUP]`, response `[ACK/NACK, FREQ_LE32]`. Example — read group 0, tuned to 1 kHz:

```
cmd   A5 45 01  00              F0 09
resp  A5 05  01  E8 03 00 00  39 BF
            │   └ FREQ       = 0x000003E8 = 1000 Hz
            └ ACK/NACK    = 1 (success)
```

NACKs carry the reason in place of the frequency: `A5 02 00 02 BE 82` (`ERR_INVALID_ARG`, group
above 2), or `A5 02 00 04 78 E2` (`ERR_INVALID_STATE`, group has no frequency configured).

---

## NACK

A NACK (`ACK/NACK = 0x00`) only signals that the command failed — there is no reason code in
the payload. Firmware still validates length, opcode, FSM state, port/pin/edge/level ranges,
EXTI-line ownership, PWM group/duty/frequency ranges, and pin ownership before executing a
command; each of those failures produces a NACK carrying its own reason code.

A NACK never carries read data — its single DATA byte is the reason code, whatever the opcode.
So `LEN` on a read's response tells the host which it got (`0x03` = a duty, `0x02` = a refusal),
and DATA is never a reading unless ACK/NACK says `0x01`. Check ACK/NACK first.

> **A NACK is only sent once a frame has been fully received and its CRC verified.** Failures
> *below* that point — a CRC mismatch, or a framing error such as `LEN > MAX_PAYLOAD` — are
> answered with **silence**: the MCU discards the frame, resyncs on the next SOF, and sends
> nothing. A corrupt frame can't be trusted to identify itself, so there is nothing to
> meaningfully reply to. Hosts must therefore treat a response timeout as a real outcome and
> cannot distinguish "frame rejected by the parser" from "MCU absent" at the protocol level.

Example — `gpio irq cfg` rejected because PA5's EXTI line is already owned by another port:

```
cmd   A5 34 03  01 00 05  AD 68
nak   A5 02  00  06    3A C2
            │   └ REASON = 0x06 ERR_BUSY (EXTI line owned by another port)
            └ ACK/NACK = 0 (failed)
```

`gpio irq bind` NACKs with `ERR_INVALID_STATE` (`0x04`) if the edge it's targeting isn't currently armed on
that pin (e.g. `bind falling` when the pin was only armed `rising`), or if the pin already has
an active binding, which reports `ERR_BUSY` (see
[§5](#5-gpio-irq-bind--attach-an-output-action-to-an-armed-edge)) — `gpio irq unbind` it first:

```
cmd   A5 33 06  01 00 05  01 01 00  56 E3
nak   A5 02  00  04    78 E2
```

The PWM commands NACK the same way. The ordering rules are the common cause:
`pwm channel cfg` on a pin whose group has no frequency yet, or `pwm channel set` on a pin
that was never claimed by `pwm channel cfg`:

```
cmd   A5 42 04  FA 00  00 05  05 C1
nak   A5 02  00  04    78 E2
```

---

## CLI parsing notes

- `bind` is a reserved pivot token in the old single-command grammar; now that `cfg`/`bind` are separate subcommands, the pivot to guard is the `irq` subcommand word itself (`cfg` vs `bind`) — reject anything else there so a typo fails loudly instead of mis-slotting arguments.
- `off` in the edge slot of `gpio irq cfg` maps to `EDGE = 0` (disarm); reject a trailing action tail (`gpio irq cfg off A 5 high C 2 …`) since `cfg` never takes output-action args.
- `gpio irq bind`'s edge slot never accepts `off` — dropping a binding is `gpio irq unbind` (keeps the trigger armed), disarming the whole pin is `gpio irq cfg off` (drops the binding too, as a side effect).
- `pwm group` is a pivot word like `gpio irq`: guard the subcommand slot after it (`cfg` vs `get`) and reject anything else, so `pwm group st 1000 0` fails loudly rather than mis-slotting arguments.
- `pwm channel cfg`'s qualifier slot is polarity (`high`/`low`), not a duty — a duty there (`pwm channel cfg 25.0 A 5`) should be rejected with a pointer to `pwm channel set`, since silently accepting it would claim the pin at a duty the protocol never carried.
- The CLI takes duty as percent with up to one decimal and multiplies by 10 for the wire (`25.0` → `250`). Reject more than one decimal place rather than rounding it away, so a host asking for a precision the hardware field can't hold hears about it.
- CLI and wire are both value-first, and so is the SDK — `mcuco_gpio_set(ctx, level, port, pin)`, not `mcuco_gpio_set(ctx, port, pin, level)`. See [open decision #3](#open-decisions).

## Open decisions

1. **CRC variant** — confirm CCITT-FALSE vs XMODEM/reflected; match firmware to this doc.
2. **`irq bind` auto-config** — decided: **manual**. Neither `gpio irq cfg` nor `gpio irq bind` configure pin direction; the host must `gpio cfg input <in>` and `gpio cfg output <out>` first — `bind` NACKs (via the existing `is_pin_an_input()`/`is_pin_an_output()` checks) if either pin isn't already in the right mode. Rationale: auto-config can silently reconfigure a pin the host is using for something else; manual config keeps pin behavior changes explicit and host-visible.
3. **SDK argument order** — resolved: **value-first**, `mcuco_gpio_set(ctx, level, port, pin)`. Rationale: CLI token order and payload byte order are already value-first, so a value-first SDK makes the call, the command that produced it and the bytes on the wire all read in the same direction — one order to remember, and a frame builder that copies its arguments straight through with no reorder to get wrong. The cost is that it reads slightly against C convention, where the target usually comes first. Applied uniformly: the host-side mock SDK in `tools/mcu-co-cli/mcuco/client.py` follows it, and the C SDK must too — the two are meant to mirror each other.
4. **NACK reason code** — resolved: a NACK carries the failing `status_t` as a single DATA byte (see [NACK reason codes](#nack-reason-codes)). Chosen over widening the ACK/NACK byte into a status code, which would have flipped the meaning of `0x00` from "failed" to "succeeded" and rewritten every response frame in this document for no extra expressiveness. The reason field reuses `common/status.h` verbatim rather than defining a protocol-only error vocabulary, so the codes can't drift from what the firmware actually returns — the cost being that `status_t` is now append-only.
5. **Rising/falling race on `both`-armed pins with different actions per edge** — moot, not just deferred: a pin can only have one active binding (see the overwrite rule in [§5](#5-gpio-irq-bind--attach-an-output-action-to-an-armed-edge)), so "different action per edge" isn't reachable through `bind` at all anymore. The `toggle` action is the intended way to get edge-agnostic behavior (e.g. LED tracking a button) without ever needing the ISR to know which edge fired. If a future need for genuinely independent rising/falling bindings comes up, this problem — and its fix (arm one direction at a time, flip `RTSR1`/`FTSR1` after each fire) — comes back with it.
6. **Unbind is per-pin, not per-edge** — resolved: `gpio irq unbind` (`0x35`) drops whatever's bound to a pin without disarming it. It's still whole-pin, not whole-edge — since a pin has at most one active binding today (see the `both` note in [§5](#5-gpio-irq-bind--attach-an-output-action-to-an-armed-edge)), there's nothing narrower to target yet. Revisit if a pin ever gets independent rising/falling bindings.
7. **Response DATA field** — resolved: the response frame's single optional `STATE` byte was generalised to a fixed-width-per-opcode `DATA` field (see [Response frame](#response-frame)) so PWM reads can return a 16-bit duty and a 32-bit frequency. `GPIO_READ` is unchanged on the wire (`LEN = 0x02`, one byte) when it succeeds. Widths are fixed per opcode *for ACKs only*; every NACK collapses to a one-byte reason (decision 4), which is safe because `LEN` precedes the ACK/NACK byte and so always tells the host the frame's size up front. Rationale for having reads at all: `pwm group get` is the only way for the host to learn the *achieved* frequency, which integer prescaler/reload division makes differ from the requested one.
8. **No PWM start/stop command** — deliberate. The counter is per-timer, so a start/stop would act on all four channels of a group at once, and `timer_stop()` freezes each pin at whatever level it held mid-period rather than driving it low — an "off" that can leave a pin high. Per-pin off is `pwm channel set 0`; whole-group teardown is `pwm group release <group>`. Revisit only if a genuine synchronised-freeze use case appears, and document the hold-level behaviour if so.

## Test vectors

The worked frames above are valid CCITT-FALSE frames and can be used directly as parser test vectors. Additional cases worth fuzzing: bytes delivered one-per-interrupt, partial frame then timeout, single-bit CRC flips, `0xA5` embedded in payload, unknown opcode, out-of-range port/pin, EXTI-line conflicts (now signaled as a bare NACK), `gpio irq bind` targeting an edge that isn't currently armed, `gpio irq bind` on a pin that's already bound, and `gpio irq unbind` on a pin with no active binding.

PWM-specific cases: `pwm channel cfg` on a pin with no PWM channel mapped (e.g. PA0),
`pwm channel cfg` before its group has a frequency, `pwm channel cfg` on a pin already claimed
by `gpio cfg`, `pwm channel set` on an
unclaimed pin, `DUTY` above 1000, `FREQ` of 0 and above 1000000 (both `ERR_INVALID_ARG`),
`pwm group release` on a group that was never configured (`ERR_NOT_INIT`),
`GROUP` above 2, `pwm group cfg` on a group that is already configured (must NACK `ERR_BUSY`
and leave the group running untouched), and the two multi-byte response widths — `PWM_GET`'s
`LEN = 0x03` and `PWM_GROUP_GET`'s `LEN = 0x05` — including the `LEN = 0x02` reason-byte form
their NACKs collapse to.
9. **Reconfiguring a live PWM group is refused, not applied** — resolved: a second `pwm group cfg` on a group that is already configured NACKs with `ERR_BUSY` and changes nothing (see [§7](#7-pwm-group-cfg--set-a-groups-frequency)). The alternative was to retune in place, rescaling every channel's compare value so each pin kept its duty. Rejected because a group's frequency is shared by four pins: a single command would silently move outputs the host may not have been thinking about, and "it was already set up" is exactly the case where the host should be told rather than obeyed. The cost is that changing a live group's frequency is now `pwm group release` then `pwm group cfg`, and its channels must be re-claimed with `pwm channel cfg` afterwards.
10. **Teardown is its own command, not `FREQ = 0`** — resolved: `pwm group release` (`0x46`) replaces the earlier rule where a frequency of zero meant "tear the group down". Overloading the value made the firmware infer an intent the host never expressed, and it took a value the driver already rejects correctly (`ERR_INVALID_ARG`, below the 1 Hz minimum) and gave it a second, unrelated meaning. A destructive operation should be asked for by name. This also made the release path symmetric with `pwm channel release` (`0x43`) one level down.
