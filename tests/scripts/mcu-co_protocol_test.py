#!/usr/bin/env python3
"""
Interactively sends one mcu-co protocol frame at a time over the command
UART (USART2), so you can pick a frame type and watch how the MCU reacts.

Open the log UART (USART1) yourself in another terminal / serial monitor
to see how the MCU responded to each frame.

Frame encoding lives in tools/mcu-co-cli/mcuco/protocol.py and the transport in
mcuco/link.py — this script is firmware bring-up, so it keeps the deliberately
malformed frames (bad CRC, oversized LEN) that exercise the parser's error paths
rather than the device.

Every test declares what it expects (see Expect) and is reported PASS or FAIL
against it. Note that the malformed frames invert the usual condition: the
firmware discards a bad frame silently instead of NACKing it, so **no reply** is
the pass, and an ACK there is a failure. Option 'a' runs every self-checking
test and tallies them; the exit code is non-zero if any of them failed, so this
can go in a bring-up checklist after each flash.

Two entries can't be judged from the wire alone: the blink and button-bind demos
report how many of their steps were accepted, but whether the LED actually moved
is yours to confirm.
"""

import argparse
import sys
import time
from enum import Enum, auto
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "mcu-co-cli"))

from mcuco.client import McuCo  # noqa: E402
from mcuco.link import LinkTimeout, McuCoLink  # noqa: E402
from mcuco.protocol import (  # noqa: E402
    SOF,
    Action,
    Dir,
    Edge,
    Level,
    NackReason,
    Opcode,
    Polarity,
    Port,
    ProtocolError,
    build_command,
    crc16_ccitt_false,
)

class Expect(Enum):
    """What a test counts as a pass."""

    ACK = auto()       # the MCU accepted the command
    NACK = auto()      # the MCU refused it, and saying so is the correct behaviour
    NO_REPLY = auto()  # a malformed frame: the parser drops it without answering


# --- Defaults for the USB-serial adapter wired to USART2; override with -d/-b ---
CMD_PORT = '/dev/ttyACM0'
BAUDRATE = 115200
TIMEOUT_S = 1.0

LED_PIN = 5     # PA5, Nucleo user LED - also TIM2_CH1, so it doubles as PWM group 0
BUTTON_PIN = 13  # PC13, Nucleo B1 user button

# PWM tests run on group 1 (TIM3) / PC6 (TIM3_CH1), deliberately away from PA5:
# a pin already claimed as GPIO cannot be handed to PWM, and the protocol has no
# way to release one, so anything sharing PA5 would depend on test order.
PWM_GROUP = 1
PWM_PORT, PWM_PIN = Port.C, 6
PWM_FREQ_HZ = 1000
PWM_DUTY = 250

# PA0 has no timer channel mapped, so claiming it is a request the hardware
# cannot satisfy rather than a bad argument.
UNMAPPED_PORT, UNMAPPED_PIN = Port.A, 0

# The firmware answers every "not configured yet" case with ERR_NOT_INIT.
# mcu-co_Protocol.md documents ERR_INVALID_STATE for three of them (sections 11
# and 12, and the reason-code table); the firmware is self-consistent and the
# doc is not, so these expectations follow the firmware. See the note in the
# module docstring.
NOT_CONFIGURED = NackReason.ERR_NOT_INIT


def _verdict(ok: bool, expect: Expect, detail: str = "") -> bool:
    if ok:
        print(f"  PASS{f' - {detail}' if detail else ''}")
    else:
        print(f"  FAIL - expected {expect.name}{f', {detail}' if detail else ''}")
    return ok


def send(link, name: str, frame: bytes, expect: Expect, reason=None) -> bool:
    """Send one frame, report what came back, and judge it against `expect`."""
    print(f"Sent [{name}]: {frame.hex(' ')}")
    try:
        response = link.send(frame)
    except LinkTimeout:
        print("  <- no response")
        return _verdict(expect is Expect.NO_REPLY, expect, "frame discarded, as it should be"
                        if expect is Expect.NO_REPLY else "the MCU said nothing")
    except ProtocolError as exc:
        if link.last_response_raw:
            print(f"  <- {link.last_response_raw.hex(' ')}")
        print(f"  <- malformed response ({exc})")
        return _verdict(False, expect, "reply did not decode")

    if link.last_response_raw:
        print(f"  <- {link.last_response_raw.hex(' ')}")
    print(f"  <- {response}")

    if expect is Expect.ACK:
        return _verdict(response.ack, expect, f"got {response}")
    if expect is Expect.NACK:
        if response.ack:
            return _verdict(False, expect, f"got {response}")
        if reason is not None and response.reason != reason:
            return _verdict(False, expect, f"expected reason {reason.name}, got {response}")
        return _verdict(True, expect, f"got {response}")
    return _verdict(False, expect, "the MCU answered a frame it should have dropped")


