"""
High-level mcu-co API — the host-side mock of the C SDK the SBC will use.

One method per command in mcu-co_Protocol.md, each returning the decoded
Response. Arguments are **value-first** (`gpio_set(level, port, pin)`), matching
CLI token order and payload byte order, so a call, the command that produced it
and the bytes on the wire all read the same direction. This resolves open
decision #3 in the protocol doc; if the C SDK ever goes target-first, this
module has to move with it.

Arguments are validated here rather than deferred to the MCU: an out-of-range
pin caught locally is a Python error naming the argument, while the same value
sent to the MCU comes back as a bare NACK that says nothing about which field
was wrong.
"""

from .link import McuCoLink
from .protocol import Action, Dir, Edge, Level, Opcode, Polarity, Port, Response

PIN_MAX = 15
GROUP_MAX = 2  # three frequency groups: TIM2, TIM3, TIM4
DUTY_MAX = 1000  # tenths of a percent, so 1000 is 100.0%
FREQ_MIN = 1
FREQ_MAX = 1_000_000


def _port(value, name: str = "port") -> int:
    try:
        return Port(value)
    except ValueError:
        valid = ", ".join(p.name for p in Port)
        raise ValueError(f"{name}: {value!r} is not a valid port (expected one of {valid})") from None


def _pin(value, name: str = "pin") -> int:
    if not isinstance(value, int) or isinstance(value, bool):
        raise ValueError(f"{name}: {value!r} is not an integer pin number")
    if not 0 <= value <= PIN_MAX:
        raise ValueError(f"{name}: {value} out of range (expected 0-{PIN_MAX})")
    return value


def _group(value) -> int:
    if not isinstance(value, int) or isinstance(value, bool):
        raise ValueError(f"group: {value!r} is not an integer group number")
    if not 0 <= value <= GROUP_MAX:
        raise ValueError(f"group: {value} out of range (expected 0-{GROUP_MAX})")
    return value


def _duty(value) -> int:
    if not isinstance(value, int) or isinstance(value, bool):
        raise ValueError(f"duty: {value!r} is not an integer duty")
    if not 0 <= value <= DUTY_MAX:
        raise ValueError(f"duty: {value} out of range (expected 0-{DUTY_MAX}, tenths of a percent)")
    return value


def _frequency(value) -> int:
    if not isinstance(value, int) or isinstance(value, bool):
        raise ValueError(f"freq_hz: {value!r} is not an integer frequency")
    if not FREQ_MIN <= value <= FREQ_MAX:
        raise ValueError(f"freq_hz: {value} out of range (expected {FREQ_MIN}-{FREQ_MAX} Hz). "
                         "0 is not shorthand for teardown - use pwm_group_release()")
    return value


def _enum(enum_type, value, name: str) -> int:
    try:
        return enum_type(value)
    except ValueError:
        valid = ", ".join(f"{m.name.lower()}={int(m)}" for m in enum_type)
        raise ValueError(f"{name}: {value!r} is not a valid {enum_type.__name__} ({valid})") from None


