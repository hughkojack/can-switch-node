#!/usr/bin/env python3
"""Verify a node_min_c3 firmware.bin is suitable for CAN OTA upload."""

from __future__ import annotations

import argparse
import sys
import zlib
from pathlib import Path

MAX_OTA_BYTES = 0x180000


def main() -> int:
    ap = argparse.ArgumentParser(description="Check firmware.bin for CAN OTA")
    ap.add_argument("image", type=Path, help="Path to firmware.bin")
    args = ap.parse_args()
    path = args.image
    if not path.is_file():
        print(f"not found: {path}", file=sys.stderr)
        return 1
    data = path.read_bytes()
    if len(data) < 24:
        print("too small", file=sys.stderr)
        return 1
    if data[0] != 0xE9:
        print(f"bad ESP magic 0x{data[0]:02X} (expected 0xE9)", file=sys.stderr)
        return 1
    if len(data) > MAX_OTA_BYTES:
        print(f"image {len(data)} exceeds OTA slot {MAX_OTA_BYTES}", file=sys.stderr)
        return 1
    crc = zlib.crc32(data) & 0xFFFFFFFF
    print(f"OK size={len(data)} crc32=0x{crc:08X}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