def build_cfg_output_pa5() -> bytes:
    """gpio cfg output A 5 - configure PA5 (Nucleo LED) as an output."""
    return build_command(Opcode.GPIO_CFG, bytes([Dir.OUTPUT, Port.A, LED_PIN]))


def build_cfg_input_pc13() -> bytes:
    """gpio cfg input C 13 - configure PC13 (Nucleo B1 button) as an input."""
    return build_command(Opcode.GPIO_CFG, bytes([Dir.INPUT, Port.C, BUTTON_PIN]))


def build_gpio_read_pc13() -> bytes:
    """gpio get C 13 - read the current level of PC13."""
    return build_command(Opcode.GPIO_READ, bytes([Port.C, BUTTON_PIN]))


def build_valid_frame_wrong_crc() -> bytes:
    """Well-formed frame with the CRC bytes deliberately corrupted."""
    frame = bytearray(build_cfg_output_pa5())
    frame[-1] ^= 0x01  # flip a bit in CRC_HIGH
    return bytes(frame)


def build_invalid_frame() -> bytes:
    """
    LEN set beyond MAX_PAYLOAD (32). frame_parser_feed() rejects this as soon
    as it reads the LEN byte (FRAME_ERROR) and resyncs on the next SOF - it
    never even looks at the payload or CRC bytes that follow.

    Built by hand rather than through build_command(), which derives LEN from
    the payload and so cannot express this mismatch.
    """
    payload = bytes([Dir.OUTPUT, Port.A, LED_PIN])
    body = bytes([Opcode.GPIO_CFG, 0xFF]) + payload
    crc = crc16_ccitt_false(body)
    return bytes([SOF]) + body + bytes([crc & 0xFF, (crc >> 8) & 0xFF])


def build_invalid_frame_wrong_crc() -> bytes:
    """Same invalid (oversized-LEN) frame, with the trailing CRC bytes also corrupted."""
    frame = bytearray(build_invalid_frame())
    frame[-1] ^= 0x01
    return bytes(frame)


def step(name: str, call) -> bool:
    """Run one client call, report the reply, and return whether it was accepted."""
    print(f"Sent [{name}]")
    try:
        response = call()
    except LinkTimeout as exc:
        print(f"  <- no response ({exc})")
        return False
    except ProtocolError as exc:
        print(f"  <- malformed response ({exc})")
        return False

    print(f"  <- {response}")
    return response.ack


def check(name: str, call, expect: Expect, value=None, reason=None, tolerance: float = 0.0) -> bool:
    """
    Run one client call and judge the reply.

    Beyond ACK/NACK this can pin the value a read returned (`value`, with an
    optional fractional `tolerance` for figures the hardware rounds) and the
    reason a NACK carried - a command refused for the wrong reason has still
    told you something is wrong.
    """
    print(f"Sent [{name}]")
    try:
        response = call()
    except LinkTimeout as exc:
        print(f"  <- no response ({exc})")
        return _verdict(expect is Expect.NO_REPLY, expect, "the MCU said nothing")
    except ProtocolError as exc:
        print(f"  <- malformed response ({exc})")
        return _verdict(False, expect, "reply did not decode")

    print(f"  <- {response}")

    if expect is Expect.NACK:
        if response.ack:
            return _verdict(False, expect, f"got {response}")
        if reason is not None and response.reason != reason:
            return _verdict(False, expect, f"expected reason {reason.name}, got {response}")
        return _verdict(True, expect, f"got {response}")

    if expect is not Expect.ACK:
        return _verdict(False, expect, "the MCU answered a frame it should have dropped")

    if not response.ack:
        return _verdict(False, expect, f"got {response}")

    if value is not None:
        slack = value * tolerance
        if abs((response.value or 0) - value) > slack:
            detail = f"expected {value}" + (f" +/-{slack:.0f}" if slack else "") + f", got {response.value}"
            return _verdict(False, expect, detail)

    return _verdict(True, expect, f"got {response}")