class McuCo:
    """
    Command API over an McuCoLink.

        with McuCo.connect('/dev/ttyACM0') as mcu:
            mcu.gpio_cfg(Dir.OUTPUT, Port.A, 5)
            mcu.gpio_set(Level.HIGH, Port.A, 5)
    """

    def __init__(self, link: McuCoLink):
        self.link = link

    @classmethod
    def connect(cls, port: str, baudrate: int = 115200, timeout: float = 1.0) -> "McuCo":
        return cls(McuCoLink(port, baudrate, timeout).open())

    def close(self) -> None:
        self.link.close()

    def __enter__(self) -> "McuCo":
        return self

    def __exit__(self, *_exc_info) -> None:
        self.close()

    def _send(self, opcode: int, payload: bytes) -> Response:
        """Single exit point for every command — subclasses override this to intercept frames."""
        return self.link.send_command(opcode, payload)

    def gpio_cfg(self, direction, port, pin) -> Response:
        """gpio cfg input|output <port> <pin>"""
        payload = bytes([_enum(Dir, direction, "direction"), _port(port), _pin(pin)])
        return self._send(Opcode.GPIO_CFG, payload)

    def gpio_set(self, level, port, pin) -> Response:
        """gpio set high|low <port> <pin>"""
        payload = bytes([_enum(Level, level, "level"), _port(port), _pin(pin)])
        return self._send(Opcode.GPIO_WRITE, payload)

    def gpio_get(self, port, pin) -> Response:
        """gpio get <port> <pin> — response carries the pin level in .value"""
        payload = bytes([_port(port), _pin(pin)])
        return self._send(Opcode.GPIO_READ, payload)

    def gpio_irq_cfg(self, edge, port, pin) -> Response:
        """gpio irq cfg <edge>|off <port> <pin> — arm or disarm an EXTI trigger"""
        payload = bytes([_enum(Edge, edge, "edge"), _port(port), _pin(pin)])
        return self._send(Opcode.GPIO_IRQ_CFG, payload)

    def gpio_irq_bind(self, edge, in_port, in_pin, action, out_port, out_pin) -> Response:
        """gpio irq bind <edge> <in_port> <in_pin> <action> <out_port> <out_pin>"""
        edge = _enum(Edge, edge, "edge")
        if edge == Edge.OFF:
            raise ValueError("edge: bind never takes 'off' — use gpio_irq_cfg(Edge.OFF, ...) to disarm, "
                             "or gpio_irq_unbind() to drop the binding")

        payload = bytes([edge, _port(in_port, "in_port"), _pin(in_pin, "in_pin"),
                         _enum(Action, action, "action"), _port(out_port, "out_port"), _pin(out_pin, "out_pin")])
        return self._send(Opcode.GPIO_IRQ_BIND, payload)

    def gpio_irq_unbind(self, port, pin) -> Response:
        """gpio irq unbind <port> <pin> — drop the binding, leave the trigger armed"""
        payload = bytes([_port(port), _pin(pin)])
        return self._send(Opcode.GPIO_IRQ_UNBIND, payload)

    def pwm_group_cfg(self, freq_hz, group) -> Response:
        """pwm group cfg <freq_hz> <group> — bring a group up and start its counter

        NACKs with ERR_BUSY if the group is already configured; release it first.
        """
        payload = _frequency(freq_hz).to_bytes(4, "little") + bytes([_group(group)])
        return self._send(Opcode.PWM_GROUP_CFG, payload)

    def pwm_group_release(self, group) -> Response:
        """pwm group release <group> — stop the counter and free all four channels"""
        return self._send(Opcode.PWM_GROUP_RELEASE, bytes([_group(group)]))

    def pwm_group_get(self, group) -> Response:
        """pwm group get <group> — response carries the achieved frequency in .value

        The achieved frequency differs from the requested one wherever the
        prescaler and reload cannot divide the timer clock exactly.
        """
        return self._send(Opcode.PWM_GROUP_GET, bytes([_group(group)]))

    def pwm_channel_cfg(self, polarity, port, pin) -> Response:
        """pwm channel cfg high|low <port> <pin> — claim a pin, silent at duty 0"""
        payload = bytes([_enum(Polarity, polarity, "polarity"), _port(port), _pin(pin)])
        return self._send(Opcode.PWM_CFG, payload)

    def pwm_channel_set(self, duty, port, pin) -> Response:
        """pwm channel set <duty> <port> <pin> — duty in tenths of a percent, 0-1000"""
        payload = _duty(duty).to_bytes(2, "little") + bytes([_port(port), _pin(pin)])
        return self._send(Opcode.PWM_SET, payload)

    def pwm_channel_release(self, port, pin) -> Response:
        """pwm channel release <port> <pin> — free one pin, leaving the group running"""
        return self._send(Opcode.PWM_RELEASE, bytes([_port(port), _pin(pin)]))

    def pwm_channel_get(self, port, pin) -> Response:
        """pwm channel get <port> <pin> — response carries the duty in .value"""
        return self._send(Opcode.PWM_GET, bytes([_port(port), _pin(pin)]))
