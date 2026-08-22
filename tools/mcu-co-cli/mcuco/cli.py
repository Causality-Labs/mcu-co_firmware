"""
mcu-co command-line interface — a mock of the CLI that will run on the Linux
host SBC.

The grammar is a transcription of the command reference in mcu-co_Protocol.md,
so a command typed here is the command that will be typed there:

    mcu-co-cli gpio cfg output A 5
    mcu-co-cli gpio set high A 5
    mcu-co-cli gpio get C 13
    mcu-co-cli gpio irq cfg both C 13
    mcu-co-cli gpio irq bind both C 13 toggle A 5
    mcu-co-cli gpio irq unbind C 13

Exit codes are the scriptable result, since ACK/NACK is the only success signal
the protocol carries. Note EXIT_NO_RESPONSE is 3, not 2: argparse already exits
2 on a usage error, and a mistyped command must stay distinguishable from a
silent MCU.
"""

import argparse
import sys

from .client import McuCo
from .link import LinkError, LinkTimeout
from .protocol import Action, Dir, Edge, Level, Port, ProtocolError, build_command

EXIT_ACK = 0
EXIT_NACK = 1
EXIT_USAGE = 2  # argparse's own exit code for a malformed command line
EXIT_NO_RESPONSE = 3

DEFAULT_DEVICE = "/dev/ttyACM0"
DEFAULT_BAUDRATE = 115200
DEFAULT_TIMEOUT_S = 1.0

DIRECTIONS = {"input": Dir.INPUT, "output": Dir.OUTPUT}
LEVELS = {"low": Level.LOW, "high": Level.HIGH}
EDGES = {"off": Edge.OFF, "rising": Edge.RISING, "falling": Edge.FALLING, "both": Edge.BOTH}
BIND_EDGES = {name: edge for name, edge in EDGES.items() if edge != Edge.OFF}
ACTIONS = {"low": Action.LOW, "high": Action.HIGH, "toggle": Action.TOGGLE}


def port_arg(token: str) -> Port:
    """Port letter as written in the CLI grammar: A-G, case-insensitive."""
    try:
        return Port[token.upper()]
    except KeyError:
        valid = "/".join(p.name for p in Port)
        raise argparse.ArgumentTypeError(f"invalid port {token!r} (expected {valid})") from None


def pin_arg(token: str) -> int:
    try:
        pin = int(token)
    except ValueError:
        raise argparse.ArgumentTypeError(f"invalid pin {token!r} (expected an integer 0-15)") from None
    if not 0 <= pin <= 15:
        raise argparse.ArgumentTypeError(f"pin {pin} out of range (expected 0-15)")
    return pin


class DryRunMcuCo(McuCo):
    """
    Builds frames without opening a port.

    Subclasses the real client rather than reimplementing frame construction, so
    --dry-run exercises the same argument validation and payload packing that a
    live run would.
    """

    def __init__(self):
        super().__init__(link=None)
        self.frames = []

    def _send(self, opcode, payload):
        self.frames.append(build_command(opcode, payload))
        return None


def _add_target(parser):
    parser.add_argument("port", type=port_arg)
    parser.add_argument("pin", type=pin_arg)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="mcu-co-cli",
        description="Send mcu-co GPIO commands over the command UART.",
    )
    parser.add_argument("-d", "--device", default=DEFAULT_DEVICE,
                        help=f"serial device (default: {DEFAULT_DEVICE})")
    parser.add_argument("-b", "--baud", type=int, default=DEFAULT_BAUDRATE,
                        help=f"baud rate (default: {DEFAULT_BAUDRATE})")
    parser.add_argument("-t", "--timeout", type=float, default=DEFAULT_TIMEOUT_S,
                        help=f"response timeout in seconds (default: {DEFAULT_TIMEOUT_S})")
    parser.add_argument("-n", "--dry-run", action="store_true",
                        help="print the frame that would be sent, without opening the device")

    gpio = parser.add_subparsers(dest="group", required=True).add_parser(
        "gpio", help="GPIO commands")
    gpio_cmd = gpio.add_subparsers(dest="command", required=True)

    cfg = gpio_cmd.add_parser("cfg", help="configure pin direction")
    cfg.add_argument("direction", choices=sorted(DIRECTIONS))
    _add_target(cfg)

    write = gpio_cmd.add_parser("set", help="drive an output pin")
    write.add_argument("level", choices=sorted(LEVELS))
    _add_target(write)

    read = gpio_cmd.add_parser("get", help="read an input pin")
    _add_target(read)

    irq = gpio_cmd.add_parser("irq", help="interrupt commands")
    irq_cmd = irq.add_subparsers(dest="irq_command", required=True)

    irq_cfg = irq_cmd.add_parser("cfg", help="arm or disarm an EXTI trigger")
    irq_cfg.add_argument("edge", choices=sorted(EDGES))
    _add_target(irq_cfg)

    # 'off' is absent by design: unbinding is 'irq unbind', disarming is 'irq cfg off'.
    irq_bind = irq_cmd.add_parser("bind", help="attach an output action to an armed edge")
    irq_bind.add_argument("edge", choices=sorted(BIND_EDGES))
    irq_bind.add_argument("in_port", type=port_arg)
    irq_bind.add_argument("in_pin", type=pin_arg)
    irq_bind.add_argument("action", choices=sorted(ACTIONS))
    irq_bind.add_argument("out_port", type=port_arg)
    irq_bind.add_argument("out_pin", type=pin_arg)

    irq_unbind = irq_cmd.add_parser("unbind", help="drop a binding, leaving the trigger armed")
    _add_target(irq_unbind)

    return parser


def run_command(mcu: McuCo, args):
    if args.command == "cfg":
        return mcu.gpio_cfg(DIRECTIONS[args.direction], args.port, args.pin)
    if args.command == "set":
        return mcu.gpio_set(LEVELS[args.level], args.port, args.pin)
    if args.command == "get":
        return mcu.gpio_get(args.port, args.pin)

    if args.irq_command == "cfg":
        return mcu.gpio_irq_cfg(EDGES[args.edge], args.port, args.pin)
    if args.irq_command == "bind":
        return mcu.gpio_irq_bind(BIND_EDGES[args.edge], args.in_port, args.in_pin,
                                 ACTIONS[args.action], args.out_port, args.out_pin)
    return mcu.gpio_irq_unbind(args.port, args.pin)


def main(argv=None, connect=McuCo.connect) -> int:
    args = build_parser().parse_args(argv)

    if args.dry_run:
        mcu = DryRunMcuCo()
        run_command(mcu, args)
        for frame in mcu.frames:
            print(frame.hex(" "))
        return EXIT_ACK

    try:
        with connect(args.device, args.baud, args.timeout) as mcu:
            response = run_command(mcu, args)
    except (LinkTimeout, ProtocolError) as exc:
        print(f"no response: {exc}", file=sys.stderr)
        return EXIT_NO_RESPONSE
    except (LinkError, OSError) as exc:
        print(f"link error: {exc}", file=sys.stderr)
        return EXIT_NO_RESPONSE

    print(response)
    return EXIT_ACK if response.ack else EXIT_NACK
