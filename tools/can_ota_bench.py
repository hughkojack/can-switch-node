#!/usr/bin/env python3
"""
Bench-test CAN OTA against a mechanical node (node_min_c3) via python-can.

Uses mcp-can-boot-style node-paced protocol (CAN_OTA_REMOTE / CAN_OTA_NODE).

Usage:
  pip install python-can
  python tools/can_ota_bench.py --interface socketcan --channel can0 --node-id 4 firmware.bin
  python tools/can_ota_bench.py --tunnel http://192.168.1.10 --node-id 4 firmware.bin --stats
  python tools/can_ota_bench.py ... --block-size 4 --no-pipeline   # legacy v2 pacing

Requires an OTA-capable node build (partitions_ota.csv, OTA_CAPABLE=1) flashed once via USB.
"""

from __future__ import annotations

import argparse
import struct
import sys
import time
import socket
import urllib.parse
import urllib.request
import json
import zlib
from dataclasses import dataclass, field

try:
    import can
except ImportError:
    print("Install python-can: pip install python-can", file=sys.stderr)
    sys.exit(1)

OTA_FLASH_READY = 0x04
OTA_FLASH_INIT = 0x06
OTA_FLASH_DATA = 0x08
OTA_FLASH_DATA_ERR = 0x0D
OTA_FLASH_DONE = 0x10
OTA_FLASH_COMPLETE = 0x14
OTA_FLASH_ERROR = 0x15
OTA_FLASH_ABORT = 0x18
OTA_DATA_BYTES = 4

CAN_MSG_OTA_REMOTE = 0xE
CAN_MSG_OTA_NODE = 0xF


class OtaDataErr(Exception):
  def __init__(self, expected_offset: int):
    self.expected_offset = expected_offset
    super().__init__(expected_offset)


@dataclass
class OtaStats:
    rounds: int = 0
    bytes_acked: int = 0
    ready_latencies_ms: list[float] = field(default_factory=list)
    t_start: float = 0.0
    t_end: float = 0.0

    def record_ready(self, t0: float, new_offset: int, prev_offset: int) -> None:
        self.rounds += 1
        self.bytes_acked += max(0, new_offset - prev_offset)
        self.ready_latencies_ms.append((time.perf_counter() - t0) * 1000.0)

    def report(self, image_size: int) -> None:
        elapsed = self.t_end - self.t_start
        if elapsed <= 0:
            return
        avg_ms = sum(self.ready_latencies_ms) / len(self.ready_latencies_ms) if self.ready_latencies_ms else 0.0
        bps = self.bytes_acked / elapsed
        print(f"Stats: {self.rounds} READY rounds, {self.bytes_acked} bytes in {elapsed:.1f}s "
              f"({bps:.0f} B/s), avg READY latency {avg_ms:.2f} ms")
        if image_size:
            print(f"       effective {100.0 * self.bytes_acked / image_size:.1f}% of image via measured acks")


def can_id(msg_type: int, node_id: int) -> int:
    return ((msg_type & 0xF) << 7) | (node_id & 0x7F)


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def build_frame(node_id: int, opcode: int, meta: int = 0, tail: bytes = b"\x00\x00\x00\x00") -> bytes:
    if len(tail) != 4:
        raise ValueError("tail must be 4 bytes")
    return bytes([
        (node_id >> 8) & 0xFF,
        node_id & 0xFF,
        opcode,
        meta & 0xFF,
        tail[0], tail[1], tail[2], tail[3],
    ])


def read_offset_be(data: bytes) -> int:
    return struct.unpack(">I", data[4:8])[0]


def send_remote(bus: can.Bus, node_id: int, frame: bytes) -> None:
    bus.send(can.Message(arbitration_id=can_id(CAN_MSG_OTA_REMOTE, node_id), data=frame, is_extended_id=False))


def send_init(bus: can.Bus, node_id: int, size: int, block_bytes: int = 16) -> None:
    meta = 0 if block_bytes == 4 else block_bytes
    send_remote(bus, node_id, build_frame(node_id, OTA_FLASH_INIT, meta, struct.pack(">I", size)))


