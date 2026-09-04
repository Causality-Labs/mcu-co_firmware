"""
Host-side tests for mcuco.client, driven by the same fake serial stream as
test_link. Each command test asserts the exact bytes the client puts on the
wire against the worked example in mcu-co_Protocol.md.

Run: python3 -m unittest discover -s tools/mcu-co-cli/tests -v
"""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from test_link import FakeSerial, frame  # noqa: E402

from mcuco.client import McuCo  # noqa: E402
from mcuco.link import McuCoLink  # noqa: E402
from mcuco.protocol import Action, Dir, Edge, Level, Polarity, Port, Response  # noqa: E402

ACK = frame("A5 01 01 1F 3E")
READ_HIGH = frame("A5 02 01 01 EC 81")
DUTY_250 = frame("A5 03 01 FA 00 26 D4")
FREQ_1KHZ = frame("A5 05 01 E8 03 00 00 39 BF")


def mcu(rx: bytes = ACK):
    """A client wired to a canned response, plus the stream to inspect afterwards."""
    stream = FakeSerial(rx)
    return McuCo(McuCoLink.from_stream(stream, timeout=0.05)), stream


# --- gpio_cfg ---

class GpioCfg(unittest.TestCase):
    # Matches the doc's section 1 frame for "gpio cfg output A 5".
    def test_SendsDocCfgFrame(self):
        client, stream = mcu()
        client.gpio_cfg(Dir.OUTPUT, Port.A, 5)
        self.assertEqual(bytes(stream.written), frame("A5 30 03 01 00 05 AB E1"))

    # Raw ints are accepted wherever an enum is, so CLI-parsed values pass through.
    def test_AcceptsPlainIntegerArguments(self):
        client, stream = mcu()
        client.gpio_cfg(1, 0, 5)
        self.assertEqual(bytes(stream.written), frame("A5 30 03 01 00 05 AB E1"))

    # The decoded ACK is handed back to the caller.
    def test_ReturnsDecodedResponse(self):
        client, _ = mcu()
        self.assertEqual(client.gpio_cfg(Dir.OUTPUT, Port.A, 5), Response(ack=True))

    # DIR is input=0/output=1 only; 2 would be silently truncated into the frame.
    def test_RejectsInvalidDirection(self):
        client, stream = mcu()
        with self.assertRaises(ValueError):
            client.gpio_cfg(2, Port.A, 5)
        self.assertEqual(bytes(stream.written), b"")  # nothing reached the wire

    # Port G is the highest valid port; H does not exist.
    def test_RejectsOutOfRangePort(self):
        client, _ = mcu()
        with self.assertRaises(ValueError):
            client.gpio_cfg(Dir.OUTPUT, 7, 5)

    # Pins are 0-15; 16 would overflow the nibble the MCU validates.
    def test_RejectsOutOfRangePin(self):
        client, _ = mcu()
        with self.assertRaises(ValueError):
            client.gpio_cfg(Dir.OUTPUT, Port.A, 16)

    # A negative pin is rejected rather than wrapping when packed into a byte.
    def test_RejectsNegativePin(self):
        client, _ = mcu()
        with self.assertRaises(ValueError):
            client.gpio_cfg(Dir.OUTPUT, Port.A, -1)

    # A non-integer pin fails here instead of raising deep inside bytes().
    def test_RejectsNonIntegerPin(self):
        client, _ = mcu()
        with self.assertRaises(ValueError):
            client.gpio_cfg(Dir.OUTPUT, Port.A, "5")


# --- gpio_set ---

class GpioSet(unittest.TestCase):
    # Matches the doc's section 2 frame for "gpio set high A 5".
    def test_SendsDocWriteFrame(self):
        client, stream = mcu()
        client.gpio_set(Level.HIGH, Port.A, 5)
        self.assertEqual(bytes(stream.written), frame("A5 31 03 01 00 05 FA 4B"))

    # LEVEL is low=0/high=1 only.
    def test_RejectsInvalidLevel(self):
        client, _ = mcu()
        with self.assertRaises(ValueError):
            client.gpio_set(3, Port.A, 5)


# --- gpio_get ---

