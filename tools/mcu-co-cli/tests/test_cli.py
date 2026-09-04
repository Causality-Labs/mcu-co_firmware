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
DUTY_250 = frame("A5 03 01 FA 00 26 D4")
FREQ_1KHZ = frame("A5 05 01 E8 03 00 00 39 BF")


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

    # A NACK is a rejected command, which must not look like success. This one
    # carries no reason byte, so there is nothing to report but the failure.
    def test_NackExitsOne(self):
        code, out = run(["gpio", "cfg", "output", "A", "5"], rx=NACK)
        self.assertEqual(code, EXIT_NACK)
        self.assertEqual(out, "FAILED")

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


# --- pwm grammar -> frame ---

class PwmGrammar(unittest.TestCase):
    # Every pwm verb in the doc's command reference, against its worked frame.
    def test_BuildsDocFrames(self):
        cases = [
            (["pwm", "group", "cfg", "1000", "0"], "a5 40 05 e8 03 00 00 00 de cd"),
            (["pwm", "group", "get", "0"], "a5 45 01 00 f0 09"),
            (["pwm", "group", "release", "0"], "a5 46 01 00 a0 50"),
            (["pwm", "channel", "cfg", "high", "A", "5"], "a5 41 03 00 00 05 4c 61"),
            (["pwm", "channel", "set", "250", "A", "5"], "a5 42 04 fa 00 00 05 05 c1"),
            (["pwm", "channel", "release", "A", "5"], "a5 43 02 00 05 45 4f"),
            (["pwm", "channel", "get", "A", "5"], "a5 44 02 00 05 68 1e"),
        ]
        for argv, expected in cases:
            with self.subTest(command=" ".join(argv)):
                code, out = dry_run(["--dry-run"] + argv)
                self.assertEqual(code, EXIT_ACK)
                self.assertEqual(out, expected)

    # Out-of-range values are a usage error, caught before a port is opened -
    # exit 2, distinct from a NACK the MCU would have sent back.
    def test_RejectsOutOfRangeArgumentsAsUsageErrors(self):
        cases = {
            "group above 2": ["pwm", "group", "get", "3"],
            "duty above full scale": ["pwm", "channel", "set", "1001", "A", "5"],
            "frequency of zero": ["pwm", "group", "cfg", "0", "0"],
            "frequency above 1 MHz": ["pwm", "group", "cfg", "1000001", "0"],
        }
        for reason, argv in cases.items():
            with self.subTest(reason=reason):
                with contextlib.redirect_stderr(io.StringIO()):
                    with self.assertRaises(SystemExit) as caught:
                        main(["--dry-run"] + argv)
                self.assertEqual(caught.exception.code, EXIT_USAGE)


# --- pwm read output ---

class PwmReadOutput(unittest.TestCase):
    # A duty is tenths of a percent on the wire; the CLI reports the percentage
    # a user asked for, not the raw 250.
    def test_ChannelGetPrintsDutyAsPercent(self):
        code, out = run(["pwm", "channel", "get", "A", "5"], rx=DUTY_250)
        self.assertEqual(code, EXIT_ACK)
        self.assertIn("25.0", out)

    # group get exists to report the *achieved* frequency, so it must print it.
    def test_GroupGetPrintsFrequencyInHz(self):
        code, out = run(["pwm", "group", "get", "0"], rx=FREQ_1KHZ)
        self.assertEqual(code, EXIT_ACK)
        self.assertIn("1000", out)

    # A NACK reports the reason the firmware sent, not a bare refusal.
    def test_NackPrintsItsReason(self):
        code, out = run(["pwm", "channel", "get", "A", "5"], rx=frame("A5 02 00 04 78 E2"))
        self.assertEqual(code, EXIT_NACK)
        self.assertIn("ERR_INVALID_STATE", out)


# --- success/failure reporting ---

class Verdict(unittest.TestCase):
    # An ACK has to say plainly that the command worked, not just print a value.
    def test_AckSaysTheCommandSucceeded(self):
        code, out = run(["pwm", "channel", "get", "A", "5"], rx=DUTY_250)
        self.assertEqual(code, EXIT_ACK)
        self.assertIn("OK", out)
        self.assertIn("25.0", out)

    # A NACK has to say the command failed, and why.
    def test_NackSaysTheCommandFailed(self):
        code, out = run(["pwm", "group", "cfg", "1000", "0"], rx=frame("A5 02 00 06 3A C2"))
        self.assertEqual(code, EXIT_NACK)
        self.assertIn("FAILED", out)
        self.assertIn("ERR_BUSY", out)


# --- --show-frames ---

class ShowFrames(unittest.TestCase):
    # Both directions are printed as hex, so a bad reply can be read by eye.
    def test_PrintsSentAndReceivedFrames(self):
        code, out = run(["--show-frames", "pwm", "channel", "get", "A", "5"], rx=DUTY_250)
        self.assertEqual(code, EXIT_ACK)
        self.assertIn("TX  a5 44 02 00 05 68 1e", out)
        self.assertIn("RX  a5 03 01 fa 00 26 d4", out)

    # Without the flag the frames stay out of the way.
    def test_StaysQuietByDefault(self):
        _, out = run(["pwm", "channel", "get", "A", "5"], rx=DUTY_250)
        self.assertNotIn("TX", out)

    # A CRC failure is exactly when the bytes matter, so they must survive the
    # error path that would otherwise report only a mismatch.
    def test_PrintsReceivedFrameOnCrcFailure(self):
        out = io.StringIO()
        err = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = main(["--show-frames", "pwm", "channel", "get", "A", "5"],
                        connect=connector(frame("A5 03 01 FA 00 26 D5")))
        self.assertEqual(code, EXIT_NO_RESPONSE)
        self.assertIn("RX  a5 03 01 fa 00 26 d5", out.getvalue() + err.getvalue())