def send_data(bus: can.Bus, node_id: int, offset: int, chunk: bytes) -> None:
    n = len(chunk)
    meta = ((n & 0x07) << 5) | (offset & 0x1F)
    tail = chunk + b"\x00" * (4 - n)
    send_remote(bus, node_id, build_frame(node_id, OTA_FLASH_DATA, meta, tail))


def _encode_tunnel_wire(msg: can.Message) -> bytes:
    sid = int(msg.arbitration_id) & 0x7FF
    data = bytes(msg.data[:8])
    dlc = len(data)
    return CAN_TUNNEL_MAGIC + sid.to_bytes(2, "little") + bytes([dlc]) + data.ljust(8, b"\x00")


def send_data_range(bus: can.Bus, node_id: int, image: bytes, start: int, end: int) -> None:
    """Send FLASH_DATA frames for [start, end). Tunnel: one TCP write per block burst."""
    wire = bytearray()
    pending: list[can.Message] = []
    pos = start
    while pos < end:
        n = min(OTA_DATA_BYTES, end - pos)
        chunk = image[pos:pos + n]
        meta = ((n & 0x07) << 5) | (pos & 0x1F)
        tail = chunk + b"\x00" * (4 - n)
        frame = build_frame(node_id, OTA_FLASH_DATA, meta, tail)
        msg = can.Message(
            arbitration_id=can_id(CAN_MSG_OTA_REMOTE, node_id),
            data=frame,
            is_extended_id=False,
        )
        if isinstance(bus, TunnelBus):
            wire += _encode_tunnel_wire(msg)
        else:
            pending.append(msg)
        pos += n
    if isinstance(bus, TunnelBus):
        if wire:
            bus.send_wire(bytes(wire))
    else:
        for msg in pending:
            bus.send(msg)


def send_done(bus: can.Bus, node_id: int, crc: int) -> None:
    send_remote(bus, node_id, build_frame(node_id, OTA_FLASH_DONE, 0, struct.pack(">I", crc)))


def send_abort(bus: can.Bus, node_id: int, reason: int = 1) -> None:
    send_remote(bus, node_id, build_frame(node_id, OTA_FLASH_ABORT, 0, bytes([reason, 0, 0, 0])))


def recv_node(bus: can.Bus, node_id: int, timeout: float = 5.0) -> tuple[int, bytes]:
    deadline = time.time() + timeout
    while time.time() < deadline:
        msg = bus.recv(timeout=0.2)
        if msg is None:
            continue
        if msg.arbitration_id != can_id(CAN_MSG_OTA_NODE, node_id):
            continue
        if len(msg.data) < 8:
            continue
        return msg.data[2], bytes(msg.data)
    raise TimeoutError("timeout waiting for node response")


def wait_ready(bus: can.Bus, node_id: int, timeout: float = 5.0) -> int:
    while True:
        opcode, data = recv_node(bus, node_id, timeout)
        if opcode == OTA_FLASH_READY:
            return read_offset_be(data)
        if opcode == OTA_FLASH_DATA_ERR:
            raise OtaDataErr(read_offset_be(data))
        if opcode == OTA_FLASH_ERROR:
            reason = data[4] if len(data) > 4 else 0
            raise RuntimeError(f"node FLASH_ERROR reason={reason}")
        if opcode == OTA_FLASH_COMPLETE:
            raise RuntimeError("unexpected FLASH_COMPLETE")