def _pwm_cleanup(mcu) -> None:
    """Best-effort teardown so a part-way failure doesn't leave the group claimed."""
    try:
        mcu.pwm_group_release(PWM_GROUP)
    except (LinkTimeout, ProtocolError):
        pass


def run_pwm_group_lifecycle(link, mcu) -> bool:
    """
    A group from cold to torn down: reading it before it exists, configuring it,
    reading the frequency back, the refusal to reconfigure a live group, and the
    release that puts it back where it started.
    """
    try:
        results = [
            check("pwm group get 1 - before any cfg", lambda: mcu.pwm_group_get(PWM_GROUP),
                  Expect.NACK, reason=NOT_CONFIGURED),
            check(f"pwm group cfg {PWM_FREQ_HZ} 1", lambda: mcu.pwm_group_cfg(PWM_FREQ_HZ, PWM_GROUP),
                  Expect.ACK),
            check("pwm group get 1 - reads back the frequency", lambda: mcu.pwm_group_get(PWM_GROUP),
                  Expect.ACK, value=PWM_FREQ_HZ, tolerance=0.01),
            check("pwm group cfg 2000 1 - reconfigure is refused, group keeps running",
                  lambda: mcu.pwm_group_cfg(2000, PWM_GROUP), Expect.NACK, reason=NackReason.ERR_BUSY),
            check("pwm group release 1", lambda: mcu.pwm_group_release(PWM_GROUP), Expect.ACK),
            check("pwm group get 1 - teardown really happened", lambda: mcu.pwm_group_get(PWM_GROUP),
                  Expect.NACK, reason=NOT_CONFIGURED),
            check("pwm group release 1 - nothing left to release",
                  lambda: mcu.pwm_group_release(PWM_GROUP), Expect.NACK, reason=NackReason.ERR_NOT_INIT),
        ]
    finally:
        _pwm_cleanup(mcu)

    return all(results)


def run_pwm_channel_lifecycle(link, mcu) -> bool:
    """
    A channel from unclaimed to released. The two readbacks are the point: a
    freshly claimed channel must sit at duty 0 (the protocol's "comes up silent"
    rule), and a duty written then read back proves the 16-bit little-endian
    value survives the round trip in both directions.
    """
    pin = f"{PWM_PORT.name} {PWM_PIN}"
    try:
        results = [
            check(f"pwm group cfg {PWM_FREQ_HZ} 1", lambda: mcu.pwm_group_cfg(PWM_FREQ_HZ, PWM_GROUP),
                  Expect.ACK),
            check(f"pwm channel set {PWM_DUTY} {pin} - pin not claimed yet",
                  lambda: mcu.pwm_channel_set(PWM_DUTY, PWM_PORT, PWM_PIN),
                  Expect.NACK, reason=NOT_CONFIGURED),
            check(f"pwm channel cfg high {pin}",
                  lambda: mcu.pwm_channel_cfg(Polarity.ACTIVE_HIGH, PWM_PORT, PWM_PIN), Expect.ACK),
            check(f"pwm channel get {pin} - comes up silent at duty 0",
                  lambda: mcu.pwm_channel_get(PWM_PORT, PWM_PIN), Expect.ACK, value=0),
            check(f"pwm channel set {PWM_DUTY} {pin}",
                  lambda: mcu.pwm_channel_set(PWM_DUTY, PWM_PORT, PWM_PIN), Expect.ACK),
            check(f"pwm channel get {pin} - reads back what was written",
                  lambda: mcu.pwm_channel_get(PWM_PORT, PWM_PIN), Expect.ACK, value=PWM_DUTY),
            check(f"pwm channel release {pin}",
                  lambda: mcu.pwm_channel_release(PWM_PORT, PWM_PIN), Expect.ACK),
            check(f"pwm channel set {PWM_DUTY} {pin} - release really happened",
                  lambda: mcu.pwm_channel_set(PWM_DUTY, PWM_PORT, PWM_PIN),
                  Expect.NACK, reason=NOT_CONFIGURED),
            check("pwm group release 1", lambda: mcu.pwm_group_release(PWM_GROUP), Expect.ACK),
        ]
    finally:
        _pwm_cleanup(mcu)

    return all(results)


