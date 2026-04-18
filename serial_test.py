import serial
import time

PORT     = '/dev/ttyUSB0'
BAUDRATE = 115200
TIMEOUT  = 2

def main():
    with serial.Serial(PORT, BAUDRATE, timeout=TIMEOUT) as ser:
        time.sleep(0.1)

        # Read the startup "Hello, World!" message
        startup = ser.read_until(b'\n').decode('utf-8', errors='replace').strip()
        print(f"Startup message: {startup}")

        # Echo test
        test_messages = [
            b"ping\r\n",
            b"hello\r\n",
            b"uart test\r\n",
        ]

        for msg in test_messages:
            ser.write(msg)
            time.sleep(0.1)

            echo = ser.read(len(msg))
            expected = msg.decode('utf-8', errors='replace').strip()
            received = echo.decode('utf-8', errors='replace').strip()

            status = "PASS" if echo == msg else "FAIL"
            print(f"[{status}] sent: {expected!r:15} received: {received!r}")

if __name__ == "__main__":
    main()
