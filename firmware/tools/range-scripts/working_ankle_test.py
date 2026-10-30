"""
Right Ankle (trailer channel, bytes 26-28) range-of-motion test - Leg Bus
Sends direct frames over USB-TTL, bypassing Android entirely.

Unlike SID1-5, this channel has no explicit ID byte - it's positional
(always the last 3 bytes before checksum). All 5 documented slots
(including L. Ankle) are held at home. Only the trailer value is varied.

Since we don't know if this channel responds to 0x40 (standard position
code) or 0x59 (the ankle-class code seen in the real capture), this
script tests 0x40 first, then lets you try 0x59 if nothing moves.

Range is unknown - steps are small and conservative (100 counts) and
start right against home (109) in both directions.

Usage:
    python right_ankle_trailer_sweep.py
"""

import serial
import time

PORT = 'COM5'
BAUD = 115200

# Confirmed LEG bus home values (from LEG_HOME_FRAME, checksum-verified)
HOME_ID1 = (0x9F, 0x05)   # L. Hip     = 1439
HOME_ID2 = (0xE6, 0xF6)   # L. Foot    = -2330
HOME_ID3 = (0xDC, 0xFF)   # L. Ankle   = -36
HOME_ID4 = (0x1D, 0x06)   # R. Hip     = 1565
HOME_ID5 = (0x18, 0xFB)   # R. Foot    = -1256
HOME_TRAILER = 109         # confirmed trailer home value (0x6D, 0x00)

STEP = 100           # counts per increment - conservative, unknown range
STEP_DELAY = 0.6

# How far to probe from home in each direction before giving up.
# Small on purpose - this channel has never been intentionally moved before.
PROBE_LIMIT = 3000


def checksum(body):
    return (256 - (sum(body) & 0xFF)) & 0xFF


def to_le_bytes(val):
    val &= 0xFFFF
    return val & 0xFF, (val >> 8) & 0xFF


def build_frame(trailer_val, trailer_cmd):
    t_lo, t_hi = to_le_bytes(trailer_val)
    frame = [
        0x77, 0x80, 0x00, 0x03, 0x06, 0x00,
        HOME_ID1[0], HOME_ID1[1], 0x40, 0x01,
        HOME_ID2[0], HOME_ID2[1], 0x40, 0x02,
        HOME_ID3[0], HOME_ID3[1], 0x40, 0x03,
        HOME_ID4[0], HOME_ID4[1], 0x40, 0x04,
        HOME_ID5[0], HOME_ID5[1], 0x40, 0x05,
        t_lo, t_hi, trailer_cmd,
        0x00,
    ]
    frame[-1] = checksum(frame[6:-1])
    return bytes(frame)


def send(ser, trailer_val, trailer_cmd, label=""):
    frame = build_frame(trailer_val, trailer_cmd)
    ser.write(frame)
    ser.flush()
    print(f"  -> trailer = {trailer_val:6d}  cmd=0x{trailer_cmd:02X}  {label}  "
          f"[{frame.hex(' ')}]")


def go_home(ser, cmd=0x40):
    print("Returning trailer channel to home (109)...")
    send(ser, HOME_TRAILER, cmd, "HOME")
    time.sleep(0.8)


def sweep(ser, cmd):
    print(f"\n--- Sweep using cmd=0x{cmd:02X} ---")

    input(f"Enter = begin NEGATIVE direction sweep (cmd=0x{cmd:02X}). "
          f"Watch/feel closely - this channel has never moved before. "
          f"Ctrl+C any time to abort and re-home.")
    val = HOME_TRAILER
    moved = False
    while val > HOME_TRAILER - PROBE_LIMIT:
        val = max(val - STEP, HOME_TRAILER - PROBE_LIMIT)
        send(ser, val, cmd, "sweep -")
        resp = input("    [Enter]=continue  [m]=it moved, keep going  "
                      "[s]=stop here  [h]=go home now: ").strip().lower()
        if resp == 'm':
            moved = True
        if resp == 's':
            print(f"Stopped at {val}.")
            break
        if resp == 'h':
            go_home(ser, cmd)
            val = HOME_TRAILER

    input(f"\nEnter = return to home before POSITIVE direction sweep (cmd=0x{cmd:02X})")
    go_home(ser, cmd)

    input(f"Enter = begin POSITIVE direction sweep (cmd=0x{cmd:02X}). "
          f"Ctrl+C any time to abort and re-home.")
    val = HOME_TRAILER
    while val < HOME_TRAILER + PROBE_LIMIT:
        val = min(val + STEP, HOME_TRAILER + PROBE_LIMIT)
        send(ser, val, cmd, "sweep +")
        resp = input("    [Enter]=continue  [m]=it moved, keep going  "
                      "[s]=stop here  [h]=go home now: ").strip().lower()
        if resp == 'm':
            moved = True
        if resp == 's':
            print(f"Stopped at {val}.")
            break
        if resp == 'h':
            go_home(ser, cmd)
            val = HOME_TRAILER

    input(f"\nEnter = return to home (cmd=0x{cmd:02X})")
    go_home(ser, cmd)

    return moved


def main():
    ser = serial.Serial(PORT, BAUD, timeout=1)
    time.sleep(0.3)

    try:
        input("Enter = send HOME baseline frame (all 6 channels at documented home)")
        send(ser, HOME_TRAILER, 0x40, "HOME/baseline")
        time.sleep(1.0)

        moved_40 = sweep(ser, 0x40)

        if not moved_40:
            print("\nNo movement reported under cmd=0x40.")
            resp = input("Try cmd=0x59 (ankle-class code from real capture) instead? [y/n]: ").strip().lower()
            if resp == 'y':
                sweep(ser, 0x59)
        else:
            print("\nMovement reported under cmd=0x40 - channel responds to standard position code.")

    except KeyboardInterrupt:
        print("\nAborted - returning trailer channel to home before exit.")
        go_home(ser)

    finally:
        ser.close()
        print("Serial closed.")


if __name__ == "__main__":
    main()
