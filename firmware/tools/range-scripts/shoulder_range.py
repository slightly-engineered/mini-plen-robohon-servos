"""
RoBoHoN Servo Range Finder
Tests the range of each servo by sweeping from min to max.
Press ENTER to step through each servo and direction.
Ctrl-C to abort at any time.

Bus 2 (Head/Elbow): IDs 1-5
  1=neck turn, 2=head nod, 3=head tilt, 4=left elbow, 5=right elbow
"""

import serial, time

PORT  = "COM5"
BAUD  = 115200

HEADER  = [0x77, 0x80, 0x00, 0x03, 0x06, 0x00]
TRAILER = [0x6D, 0x00, 0x40]

def checksum(body):
    return (256 - (sum(body) & 0xFF)) & 0xFF

def build_frame(positions, flag=0x40):
    body = []
    for sid, pos in sorted(positions.items()):
        pos_u = int(pos) & 0xFFFF
        body += [pos_u & 0xFF, (pos_u >> 8) & 0xFF, flag, sid]
    body += TRAILER
    ck = checksum(body)
    return bytes(HEADER + body + [ck])

def send(ser, positions, n=8, interval=0.02):
    frame = build_frame(positions)
    for _ in range(n):
        ser.write(frame)
        time.sleep(interval)

# Home positions — all servos safe starting point
HOME = {1: 2839, 2: -2330, 3: -36, 4: 1565, 5: -1256}

SERVOS = {
    1: ("Neck turn",   2839),
    2: ("Head nod",   -2330),
    3: ("Head tilt",    -36),
    4: ("Left elbow",  1565),
    5: ("Right elbow",-1256),
}

# How far to step per keypress, and total sweep range to try
STEP      = 200   # counts per step
MAX_SWEEP = 4000  # max counts from home in each direction

with serial.Serial(PORT, BAUD, timeout=1) as ser:
    print(f"Servo range finder | {PORT} @ {BAUD}")
    print("Steps: press ENTER to move one step, 'b' to go back, 'h' to home, 'n' for next servo, 'q' to quit\n")

    # First send all home
    print("Homing all servos...")
    send(ser, HOME, n=10)
    time.sleep(1.0)

    for sid, (name, home_pos) in SERVOS.items():
        print(f"\n{'='*50}")
        print(f"Servo {sid}: {name}  (home={home_pos})")
        print(f"{'='*50}")
        print("ENTER=step forward, b=step back, h=home, n=next servo, q=quit")

        current = home_pos
        step_dir = +STEP

        while True:
            cmd = input(f"  pos={current}  > ").strip().lower()

            if cmd == 'q':
                # home everything before quitting
                send(ser, HOME, n=10)
                print("Homed. Bye.")
                exit()

            elif cmd == 'n':
                # home this servo before moving to next
                positions = dict(HOME)
                send(ser, positions, n=8)
                time.sleep(0.5)
                break

            elif cmd == 'h':
                current = home_pos
                positions = dict(HOME)
                send(ser, positions, n=8)
                print(f"  -> homed to {home_pos}")

            elif cmd == 'b':
                current -= STEP
                positions = dict(HOME)
                positions[sid] = current
                send(ser, positions)
                print(f"  -> {current}  (delta from home: {current - home_pos:+d})")

            elif cmd == '':
                current += STEP
                positions = dict(HOME)
                positions[sid] = current
                send(ser, positions)
                print(f"  -> {current}  (delta from home: {current - home_pos:+d})")

            elif cmd == 'r':
                # reverse direction
                STEP = -STEP
                print(f"  Direction reversed, step={STEP}")

            else:
                # allow typing a direct position value
                try:
                    current = int(cmd)
                    positions = dict(HOME)
                    positions[sid] = current
                    send(ser, positions)
                    print(f"  -> {current}  (delta from home: {current - home_pos:+d})")
                except ValueError:
                    print("  ENTER=fwd, b=back, h=home, n=next, q=quit, or type a number")

    print("\nAll servos done. Homing...")
    send(ser, HOME, n=10)
    print("Done.")