"""
Host-side tests for mcuco.protocol. No hardware, no serial port.

Every expected byte string here is copied from a worked example in
mcu-co_Protocol.md, so a failure means firmware, doc and host have drifted
apart — not just that a Python detail changed.

Run: python3 -m unittest discover -s tools/mcu-co-cli/tests -v
"""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from mcuco.protocol import (  # noqa: E402
    MAX_PAYLOAD,
    Action,
    Dir,
    Edge,
    Level,
    Opcode,
    Port,
    ProtocolError,
    Response,
    build_command,
    crc16_ccitt_false,
    parse_response,
    response_frame_len,
)


def frame(text: str) -> bytes:
    return bytes.fromhex(text.replace(" ", ""))


# --- crc16_ccitt_false ---

class Crc16CcittFalse(unittest.TestCase):
    # CCITT-FALSE seeds at 0xFFFF, so empty input returns the seed unchanged.
    def test_EmptyInputReturnsInitialSeed(self):
        self.assertEqual(crc16_ccitt_false(b""), 0xFFFF)

    # Matches the CRC the doc's gpio cfg example carries (transmitted AB E1).
    def test_MatchesDocCommandVector(self):
        self.assertEqual(crc16_ccitt_false(bytes([0x30, 0x03, 0x01, 0x00, 0x05])), 0xE1AB)

    # Matches the CRC the doc's bare ACK carries (transmitted 1F 3E).
    def test_MatchesDocAckVector(self):
        self.assertEqual(crc16_ccitt_false(bytes([0x01, 0x01])), 0x3E1F)


# --- build_command ---

class BuildCommand(unittest.TestCase):
    # Every worked command frame in mcu-co_Protocol.md sections 1-6.
    def test_ReproducesDocCommandFrames(self):
        cases = [
            ("gpio cfg output A 5",
             Opcode.GPIO_CFG, bytes([Dir.OUTPUT, Port.A, 5]),
             "A5 30 03 01 00 05 AB E1"),
            ("gpio set high A 5",
             Opcode.GPIO_WRITE, bytes([Level.HIGH, Port.A, 5]),
             "A5 31 03 01 00 05 FA 4B"),
            ("gpio get A 5",
             Opcode.GPIO_READ, bytes([Port.A, 5]),
             "A5 32 02 00 05 84 7B"),
            ("gpio irq cfg both A 5",
             Opcode.GPIO_IRQ_CFG, bytes([Edge.BOTH, Port.A, 5]),
             "A5 34 03 03 00 05 CD 06"),
            ("gpio irq cfg off A 5",
             Opcode.GPIO_IRQ_CFG, bytes([Edge.OFF, Port.A, 5]),
             "A5 34 03 00 00 05 9D 5F"),
            ("gpio irq bind both C 13 toggle A 5",
             Opcode.GPIO_IRQ_BIND,
             bytes([Edge.BOTH, Port.C, 13, Action.TOGGLE, Port.A, 5]),
             "A5 33 06 03 02 0D 02 00 05 92 93"),
            ("gpio irq unbind A 5",
             Opcode.GPIO_IRQ_UNBIND, bytes([Port.A, 5]),
             "A5 35 02 00 05 A9 2A"),
        ]
        for cli, opcode, payload, expected in cases:
            with self.subTest(cli=cli):
                self.assertEqual(build_command(opcode, payload), frame(expected))

    # LEN is derived from the payload, not passed in, so it can never disagree.
    def test_LengthByteMatchesPayloadLength(self):
        built = build_command(Opcode.GPIO_IRQ_BIND, bytes(6))
        self.assertEqual(built[2], 6)

    # A payload-less opcode still produces a well-formed 5-byte frame.
    def test_EmptyPayloadProducesMinimalFrame(self):
        built = build_command(Opcode.GPIO_READ)
        self.assertEqual(len(built), 5)
        self.assertEqual(built[2], 0)

    # frame_parser_feed() rejects LEN > RX_MAX_PAYLOAD, so refuse to build one.
    def test_RejectsPayloadOverMaxPayload(self):
        with self.assertRaises(ValueError):
            build_command(Opcode.GPIO_CFG, bytes(MAX_PAYLOAD + 1))

    # The largest legal payload is still accepted.
    def test_AcceptsPayloadAtMaxPayload(self):
        self.assertEqual(build_command(Opcode.GPIO_CFG, bytes(MAX_PAYLOAD))[2], MAX_PAYLOAD)


# --- response_frame_len ---

class ResponseFrameLen(unittest.TestCase):
    # LEN=1 is a bare ACK/NACK: 5 bytes on the wire; LEN=2 adds STATE.
    def test_SizesBareAndStateCarryingResponses(self):
        self.assertEqual(response_frame_len(1), 5)
        self.assertEqual(response_frame_len(2), 6)


# --- parse_response ---

class ParseResponse(unittest.TestCase):
    # The doc's bare ACK decodes with no STATE field.
    def test_DecodesDocAck(self):
        self.assertEqual(parse_response(frame("A5 01 01 1F 3E")), Response(ack=True, state=None))

    # The doc's bare NACK decodes as a failure with no STATE field.
    def test_DecodesDocNack(self):
        self.assertEqual(parse_response(frame("A5 01 00 3E 2E")), Response(ack=False, state=None))

    # The doc's GPIO_READ response carries the pin level in STATE.
    def test_DecodesDocReadResponseWithState(self):
        self.assertEqual(parse_response(frame("A5 02 01 01 EC 81")), Response(ack=True, state=1))

    # A single-bit flip in the CRC is caught rather than silently accepted.
    def test_RejectsCrcMismatch(self):
        with self.assertRaises(ProtocolError):
            parse_response(frame("A5 01 01 1F 3F"))

    # Anything not anchored on SOF is refused; the caller must resync first.
    def test_RejectsBadStartOfFrame(self):
        with self.assertRaises(ProtocolError):
            parse_response(frame("5A 01 01 1F 3E"))

    # Firmware only ever emits LEN 1 or 2; anything else is a desynced stream.
    def test_RejectsUnexpectedLength(self):
        with self.assertRaises(ProtocolError):
            parse_response(frame("A5 03 01 01 EC 81"))

    # A frame cut short before its CRC is an error, not a partial decode.
    def test_RejectsTruncatedFrame(self):
        with self.assertRaises(ProtocolError):
            parse_response(frame("A5 01 01 1F"))

    # Fewer bytes than the smallest legal response cannot be decoded at all.
    def test_RejectsRunt(self):
        with self.assertRaises(ProtocolError):
            parse_response(frame("A5 01"))

    # Trailing bytes past a complete frame are ignored, not treated as corruption.
    def test_IgnoresTrailingBytesAfterCompleteFrame(self):
        self.assertEqual(parse_response(frame("A5 01 01 1F 3E A5 A5")), Response(ack=True, state=None))


if __name__ == "__main__":
    unittest.main()
