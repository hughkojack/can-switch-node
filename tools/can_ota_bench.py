#!/usr/bin/env python3
"""
Bench-test CAN OTA against a mechanical node (node_min_c3) via python-can.

Usage:
  pip install python-can
  python tools/can_ota_bench.py --interface socketcan --channel can0 --node-id 4 firmware.bin
  python tools/can_ota_bench.py --interface slcan --channel COM9 --node-id 4 firmware.bin --abort
  python tools/can_ota_bench.py ... --bad-crc   # expect node to reject / stay on old image

Requires an OTA-capable node build (partitions_ota.csv, OTA_CAPABLE=1) flashed once via USB.
"""

from __future__ import annotations

import argparse
import struct
import sys
import time
import zlib

try:
    import can
except ImportError:
    print("Install python-can: pip install python-can", file=sys.stderr)
    sys.exit(1)

FW_CMD_BEGIN = 0x01
FW_CMD_DATA = 0x02
FW_CMD_END = 0x03
FW_CMD_ABORT = 0x04
FW_BEGIN_PART0 = 0
FW_BEGIN_PART1 = 1
FW_DATA_BYTES = 5

FW_STATUS_READY = 1
FW_STATUS_ACK = 2
FW_STATUS_NACK = 3
FW_STATUS_COMPLETE = 4
FW_STATUS_ERROR = 5

CAN_MSG_FIRMWARE = 0x5
CAN_MSG_FIRMWARE_STATUS = 0x9
ACK_WINDOW = 16


def can_id(msg_type: int, node_id: int) -> int:
    return ((msg_type & 0xF) << 7) | (node_id & 0x7F)


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def send_begin(bus: can.Bus, node_id: int, size: int, crc: int, version: int) -> None:
    p0 = bytes([
        FW_CMD_BEGIN, FW_BEGIN_PART0,
        size & 0xFF, (size >> 8) & 0xFF, (size >> 16) & 0xFF, (size >> 24) & 0xFF,
        crc & 0xFF,
    ])
    p1 = bytes([
        FW_CMD_BEGIN, FW_BEGIN_PART1,
        crc & 0xFF, (crc >> 8) & 0xFF, (crc >> 16) & 0xFF, (crc >> 24) & 0xFF,
        version & 0xFF, (version >> 8) & 0xFF,
    ])
    bus.send(can.Message(arbitration_id=can_id(CAN_MSG_FIRMWARE, node_id), data=p0, is_extended_id=False))
    bus.send(can.Message(arbitration_id=can_id(CAN_MSG_FIRMWARE, node_id), data=p1, is_extended_id=False))


def send_data(bus: can.Bus, node_id: int, seq: int, chunk: bytes) -> None:
    payload = bytes([FW_CMD_DATA, seq & 0xFF, (seq >> 8) & 0xFF]) + chunk
    bus.send(can.Message(arbitration_id=can_id(CAN_MSG_FIRMWARE, node_id), data=payload, is_extended_id=False))


def send_end(bus: can.Bus, node_id: int, crc: int) -> None:
    payload = bytes([FW_CMD_END, crc & 0xFF, (crc >> 8) & 0xFF, (crc >> 16) & 0xFF, (crc >> 24) & 0xFF])
    bus.send(can.Message(arbitration_id=can_id(CAN_MSG_FIRMWARE, node_id), data=payload, is_extended_id=False))


def send_abort(bus: can.Bus, node_id: int, reason: int = 1) -> None:
    bus.send(can.Message(
        arbitration_id=can_id(CAN_MSG_FIRMWARE, node_id),
        data=bytes([FW_CMD_ABORT, reason]),
        is_extended_id=False,
    ))


