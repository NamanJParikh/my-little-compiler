"""Display an x86 emitter state snapshot in a human-readable format."""
import argparse
from pathlib import Path
import struct
import sys


MAGIC = b"SASMST1\0"
HEADER_SIZE = 88
REGISTER_COUNT = 8
ROW_SIZE = 16


def read_snapshot(path):
    data = path.read_bytes()
    if len(data) < HEADER_SIZE:
        raise ValueError(f"snapshot is too short: expected at least {HEADER_SIZE} bytes")
    if data[:len(MAGIC)] != MAGIC:
        raise ValueError("unrecognized snapshot magic or version")

    storage_size = struct.unpack_from("<Q", data, 8)[0]
    expected_size = HEADER_SIZE + storage_size
    if len(data) != expected_size:
        raise ValueError(
            f"snapshot size mismatch: header says {storage_size} storage bytes, "
            f"but file contains {len(data) - HEADER_SIZE}"
        )

    registers = struct.unpack_from("<8Q", data, 16)
    condition = struct.unpack_from("<Q", data, 80)[0]
    if condition > 3:
        raise ValueError(f"invalid condition register value: {condition}")
    return registers, condition, data[HEADER_SIZE:]


def display_storage(storage):
    if not storage:
        print("  (empty)")
        return

    for offset in range(0, len(storage), ROW_SIZE):
        row = storage[offset:offset + ROW_SIZE]
        hex_bytes = " ".join(f"{byte:02x}" for byte in row)
        ascii_bytes = "".join(chr(byte) if 0x20 <= byte < 0x7f else "." for byte in row)
        print(f"  {offset:08x}  {hex_bytes:<47}  |{ascii_bytes}|")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("snapshot", nargs="?", type=Path, default=Path("state.bin"),
                        help="snapshot file (default: ./state.bin)")
    args = parser.parse_args()

    try:
        registers, condition, storage = read_snapshot(args.snapshot)
    except (OSError, ValueError) as error:
        parser.error(str(error))

    print(f"Snapshot: {args.snapshot}")
    print(f"Storage size: {len(storage)} bytes")
    print("Registers:")
    for index, value in enumerate(registers):
        print(f"  R{index}: {value} (0x{value:016x})")
    print(f"  Rc: {condition}")
    print("Storage (offset, hex, ASCII):")
    display_storage(storage)
    return 0


if __name__ == "__main__":
    sys.exit(main())
