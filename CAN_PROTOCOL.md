# CAN Protocol (Node ↔ Hub)

This document describes the CAN frame layout and commands so hub firmware (and web UI) can interoperate with switch nodes (LCD and mechanical).

## Identifier

- **11-bit CAN ID**: `(msg_type << 7) | (node_id & 0x7F)`
- **node_id**: 1..126 = configured node; **127 = unconfigured** (new node, not yet assigned by hub).

## Message types

| Type | Value | Direction   | Description |
|------|--------|------------|-------------|
| Input event | 0x1 | Node → Hub | Button/input events (see `can.h` event codes) |
| Node config  | 0x3 | Hub → Node | Configuration commands (see below) |
| State feedback | 0x4 | Hub → Node | Per-button brightness for LCD (4 bytes 0–100 or 0xFF) |
| Firmware | 0xE | Hub → Node | OTA remote commands (`CAN_OTA_REMOTE`) |
| OTA node | 0xF | Node → Hub | OTA responses (`CAN_OTA_NODE`; wins arbitration) |
| Node announce| 0x8 | Node → Hub | Heartbeat/announce (node_id, type, input_count, fw version) |

## Unconfigured node (127)

- On first boot (no NVS), node uses **node_id = 127**.
- Hub treats traffic from 127 as a new node; it can assign a real ID by sending `CMD_SET_NODE_ID` to 127.
- Valid configured IDs: 1..126.

## Hub → Node: CAN_MSG_NODE_CONFIG (0x3)

- **ID**: `(0x3 << 7) | target_node_id` (target = 127 for unconfigured, or current node id).
- **Payload**: `[ cmd_byte, ... ]`. Commands:

| Cmd | Name | Payload | Description |
|-----|------|---------|-------------|
| 0x01 | CMD_SET_NODE_ID | `[0x01, new_id]` | Set node ID (1..126). Node saves to NVS and re-inits. |
| 0x02 | CMD_SET_INPUT_CFG | `[0x02, input_index, input_id, mode, gpio?, active_high?]` | DLC 4 legacy (to mode); DLC 5 adds **gpio** (0..48 or 0xFF); DLC 6 adds **active_high** (0=active low, 1=active high). If DLC < 6, node keeps active low. |
| 0x03 | CMD_SET_INPUT_COUNT | `[0x03, count]` | **Count 1..6** (hub and node aligned). |
| 0x04 | CMD_SET_TIMING | Two frames (6 bytes each). **Frame 1**: `[0x04, 0, c_lo, c_hi, g_lo, g_hi]` = part 0, click_max_ms, double_click_gap_ms. **Frame 2**: `[0x04, 1, h_lo, h_hi, l_lo, l_hi]` = part 1, hold_min_ms, long_hold_min_ms. All uint16 LE, 0–65535 ms. |
| 0x05 | CMD_FIND_ME | `[0x05, duration_min]` | Drive find-me for **N minutes** (1–30). **Mechanical WS2812** (`node_min_c3`): blinks all 12 LEDs amber on the strip. **Other nodes**: GPIO output from `CMD_SET_FIND_ME_OUTPUT` (solid for index 0, blink for others). |
| 0x06 | CMD_SET_FIND_ME_OUTPUT | `[0x06, output_index]` | Set find-me GPIO index (stored in NVS). **Ignored on WS2812 mechanical builds** — find-me always uses the strip. |
| 0x0A | CMD_SET_CAN_LINK_INDICATOR | `[0x0A, gpio]` | CAN link indicator GPIO (mechanical): 0–48 = LED solid=good link, flash=bad/no link; 0xFF=disable. |
| 0x0C | CMD_SET_NIGHT_LIGHT | `[0x0C, enabled, brightness]` | Mechanical WS2812 night light: enabled 0=off 1=on, brightness 0–100. Stored in node NVS. Applied on the strip via the dedicated `ws2812` FreeRTOS task. |
| 0x0D | CMD_SET_WS2812_CLICK_EFFECT | `[0x0D, effect]` | Click/double-click LED feedback: 0=strobe, 1=chase (1→12→1 on button press). Stored in node NVS only — **does not** animate on this command. |
| 0x0E | CMD_SET_WS2812_EFFECT_PARAMS | `[0x0E, effect_id, r, g, b, timing_lo, timing_hi]` | Per-effect RGB + speed (NVS only). `effect_id`: 0=strobe, 1=chase, 2=find_me. `timing_ms` uint16 LE: strobe half-period, chase step, find-me blink toggle (10–2000 ms). |
| 0x07 | CMD_SET_INPUT_LABEL | `[0x07, input_index, total_len|0xFF, c0..c5]` | Set display label for one input. Multi-frame: first frame byte 2 = total length (1..24), continuation frames byte 2 = 0xFF; 6 chars per frame. Use total_len=0 to clear. |

## Hub → Node: CAN_MSG_STATE_FEEDBACK (0x4)

- **ID**: `(0x4 << 7) | target_node_id`
- **DLC**: 4
- **Payload**: bytes 0–3 = brightness for button 1–4 (0–100, or 0xFF if no binding). Hub sends when output state changes so LCD nodes can show current level per button.

## Node → Hub: CAN_MSG_NODE_ANNOUNCE (0x8)

- **ID**: `(0x8 << 7) | node_id` (127 when unconfigured).
- **Payload** (DLC ≥ 2):
  - Byte 0: **node_type** — 1 = LCD, 2 = mechanical.
  - Byte 1: **input_count** — 1..6.
  - Bytes 2–3 (DLC ≥ 5): **fw_version** uint16 LE (e.g. `0x0100` = v1.0).
  - Byte 4 (DLC ≥ 5): **ota_capable** — `1` if node accepts CAN OTA.