def run_pwm_rejections(link, mcu) -> bool:
    """
    Out-of-range fields, sent as hand-built frames.

    The client validates these locally and raises before anything reaches the
    wire, so calling through it would only test the client. Unlike the malformed
    frames in options 2-4 these are well formed - the parser accepts them and the
    controller is what must refuse - so a NACK is the pass, not silence.
    """
    cases = [
        ("pwm group cfg, GROUP = 3",
         build_command(Opcode.PWM_GROUP_CFG, (PWM_FREQ_HZ).to_bytes(4, "little") + bytes([3])),
         NackReason.ERR_INVALID_ARG),
        ("pwm group cfg, FREQ = 0",
         build_command(Opcode.PWM_GROUP_CFG, (0).to_bytes(4, "little") + bytes([PWM_GROUP])),
         NackReason.ERR_INVALID_ARG),
        ("pwm group cfg, FREQ above 1 MHz",
         build_command(Opcode.PWM_GROUP_CFG, (1_000_001).to_bytes(4, "little") + bytes([PWM_GROUP])),
         NackReason.ERR_INVALID_ARG),
        ("pwm channel set, DUTY = 1001",
         build_command(Opcode.PWM_SET, (1001).to_bytes(2, "little") + bytes([PWM_PORT, PWM_PIN])),
         NackReason.ERR_INVALID_ARG),
    ]

    results = [send(link, name, frame, Expect.NACK, reason=reason) for name, frame, reason in cases]

    results.append(check(f"pwm channel cfg high {UNMAPPED_PORT.name} {UNMAPPED_PIN} - no timer channel on this pin",
                         lambda: mcu.pwm_channel_cfg(Polarity.ACTIVE_HIGH, UNMAPPED_PORT, UNMAPPED_PIN),
                         Expect.NACK, reason=NackReason.ERR_UNSUPPORTED))
    return all(results)


def run_pwm_achieved_frequency(link, mcu, requested: int = 7000) -> bool:
    """
    The only test that exercises the prescaler/reload division on real silicon -
    everything else about it runs against timer_spy. 7000 Hz does not divide the
    170 MHz timer clock evenly, so the achieved figure is the interesting part.
    """
    try:
        if not check(f"pwm group cfg {requested} 1", lambda: mcu.pwm_group_cfg(requested, PWM_GROUP),
                     Expect.ACK):
            return False

        response = mcu.pwm_group_get(PWM_GROUP)
        print(f"Sent [pwm group get 1]\n  <- {response}")
        if not response.ack:
            return _verdict(False, Expect.ACK, f"got {response}")

        error_pct = abs(response.value - requested) / requested * 100
        print(f"  requested {requested} Hz, achieved {response.value} Hz ({error_pct:.3f}% off)")
        return _verdict(error_pct <= 1.0, Expect.ACK, f"achieved frequency is {error_pct:.3f}% off")
    finally:
        _pwm_cleanup(mcu)


def run_pwm_fade_demo(link, mcu, step_delay_s: float = 0.01):
    """
    Ramps PA5 (TIM2_CH1, the Nucleo LED) from dark to full and back.

    PA5 must not have been configured as a GPIO earlier in this session: a pin
    already owned by the GPIO driver cannot be handed to PWM, and the protocol
    has no command to release one. If this fails with ERR_BUSY, reset the board
    and run it first.
    """
    print("NOTE: needs a board where PA5 was not already used by a gpio test this session.\n")

    ramp = list(range(0, 1001, 20)) + list(range(1000, -1, -20))
    accepted = step("pwm group cfg 1000 0", lambda: mcu.pwm_group_cfg(1000, 0))
    accepted += step("pwm channel cfg high A 5",
                     lambda: mcu.pwm_channel_cfg(Polarity.ACTIVE_HIGH, Port.A, LED_PIN))
    sent = 2

    if accepted == sent:
        print(f"  ramping duty 0 -> 1000 -> 0 in {len(ramp)} steps...")
        for duty in ramp:
            try:
                accepted += bool(mcu.pwm_channel_set(duty, Port.A, LED_PIN).ack)
            except (LinkTimeout, ProtocolError):
                pass
            sent += 1
            time.sleep(step_delay_s)

    _pwm_cleanup(mcu)
    try:
        mcu.pwm_group_release(0)
    except (LinkTimeout, ProtocolError):
        pass

    return _report_demo(accepted, sent, "the LED should have faded up and back down smoothly")


