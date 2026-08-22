"""
Pure encode/decode for the mcu-co wire protocol (see mcu-co_Protocol.md).

Deliberately free of any I/O: no serial import, nothing that touches a port.
Everything here is bytes in, objects out, so it can be exercised on a host
with no board attached.

    command   SOF . OPCODE . LEN . PAYLOAD . CRC_L . CRC_H
    response  SOF . LEN . ACK/NACK [ . STATE ] . CRC_L . CRC_H

CRC16 is CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflect, no xorout) over
everything except SOF and the CRC bytes themselves.
"""

from dataclasses import dataclass
from enum import IntEnum

SOF = 0xA5
MAX_PAYLOAD = 32  # RX_MAX_PAYLOAD in frame_parser.h

RESPONSE_HEADER_LEN = 2  # SOF + LEN, read before the rest of the frame is sized
CRC_LEN = 2


class Opcode(IntEnum):
    GPIO_CFG = 0x30
    GPIO_WRITE = 0x31
    GPIO_READ = 0x32
    GPIO_IRQ_BIND = 0x33
    GPIO_IRQ_CFG = 0x34
    GPIO_IRQ_UNBIND = 0x35


class Port(IntEnum):
    A = 0
    B = 1
    C = 2
    D = 3
    E = 4
    F = 5
    G = 6


class Dir(IntEnum):
    INPUT = 0
    OUTPUT = 1


class Level(IntEnum):
    LOW = 0
    HIGH = 1


class Edge(IntEnum):
    OFF = 0
    RISING = 1
    FALLING = 2
    BOTH = 3


class Action(IntEnum):
    LOW = 0
    HIGH = 1
    TOGGLE = 2


class ProtocolError(Exception):
    """A byte sequence could not be decoded as a well-formed response frame."""


def crc16_ccitt_false(data: bytes) -> int:
    """Same algorithm as crc16_compute() in common/crc16.c."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def _crc_bytes(crc: int) -> bytes:
    return bytes([crc & 0xFF, (crc >> 8) & 0xFF])


def build_command(opcode: int, payload: bytes = b"") -> bytes:
    """Frame `payload` under `opcode` with a correct CRC."""
    if len(payload) > MAX_PAYLOAD:
        raise ValueError(f"payload of {len(payload)} exceeds MAX_PAYLOAD ({MAX_PAYLOAD})")

    body = bytes([opcode, len(payload)]) + payload
    return bytes([SOF]) + body + _crc_bytes(crc16_ccitt_false(body))


@dataclass(frozen=True)
class Response:
    ack: bool
    state: int | None = None  # GPIO_READ only; None when the frame carried no STATE

    def __str__(self) -> str:
        verdict = "ACK" if self.ack else "NACK"
        if self.state is None:
            return verdict
        return f"{verdict} state={self.state} ({'high' if self.state else 'low'})"


def response_frame_len(length_byte: int) -> int:
    """Total on-wire size of a response whose LEN field is `length_byte`."""
    return 1 + 1 + length_byte + CRC_LEN


def parse_response(raw: bytes) -> Response:
    """
    Decode a complete response frame.

    Raises ProtocolError on a bad SOF, an unexpected LEN, a truncated frame, or
    a CRC mismatch. The MCU only ever emits LEN 1 (bare ACK/NACK) or LEN 2
    (GPIO_READ, which appends STATE).
    """
    if len(raw) < response_frame_len(1):
        raise ProtocolError(f"response too short: {len(raw)} bytes")

    if raw[0] != SOF:
        raise ProtocolError(f"bad SOF: expected 0x{SOF:02X}, got 0x{raw[0]:02X}")

    length = raw[1]
    if length not in (1, 2):
        raise ProtocolError(f"bad LEN: expected 1 or 2, got {length}")

    expected = response_frame_len(length)
    if len(raw) < expected:
        raise ProtocolError(f"truncated frame: need {expected} bytes, got {len(raw)}")

    body = raw[1 : 2 + length]
    received_crc = raw[2 + length] | (raw[3 + length] << 8)
    computed_crc = crc16_ccitt_false(body)
    if received_crc != computed_crc:
        raise ProtocolError(f"CRC mismatch: computed 0x{computed_crc:04X}, received 0x{received_crc:04X}")

    ack = bool(raw[2])
    state = raw[3] if length == 2 else None
    return Response(ack=ack, state=state)