class GpioGet(unittest.TestCase):
    # Matches the doc's section 3 frame for "gpio get A 5" (no value token).
    def test_SendsDocReadFrame(self):
        client, stream = mcu(READ_HIGH)
        client.gpio_get(Port.A, 5)
        self.assertEqual(bytes(stream.written), frame("A5 32 02 00 05 84 7B"))

    # The pin level arrives in the response's STATE field, not the ack flag.
    def test_ReturnsPinStateFromResponse(self):
        client, _ = mcu(READ_HIGH)
        self.assertEqual(client.gpio_get(Port.A, 5), Response(ack=True, data=bytes([1])))


# --- gpio_irq_cfg ---

class GpioIrqCfg(unittest.TestCase):
    # Matches the doc's section 4 frame for "gpio irq cfg both A 5".
    def test_SendsDocArmFrame(self):
        client, stream = mcu()
        client.gpio_irq_cfg(Edge.BOTH, Port.A, 5)
        self.assertEqual(bytes(stream.written), frame("A5 34 03 03 00 05 CD 06"))

    # Unlike bind, cfg does take 'off' (EDGE=0) — that is how a pin is disarmed.
    def test_AcceptsEdgeOffToDisarm(self):
        client, stream = mcu()
        client.gpio_irq_cfg(Edge.OFF, Port.A, 5)
        self.assertEqual(bytes(stream.written), frame("A5 34 03 00 00 05 9D 5F"))

    # EDGE is 0-3; 4 is not a defined trigger.
    def test_RejectsInvalidEdge(self):
        client, _ = mcu()
        with self.assertRaises(ValueError):
            client.gpio_irq_cfg(4, Port.A, 5)


# --- gpio_irq_bind ---

class GpioIrqBind(unittest.TestCase):
    # Matches the doc's section 5 frame: PC13 both-edge toggling PA5.
    def test_SendsDocBindFrame(self):
        client, stream = mcu()
        client.gpio_irq_bind(Edge.BOTH, Port.C, 13, Action.TOGGLE, Port.A, 5)
        self.assertEqual(bytes(stream.written), frame("A5 33 06 03 02 0D 02 00 05 92 93"))

    # EDGE_SELECT has no 'off': disarming is gpio_irq_cfg, unbinding is gpio_irq_unbind.
    def test_RejectsEdgeOff(self):
        client, stream = mcu()
        with self.assertRaises(ValueError):
            client.gpio_irq_bind(Edge.OFF, Port.C, 13, Action.TOGGLE, Port.A, 5)
        self.assertEqual(bytes(stream.written), b"")

    # ACTION is low/high/toggle (0-2) only.
    def test_RejectsInvalidAction(self):
        client, _ = mcu()
        with self.assertRaises(ValueError):
            client.gpio_irq_bind(Edge.BOTH, Port.C, 13, 3, Port.A, 5)

    # The output pin is range-checked the same as the input pin.
    def test_RejectsOutOfRangeOutputPin(self):
        client, _ = mcu()
        with self.assertRaises(ValueError):
            client.gpio_irq_bind(Edge.BOTH, Port.C, 13, Action.TOGGLE, Port.A, 16)


# --- gpio_irq_unbind ---

class GpioIrqUnbind(unittest.TestCase):
    # Matches the doc's section 6 frame for "gpio irq unbind A 5".
    def test_SendsDocUnbindFrame(self):
        client, stream = mcu()
        client.gpio_irq_unbind(Port.A, 5)
        self.assertEqual(bytes(stream.written), frame("A5 35 02 00 05 A9 2A"))


if __name__ == "__main__":
    unittest.main()


# --- pwm_group_cfg ---

class PwmGroupCfg(unittest.TestCase):
    # Matches the doc's section 7 frame for "pwm group cfg 1000 0".
    def test_SendsDocGroupCfgFrame(self):
        client, stream = mcu()
        client.pwm_group_cfg(1000, 0)
        self.assertEqual(bytes(stream.written), frame("A5 40 05 E8 03 00 00 00 DE CD"))

    # FREQ is 32-bit little-endian; a value using all four bytes catches a
    # byte-swapped or truncated encode that 1000 would not.
    def test_EncodesFrequencyLittleEndian(self):
        client, stream = mcu()
        client.pwm_group_cfg(100000, 0)
        self.assertEqual(bytes(stream.written)[3:7], bytes([0xA0, 0x86, 0x01, 0x00]))

    # Valid range is 1 Hz to 1 MHz; 0 is not shorthand for teardown.
    def test_RejectsFrequencyOutOfRange(self):
        client, stream = mcu()
        for bad in (0, 1_000_001):
            with self.assertRaises(ValueError):
                client.pwm_group_cfg(bad, 0)
        self.assertEqual(bytes(stream.written), b"")

    # There are three groups; 3 would be silently truncated into the frame.
    def test_RejectsGroupOutOfRange(self):
        client, stream = mcu()
        with self.assertRaises(ValueError):
            client.pwm_group_cfg(1000, 3)
        self.assertEqual(bytes(stream.written), b"")


