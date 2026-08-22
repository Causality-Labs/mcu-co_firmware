"""
Host-side tests for mcuco.link, driven by a fake serial stream.

No port is opened: McuCoLink.from_stream() takes the fake below, so the resync
and framing logic is exercised without a board. A read() that returns b"" is
how pyserial reports a timeout, and that is how starvation is simulated here.

Run: python3 -m unittest discover -s tools/mcu-co-cli/tests -v
"""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from mcuco.link import LinkError, LinkTimeout, McuCoLink  # noqa: E402
from mcuco.protocol import Opcode, Port, ProtocolError, Response  # noqa: E402


class FakeSerial:
    """Serial-like stub: replays `rx` to the reader, records everything written."""

    def __init__(self, rx: bytes = b""):
        self.rx = bytearray(rx)
        self.written = bytearray()
        self.resets = 0

    def read(self, size: int = 1) -> bytes:
        chunk = bytes(self.rx[:size])
        del self.rx[: len(chunk)]
        return chunk  # short/empty read == pyserial timeout

    def write(self, data: bytes) -> int:
        self.written += data
        return len(data)

    def reset_input_buffer(self) -> None:
        self.resets += 1

    def close(self) -> None:
        pass


def frame(text: str) -> bytes:
    return bytes.fromhex(text.replace(" ", ""))


# --- read_response ---

class ReadResponse(unittest.TestCase):
    # A clean 5-byte ACK decodes straight through.
    def test_DecodesBareAck(self):
        link = McuCoLink.from_stream(FakeSerial(frame("A5 01 01 1F 3E")))
        self.assertEqual(link.read_response(), Response(ack=True, state=None))

    # A 6-byte GPIO_READ reply is read to its longer length, not cut at 5.
    def test_DecodesSixByteReadResponse(self):
        link = McuCoLink.from_stream(FakeSerial(frame("A5 02 01 01 EC 81")))
        self.assertEqual(link.read_response(), Response(ack=True, state=1))

    # Leading garbage before SOF is discarded rather than corrupting the frame.
    def test_ResyncsPastLeadingGarbage(self):
        link = McuCoLink.from_stream(FakeSerial(frame("DE AD BE EF A5 01 01 1F 3E")))
        self.assertEqual(link.read_response(), Response(ack=True, state=None))

    # Back-to-back replies are consumed one exchange at a time, in order.
    def test_ReadsConsecutiveResponsesIndependently(self):
        link = McuCoLink.from_stream(FakeSerial(frame("A5 01 01 1F 3E") + frame("A5 01 00 3E 2E")))
        self.assertEqual(link.read_response(), Response(ack=True, state=None))
        self.assertEqual(link.read_response(), Response(ack=False, state=None))

    # Silence is the firmware's answer to a CRC/framing error, so it must not hang.
    def test_RaisesTimeoutOnSilence(self):
        link = McuCoLink.from_stream(FakeSerial(b""))
        with self.assertRaises(LinkTimeout):
            link.read_response()

    # A stream that stops mid-frame is a timeout, not a partial decode.
    def test_RaisesTimeoutOnTruncatedFrame(self):
        link = McuCoLink.from_stream(FakeSerial(frame("A5 01 01")))
        with self.assertRaises(LinkTimeout):
            link.read_response()

    # SOF with nothing behind it cannot be sized, so it times out too.
    def test_RaisesTimeoutWhenStreamEndsAfterStartOfFrame(self):
        link = McuCoLink.from_stream(FakeSerial(frame("A5")))
        with self.assertRaises(LinkTimeout):
            link.read_response()

    # A corrupt CRC surfaces as a protocol error, distinct from a timeout.
    def test_PropagatesCrcErrorFromParser(self):
        link = McuCoLink.from_stream(FakeSerial(frame("A5 01 01 1F 3F")))
        with self.assertRaises(ProtocolError):
            link.read_response()


# --- send / send_command ---

class Send(unittest.TestCase):
    # send() puts the exact frame on the wire and hands back the decoded reply.
    def test_WritesFrameAndReturnsResponse(self):
        stream = FakeSerial(frame("A5 01 01 1F 3E"))
        link = McuCoLink.from_stream(stream)
        response = link.send(frame("A5 35 02 00 05 A9 2A"))
        self.assertEqual(bytes(stream.written), frame("A5 35 02 00 05 A9 2A"))
        self.assertEqual(response, Response(ack=True, state=None))

    # Stale bytes from an abandoned exchange are flushed before a new command.
    def test_ClearsInputBufferBeforeWriting(self):
        stream = FakeSerial(frame("A5 01 01 1F 3E"))
        McuCoLink.from_stream(stream).send(frame("A5 35 02 00 05 A9 2A"))
        self.assertEqual(stream.resets, 1)

    # send_command() builds the frame itself, matching the doc's gpio get vector.
    def test_BuildsCommandFrameFromOpcodeAndPayload(self):
        stream = FakeSerial(frame("A5 02 01 01 EC 81"))
        link = McuCoLink.from_stream(stream)
        response = link.send_command(Opcode.GPIO_READ, bytes([Port.A, 5]))
        self.assertEqual(bytes(stream.written), frame("A5 32 02 00 05 84 7B"))
        self.assertEqual(response, Response(ack=True, state=1))


# --- open / close ---

class Lifecycle(unittest.TestCase):
    # Using a link before it is open fails loudly instead of on a None attribute.
    def test_SendBeforeOpenRaisesLinkError(self):
        with self.assertRaises(LinkError):
            McuCoLink("/dev/null").send(b"\xa5")

    # from_stream() is already usable, and the context manager leaves it so.
    def test_ContextManagerYieldsUsableLink(self):
        with McuCoLink.from_stream(FakeSerial(frame("A5 01 01 1F 3E"))) as link:
            self.assertEqual(link.read_response(), Response(ack=True, state=None))


if __name__ == "__main__":
    unittest.main()