def wait_ready_advance(
    bus: can.Bus,
    node_id: int,
    chunk_offset: int,
    nbytes: int,
    image_size: int,
    timeout: float = 5.0,
) -> int:
    """Wait until node READY offset reaches chunk_offset + nbytes (drains sub-block READYs)."""
    target = min(chunk_offset + nbytes, image_size)
    deadline = time.time() + timeout
    while time.time() < deadline:
        remaining = deadline - time.time()
        opcode, data = recv_node(bus, node_id, timeout=min(0.25, remaining))
        if opcode == OTA_FLASH_DATA_ERR:
            continue
        if opcode == OTA_FLASH_READY:
            off = read_offset_be(data)
            if off < chunk_offset:
                raise RuntimeError(
                    f"node regressed at block {chunk_offset} (got READY {off})"
                )
            if off > target:
                raise RuntimeError(
                    f"READY ahead at block {chunk_offset}: got {off} expected {target}"
                )
            if off >= target:
                return off
    raise TimeoutError(f"timeout waiting for READY offset {target} (block at {chunk_offset})")


def wait_ready_timed(
    bus: can.Bus,
    node_id: int,
    timeout: float,
    stats: OtaStats | None,
    prev_offset: int,
) -> int:
    t0 = time.perf_counter()
    new_offset = wait_ready(bus, node_id, timeout=timeout)
    if stats is not None:
        stats.record_ready(t0, new_offset, prev_offset)
    return new_offset


def run_ota(
    bus: can.Bus,
    node_id: int,
    image: bytes,
    version: int,
    do_abort: bool = False,
    ready_timeout: float = 5.0,
    block_bytes: int = 16,
    pipeline: bool = True,
    stats: OtaStats | None = None,
) -> None:
    _ = version
    c = crc32(image)
    print(f"Image size={len(image)} crc=0x{c:08X} block={block_bytes} pipeline={pipeline}")
    if stats is not None:
        stats.t_start = time.perf_counter()

    send_init(bus, node_id, len(image), block_bytes=block_bytes)
    offset = wait_ready_timed(bus, node_id, max(ready_timeout, 15.0), stats, 0)
    print(f"Node READY offset={offset}")

    if do_abort:
        send_abort(bus, node_id)
        print("Sent FLASH_ABORT (expect node to return to idle)")
        if stats is not None:
            stats.t_end = time.perf_counter()
            stats.report(len(image))
        return

    while offset < len(image):
        chunk_offset = offset
        t0 = time.perf_counter()

        if block_bytes > OTA_DATA_BYTES:
            block_start = offset
            block_end = min(block_start + block_bytes, len(image))
            block_len = block_end - block_start
            gap_from = block_start
            for attempt in range(5):
                send_data_range(bus, node_id, image, gap_from, block_end)
                try:
                    new_offset = wait_ready_advance(
                        bus, node_id, block_start, block_len, len(image),
                        timeout=ready_timeout,
                    )
                    break
                except TimeoutError:
                    if attempt == 4:
                        raise
                    need = gap_from
                    if attempt >= 1:
                        need = block_start
                    print(f"  timeout at block [{block_start},{block_end}), "
                          f"resending from {need}…")
                    gap_from = need
            else:
                raise RuntimeError(f"failed block at offset {block_start}")
            if stats is not None:
                stats.record_ready(t0, new_offset, block_start)
        else:
            n = min(OTA_DATA_BYTES, len(image) - offset)
            send_data(bus, node_id, chunk_offset, image[chunk_offset:chunk_offset + n])
            if pipeline and chunk_offset + n < len(image):
                n2 = min(OTA_DATA_BYTES, len(image) - chunk_offset - n)
                send_data(bus, node_id, chunk_offset + n, image[chunk_offset + n:chunk_offset + n + n2])
            try:
                new_offset = wait_ready(bus, node_id, timeout=ready_timeout)
            except TimeoutError:
                print(f"  timeout at offset={chunk_offset}, resending DATA once…")
                send_data(bus, node_id, chunk_offset, image[chunk_offset:chunk_offset + n])
                new_offset = wait_ready(bus, node_id, timeout=ready_timeout)
            if stats is not None:
                stats.record_ready(t0, new_offset, chunk_offset)
            while new_offset > chunk_offset + n and offset < len(image):
                prev = new_offset
                t1 = time.perf_counter()
                new_offset = wait_ready(bus, node_id, timeout=ready_timeout)
                if stats is not None:
                    stats.record_ready(t1, new_offset, prev)

        if new_offset < chunk_offset:
            raise RuntimeError(f"node regressed at offset {chunk_offset} (got {new_offset})")
        if new_offset == chunk_offset:
            raise RuntimeError(
                f"stalled at offset {chunk_offset} (no progress after block/segment)"
            )
        offset = new_offset
        if offset % 4096 < max(OTA_DATA_BYTES, block_bytes) or offset >= len(image):
            print(f"  offset={offset} ({100 * offset // len(image)}%)")

    send_done(bus, node_id, c)
    opcode, _ = recv_node(bus, node_id, timeout=15.0)
    if opcode != OTA_FLASH_COMPLETE:
        raise RuntimeError(f"expected FLASH_COMPLETE, got opcode=0x{opcode:02X}")
    print("OTA COMPLETE (reboot node via hub CMD_REBOOT or power cycle to run new image)")
    if stats is not None:
        stats.t_end = time.perf_counter()
        stats.report(len(image))


