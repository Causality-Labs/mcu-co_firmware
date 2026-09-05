"""
Pure encode/decode for the mcu-co wire protocol (see mcu-co_Protocol.md).

Deliberately free of any I/O: no serial import, nothing that touches a port.
Everything here is bytes in, objects out, so it can be exercised on a host
with no board attached.

    command   SOF . OPCODE . LEN . PAYLOAD . CRC_L . CRC_H
    response  SOF . LEN . ACK/NACK [ . DATA ] . CRC_L . CRC_H

CRC16 is CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflect, no xorout) over
everything except SOF and the CRC bytes themselves.
"""

from dataclasses import dataclass
from enum import IntEnum

SOF = 0xA5
MAX_PAYLOAD = 32  # RX_MAX_PAYLOAD in frame_parser.h

RESPONSE_HEADER_LEN = 2  # SOF + LEN, read before the rest of the frame is sized
CRC_LEN = 2

# Widest response DATA the protocol defines: PWM_GROUP_GET's uint32 frequency.
# TX_DATA_MAX in frame_parser.h.
MAX_RESPONSE_DATA = 4
MAX_RESPONSE_LEN = 1 + MAX_RESPONSE_DATA  # LEN counts the ACK/NACK byte too


class Opcode(IntEnum):
    GPIO_CFG = 0x30
    GPIO_WRITE = 0x31
    GPIO_READ = 0x32
    GPIO_IRQ_BIND = 0x33
    GPIO_IRQ_CFG = 0x34
    GPIO_IRQ_UNBIND = 0x35
    PWM_GROUP_CFG = 0x40
    PWM_CFG = 0x41
    PWM_SET = 0x42
    PWM_RELEASE = 0x43
    PWM_GET = 0x44
    PWM_GROUP_GET = 0x45
    PWM_GROUP_RELEASE = 0x46


class Polarity(IntEnum):
    """The level held during the active part of a PWM period."""

    ACTIVE_HIGH = 0
    ACTIVE_LOW = 1


class NackReason(IntEnum):
    """The firmware's own status_t values, sent verbatim as a NACK's DATA byte."""

    ERR = 0x01
    ERR_INVALID_ARG = 0x02
    ERR_INVALID_PIN = 0x03
    ERR_INVALID_STATE = 0x04
    ERR_NOT_INIT = 0x05
    ERR_BUSY = 0x06
    ERR_TIMEOUT = 0x07
    ERR_UNSUPPORTED = 0x08


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
    """
    A decoded response frame.

    DATA means one of two things and the ACK/NACK byte is what tells them
    apart: on an ACK it is the value a read command returned, width fixed per
    opcode; on a NACK it is always a single reason code. `data` is kept raw
    because LEN, not the opcode, is what sizes the frame - so this stays
    decodable without knowing which command was sent.
    """

    ack: bool
    data: bytes = b""

    @property
    def value(self) -> int | None:
        """The value a read returned, decoded little-endian. None unless a successful read."""
        if not self.ack or not self.data:
            return None
        return int.from_bytes(self.data, "little")

    @property
    def reason(self) -> NackReason | int | None:
        """Why the command failed. None on an ACK; the raw byte if the code is unknown."""
        if self.ack or not self.data:
            return None
        try:
            return NackReason(self.data[0])
        except ValueError:
            return self.data[0]

    def __str__(self) -> str:
        if not self.ack:
            reason = self.reason
            if reason is None:
                return "NACK"
            name = reason.name if isinstance(reason, NackReason) else f"0x{reason:02X}"
            return f"NACK reason={name}"

        if not self.data:
            return "ACK"
        return f"ACK value={self.value}"


def response_frame_len(length_byte: int) -> int:
    """Total on-wire size of a response whose LEN field is `length_byte`."""
    return 1 + 1 + length_byte + CRC_LEN


def parse_response(raw: bytes) -> Response:
    """
    Decode a complete response frame.

    Raises ProtocolError on a bad SOF, an unexpected LEN, a truncated frame, or
    a CRC mismatch. LEN runs from 1 (bare ACK) to MAX_RESPONSE_LEN, which is
    PWM_GROUP_GET's 4-byte frequency plus the ACK byte.
    """
    if len(raw) < response_frame_len(1):
        raise ProtocolError(f"response too short: {len(raw)} bytes")

    if raw[0] != SOF:
        raise ProtocolError(f"bad SOF: expected 0x{SOF:02X}, got 0x{raw[0]:02X}")

    length = raw[1]
    if not 1 <= length <= MAX_RESPONSE_LEN:
        raise ProtocolError(f"bad LEN: expected 1 to {MAX_RESPONSE_LEN}, got {length}")

    expected = response_frame_len(length)
    if len(raw) < expected:
        raise ProtocolError(f"truncated frame: need {expected} bytes, got {len(raw)}")

    body = raw[1 : 2 + length]
    received_crc = raw[2 + length] | (raw[3 + length] << 8)
    computed_crc = crc16_ccitt_false(body)
    if received_crc != computed_crc:
        raise ProtocolError(f"CRC mismatch: computed 0x{computed_crc:04X}, received 0x{received_crc:04X}")

    ack = bool(raw[2])
    data = bytes(raw[3 : 2 + length])
    return Response(ack=ack, data=data)
