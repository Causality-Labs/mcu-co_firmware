"""
Interactively sends one mcu-co protocol frame at a time over the command
UART (USART2), so you can pick a frame type and watch how the MCU reacts.

Open the log UART (USART1) yourself in another terminal / serial monitor
to see how the MCU responded to each frame.

Wire format (see mcu-co_Protocol.md):

    SOF . OPCODE . LEN . PAYLOAD . CRC_L . CRC_H

CRC16 is CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflect, no xorout),
computed over OPCODE, LEN and PAYLOAD only (not SOF, not the CRC bytes).
"""

import serial
import time

# --- Adjust this to match your USB-serial adapter wired to USART2 ---------
CMD_PORT = '/dev/ttyACM0'
BAUDRATE = 115200

SOF = 0xA5
MAX_PAYLOAD = 32  # from frame_parser.h

# Opcode + payload used for every frame type below: "gpio cfg output PA5".
OPCODE  = 0x30  # GPIO_CFG
PAYLOAD = bytes([0x01, 0x00, 0x05])  # [DIR=output, PORT=A, PIN=5]

GPIO_WRITE_OPCODE    = 0x31  # GPIO_WRITE
GPIO_READ_OPCODE     = 0x32  # GPIO_READ
GPIO_IRQ_BIND_OPCODE = 0x33  # GPIO_IRQ_BIND
GPIO_IRQ_CFG_OPCODE  = 0x34  # GPIO_IRQ_CFG

EDGE_OFF     = 0x00
EDGE_RISING  = 0x01
EDGE_FALLING = 0x02
EDGE_BOTH    = 0x03

ACTION_LOW    = 0x00
ACTION_HIGH   = 0x01
ACTION_TOGGLE = 0x02


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


def build_valid_frame() -> bytes:
    """Well-formed frame with a correct CRC. The MCU should accept it."""
    body = bytes([OPCODE, len(PAYLOAD)]) + PAYLOAD
    crc = crc16_ccitt_false(body)
    return bytes([SOF]) + body + bytes([crc & 0xFF, (crc >> 8) & 0xFF])


def build_valid_frame_wrong_crc() -> bytes:
    """Same well-formed frame, but with the CRC bytes deliberately corrupted."""
    frame = bytearray(build_valid_frame())
    frame[-1] ^= 0x01  # flip a bit in CRC_HIGH
    return bytes(frame)


def build_invalid_frame() -> bytes:
    """
    LEN set beyond MAX_PAYLOAD (32). frame_parser_feed() rejects this as soon
    as it reads the LEN byte (FRAME_ERROR) and resyncs on the next SOF - it
    never even looks at the payload or CRC bytes that follow.
    """
    body = bytes([OPCODE, 0xFF]) + PAYLOAD
    crc = crc16_ccitt_false(body)
    return bytes([SOF]) + body + bytes([crc & 0xFF, (crc >> 8) & 0xFF])


def build_invalid_frame_wrong_crc() -> bytes:
    """Same invalid (oversized-LEN) frame, with the trailing CRC bytes also corrupted."""
    frame = bytearray(build_invalid_frame())
    frame[-1] ^= 0x01
    return bytes(frame)


def build_gpio_write_pa5(level: int) -> bytes:
    """gpio set high|low A 5 - drive PA5 (Nucleo LED) to LEVEL (0=low, 1=high)."""
    payload = bytes([level, 0x00, 0x05])  # [LEVEL, PORT=A, PIN=5]
    body = bytes([GPIO_WRITE_OPCODE, len(payload)]) + payload
    crc = crc16_ccitt_false(body)
    return bytes([SOF]) + body + bytes([crc & 0xFF, (crc >> 8) & 0xFF])


def build_gpio_cfg_input_pc13() -> bytes:
    """gpio cfg input C 13 - configure PC13 (Nucleo B1 user button) as an input."""
    payload = bytes([0x00, 0x02, 0x0D])  # [DIR=input, PORT=C, PIN=13]
    body = bytes([OPCODE, len(payload)]) + payload
    crc = crc16_ccitt_false(body)
    return bytes([SOF]) + body + bytes([crc & 0xFF, (crc >> 8) & 0xFF])


def build_gpio_read_pc13() -> bytes:
    """gpio get C 13 - read the current level of PC13 (Nucleo B1 user button)."""
    payload = bytes([0x02, 0x0D])  # [PORT=C, PIN=13]
    body = bytes([GPIO_READ_OPCODE, len(payload)]) + payload
    crc = crc16_ccitt_false(body)
    return bytes([SOF]) + body + bytes([crc & 0xFF, (crc >> 8) & 0xFF])


