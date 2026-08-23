#!/usr/bin/env python3
"""
Interactively sends one mcu-co protocol frame at a time over the command
UART (USART2), so you can pick a frame type and watch how the MCU reacts.

Open the log UART (USART1) yourself in another terminal / serial monitor
to see how the MCU responded to each frame.

Frame encoding lives in tools/mcu-co-cli/mcuco/protocol.py and the transport in
mcuco/link.py — this script is firmware bring-up, so it keeps the deliberately
malformed frames (bad CRC, oversized LEN) that exercise the parser's error paths
rather than the device. Those frames draw no reply at all: the firmware discards
a bad frame silently instead of NACKing it, so a timeout is the pass condition.
"""

import argparse
import sys
import time
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

# --- Defaults for the USB-serial adapter wired to USART2; override with -d/-b ---
CMD_PORT = '/dev/ttyACM0'
BAUDRATE = 115200
TIMEOUT_S = 1.0

LED_PIN = 5     # PA5, Nucleo user LED
BUTTON_PIN = 13  # PC13, Nucleo B1 user button


def send(link, name: str, frame: bytes) -> None:
    """Send one frame and report what came back, if anything."""
    print(f"Sent [{name}]: {frame.hex(' ')}")
    try:
        print(f"  <- {link.send(frame)}")
    except LinkTimeout as exc:
        print(f"  <- no response ({exc})")
    except ProtocolError as exc:
        print(f"  <- malformed response ({exc})")


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


def step(name: str, call) -> None:
    """Run one client call and report what came back, if anything."""
    print(f"Sent [{name}]")
    try:
        print(f"  <- {call()}")
    except LinkTimeout as exc:
        print(f"  <- no response ({exc})")
    except ProtocolError as exc:
        print(f"  <- malformed response ({exc})")


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

    for name, call in steps:
        step(name, call)
        time.sleep(step_delay_s)


def run_blink_led_test(mcu, toggle_count: int = 20, period_s: float = 0.5):
    """
    Configure PA5 (the Nucleo board's LED pin) as an output, then drive it
    high/low alternately every `period_s` seconds, `toggle_count` times.
    Watch the LED, or the log UART (USART1), to confirm each toggle. Ctrl+C
    stops early.
    """
    step("gpio cfg output A 5", lambda: mcu.gpio_cfg(Dir.OUTPUT, Port.A, LED_PIN))
    time.sleep(period_s)

    level = Level.HIGH
    try:
        for _ in range(toggle_count):
            step(f"gpio set {'high' if level else 'low'} A 5",
                 lambda lvl=level: mcu.gpio_set(lvl, Port.A, LED_PIN))
            level = Level.LOW if level else Level.HIGH
            time.sleep(period_s)
    except KeyboardInterrupt:
        print("\nBlink test stopped early.")


MENU_OPTIONS = {
    '1': ("Valid frame", build_cfg_output_pa5),
    '2': ("Valid frame, wrong CRC", build_valid_frame_wrong_crc),
    '3': ("Invalid frame (LEN > MAX_PAYLOAD)", build_invalid_frame),
    '4': ("Invalid frame, wrong CRC", build_invalid_frame_wrong_crc),
    '6': ("Configure PC13 as input (Nucleo B1 button)", build_cfg_input_pc13),
    '7': ("Read PC13 state (Nucleo B1 button)", build_gpio_read_pc13),
}

ACTIONS = {
    '5': ("Blink LED (PA5): cfg output, then toggle high/low", run_blink_led_test),
    '8': ("Bind button (PC13) both-edge -> toggle LED (PA5)", run_irq_bind_demo),
}


def print_menu():
    print("\nSelect a frame to send:")
    for key, (name, _builder) in MENU_OPTIONS.items():
        print(f"  {key}) {name}")
    for key, (name, _action) in ACTIONS.items():
        print(f"  {key}) {name}")
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

    with link:
        mcu = McuCo(link)

        while True:
            print_menu()
            choice = input("> ").strip().lower()

            if choice == 'q':
                break

            option = MENU_OPTIONS.get(choice)
            if option is not None:
                name, builder = option
                send(link, name, builder())
                continue

            action = ACTIONS.get(choice)
            if action is not None:
                _name, action_fn = action
                action_fn(mcu)
                continue

            print("Not a valid choice, try again.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