def _report_demo(accepted: int, sent: int, watch_for: str) -> bool:
    """
    Demos are only half-checkable: the wire says whether every step was
    accepted, but whether the hardware actually moved is yours to see. Reported
    separately from PASS/FAIL so the two are never confused.
    """
    print(f"  {accepted}/{sent} steps ACKed"
          + ("" if accepted == sent else "  <- some steps were refused"))
    print(f"  CONFIRM VISUALLY: {watch_for}")
    return accepted == sent


def run_irq_bind_demo(mcu, step_delay_s: float = 0.3):
    """
    Wires the Nucleo B1 button (PC13) to toggle the LED (PA5) entirely on the
    MCU: cfg PC13 as input, PA5 as output, set PA5 low as a known starting
    point, arm PC13 for both edges, then bind both edges to toggle PA5.

    After the last ACK, the host is out of the loop - pressing the button
    should flip the LED with no further frames sent. Watch the LED, or the
    log UART, to confirm.
    """
    steps = [
        ("gpio cfg input C 13",
         lambda: mcu.gpio_cfg(Dir.INPUT, Port.C, BUTTON_PIN)),
        ("gpio cfg output A 5",
         lambda: mcu.gpio_cfg(Dir.OUTPUT, Port.A, LED_PIN)),
        ("gpio set low A 5",
         lambda: mcu.gpio_set(Level.LOW, Port.A, LED_PIN)),
        ("gpio irq cfg both C 13",
         lambda: mcu.gpio_irq_cfg(Edge.BOTH, Port.C, BUTTON_PIN)),
        ("gpio irq bind both C 13 toggle A 5",
         lambda: mcu.gpio_irq_bind(Edge.BOTH, Port.C, BUTTON_PIN, Action.TOGGLE, Port.A, LED_PIN)),
    ]

    accepted = 0
    for name, call in steps:
        accepted += step(name, call)
        time.sleep(step_delay_s)

    return _report_demo(accepted, len(steps),
                        "press B1 - the LED should toggle with no further frames sent")


def run_blink_led_test(mcu, toggle_count: int = 20, period_s: float = 0.5):
    """
    Configure PA5 (the Nucleo board's LED pin) as an output, then drive it
    high/low alternately every `period_s` seconds, `toggle_count` times.
    Watch the LED, or the log UART (USART1), to confirm each toggle. Ctrl+C
    stops early.
    """
    accepted = step("gpio cfg output A 5", lambda: mcu.gpio_cfg(Dir.OUTPUT, Port.A, LED_PIN))
    sent = 1
    time.sleep(period_s)

    level = Level.HIGH
    try:
        for _ in range(toggle_count):
            accepted += step(f"gpio set {'high' if level else 'low'} A 5",
                             lambda lvl=level: mcu.gpio_set(lvl, Port.A, LED_PIN))
            sent += 1
            level = Level.LOW if level else Level.HIGH
            time.sleep(period_s)
    except KeyboardInterrupt:
        print("\nBlink test stopped early.")

    return _report_demo(accepted, sent, "the LED should have alternated on each step")


# Each entry carries the outcome that counts as a pass. The malformed frames
# (2-4) expect NO_REPLY: the parser drops them without answering, so an ACK
# there is the firmware failing to reject something it should have.
MENU_OPTIONS = {
    '1': ("Valid frame", build_cfg_output_pa5, Expect.ACK),
    '2': ("Valid frame, wrong CRC", build_valid_frame_wrong_crc, Expect.NO_REPLY),
    '3': ("Invalid frame (LEN > MAX_PAYLOAD)", build_invalid_frame, Expect.NO_REPLY),
    '4': ("Invalid frame, wrong CRC", build_invalid_frame_wrong_crc, Expect.NO_REPLY),
    '6': ("Configure PC13 as input (Nucleo B1 button)", build_cfg_input_pc13, Expect.ACK),
    '7': ("Read PC13 state (Nucleo B1 button)", build_gpio_read_pc13, Expect.ACK),
}

# Demos, not tests: they need a human to watch the board, so they are left out
# of the run-all tally rather than reported as passes.
ACTIONS = {
    '5': ("Blink LED (PA5): cfg output, then toggle high/low", run_blink_led_test),
    '8': ("Bind button (PC13) both-edge -> toggle LED (PA5)", run_irq_bind_demo),
    '13': ("Fade LED (PA5) with PWM - needs PA5 unused this session", run_pwm_fade_demo),
}

