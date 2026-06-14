# WS2812 strip — mechanical node (`node_min_c3`)

This project drives a **12× WS2812B** on **GPIO 7 (D5)** for mechanical nodes built with env **`node_min_c3`**.

Full protocol, hub UI, and API details: **`LED-Controller/docs/WS2812_MECHANICAL_NODE.md`** (hub repo, same workspace).

---

## Quick reference

| Build flag | Default |
|------------|---------|
| `WS2812_ENABLE` | `1` in `node_min_c3` |
| `WS2812_GPIO` | 7 |
| `WS2812_COUNT` | 12 |
| `CAN_CS_GPIO` | 20 (MCP2515 CS on D7) |

**Pin conflict:** On the Seeed XIAO CAN expansion board, **MCP2515 INT is wired to D6 (GPIO 21)**. Do **not** use GPIO 21 for WS2812 data. **GPIO 7 (D5)** is reserved for the strip — not valid for hub-assigned inputs or CAN-link LED.

**Find Me** drives the WS2812 strip (not a separate GPIO).

---

## Hardware (required for reliability)

| Item | Requirement |
|------|-------------|
| LED data | **GPIO 7 (D5)** through a **3.3 V → 5 V level shifter** to the strip DIN |
| Strip power | **5 V** adequate for 12 LEDs; **common GND** with the MCU |
| Decoupling | **100–1000 µF** across 5 V/GND at the first LED recommended |

Direct 3.3 V data into a 5 V strip is marginal and often causes intermittent behaviour.

---

## Firmware architecture

| File | Role |
|------|------|
| `src/common/ws2812/ws2812_hal.cpp` | ESP-IDF RMT TX + led_strip encoder; init once, blocking refresh |
| `src/common/ws2812/ws2812_effects.cpp` | Effect timing helpers, night RGB scaling |
| `src/common/ws2812/ws2812_task.cpp` | Command queue, state machine, FreeRTOS task |
| `src/common/ws2812/ws2812_types.h` | Types, defaults, command enums |
| `src/common/ws2812_strip.h` | `ws2812_post_*` / `ws2812_request_*` public API |
| `src/main_idf.cpp` | ESP-IDF entry; CAN → `ws2812_post_*`; boot calls `ws2812_start_task()` |

- **Single owner:** only the `ws2812` FreeRTOS task calls RMT (`rmt_transmit` + `rmt_tx_wait_all_done`).
- **Commands:** CAN/input/OTA call `ws2812_post_*` (non-blocking queue).
- **Baseline:** LEDs hold last frame until night light, click effect, or find-me updates them.

---

## CAN (from hub)

| Cmd | Payload | Node action |
|-----|---------|-------------|
| `0x05` | duration_min (1–30) | **Find Me** — blink configured find-me RGB |
| `0x0C` | enabled, brightness 0–100 | Night light (queued; min 8% if on @ 0%) |
| `0x0D` | effect 0=strobe, 1=chase | NVS only until switch press |
| `0x0E` | effect_id, r, g, b, timing_ms u16 LE | NVS only; applies on next click or find-me |

**Defaults** (empty NVS): strobe 220/180/80 @ 45 ms; chase 80/60/24 @ 50 ms; find-me 180/120/20 @ 150 ms.

---

## Serial debug

At boot (inside ws2812 task):

```
I (...) ws2812: driver init 12 LEDs GPIO 7 RMT
```

After night light / find-me:

```
I (...) ws2812: applied night light on brightness=40
I (...) ws2812: find-me 1 min
```

---

## Flash

```bash
pio run -e node_min_c3 -t upload --upload-port COMx
```

Also see [CAN_PROTOCOL.md](../CAN_PROTOCOL.md) in this repo.
