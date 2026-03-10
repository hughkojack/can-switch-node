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
| Node announce| 0x8 | Node → Hub | Heartbeat/announce (node_id, type, input_count) |

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
| 0x02 | CMD_SET_INPUT_CFG | `[0x02, input_index, input_id, mode]` | Per-input config: **input_index 0..5**, **mode 0=momentary 1=toggle**. |
| 0x03 | CMD_SET_INPUT_COUNT | `[0x03, count]` | **Count 1..6** (hub and node aligned). |
| 0x04 | CMD_SET_TIMING | `[0x04, click_max_lo, click_max_hi, dbl_gap_lo, dbl_gap_hi, hold_lo, hold_hi, long_lo, long_hi]` | 4× uint16 little-endian: click_max_ms, double_click_gap_ms, hold_min_ms, long_hold_min_ms. |
| 0x05 | CMD_FIND_ME | `[0x05, duration_min]` | Drive find-me output for **N minutes** (1–30). Byte 1 = duration in minutes. |
| 0x06 | CMD_SET_FIND_ME_OUTPUT | `[0x06, output_index]` | Set which output index to use for find-me (stored in NVS). |
| 0x07 | CMD_SET_INPUT_LABEL | `[0x07, input_index, total_len|0xFF, c0..c5]` | Set display label for one input. Multi-frame: first frame byte 2 = total length (1..24), continuation frames byte 2 = 0xFF; 6 chars per frame. Use total_len=0 to clear. |

## Hub → Node: CAN_MSG_STATE_FEEDBACK (0x4)

- **ID**: `(0x4 << 7) | target_node_id`
- **DLC**: 4
- **Payload**: bytes 0–3 = brightness for button 1–4 (0–100, or 0xFF if no binding). Hub sends when output state changes so LCD nodes can show current level per button.

## Node → Hub: CAN_MSG_NODE_ANNOUNCE (0x8)

- **ID**: `(0x8 << 7) | node_id` (127 when unconfigured).
- **Payload**: `[ node_type, input_count ]`
  - **node_type**: 1 = LCD, 2 = mechanical.
  - **input_count**: **1..6** (hub and node aligned).

## Node types

- **NODE_TYPE_LCD** = 1  
- **NODE_TYPE_MECHANICAL** = 2  

## Find-me output

- **Find Me I/O index** = **ESP32-S3 chip I/O** (GPIO number 0–48). Not related to the number of inputs (1–6).
- Stored in NVS via `CMD_SET_FIND_ME_OUTPUT`. When Find Me runs, the node drives that GPIO (solid on for index 0, blinked for others).
- Choose a GPIO that is free on your board (e.g. not used by CAN, I2C, or display).

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
#define CAN_MSG_NODE_ANNOUNCE 0x8
#define CMD_SET_NODE_ID        0x01
#define CMD_SET_INPUT_CFG     0x02
#define CMD_SET_INPUT_COUNT   0x03
#define CMD_SET_TIMING        0x04
#define CMD_FIND_ME           0x05
#define CMD_SET_FIND_ME_OUTPUT 0x06
#define CMD_SET_INPUT_LABEL   0x07
#define NODE_TYPE_LCD         1
#define NODE_TYPE_MECHANICAL   2
```