## CAN OTA: CAN_MSG_OTA_REMOTE (0xE) / CAN_MSG_OTA_NODE (0xF)

Node-paced OTA for mechanical nodes (`node_min_c3`). **Full guide:** [docs/CAN_OTA.md](docs/CAN_OTA.md). Bench script: [tools/can_ota_bench.py](tools/can_ota_bench.py).

**8-byte payload** (both directions): `[node_id u16 BE][opcode][meta][data or offset u32 BE]`

| Opcode | Value | Direction | Purpose |
|--------|-------|-----------|---------|
| `OTA_FLASH_READY` | `0x04` | Node → hub | Ready; bytes 4–7 = next write offset u32 BE |
| `OTA_FLASH_INIT` | `0x06` | Hub → node | Start; bytes 4–7 = image size u32 BE |
| `OTA_FLASH_DATA` | `0x08` | Hub → node | 1–4 bytes; byte 3 = `(len<<5)\|(offset&0x1F)` |
| `OTA_FLASH_DATA_ERR` | `0x0D` | Node → hub | Offset mismatch; bytes 4–7 = expected offset |
| `OTA_FLASH_DONE` | `0x10` | Hub → node | End; bytes 4–7 = CRC32 BE |
| `OTA_FLASH_COMPLETE` | `0x14` | Node → hub | Success |
| `OTA_FLASH_ERROR` | `0x15` | Node → hub | Failure; byte 4 = reason |
| `OTA_FLASH_ABORT` | `0x18` | Either | Cancel |

**v2 strict stop-and-wait:** `OTA_SEGMENT_BYTES` = 4 per CAN frame (max payload in bytes 4–7).

**v3 block transfer (faster):** `FLASH_INIT` byte 3 (`meta`) = block size per `FLASH_READY`: `0` → legacy 4 bytes; `16` or `32` → host sends that many bytes as consecutive `FLASH_DATA` frames (4 bytes each), node ACKs once. Bench default: `--block-size 16`.

Hub sends one or more `FLASH_DATA` frames, then waits for `FLASH_READY` with **exact** offset `previous + len`. Node sends `READY` only after `esp_ota_write` completes; legacy mode may pipeline one extra segment (window 2). Offset mismatch → node sends `FLASH_DATA_ERR` with expected offset; hub resends from that offset. `READY` ahead of expected offset → hub aborts (desync).

## Node types

- **NODE_TYPE_LCD** = 1  
- **NODE_TYPE_MECHANICAL** = 2  

## Find-me output

- **Mechanical WS2812 nodes** (`node_min_c3`, `WS2812_ENABLE`): `CMD_FIND_ME` blinks the **12× WS2812 strip** on GPIO 7 (D5) — all LEDs amber, 150 ms toggle. No GPIO config needed; `CMD_SET_FIND_ME_OUTPUT` is ignored. When find-me ends, night-light baseline is restored.
- **Other nodes (LCD, etc.)**: **Find Me I/O index** = **chip GPIO number** (stored via `CMD_SET_FIND_ME_OUTPUT`). When Find Me runs, the node drives that GPIO (solid on for index 0, blinked for others).
- The hub UI may allow GPIO **0–48** on non-mechanical nodes; **valid pins depend on the node board**.

## Node → Hub: Input event (CAN_MSG_INPUT_EVENT = 0x1)

- **ID**: `(0x1 << 7) | node_id`
- **Payload**: `[ button_id, event_code ]` (DLC ≥ 2), or for **EVT_DIM**: `[ button_id, EVT_DIM, brightness_0_100 ]` (DLC ≥ 3).
- **Event codes**: CLICK=0x01, HOLD=0x02, DOUBLE_CLICK=0x03, TRIPLE_CLICK=0x04, LONG_HOLD=0x05, HOLD_REPEAT=0x06, **EVT_DIM=0x07** (byte 2 = brightness 0..100; hub uses same binding as CLICK for that button).

## Constants (match `can.h`)

```c
#define NODE_ID_UNCONFIGURED  127
#define CAN_MSG_INPUT_EVENT   0x1
#define CAN_MSG_NODE_CONFIG   0x3
#define CAN_MSG_STATE_FEEDBACK 0x4
#define CAN_MSG_OTA_REMOTE    0xE
#define CAN_MSG_OTA_NODE      0xF
#define CAN_MSG_NODE_ANNOUNCE 0x8
#define CMD_SET_NODE_ID        0x01
#define CMD_SET_INPUT_CFG     0x02
#define CMD_SET_INPUT_COUNT   0x03
#define CMD_SET_TIMING        0x04
#define CMD_FIND_ME           0x05
#define CMD_SET_FIND_ME_OUTPUT 0x06
#define CMD_SET_INPUT_LABEL   0x07
#define CMD_REBOOT            0x09
#define NODE_TYPE_LCD         1
#define NODE_TYPE_MECHANICAL   2
#define CMD_SET_NIGHT_LIGHT       0x0C
#define CMD_SET_WS2812_CLICK_EFFECT 0x0D
#define CMD_SET_WS2812_EFFECT_PARAMS 0x0E
```

## WS2812 mechanical strip (`node_min_c3`)

- **12 LEDs**, data pin **GPIO 7 (D5)**. CAN MCP2515 CS on **GPIO 20** — do not confuse with the LED pin.
- **GPIO 7 reserved** — not valid for input GPIO or CAN-link indicator. **Find Me** uses the WS2812 strip (not GPIO bit-bang).
- Hub UI: **WS2812** button per mechanical node; **Configure** → Find Me (duration only) or **WS2812** modal → Find Me.
- Full details: [docs/WS2812_MECHANICAL_NODE.md](docs/WS2812_MECHANICAL_NODE.md)