def build_gpio_irq_cfg(edge: int, port: int, pin: int) -> bytes:
    """gpio irq cfg <edge> <port> <pin> - arm (or disarm, edge=EDGE_OFF) an EXTI trigger."""
    payload = bytes([edge, port, pin])
    body = bytes([GPIO_IRQ_CFG_OPCODE, len(payload)]) + payload
    crc = crc16_ccitt_false(body)
    return bytes([SOF]) + body + bytes([crc & 0xFF, (crc >> 8) & 0xFF])


def build_gpio_irq_bind(edge_select: int, in_port: int, in_pin: int,
                         action: int, out_port: int, out_pin: int) -> bytes:
    """gpio irq bind <edge> <in_port> <in_pin> <action> <out_port> <out_pin>."""
    payload = bytes([edge_select, in_port, in_pin, action, out_port, out_pin])
    body = bytes([GPIO_IRQ_BIND_OPCODE, len(payload)]) + payload
    crc = crc16_ccitt_false(body)
    return bytes([SOF]) + body + bytes([crc & 0xFF, (crc >> 8) & 0xFF])


def run_irq_bind_demo(cmd_ser, step_delay_s: float = 0.3):
    """
    Wires the Nucleo B1 button (PC13) to toggle the LED (PA5) entirely on the
    MCU: cfg PC13 as input, PA5 as output, set PA5 low as a known starting
    point, arm PC13 for both edges, then bind both edges to toggle PA5.

    After the last ACK, the host is out of the loop - pressing the button
    should flip the LED with no further frames sent. Watch the LED, or the
    log UART, to confirm.
    """
    steps = [
        ("gpio cfg input C 13", build_gpio_cfg_input_pc13()),
        ("gpio cfg output A 5", build_valid_frame()),
        ("gpio set low A 5", build_gpio_write_pa5(0)),
        ("gpio irq cfg both C 13", build_gpio_irq_cfg(EDGE_BOTH, 0x02, 0x0D)),
        ("gpio irq bind both C 13 toggle A 5",
         build_gpio_irq_bind(EDGE_BOTH, 0x02, 0x0D, ACTION_TOGGLE, 0x00, 0x05)),
    ]

    for name, frame in steps:
        cmd_ser.write(frame)
        print(f"Sent [{name}]: {frame.hex(' ')}")
        time.sleep(step_delay_s)


def run_blink_led_test(cmd_ser, toggle_count: int = 20, period_s: float = 0.5):
    """
    Configure PA5 (the Nucleo board's LED pin) as an output, then drive it
    high/low alternately every `period_s` seconds, `toggle_count` times.
    Watch the LED, or the log UART (USART1), to confirm each toggle. Ctrl+C
    stops early.
    """
    cfg_frame = build_valid_frame()  # gpio cfg output PA5
    cmd_ser.write(cfg_frame)
    print(f"Sent [gpio cfg output PA5]: {cfg_frame.hex(' ')}")
    time.sleep(period_s)

    level = 1
    try:
        for _ in range(toggle_count):
            frame = build_gpio_write_pa5(level)
            cmd_ser.write(frame)
            print(f"Sent [gpio set {'high' if level else 'low'} PA5]: {frame.hex(' ')}")
            level ^= 1
            time.sleep(period_s)
    except KeyboardInterrupt:
        print("\nBlink test stopped early.")


MENU_OPTIONS = {
    '1': ("Valid frame", build_valid_frame),
    '2': ("Valid frame, wrong CRC", build_valid_frame_wrong_crc),
    '3': ("Invalid frame (LEN > MAX_PAYLOAD)", build_invalid_frame),
    '4': ("Invalid frame, wrong CRC", build_invalid_frame_wrong_crc),
    '6': ("Configure PC13 as input (Nucleo B1 button)", build_gpio_cfg_input_pc13),
    '7': ("Read PC13 state (Nucleo B1 button)", build_gpio_read_pc13),
}

ACTIONS = {
    '5': ("Blink LED (PA5): cfg output, then toggle high/low", run_blink_led_test),
    '8': ("Bind button (PC13) both-edge -> toggle LED (PA5)", run_irq_bind_demo),
}


def print_menu():
    print("\nSelect a frame to send:")
    for key, (name, _builder) in MENU_OPTIONS.items():
        print(f"  {key}) {name}")
    for key, (name, _action) in ACTIONS.items():
        print(f"  {key}) {name}")
    print("  q) Quit")


def main():
    with serial.Serial(CMD_PORT, BAUDRATE, timeout=1.0) as cmd_ser:
        time.sleep(0.1)

        while True:
            print_menu()
            choice = input("> ").strip().lower()

            if choice == 'q':
                break

            option = MENU_OPTIONS.get(choice)
            if option is not None:
                name, builder = option
                frame = builder()
                cmd_ser.write(frame)
                print(f"Sent [{name}]: {frame.hex(' ')}")
                continue

            action = ACTIONS.get(choice)
            if action is not None:
                _name, action_fn = action
                action_fn(cmd_ser)
                continue

            print("Not a valid choice, try again.")


if __name__ == "__main__":
    main()