CAN_TUNNEL_MAGIC = b"\xCA\xFE"
CAN_TUNNEL_FRAME_SIZE = 13
CAN_TUNNEL_TCP_PORT = 5250


class TunnelBus:
    def __init__(self, hub_base: str, port: int = CAN_TUNNEL_TCP_PORT):
        base = hub_base.rstrip("/")
        if not base.startswith("http"):
            base = "http://" + base
        self._hub = base
        self._port = port
        self._sock = None
        self._rx_buf = bytearray()
        self._rx_queue: list[can.Message] = []

    def _http_post(self, path: str) -> dict:
        req = urllib.request.Request(self._hub + path, data=b"", method="POST")
        with urllib.request.urlopen(req, timeout=30) as resp:
            return json.loads(resp.read().decode())

    def _connect_tcp(self) -> None:
        parsed = urllib.parse.urlparse(self._hub)
        host = parsed.hostname or "localhost"
        self._sock = socket.create_connection((host, self._port), timeout=30)
        self._sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

    def __enter__(self):
        self._http_post("/api/can/tunnel/open")
        self._connect_tcp()
        return self

    def __exit__(self, exc_type, exc, tb):
        try:
            if self._sock:
                self._sock.close()
        finally:
            self._sock = None
            try:
                self._http_post("/api/can/tunnel/close")
            except Exception:
                pass

    def send(self, msg):
        self.send_wire(_encode_tunnel_wire(msg))

    def send_wire(self, wire: bytes) -> None:
        if self._sock and wire:
            self._sock.sendall(wire)

    def _fill_rx_queue(self) -> None:
        while True:
            while len(self._rx_buf) >= 2 and self._rx_buf[:2] != CAN_TUNNEL_MAGIC:
                del self._rx_buf[0]
            if len(self._rx_buf) < CAN_TUNNEL_FRAME_SIZE:
                return
            if self._rx_buf[:2] != CAN_TUNNEL_MAGIC:
                continue
            sid = int.from_bytes(self._rx_buf[2:4], "little")
            dlc = min(self._rx_buf[4], 8)
            payload = bytes(self._rx_buf[5:5 + dlc])
            del self._rx_buf[:CAN_TUNNEL_FRAME_SIZE]
            self._rx_queue.append(
                can.Message(arbitration_id=sid, data=payload, is_extended_id=False)
            )

    def recv(self, timeout=None):
        if not self._sock:
            return None
        if self._rx_queue:
            return self._rx_queue.pop(0)
        if timeout is not None:
            self._sock.settimeout(timeout)
        try:
            chunk = self._sock.recv(4096)
        except socket.timeout:
            return None
        if not chunk:
            return None
        self._rx_buf.extend(chunk)
        self._fill_rx_queue()
        if self._rx_queue:
            return self._rx_queue.pop(0)
        return None

    def shutdown(self):
        if self._sock:
            self._sock.close()
            self._sock = None


