# CAN OTA (mechanical nodes)

Node firmware is updated from a **laptop** running `can_ota_bench.py`. The hub forwards raw CAN frames only (exclusive tunnel); it does not implement the OTA protocol.

## Remote (hub tunnel)

1. Open the CAN tunnel from the hub UI (**Nodes** tab) or `POST /api/can/tunnel/open`.
2. From your laptop:

```bash
pip install python-can
python tools/can_ota_bench.py --tunnel http://<hub-ip> --node-id 4 path/to/firmware.bin --stats
```

Default **block size 16** (four `FLASH_DATA` frames per `FLASH_READY`). Legacy v2 pacing: `--block-size 4`.

3. Close the tunnel when finished (`POST /api/can/tunnel/close` or UI).

## Speed options (`can_ota_bench.py`)

| Flag | Effect |
|------|--------|
| `--block-size 16` (default) | One READY per 16 bytes (~4× fewer round-trips vs v2) |
| `--block-size 32` | One READY per 32 bytes |
| `--block-size 4` | Legacy: one READY per 4 bytes |
| `--no-pipeline` | Disable window-2 send-ahead (only with `--block-size 4`) |
| `--stats` | Print throughput (B/s) and average READY latency |

Node firmware sends `FLASH_READY` directly after each flash write (no poll-task hop) and logs progress every 4 KB.

## WS2812 indicators (node_min_c3)

| Phase | All 12 LEDs |
|-------|-------------|
| OTA in progress | Flash **red** (250 ms on/off) |
| Failed / aborted | **Solid red** — single button click returns to night-light baseline |
| Success | **Solid green** — single button click reboots into new firmware, or hub sends `CMD_REBOOT` |

Wire format: TCP port **5250**, 13-byte frames: `0xCA 0xFE` + CAN ID (u16 LE) + DLC + 8 data bytes.

## Local (USB-CAN at panel)

```bash
python tools/can_ota_bench.py --interface slcan --channel COM9 --node-id 4 firmware.bin --stats
```

## Hub firmware

Hub self-OTA remains at `POST /api/ota` (Advanced tab). Flash updated hub firmware for lower tunnel forwarding latency during OTA.