# Multi-step tests, judged entirely from the wire. Each takes (link, mcu) and
# tears down whatever it configured, including when a step fails part-way.
SEQUENCES = {
    '9': ("PWM group lifecycle (group 1 / TIM3)", run_pwm_group_lifecycle),
    '10': ("PWM channel lifecycle (PC6, with duty readback)", run_pwm_channel_lifecycle),
    '11': ("PWM out-of-range fields are refused", run_pwm_rejections),
    '12': ("PWM achieved vs requested frequency", run_pwm_achieved_frequency),
}

RUN_ALL_KEY = 'a'
# Valid frames first, malformed next, then the PWM sequences - which use group 1
# and PC6, so nothing above can leave state that upsets them.
RUN_ALL_ORDER = ('1', '6', '7', '2', '3', '4')


def run_all(link, mcu) -> bool:
    """Run every self-checking test in order and tally the result."""
    print("\nRunning all self-checking tests. The malformed frames wait out the "
          "response timeout each, so this is not instant.\n")

    results = {}
    for key in RUN_ALL_ORDER:
        name, builder, expect = MENU_OPTIONS[key]
        results[name] = send(link, name, builder(), expect)
        print()

    for name, sequence in SEQUENCES.values():
        print(f"--- {name} ---")
        results[name] = sequence(link, mcu)
        print()

    passed = sum(results.values())
    print(f"--- {passed}/{len(results)} passed ---")
    for name, ok in results.items():
        if not ok:
            print(f"  FAILED: {name}")

    return passed == len(results)


def print_menu():
    """Frames and demos are held in separate dicts, so merge them to list in key order."""
    entries = {key: (name, f"[expects {expect.name}]")
               for key, (name, _builder, expect) in MENU_OPTIONS.items()}
    entries.update({key: (name, "[sequence, self-checking]")
                    for key, (name, _sequence) in SEQUENCES.items()})
    entries.update({key: (name, "[demo, confirm visually]")
                    for key, (name, _action) in ACTIONS.items()})

    print("\nSelect a frame to send:")
    for key in sorted(entries, key=lambda k: (0, int(k), "") if k.isdigit() else (1, 0, k)):
        name, tag = entries[key]
        print(f"  {key}) {name}  {tag}")
    print(f"  {RUN_ALL_KEY}) Run all self-checking tests")
    print("  q) Quit")


def parse_args(argv=None):
    parser = argparse.ArgumentParser(
        description=__doc__.strip().splitlines()[0],
        epilog="Options 2-4 send deliberately malformed frames; a timeout there is the pass "
               "condition, since the firmware discards a bad frame without replying.",
    )
    parser.add_argument("-d", "--device", default=CMD_PORT,
                        help=f"command UART device (default: {CMD_PORT})")
    parser.add_argument("-b", "--baud", type=int, default=BAUDRATE,
                        help=f"baud rate (default: {BAUDRATE})")
    parser.add_argument("-t", "--timeout", type=float, default=TIMEOUT_S,
                        help=f"response timeout in seconds (default: {TIMEOUT_S})")
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)

    try:
        link = McuCoLink(args.device, args.baud, timeout=args.timeout).open()
    except OSError as exc:  # serial.SerialException derives from OSError
        print(f"Cannot open {args.device}: {exc}", file=sys.stderr)
        print("Is the board plugged in? Pass -d to use a different device.", file=sys.stderr)
        return 1

    failures = 0

    with link:
        mcu = McuCo(link)

        while True:
            print_menu()
            choice = input("> ").strip().lower()

            if choice == 'q':
                break

            if choice == RUN_ALL_KEY:
                failures += not run_all(link, mcu)
                continue

            sequence = SEQUENCES.get(choice)
            if sequence is not None:
                _name, sequence_fn = sequence
                failures += not sequence_fn(link, mcu)
                continue

            option = MENU_OPTIONS.get(choice)
            if option is not None:
                name, builder, expect = option
                failures += not send(link, name, builder(), expect)
                continue

            action = ACTIONS.get(choice)
            if action is not None:
                _name, action_fn = action
                failures += not (action_fn(link, mcu) if choice == '13' else action_fn(mcu))
                continue

            print("Not a valid choice, try again.")

    if failures:
        print(f"\n{failures} test(s) failed this session.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
