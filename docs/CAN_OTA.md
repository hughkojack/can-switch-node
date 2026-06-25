# CAN OTA (mechanical nodes)

Node firmware is updated from a **laptop** running `can_ota_bench.py`. The hub forwards raw CAN frames only (exclusive tunnel); it does not implement the OTA protocol.

## Remote (hub tunnel)

1. Open the CAN tunnel from the hub UI (**Nodes** tab) or `POST /api/can/tunnel/open`.
2. From your laptop:

```bash
pip install python-can
python tools/can_ota_bench.py --tunnel http://<hub-ip> --node-id 4 path/to/firmware.bin
```

3. Close the tunnel when finished (`POST /api/can/tunnel/close` or UI).

## WS2812 indicators (node_min_c3)

| Phase | All 12 LEDs |
|-------|-------------|
| OTA in progress | Flash **red** (250 ms on/off) |
| Failed / aborted | **Solid red** — single button click returns to night-light baseline |
| Success | **Solid green** — single button click reboots into new firmware, or hub sends `CMD_REBOOT` |

Wire format: TCP port **5250**, 13-byte frames: `0xCA 0xFE` + CAN ID (u16 LE) + DLC + 8 data bytes.

## Local (USB-CAN at panel)

```bash
python tools/can_ota_bench.py --interface slcan --channel COM9 --node-id 4 firmware.bin
```

## Hub firmware

Hub self-OTA remains at `POST /api/ota` (Advanced tab).
