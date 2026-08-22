"""
Host-side tests for mcuco.cli.

Two ways in, neither of which opens a port: --dry-run asserts the frame the
grammar produces, and a `connect` factory injected into main() supplies a canned
response so exit codes can be checked.

Run: python3 -m unittest discover -s tools/mcu-co-cli/tests -v
"""

import contextlib
import io
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from test_link import FakeSerial, frame  # noqa: E402

from mcuco.cli import EXIT_ACK, EXIT_NACK, EXIT_NO_RESPONSE, EXIT_USAGE, main  # noqa: E402
from mcuco.client import McuCo  # noqa: E402
from mcuco.link import McuCoLink  # noqa: E402

ACK = frame("A5 01 01 1F 3E")
NACK = frame("A5 01 00 3E 2E")
READ_HIGH = frame("A5 02 01 01 EC 81")


def connector(rx: bytes, sink=None):
    """A stand-in for McuCo.connect that replays `rx` and records what was written."""
    def connect(_device, _baud, timeout):
        stream = FakeSerial(rx)
        if sink is not None:
            sink.append(stream)
        return McuCo(McuCoLink.from_stream(stream, timeout=0.02))
    return connect


def run(argv, rx=ACK, sink=None):
    """Run the CLI, returning (exit_code, stdout)."""
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        code = main(argv, connect=connector(rx, sink))
    return code, out.getvalue().strip()


def dry_run(argv):
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        code = main(argv)
    return code, out.getvalue().strip()


# --- grammar -> frame ---

class Grammar(unittest.TestCase):
    # Each CLI command in mcu-co_Protocol.md produces that section's worked frame.
    def test_ProducesDocFrameForEveryCommand(self):
        cases = [
            (["gpio", "cfg", "output", "A", "5"], "a5 30 03 01 00 05 ab e1"),
            (["gpio", "set", "high", "A", "5"], "a5 31 03 01 00 05 fa 4b"),
            (["gpio", "get", "A", "5"], "a5 32 02 00 05 84 7b"),
            (["gpio", "irq", "cfg", "both", "A", "5"], "a5 34 03 03 00 05 cd 06"),
            (["gpio", "irq", "cfg", "off", "A", "5"], "a5 34 03 00 00 05 9d 5f"),
            (["gpio", "irq", "bind", "both", "C", "13", "toggle", "A", "5"],
             "a5 33 06 03 02 0d 02 00 05 92 93"),
            (["gpio", "irq", "unbind", "A", "5"], "a5 35 02 00 05 a9 2a"),
        ]
        for argv, expected in cases:
            with self.subTest(command=" ".join(argv)):
                code, out = dry_run(["--dry-run"] + argv)
                self.assertEqual(code, EXIT_ACK)
                self.assertEqual(out, expected)

    # Port letters are accepted in either case, as a user would type them.
    def test_AcceptsLowercasePortLetter(self):
        self.assertEqual(dry_run(["-n", "gpio", "cfg", "output", "a", "5"])[1],
                         "a5 30 03 01 00 05 ab e1")

    # --dry-run must not touch the device, so it works with no board present.
    def test_DryRunOpensNoDevice(self):
        code, out = dry_run(["-n", "-d", "/dev/does-not-exist", "gpio", "get", "A", "5"])
        self.assertEqual(code, EXIT_ACK)
        self.assertEqual(out, "a5 32 02 00 05 84 7b")


# --- exit codes ---

class ExitCodes(unittest.TestCase):
    # An ACK is a successful command: exit 0.
    def test_AckExitsZero(self):
        self.assertEqual(run(["gpio", "cfg", "output", "A", "5"], rx=ACK)[0], EXIT_ACK)

    # A NACK is a rejected command, which must not look like success.
    def test_NackExitsOne(self):
        code, out = run(["gpio", "cfg", "output", "A", "5"], rx=NACK)
        self.assertEqual(code, EXIT_NACK)
        self.assertEqual(out, "NACK")

    # Silence (the firmware's answer to a bad CRC/frame) is distinct from a NACK.
    def test_SilenceExitsNoResponse(self):
        with contextlib.redirect_stderr(io.StringIO()):
            code, _ = run(["gpio", "cfg", "output", "A", "5"], rx=b"")
        self.assertEqual(code, EXIT_NO_RESPONSE)

    # A corrupt reply is a link failure, not a protocol-level NACK.
    def test_CorruptResponseExitsNoResponse(self):
        with contextlib.redirect_stderr(io.StringIO()):
            code, _ = run(["gpio", "get", "A", "5"], rx=frame("A5 01 01 1F 3F"))
        self.assertEqual(code, EXIT_NO_RESPONSE)

    # gpio get reports the pin level it read, and still exits 0 on ACK.
    def test_ReadPrintsPinState(self):
        code, out = run(["gpio", "get", "A", "5"], rx=READ_HIGH)
        self.assertEqual(code, EXIT_ACK)
        self.assertIn("high", out)


# --- wire output ---

class WireOutput(unittest.TestCase):
    # A live run puts the same bytes on the wire that --dry-run advertises.
    def test_LiveRunWritesDocFrame(self):
        streams = []
        run(["gpio", "irq", "bind", "both", "C", "13", "toggle", "A", "5"], rx=ACK, sink=streams)
        self.assertEqual(bytes(streams[0].written), frame("A5 33 06 03 02 0D 02 00 05 92 93"))


# --- usage errors ---

class UsageErrors(unittest.TestCase):
    # Usage errors keep argparse's exit 2, distinct from a silent MCU (3).
    def test_RejectsInvalidTokensWithUsageExit(self):
        cases = {
            "port H does not exist": ["gpio", "cfg", "output", "H", "5"],
            "pin above 15": ["gpio", "cfg", "output", "A", "16"],
            "non-numeric pin": ["gpio", "cfg", "output", "A", "five"],
            "bad direction": ["gpio", "cfg", "sideways", "A", "5"],
            "bad level": ["gpio", "set", "sideways", "A", "5"],
            "missing subcommand": ["gpio"],
            "missing irq subcommand": ["gpio", "irq"],
            "get takes no value token": ["gpio", "get", "high", "A", "5"],
        }
        for reason, argv in cases.items():
            with self.subTest(reason=reason):
                with contextlib.redirect_stderr(io.StringIO()):
                    with self.assertRaises(SystemExit) as caught:
                        main(["--dry-run"] + argv)
                self.assertEqual(caught.exception.code, EXIT_USAGE)

    # 'bind off' is not in the grammar: disarming is 'irq cfg off', dropping is 'irq unbind'.
    def test_RejectsOffAsBindEdge(self):
        with contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as caught:
                main(["--dry-run", "gpio", "irq", "bind", "off", "C", "13", "toggle", "A", "5"])
        self.assertEqual(caught.exception.code, EXIT_USAGE)

    # 'irq cfg off' stays legal — the same token is valid in the cfg slot.
    def test_AcceptsOffAsCfgEdge(self):
        self.assertEqual(dry_run(["-n", "gpio", "irq", "cfg", "off", "A", "5"])[0], EXIT_ACK)


if __name__ == "__main__":
    unittest.main()