# --- pwm_group_release ---

class PwmGroupRelease(unittest.TestCase):
    # Matches the doc's section 7.5 frame for "pwm group release 0".
    def test_SendsDocGroupReleaseFrame(self):
        client, stream = mcu()
        client.pwm_group_release(0)
        self.assertEqual(bytes(stream.written), frame("A5 46 01 00 A0 50"))

    def test_RejectsGroupOutOfRange(self):
        client, _ = mcu()
        with self.assertRaises(ValueError):
            client.pwm_group_release(3)


# --- pwm_group_get ---

class PwmGroupGet(unittest.TestCase):
    # Matches the doc's section 12 frame for "pwm group get 0".
    def test_SendsDocGroupGetFrame(self):
        client, stream = mcu(FREQ_1KHZ)
        client.pwm_group_get(0)
        self.assertEqual(bytes(stream.written), frame("A5 45 01 00 F0 09"))

    # The achieved frequency comes back as a 4-byte little-endian value.
    def test_ReturnsAchievedFrequency(self):
        client, _ = mcu(FREQ_1KHZ)
        self.assertEqual(client.pwm_group_get(0).value, 1000)


# --- pwm_channel_cfg ---

class PwmChannelCfg(unittest.TestCase):
    # Matches the doc's section 8 frame for "pwm channel cfg high A 5".
    def test_SendsDocChannelCfgFrame(self):
        client, stream = mcu()
        client.pwm_channel_cfg(Polarity.ACTIVE_HIGH, Port.A, 5)
        self.assertEqual(bytes(stream.written), frame("A5 41 03 00 00 05 4C 61"))

    # POL is high=0/low=1 only.
    def test_RejectsInvalidPolarity(self):
        client, stream = mcu()
        with self.assertRaises(ValueError):
            client.pwm_channel_cfg(2, Port.A, 5)
        self.assertEqual(bytes(stream.written), b"")


# --- pwm_channel_set ---

class PwmChannelSet(unittest.TestCase):
    # Matches the doc's section 9 frame for "pwm channel set 250 A 5" (25.0%).
    def test_SendsDocChannelSetFrame(self):
        client, stream = mcu()
        client.pwm_channel_set(250, Port.A, 5)
        self.assertEqual(bytes(stream.written), frame("A5 42 04 FA 00 00 05 05 C1"))

    # DUTY is 16-bit little-endian; 1000 (0x03E8) has two distinct bytes, so a
    # byte-swapped encode cannot pass.
    def test_EncodesDutyLittleEndian(self):
        client, stream = mcu()
        client.pwm_channel_set(1000, Port.A, 5)
        self.assertEqual(bytes(stream.written)[3:5], bytes([0xE8, 0x03]))

    # Duty is tenths of a percent, so 1000 is full scale and 1001 is not a duty.
    def test_RejectsDutyAboveFullScale(self):
        client, stream = mcu()
        with self.assertRaises(ValueError):
            client.pwm_channel_set(1001, Port.A, 5)
        self.assertEqual(bytes(stream.written), b"")


# --- pwm_channel_release ---

class PwmChannelRelease(unittest.TestCase):
    # Matches the doc's section 10 frame for "pwm channel release A 5".
    def test_SendsDocChannelReleaseFrame(self):
        client, stream = mcu()
        client.pwm_channel_release(Port.A, 5)
        self.assertEqual(bytes(stream.written), frame("A5 43 02 00 05 45 4F"))


# --- pwm_channel_get ---

class PwmChannelGet(unittest.TestCase):
    # Matches the doc's section 11 frame for "pwm channel get A 5".
    def test_SendsDocChannelGetFrame(self):
        client, stream = mcu(DUTY_250)
        client.pwm_channel_get(Port.A, 5)
        self.assertEqual(bytes(stream.written), frame("A5 44 02 00 05 68 1E"))

    # The duty comes back as a 2-byte little-endian value, 250 = 25.0%.
    def test_ReturnsDutyFromResponse(self):
        client, _ = mcu(DUTY_250)
        self.assertEqual(client.pwm_channel_get(Port.A, 5).value, 250)