def main() -> int:
    ap = argparse.ArgumentParser(description="CAN OTA bench script for mechanical nodes")
    ap.add_argument("firmware", help="Path to .bin firmware image")
    ap.add_argument("--node-id", type=int, required=True, help="Target node ID 1..126")
    ap.add_argument("--interface", default="socketcan", help="python-can interface (socketcan, slcan, ...)")
    ap.add_argument("--channel", default="can0", help="CAN channel / COM port")
    ap.add_argument("--tunnel", default=None, help="Hub base URL for CAN tunnel (e.g. http://192.168.1.10)")
    ap.add_argument("--ready-timeout", type=float, default=None,
                    help="Seconds to wait for node READY (default 5, 20 when --tunnel)")
    ap.add_argument("--block-size", type=int, default=16, choices=[4, 16, 32],
                    help="Bytes per READY handshake (default 16; 4 = legacy v2)")
    ap.add_argument("--no-pipeline", action="store_true",
                    help="Disable window-2 send-ahead (only applies when --block-size 4)")
    ap.add_argument("--stats", action="store_true", help="Print throughput and READY latency stats")
    ap.add_argument("--bitrate", type=int, default=500000)
    ap.add_argument("--fw-version", type=lambda x: int(x, 0), default=0x0100)
    ap.add_argument("--abort", action="store_true", help="Abort after READY (timeout test)")
    ap.add_argument("--bad-crc", action="store_true", help="Send wrong CRC on DONE (rollback test)")
    args = ap.parse_args()

    if args.node_id < 1 or args.node_id > 126:
        print("node-id must be 1..126", file=sys.stderr)
        return 1

    with open(args.firmware, "rb") as f:
        image = f.read()
    if not image or image[0] != 0xE9:
        print("Invalid ESP32 image (expected magic 0xE9)", file=sys.stderr)
        return 1

    ready_timeout = args.ready_timeout if args.ready_timeout is not None else (20.0 if args.tunnel else 5.0)
    pipeline = not args.no_pipeline
    stats = OtaStats() if args.stats else None

    def run_with_bus(bus):
        if args.bad_crc:
            c = crc32(image)
            send_init(bus, args.node_id, len(image), block_bytes=args.block_size)
            offset = wait_ready(bus, args.node_id, timeout=ready_timeout)
            while offset < len(image):
                if args.block_size > OTA_DATA_BYTES:
                    block_end = min(offset + args.block_size, len(image))
                    pos = offset
                    while pos < block_end:
                        n = min(OTA_DATA_BYTES, block_end - pos)
                        send_data(bus, args.node_id, pos, image[pos:pos + n])
                        pos += n
                    offset = wait_ready(bus, args.node_id, timeout=ready_timeout)
                else:
                    n = min(OTA_DATA_BYTES, len(image) - offset)
                    send_data(bus, args.node_id, offset, image[offset:offset + n])
                    offset = wait_ready(bus, args.node_id, timeout=ready_timeout)
            send_done(bus, args.node_id, c ^ 0xFFFFFFFF)
            try:
                opcode, _ = recv_node(bus, args.node_id, timeout=10.0)
                if opcode == OTA_FLASH_COMPLETE:
                    print("Unexpected COMPLETE on bad CRC", file=sys.stderr)
                    return 2
            except (TimeoutError, RuntimeError):
                print("Bad CRC rejected as expected")
                return 0
            print("Bad CRC rejected as expected")
            return 0
        run_ota(bus, args.node_id, image, args.fw_version, do_abort=args.abort,
                ready_timeout=ready_timeout, block_bytes=args.block_size,
                pipeline=pipeline, stats=stats)
        return 0

    if args.tunnel:
        with TunnelBus(args.tunnel) as bus:
            return run_with_bus(bus)

    bus = can.interface.Bus(channel=args.channel, interface=args.interface, bitrate=args.bitrate)
    try:
        return run_with_bus(bus)
    finally:
        bus.shutdown()


if __name__ == "__main__":
    raise SystemExit(main())