def wait_status(bus: can.Bus, node_id: int, expect: int, timeout: float = 5.0) -> tuple[int, int]:
    deadline = time.time() + timeout
    while time.time() < deadline:
        msg = bus.recv(timeout=0.2)
        if msg is None:
            continue
        if msg.arbitration_id != can_id(CAN_MSG_FIRMWARE_STATUS, node_id):
            continue
        if len(msg.data) < 3:
            continue
        status = msg.data[0]
        seq = msg.data[1] | (msg.data[2] << 8)
        if status == expect:
            return status, seq
        if status in (FW_STATUS_ERROR, FW_STATUS_NACK):
            raise RuntimeError(f"node status={status} seq={seq}")
    raise TimeoutError(f"timeout waiting for status {expect}")


def run_ota(bus: can.Bus, node_id: int, image: bytes, version: int, do_abort: bool = False) -> None:
    c = crc32(image)
    print(f"Image size={len(image)} crc=0x{c:08X} version=0x{version:04X}")
    send_begin(bus, node_id, len(image), c, version)
    wait_status(bus, node_id, FW_STATUS_READY, timeout=5.0)
    print("Node READY")

    if do_abort:
        send_abort(bus, node_id)
        print("Sent FW_ABORT (expect node to return to idle)")
        return

    seq = 0
    offset = 0
    while offset < len(image):
        n = min(FW_DATA_BYTES, len(image) - offset)
        send_data(bus, node_id, seq, image[offset:offset + n])
        offset += n
        seq += 1
        if seq % ACK_WINDOW == 0 or offset >= len(image):
            _, ack_seq = wait_status(bus, node_id, FW_STATUS_ACK, timeout=2.0)
            print(f"  ACK seq={ack_seq} ({100 * offset // len(image)}%)")

    send_end(bus, node_id, c)
    wait_status(bus, node_id, FW_STATUS_COMPLETE, timeout=10.0)
    print("OTA COMPLETE (reboot node via hub CMD_REBOOT or power cycle to run new image)")


def main() -> int:
    ap = argparse.ArgumentParser(description="CAN OTA bench script for mechanical nodes")
    ap.add_argument("firmware", help="Path to .bin firmware image")
    ap.add_argument("--node-id", type=int, required=True, help="Target node ID 1..126")
    ap.add_argument("--interface", default="socketcan", help="python-can interface (socketcan, slcan, ...)")
    ap.add_argument("--channel", default="can0", help="CAN channel / COM port")
    ap.add_argument("--bitrate", type=int, default=500000)
    ap.add_argument("--fw-version", type=lambda x: int(x, 0), default=0x0100)
    ap.add_argument("--abort", action="store_true", help="Abort after READY (timeout test)")
    ap.add_argument("--bad-crc", action="store_true", help="Send wrong CRC on END (rollback test)")
    args = ap.parse_args()

    if args.node_id < 1 or args.node_id > 126:
        print("node-id must be 1..126", file=sys.stderr)
        return 1

    with open(args.firmware, "rb") as f:
        image = f.read()
    if not image or image[0] != 0xE9:
        print("Invalid ESP32 image (expected magic 0xE9)", file=sys.stderr)
        return 1

    bus = can.interface.Bus(channel=args.channel, interface=args.interface, bitrate=args.bitrate)
    try:
        if args.bad_crc:
            c = crc32(image)
            send_begin(bus, args.node_id, len(image), c, args.fw_version)
            wait_status(bus, args.node_id, FW_STATUS_READY)
            seq = 0
            offset = 0
            while offset < len(image):
                n = min(FW_DATA_BYTES, len(image) - offset)
                send_data(bus, args.node_id, seq, image[offset:offset + n])
                offset += n
                seq += 1
            send_end(bus, args.node_id, c ^ 0xFFFFFFFF)
            try:
                wait_status(bus, args.node_id, FW_STATUS_COMPLETE, timeout=5.0)
                print("Unexpected COMPLETE on bad CRC", file=sys.stderr)
                return 2
            except (TimeoutError, RuntimeError):
                print("Bad CRC rejected as expected")
                return 0
        run_ota(bus, args.node_id, image, args.fw_version, do_abort=args.abort)
    finally:
        bus.shutdown()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
