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
    Opcode,
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

LED_PIN = 5     # PA5, Nucleo user LED
BUTTON_PIN = 13  # PC13, Nucleo B1 user button


def _verdict(ok: bool, expect: Expect, detail: str = "") -> bool:
    if ok:
        print(f"  PASS{f' - {detail}' if detail else ''}")
    else:
        print(f"  FAIL - expected {expect.name}{f', {detail}' if detail else ''}")
    return ok


def send(link, name: str, frame: bytes, expect: Expect) -> bool:
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
        return _verdict(not response.ack, expect, f"got {response}")
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
}

RUN_ALL_KEY = 'a'
RUN_ALL_ORDER = ('1', '6', '7', '2', '3', '4')  # valid frames first, malformed last


def run_all(link) -> bool:
    """Run every self-checking test in order and tally the result."""
    print("\nRunning all self-checking tests. The malformed frames wait out the "
          "response timeout each, so this is not instant.\n")

    results = {}
    for key in RUN_ALL_ORDER:
        name, builder, expect = MENU_OPTIONS[key]
        results[name] = send(link, name, builder(), expect)
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
    entries.update({key: (name, "[demo, confirm visually]")
                    for key, (name, _action) in ACTIONS.items()})

    print("\nSelect a frame to send:")
    for key in sorted(entries):
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
                failures += not run_all(link)
                continue

            option = MENU_OPTIONS.get(choice)
            if option is not None:
                name, builder, expect = option
                failures += not send(link, name, builder(), expect)
                continue

            action = ACTIONS.get(choice)
            if action is not None:
                _name, action_fn = action
                failures += not action_fn(mcu)
                continue

            print("Not a valid choice, try again.")

    if failures:
        print(f"\n{failures} test(s) failed this session.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
