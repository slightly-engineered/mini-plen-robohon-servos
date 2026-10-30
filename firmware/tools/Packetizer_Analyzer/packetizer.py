import os

INPUT_FILE  = "packets.txt"            # path to the decoded capture export
OUTPUT_FILE = "processed_packets.txt"

PACKET_GAP = 0.01  


def load_file():
    data = []
    with open(INPUT_FILE, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue

            # Example line:
            # 1485365-1485435 UART: RX data: 00

            try:
                # Split at first space to isolate timestamps
                ts_part, rest = line.split(" ", 1)

                # Split timestamps at hyphen
                start_ts_str, end_ts_str = ts_part.split("-")

                start_ts = float(start_ts_str) / 1_000_000.0  # convert µs → seconds

                # Extract the byte (last token)
                byte_hex = line.split()[-1].upper()

                # Validate hex
                int(byte_hex, 16)

                data.append((start_ts, byte_hex))

            except Exception:
                continue

    return data


def packetize(data):
    packets = []
    current = []
    last_time = None

    for timestamp, byte_hex in data:
        if last_time is None:
            current = [byte_hex]
        else:
            dt = timestamp - last_time

            if dt > PACKET_GAP:
                packets.append(current)
                current = [byte_hex]
            else:
                current.append(byte_hex)

        last_time = timestamp

    if current:
        packets.append(current)

    return packets


def save_packets(packets):
    with open(OUTPUT_FILE, "w") as f:
        for i, pkt in enumerate(packets, start=1):
            hex_string = " ".join(pkt)
            f.write(f"Packet {i:03d}: {hex_string}\n")


def main():
    print("Loading:", INPUT_FILE)

    if not os.path.exists(INPUT_FILE):
        print("ERROR: Input file not found.")
        return

    data = load_file()
    print(f"Loaded {len(data)} bytes")

    if not data:
        print("ERROR: No valid data found in file.")
        return

    packets = packetize(data)
    print(f"Detected {len(packets)} packets")

    save_packets(packets)
    print("Saved:", OUTPUT_FILE)


if __name__ == "__main__":
    main()
