#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- CAN Protocol ----
#define CAN_MSG_INPUT_EVENT   0x1
#define CAN_MSG_NODE_CONFIG   0x3   // Hub -> node: configuration
#define CAN_MSG_NODE_ANNOUNCE 0x8   // Node -> hub: heartbeat/announce (reuse HEARTBEAT type)

// Unconfigured node ID (127): hub detects new nodes; valid configured IDs 1..126
#define NODE_ID_UNCONFIGURED  127

// Config sub-commands (payload byte 0 when CAN_MSG_NODE_CONFIG)
#define CMD_SET_NODE_ID        0x01
#define CMD_SET_INPUT_CFG     0x02  // input_index, input_id, mode (momentary/toggle)
#define CMD_SET_INPUT_COUNT   0x03  // count 1..6
#define CMD_SET_TIMING        0x04  // optional: click_max, dbl_gap, hold_min, long_hold (e.g. 4x uint16_t)
#define CMD_FIND_ME           0x05  // optional duration byte: drive find-me output for N seconds
#define CMD_SET_FIND_ME_OUTPUT 0x06 // output_index -> store in NVS

// Node types for announce (payload byte 0)
#define NODE_TYPE_LCD        1
#define NODE_TYPE_MECHANICAL 2

// Event codes must match HUB firmware CanInputEventCode
typedef enum {
  EVT_CLICK        = 0x01,
  EVT_HOLD         = 0x02,
  EVT_DOUBLE_CLICK = 0x03,
  EVT_TRIPLE_CLICK = 0x04,
  EVT_LONG_HOLD    = 0x05,
  EVT_HOLD_REPEAT  = 0x06,
} can_input_event_code_t;

// Build 11-bit ID: (msgType<<7) | (nodeId&0x7F)
uint16_t can_id(uint8_t msg_type, uint8_t node_id);

// Init TWAI (pins + bitrate)
//bool can_init_twai(int tx_gpio, int rx_gpio, int bitrate_kbps);

// Send events (codes match HUB CanInputEventCode)
bool can_send_click(uint8_t node_id, uint8_t input_id);
bool can_send_double_click(uint8_t node_id, uint8_t input_id);
bool can_send_triple_click(uint8_t node_id, uint8_t input_id);
bool can_send_hold(uint8_t node_id, uint8_t input_id);
bool can_send_long_hold(uint8_t node_id, uint8_t input_id);
bool can_send_hold_repeat(uint8_t node_id, uint8_t input_id);

// Node -> hub: announce/heartbeat (node_id, type, input_count) so hub can detect new nodes
bool can_send_node_announce(uint8_t node_id, uint8_t node_type, uint8_t input_count);

#ifdef __cplusplus
}
#endif