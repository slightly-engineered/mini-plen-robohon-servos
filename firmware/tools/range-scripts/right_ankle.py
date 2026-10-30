import serial
import time

PORT = 'COM5'
BAUD = 115200

def checksum(body):
    return (256 - (sum(body) & 0xFF)) & 0xFF

def send_frame(ser, block3_bytes):
    frame = [
        0x77, 0x80, 0x00, 0x03, 0x06, 0x00,
        0x17, 0x0B, 0x40, 0x01,
        0xE6, 0xF6, 0x40, 0x02,
        block3_bytes[0], block3_bytes[1], 0x40, block3_bytes[2],
        0x1D, 0x06, 0x40, 0x04,
        0x18, 0xFB, 0x40, 0x05,
        0x6D, 0x00, 0x40,
        0x00
    ]
    frame[-1] = checksum(frame[6:-1])
    ser.write(bytes(frame))
    ser.flush()

if __name__ == "__main__":
    ser = serial.Serial(PORT, BAUD, timeout=1)
    time.sleep(0.3)

    input("Enter = RIGHT ANKLE MOVE (trigger packet)")
    send_frame(ser, [0x00, 0x00, 0x05])   # 00 00 40 05

    time.sleep(1)

    input("Enter = RIGHT ANKLE CENTER (home packet)")
    send_frame(ser, [0xDC, 0xFF, 0x03])   # DC FF 40 03

    ser.close()
