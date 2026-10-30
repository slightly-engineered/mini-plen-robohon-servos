"""
RoBoHoN arm bus analyzer — multi‑channel version.
Now analyzes ALL logic channels in a PulseView .sr file.
"""

import sys, os, glob, struct, zipfile
import re

SAMPLE_RATE = 1_000_000
BAUD        = 115200

def read_sr(path):
    """
    Return dict: { channel_number : raw_bytes }
    Detects ALL logic/digital channel files inside the .sr zip.
    Works with every naming scheme used by PulseView/sigrok.
    """
    channels = {}

    # Regex matches:
    #   logic-1-1
    #   logic-0-1
    #   digital-2-1
    #   channel-3-1
    #   ch3-1
    #   etc.
    channel_regex = re.compile(r"(?:logic|digital|channel|ch)-?(\d+)-\d+")

    with zipfile.ZipFile(path) as z:
        for name in z.namelist():
            m = channel_regex.search(name)
            if not m:
                continue

            ch = int(m.group(1))
            data = z.read(name)

            if ch not in channels:
                channels[ch] = b""
            channels[ch] += data

    return channels


def decode_uart(raw, sample_rate=SAMPLE_RATE, baud=BAUD):
    spb = sample_rate / baud
    bits = [b & 1 for b in raw]
    total = len(bits)
    decoded = []
    i = 0
    while i < total - int(spb * 10):
        if bits[i] == 1 and i + 1 < total and bits[i+1] == 0:
            start = i + int(spb * 0.5)
            byte_val = 0
            valid = True
            for bit in range(8):
                sp = int(start + spb * (bit + 1))
                if sp >= total:
                    valid = False
                    break
                byte_val |= bits[sp] << bit
            stop = int(start + spb * 9)
            if valid and stop < total and bits[stop] == 1:
                decoded.append((i, byte_val))
                i = stop + 1
                continue
        i += 1
    return decoded


def split_packets(decoded, sample_rate=SAMPLE_RATE, gap_ms=1.5):
    if not decoded:
        return []
    gap_samples = int(sample_rate * gap_ms / 1000)
    packets = []
    current = [decoded[0]]
    for i in range(1, len(decoded)):
        if decoded[i][0] - decoded[i-1][0] > gap_samples:
            packets.append(current)
            current = []
        current.append(decoded[i])
    if current:
        packets.append(current)
    return packets


def classify(pkt_bytes):
    b = pkt_bytes
    if len(b) >= 7 and b[0] == 0x77 and b[1] == 0x80:
        return "CMD_BROADCAST"
    if len(b) >= 2 and b[0] == 0x00 and b[1] == 0x80:
        return "FEEDBACK_ALL"
    if len(b) >= 2 and b[0] == 0x80:
        return "FEEDBACK_ALL (no lead)"
    if len(b) >= 3 and b[0] == 0x00 and b[1] == 0x00 and b[2] == 0x52:
        return "POLL_CMD"
    if len(b) >= 1 and b[0] in [0x00,0x01,0x02,0x03,0x04] and len(b) < 15:
        return f"POLL_RESP (id={b[0]:02X})"
    if len(b) >= 3 and b[0] == 0x00 and b[1] == 0x00 and b[2] == 0x00:
        if len(b) >= 4 and b[3] == 0x77:
            return "SUB_FRAME"
    return "UNKNOWN"


def parse_feedback(b):
    servos = []
    i = 0
    if b[0] == 0x00:
        i = 1
    while i + 4 < len(b):
        if b[i] == 0x80:
            pos = struct.unpack_from('<h', bytes([b[i+1], b[i+2]]))[0]
            param = b[i+3]
            sid = b[i+4]
            servos.append((sid, pos, param))
            i += 5
        else:
            i += 1
    return servos


def main():
    if len(sys.argv) < 2:
        print("Usage: python analyze_arm_bus.py capture.sr")
        sys.exit(1)

    path = sys.argv[1]
    print(f"Reading {path}...")

    channels = read_sr(path)

    print(f"Found {len(channels)} channels with data\n")

    for ch, raw in sorted(channels.items()):
        print(f"=== CHANNEL {ch} ===")
        print(f"  {len(raw):,} samples ({len(raw)/SAMPLE_RATE:.2f}s)")

        decoded = decode_uart(raw)
        print(f"  {len(decoded)} bytes decoded")

        packets = split_packets(decoded)
        print(f"  {len(packets)} packets found\n")

        print(f"{'PKT':>4}  {'TIME_MS':>8}  {'TYPE':<22}  {'LEN':>4}  BYTES")
        print("-" * 120)

        for i, pkt in enumerate(packets):
            t_ms = pkt[0][0] / SAMPLE_RATE * 1000
            b = [x[1] for x in pkt]
            ptype = classify(b)
            hex_str = " ".join(f"{x:02X}" for x in b)

            print(f"{i+1:>4}  {t_ms:>8.1f}ms  {ptype:<22}  {len(b):>4}  {hex_str}")

            if "FEEDBACK" in ptype:
                servos = parse_feedback(b)
                for sid, pos, param in servos:
                    print(f"                                              "
                          f"  servo {sid:02X}: pos={pos:6d} (0x{pos&0xFFFF:04X})  param=0x{param:02X}")

        print("\n")

    print("Done.")


if __name__ == "__main__":
    main()
