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


MENU_OPTIONS = {
    '1': ("Valid frame", build_valid_frame),
    '2': ("Valid frame, wrong CRC", build_valid_frame_wrong_crc),
    '3': ("Invalid frame (LEN > MAX_PAYLOAD)", build_invalid_frame),
    '4': ("Invalid frame, wrong CRC", build_invalid_frame_wrong_crc),
}


def print_menu():
    print("\nSelect a frame to send:")
    for key, (name, _builder) in MENU_OPTIONS.items():
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
            if option is None:
                print("Not a valid choice, try again.")
                continue

            name, builder = option
            frame = builder()
            cmd_ser.write(frame)
            print(f"Sent [{name}]: {frame.hex(' ')}")


if __name__ == "__main__":
    main()
