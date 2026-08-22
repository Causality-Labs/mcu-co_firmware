"""
Serial transport for the mcu-co command UART.

Owns the port and the strict master-slave rule from mcu-co_Protocol.md: one
command in flight, every command answered before the next is sent. Framing and
CRC live in protocol.py; this module only moves bytes and decides when a reply
is complete.

Not every command gets a reply. The firmware answers dispatch-level failures
with a bare NACK, but a CRC mismatch or a framing error makes it discard the
frame silently (see the FRAME_ERROR / crc16_compare paths in src/main.c), so a
deliberately corrupt frame times out rather than returning NACK.
"""

import time

from .protocol import SOF, Response, build_command, parse_response

DEFAULT_BAUDRATE = 115200
DEFAULT_TIMEOUT_S = 1.0


class LinkError(Exception):
    """The link could not complete a command/response exchange."""


class LinkTimeout(LinkError):
    """No complete response arrived before the timeout expired."""


class McuCoLink:
    """
    Command/response channel to the MCU.

        with McuCoLink('/dev/ttyACM0') as link:
            response = link.send_command(Opcode.GPIO_READ, bytes([Port.C, 13]))
    """

    def __init__(self, port: str, baudrate: int = DEFAULT_BAUDRATE,
                 timeout: float = DEFAULT_TIMEOUT_S):
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self._stream = None

    @classmethod
    def from_stream(cls, stream, timeout: float = DEFAULT_TIMEOUT_S) -> "McuCoLink":
        """Wrap an already-open serial-like object. Used by the tests to run without hardware."""
        link = cls(port="<stream>", timeout=timeout)
        link._stream = stream
        return link

    def open(self) -> "McuCoLink":
        if self._stream is None:
            import serial  # deferred so importing this module needs no pyserial

            self._stream = serial.Serial(self.port, self.baudrate, timeout=self.timeout)
            time.sleep(0.1)  # USB-serial adapters drop bytes written immediately after open
        return self

    def close(self) -> None:
        if self._stream is not None:
            self._stream.close()
            self._stream = None

    def __enter__(self) -> "McuCoLink":
        return self.open()

    def __exit__(self, *_exc_info) -> None:
        self.close()

    def send(self, frame: bytes) -> Response:
        """Write one framed command and return the decoded response."""
        stream = self._require_stream()

        if hasattr(stream, "reset_input_buffer"):
            stream.reset_input_buffer()  # drop anything left over from an abandoned exchange

        stream.write(frame)
        return self.read_response()

    def send_command(self, opcode: int, payload: bytes = b"") -> Response:
        return self.send(build_command(opcode, payload))

    def read_response(self) -> Response:
        """
        Read one response frame, discarding bytes until SOF.

        The length is not known until LEN has been read, so the frame is pulled
        in three steps: resync to SOF, read LEN, then read LEN + 2 more bytes.
        """
        stream = self._require_stream()
        deadline = time.monotonic() + self.timeout

        discarded = 0
        while True:
            byte = stream.read(1)
            if not byte:
                raise LinkTimeout(f"no response within {self.timeout:g}s")
            if byte[0] == SOF:
                break
            discarded += 1
            if time.monotonic() > deadline:
                raise LinkTimeout(f"no SOF within {self.timeout:g}s ({discarded} bytes discarded)")

        length_byte = stream.read(1)
        if not length_byte:
            raise LinkTimeout("response ended after SOF, before LEN")

        remaining = length_byte[0] + 2  # payload + CRC_L + CRC_H
        rest = stream.read(remaining)
        if len(rest) < remaining:
            raise LinkTimeout(f"truncated response: expected {remaining} bytes after LEN, got {len(rest)}")

        return parse_response(bytes([SOF]) + length_byte + rest)

    def _require_stream(self):
        if self._stream is None:
            raise LinkError("link is not open; call open() or use it as a context manager")
        return self._stream
